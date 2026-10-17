#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include <SQLiteCpp/SQLiteCpp.h>

#include "library/Camelot.h"
#include "library/ImportScanner.h"
#include "library/LibraryDatabase.h"
#include "library/LibraryFilter.h"
#include "library/TrackColours.h"

using namespace djmix;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    fs::path path;

    TempDir()
    {
        path = fs::temp_directory_path() / ("djmix-test-" + std::to_string (std::rand()) + "-"
                                            + std::to_string (reinterpret_cast<std::uintptr_t> (this)));
        fs::create_directories (path);
    }

    ~TempDir() { fs::remove_all (path); }

    fs::path touch (const fs::path& relative) const
    {
        const auto file = path / relative;
        fs::create_directories (file.parent_path());
        std::ofstream (file) << "x";
        return file;
    }
};

CamelotKey key (const char* text)
{
    return *parseCamelot (text);
}
} // namespace

TEST_CASE ("Camelot codes parse and print", "[library][camelot]")
{
    CHECK (parseCamelot ("8A") == CamelotKey { 8, true });
    CHECK (parseCamelot ("12b") == CamelotKey { 12, false });
    CHECK_FALSE (parseCamelot ("13A"));
    CHECK_FALSE (parseCamelot ("0B"));
    CHECK_FALSE (parseCamelot ("8"));
    CHECK_FALSE (parseCamelot (""));
    CHECK_FALSE (parseCamelot ("8C"));
    CHECK (toString (key ("11A")) == "11A");
}

TEST_CASE ("Camelot relations follow the design spec", "[library][camelot]")
{
    CHECK (relation (key ("8A"), key ("8A")) == KeyRelation::compatible);
    CHECK (relation (key ("8A"), key ("9A")) == KeyRelation::compatible);
    CHECK (relation (key ("8A"), key ("7A")) == KeyRelation::compatible);
    CHECK (relation (key ("1A"), key ("12A")) == KeyRelation::compatible); // the wheel wraps
    CHECK (relation (key ("8A"), key ("8B")) == KeyRelation::compatible);  // relative major/minor

    CHECK (relation (key ("8A"), key ("10A")) == KeyRelation::caution);
    CHECK (relation (key ("2B"), key ("12B")) == KeyRelation::caution);
    CHECK (relation (key ("8A"), key ("9B")) == KeyRelation::caution);

    CHECK (relation (key ("8A"), key ("11A")) == KeyRelation::clash);
    CHECK (relation (key ("8A"), key ("2A")) == KeyRelation::clash);
    CHECK (relation (key ("8A"), key ("10B")) == KeyRelation::clash);

    CHECK (wheelDistance (1, 7) == 6);
    CHECK (wheelDistance (11, 2) == 3);
}

TEST_CASE ("New tracks take the least used colour", "[library][colour]")
{
    CHECK (nextTrackColour ({}) == 0);
    CHECK (nextTrackColour ({ 1, 1, 0, 1 }) == 2);
    CHECK (nextTrackColour ({ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }) == 0);
    CHECK (nextTrackColour ({ 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }) == 1);
    CHECK (trackPalette.size() == 12);
}

TEST_CASE ("The library database stores tracks and their analysis", "[library][database]")
{
    TempDir dir;
    const auto file = dir.path / "library.sqlite";

    int64_t id = 0;
    {
        LibraryDatabase db (file);
        const auto added = db.addTrack (dir.path / "Ünïcode Artist - Title.flac", "Title", "Ünïcode Artist", 3);
        REQUIRE (added);
        id = *added;
        CHECK_FALSE (db.addTrack (dir.path / "Ünïcode Artist - Title.flac", "Again", "", 0));
        CHECK (db.containsPath (dir.path / "Ünïcode Artist - Title.flac"));

        const auto track = db.track (id);
        REQUIRE (track);
        CHECK (track->status == AnalysisStatus::pending);
        CHECK (track->colour == 3);
        CHECK_FALSE (track->bpm);

        db.setStatus (id, AnalysisStatus::failed, "no decoder");
        CHECK (db.track (id)->error == "no decoder");

        db.setAnalysis (id, { "sha256:abc", 124.0, "A minor", "8A", 372.4, 0.93, dir.path / "a.json", "0.1.2" });
        db.setColour (id, 7);
    }

    // Reopening reads the same data back.
    LibraryDatabase reopened (file);
    const auto tracks = reopened.allTracks();
    REQUIRE (tracks.size() == 1);
    const auto& t = tracks.front();
    CHECK (t.id == id);
    CHECK (t.path == dir.path / "Ünïcode Artist - Title.flac");
    CHECK (t.artist == "Ünïcode Artist");
    CHECK (t.status == AnalysisStatus::ready);
    CHECK (t.error.empty());
    CHECK (t.fileHash == "sha256:abc");
    CHECK (*t.bpm == Catch::Approx (124.0));
    CHECK (t.camelot == "8A");
    CHECK (t.keyName == "A minor");
    CHECK (*t.durationSec == Catch::Approx (372.4));
    CHECK (*t.beatConfidence == Catch::Approx (0.93));
    CHECK (t.analysisFile == dir.path / "a.json");
    CHECK (t.analyzerVersion == "0.1.2");
    CHECK (t.colour == 7);

    const auto usage = reopened.colourUsage();
    REQUIRE (usage.size() == 12);
    CHECK (usage[7] == 1);

    reopened.removeTrack (id);
    CHECK (reopened.allTracks().empty());
}

