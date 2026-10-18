#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "LibraryDatabase.h"
#include "analysis/Analysis.h"
#include "view/WaveformPyramid.h"

namespace djmix
{

// What the arrangement needs of a library track: its effective analysis (the
// analysis with the track's grid and section overrides applied) and its
// waveform.
struct TrackMaterial
{
    int64_t trackId = 0;
    std::filesystem::path file;
    std::string fileHash;
    std::string title;
    int colour = 0;           // index into trackPalette
    TrackAnalysis analysis;   // effective
    std::shared_ptr<const WaveformPyramid> waveform; // never null; empty if the file could not be read
};

// Loads tracks' material on first use and keeps it, loading again only when
// a track's record changes (a new analysis, new overrides, a new colour).
// Message thread only.
class TrackCatalog
{
public:
    // The material for a track; null while it has no readable analysis
    // (errorFor says why, if it was tried).
    std::shared_ptr<const TrackMaterial> get (const TrackRecord& track);
    std::string errorFor (int64_t trackId) const;

    // Drops what is kept for tracks no longer in the library.
    void forget (int64_t trackId) { entries.erase (trackId); }

private:
    struct Key
    {
        std::filesystem::path path;
        std::filesystem::path analysisFile;
        std::string analyzerVersion;
        std::string fileHash;
        std::string title;
        int colour = 0;
        TrackOverrides overrides;

        bool operator== (const Key&) const = default;
    };

    struct Entry
    {
        Key key;
        std::shared_ptr<const TrackAnalysis> base; // as analysed
        std::shared_ptr<const WaveformPyramid> waveform;
        std::shared_ptr<const TrackMaterial> material;
        std::string error;
    };

    static Key keyFor (const TrackRecord& track);

    std::map<int64_t, Entry> entries;
};

} // namespace djmix
