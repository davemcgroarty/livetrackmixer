#include "TrackColours.h"

namespace djmix
{

int nextTrackColour (const std::vector<int>& usage)
{
    int best = 0;
    int bestCount = -1;
    for (int i = 0; i < static_cast<int> (trackPalette.size()); ++i)
    {
        const int count = i < static_cast<int> (usage.size()) ? usage[static_cast<size_t> (i)] : 0;
        if (bestCount < 0 || count < bestCount)
        {
            best = i;
            bestCount = count;
        }
    }
    return best;
}

} // namespace djmix
