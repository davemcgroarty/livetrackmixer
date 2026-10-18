#include "LibraryDatabase.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include "TrackColours.h"

namespace djmix
{

namespace
{
TrackRecord readRecord (SQLite::Statement& row)
{
    TrackRecord t;
    t.id = row.getColumn ("id").getInt64();
    t.path = pathFromUtf8 (row.getColumn ("path").getString());
    t.fileHash = row.getColumn ("file_hash").getString();
    t.title = row.getColumn ("title").getString();
    t.artist = row.getColumn ("artist").getString();
    t.colour = row.getColumn ("colour").getInt();
    t.status = analysisStatusFrom (row.getColumn ("status").getString());
    t.error = row.getColumn ("error").getString();

    if (! row.getColumn ("bpm").isNull())
        t.bpm = row.getColumn ("bpm").getDouble();
    t.keyName = row.getColumn ("key_name").getString();
    t.camelot = row.getColumn ("camelot").getString();
    if (! row.getColumn ("duration_sec").isNull())
        t.durationSec = row.getColumn ("duration_sec").getDouble();
    if (! row.getColumn ("beat_confidence").isNull())
        t.beatConfidence = row.getColumn ("beat_confidence").getDouble();
    t.analysisFile = pathFromUtf8 (row.getColumn ("analysis_file").getString());
    t.analyzerVersion = row.getColumn ("analyzer_version").getString();

    if (! row.getColumn ("override_bpm").isNull())
        t.overrides.grid.bpm = row.getColumn ("override_bpm").getDouble();
    if (! row.getColumn ("grid_offset_sec").isNull())
        t.overrides.grid.offsetSeconds = row.getColumn ("grid_offset_sec").getDouble();
    if (! row.getColumn ("downbeat_shift").isNull())
        t.overrides.grid.downbeatShift = row.getColumn ("downbeat_shift").getInt();
    if (! row.getColumn ("section_starts").isNull())
        t.overrides.sectionStarts = sectionStartsFromJson (row.getColumn ("section_starts").getString());
    return t;
}

constexpr const char* selectColumns =
    "SELECT id, path, file_hash, title, artist, colour, status, error, bpm, key_name, camelot, "
    "duration_sec, beat_confidence, analysis_file, analyzer_version, "
    "override_bpm, grid_offset_sec, downbeat_shift, section_starts "
    "FROM tracks LEFT JOIN track_overrides ON track_overrides.track_id = tracks.id";
} // namespace

std::string pathToUtf8 (const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return { u8.begin(), u8.end() };
}

std::filesystem::path pathFromUtf8 (const std::string& text)
{
    return std::filesystem::path (std::u8string (text.begin(), text.end()));
}

std::string_view toString (AnalysisStatus status)
{
    switch (status)
    {
        case AnalysisStatus::pending: return "pending";
        case AnalysisStatus::analysing: return "analysing";
        case AnalysisStatus::ready: return "ready";
        case AnalysisStatus::failed: return "failed";
    }
    return "pending";
}

AnalysisStatus analysisStatusFrom (std::string_view text)
{
    if (text == "analysing")
        return AnalysisStatus::analysing;
    if (text == "ready")
        return AnalysisStatus::ready;
    if (text == "failed")
        return AnalysisStatus::failed;
    return AnalysisStatus::pending;
}

LibraryDatabase::LibraryDatabase (const std::filesystem::path& file)
    : db (std::make_unique<SQLite::Database> (pathToUtf8 (file), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE))
{
    db->exec ("PRAGMA foreign_keys = ON");
    db->exec ("PRAGMA journal_mode = WAL");
    migrate();
}

LibraryDatabase::~LibraryDatabase() = default;

void LibraryDatabase::migrate()
{
    const int version = db->execAndGet ("PRAGMA user_version").getInt();
    if (version > schemaVersion)
        throw std::runtime_error ("the library was written by a newer version of the app");

    SQLite::Transaction transaction (*db);
    if (version < 1)
    {
        db->exec (R"sql(
            CREATE TABLE tracks (
                id              INTEGER PRIMARY KEY,
                path            TEXT NOT NULL UNIQUE,
                file_hash       TEXT NOT NULL DEFAULT '',
                title           TEXT NOT NULL DEFAULT '',
                artist          TEXT NOT NULL DEFAULT '',
                colour          INTEGER NOT NULL DEFAULT 0,
                status          TEXT NOT NULL DEFAULT 'pending',
                error           TEXT NOT NULL DEFAULT '',
                bpm             REAL,
                key_name        TEXT NOT NULL DEFAULT '',
                camelot         TEXT NOT NULL DEFAULT '',
                duration_sec    REAL,
                beat_confidence REAL,
                analysis_file   TEXT NOT NULL DEFAULT '',
                added_at        TEXT NOT NULL DEFAULT (datetime('now'))
            );
            CREATE INDEX tracks_by_hash ON tracks (file_hash);
        )sql");
    }
    if (version < 2)
    {
        // Lets the app spot analyses written by an older djanalyze. Rows from
        // version 1 read as unknown, so they are checked once more.
        db->exec ("ALTER TABLE tracks ADD COLUMN analyzer_version TEXT NOT NULL DEFAULT ''");
    }
    if (version < 3)
    {
        // Hand corrections live apart from the analysis columns, which
        // re-analysis overwrites.
        db->exec (R"sql(
            CREATE TABLE track_overrides (
                track_id        INTEGER PRIMARY KEY REFERENCES tracks (id) ON DELETE CASCADE,
                override_bpm    REAL,
                grid_offset_sec REAL NOT NULL DEFAULT 0,
                downbeat_shift  INTEGER NOT NULL DEFAULT 0,
                section_starts  TEXT
            );
        )sql");
    }
    db->exec ("PRAGMA user_version = " + std::to_string (schemaVersion));
    transaction.commit();
}

std::optional<int64_t> LibraryDatabase::addTrack (const std::filesystem::path& path,
                                                  const std::string& title,
                                                  const std::string& artist,
                                                  int colour)
{
    SQLite::Statement insert (*db,
                              "INSERT OR IGNORE INTO tracks (path, title, artist, colour) VALUES (?, ?, ?, ?)");
    insert.bind (1, pathToUtf8 (path));
    insert.bind (2, title);
    insert.bind (3, artist);
    insert.bind (4, colour);
    if (insert.exec() == 0)
        return std::nullopt;
    return db->getLastInsertRowid();
}

std::vector<TrackRecord> LibraryDatabase::allTracks() const
{
    SQLite::Statement query (*db, std::string (selectColumns) + " ORDER BY tracks.id");
    std::vector<TrackRecord> tracks;
    while (query.executeStep())
        tracks.push_back (readRecord (query));
    return tracks;
}

std::optional<TrackRecord> LibraryDatabase::track (int64_t id) const
{
    SQLite::Statement query (*db, std::string (selectColumns) + " WHERE tracks.id = ?");
    query.bind (1, id);
    if (! query.executeStep())
        return std::nullopt;
    return readRecord (query);
}

bool LibraryDatabase::containsPath (const std::filesystem::path& path) const
{
    SQLite::Statement query (*db, "SELECT 1 FROM tracks WHERE path = ?");
    query.bind (1, pathToUtf8 (path));
    return query.executeStep();
}

void LibraryDatabase::setStatus (int64_t id, AnalysisStatus status, const std::string& error)
{
    SQLite::Statement update (*db, "UPDATE tracks SET status = ?, error = ? WHERE id = ?");
    update.bind (1, std::string (toString (status)));
    update.bind (2, error);
    update.bind (3, id);
    update.exec();
}

void LibraryDatabase::setAnalysis (int64_t id, const AnalysisSummary& s)
{
    SQLite::Statement update (*db, R"sql(
        UPDATE tracks SET status = 'ready', error = '', file_hash = ?, bpm = ?, key_name = ?, camelot = ?,
                          duration_sec = ?, beat_confidence = ?, analysis_file = ?, analyzer_version = ?
        WHERE id = ?)sql");
    update.bind (1, s.fileHash);
    update.bind (2, s.bpm);
    update.bind (3, s.keyName);
    update.bind (4, s.camelot);
    update.bind (5, s.durationSec);
    update.bind (6, s.beatConfidence);
    update.bind (7, pathToUtf8 (s.analysisFile));
    update.bind (8, s.analyzerVersion);
    update.bind (9, id);
    update.exec();
}

void LibraryDatabase::setColour (int64_t id, int colour)
{
    SQLite::Statement update (*db, "UPDATE tracks SET colour = ? WHERE id = ?");
    update.bind (1, colour);
    update.bind (2, id);
    update.exec();
}

void LibraryDatabase::setOverrides (int64_t id, const TrackOverrides& o)
{
    if (o.isEmpty())
    {
        SQLite::Statement remove (*db, "DELETE FROM track_overrides WHERE track_id = ?");
        remove.bind (1, id);
        remove.exec();
        return;
    }

    SQLite::Statement upsert (*db, R"sql(
        INSERT OR REPLACE INTO track_overrides (track_id, override_bpm, grid_offset_sec, downbeat_shift, section_starts)
        VALUES (?, ?, ?, ?, ?))sql");
    upsert.bind (1, id);
    if (o.grid.bpm)
        upsert.bind (2, *o.grid.bpm);
    else
        upsert.bind (2);
    upsert.bind (3, o.grid.offsetSeconds);
    upsert.bind (4, o.grid.downbeatShift);
    if (o.sectionStarts)
        upsert.bind (5, sectionStartsToJson (*o.sectionStarts));
    else
        upsert.bind (5);
    upsert.exec();
}

void LibraryDatabase::removeTrack (int64_t id)
{
    SQLite::Statement remove (*db, "DELETE FROM tracks WHERE id = ?");
    remove.bind (1, id);
    remove.exec();
}

std::vector<int> LibraryDatabase::colourUsage() const
{
    std::vector<int> usage (trackPalette.size(), 0);
    SQLite::Statement query (*db, "SELECT colour, COUNT(*) FROM tracks GROUP BY colour");
    while (query.executeStep())
    {
        const int colour = query.getColumn (0).getInt();
        if (colour >= 0 && colour < static_cast<int> (usage.size()))
            usage[static_cast<size_t> (colour)] = query.getColumn (1).getInt();
    }
    return usage;
}

} // namespace djmix
