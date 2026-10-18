#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "AllocationCounter.h"
#include "engine/AudioEngine.h"
#include "engine/RealtimeGuard.h"

using Catch::Approx;
using djmix::AudioEngine;
using djmix::CuePlayer;
using djmix::DecodedAudio;

//==============================================================================

namespace
{
constexpr double rate = 44100.0;
constexpr int block = 256;
const int fadeSamples = static_cast<int> (CuePlayer::fadeSeconds * rate);

std::shared_ptr<DecodedAudio> constant (float left, float right, double seconds, double sampleRate = rate)
{
    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = sampleRate;
    const int n = static_cast<int> (seconds * sampleRate);
    audio->samples.setSize (2, n);
    for (int i = 0; i < n; ++i)
    {
        audio->samples.setSample (0, i, left);
        audio->samples.setSample (1, i, right);
    }
    return audio;
}

// Runs the callback for `numSamples` and returns each output channel.
std::vector<std::vector<float>> render (AudioEngine& engine, int numOutputs, int numSamples)
{
    std::vector<std::vector<float>> out (static_cast<size_t> (numOutputs), std::vector<float> (static_cast<size_t> (numSamples)));
    std::vector<float*> pointers (static_cast<size_t> (numOutputs));
    std::vector<float> chunk (static_cast<size_t> (numOutputs * block));
    for (int done = 0; done < numSamples; done += block)
    {
        const int n = std::min (block, numSamples - done);
        for (int ch = 0; ch < numOutputs; ++ch)
            pointers[static_cast<size_t> (ch)] = chunk.data() + ch * block;
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, pointers.data(), numOutputs, n, {});
        for (int ch = 0; ch < numOutputs; ++ch)
            std::copy (pointers[static_cast<size_t> (ch)], pointers[static_cast<size_t> (ch)] + n,
                       out[static_cast<size_t> (ch)].begin() + done);
    }
    return out;
}

float maxAbs (const std::vector<float>& v)
{
    float m = 0.0f;
    for (float x : v)
        m = std::max (m, std::abs (x));
    return m;
}

// A click is a jump of about the signal level; a fade moves a little each sample.
// The interpolator adds a short transient as a sound starts, so allow 2% of the
// level per sample (the ideal 5 ms fade moves 0.45%).
constexpr float clickFree = 0.02f;

float maxStep (const std::vector<float>& v)
{
    float m = 0.0f;
    for (size_t i = 1; i < v.size(); ++i)
        m = std::max (m, std::abs (v[i] - v[i - 1]));
    return m;
}
} // namespace

TEST_CASE ("Cue plays on the Cue pair only, faded in", "[audio][cue]")
{
    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b1111, block);
    player.setGain (0.5f);
    REQUIRE (player.play (constant (0.8f, -0.4f, 1.0), 0.0, 0.0, 7));

    const auto out = render (engine, 4, 4096);
    CHECK (maxAbs (out[0]) == 0.0f); // Master untouched
    CHECK (maxAbs (out[1]) == 0.0f);
    CHECK (out[2][4000] == Approx (0.4f).margin (1e-4));
    CHECK (out[3][4000] == Approx (-0.2f).margin (1e-4));

    // A fade, not a step, at the start.
    CHECK (std::abs (out[2][0]) < 0.01f);
    CHECK (maxStep (out[2]) < clickFree * 0.4f);

    const auto state = player.state();
    CHECK (state.playing);
    CHECK (state.tag == 7);
    CHECK (state.position == Approx (4096 / rate).margin (0.001));
}

TEST_CASE ("Cue stops at the end of the range, and on stop, without a click", "[audio][cue]")
{
    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b1111, block);
    REQUIRE (player.play (constant (0.5f, 0.5f, 1.0), 0.1, 0.2, 1));

    auto out = render (engine, 4, static_cast<int> (0.05 * rate));
    CHECK (player.state().playing);
    CHECK (maxStep (out[2]) < clickFree * 0.5f);
    CHECK (out[2].back() == Approx (0.5f).margin (1e-4));

    out = render (engine, 4, static_cast<int> (0.1 * rate));
    CHECK_FALSE (player.state().playing);
    CHECK (out[2].back() == 0.0f);
    CHECK (maxStep (out[2]) < clickFree * 0.5f);

    // Stopping part-way fades out within the fade time.
    REQUIRE (player.play (constant (0.5f, 0.5f, 1.0), 0.0, 0.0, 2));
    render (engine, 4, 2048);
    player.stop();
    out = render (engine, 4, 2048);
    CHECK_FALSE (player.state().playing);
    CHECK (maxStep (out[2]) < clickFree * 0.5f);
    CHECK (maxAbs (std::vector<float> (out[2].begin() + fadeSamples + 8, out[2].end())) == 0.0f);
}

