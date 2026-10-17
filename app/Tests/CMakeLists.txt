include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

# Tests for djmix_core: no JUCE, no window, no audio device.
add_executable(djmix_tests
    AnalysisLoaderTests.cpp
    AnalysisSchemaTests.cpp
    AnalyzerProtocolTests.cpp
    LibraryTests.cpp
    WaveformFileTests.cpp
    RealtimeGuardTests.cpp
    ViewTests.cpp
    TestPaths.h
)

target_link_libraries(djmix_tests PRIVATE
    djmix_core
    SQLiteCpp
    Catch2::Catch2WithMain
    nlohmann_json_schema_validator::validator
)

target_compile_definitions(djmix_tests PRIVATE
    DJMIX_REALTIME_CHECKS=1
    DJMIX_REPO_ROOT="${PROJECT_SOURCE_DIR}"
)

catch_discover_tests(djmix_tests)

if(NOT DJMIX_BUILD_APP)
    return()
endif()

# A stand-in for djanalyze that follows its command line and output protocol.
add_executable(fake_djanalyze FakeAnalyzer.cpp)
target_compile_features(fake_djanalyze PRIVATE cxx_std_20)
target_compile_definitions(fake_djanalyze PRIVATE DJMIX_REPO_ROOT="${PROJECT_SOURCE_DIR}")

# Tests for the code that uses JUCE: the analyzer runner, and views drawn
# into images (no window needed).
juce_add_console_app(djmix_app_tests PRODUCT_NAME "djmix_app_tests")
target_sources(djmix_app_tests PRIVATE
    ${DJMIX_SERVICE_SOURCES}
    ${DJMIX_VIEW_SOURCES}
    AnalyzerRunnerTests.cpp
    TrackViewTests.cpp
)
target_include_directories(djmix_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(djmix_app_tests PRIVATE
    djmix_core
    juce::juce_gui_basics
    Catch2::Catch2WithMain
)
target_compile_definitions(djmix_app_tests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
    DJMIX_REPO_ROOT="${PROJECT_SOURCE_DIR}"
    DJMIX_FAKE_ANALYZER="$<TARGET_FILE:fake_djanalyze>"
)
add_dependencies(djmix_app_tests fake_djanalyze)

catch_discover_tests(djmix_app_tests)
