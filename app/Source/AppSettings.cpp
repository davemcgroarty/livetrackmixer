#include "AppSettings.h"

#include "analysis/AnalyzerProtocol.h"
#include "library/LibraryDatabase.h"

namespace djmix
{

namespace
{
constexpr const char* analyzerKey = "analyzerExecutable";
constexpr const char* audioDeviceKey = "audioDevice";
constexpr const char* masterPairKey = "masterOutputs";
constexpr const char* cuePairKey = "cueOutputs";
constexpr const char* splitCueKey = "splitCue";
constexpr const char* cueVolumeKey = "cueVolume";
constexpr float defaultCueVolume = 0.8f;
} // namespace

std::filesystem::path toPath (const juce::File& file)
{
    return pathFromUtf8 (file.getFullPathName().toStdString());
}

juce::File toFile (const std::filesystem::path& path)
{
    return juce::File (juce::String::fromUTF8 (pathToUtf8 (path).c_str()));
}

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "settings";
    options.folderName = "DJ Mixer";
    options.filenameSuffix = ".xml";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    properties.setStorageParameters (options);
}

std::filesystem::path AppSettings::dataDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
#endif
    dir = dir.getChildFile ("DJ Mixer");
    dir.createDirectory();
    return toPath (dir);
}

std::filesystem::path AppSettings::libraryFile() const
{
    return dataDirectory() / "library.sqlite";
}

std::filesystem::path AppSettings::analysisCacheDirectory() const
{
    return dataDirectory() / "analysis";
}

std::filesystem::path AppSettings::analyzerExecutable() const
{
    const auto saved = properties.getUserSettings()->getValue (analyzerKey);
    if (saved.isNotEmpty())
        return toPath (juce::File (saved));
    return findOnPath ("djanalyze").value_or (std::filesystem::path());
}

void AppSettings::setAnalyzerExecutable (const std::filesystem::path& executable)
{
    auto* settings = properties.getUserSettings();
    settings->setValue (analyzerKey, executable.empty() ? juce::String() : toFile (executable).getFullPathName());
    settings->saveIfNeeded();
}

std::unique_ptr<juce::XmlElement> AppSettings::audioDeviceState() const
{
    return properties.getUserSettings()->getXmlValue (audioDeviceKey);
}

void AppSettings::setAudioDeviceState (const juce::XmlElement* state)
{
    auto* settings = properties.getUserSettings();
    if (state != nullptr)
        settings->setValue (audioDeviceKey, state);
    else
        settings->removeValue (audioDeviceKey);
    settings->saveIfNeeded();
}

std::optional<OutputRouting> AppSettings::outputRouting() const
{
    auto* settings = properties.getUserSettings();
    if (! settings->containsKey (masterPairKey))
        return std::nullopt;
    OutputRouting r;
    r.masterFirst = std::max (0, settings->getIntValue (masterPairKey, 0));
    r.cueFirst = std::max (0, settings->getIntValue (cuePairKey, 2));
    r.splitCue = settings->getBoolValue (splitCueKey, false);
    return r;
}

void AppSettings::setOutputRouting (const OutputRouting& r)
{
    auto* settings = properties.getUserSettings();
    settings->setValue (masterPairKey, r.masterFirst);
    settings->setValue (cuePairKey, r.cueFirst);
    settings->setValue (splitCueKey, r.splitCue);
    settings->saveIfNeeded();
}

float AppSettings::cueVolume() const
{
    return juce::jlimit (0.0f, 1.0f,
                         static_cast<float> (properties.getUserSettings()->getDoubleValue (cueVolumeKey, defaultCueVolume)));
}

void AppSettings::setCueVolume (float gain)
{
    auto* settings = properties.getUserSettings();
    settings->setValue (cueVolumeKey, juce::jlimit (0.0f, 1.0f, gain));
    settings->saveIfNeeded();
}

} // namespace djmix
