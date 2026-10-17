#pragma once

#include <filesystem>
#include <string>
#include <variant>

#include <nlohmann/json_fwd.hpp>

#include "Analysis.h"

namespace djmix
{

struct AnalysisLoadError
{
    std::string message;
};

using AnalysisLoadResult = std::variant<TrackAnalysis, AnalysisLoadError>;

// Reads analysis JSON written by djanalyze. Does structural checks only (types,
// required fields, schema version, internal consistency); full JSON Schema
// validation happens in the tests, against schemas/analysis.v1.schema.json.
class AnalysisLoader
{
public:
    static AnalysisLoadResult fromJson (const nlohmann::json& json);
    static AnalysisLoadResult fromString (const std::string& text);
    static AnalysisLoadResult fromFile (const std::filesystem::path& file);

    // Cache file name for a file hash: "sha256:<hex>" -> "sha256-<hex>.json".
    static std::string cacheFileNameForHash (const std::string& fileHash);
};

} // namespace djmix
