#include "LibraryFilter.h"

namespace djmix
{

bool LibraryFilter::matches (const TrackRecord& track) const
{
    if (minBpm || maxBpm)
    {
        if (! track.bpm)
            return false;
        if (minBpm && *track.bpm < *minBpm)
            return false;
        if (maxBpm && *track.bpm > *maxBpm)
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
