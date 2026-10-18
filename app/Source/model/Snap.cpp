#include "Snap.h"

#include <cmath>

namespace djmix
{

double snapStep (SnapMode mode)
{
    switch (mode)
    {
        case SnapMode::off: return 0.0;
        case SnapMode::beat: return 1.0;
        case SnapMode::bar: return beatsPerBar;
        case SnapMode::phrase: return beatsPerPhrase;
    }
    return 0.0;
}

double snapBeat (double beat, SnapMode mode)
{
    const double step = snapStep (mode);
    return step > 0.0 ? std::round (beat / step) * step : beat;
}

const char* snapModeLabel (SnapMode mode)
{
    switch (mode)
    {
        case SnapMode::off: return "Off";
        case SnapMode::beat: return "Beat";
        case SnapMode::bar: return "Bar";
        case SnapMode::phrase: return "Phrase";
    }
    return "";
}

const char* snapModeKey (SnapMode mode)
{
    switch (mode)
    {
        case SnapMode::off: return "off";
        case SnapMode::beat: return "beat";
        case SnapMode::bar: return "bar";
        case SnapMode::phrase: return "phrase";
    }
    return "";
}

std::optional<SnapMode> snapModeFromKey (const std::string& key)
{
    for (const auto mode : { SnapMode::off, SnapMode::beat, SnapMode::bar, SnapMode::phrase })
        if (key == snapModeKey (mode))
            return mode;
    return std::nullopt;
}

} // namespace djmix
