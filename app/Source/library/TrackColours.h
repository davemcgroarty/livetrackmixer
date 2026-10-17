#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace djmix
{

// The fixed palette new tracks take their colour from: 12 hues 30° apart,
// at one saturation and lightness so no track stands out from the others.
struct TrackColour
{
    std::string_view name;
    uint32_t argb;
};

inline constexpr std::array<TrackColour, 12> trackPalette {
    TrackColour { "Red", 0xffe05a4f },
    TrackColour { "Orange", 0xffe0884f },
    TrackColour { "Amber", 0xffe0b54f },
    TrackColour { "Lime", 0xffb5e04f },
    TrackColour { "Green", 0xff6fd16a },
    TrackColour { "Teal", 0xff4fe0a6 },
    TrackColour { "Cyan", 0xff4fd2e0 },
    TrackColour { "Sky", 0xff4f9fe0 },
    TrackColour { "Blue", 0xff6a73e0 },
    TrackColour { "Violet", 0xff9a5fe0 },
    TrackColour { "Magenta", 0xffd35fe0 },
    TrackColour { "Pink", 0xffe05f9a },
};

// Palette index for a new track: the least used colour, the lowest index on a tie.
// usage[i] is how many tracks use colour i (shorter vectors count as zero).
int nextTrackColour (const std::vector<int>& usage);

} // namespace djmix
