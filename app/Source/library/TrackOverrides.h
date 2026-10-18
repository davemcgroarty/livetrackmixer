#pragma once

#include <optional>
#include <string>
#include <vector>

namespace djmix
{

// Hand corrections to a track's grid (design spec, "Grid override"). Applied
// on top of the analysis, so re-analysing a track leaves them in place.
struct GridOverride
{
    std::optional<double> bpm; // a constant tempo, replacing the analysed beats
    double offsetSeconds = 0.0; // the whole grid moved later by this much
    int downbeatShift = 0;      // bars start this many beats later (0 to 3)

    bool isEmpty() const { return ! bpm && offsetSeconds > -1e-9 && offsetSeconds < 1e-9 && downbeatShift == 0; }
    bool operator== (const GridOverride&) const = default;
};

// Hand corrections to a track's sections ("Section override"). The section
// starts are kept as times rather than bar numbers, so they stay where the
// music changes when the grid is corrected; each snaps to the nearest bar of
// the grid in force.
struct TrackOverrides
{
    GridOverride grid;
    std::optional<std::vector<double>> sectionStarts; // seconds, ascending

    bool isEmpty() const { return grid.isEmpty() && ! sectionStarts; }
    bool operator== (const TrackOverrides&) const = default;
};

// Section starts as stored in the library: a JSON array of seconds.
std::string sectionStartsToJson (const std::vector<double>& starts);
std::optional<std::vector<double>> sectionStartsFromJson (const std::string& json);

} // namespace djmix
