#include "AnalysisLoader.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace djmix
{

namespace
{
using nlohmann::json;

// Thrown inside the loader only, turned into an AnalysisLoadError at the boundary.
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

int integer (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_number_integer())
        fail (std::string (where) + "." + name + " is not an integer");
    return value.get<int>();
}

std::string string (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_string())
        fail (std::string (where) + "." + name + " is not a string");
    return value.get<std::string>();
}

const json& array (const json& object, const char* name, std::string_view where)
{
    const auto& value = member (object, name, where);
    if (! value.is_array())
        fail (std::string (where) + "." + name + " is not an array");
    return value;
}

std::vector<double> numbers (const json& object, const char* name, std::string_view where)
{
    std::vector<double> result;
    for (const auto& item : array (object, name, where))
    {
        if (! item.is_number())
            fail (std::string (where) + "." + name + " contains a non-number");
        result.push_back (item.get<double>());
    }
    return result;
}

AnalysisKey parseKey (const json& object, std::string_view where, bool nameRequired)
{
    AnalysisKey key;
    if (nameRequired || object.contains ("name"))
        key.name = string (object, "name", where);
    key.camelot = string (object, "camelot", where);
    key.confidence = number (object, "confidence", where);
    return key;
}

std::vector<ContentScores> zipScores (const json& perBar, std::string_view where)
{
    const auto rhythm = numbers (perBar, "rhythm", where);
    const auto bass = numbers (perBar, "bass", where);
    const auto high = numbers (perBar, "high", where);
    const auto melody = numbers (perBar, "melody", where);

    if (bass.size() != rhythm.size() || high.size() != rhythm.size() || melody.size() != rhythm.size())
        fail (std::string (where) + " arrays differ in length");

    std::vector<ContentScores> result (rhythm.size());
    for (size_t i = 0; i < rhythm.size(); ++i)
        result[i] = { rhythm[i], bass[i], high[i], melody[i] };
    return result;
}

void checkAscending (const std::vector<double>& times, std::string_view where)
{
    if (! std::is_sorted (times.begin(), times.end()))
        fail (std::string (where) + " is not in ascending order");
}

