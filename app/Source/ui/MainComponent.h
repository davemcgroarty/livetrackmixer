#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace djmix
{

// Top-level window content. M0 draws an empty window; later milestones split it
// into Library (left), Track view (top) and Arrangement (bottom).
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace djmix
