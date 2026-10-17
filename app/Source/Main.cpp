#include <juce_gui_extra/juce_gui_extra.h>

#include "AppSettings.h"
#include "library/LibraryController.h"
#include "library/LibraryDatabase.h"
#include "ui/MainComponent.h"

namespace
{
// Opens the library, or an empty in-memory one if the file cannot be used, so
// the app still starts and says why.
std::unique_ptr<djmix::LibraryDatabase> openLibrary (const djmix::AppSettings& settings)
{
    try
    {
        return std::make_unique<djmix::LibraryDatabase> (settings.libraryFile());
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Library not available",
            "The track library at " + djmix::toFile (settings.libraryFile()).getFullPathName()
                + " could not be opened (" + juce::String (e.what())
                + "). Changes this session will not be saved.");
        return std::make_unique<djmix::LibraryDatabase> (":memory:");
    }
}
} // namespace

class DJMixerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        settings = std::make_unique<djmix::AppSettings>();
        library = std::make_unique<djmix::LibraryController> (openLibrary (*settings),
                                                              settings->analyzerExecutable(),
                                                              settings->analysisCacheDirectory());
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings, *library);
        importFromCommandLine (commandLine);
    }

    // Files opened with the app, or dropped on its icon, while it is running.
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        importFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        library = nullptr; // stops any running analysis
        settings = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    // Audio files and folders named on the command line go into the library.
    void importFromCommandLine (const juce::String& commandLine)
    {
        std::vector<std::filesystem::path> paths;
        for (const auto& argument : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File file (argument.unquoted());
            if (juce::File::isAbsolutePath (argument.unquoted()) && file.exists())
                paths.push_back (djmix::toPath (file));
        }
        if (! paths.empty() && library != nullptr)
            library->importPaths (paths);
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, djmix::AppSettings& settings, djmix::LibraryController& library)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new djmix::MainComponent (settings, library), true);
            setResizable (true, true);
            setResizeLimits (900, 560, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<djmix::AppSettings> settings;
    std::unique_ptr<djmix::LibraryController> library;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (DJMixerApplication)
