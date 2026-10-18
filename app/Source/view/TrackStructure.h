#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "analysis/Analysis.h"

namespace djmix
{

// Lookups the Track view needs over a loaded analysis: which bar or section a
// time falls in, where bars end, which bars start a phrase, and which grid
// lines are far enough apart on screen to draw.
class TrackStructure
{
public:
    // Grid lines closer than these (pixels) are left out.
    static constexpr double minBeatSpacing = 10.0;
    static constexpr double minBarSpacing = 14.0;

    struct GridVisibility
    {
        bool beats = false;
        bool bars = false;
    };

    TrackStructure() = default;
    explicit TrackStructure (const TrackAnalysis& analysis);

    // Index of the bar or section containing `seconds`, or -1 outside them.
    int barAt (double seconds) const;
    int sectionAt (double seconds) const;

    double barStart (int bar) const { return analysis.bars[static_cast<size_t> (bar)].start; }
    double barEnd (int bar) const;
    int numBars() const { return static_cast<int> (analysis.bars.size()); }

    bool startsPhrase (int bar) const;

    // The first and last bar (0-based, inclusive) of the phrase holding `bar`:
    // from the analysis, or the 8-bar block around it if no phrase covers it.
    std::pair<int, int> phraseAround (int bar) const;
    bool startsSection (int bar) const;

    // Seconds per beat (median, for variable-tempo tracks too).
    double beatPeriod() const { return period; }

    GridVisibility gridAt (double pixelsPerSecond) const;

    // The beat nearest a time (ties go to the earlier beat, as grid edits do),
    // with the bar it falls in (-1 before the first bar) and its place in it.
    struct BeatPosition
    {
        int index = 0;      // into the analysis's beats
        double time = 0.0;
        int bar = -1;       // 0-based
        int beatInBar = 0;  // 1-based
    };
    std::optional<BeatPosition> beatNear (double seconds) const;

    const TrackAnalysis& data() const { return analysis; }

private:
    TrackAnalysis analysis;
    std::vector<bool> phraseStarts; // per bar
    std::vector<bool> sectionStarts; // per bar
    double period = 0.5;
};

} // namespace djmix
