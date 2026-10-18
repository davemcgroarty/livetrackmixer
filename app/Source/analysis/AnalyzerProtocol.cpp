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

namespace
{
// Environment variables in the platform's native encoding: UTF-16 on Windows,
// where getenv would return them in the ANSI code page and lose any name
// outside it.
#ifdef _WIN32
std::optional<std::wstring> environmentVariable (const wchar_t* name)
{
    wchar_t* value = nullptr;
    size_t length = 0;
    if (_wdupenv_s (&value, &length, name) != 0 || value == nullptr)
        return std::nullopt;
    std::wstring result (value);
    std::free (value);
    return result;
}
#else
std::optional<std::string> environmentVariable (const char* name)
{
    const char* value = std::getenv (name);
    if (value == nullptr)
        return std::nullopt;
    return std::string (value);
}
#endif

template <typename String>
std::vector<String> split (const String& text, typename String::value_type by)
{
    std::vector<String> parts;
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
}
} // namespace

std::optional<std::filesystem::path> findOnPath (std::string_view name)
{
    namespace fs = std::filesystem;
    using Native = fs::path::string_type;

#ifdef _WIN32
    constexpr wchar_t separator = L';';
    const auto extensions = environmentVariable (L"PATHEXT").value_or (L".COM;.EXE;.BAT;.CMD");
    const auto pathVariable = environmentVariable (L"PATH");
#else
    constexpr char separator = ':';
    const Native extensions;
    const auto pathVariable = environmentVariable ("PATH");
#endif
    if (! pathVariable)
        return std::nullopt;

    std::vector<Native> suffixes { Native() };
    for (const auto& extension : split (extensions, separator))
        suffixes.push_back (extension);

    const auto base = fs::path (std::string (name)).native();
    std::error_code error;
    for (const auto& directory : split (*pathVariable, separator))
    {
        for (const auto& suffix : suffixes)
        {
            const auto candidate = fs::path (directory) / fs::path (base + suffix);
            if (fs::is_regular_file (candidate, error))
                return candidate;
        }
    }
    return std::nullopt;
}

} // namespace djmix