TEST_CASE ("The library migrates a version 1 database", "[library][database]")
{
    TempDir dir;
    const auto file = dir.path / "library.sqlite";
    {
        // The version 1 schema, with one analysed track.
        SQLite::Database raw (pathToUtf8 (file), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        raw.exec (R"sql(
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
            INSERT INTO tracks (path, title, status, bpm, analysis_file) VALUES ('/music/a.flac', 'A', 'ready', 128, '/cache/a.json');
            PRAGMA user_version = 1;
        )sql");
    }

    LibraryDatabase db (file);
    const auto tracks = db.allTracks();
    REQUIRE (tracks.size() == 1);
    CHECK (tracks[0].title == "A");
    CHECK (tracks[0].status == AnalysisStatus::ready);
    CHECK (*tracks[0].bpm == Catch::Approx (128.0));
    CHECK (tracks[0].analyzerVersion.empty());

    db.setAnalysis (tracks[0].id, { "sha256:a", 128.0, "", "", 1.0, 0.9, "/cache/a.json", "0.1.2" });
    CHECK (db.track (tracks[0].id)->analyzerVersion == "0.1.2");

    SQLite::Database raw (pathToUtf8 (file), SQLite::OPEN_READONLY);
    CHECK (raw.execAndGet ("PRAGMA user_version").getInt() == LibraryDatabase::schemaVersion);
}

TEST_CASE ("The library refuses a database from a newer app", "[library][database]")
{
    TempDir dir;
    const auto file = dir.path / "library.sqlite";
    {
        LibraryDatabase db (file);
    }
    {
        SQLite::Database raw (pathToUtf8 (file), SQLite::OPEN_READWRITE);
        raw.exec ("PRAGMA user_version = " + std::to_string (LibraryDatabase::schemaVersion + 1));
    }
    CHECK_THROWS (LibraryDatabase (file));
}

TEST_CASE ("The library filter matches BPM ranges and compatible keys", "[library][filter]")
{
    TrackRecord house;
    house.bpm = 124.0;
    house.camelot = "8A";
    TrackRecord techno;
    techno.bpm = 132.0;
    techno.camelot = "11A";
    TrackRecord unanalysed;

    LibraryFilter filter;
    CHECK_FALSE (filter.isActive());
    CHECK (filter.matches (unanalysed));

    filter.minBpm = 120.0;
    filter.maxBpm = 128.0;
    CHECK (filter.matches (house));
    CHECK_FALSE (filter.matches (techno));
    CHECK_FALSE (filter.matches (unanalysed));

    filter = {};
    filter.compatibleWith = key ("9A");
    CHECK (filter.matches (house));
    CHECK_FALSE (filter.matches (techno));
    CHECK_FALSE (filter.matches (unanalysed));
}

TEST_CASE ("Import finds audio files in folders", "[library][import]")
{
    TempDir dir;
    const auto a = dir.touch ("crate/b.FLAC");
    const auto b = dir.touch ("crate/sub/a.mp3");
    const auto c = dir.touch ("single.m4a");
    dir.touch ("crate/cover.jpg");
    dir.touch ("crate/.hidden.wav");
    dir.touch ("crate/.cache/c.wav");

    const auto found = collectAudioFiles ({ dir.path / "crate", c, c, dir.path / "missing" });
    REQUIRE (found.size() == 3);
    CHECK (std::find (found.begin(), found.end(), a) != found.end());
    CHECK (std::find (found.begin(), found.end(), b) != found.end());
    CHECK (std::find (found.begin(), found.end(), c) != found.end());
    CHECK (std::is_sorted (found.begin(), found.end()));
}

TEST_CASE ("Track names come from file names", "[library][import]")
{
    auto name = [] (const char* file) { return trackNameFromFile (fs::path ("/music") / pathFromUtf8 (file)); };

    CHECK (name ("Artist - Title.flac").artist == "Artist");
    CHECK (name ("Artist - Title.flac").title == "Title");
    CHECK (name ("01 - Artist - Title (Extended Mix).mp3").artist == "Artist");
    CHECK (name ("01 - Artist - Title (Extended Mix).mp3").title == "Title (Extended Mix)");
    CHECK (name ("07. Title Only.wav").title == "Title Only");
    CHECK (name ("07. Title Only.wav").artist.empty());
    CHECK (name ("808 State - Pacific.aiff").artist == "808 State");
    CHECK (name ("50 Cent - In Da Club.mp3").artist == "50 Cent");
    CHECK (name ("10cc - Dreadlock Holiday.flac").artist == "10cc");
    CHECK (name ("Untitled.wav").title == "Untitled");
}
