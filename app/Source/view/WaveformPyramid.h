#pragma once

#include <vector>

#include "analysis/WaveformFile.h"

namespace djmix
{

// A waveform overview at successively halved resolutions, so drawing any
// zoom level reads about one stored point per pixel column.
class WaveformPyramid
{
public:
    struct Column
    {
        float peak = 0.0f;
        float rms = 0.0f;
    };

    WaveformPyramid() = default;
    explicit WaveformPyramid (const WaveformOverview& overview);

    bool empty() const { return levels.empty(); }
    double duration() const;
    int numLevels() const { return static_cast<int> (levels.size()); }
    size_t levelSize (int level) const { return levels[static_cast<size_t> (level)].peak.size(); }

    // The loudest peak in the track.
    float maxPeak() const { return levels.empty() ? 0.0f : levels.back().peak.front(); }

    // Loudest peak and RMS between two times (seconds), read from the coarsest
    // level that still has at least one point per `secondsPerPixel`.
    Column column (double start, double end, double secondsPerPixel) const;

private:
    struct Level
    {
        std::vector<float> peak;
        std::vector<float> rms;
    };

    std::vector<Level> levels; // levels[0] is the file's own resolution
    double secondsPerPoint = 0.0;
};

} // namespace djmix
