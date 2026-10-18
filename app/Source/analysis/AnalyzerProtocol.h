#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace djmix
{

// One JSON line from `djanalyze analyze` on stdout.
struct AnalyzerEvent
{
    enum class Kind
    {
        progress, // a stage started; progress is the overall fraction done
        done,     // analysis written to jsonPath
        cached,   // a current analysis already existed at jsonPath
        error     // message says why
    };

    Kind kind = Kind::progress;
    std::string stage;
    double progress = 0.0;
    std::filesystem::path jsonPath;
    std::string message;
};

// Parses one line. Returns nothing for lines that are not analyzer events.
std::optional<AnalyzerEvent> parseAnalyzerLine (std::string_view line);

// Arguments for analysing one file, after the executable itself.
std::vector<std::string> analyzerArguments (const std::filesystem::path& audioFile,
                                            const std::filesystem::path& cacheDir);

// The version number from `djanalyze --version` output ("djanalyze 0.1.2"),
// or nothing if the output is not from djanalyze.
std::optional<std::string> parseAnalyzerVersion (std::string_view versionOutput);

// The first executable called `name` on PATH (trying PATHEXT on Windows).
std::optional<std::filesystem::path> findOnPath (std::string_view name);

} // namespace djmix
