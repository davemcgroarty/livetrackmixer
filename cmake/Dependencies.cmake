# Third-party dependencies, fetched with CPM.cmake.
# Set CPM_SOURCE_CACHE (for example to ~/.cache/CPM) to share downloads between builds.

CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    VERSION 3.12.0
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
)

if(DJMIX_BUILD_APP)
    # JUCE 8 under AGPLv3 (free tier). JUCE 9 is also AGPLv3; moving to it is a one-line change here.
    CPMAddPackage(
        NAME JUCE
        GITHUB_REPOSITORY juce-framework/JUCE
        GIT_TAG 8.0.16-security
        VERSION 8.0.16
        GIT_SHALLOW ON
    )
endif()

if(DJMIX_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        VERSION 3.16.1
        OPTIONS "CATCH_INSTALL_DOCS OFF" "CATCH_INSTALL_EXTRAS OFF"
    )

    # Validates fixture and analyzer output against schemas/*.schema.json in tests.
    CPMAddPackage(
        NAME nlohmann_json_schema_validator
        GITHUB_REPOSITORY pboettch/json-schema-validator
        GIT_TAG 2.4.1
        VERSION 2.4.1
        OPTIONS "JSON_VALIDATOR_BUILD_TESTS OFF" "JSON_VALIDATOR_BUILD_EXAMPLES OFF" "JSON_VALIDATOR_INSTALL OFF"
    )
endif()
