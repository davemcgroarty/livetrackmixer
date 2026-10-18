#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace djmix
{

// Where Master and Cue go on the output device (design spec, "Audio outputs
// and cueing"). Channels are numbered from 0 on the device.
//
// Normally each bus has its own stereo pair: Master on 1-2 and Cue on 3-4.
// With split cue, one stereo pair carries both: Cue summed to mono on the
// left, Master in mono on the right, for practising on one pair of headphones.
struct OutputRouting
{
    int masterFirst = 0;
    int cueFirst = 2;
    bool splitCue = false;

    // Separate pairs when the device has at least four outputs, split cue otherwise.
    static OutputRouting defaultFor (int numOutputs);

    bool operator== (const OutputRouting&) const = default;
};

// The routing as indices into the audio callback's channel array, which holds
// only the device's active output channels, in order. -1 means "not sent".
struct ResolvedRouting
{
    int masterLeft = -1;
    int masterRight = -1;
    int cueLeft = -1;
    int cueRight = -1;
    bool split = false; // Cue mono on cueLeft, Master mono on masterRight

    bool operator== (const ResolvedRouting&) const = default;
};

// Resolves a routing against the active output channels (bit n = device
// channel n). Falls back to split cue on the Master pair when the Cue pair
// is not active, so auditions are never silently lost. No allocation: safe on
// the audio thread.
ResolvedRouting resolveRouting (const OutputRouting& routing, uint64_t activeOutputs) noexcept;

// The device channels a routing needs switched on.
uint64_t channelsNeeded (const OutputRouting& routing) noexcept;

// "1-2", "3-4", ... for a device with `numOutputs` outputs (pairs only).
std::vector<int> stereoPairs (int numOutputs);
std::string pairName (int first);

} // namespace djmix
