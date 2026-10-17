#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json-schema.hpp>

#include "TestPaths.h"

using nlohmann::json;
using nlohmann::json_schema::json_validator;

namespace
{
json_validator analysisValidator()
{
    json_validator validator;
    validator.set_root_schema (djmix::test::readJson (djmix::test::schemaPath ("analysis.v1.schema.json")));
    return validator;
}

bool isValid (const json_validator& validator, const json& document, std::string& error)
{
    try
    {
        validator.validate (document);
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}
} // namespace

TEST_CASE ("The example analysis file matches the schema", "[schema]")
{
    const auto validator = analysisValidator();
    const auto example = djmix::test::readJson (djmix::test::schemaPath ("examples/analysis.v1.example.json"));

    std::string error;
    const bool valid = isValid (validator, example, error);
    INFO (error);
    CHECK (valid);
}

TEST_CASE ("The schema rejects malformed analysis files", "[schema]")
{
    const auto validator = analysisValidator();
    const auto example = djmix::test::readJson (djmix::test::schemaPath ("examples/analysis.v1.example.json"));
    std::string error;

    SECTION ("labelled sections")
    {
        auto doc = example;
        doc["sections"][0]["label"] = "chorus";
        CHECK_FALSE (isValid (validator, doc, error));
    }

    SECTION ("wrong schema version")
    {
        auto doc = example;
        doc["schemaVersion"] = 2;
        CHECK_FALSE (isValid (validator, doc, error));
    }

    SECTION ("descriptor out of range")
    {
        auto doc = example;
        doc["descriptors"]["perBar"]["bass"][0] = 1.5;
        CHECK_FALSE (isValid (validator, doc, error));
    }

    SECTION ("bad Camelot code")
    {
        auto doc = example;
        doc["key"]["camelot"] = "13A";
        CHECK_FALSE (isValid (validator, doc, error));
    }
}

TEST_CASE ("Analyzer output matches the schema", "[schema][analyzer]")
{
    const auto dir = djmix::test::analyzerOutputDir();
    if (dir.empty())
        SKIP ("DJMIX_ANALYZER_OUTPUT is not set");

    const auto validator = analysisValidator();
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator (dir))
    {
        if (entry.path().extension() != ".json")
            continue;

        std::string error;
        const bool valid = isValid (validator, djmix::test::readJson (entry.path()), error);
        INFO (entry.path().string() << ": " << error);
        CHECK (valid);
        ++checked;
    }
    CHECK (checked > 0);
}
