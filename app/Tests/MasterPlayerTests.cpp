#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "AllocationCounter.h"
#include "SyntheticAudio.h"
#include "engine/AudioEngine.h"
#include "engine/MasterPlayer.h"

using Catch::Approx;
using djmix::MasterPlayer;
using djmix::PlaybackSnapshot;
using djmix::RenderedClip;
namespace test = djmix::test;

namespace
{
constexpr double rate = 44100.0;
constexpr int block = 256;
const int fade = static_cast<int> (std::lround (MasterPlayer::fadeSeconds * rate));

// A render of `frames` frames: a constant level, or a sine at `hz`.
std::shared_ptr<RenderedClip> renderOf (int frames, float level, double hz = 0.0, double tempo = 120.0)
{
    auto r = std::make_shared<RenderedClip>();
    r->settings = { tempo, 0, rate };
    r->samples.setSize (2, frames);
    for (int i = 0; i < frames; ++i)
    {
        const auto v = hz > 0.0 ? static_cast<float> (level * std::sin (2.0 * test::pi * hz * i / rate)) : level;
        r->samples.setSample (0, i, v);
        r->samples.setSample (1, i, -v);
    }
    r->toBeat = frames / r->framesPerBeat();
    return r;
}

// One clip on one layer, from timeline beat `startBeat` for `lengthBeats`,
// starting 1000 frames into the render (as a real render has a margin before
// the clip's first beat).
constexpr int margin = 1000;
std::shared_ptr<PlaybackSnapshot> snapshotOf (std::shared_ptr<RenderedClip> render, double startBeat, double lengthBeats,
                                              double tempo = 120.0, float layerGain = 1.0f)
{
    auto s = std::make_shared<PlaybackSnapshot>();
    s->tempo = tempo;
    s->sampleRate = rate;
    s->layerGains = { layerGain };
    PlaybackSnapshot::Voice v;
    v.render = render.get();
    v.start = std::llround (startBeat * s->samplesPerBeat());
    v.end = std::llround ((startBeat + lengthBeats) * s->samplesPerBeat());
    v.frameAtStart = margin;
    v.step = tempo / render->settings.tempo;
    s->voices.push_back (v);
    s->renders.push_back (std::move (render));
    return s;
}

// Runs the Master bus for `numSamples`, a block at a time: left and right.
std::pair<std::vector<float>, std::vector<float>> run (MasterPlayer& player, int numSamples)
{
    std::vector<float> left (static_cast<size_t> (numSamples)), right (static_cast<size_t> (numSamples));
    for (int done = 0; done < numSamples; done += block)
    {
        const int n = std::min (block, numSamples - done);
        player.render (left.data() + done, right.data() + done, n);
    }
    return { left, right };
}

float maxAbs (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    float m = 0.0f;
    for (size_t i = from; i < std::min (to, v.size()); ++i)
        m = std::max (m, std::abs (v[i]));
    return m;
}
} // namespace

TEST_CASE ("A clip plays at its place on the timeline, faded in just before it", "[audio][master]")
{
    MasterPlayer player;
    player.prepare (rate, block);
    // 120 BPM: 22050 samples a beat. A clip from beat 2 to beat 4.
    player.publish (snapshotOf (renderOf (100000, 0.5f), 2.0, 2.0));
    player.play();

    // Playback starts one fade early, so output sample i is timeline sample i - fade.
    const auto [left, right] = run (player, 120000);
    const size_t start = 44100 + static_cast<size_t> (fade), end = 88200 + static_cast<size_t> (fade);
    CHECK (maxAbs (left, 0, start - static_cast<size_t> (fade)) == 0.0f);
    CHECK (left[start - static_cast<size_t> (fade) / 2] == Approx (0.25f).margin (0.01));
    CHECK (left[start] == Approx (0.5f));
    CHECK (right[start + 1000] == Approx (-0.5f));
    CHECK (left[end - static_cast<size_t> (fade) - 1] == Approx (0.5f).margin (0.001));
    CHECK (maxAbs (left, end) == 0.0f);
    CHECK (test::maxStep (left) < 0.5f / fade * 1.1f);

    const auto state = player.state();
    CHECK (state.playing);
    CHECK (state.beat == Approx ((120000 - fade) / 22050.0));
}

TEST_CASE ("Pausing, playing and seeking fade instead of clicking", "[audio][master]")
{
    MasterPlayer player;
    player.prepare (rate, block);
    player.publish (snapshotOf (renderOf (20 * 44100, 0.5f, 440.0), 0.0, 40.0));
    player.seek (4.0);
    CHECK (player.state().beat == 0.0); // not yet: the audio thread has not run
    run (player, block);
    CHECK (player.state().beat == Approx (4.0));
    CHECK_FALSE (player.state().playing);

    player.play();
    auto [a, a2] = run (player, 22050);
    CHECK (player.state().beat == Approx (5.0 - fade / 22050.0));

    player.seek (16.0);
    auto [b, b2] = run (player, 22050);
    CHECK (player.state().beat == Approx (17.0 - 2.0 * fade / 22050.0).margin (0.001));

    player.pause();
    auto [c, c2] = run (player, 4096);
    CHECK_FALSE (player.state().playing);
    const double pausedAt = player.state().beat;
    run (player, 4096);
    CHECK (player.state().beat == pausedAt);

    // A sine at 0.5 moves at most 0.031 a sample; a click would jump much further.
    std::vector<float> all (a);
    all.insert (all.end(), b.begin(), b.end());
    all.insert (all.end(), c.begin(), c.end());
    CHECK (test::maxStep (all) < 0.035f);
    CHECK (maxAbs (c, 1024) == 0.0f);
}

