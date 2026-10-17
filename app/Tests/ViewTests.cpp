#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "view/TrackStructure.h"
#include "view/ViewRange.h"
#include "view/WaveformPyramid.h"

using Catch::Approx;
using djmix::TrackStructure;
using djmix::ViewRange;
using djmix::WaveformPyramid;

namespace
{
// 120 BPM in 4/4 from 0.25 s: 64 bars, 8-bar phrases, sections at bars 1, 17 and 49.
djmix::TrackAnalysis syntheticAnalysis()
{
    djmix::TrackAnalysis a;
    a.bpm = 120.0;
    const double first = 0.25, period = 0.5;
    const int numBars = 64;
    a.durationSec = first + numBars * 4 * period + 1.0;
    for (int i = 0; i < numBars * 4; ++i)
        a.beats.push_back (first + i * period);
    for (int b = 0; b < numBars; ++b)
    {
        a.bars.push_back ({ b + 1, a.beats[static_cast<size_t> (b * 4)], b * 4 });
        a.downbeats.push_back (a.bars.back().start);
        a.descriptorsPerBar.push_back ({ 0.5, 0.5, 0.5, 0.5 });
    }
    for (int p = 0; p < numBars / 8; ++p)
        a.phrases.push_back ({ p + 1, p * 8 + 1, 8, p < 2 ? 0 : (p < 6 ? 1 : 2) });

    auto section = [&a] (int startBar, int lengthBars)
    {
        const double start = a.bars[static_cast<size_t> (startBar - 1)].start;
        const auto endBar = static_cast<size_t> (startBar - 1 + lengthBars);
        const double end = endBar < a.bars.size() ? a.bars[endBar].start : a.durationSec;
        a.sections.push_back ({ start, end, startBar, lengthBars, std::nullopt });
        a.descriptorsPerSection.push_back ({ 0.5, 0.5, 0.5, 0.5 });
    };
    section (1, 16);
    section (17, 32);
    section (49, 16);
    return a;
}

djmix::WaveformOverview overview (std::vector<float> peak, std::vector<float> rms)
{
    djmix::WaveformOverview o;
    o.sampleRate = 1000;
    o.samplesPerPoint = 100; // 0.1 s per point
    o.peak = std::move (peak);
    o.rms = std::move (rms);
    return o;
}
} // namespace

TEST_CASE ("ViewRange maps times to pixels and back", "[view]")
{
    ViewRange range (100.0, 1.0);
    range.setWidth (1000.0);
    CHECK (range.start() == 0.0);
    CHECK (range.span() == 100.0);
    CHECK (range.timeToX (50.0) == Approx (500.0));
    CHECK (range.xToTime (250.0) == Approx (25.0));
    CHECK (range.pixelsPerSecond() == Approx (10.0));

    range.show (40.0, 10.0);
    CHECK (range.timeToX (45.0) == Approx (500.0));
    CHECK (range.xToTime (0.0) == Approx (40.0));
}

TEST_CASE ("ViewRange zooms around the anchor and stays inside the track", "[view]")
{
    ViewRange range (100.0, 2.0);
    range.setWidth (1000.0);

    const double anchor = range.xToTime (300.0);
    range.zoomAt (300.0, 4.0);
    CHECK (range.span() == Approx (25.0));
    CHECK (range.xToTime (300.0) == Approx (anchor));

    // No further in than the minimum span, and the anchor still holds.
    for (int i = 0; i < 20; ++i)
        range.zoomAt (300.0, 2.0);
    CHECK (range.span() == Approx (2.0));
    CHECK (range.xToTime (300.0) == Approx (anchor));

    // No further out than the whole track.
    for (int i = 0; i < 20; ++i)
        range.zoomAt (900.0, 0.5);
    CHECK (range.start() == 0.0);
    CHECK (range.span() == Approx (100.0));

    // Zooming in at the right edge keeps the view inside the track.
    range.zoomAt (1000.0, 10.0);
    CHECK (range.end() == Approx (100.0));

    range.show (10.0, 20.0);
    range.scrollBy (-50.0);
    CHECK (range.start() == 0.0);
    range.scrollBy (500.0);
    CHECK (range.end() == Approx (100.0));
    CHECK (range.span() == Approx (20.0));
}

