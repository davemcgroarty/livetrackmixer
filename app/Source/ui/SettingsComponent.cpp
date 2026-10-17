#include "SettingsComponent.h"

#include "analysis/AnalyzerProtocol.h"
#include "analysis/AnalyzerRunner.h"

namespace djmix
{

namespace
{
void closeDialog (juce::Component& c)
{
    if (auto* window = c.findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}
} // namespace

SettingsComponent::SettingsComponent (AppSettings& s, LibraryController& c)
    : settings (s), controller (c)
{
    for (auto* heading : { &analyzerHeading, &storageHeading })
    {
        addAndMakeVisible (*heading);
        heading->setFont (juce::FontOptions (16.0f, juce::Font::bold));
    }

    addAndMakeVisible (analyzerHelp);
    analyzerHelp.setText ("The djanalyze program from analyzer/. In a virtual environment it is "
                          ".venv\\Scripts\\djanalyze.exe on Windows or .venv/bin/djanalyze elsewhere. "
                          "A Python interpreter with djanalyze installed also works.",
                          juce::dontSendNotification);
    analyzerHelp.setColour (juce::Label::textColourId, juce::Colours::grey);

    addAndMakeVisible (analyzerPath);
    analyzerPath.setText (toFile (controller.analyzerExecutable()).getFullPathName());
    if (controller.analyzerExecutable().empty())
        analyzerPath.setText ({});
    analyzerPath.setTextToShowWhenEmpty ("Path to djanalyze", juce::Colours::grey);

    addAndMakeVisible (browseButton);
    browseButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose djanalyze", juce::File (analyzerPath.getText()));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                  {
                                      analyzerPath.setText (fc.getResult().getFullPathName());
                                      test();
                                  }
                              });
    };

    addAndMakeVisible (detectButton);
    detectButton.onClick = [this]
    {
        if (const auto found = findOnPath ("djanalyze"))
        {
            analyzerPath.setText (toFile (*found).getFullPathName());
            test();
        }
        else
        {
            setStatus ("djanalyze is not on PATH.", juce::Colour (0xffe06a5f));
        }
    };

    addAndMakeVisible (testButton);
    testButton.onClick = [this] { test(); };
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (libraryLabel);
    libraryLabel.setText ("Library: " + toFile (settings.libraryFile()).getFullPathName(), juce::dontSendNotification);
    addAndMakeVisible (showLibraryButton);
    showLibraryButton.onClick = [this] { toFile (settings.libraryFile()).revealToUser(); };

    addAndMakeVisible (cacheLabel);
    cacheLabel.setText ("Analysis cache: " + toFile (settings.analysisCacheDirectory()).getFullPathName(),
                        juce::dontSendNotification);
    addAndMakeVisible (showCacheButton);
    showCacheButton.onClick = [this]
    {
        auto dir = toFile (settings.analysisCacheDirectory());
        dir.createDirectory();
        dir.startAsProcess();
    };

    addAndMakeVisible (saveButton);
    saveButton.onClick = [this] { apply(); closeDialog (*this); };
    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] { closeDialog (*this); };

    setSize (640, 350);
    if (! controller.analyzerExecutable().empty())
        test();
}

SettingsComponent::~SettingsComponent()
{
    *alive = false;
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    analyzerHeading.setBounds (area.removeFromTop (24));
    analyzerHelp.setBounds (area.removeFromTop (60));
    auto row = area.removeFromTop (28);
    detectButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (6);
    browseButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (6);
    analyzerPath.setBounds (row);
    area.removeFromTop (6);
    row = area.removeFromTop (26);
    testButton.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (8);
    statusLabel.setBounds (row);

    area.removeFromTop (16);
    storageHeading.setBounds (area.removeFromTop (24));
    row = area.removeFromTop (26);
    showLibraryButton.setBounds (row.removeFromRight (60));
    libraryLabel.setBounds (row);
    row = area.removeFromTop (26);
    showCacheButton.setBounds (row.removeFromRight (60));
    cacheLabel.setBounds (row);

    auto buttons = area.removeFromBottom (30);
    saveButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

void SettingsComponent::show (AppSettings& settings, LibraryController& controller)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new SettingsComponent (settings, controller));
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = juce::Colour (0xff262b30);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void SettingsComponent::test()
{
    const auto exe = toPath (juce::File (analyzerPath.getText().trim()));
    if (analyzerPath.getText().trim().isEmpty())
        return setStatus ("No analyzer set.", juce::Colour (0xffe06a5f));

    setStatus ("Testing...", juce::Colours::grey);
    testButton.setEnabled (false);

    // The first run of djanalyze can take a few seconds while Python loads.
    std::weak_ptr<bool> weak = alive;
    juce::Thread::launch ([this, weak, exe]
    {
        const auto version = AnalyzerRunner::probeVersion (exe);
        juce::MessageManager::callAsync ([this, weak, version]
        {
            if (auto ok = weak.lock(); ! ok || ! *ok)
                return;
            testButton.setEnabled (true);
            if (version)
                setStatus ("Found " + juce::String (*version) + ".", juce::Colour (0xff6fd16a));
            else
                setStatus ("That did not run as djanalyze.", juce::Colour (0xffe06a5f));
        });
    });
}

void SettingsComponent::apply()
{
    const auto text = analyzerPath.getText().trim();
    const auto exe = text.isEmpty() ? std::filesystem::path() : toPath (juce::File (text));
    settings.setAnalyzerExecutable (exe);
    controller.setAnalyzerExecutable (exe);
}

void SettingsComponent::setStatus (const juce::String& text, juce::Colour colour)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);
}

} // namespace djmix
