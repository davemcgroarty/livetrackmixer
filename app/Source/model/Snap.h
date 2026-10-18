#pragma once

#include <optional>
#include <string>

namespace djmix
{

// Arrangement timing is 4/4 from beat 0: a bar is 4 beats, a phrase 8 bars.
constexpr double beatsPerBar = 4.0;
constexpr double beatsPerPhrase = 32.0;

// What clips snap to (design spec: off, beat, bar, phrase; keys 1-4).
enum class SnapMode
{
    off,
    beat,
    bar,
    phrase
};

// The snap step in beats; 0 for off.
double snapStep (SnapMode mode);

// The nearest multiple of the step (the beat itself when off).
double snapBeat (double beat, SnapMode mode);

const char* snapModeLabel (SnapMode mode);
const char* snapModeKey (SnapMode mode); // for settings
std::optional<SnapMode> snapModeFromKey (const std::string& key);

} // namespace djmix
