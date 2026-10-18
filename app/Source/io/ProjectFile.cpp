#include "ProjectFile.h"

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

namespace djmix
{

namespace
{
using nlohmann::json;

// Thrown inside the loader only, turned into a ProjectLoadError at the boundary.
struct LoadFailure
{
    std::string message;
};

[[noreturn]] void fail (const std::string& message)
{
    throw LoadFailure { message };
}

const json& member (const json& object, const char* name, std::string_view where)
{
    if (! object.is_object())
        fail (std::string (where) + " is not an object");
    const auto it = object.find (name);
    if (it == object.end())
        fail (std::string (where) + "." + name + " is missing");
    return *it;
}

double number (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_number())
        fail (std::string (where) + "." + name + " is not a number");
    return value.get<double>();
}

int64_t integer (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_number_integer())
        fail (std::string (where) + "." + name + " is not an integer");
    return value.get<int64_t>();
}

std::string string (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_string())
        fail (std::string (where) + "." + name + " is not a string");
    return value.get<std::string>();
}

bool boolean (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_boolean())
        fail (std::string (where) + "." + name + " is not a boolean");
    return value.get<bool>();
}

const json& array (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_array())
        fail (std::string (where) + "." + name + " is not an array");
    return value;
}

double gain (const json& object, std::string_view where)
{
    const double g = number (object, "gain", where);
    if (! (g >= 0.0 && g <= 16.0))
        fail (std::string (where) + ".gain is out of range");
    return g;
}

Project parse (const json& root)
{
    if (string (root, "format", "project") != ProjectFile::format)
        fail ("not a project file");
    const auto version = integer (root, "version", "project");
    if (version != ProjectFile::version)
        fail ("unsupported project version " + std::to_string (version));

    Project p;
    p.name = string (root, "name", "project");
    p.tempo = number (root, "tempo", "project");
    if (! (p.tempo >= Project::minTempo && p.tempo <= Project::maxTempo))
        fail ("project.tempo is out of range");

    const auto& key = member (root, "key", "project");
    if (key.is_string())
        p.key = key.get<std::string>();
    else if (! key.is_null())
        fail ("project.key is not a string or null");

    std::set<int64_t> ids;
    auto takeId = [&ids] (int64_t id, std::string_view where)
    {
        if (id <= 0)
            fail (std::string (where) + ".id is not positive");
        if (! ids.insert (id).second)
            fail ("id " + std::to_string (id) + " is used twice");
    };

    for (const auto& item : array (root, "layers", "project"))
    {
        Layer l;
        l.id = integer (item, "id", "layer");
        takeId (l.id, "layer");
        l.name = string (item, "name", "layer");
        l.gain = gain (item, "layer");
        l.mute = boolean (item, "mute", "layer");
        l.solo = boolean (item, "solo", "layer");
        l.cue = boolean (item, "cue", "layer");
        p.layers.push_back (std::move (l));
    }

    for (const auto& item : array (root, "clips", "project"))
    {
        Clip c;
        c.id = integer (item, "id", "clip");
        takeId (c.id, "clip");
        c.trackId = integer (item, "trackId", "clip");
        c.fileHash = string (item, "fileHash", "clip");
        c.layerId = integer (item, "layerId", "clip");
        if (p.layer (c.layerId) == nullptr)
            fail ("clip " + std::to_string (c.id) + " is on a layer that does not exist");
        c.sourceStartBeat = number (item, "sourceStartBeat", "clip");
        c.timelineStartBeat = number (item, "timelineStartBeat", "clip");
        if (c.timelineStartBeat < 0.0)
            fail ("clip.timelineStartBeat is negative");
        c.lengthBeats = number (item, "lengthBeats", "clip");
        if (! (c.lengthBeats > 0.0))
            fail ("clip.lengthBeats is not positive");
        c.gain = gain (item, "clip");
        c.transposeSemitones = static_cast<int> (integer (item, "transposeSemitones", "clip"));
        if (std::abs (c.transposeSemitones) > Project::maxTranspose)
            fail ("clip.transposeSemitones is out of range");
        c.label = string (item, "label", "clip");
        p.clips.push_back (std::move (c));
    }

    // Transitions arrive in M5; until then a project has none.
    if (! array (root, "transitions", "project").empty())
        fail ("transitions are not supported yet");

    return p;
}
} // namespace

json ProjectFile::toJson (const Project& p)
{
    json layers = json::array();
    for (const auto& l : p.layers)
        layers.push_back ({ { "id", l.id }, { "name", l.name }, { "gain", l.gain },
                            { "mute", l.mute }, { "solo", l.solo }, { "cue", l.cue } });

    json clips = json::array();
    for (const auto& c : p.clips)
        clips.push_back ({ { "id", c.id },
                           { "trackId", c.trackId },
                           { "fileHash", c.fileHash },
                           { "layerId", c.layerId },
                           { "sourceStartBeat", c.sourceStartBeat },
                           { "timelineStartBeat", c.timelineStartBeat },
                           { "lengthBeats", c.lengthBeats },
                           { "gain", c.gain },
                           { "transposeSemitones", c.transposeSemitones },
                           { "label", c.label } });

    return { { "format", format },
             { "version", version },
             { "name", p.name },
             { "tempo", p.tempo },
             { "key", p.key ? json (*p.key) : json (nullptr) },
             { "layers", std::move (layers) },
             { "clips", std::move (clips) },
             { "transitions", json::array() } };
}

std::string ProjectFile::toString (const Project& project)
{
    return toJson (project).dump (2) + "\n";
}

ProjectLoadResult ProjectFile::fromJson (const json& json)
{
    try
    {
        return parse (json);
    }
    catch (const LoadFailure& f)
    {
        return ProjectLoadError { f.message };
    }
    catch (const nlohmann::json::exception& e)
    {
        return ProjectLoadError { e.what() };
    }
}

ProjectLoadResult ProjectFile::fromString (const std::string& text)
{
    const auto parsed = json::parse (text, nullptr, false);
    if (parsed.is_discarded())
        return ProjectLoadError { "not valid JSON" };
    return fromJson (parsed);
}

ProjectLoadResult ProjectFile::load (const std::filesystem::path& file)
{
    std::ifstream in (file, std::ios::binary);
    if (! in)
        return ProjectLoadError { "cannot open " + file.string() };
    std::ostringstream text;
    text << in.rdbuf();
    return fromString (text.str());
}

bool ProjectFile::save (const Project& project, const std::filesystem::path& file, std::string& error)
{
    auto temporary = file;
    temporary += ".saving";
    {
        std::ofstream out (temporary, std::ios::binary | std::ios::trunc);
        if (! out)
        {
            error = "cannot write " + temporary.string();
            return false;
        }
        out << toString (project);
        out.flush();
        if (! out)
        {
            error = "cannot write " + temporary.string();
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename (temporary, file, ec);
    if (ec)
    {
        error = "cannot replace " + file.string() + ": " + ec.message();
        std::filesystem::remove (temporary, ec);
        return false;
    }
    return true;
}

} // namespace djmix
