#include "SettingsComponent.h"

#include "analysis/AnalyzerProtocol.h"
#include "analysis/AnalyzerRunner.h"

#include <cmath>

namespace djmix
{

namespace
{
void closeDialog (juce::Component& c)
{
    if (auto* window = c.findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

constexpr int splitCueId = 1000;

// Three short 1 kHz beeps, to check where Cue comes out.
std::shared_ptr<DecodedAudio> testBeeps()
{
    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = 44100.0;
    const int length = static_cast<int> (1.2 * audio->sampleRate);
    audio->samples.setSize (2, length);
    audio->samples.clear();
    for (int i = 0; i < length; ++i)
    {
        const double t = i / audio->sampleRate;
        const bool on = std::fmod (t, 0.4) < 0.2;
        const auto v = on ? static_cast<float> (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t)) : 0.0f;
        audio->samples.setSample (0, i, v);
        audio->samples.setSample (1, i, v);
    }
    return audio;
}
} // namespace

SettingsComponent::SettingsComponent (AppSettings& s, LibraryController& c, AudioOutput& a)
    : settings (s),
      controller (c),
      audio (a),
      // Channel choice is the Master and Cue boxes below, not JUCE's channel
      // list, which only shows when a device has more outputs than the minimum.
      deviceSelector (a.devices(), 0, 0, 256, 256, false, false, true, false)
{
    for (auto* heading : { &analyzerHeading, &audioHeading, &storageHeading })
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

    addAndMakeVisible (deviceSelector);
    for (auto* label : { &masterLabel, &cueLabel, &volumeLabel })
        addAndMakeVisible (*label);
    addAndMakeVisible (masterBox);
    masterBox.onChange = [this]
    {
        auto r = audio.routing();
        r.masterFirst = masterBox.getSelectedId() - 1;
        audio.setRouting (r);
        refreshRouting();
    };
    addAndMakeVisible (cueBox);
    cueBox.onChange = [this]
    {
        auto r = audio.routing();
        r.splitCue = cueBox.getSelectedId() == splitCueId;
        if (! r.splitCue)
            r.cueFirst = cueBox.getSelectedId() - 1;
        audio.setRouting (r);
        refreshRouting();
    };
    addAndMakeVisible (cueVolume);
    cueVolume.setRange (0.0, 100.0, 1.0);
    cueVolume.setTextValueSuffix (" %");
    cueVolume.setValue (audio.cueVolume() * 100.0, juce::dontSendNotification);
    cueVolume.onValueChange = [this] { audio.setCueVolume (static_cast<float> (cueVolume.getValue() / 100.0)); };
    addAndMakeVisible (testCueButton);
    testCueButton.onClick = [this] { testCue(); };
    addAndMakeVisible (routingLabel);
    routingLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    audio.devices().addChangeListener (this);
    refreshRouting();

    addAndMakeVisible (saveButton);
    saveButton.onClick = [this] { apply(); closeDialog (*this); };
    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] { closeDialog (*this); };

    setSize (640, 620);
    if (! controller.analyzerExecutable().empty())
        test();
}

SettingsComponent::~SettingsComponent()
{
    *alive = false;
    audio.devices().removeChangeListener (this);
}

void SettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshRouting();
}

void SettingsComponent::refreshRouting()
{
    const int outputs = audio.numOutputs();
    const auto routing = audio.routing();

    masterBox.clear (juce::dontSendNotification);
    cueBox.clear (juce::dontSendNotification);
    for (const int first : stereoPairs (outputs))
    {
        const auto name = "Outputs " + juce::String (pairName (first));
        masterBox.addItem (name, first + 1);
        cueBox.addItem (name, first + 1);
    }
    cueBox.addItem ("Split: Cue left, Master right (mono)", splitCueId);
    masterBox.setSelectedId (routing.masterFirst + 1, juce::dontSendNotification);
    cueBox.setSelectedId (routing.splitCue ? splitCueId : routing.cueFirst + 1, juce::dontSendNotification);

    const bool open = audio.isOpen();
    masterBox.setEnabled (open);
    cueBox.setEnabled (open);
    testCueButton.setEnabled (open);

    juce::String text;
    if (! open)
    {
        text = "No audio device is open" + (audio.openError().isNotEmpty() ? " (" + audio.openError() + ")." : juce::String ("."));
    }
    else
    {
        const auto resolved = audio.engine().resolvedRouting();
        if (resolved.split && ! routing.splitCue)
            text = "Outputs " + juce::String (pairName (routing.cueFirst))
                   + " are not available, so Cue plays in mono on the left of the Master pair and Master on the right.";
        else if (resolved.split)
            text = "Cue plays in mono on the left, Master in mono on the right.";
        else
            text = "Master on outputs " + juce::String (pairName (routing.masterFirst)) + ", Cue on outputs "
                   + juce::String (pairName (routing.cueFirst)) + ".";
    }
    routingLabel.setText (text, juce::dontSendNotification);
}

void SettingsComponent::testCue()
{
    if (! audio.cue().play (testBeeps(), 0.0, 0.0, 0))
        routingLabel.setText ("No audio device is open.", juce::dontSendNotification);
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
    audioHeading.setBounds (area.removeFromTop (24));
    deviceSelector.setBounds (area.removeFromTop (150));
    row = area.removeFromTop (26);
    masterLabel.setBounds (row.removeFromLeft (60));
    masterBox.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (16);
    cueLabel.setBounds (row.removeFromLeft (40));
    cueBox.setBounds (row.removeFromLeft (290));
    area.removeFromTop (6);
    row = area.removeFromTop (26);
    volumeLabel.setBounds (row.removeFromLeft (80));
    testCueButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    cueVolume.setBounds (row);
    routingLabel.setBounds (area.removeFromTop (40));

    area.removeFromTop (8);
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

void SettingsComponent::show (AppSettings& settings, LibraryController& controller, AudioOutput& audio)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new SettingsComponent (settings, controller, audio));
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
