#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include <juce_data_structures/juce_data_structures.h>

#include "engine/CueMode.h"
#include "engine/OutputRouting.h"
#include "model/Snap.h"

namespace djmix
{

// App-wide settings, kept outside any project: where the analyzer is, where
// the library and analysis cache live, and the audio output setup.
class AppSettings
{
public:
    AppSettings();

    // <user app data>/DJ Mixer
    std::filesystem::path dataDirectory() const;
    std::filesystem::path libraryFile() const;
    std::filesystem::path analysisCacheDirectory() const;

    // The djanalyze executable (or a Python interpreter with djanalyze
    // installed). Falls back to djanalyze on PATH when none is saved.
    std::filesystem::path analyzerExecutable() const;
    void setAnalyzerExecutable (const std::filesystem::path& executable);

    // The audio device as juce::AudioDeviceManager saves it; empty for the default.
    std::unique_ptr<juce::XmlElement> audioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement* state);

    // Where Master and Cue go; empty until chosen, for the device's default.
    std::optional<OutputRouting> outputRouting() const;
    void setOutputRouting (const OutputRouting& routing);

    // Cue volume as a gain, 0 to 1.
    float cueVolume() const;
    void setCueVolume (float gain);

    // What a cue does at the end of its section or phrase.
    CueMode cueMode() const;
    void setCueMode (CueMode mode);

    // The project open when the app last closed; empty for the untitled one.
    std::filesystem::path projectFile() const;
    void setProjectFile (const std::filesystem::path& file);

    // Where the untitled project saves itself: <data directory>/Untitled.djmix
    std::filesystem::path untitledProjectFile() const;

    SnapMode snapMode() const; // bar until chosen
    void setSnapMode (SnapMode mode);
    bool followPlayhead() const; // on until turned off
    void setFollowPlayhead (bool follow);

private:
    mutable juce::ApplicationProperties properties;
};

std::filesystem::path toPath (const juce::File& file);
juce::File toFile (const std::filesystem::path& path);

} // namespace djmix
