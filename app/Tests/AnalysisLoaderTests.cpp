#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.h"
#include "analysis/AnalysisLoader.h"
#include "analysis/WaveformFile.h"

using djmix::AnalysisLoader;
using djmix::AnalysisLoadError;
using djmix::TrackAnalysis;

namespace
{
nlohmann::json example()
{
    return djmix::test::readJson (djmix::test::schemaPath ("examples/analysis.v1.example.json"));
}

std::string errorOf (const djmix::AnalysisLoadResult& result)
{
    const auto* error = std::get_if<AnalysisLoadError> (&result);
    return error == nullptr ? std::string() : error->message;
}
} // namespace

TEST_CASE ("AnalysisLoader reads the example file", "[analysis]")
{
    const auto result = AnalysisLoader::fromFile (djmix::test::schemaPath ("examples/analysis.v1.example.json"));
    INFO (errorOf (result));
    REQUIRE (std::holds_alternative<TrackAnalysis> (result));
    const auto& a = std::get<TrackAnalysis> (result);

    CHECK (a.schemaVersion == 1);
    CHECK (a.bpm == Catch::Approx (120.0));
    CHECK_FALSE (a.tempoVariable);
    CHECK (a.key.camelot == "8A");
    REQUIRE (a.integratedLufs.has_value());
    CHECK (*a.integratedLufs == Catch::Approx (-8.1));
    CHECK (a.beats.size() == 8);
    REQUIRE (a.bars.size() == 2);
    CHECK (a.bars[1].firstBeat == 4);
    REQUIRE (a.sections.size() == 1);
    REQUIRE (a.sections[0].key.has_value());
    CHECK (a.sections[0].key->camelot == "8A");
    CHECK (a.sections[0].key->name.empty());
    REQUIRE (a.descriptorsPerBar.size() == 2);
    CHECK (a.descriptorsPerBar[1].melody == Catch::Approx (0.7));
    REQUIRE (a.descriptorsPerSection.size() == 1);
    CHECK (a.descriptorsPerSection[0].high == Catch::Approx (0.41));
    CHECK (a.beatConfidence == Catch::Approx (0.93));
}

TEST_CASE ("AnalysisLoader accepts sections without a reliable key", "[analysis]")
{
    auto doc = example();
    doc["sections"][0]["key"] = nullptr;
    doc["loudness"]["integratedLufs"] = nullptr;

    const auto result = AnalysisLoader::fromJson (doc);
    INFO (errorOf (result));
    REQUIRE (std::holds_alternative<TrackAnalysis> (result));
    CHECK_FALSE (std::get<TrackAnalysis> (result).sections[0].key.has_value());
    CHECK_FALSE (std::get<TrackAnalysis> (result).integratedLufs.has_value());
}

TEST_CASE ("AnalysisLoader rejects inconsistent files", "[analysis]")
{
    auto doc = example();

    SECTION ("unsupported schema version")
    {
        doc["schemaVersion"] = 2;
        CHECK (errorOf (AnalysisLoader::fromJson (doc)).find ("schemaVersion") != std::string::npos);
    }

    SECTION ("missing field")
    {
        doc.erase ("beats");
        CHECK (errorOf (AnalysisLoader::fromJson (doc)) == "analysis.beats is missing");
    }

    SECTION ("bar refers past the last beat")
    {
        doc["bars"][1]["firstBeat"] = 99;
        CHECK_FALSE (errorOf (AnalysisLoader::fromJson (doc)).empty());
    }

    SECTION ("phrase refers to a missing section")
    {
        doc["phrases"][0]["section"] = 3;
        CHECK_FALSE (errorOf (AnalysisLoader::fromJson (doc)).empty());
    }

    SECTION ("per-bar arrays do not match the bar count")
    {
        doc["energyPerBar"].push_back (0.5);
        CHECK_FALSE (errorOf (AnalysisLoader::fromJson (doc)).empty());
    }

    SECTION ("beats out of order")
    {
        doc["beats"][2] = 0.1;
        CHECK_FALSE (errorOf (AnalysisLoader::fromJson (doc)).empty());
    }

    SECTION ("not JSON")
    {
        CHECK_FALSE (errorOf (AnalysisLoader::fromString ("{ not json")).empty());
    }
}

TEST_CASE ("Cache file names replace the hash colon", "[analysis]")
{
    CHECK (AnalysisLoader::cacheFileNameForHash ("sha256:abc") == "sha256-abc.json");
}

TEST_CASE ("AnalysisLoader reads analyzer output and its waveform", "[analysis][analyzer]")
{
    const auto dir = djmix::test::analyzerOutputDir();
    if (dir.empty())
        SKIP ("DJMIX_ANALYZER_OUTPUT is not set");

    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator (dir))
    {
        if (entry.path().extension() != ".json")
            continue;

        INFO (entry.path().string());
        const auto result = AnalysisLoader::fromFile (entry.path());
        INFO (errorOf (result));
        REQUIRE (std::holds_alternative<TrackAnalysis> (result));
        const auto& a = std::get<TrackAnalysis> (result);
        CHECK (AnalysisLoader::cacheFileNameForHash (a.fileHash) == entry.path().filename().string());

        std::string error;
        const auto waveform = djmix::WaveformFile::read (dir / a.waveformFile, &error);
        INFO (error);
        REQUIRE (waveform.has_value());
        CHECK (waveform->sampleRate == static_cast<uint32_t> (a.sampleRate));
        CHECK (waveform->secondsPerPoint() * static_cast<double> (waveform->peak.size())
               == Catch::Approx (a.durationSec).margin (waveform->secondsPerPoint()));
        ++checked;
    }
    CHECK (checked > 0);
}
