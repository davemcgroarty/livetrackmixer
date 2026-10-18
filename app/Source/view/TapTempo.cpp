#include "TapTempo.h"

namespace djmix
{

std::optional<double> TapTempo::tap (double seconds)
{
    if (! taps.empty() && (seconds - taps.back() > resetAfterSeconds || seconds <= taps.back()))
        taps.clear();
    taps.push_back (seconds);
    if (taps.size() > maximumTaps)
        taps.erase (taps.begin());
    if (taps.size() < minimumTaps)
        return std::nullopt;

    // Least-squares slope of tap time against tap number: seconds per beat.
    const auto n = static_cast<double> (taps.size());
    double meanIndex = (n - 1.0) / 2.0, meanTime = 0.0;
    for (const double t : taps)
        meanTime += t / n;
    double covariance = 0.0, variance = 0.0;
    for (size_t i = 0; i < taps.size(); ++i)
    {
        const double di = static_cast<double> (i) - meanIndex;
        covariance += di * (taps[i] - meanTime);
        variance += di * di;
    }
    const double period = covariance / variance;
    if (period <= 0.0)
        return std::nullopt;
    return 60.0 / period;
}

} // namespace djmix
