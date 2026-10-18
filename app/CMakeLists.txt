# djmix_core: everything that must be unit-testable without a window or audio device.
# It must not depend on JUCE UI classes.
add_library(djmix_core STATIC
    Source/analysis/Analysis.h
    Source/analysis/AnalysisLoader.h
    Source/analysis/AnalysisLoader.cpp
    Source/analysis/WaveformFile.h
    Source/analysis/WaveformFile.cpp
    Source/analysis/AnalyzerProtocol.h
    Source/analysis/AnalyzerProtocol.cpp
    Source/engine/Mp3Gapless.h
    Source/engine/Mp3Gapless.cpp
    Source/engine/OutputRouting.h
    Source/engine/OutputRouting.cpp
    Source/engine/RealtimeGuard.h
    Source/engine/RealtimeGuard.cpp
    Source/library/Camelot.h
    Source/library/Camelot.cpp
    Source/library/ImportScanner.h
    Source/library/ImportScanner.cpp
    Source/library/LibraryDatabase.h
    Source/library/LibraryDatabase.cpp
    Source/library/LibraryFilter.h
    Source/library/LibraryFilter.cpp
    Source/library/TrackColours.h
    Source/library/TrackColours.cpp
    Source/view/TrackStructure.h
    Source/view/TrackStructure.cpp
    Source/view/ViewRange.h
    Source/view/ViewRange.cpp
    Source/view/WaveformPyramid.h
    Source/view/WaveformPyramid.cpp
)
target_include_directories(djmix_core PUBLIC Source)
target_link_libraries(djmix_core PUBLIC nlohmann_json::nlohmann_json PRIVATE SQLiteCpp)
target_compile_features(djmix_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(djmix_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(djmix_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT DJMIX_BUILD_APP)
    return()
endif()

# Non-UI code that needs JUCE (juce_core). JUCE modules compile into each
# target that links them, so these sources go into the app and the app tests
# directly rather than into a shared library.
set(DJMIX_SERVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/analysis/AnalyzerRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/analysis/AnalyzerRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/AuditionController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/AuditionController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/CuePlayer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/CuePlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/TrackDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/TrackDecoder.cpp
)
set(DJMIX_SERVICE_SOURCES ${DJMIX_SERVICE_SOURCES} PARENT_SCOPE) # for app/Tests

# UI components the app tests draw into images.
set(DJMIX_VIEW_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/TrackView.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/TrackView.cpp
)
set(DJMIX_VIEW_SOURCES ${DJMIX_VIEW_SOURCES} PARENT_SCOPE)

juce_add_gui_app(DJMixer
    PRODUCT_NAME "DJ Mixer"
    COMPANY_NAME "livetrackmixer"
    BUNDLE_ID "net.livetrackmixer.djmixer"
    VERSION ${PROJECT_VERSION}
)

target_sources(DJMixer PRIVATE
    ${DJMIX_SERVICE_SOURCES}
    ${DJMIX_VIEW_SOURCES}
    Source/Main.cpp
    Source/AppSettings.h
    Source/AppSettings.cpp
    Source/AudioOutput.h
    Source/AudioOutput.cpp
    Source/library/LibraryController.h
    Source/library/LibraryController.cpp
    Source/ui/LibraryPanel.h
    Source/ui/LibraryPanel.cpp
    Source/ui/MainComponent.h
    Source/ui/MainComponent.cpp
    Source/ui/SettingsComponent.h
    Source/ui/SettingsComponent.cpp
)

target_compile_definitions(DJMixer PRIVATE
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DJMixer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DJMixer,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(DJMixer
    PRIVATE
        djmix_core
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
