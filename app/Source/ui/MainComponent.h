#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "AudioOutput.h"
#include "LibraryPanel.h"
#include "TrackView.h"
#include "engine/AuditionController.h"
#include "library/LibraryController.h"

namespace djmix
{

// Top-level window content: the Library on the left, the Track view on top
// and the Arrangement below. The Arrangement arrives in M4.
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent (AppSettings& settings, LibraryController& controller, AudioOutput& audio, AuditionController& audition);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override; // follows the audition for the Track view

    class Placeholder final : public juce::Component
    {
    public:
        explicit Placeholder (juce::String textToShow) : text (std::move (textToShow)) {}
        void paint (juce::Graphics&) override;
        juce::String text;
    };

    AppSettings& settings;
    LibraryController& controller;
    AudioOutput& audio;
    AuditionController& audition;

    LibraryPanel library;
    TrackView trackView;
    int64_t selectedTrack = 0;
    Placeholder arrangement { "Arrangement" };
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar divider { &layout, 1, true };
    juce::TooltipWindow tooltips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace djmix
