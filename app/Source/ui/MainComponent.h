#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "LibraryPanel.h"
#include "library/LibraryController.h"

namespace djmix
{

// Top-level window content: the Library on the left, the Track view on top
// and the Arrangement below. Track view and Arrangement arrive in M2b and M4.
class MainComponent final : public juce::Component
{
public:
    MainComponent (AppSettings& settings, LibraryController& controller);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Placeholder final : public juce::Component
    {
    public:
        explicit Placeholder (juce::String textToShow) : text (std::move (textToShow)) {}
        void paint (juce::Graphics&) override;
        juce::String text;
    };

    AppSettings& settings;
    LibraryController& controller;

    LibraryPanel library;
    Placeholder trackView { "Track view: select a track" };
    Placeholder arrangement { "Arrangement" };
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar divider { &layout, 1, true };
    juce::TooltipWindow tooltips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace djmix