TEST_CASE ("WaveformPyramid halves the resolution per level", "[view]")
{
    const WaveformPyramid pyramid (overview ({ 0.1f, 0.9f, 0.2f, 0.3f, 0.5f }, { 0.1f, 0.3f, 0.2f, 0.2f, 0.4f }));
    REQUIRE (pyramid.numLevels() == 4);
    CHECK (pyramid.levelSize (0) == 5);
    CHECK (pyramid.levelSize (1) == 3);
    CHECK (pyramid.levelSize (2) == 2);
    CHECK (pyramid.levelSize (3) == 1);
    CHECK (pyramid.duration() == Approx (0.5));
    CHECK (pyramid.maxPeak() == Approx (0.9f));

    // At the finest level a column reads just the points it covers.
    auto c = pyramid.column (0.0, 0.1, 0.1);
    CHECK (c.peak == Approx (0.1f));
    c = pyramid.column (0.1, 0.2, 0.1);
    CHECK (c.peak == Approx (0.9f));
    CHECK (c.rms == Approx (0.3f));

    // Coarser columns keep the loudest peak, and RMS combines as power.
    c = pyramid.column (0.0, 0.2, 0.2);
    CHECK (c.peak == Approx (0.9f));
    CHECK (c.rms == Approx (std::sqrt ((0.01f + 0.09f) / 2.0f)));

    // The whole track in one column.
    c = pyramid.column (0.0, 0.5, 1.0);
    CHECK (c.peak == Approx (0.9f));

    // Outside the track.
    c = pyramid.column (-1.0, -0.5, 0.1);
    CHECK (c.peak == 0.0f);
}

TEST_CASE ("WaveformPyramid finds short peaks at every zoom", "[view]")
{
    std::vector<float> peak (100000, 0.1f), rms (100000, 0.05f);
    peak[54321] = 1.0f;
    const WaveformPyramid pyramid (overview (peak, rms));
    for (const double secondsPerPixel : { 0.1, 0.3, 1.0, 7.0, 100.0 })
    {
        // The pixel column covering point 54321 (5432.1 s).
        const double start = std::floor (5432.15 / secondsPerPixel) * secondsPerPixel;
        INFO ("seconds per pixel " << secondsPerPixel);
        CHECK (pyramid.column (start, start + secondsPerPixel, secondsPerPixel).peak == Approx (1.0f));
    }
    CHECK (WaveformPyramid().empty());
    CHECK (WaveformPyramid().column (0.0, 1.0, 0.1).peak == 0.0f);
}

TEST_CASE ("TrackStructure finds bars, sections and phrase starts", "[view]")
{
    const TrackStructure s (syntheticAnalysis());
    CHECK (s.numBars() == 64);
    CHECK (s.beatPeriod() == Approx (0.5));

    CHECK (s.barAt (0.0) == -1); // before the first downbeat
    CHECK (s.barAt (0.25) == 0);
    CHECK (s.barAt (2.24) == 0);
    CHECK (s.barAt (2.25) == 1);
    CHECK (s.barAt (s.barStart (63) + 1.0) == 63);
    CHECK (s.barAt (s.barStart (63) + 2.5) == -1); // after the last bar's four beats
    CHECK (s.barEnd (63) == Approx (s.barStart (63) + 2.0));

    CHECK (s.sectionAt (0.1) == -1);
    CHECK (s.sectionAt (s.barStart (0)) == 0);
    CHECK (s.sectionAt (s.barStart (16)) == 1);
    CHECK (s.sectionAt (s.barStart (48) + 0.1) == 2);

    CHECK (s.startsPhrase (0));
    CHECK_FALSE (s.startsPhrase (1));
    CHECK (s.startsPhrase (8));
    CHECK (s.startsPhrase (56));
    CHECK_FALSE (s.startsPhrase (64));
    CHECK (s.startsSection (16));
    CHECK_FALSE (s.startsSection (8));
}

TEST_CASE ("TrackStructure leaves out grid lines that would crowd together", "[view]")
{
    const TrackStructure s (syntheticAnalysis());

    // 0.5 s per beat: beats need 20 px per second, bars 7.
    auto grid = s.gridAt (20.0);
    CHECK (grid.beats);
    CHECK (grid.bars);
    grid = s.gridAt (19.0);
    CHECK_FALSE (grid.beats);
    grid = s.gridAt (8.0);
    CHECK_FALSE (grid.beats);
    CHECK (grid.bars);
    grid = s.gridAt (1.0);
    CHECK_FALSE (grid.beats);
    CHECK_FALSE (grid.bars);

    // An empty analysis still answers sensibly.
    const TrackStructure empty;
    CHECK (empty.barAt (1.0) == -1);
    CHECK (empty.sectionAt (1.0) == -1);
    CHECK_FALSE (empty.startsPhrase (0));
}
