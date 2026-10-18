#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>

#include "view/EffectiveAnalysis.h"
#include "view/TapTempo.h"

using Catch::Approx;
using djmix::TrackAnalysis;
using djmix::TrackOverrides;
using djmix::effectiveAnalysis;
namespace edits = djmix::edits;

namespace
{
// 120 BPM in 4/4 from 0.25 s: 64 bars, sections at bars 1, 17 and 49. Bar i
// has rhythm i / 100, so tests can see which analysed bar a new bar took its
// scores from.
TrackAnalysis analysis()
{
    TrackAnalysis a;
    a.bpm = 120.0;
    a.durationSec = 0.25 + 64 * 2.0 + 1.0;
    for (int i = 0; i < 256; ++i)
        a.beats.push_back (0.25 + i * 0.5);
    for (int b = 0; b < 64; ++b)
    {
        a.bars.push_back ({ b + 1, a.beats[static_cast<size_t> (b * 4)], b * 4 });
        a.downbeats.push_back (a.bars.back().start);
        a.descriptorsPerBar.push_back ({ b / 100.0, 0.5, 0.5, 0.5 });
        a.energyPerBar.push_back (0.5);
    }
    auto section = [&a] (int startBar, int length, const char* camelot)
    {
        const double start = a.bars[static_cast<size_t> (startBar - 1)].start;
        const auto end = static_cast<size_t> (startBar - 1 + length);
        a.sections.push_back ({ start, end < a.bars.size() ? a.bars[end].start : a.durationSec, startBar, length,
                                djmix::AnalysisKey { "", camelot, 0.8 } });
        a.descriptorsPerSection.push_back ({});
    };
    section (1, 16, "8A");
    section (17, 32, "9A");
    section (49, 16, "8A");
    for (int p = 0; p < 8; ++p)
        a.phrases.push_back ({ p + 1, p * 8 + 1, 8, p < 2 ? 0 : (p < 6 ? 1 : 2) });
    return a;
}

std::vector<int> sectionStartBars (const TrackAnalysis& e)
{
    std::vector<int> bars;
    for (const auto& s : e.sections)
        bars.push_back (s.startBar);
    return bars;
}
} // namespace

TEST_CASE ("Without overrides the analysis is used as it is", "[edit]")
{
    const auto a = analysis();
    const auto e = effectiveAnalysis (a, {});
    CHECK (e.beats == a.beats);
    CHECK (e.bars.size() == a.bars.size());
    CHECK (sectionStartBars (e) == std::vector<int> { 1, 17, 49 });
}

TEST_CASE ("Nudging moves the whole grid and the sections with it", "[edit]")
{
    const auto a = analysis();
    const auto o = edits::nudge ({}, 0.1);
    const auto e = effectiveAnalysis (a, o);
    REQUIRE (e.beats.size() == a.beats.size());
    CHECK (e.beats[10] == Approx (a.beats[10] + 0.1));
    REQUIRE (e.bars.size() == 64);
    CHECK (e.bars[3].start == Approx (a.bars[3].start + 0.1));
    CHECK (sectionStartBars (e) == std::vector<int> { 1, 17, 49 });
    CHECK (e.sections[1].start == Approx (32.35));
    CHECK (e.descriptorsPerBar[5].rhythm == Approx (0.05)); // still bar 6's scores

    // Back again: no override left.
    CHECK (edits::nudge (o, -0.1).isEmpty());
}

TEST_CASE ("A BPM override lays a constant grid from the first downbeat", "[edit]")
{
    const auto a = analysis();
    const auto e = effectiveAnalysis (a, edits::setBpm ({}, 121.0));
    CHECK (e.bpm == 121.0);
    CHECK_FALSE (e.tempoVariable);
    REQUIRE (e.bars.size() >= 2);
    CHECK (e.bars[0].start == Approx (0.25));
    CHECK (e.bars[1].start - e.bars[0].start == Approx (4 * 60.0 / 121.0));
    CHECK (e.beats.back() < a.durationSec);
    CHECK (e.beats.size() == 261);

    // Halve and double the effective tempo.
    CHECK (*edits::scaleBpm (a, {}, 2.0).grid.bpm == Approx (240.0));
    CHECK (*edits::scaleBpm (a, edits::setBpm ({}, 121.0), 0.5).grid.bpm == Approx (60.5));
    CHECK (edits::setBpm (edits::setBpm ({}, 121.0), std::nullopt).isEmpty());
}

TEST_CASE ("Setting the downbeat moves where bars start", "[edit]")
{
    const auto a = analysis();
    // The third beat (1.25 s) is really the downbeat.
    const auto o = edits::setDownbeat (a, {}, 1.3);
    CHECK (o.grid.downbeatShift == 2);
    const auto e = effectiveAnalysis (a, o);
    REQUIRE (! e.bars.empty());
    CHECK (e.bars[0].start == Approx (1.25));
    CHECK (e.bars[1].start == Approx (3.25));
    CHECK (e.bars.size() == 64);
    CHECK (e.descriptorsPerBar[0].rhythm == Approx (0.01)); // its middle (2.25 s) is in analysed bar 2

    // Sections and phrases follow the new bars.
    for (const auto& s : e.sections)
        CHECK (std::find_if (e.bars.begin(), e.bars.end(), [&] (const djmix::Bar& b) { return b.start == s.start; }) != e.bars.end());
    CHECK (e.phrases.front().startBar == 1);

    // And back: the first beat starts bar 1 again.
    CHECK (edits::setDownbeat (a, o, 0.2).isEmpty());
}

