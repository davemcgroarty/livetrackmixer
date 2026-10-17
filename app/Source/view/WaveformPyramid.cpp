#include "WaveformPyramid.h"

#include <algorithm>
#include <cmath>

namespace djmix
{

WaveformPyramid::WaveformPyramid (const WaveformOverview& overview)
    : secondsPerPoint (overview.secondsPerPoint())
{
    if (overview.peak.empty() || secondsPerPoint <= 0.0)
        return;

    levels.push_back ({ overview.peak, overview.rms });
    while (levels.back().peak.size() > 1)
    {
        const auto& below = levels.back();
        Level next;
        const size_t n = (below.peak.size() + 1) / 2;
        next.peak.resize (n);
        next.rms.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const size_t a = 2 * i, b = std::min (a + 1, below.peak.size() - 1);
            next.peak[i] = std::max (below.peak[a], below.peak[b]);
            // RMS of two equal-length blocks: root of the mean of their squares.
            next.rms[i] = std::sqrt (0.5f * (below.rms[a] * below.rms[a] + below.rms[b] * below.rms[b]));
        }
        levels.push_back (std::move (next));
    }
}

double WaveformPyramid::duration() const
{
    return levels.empty() ? 0.0 : static_cast<double> (levels.front().peak.size()) * secondsPerPoint;
}

WaveformPyramid::Column WaveformPyramid::column (double start, double end, double secondsPerPixel) const
{
    if (levels.empty() || end <= 0.0)
        return {};

    // Each level up halves the resolution; pick the coarsest one that still
    // has a point per pixel.
    const double pointsPerPixel = secondsPerPixel / secondsPerPoint;
    int level = pointsPerPixel > 1.0 ? static_cast<int> (std::floor (std::log2 (pointsPerPixel))) : 0;
    level = std::clamp (level, 0, numLevels() - 1);

    const auto& l = levels[static_cast<size_t> (level)];
    const double scale = secondsPerPoint * static_cast<double> (1 << level);
    const auto size = static_cast<long long> (l.peak.size());
    const auto first = std::clamp (static_cast<long long> (std::floor (start / scale)), 0LL, size - 1);
    const auto last = std::clamp (static_cast<long long> (std::ceil (end / scale)), first + 1, size);

    Column c;
    for (auto i = first; i < last; ++i)
    {
        c.peak = std::max (c.peak, l.peak[static_cast<size_t> (i)]);
        c.rms = std::max (c.rms, l.rms[static_cast<size_t> (i)]);
    }
    return c;
}

} // namespace djmix
