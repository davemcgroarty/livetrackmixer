#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace djmix::test
{

inline std::filesystem::path repoRoot()
{
    return std::filesystem::path (DJMIX_REPO_ROOT);
}

inline std::filesystem::path schemaPath (const std::string& name)
{
    return repoRoot() / "schemas" / name;
}

inline nlohmann::json readJson (const std::filesystem::path& file)
{
    std::ifstream in (file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return nlohmann::json::parse (text.str());
}

// Extra analysis files to check, written by the analyzer test run
// (DJMIX_ANALYZER_OUTPUT points at a cache directory). Empty when unset.
inline std::filesystem::path analyzerOutputDir()
{
    const char* dir = std::getenv ("DJMIX_ANALYZER_OUTPUT");
    return dir == nullptr ? std::filesystem::path() : std::filesystem::path (dir);
}

} // namespace djmix::test
