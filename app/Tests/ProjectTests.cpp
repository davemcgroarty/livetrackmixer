#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <variant>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include "TestPaths.h"
#include "io/ProjectFile.h"
#include "model/BeatMap.h"
#include "model/Project.h"

using Catch::Approx;
using djmix::Clip;
using djmix::Project;
using djmix::ProjectFile;
using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
const std::string hashA = "sha256:" + std::string (64, 'a');
const std::string hashB = "sha256:" + std::string (64, 'b');

Project example()
{
    auto p = Project::create ("Friday warm-up", 126.0);
    p.key = "8A";
    const auto vocals = p.addLayer ("Vocals");
    p.layers[0].gain = 0.8;
    p.layers[1].solo = true;
    p.layers[1].cue = true;

    Clip a;
    a.id = p.nextId();
    a.trackId = 3;
    a.fileHash = hashA;
    a.layerId = p.layers[0].id;
    a.sourceStartBeat = 64.0;
    a.timelineStartBeat = 0.0;
    a.lengthBeats = 128.0;
    a.label = "Intro";
    p.clips.push_back (a);

    Clip b = a;
    b.id = p.nextId();
    b.trackId = 9;
    b.fileHash = hashB;
    b.layerId = vocals;
    b.sourceStartBeat = 0.5;
    b.timelineStartBeat = 96.0;
    b.lengthBeats = 64.0;
    b.gain = 0.5;
    b.transposeSemitones = -2;
    b.label = "";
    p.clips.push_back (b);
    return p;
}

Project loaded (const djmix::ProjectLoadResult& result)
{
    if (const auto* error = std::get_if<djmix::ProjectLoadError> (&result))
        FAIL (error->message);
    return std::get<Project> (result);
}

std::string loadError (const json& j)
{
    const auto result = ProjectFile::fromJson (j);
    const auto* error = std::get_if<djmix::ProjectLoadError> (&result);
    return error != nullptr ? error->message : std::string();
}
} // namespace

TEST_CASE ("A new project has one layer, and ids are never reused", "[project]")
{
    auto p = Project::create ("Mix");
    REQUIRE (p.layers.size() == 1);
    CHECK (p.layers[0].name == "Layer 1");
    CHECK (p.tempo == 128.0);
    CHECK (p.lengthBeats() == 0.0);

    const auto second = p.addLayer();
    CHECK (second == 2);
    CHECK (p.layer (second)->name == "Layer 2");
    CHECK (p.nextId() == 3);
    CHECK (p.layer (99) == nullptr);

    const auto e = example();
    CHECK (e.lengthBeats() == 160.0);
    CHECK (e.clip (3)->timelineEndBeat() == 128.0);
    CHECK (e.clip (4)->sourceEndBeat() == 64.5);
}

TEST_CASE ("Solo and mute decide which layers are heard", "[project]")
{
    auto p = Project::create ("Mix");
    p.addLayer();
    p.addLayer();
    CHECK (djmix::isAudible (p, p.layers[0]));
    p.layers[0].mute = true;
    CHECK_FALSE (djmix::isAudible (p, p.layers[0]));
    p.layers[1].solo = true;
    CHECK (djmix::isAudible (p, p.layers[1]));
    CHECK_FALSE (djmix::isAudible (p, p.layers[2]));
    p.layers[1].mute = true; // muting wins over solo
    CHECK_FALSE (djmix::isAudible (p, p.layers[1]));
}

