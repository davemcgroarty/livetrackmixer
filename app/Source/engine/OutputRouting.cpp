#include "OutputRouting.h"

#include <bit>

namespace djmix
{

namespace
{
constexpr int maxChannels = 64;

bool isActive (uint64_t active, int channel) noexcept
{
    return channel >= 0 && channel < maxChannels && ((active >> channel) & 1u) != 0;
}

// Position of a device channel among the active ones, or -1 if it is not active.
int activeIndex (uint64_t active, int channel) noexcept
{
    if (! isActive (active, channel))
        return -1;
    const uint64_t below = channel == 0 ? 0 : (active & ((uint64_t { 1 } << channel) - 1));
    return std::popcount (below);
}
} // namespace

OutputRouting OutputRouting::defaultFor (int numOutputs)
{
    OutputRouting r;
    r.splitCue = numOutputs < 4;
    return r;
}

ResolvedRouting resolveRouting (const OutputRouting& routing, uint64_t active) noexcept
{
    ResolvedRouting r;
    const int masterLeft = activeIndex (active, routing.masterFirst);
    const int masterRight = activeIndex (active, routing.masterFirst + 1);
    const int cueLeft = activeIndex (active, routing.cueFirst);
    const int cueRight = activeIndex (active, routing.cueFirst + 1);

    const bool cuePairAvailable = cueLeft >= 0 && cueRight >= 0 && routing.cueFirst != routing.masterFirst;
    if (! routing.splitCue && cuePairAvailable)
    {
        r.masterLeft = masterLeft;
        r.masterRight = masterRight;
        r.cueLeft = cueLeft;
        r.cueRight = cueRight;
        return r;
    }

    // Split cue on the Master pair; if that pair is not there either, on the
    // first active pair.
    int left = masterLeft, right = masterRight;
    if (left < 0 || right < 0)
    {
        left = active != 0 ? 0 : -1;
        right = std::popcount (active) > 1 ? 1 : -1;
    }
    r.split = true;
    r.cueLeft = left;
    r.masterRight = right;
    return r;
}

uint64_t channelsNeeded (const OutputRouting& routing) noexcept
{
    auto pair = [] (int first) -> uint64_t
    {
        if (first < 0 || first + 1 >= maxChannels)
            return 0;
        return (uint64_t { 3 } << first);
    };
    return pair (routing.masterFirst) | (routing.splitCue ? 0 : pair (routing.cueFirst));
}

std::vector<int> stereoPairs (int numOutputs)
{
    std::vector<int> pairs;
    for (int first = 0; first + 1 < numOutputs && first + 1 < maxChannels; first += 2)
        pairs.push_back (first);
    return pairs;
}

std::string pairName (int first)
{
    return std::to_string (first + 1) + "-" + std::to_string (first + 2);
}

} // namespace djmix