TEST_CASE ("A new snapshot crossfades in, on the same beat", "[audio][master]")
{
    MasterPlayer player;
    player.prepare (rate, block);
    player.publish (snapshotOf (renderOf (20 * 44100, 0.5f, 440.0), 0.0, 40.0));
    player.play();
    auto [a, a2] = run (player, 44100);
    const double before = player.state().beat;

    // Same clip, layer at half level: no jump, just a quick fade down.
    player.publish (snapshotOf (renderOf (20 * 44100, 0.5f, 440.0), 0.0, 40.0, 120.0, 0.5f));
    auto [b, b2] = run (player, 4096);
    CHECK (player.state().beat == Approx (before + 4096 / 22050.0));
    CHECK (maxAbs (b, 1024) == Approx (0.25f).margin (0.01));

    // Twice the tempo: the playhead stays on its beat, now twice as many beats a second.
    const double beat = player.state().beat;
    player.publish (snapshotOf (renderOf (20 * 44100, 0.5f, 440.0, 240.0), 0.0, 80.0, 240.0));
    auto [c, c2] = run (player, 4096);
    CHECK (player.state().beat == Approx (beat + 2.0 * 4096 / 22050.0).margin (0.001));

    std::vector<float> all (a);
    all.insert (all.end(), b.begin(), b.end());
    all.insert (all.end(), c.begin(), c.end());
    CHECK (test::maxStep (all) < 0.035f);
}

TEST_CASE ("A render for another tempo plays at the project tempo, on the beat", "[audio][master]")
{
    // Rendered at 120 BPM, the project now at 132: it is read 10% faster.
    MasterPlayer player;
    player.prepare (rate, block);
    player.publish (snapshotOf (renderOf (10 * 44100, 0.5f, 441.0, 120.0), 0.0, 16.0, 132.0));
    player.play();
    const auto [left, right] = run (player, 3 * 44100);
    CHECK (test::crossingsPerSecond (left, rate, 44100, 2 * 44100) / 2.0 == Approx (441.0 * 1.1).margin (1.0));
}

TEST_CASE ("Snapshots are freed once the audio thread is done with them", "[audio][master]")
{
    MasterPlayer player;
    player.prepare (rate, block);
    auto first = snapshotOf (renderOf (44100, 0.5f), 0.0, 2.0);
    std::weak_ptr<const PlaybackSnapshot> weakFirst = first;
    std::weak_ptr<const RenderedClip> weakRender = first->renders[0];
    player.publish (std::move (first));
    player.play();
    run (player, block);

    player.publish (snapshotOf (renderOf (44100, 0.25f), 0.0, 2.0));
    CHECK_FALSE (weakFirst.expired()); // the audio thread may still be in it
    run (player, 64);                   // crossfading from it
    player.releaseUnused();
    CHECK_FALSE (weakFirst.expired());
    run (player, block);                // the crossfade is over
    player.releaseUnused();
    CHECK (weakFirst.expired());
    CHECK (weakRender.expired());
    CHECK (player.numHeldSnapshots() == 1);

    // With the device stopped, all but the newest go at once.
    auto third = snapshotOf (renderOf (44100, 0.1f), 0.0, 2.0);
    player.publish (third);
    player.stopped();
    player.publish (snapshotOf (renderOf (44100, 0.1f), 0.0, 2.0));
    std::weak_ptr<const PlaybackSnapshot> weakThird = third;
    third.reset();
    CHECK (weakThird.expired());
    CHECK (player.numHeldSnapshots() == 1);
}

TEST_CASE ("Master and Cue go to their own outputs, or share one pair with split cue", "[audio][master]")
{
    djmix::AudioEngine engine;
    engine.prepare (rate, 0b1111, block);
    engine.master().publish (snapshotOf (renderOf (44100, 0.5f), 0.0, 2.0));
    engine.master().play();
    REQUIRE (engine.cue().play (test::sineTrack (100.0, 1.0, rate, 0.0f), 0.0, 0.0, 1)); // silent cue

    std::vector<std::vector<float>> out (4, std::vector<float> (block));
    std::vector<float*> pointers { out[0].data(), out[1].data(), out[2].data(), out[3].data() };
    for (int i = 0; i < 4; ++i)
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, pointers.data(), 4, block, {});
    CHECK (out[0][100] == Approx (0.5f));
    CHECK (out[1][100] == Approx (-0.5f));
    CHECK (maxAbs (out[2]) == 0.0f);

    // A stereo device: Master in mono on the right; here left and right cancel.
    engine.prepare (rate, 0b11, block);
    engine.master().play();
    for (int i = 0; i < 4; ++i)
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, pointers.data(), 2, block, {});
    CHECK (maxAbs (out[1]) < 1e-6f);
}

TEST_CASE ("The audio callback never allocates while playing Master", "[audio][master][realtime]")
{
    if (! test::allocationsAreCounted)
        SKIP ("allocations are not counted under a sanitizer");

    djmix::AudioEngine engine;
    engine.prepare (rate, 0b1111, block);
    auto& player = engine.master();
    std::vector<std::vector<float>> out (4, std::vector<float> (block));
    std::vector<float*> pointers { out[0].data(), out[1].data(), out[2].data(), out[3].data() };
    auto callback = [&] (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, pointers.data(), 4, block, {});
    };

    auto s1 = snapshotOf (renderOf (4 * 44100, 0.5f, 440.0), 0.0, 8.0);
    auto s2 = snapshotOf (renderOf (4 * 44100, 0.5f, 440.0, 240.0), 1.0, 8.0, 240.0);
    test::resetRealtimeAllocations();
    player.publish (s1);
    callback (4);
    player.play();
    callback (16);
    player.publish (s2); // a crossfade and a tempo change
    callback (16);
    player.seek (2.0);
    callback (16);
    player.pause();
    callback (16);
    CHECK (test::realtimeAllocations() == 0);
}
