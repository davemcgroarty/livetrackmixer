#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <vector>

#include "SyntheticAudio.h"
#include "engine/ClipRenderer.h"

using Catch::Approx;
using djmix::RenderSettings;
using djmix::renderClip;
namespace test = djmix::test;

namespace
{
std::vector<float> channel (const djmix::RenderedClip& r, int ch)
{
    const float* p = r.samples.getReadPointer (ch);
    return { p, p + r.samples.getNumSamples() };
}
} // namespace

TEST_CASE ("A render puts each beat of the track on the project grid", "[render]")
{
    // 124 BPM clicks from 0.1 s, rendered at 128 BPM.
    const auto audio = test::clickTrack (124.0, 0.1, 20.0);
    const auto beats = test::beatGrid (124.0, 0.1, 20.0);
    const RenderSettings settings { 128.0, 0, 44100.0 };
    const auto r = renderClip (*audio, beats, 4.0, 36.0, settings);
    REQUIRE (r != nullptr);

    CHECK (r->firstSourceBeat == Approx (4.0));
    CHECK (r->framesPerBeat() == Approx (44100.0 * 60.0 / 128.0));
    CHECK (r->samples.getNumSamples() == static_cast<int> (std::lround (32 * r->framesPerBeat())));
    CHECK (r->covers (4.0, 36.0));
    CHECK_FALSE (r->covers (3.0, 36.0));

    const auto left = channel (*r, 0);
    for (int beat = 4; beat < 36; ++beat)
    {
        const double expected = r->frameAt (beat) / 44100.0;
        const auto onset = test::onsetNear (left, 44100.0, expected);
        INFO ("beat " << beat);
        REQUIRE (onset);
        CHECK (std::abs (*onset - expected) < 0.003);
    }
}

TEST_CASE ("A render keeps a track whose tempo moves on the beat", "[render]")
{
    // From 120 BPM up to 126 over 40 seconds, rendered at 128. Pinned bar
    // lines keep it within a few milliseconds (v1 is for steady tracks, which
    // keep to a millisecond or so).
    std::vector<double> beats;
    for (double t = 0.2, bpm = 120.0; t < 40.0; t += 60.0 / bpm, bpm += 0.075)
        beats.push_back (t);
    const auto audio = test::clicksAt (beats, 40.0);
    const auto r = renderClip (*audio, beats, 0.0, 72.0, { 128.0, 0, 44100.0 });
    REQUIRE (r != nullptr);

    const auto left = channel (*r, 0);
    for (int beat = 0; beat < 72; ++beat)
    {
        const double expected = r->frameAt (beat) / 44100.0;
        const auto onset = test::onsetNear (left, 44100.0, expected);
        INFO ("beat " << beat);
        REQUIRE (onset);
        CHECK (std::abs (*onset - expected) < 0.005);
    }
}

TEST_CASE ("A render stops at the ends of the track", "[render]")
{
    const auto audio = test::clickTrack (124.0, 0.1, 10.0);
    const auto beats = test::beatGrid (124.0, 0.1, 10.0);
    const auto r = renderClip (*audio, beats, -1.0, 100.0, { 124.0, 0, 44100.0 });
    REQUIRE (r != nullptr);

    // It starts at the start of the file, a fifth of a beat before beat 0...
    CHECK (r->firstSourceBeat == Approx (-0.1 / (60.0 / 124.0)).margin (1e-4));
    // ...ends at the end of it, and still says it covers what was asked.
    CHECK (r->samples.getNumSamples() == Approx (10.0 * 44100.0).margin (2));
    CHECK (r->covers (-1.0, 100.0));
}

TEST_CASE ("A render is transposed and resampled without changing its timing", "[render]")
{
    const auto audio = test::sineTrack (440.0, 4.0);
    const std::vector<double> beats = test::beatGrid (120.0, 0.0, 4.0);

    // Two semitones up.
    auto r = renderClip (*audio, beats, 0.0, 8.0, { 120.0, 2, 44100.0 });
    REQUIRE (r != nullptr);
    CHECK (r->samples.getNumSamples() == 4 * 44100);
    auto left = channel (*r, 0);
    CHECK (test::crossingsPerSecond (left, 44100.0, 44100, 3 * 44100) / 2.0 == Approx (493.88).margin (1.5));

    // At 48 kHz: as many seconds, the same pitch.
    r = renderClip (*audio, beats, 0.0, 8.0, { 120.0, 0, 48000.0 });
    REQUIRE (r != nullptr);
    CHECK (r->samples.getNumSamples() == 4 * 48000);
    left = channel (*r, 0);
    CHECK (test::crossingsPerSecond (left, 48000.0, 48000, 3 * 48000) / 2.0 == Approx (440.0).margin (1.5));
}

TEST_CASE ("A cancelled render gives up", "[render]")
{
    const auto audio = test::sineTrack (440.0, 4.0);
    std::atomic<bool> cancelled { true };
    CHECK (renderClip (*audio, test::beatGrid (120.0, 0.0, 4.0), 0.0, 8.0, { 128.0, 0, 44100.0 }, &cancelled) == nullptr);
}
