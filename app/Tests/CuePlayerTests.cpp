#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

#include "engine/CuePlayer.h"
#include "engine/RealtimeGuard.h"

using Catch::Approx;
using djmix::CuePlayer;
using djmix::DecodedAudio;

//==============================================================================
// Every allocation in this test program goes through here, so an allocation
// on a thread marked real-time (the audio callback) is reported. Sanitizers
// replace operator new themselves, so under them the check is left out.

#if defined(__SANITIZE_ADDRESS__)
    #define DJMIX_COUNT_ALLOCATIONS 0
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define DJMIX_COUNT_ALLOCATIONS 0
    #endif
#endif
#ifndef DJMIX_COUNT_ALLOCATIONS
    #define DJMIX_COUNT_ALLOCATIONS 1
#endif

namespace
{
std::atomic<int> realtimeAllocations { 0 };
}

#if DJMIX_COUNT_ALLOCATIONS
namespace
{

void* allocate (std::size_t size)
{
    if (djmix::rt::isRealtimeThread())
        ++realtimeAllocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
#endif

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
std::vector<std::vector<float>> render (CuePlayer& player, int numOutputs, int numSamples)
{
    std::vector<std::vector<float>> out (static_cast<size_t> (numOutputs), std::vector<float> (static_cast<size_t> (numSamples)));
    std::vector<float*> pointers (static_cast<size_t> (numOutputs));
    std::vector<float> chunk (static_cast<size_t> (numOutputs * block));
    for (int done = 0; done < numSamples; done += block)
    {
        const int n = std::min (block, numSamples - done);
        for (int ch = 0; ch < numOutputs; ++ch)
            pointers[static_cast<size_t> (ch)] = chunk.data() + ch * block;
        player.audioDeviceIOCallbackWithContext (nullptr, 0, pointers.data(), numOutputs, n, {});
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
    CuePlayer player;
    player.prepare (rate, 0b1111, block);
    player.setGain (0.5f);
    REQUIRE (player.play (constant (0.8f, -0.4f, 1.0), 0.0, 0.0, 7));

    const auto out = render (player, 4, 4096);
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
    CuePlayer player;
    player.prepare (rate, 0b1111, block);
    REQUIRE (player.play (constant (0.5f, 0.5f, 1.0), 0.1, 0.2, 1));

    auto out = render (player, 4, static_cast<int> (0.05 * rate));
    CHECK (player.state().playing);
    CHECK (maxStep (out[2]) < clickFree * 0.5f);
    CHECK (out[2].back() == Approx (0.5f).margin (1e-4));

    out = render (player, 4, static_cast<int> (0.1 * rate));
    CHECK_FALSE (player.state().playing);
    CHECK (out[2].back() == 0.0f);
    CHECK (maxStep (out[2]) < clickFree * 0.5f);

    // Stopping part-way fades out within the fade time.
    REQUIRE (player.play (constant (0.5f, 0.5f, 1.0), 0.0, 0.0, 2));
    render (player, 4, 2048);
    player.stop();
    out = render (player, 4, 2048);
    CHECK_FALSE (player.state().playing);
    CHECK (maxStep (out[2]) < clickFree * 0.5f);
    CHECK (maxAbs (std::vector<float> (out[2].begin() + fadeSamples + 8, out[2].end())) == 0.0f);
}

TEST_CASE ("Switching auditions crossfades", "[audio][cue]")
{
    CuePlayer player;
    player.prepare (rate, 0b1111, block);
    REQUIRE (player.play (constant (0.5f, 0.5f, 1.0), 0.0, 0.0, 1));
    auto first = render (player, 4, 2048);
    REQUIRE (player.play (constant (-0.5f, -0.5f, 1.0), 0.0, 0.0, 2));
    auto second = render (player, 4, 2048);

    CHECK (second[2].back() == Approx (-0.5f).margin (1e-4));
    CHECK (player.state().tag == 2);
    std::vector<float> joined (first[2]);
    joined.insert (joined.end(), second[2].begin(), second[2].end());
    CHECK (maxStep (joined) < clickFree * 1.0f);
}

TEST_CASE ("Split cue sums Cue to mono on the left", "[audio][cue]")
{
    CuePlayer player;
    player.prepare (rate, 0b11, block); // a stereo device
    REQUIRE (player.play (constant (0.6f, 0.2f, 1.0), 0.0, 0.0, 1));
    const auto out = render (player, 2, 4096);
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

    CuePlayer player;
    player.prepare (48000.0, 0b1111, block);
    REQUIRE (player.play (audio, 0.0, 0.0, 1));
    const auto out = render (player, 4, 48000);

    int crossings = 0;
    for (size_t i = 1; i < out[2].size(); ++i)
        if ((out[2][i - 1] < 0.0f) != (out[2][i] < 0.0f))
            ++crossings;
    CHECK (crossings == Approx (2000).margin (4));
    CHECK (player.state().position == Approx (1.0).margin (0.01));
}

TEST_CASE ("The Cue callback never allocates", "[audio][cue][realtime]")
{
    if (! DJMIX_COUNT_ALLOCATIONS)
        SKIP ("allocations are not counted under a sanitizer");

    CuePlayer player;
    player.prepare (rate, 0b1111, block);
    auto a = constant (0.5f, 0.5f, 1.0);
    auto b = constant (0.3f, 0.3f, 1.0, 48000.0);

    realtimeAllocations = 0;
    render (player, 4, 1024); // idle
    player.play (a, 0.0, 0.5, 1);
    render (player, 4, 4096);
    player.play (b, 0.2, 0.0, 2); // switch, resampling
    render (player, 4, 4096);
    player.stop();
    render (player, 4, 4096);
    CHECK (realtimeAllocations == 0);

    // The check itself works: an allocation on a real-time thread is counted.
    {
        djmix::rt::ScopedRealtimeThread realtime;
        auto* leak = new std::vector<int> (16);
        delete leak;
    }
    CHECK (realtimeAllocations > 0);
}

TEST_CASE ("Decoded audio is freed only once the audio thread is done with it", "[audio][cue]")
{
    CuePlayer player;
    CHECK_FALSE (player.play (constant (0.5f, 0.5f, 1.0), 0.0, 0.0, 1)); // no device yet

    player.prepare (rate, 0b1111, block);
    auto audio = constant (0.5f, 0.5f, 1.0);
    std::weak_ptr<const DecodedAudio> weak = audio;
    REQUIRE (player.play (std::move (audio), 0.0, 0.0, 1));

    player.releaseUnused(); // queued, not yet read
    CHECK_FALSE (weak.expired());
    render (player, 4, 1024);
    player.releaseUnused(); // playing
    CHECK_FALSE (weak.expired());

    player.stop();
    render (player, 4, 4096);
    player.releaseUnused(); // finished
    CHECK (weak.expired());

    // Stopping the device frees everything.
    auto another = constant (0.5f, 0.5f, 1.0);
    weak = another;
    REQUIRE (player.play (std::move (another), 0.0, 0.0, 2));
    render (player, 4, 1024);
    player.audioDeviceStopped();
    player.releaseUnused();
    CHECK (weak.expired());
}
