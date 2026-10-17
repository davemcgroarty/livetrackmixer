# djmix_core: everything that must be unit-testable without a window or audio device.
# It must not depend on JUCE UI classes.
add_library(djmix_core STATIC
    Source/analysis/Analysis.h
    Source/analysis/AnalysisLoader.h
    Source/analysis/AnalysisLoader.cpp
    Source/analysis/WaveformFile.h
    Source/analysis/WaveformFile.cpp
    Source/engine/RealtimeGuard.h
    Source/engine/RealtimeGuard.cpp
)
target_include_directories(djmix_core PUBLIC Source)
target_link_libraries(djmix_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(djmix_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(djmix_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(djmix_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT DJMIX_BUILD_APP)
    return()
endif()

juce_add_gui_app(DJMixer
    PRODUCT_NAME "DJ Mixer"
    COMPANY_NAME "livetrackmixer"
    BUNDLE_ID "net.livetrackmixer.djmixer"
    VERSION ${PROJECT_VERSION}
)

target_sources(DJMixer PRIVATE
    Source/Main.cpp
    Source/ui/MainComponent.h
    Source/ui/MainComponent.cpp
)

target_compile_definitions(DJMixer PRIVATE
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
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
