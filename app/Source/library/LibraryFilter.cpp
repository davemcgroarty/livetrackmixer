#include "LibraryFilter.h"

namespace djmix
{

bool LibraryFilter::matches (const TrackRecord& track) const
{
    if (minBpm || maxBpm)
    {
        const auto bpm = track.effectiveBpm();
        if (! bpm)
            return false;
        if (minBpm && *bpm < *minBpm)
            return false;
        if (maxBpm && *bpm > *maxBpm)
            return false;
    }

    if (compatibleWith)
    {
        const auto key = parseCamelot (track.camelot);
        if (! key || relation (*key, *compatibleWith) != KeyRelation::compatible)
            return false;
    }

    return true;
}

} // namespace djmix
