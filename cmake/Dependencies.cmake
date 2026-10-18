# Third-party dependencies, fetched with CPM.cmake.
# Set CPM_SOURCE_CACHE (for example to ~/.cache/CPM) to share downloads between builds.

CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    VERSION 3.12.0
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
)

# Track library database. SQLiteCpp bundles the SQLite amalgamation (public domain).
CPMAddPackage(
    NAME SQLiteCpp
    GITHUB_REPOSITORY SRombauts/SQLiteCpp
    GIT_TAG 3.4.0
    VERSION 3.4.0
    OPTIONS "SQLITECPP_RUN_CPPLINT OFF" "SQLITECPP_RUN_CPPCHECK OFF" "SQLITECPP_BUILD_EXAMPLES OFF"
            "SQLITECPP_BUILD_TESTS OFF" "SQLITECPP_INTERNAL_SQLITE ON"
)

if(DJMIX_BUILD_APP)
    # JUCE 9 under AGPLv3 (free tier).
    CPMAddPackage(
        NAME JUCE
        GITHUB_REPOSITORY juce-framework/JUCE
        GIT_TAG 9.0.4
        VERSION 9.0.4
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

if(DJMIX_BUILD_APP)
    # Time-stretching for the render cache: Rubber Band Library, GPL v2 or later
    # (compatible with the app's AGPLv3). Built from its single-file source.
    CPMAddPackage(
        NAME rubberband
        GITHUB_REPOSITORY breakfastquay/rubberband
        GIT_TAG v4.0.0
        VERSION 4.0.0
        DOWNLOAD_ONLY YES
    )
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/rubberband ${CMAKE_BINARY_DIR}/rubberband)
endif()
