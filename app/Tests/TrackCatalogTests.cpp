#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "TestPaths.h"
#include "library/TrackCatalog.h"
#include "view/EffectiveAnalysis.h"

using Catch::Approx;
using djmix::TrackCatalog;
using djmix::TrackRecord;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    fs::path dir = fs::temp_directory_path() / "djmix-catalog-test";
    TempDir()
    {
        fs::remove_all (dir);
        fs::create_directories (dir);
    }
    ~TempDir() { fs::remove_all (dir); }
};

TrackRecord recordFor (const fs::path& analysisFile)
{
    TrackRecord t;
    t.id = 4;
    t.path = "music/track.wav";
    t.title = "Track";
    t.colour = 3;
    t.status = djmix::AnalysisStatus::ready;
    t.analysisFile = analysisFile;
    t.analyzerVersion = "0.1.3";
    return t;
}
} // namespace

TEST_CASE ("The catalog loads a track's analysis once, with its overrides applied", "[library][catalog]")
{
    TempDir temp;
    const auto file = temp.dir / "analysis.json";
    fs::copy_file (djmix::test::schemaPath ("examples/analysis.v1.example.json"), file);

    TrackCatalog catalog;
    auto track = recordFor (file);
    const auto first = catalog.get (track);
    REQUIRE (first != nullptr);
    CHECK (first->trackId == 4);
    CHECK (first->colour == 3);
    CHECK (first->analysis.beats.size() == 8);
    CHECK (first->waveform->empty()); // the example's waveform file is not there
    CHECK (catalog.get (track) == first); // kept

    // New overrides: the same files, the effective grid again.
    track.overrides = djmix::edits::nudge ({}, 0.1);
    const auto nudged = catalog.get (track);
    REQUIRE (nudged != nullptr);
    CHECK (nudged != first);
    CHECK (nudged->analysis.beats[0] == Approx (first->analysis.beats[0] + 0.1));
    CHECK (nudged->waveform == first->waveform);

    // Gone, or not analysed: nothing, and why.
    fs::remove (file);
    track.analyzerVersion = "0.1.4";
    CHECK (catalog.get (track) == nullptr);
    CHECK (catalog.errorFor (4) == "not analysed yet");
    CHECK (catalog.errorFor (99).empty());
}
