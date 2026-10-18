#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "AudioOutput.h"
#include "library/LibraryController.h"

namespace djmix
{

// Settings dialog: where the analyzer is, the audio output and where Master
// and Cue go on it, and where the library and the analysis cache are kept.
// Audio changes apply at once; the analyzer path on Save.
class SettingsComponent final : public juce::Component,
                                private juce::ChangeListener
{
public:
    SettingsComponent (AppSettings& settings, LibraryController& controller, AudioOutput& audio);
    ~SettingsComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static void show (AppSettings& settings, LibraryController& controller, AudioOutput& audio);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override; // the audio device changed
    void refreshRouting();
    void testCue();
    void test();
    void apply();
    void setStatus (const juce::String& text, juce::Colour colour);

    AppSettings& settings;
    LibraryController& controller;
    AudioOutput& audio;

    juce::Label analyzerHeading { {}, "Analyzer" };
    juce::Label analyzerHelp;
    juce::TextEditor analyzerPath;
    juce::TextButton browseButton { "Browse..." };
    juce::TextButton detectButton { "Find on PATH" };
    juce::TextButton testButton { "Test" };
    juce::Label statusLabel;

    juce::Label audioHeading { {}, "Audio output" };
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label masterLabel { {}, "Master" };
    juce::ComboBox masterBox;
    juce::Label cueLabel { {}, "Cue" };
    juce::ComboBox cueBox;
    juce::Label volumeLabel { {}, "Cue volume" };
    juce::Slider cueVolume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton testCueButton { "Test cue" };
    juce::Label routingLabel;

    juce::Label storageHeading { {}, "Storage" };
    juce::Label libraryLabel;
    juce::TextButton showLibraryButton { "Show" };
    juce::Label cacheLabel;
    juce::TextButton showCacheButton { "Show" };

    juce::TextButton saveButton { "Save" };
    juce::TextButton cancelButton { "Cancel" };

    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsComponent)
};

} // namespace djmix
