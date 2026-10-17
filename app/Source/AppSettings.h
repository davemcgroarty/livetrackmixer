#pragma once

#include <filesystem>

#include <juce_data_structures/juce_data_structures.h>

namespace djmix
{

// App-wide settings, kept outside any project: where the analyzer is and
// where the library and analysis cache live.
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

private:
    mutable juce::ApplicationProperties properties;
};

std::filesystem::path toPath (const juce::File& file);
juce::File toFile (const std::filesystem::path& path);

} // namespace djmix
