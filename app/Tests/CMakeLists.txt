add_executable(djmix_tests
    AnalysisLoaderTests.cpp
    AnalysisSchemaTests.cpp
    WaveformFileTests.cpp
    RealtimeGuardTests.cpp
    TestPaths.h
)

target_link_libraries(djmix_tests PRIVATE
    djmix_core
    Catch2::Catch2WithMain
    nlohmann_json_schema_validator::validator
)

target_compile_definitions(djmix_tests PRIVATE
    DJMIX_REALTIME_CHECKS=1
    DJMIX_REPO_ROOT="${PROJECT_SOURCE_DIR}"
)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(djmix_tests)