TrackAnalysis parse (const json& root)
{
    TrackAnalysis a;

    a.schemaVersion = integer (root, "schemaVersion", "analysis");
    if (a.schemaVersion != TrackAnalysis::supportedSchemaVersion)
        fail ("unsupported schemaVersion " + std::to_string (a.schemaVersion));

    a.analyzerVersion = string (root, "analyzerVersion", "analysis");

    const auto& file = member (root, "file", "analysis");
    a.fileHash = string (file, "hash", "file");
    a.durationSec = number (file, "durationSec", "file");
    a.sampleRate = integer (file, "sampleRate", "file");

    const auto& tempo = member (root, "tempo", "analysis");
    a.bpm = number (tempo, "bpm", "tempo");
    const auto& variable = member (tempo, "variable", "tempo");
    if (! variable.is_boolean())
        fail ("tempo.variable is not a boolean");
    a.tempoVariable = variable.get<bool>();

    a.key = parseKey (member (root, "key", "analysis"), "key", true);

    const auto& lufs = member (member (root, "loudness", "analysis"), "integratedLufs", "loudness");
    if (lufs.is_number())
        a.integratedLufs = lufs.get<double>();
    else if (! lufs.is_null())
        fail ("loudness.integratedLufs is not a number or null");

    a.beats = numbers (root, "beats", "analysis");
    a.downbeats = numbers (root, "downbeats", "analysis");
    checkAscending (a.beats, "beats");
    checkAscending (a.downbeats, "downbeats");

    for (const auto& item : array (root, "bars", "analysis"))
    {
        Bar bar { integer (item, "n", "bar"), number (item, "start", "bar"), integer (item, "firstBeat", "bar") };
        if (bar.firstBeat < 0 || static_cast<size_t> (bar.firstBeat) >= a.beats.size())
            fail ("bar " + std::to_string (bar.n) + " firstBeat is out of range");
        a.bars.push_back (bar);
    }

    for (const auto& item : array (root, "sections", "analysis"))
    {
        Section section;
        section.start = number (item, "start", "section");
        section.end = number (item, "end", "section");
        section.startBar = integer (item, "startBar", "section");
        section.lengthBars = integer (item, "lengthBars", "section");

        const auto& key = member (item, "key", "section");
        if (! key.is_null())
            section.key = parseKey (key, "section.key", false);

        if (section.end < section.start)
            fail ("section ends before it starts");
        a.sections.push_back (section);
    }

    for (const auto& item : array (root, "phrases", "analysis"))
    {
        Phrase phrase { integer (item, "n", "phrase"),
                        integer (item, "startBar", "phrase"),
                        integer (item, "lengthBars", "phrase"),
                        integer (item, "section", "phrase") };
        if (phrase.section < 0 || static_cast<size_t> (phrase.section) >= a.sections.size())
            fail ("phrase " + std::to_string (phrase.n) + " refers to a missing section");
        a.phrases.push_back (phrase);
    }

    a.energyPerBar = numbers (root, "energyPerBar", "analysis");

    const auto& descriptors = member (root, "descriptors", "analysis");
    a.descriptorsPerBar = zipScores (member (descriptors, "perBar", "descriptors"), "descriptors.perBar");

    a.descriptorsPerSection.resize (a.sections.size());
    for (const auto& item : array (descriptors, "perSection", "descriptors"))
    {
        const auto index = integer (item, "section", "descriptors.perSection");
        if (index < 0 || static_cast<size_t> (index) >= a.sections.size())
            fail ("descriptors.perSection refers to a missing section");
        a.descriptorsPerSection[static_cast<size_t> (index)] = { number (item, "rhythm", "descriptors.perSection"),
                                                                 number (item, "bass", "descriptors.perSection"),
                                                                 number (item, "high", "descriptors.perSection"),
                                                                 number (item, "melody", "descriptors.perSection") };
    }

    if (a.energyPerBar.size() != a.bars.size())
        fail ("energyPerBar has " + std::to_string (a.energyPerBar.size()) + " entries for "
              + std::to_string (a.bars.size()) + " bars");
    if (a.descriptorsPerBar.size() != a.bars.size())
        fail ("descriptors.perBar length does not match bars");

    const auto& confidence = member (root, "confidence", "analysis");
    a.beatConfidence = number (confidence, "beats", "confidence");
    a.sectionConfidence = number (confidence, "sections", "confidence");

    a.waveformFile = string (root, "waveformFile", "analysis");

    return a;
}
} // namespace

AnalysisLoadResult AnalysisLoader::fromJson (const nlohmann::json& json)
{
    try
    {
        return parse (json);
    }
    catch (const LoadFailure& failure)
    {
        return AnalysisLoadError { failure.message };
    }
    catch (const nlohmann::json::exception& e)
    {
        return AnalysisLoadError { e.what() };
    }
}

AnalysisLoadResult AnalysisLoader::fromString (const std::string& text)
{
    auto parsed = nlohmann::json::parse (text, nullptr, false);
    if (parsed.is_discarded())
        return AnalysisLoadError { "analysis file is not valid JSON" };
    return fromJson (parsed);
}

AnalysisLoadResult AnalysisLoader::fromFile (const std::filesystem::path& file)
{
    std::ifstream in (file, std::ios::binary);
    if (! in)
        return AnalysisLoadError { "cannot open " + file.string() };

    std::ostringstream text;
    text << in.rdbuf();
    return fromString (text.str());
}

std::string AnalysisLoader::cacheFileNameForHash (const std::string& fileHash)
{
    auto name = fileHash;
    std::replace (name.begin(), name.end(), ':', '-');
    return name + ".json";
}

} // namespace djmix