TEST_CASE ("Switching auditions crossfades", "[audio][cue]")
{
    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b1111, block);
    REQUIRE (player.play (constant (0.5f, 0.5f, 1.0), 0.0, 0.0, 1));
    auto first = render (engine, 4, 2048);
    REQUIRE (player.play (constant (-0.5f, -0.5f, 1.0), 0.0, 0.0, 2));
    auto second = render (engine, 4, 2048);

    CHECK (second[2].back() == Approx (-0.5f).margin (1e-4));
    CHECK (player.state().tag == 2);
    std::vector<float> joined (first[2]);
    joined.insert (joined.end(), second[2].begin(), second[2].end());
    CHECK (maxStep (joined) < clickFree * 1.0f);
}

TEST_CASE ("A looping cue repeats its stretch on time, without a seam", "[audio][cue]")
{
    // Silence with a click on the stretch's first beat (0.1 s), so each pass
    // shows up as one peak in the output.
    auto audio = constant (0.0f, 0.0f, 2.0);
    const int beat = static_cast<int> (0.1 * rate);
    const int length = static_cast<int> (0.1 * rate);
    audio->samples.setSample (0, beat, 1.0f);

    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b1111, block);
    REQUIRE (player.play (audio, 0.1, 0.2, 1, djmix::CueMode::loop));
    const auto out = render (engine, 4, static_cast<int> (1.0 * rate));
    CHECK (player.state().playing);

    std::vector<int> peaks;
    for (int i = 1; i + 1 < static_cast<int> (out[2].size()); ++i)
        if (out[2][static_cast<size_t> (i)] > 0.5f && out[2][static_cast<size_t> (i)] >= out[2][static_cast<size_t> (i - 1)]
            && out[2][static_cast<size_t> (i)] >= out[2][static_cast<size_t> (i + 1)])
            peaks.push_back (i);

    // Ten passes in a second, each a stretch apart, each click at full level
    // (the fade-in is over before the first beat, the first pass included).
    REQUIRE (peaks.size() == 10);
    for (size_t i = 1; i < peaks.size(); ++i)
        CHECK (peaks[i] - peaks[i - 1] == length);
    CHECK (peaks.front() <= fadeSamples + 4);

    // A steady sound loops with no dip or step at the seam.
    auto steady = constant (0.5f, 0.5f, 2.0);
    REQUIRE (player.play (steady, 0.1, 0.2, 2, djmix::CueMode::loop));
    render (engine, 4, 2048);
    const auto looped = render (engine, 4, static_cast<int> (0.5 * rate));
    CHECK (*std::min_element (looped[2].begin(), looped[2].end()) == Approx (0.5f).margin (0.01));
    CHECK (*std::max_element (looped[2].begin(), looped[2].end()) == Approx (0.5f).margin (0.01));
    const auto position = player.state().position;
    CHECK (position >= 0.095);
    CHECK (position < 0.2);
}

TEST_CASE ("A cue set to continue plays on past its stretch", "[audio][cue]")
{
    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b1111, block);
    REQUIRE (player.play (constant (0.5f, 0.5f, 1.0), 0.1, 0.2, 1, djmix::CueMode::continueTrack));
    const auto out = render (engine, 4, static_cast<int> (0.3 * rate));
    CHECK (player.state().playing);
    CHECK (player.state().position == Approx (0.4).margin (0.01));
    CHECK (out[2].back() == Approx (0.5f).margin (1e-4));

    // To the end of the track, then it stops.
    render (engine, 4, static_cast<int> (0.7 * rate));
    CHECK_FALSE (player.state().playing);
}

TEST_CASE ("Changing the cue mode changes the cue playing", "[audio][cue]")
{
    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b1111, block);
    REQUIRE (player.play (constant (0.5f, 0.5f, 2.0), 0.1, 0.2, 1, djmix::CueMode::once));
    render (engine, 4, static_cast<int> (0.05 * rate));

    // Once -> loop before the end: it keeps going round.
    player.setMode (djmix::CueMode::loop);
    render (engine, 4, static_cast<int> (0.5 * rate));
    CHECK (player.state().playing);
    CHECK (player.state().position < 0.2);

    // Loop -> continue: it carries on past the stretch.
    player.setMode (djmix::CueMode::continueTrack);
    render (engine, 4, static_cast<int> (0.2 * rate));
    CHECK (player.state().playing);
    CHECK (player.state().position > 0.2);

    // Continue -> once, already past the stretch: it fades out where it is.
    player.setMode (djmix::CueMode::once);
    const auto out = render (engine, 4, 4096);
    CHECK_FALSE (player.state().playing);
    CHECK (maxStep (out[2]) < clickFree * 0.5f);
    CHECK (out[2].back() == 0.0f);

    // Continue -> loop past the stretch: back to its start.
    REQUIRE (player.play (constant (0.5f, 0.5f, 2.0), 0.1, 0.2, 2, djmix::CueMode::continueTrack));
    render (engine, 4, static_cast<int> (0.3 * rate));
    player.setMode (djmix::CueMode::loop);
    const auto back = render (engine, 4, 2048);
    CHECK (player.state().playing);
    CHECK (player.state().position < 0.2);
    CHECK (maxStep (back[2]) < clickFree * 0.5f);
}

