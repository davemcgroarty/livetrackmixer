#pragma once

#include <optional>

#include "analysis/Analysis.h"
#include "library/TrackOverrides.h"

namespace djmix
{

// A track's analysis with its hand corrections applied (design spec: "a
// track's effective grid = analysis + grid override; section and phrase
// views always use the effective grid").
//
// With a grid override the beats become a constant-tempo 4/4 grid: at the
// override BPM from the analysis's first downbeat (or the analysed beats if
// there is no BPM override), moved by the offset, with bars starting
// `downbeatShift` beats later. Sections start at the bar nearest each section
// start (the override's, or the analysis's), and phrases restart at each
// section as djanalyze lays them out. Per-bar scores come from the analysed
// bar under each new bar; per-section scores are their means.
TrackAnalysis effectiveAnalysis (const TrackAnalysis& analysis, const TrackOverrides& overrides);

// Where a track's sections start, in seconds.
std::vector<double> sectionStartTimes (const TrackAnalysis& analysis);

// Edits. Each takes the analysis (as analysed) and the current overrides and
// returns the overrides after the edit; times are in seconds.
namespace edits
{
    // Moves the whole grid later (or earlier, if negative).
    TrackOverrides nudge (const TrackOverrides& overrides, double seconds);

    // Sets the tempo (nothing: back to the analysed beats).
    TrackOverrides setBpm (const TrackOverrides& overrides, std::optional<double> bpm);

    // Halves or doubles the effective tempo.
    TrackOverrides scaleBpm (const TrackAnalysis& analysis, const TrackOverrides& overrides, double factor);

    // Makes the beat nearest `time` the first beat of a bar.
    TrackOverrides setDownbeat (const TrackAnalysis& analysis, const TrackOverrides& overrides, double time);

    // Starts a new section at the bar nearest `time`.
    TrackOverrides splitSection (const TrackAnalysis& analysis, const TrackOverrides& overrides, double time);

    // Joins section `index` (0-based, effective sections) to the one after it.
    TrackOverrides mergeWithNext (const TrackAnalysis& analysis, const TrackOverrides& overrides, int index);

    // Moves the start of section `index` (not the first) to the bar nearest
    // `time`, keeping at least one bar in it and in the section before.
    TrackOverrides moveSectionStart (const TrackAnalysis& analysis, const TrackOverrides& overrides, int index, double time);

    TrackOverrides resetGrid (const TrackOverrides& overrides);
    TrackOverrides resetSections (const TrackOverrides& overrides);
} // namespace edits

} // namespace djmix
