#include "AppSettings.h"

#include "analysis/AnalyzerProtocol.h"
#include "library/LibraryDatabase.h"

namespace djmix
{

namespace
{
constexpr const char* analyzerKey = "analyzerExecutable";
}

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

} // namespace djmix