TEST_CASE ("Split cue sums Cue to mono on the left", "[audio][cue]")
{
    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b11, block); // a stereo device
    REQUIRE (player.play (constant (0.6f, 0.2f, 1.0), 0.0, 0.0, 1));
    const auto out = render (engine, 2, 4096);
    CHECK (out[0][4000] == Approx (0.4f).margin (1e-4));
    CHECK (maxAbs (out[1]) == 0.0f); // the Master side
}

TEST_CASE ("Cue resamples to the device rate", "[audio][cue]")
{
    // 1 kHz at 44.1 kHz, played on a 48 kHz device, is still 1 kHz.
    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = 44100.0;
    audio->samples.setSize (2, 44100 * 2);
    for (int i = 0; i < audio->samples.getNumSamples(); ++i)
    {
        const auto v = static_cast<float> (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 44100.0));
        audio->samples.setSample (0, i, v);
        audio->samples.setSample (1, i, v);
    }

    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (48000.0, 0b1111, block);
    REQUIRE (player.play (audio, 0.0, 0.0, 1));
    const auto out = render (engine, 4, 48000);

    int crossings = 0;
    for (size_t i = 1; i < out[2].size(); ++i)
        if ((out[2][i - 1] < 0.0f) != (out[2][i] < 0.0f))
            ++crossings;
    CHECK (crossings == Approx (2000).margin (4));
    CHECK (player.state().position == Approx (1.0).margin (0.01));
}

TEST_CASE ("The audio callback never allocates while cueing", "[audio][cue][realtime]")
{
    if (! djmix::test::allocationsAreCounted)
        SKIP ("allocations are not counted under a sanitizer");

    AudioEngine engine;
    auto& player = engine.cue();
    engine.prepare (rate, 0b1111, block);
    auto a = constant (0.5f, 0.5f, 1.0);
    auto b = constant (0.3f, 0.3f, 1.0, 48000.0);

    djmix::test::resetRealtimeAllocations();
    render (engine, 4, 1024); // idle
    player.play (a, 0.0, 0.5, 1);
    render (engine, 4, 4096);
    player.play (b, 0.2, 0.0, 2); // switch, resampling
    render (engine, 4, 4096);
    player.play (a, 0.1, 0.15, 3, djmix::CueMode::loop); // several loops
    render (engine, 4, 16384);
    player.setMode (djmix::CueMode::continueTrack);
    render (engine, 4, 4096);
    player.stop();
    render (engine, 4, 4096);
    CHECK (djmix::test::realtimeAllocations() == 0);

    // The check itself works: an allocation on a real-time thread is counted.
    // (Called directly, as the compiler may leave out a new-expression whose
    // result is never used.)
    {
        djmix::rt::ScopedRealtimeThread realtime;
        void* p = ::operator new (64);
        ::operator delete (p);
    }
    CHECK (djmix::test::realtimeAllocations() > 0);
}

TEST_CASE ("Decoded audio is freed only once the audio thread is done with it", "[audio][cue]")
{
    AudioEngine engine;
    auto& player = engine.cue();
    CHECK_FALSE (player.play (constant (0.5f, 0.5f, 1.0), 0.0, 0.0, 1)); // no device yet

    engine.prepare (rate, 0b1111, block);
    auto audio = constant (0.5f, 0.5f, 1.0);
    std::weak_ptr<const DecodedAudio> weak = audio;
    REQUIRE (player.play (std::move (audio), 0.0, 0.0, 1));

    player.releaseUnused(); // queued, not yet read
    CHECK_FALSE (weak.expired());
    render (engine, 4, 1024);
    player.releaseUnused(); // playing
    CHECK_FALSE (weak.expired());

    player.stop();
    render (engine, 4, 4096);
    player.releaseUnused(); // finished
    CHECK (weak.expired());

    // Stopping the device frees everything.
    auto another = constant (0.5f, 0.5f, 1.0);
    weak = another;
    REQUIRE (player.play (std::move (another), 0.0, 0.0, 2));
    render (engine, 4, 1024);
    engine.audioDeviceStopped();
    player.releaseUnused();
    CHECK (weak.expired());
}
