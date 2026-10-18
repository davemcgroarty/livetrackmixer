#pragma once

#include <optional>

#include "Camelot.h"
#include "LibraryDatabase.h"

namespace djmix
{

// The Library table's filter: a BPM range and, optionally, only tracks whose
// key mixes well with a given key (same, ±1 on the wheel, relative major/minor).
// Tracks without an analysis pass only when no filter needs one.
struct LibraryFilter
{
    std::optional<double> minBpm;
    std::optional<double> maxBpm;
    std::optional<CamelotKey> compatibleWith;

    bool isActive() const { return minBpm || maxBpm || compatibleWith; }
    bool matches (const TrackRecord& track) const;
};

} // namespace djmix