TEST_CASE ("Sections split, merge and move by whole bars", "[edit]")
{
    const auto a = analysis();

    // Split at 50 s: the nearest bar starts at 50.25 s, bar 26.
    auto o = edits::splitSection (a, {}, 50.0);
    auto e = effectiveAnalysis (a, o);
    CHECK (sectionStartBars (e) == std::vector<int> { 1, 17, 26, 49 });
    CHECK (e.sections[1].lengthBars == 9);
    CHECK (e.sections[2].key->camelot == "9A"); // keys come from the analysed section
    // Phrases restart at the new section.
    CHECK (std::any_of (e.phrases.begin(), e.phrases.end(), [] (const djmix::Phrase& p) { return p.startBar == 26; }));
    CHECK (e.descriptorsPerSection.size() == 4);

    // Merge the first two: one 25-bar section, in 8-bar phrases.
    o = edits::mergeWithNext (a, o, 0);
    e = effectiveAnalysis (a, o);
    CHECK (sectionStartBars (e) == std::vector<int> { 1, 26, 49 });

    // Move the second section's start to bar 21, then try past its neighbour.
    o = edits::moveSectionStart (a, o, 1, 40.0);
    CHECK (sectionStartBars (effectiveAnalysis (a, o)) == std::vector<int> { 1, 21, 49 });
    o = edits::moveSectionStart (a, o, 1, 1000.0);
    CHECK (sectionStartBars (effectiveAnalysis (a, o)) == std::vector<int> { 1, 48, 49 });
    CHECK (edits::moveSectionStart (a, o, 0, 10.0) == o); // the first section always starts at bar 1

    // A 48-bar section is laid out in 16-bar phrases, as djanalyze does.
    const auto merged = effectiveAnalysis (a, edits::mergeWithNext (a, {}, 1));
    CHECK (sectionStartBars (merged) == std::vector<int> { 1, 17 });
    CHECK (merged.phrases[2].lengthBars == 16);

    CHECK (edits::resetSections (o).isEmpty());
}

TEST_CASE ("Several sections merge into one", "[edit]")
{
    const auto a = analysis();
    auto o = edits::splitSection (a, {}, 50.25); // sections at bars 1, 17, 26, 49

    // The middle two.
    auto merged = edits::mergeSections (a, o, 1, 2);
    CHECK (sectionStartBars (effectiveAnalysis (a, merged)) == std::vector<int> { 1, 17, 49 });

    // Three at once, to the end.
    merged = edits::mergeSections (a, o, 1, 3);
    CHECK (sectionStartBars (effectiveAnalysis (a, merged)) == std::vector<int> { 1, 17 });

    // All of them.
    merged = edits::mergeSections (a, o, 0, 3);
    CHECK (sectionStartBars (effectiveAnalysis (a, merged)) == std::vector<int> { 1 });

    // Nothing to merge, or out of range.
    CHECK (edits::mergeSections (a, o, 2, 2) == o);
    CHECK (edits::mergeSections (a, o, 2, 1) == o);
    CHECK (edits::mergeSections (a, o, 1, 9) == o);
    CHECK (edits::mergeSections (a, o, -1, 2) == o);
}

TEST_CASE ("Section edits stay where the music is when the grid changes", "[edit]")
{
    const auto a = analysis();
    auto o = edits::splitSection (a, {}, 50.25);
    o = edits::nudge (o, 0.1);
    const auto e = effectiveAnalysis (a, o);
    CHECK (sectionStartBars (e) == std::vector<int> { 1, 17, 26, 49 });
    CHECK (e.sections[2].start == Approx (50.35));
    CHECK (edits::resetGrid (o).grid.isEmpty());
    CHECK (edits::resetGrid (o).sectionStarts == o.sectionStarts);
}

TEST_CASE ("Tap tempo fits a line through the taps", "[edit]")
{
    djmix::TapTempo taps;
    CHECK_FALSE (taps.tap (10.0));
    CHECK_FALSE (taps.tap (10.5));
    CHECK_FALSE (taps.tap (11.0));
    CHECK (*taps.tap (11.5) == Approx (120.0));

    // Uneven taps average out.
    taps.reset();
    std::optional<double> bpm;
    for (const double t : { 0.0, 0.47, 0.98, 1.49, 1.95, 2.46, 2.96, 3.43 })
        bpm = taps.tap (t);
    REQUIRE (bpm);
    CHECK (*bpm == Approx (122.0).margin (1.0));

    // A pause starts a new count.
    CHECK_FALSE (taps.tap (10.0));
    CHECK (taps.count() == 1);
}

TEST_CASE ("Section starts are stored as a JSON list", "[edit]")
{
    const std::vector<double> starts { 0.25, 32.25, 96.5 };
    CHECK (djmix::sectionStartsFromJson (djmix::sectionStartsToJson (starts)) == starts);
    CHECK_FALSE (djmix::sectionStartsFromJson (""));
    CHECK_FALSE (djmix::sectionStartsFromJson ("{\"a\": 1}"));
    CHECK_FALSE (djmix::sectionStartsFromJson ("[1, \"two\"]"));
    CHECK_FALSE (djmix::sectionStartsFromJson ("not json"));
}
