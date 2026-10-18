#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Snap.h"

namespace djmix
{

// The bar above the Arrangement: play and pause, the playhead's position,
// the project tempo, the snap mode, follow, what is rendering, and the
// project file buttons.
class TransportBar final : public juce::Component
{
public:
    TransportBar();

    void setPlaying (bool playing);
    void setPosition (double beat);
    void setTempo (double bpm);
    void setSnapMode (SnapMode mode);
    void setFollow (bool follow);
    void setStatus (const juce::String& status);

    std::function<void()> onPlayPause;
    std::function<void (double bpm)> onTempo;
    std::function<void (SnapMode)> onSnapMode;
    std::function<void (bool)> onFollow;
    std::function<void()> onNew;
    std::function<void()> onOpen;
    std::function<void()> onSaveAs;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void commitTempo();

    juce::TextButton play { "Play" };
    juce::Label position;
    juce::Label tempoLabel { {}, "Tempo" };
    juce::TextEditor tempo;
    juce::Label snapLabel { {}, "Snap" };
    juce::ComboBox snap;
    juce::ToggleButton follow { "Follow" };
    juce::Label status;
    juce::TextButton newButton { "New" };
    juce::TextButton openButton { "Open..." };
    juce::TextButton saveAsButton { "Save As..." };
    double shownTempo = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace djmix
