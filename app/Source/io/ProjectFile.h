#pragma once

#include <filesystem>
#include <string>
#include <variant>

#include <nlohmann/json_fwd.hpp>

#include "model/Project.h"

namespace djmix
{

struct ProjectLoadError
{
    std::string message;
};

using ProjectLoadResult = std::variant<Project, ProjectLoadError>;

// A project as a .djmix file: JSON, described by schemas/project.v1.schema.json.
// Loading checks types, ranges and references (every clip on a layer that
// exists, no id used twice); the tests validate against the schema itself.
class ProjectFile
{
public:
    static constexpr const char* extension = ".djmix";
    static constexpr const char* format = "djmix.project";
    static constexpr int version = 1;

    static nlohmann::json toJson (const Project& project);
    static std::string toString (const Project& project);

    static ProjectLoadResult fromJson (const nlohmann::json& json);
    static ProjectLoadResult fromString (const std::string& text);
    static ProjectLoadResult load (const std::filesystem::path& file);

    // Writes to a temporary file beside `file`, then moves it into place, so
    // a failed save never leaves a half-written project. False (with `error`
    // set) if it could not.
    static bool save (const Project& project, const std::filesystem::path& file, std::string& error);
};

} // namespace djmix
