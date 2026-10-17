#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace SQLite
{
class Database;
}

namespace djmix
{

enum class AnalysisStatus
{
    pending,   // waiting for the analyzer
    analysing, // the analyzer is running on it
    ready,     // analysis loaded
    failed     // the analyzer reported an error (see TrackRecord::error)
};

// One row of the track library. The analysis itself lives in the cache
// directory; the library keeps the few fields the table shows and sorts by.
struct TrackRecord
{
    int64_t id = 0;
    std::filesystem::path path;
    std::string fileHash; // "sha256:<hex>", empty until analysed
    std::string title;
    std::string artist;
    int colour = 0; // index into trackPalette
    AnalysisStatus status = AnalysisStatus::pending;
    std::string error;

    std::optional<double> bpm;
    std::string keyName;
    std::string camelot;
    std::optional<double> durationSec;
    std::optional<double> beatConfidence;
    std::filesystem::path analysisFile;
};

struct AnalysisSummary
{
    std::string fileHash;
    double bpm = 0.0;
    std::string keyName;
    std::string camelot;
    double durationSec = 0.0;
    double beatConfidence = 0.0;
    std::filesystem::path analysisFile;
};

// The SQLite track library. Not thread-safe: use it from one thread (the
// message thread in the app).
class LibraryDatabase
{
public:
    static constexpr int schemaVersion = 1;

    // Opens or creates the library at `file`, migrating older schemas.
    explicit LibraryDatabase (const std::filesystem::path& file);
    ~LibraryDatabase();

    LibraryDatabase (const LibraryDatabase&) = delete;
    LibraryDatabase& operator= (const LibraryDatabase&) = delete;

    // Adds a pending track. Returns nothing if the path is already in the library.
    std::optional<int64_t> addTrack (const std::filesystem::path& path,
                                     const std::string& title,
                                     const std::string& artist,
                                     int colour);

    std::vector<TrackRecord> allTracks() const;
    std::optional<TrackRecord> track (int64_t id) const;
    bool containsPath (const std::filesystem::path& path) const;

    void setStatus (int64_t id, AnalysisStatus status, const std::string& error = {});
    void setAnalysis (int64_t id, const AnalysisSummary& summary); // also sets status to ready
    void setColour (int64_t id, int colour);
    void removeTrack (int64_t id);

    // How many tracks use each palette colour.
    std::vector<int> colourUsage() const;

private:
    void migrate();

    std::unique_ptr<SQLite::Database> db;
};

// Paths are stored as UTF-8 so non-ASCII file names survive on every platform.
std::string pathToUtf8 (const std::filesystem::path& path);
std::filesystem::path pathFromUtf8 (const std::string& text);

std::string_view toString (AnalysisStatus status);
AnalysisStatus analysisStatusFrom (std::string_view text);

} // namespace djmix
