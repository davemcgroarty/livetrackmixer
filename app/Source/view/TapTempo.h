#pragma once

#include <optional>
#include <vector>

namespace djmix
{

// Tempo from taps: the slope of a straight line through the last few tap
// times, so one early or late tap moves it little. A pause of more than two
// seconds starts a new count.
class TapTempo
{
public:
    static constexpr double resetAfterSeconds = 2.0;
    static constexpr size_t minimumTaps = 4;
    static constexpr size_t maximumTaps = 16;

    // Records a tap at `seconds` (any steady clock) and returns the tempo once
    // there are enough taps.
    std::optional<double> tap (double seconds);
    void reset() { taps.clear(); }
    size_t count() const { return taps.size(); }

private:
    std::vector<double> taps;
};

} // namespace djmix
