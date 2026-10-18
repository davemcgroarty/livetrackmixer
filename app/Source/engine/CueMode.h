#pragma once

#include <string_view>

namespace djmix
{

// What a cue does when it reaches the end of the section or phrase it plays.
enum class CueMode
{
    once,         // stop
    loop,         // go back to its start
    continueTrack // play on through the rest of the track
};

// The next mode when the mode button is clicked: once, loop, continue, once...
constexpr CueMode nextCueMode (CueMode mode)
{
    switch (mode)
    {
        case CueMode::once: return CueMode::loop;
        case CueMode::loop: return CueMode::continueTrack;
        case CueMode::continueTrack: return CueMode::once;
    }
    return CueMode::once;
}

// "Once", "Loop" or "Continue", as the button shows it.
constexpr std::string_view cueModeLabel (CueMode mode)
{
    switch (mode)
    {
        case CueMode::once: return "Once";
        case CueMode::loop: return "Loop";
        case CueMode::continueTrack: return "Continue";
    }
    return "Once";
}

// For the settings file: "once", "loop" or "continue"; anything else is once.
constexpr std::string_view cueModeKey (CueMode mode)
{
    switch (mode)
    {
        case CueMode::once: return "once";
        case CueMode::loop: return "loop";
        case CueMode::continueTrack: return "continue";
    }
    return "once";
}

constexpr CueMode cueModeFromKey (std::string_view key)
{
    if (key == "loop")
        return CueMode::loop;
    if (key == "continue")
        return CueMode::continueTrack;
    return CueMode::once;
}

} // namespace djmix