TEST_CASE ("A project survives a round trip through its file", "[project]")
{
    const auto p = example();
    CHECK (loaded (ProjectFile::fromString (ProjectFile::toString (p))) == p);

    Project noKey = p;
    noKey.key.reset();
    CHECK (ProjectFile::toJson (noKey)["key"].is_null());
    CHECK (loaded (ProjectFile::fromJson (ProjectFile::toJson (noKey))) == noKey);

    const auto file = fs::temp_directory_path() / "djmix-project-test.djmix";
    fs::remove (file);
    std::string error;
    REQUIRE (ProjectFile::save (p, file, error));
    CHECK (loaded (ProjectFile::load (file)) == p);

    // Saving again replaces it, and leaves no temporary file behind.
    auto changed = p;
    changed.tempo = 130.0;
    REQUIRE (ProjectFile::save (changed, file, error));
    CHECK (loaded (ProjectFile::load (file)).tempo == 130.0);
    CHECK_FALSE (fs::exists (fs::path (file.string() + ".saving")));
    fs::remove (file);

    CHECK_FALSE (ProjectFile::save (p, fs::temp_directory_path() / "no-such-dir" / "x.djmix", error));
    CHECK_FALSE (error.empty());
    CHECK (std::holds_alternative<djmix::ProjectLoadError> (ProjectFile::load (file)));
}

TEST_CASE ("Project files match the schema", "[project][schema]")
{
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema (djmix::test::readJson (djmix::test::schemaPath ("project.v1.schema.json")));
    CHECK_NOTHROW (validator.validate (ProjectFile::toJson (example())));
    CHECK_NOTHROW (validator.validate (ProjectFile::toJson (Project::create ("Empty"))));

    auto bad = ProjectFile::toJson (example());
    bad["clips"][0]["lengthBeats"] = 0;
    CHECK_THROWS (validator.validate (bad));
}

TEST_CASE ("Damaged project files are refused with a reason", "[project]")
{
    const auto good = ProjectFile::toJson (example());
    CHECK (loadError (good).empty());

    auto j = good;
    j["format"] = "something else";
    CHECK (loadError (j) == "not a project file");

    j = good;
    j["version"] = 2;
    CHECK (loadError (j) == "unsupported project version 2");

    j = good;
    j["tempo"] = 500;
    CHECK (loadError (j) == "project.tempo is out of range");

    j = good;
    j["clips"][0]["layerId"] = 42;
    CHECK (loadError (j) == "clip 3 is on a layer that does not exist");

    j = good;
    j["clips"][1]["id"] = 1;
    CHECK (loadError (j) == "id 1 is used twice");

    j = good;
    j["clips"][0].erase ("gain");
    CHECK (loadError (j) == "clip.gain is missing");

    j = good;
    j["layers"][0]["mute"] = "no";
    CHECK (loadError (j) == "layer.mute is not a boolean");

    j = good;
    j["clips"][0]["lengthBeats"] = -4;
    CHECK (loadError (j) == "clip.lengthBeats is not positive");

    CHECK (std::get<djmix::ProjectLoadError> (ProjectFile::fromString ("{ nope")).message == "not valid JSON");
}

TEST_CASE ("Fractional beats map to times on a track's grid and back", "[project]")
{
    // Beats every 0.5 s from 0.25 s, then one 0.6 s apart.
    const std::vector<double> beats { 0.25, 0.75, 1.25, 1.75, 2.35 };
    CHECK (djmix::timeAtBeat (beats, 0.0) == Approx (0.25));
    CHECK (djmix::timeAtBeat (beats, 2.5) == Approx (1.5));
    CHECK (djmix::timeAtBeat (beats, 3.5) == Approx (2.05));
    CHECK (djmix::timeAtBeat (beats, -1.0) == Approx (-0.25)); // before the first beat, at its period
    CHECK (djmix::timeAtBeat (beats, 6.0) == Approx (3.55));   // after the last, at the last period

    for (const double b : { -2.0, 0.0, 0.3, 2.5, 3.0, 3.99, 4.0, 7.25 })
        CHECK (djmix::beatAtTime (beats, djmix::timeAtBeat (beats, b)) == Approx (b));

    CHECK (djmix::timeAtBeat ({}, 4.0) == Approx (2.0));
    CHECK (djmix::beatAtTime ({ 1.0 }, 2.0) == Approx (2.0));
}
