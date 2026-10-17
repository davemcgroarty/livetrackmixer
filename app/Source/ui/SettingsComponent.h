#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "library/LibraryController.h"

namespace djmix
{

// Settings dialog: where the analyzer is, and where the library and the
// analysis cache are kept.
class SettingsComponent final : public juce::Component
{
public:
    SettingsComponent (AppSettings& settings, LibraryController& controller);
    ~SettingsComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static void show (AppSettings& settings, LibraryController& controller);

private:
    void test();
    void apply();
    void setStatus (const juce::String& text, juce::Colour colour);

    AppSettings& settings;
    LibraryController& controller;

    juce::Label analyzerHeading { {}, "Analyzer" };
    juce::Label analyzerHelp;
    juce::TextEditor analyzerPath;
    juce::TextButton browseButton { "Browse..." };
    juce::TextButton detectButton { "Find on PATH" };
    juce::TextButton testButton { "Test" };
    juce::Label statusLabel;

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
