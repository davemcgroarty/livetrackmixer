#include "BeatMap.h"

#include <algorithm>
#include <cmath>

namespace djmix
{

namespace
{
constexpr double defaultPeriod = 0.5;
}

double timeAtBeat (const std::vector<double>& beats, double beat)
{
    const auto n = beats.size();
    if (n == 0)
        return beat * defaultPeriod;
    if (n == 1)
        return beats[0] + beat * defaultPeriod;

    if (beat <= 0.0)
        return beats[0] + beat * (beats[1] - beats[0]);
    const auto last = static_cast<double> (n - 1);
    if (beat >= last)
        return beats[n - 1] + (beat - last) * (beats[n - 1] - beats[n - 2]);

    const auto i = static_cast<size_t> (std::floor (beat));
    const double frac = beat - static_cast<double> (i);
    return beats[i] + frac * (beats[i + 1] - beats[i]);
}

double beatAtTime (const std::vector<double>& beats, double seconds)
{
    const auto n = beats.size();
    if (n == 0)
        return seconds / defaultPeriod;
    if (n == 1)
        return (seconds - beats[0]) / defaultPeriod;

    if (seconds <= beats[0])
        return (seconds - beats[0]) / (beats[1] - beats[0]);
    if (seconds >= beats[n - 1])
        return static_cast<double> (n - 1) + (seconds - beats[n - 1]) / (beats[n - 1] - beats[n - 2]);

    const auto after = std::upper_bound (beats.begin(), beats.end(), seconds);
    const auto i = static_cast<size_t> (after - beats.begin()) - 1;
    return static_cast<double> (i) + (seconds - beats[i]) / (beats[i + 1] - beats[i]);
}

} // namespace djmix
