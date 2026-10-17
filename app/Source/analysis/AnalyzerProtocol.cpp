#include "AnalyzerProtocol.h"

#include <cstdlib>

#include <nlohmann/json.hpp>

#include "library/LibraryDatabase.h"

namespace djmix
{

std::optional<AnalyzerEvent> parseAnalyzerLine (std::string_view line)
{
    const auto json = nlohmann::json::parse (line, nullptr, false);
    if (json.is_discarded() || ! json.is_object() || ! json.contains ("stage") || ! json["stage"].is_string())
        return std::nullopt;

    AnalyzerEvent event;
    event.stage = json["stage"].get<std::string>();
    if (json.contains ("progress") && json["progress"].is_number())
        event.progress = json["progress"].get<double>();
    if (json.contains ("json") && json["json"].is_string())
        event.jsonPath = pathFromUtf8 (json["json"].get<std::string>());
    if (json.contains ("message") && json["message"].is_string())
        event.message = json["message"].get<std::string>();

    if (event.stage == "done")
        event.kind = AnalyzerEvent::Kind::done;
    else if (event.stage == "cached")
        event.kind = AnalyzerEvent::Kind::cached;
    else if (event.stage == "error")
        event.kind = AnalyzerEvent::Kind::error;
    return event;
}

std::vector<std::string> analyzerArguments (const std::filesystem::path& audioFile,
                                            const std::filesystem::path& cacheDir)
{
    return { "analyze", pathToUtf8 (audioFile), "--out", pathToUtf8 (cacheDir) };
}

std::optional<std::string> parseAnalyzerVersion (std::string_view output)
{
    constexpr std::string_view prefix = "djanalyze ";
    if (output.substr (0, prefix.size()) != prefix)
        return std::nullopt;
    auto version = output.substr (prefix.size());
    const auto end = version.find_first_of (" \t\r\n");
    version = version.substr (0, end);
    if (version.empty())
        return std::nullopt;
    return std::string (version);
}

std::optional<std::filesystem::path> findOnPath (std::string_view name)
{
    namespace fs = std::filesystem;

#ifdef _WIN32
    constexpr char separator = ';';
    const char* pathExt = std::getenv ("PATHEXT");
    std::string extensions = pathExt != nullptr ? pathExt : ".COM;.EXE;.BAT;.CMD";
#else
    constexpr char separator = ':';
    std::string extensions;
#endif

    const char* pathVariable = std::getenv ("PATH");
    if (pathVariable == nullptr)
        return std::nullopt;

    auto split = [] (const std::string& text, char by)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); ++i)
        {
            if (i == text.size() || text[i] == by)
            {
                if (i > start)
                    parts.push_back (text.substr (start, i - start));
                start = i + 1;
            }
        }
        return parts;
    };

    std::vector<std::string> suffixes { "" };
    for (const auto& extension : split (extensions, ';'))
        suffixes.push_back (extension);

    std::error_code error;
    for (const auto& directory : split (pathVariable, separator))
    {
        for (const auto& suffix : suffixes)
        {
            const fs::path candidate = fs::path (directory) / (std::string (name) + suffix);
            if (fs::is_regular_file (candidate, error))
                return candidate;
        }
    }
    return std::nullopt;
}

} // namespace djmix
