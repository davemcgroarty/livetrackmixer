#include "TransportBar.h"

#include <cmath>

#include "model/Project.h"

namespace djmix
{

namespace
{
const SnapMode snapModes[] = { SnapMode::off, SnapMode::beat, SnapMode::bar, SnapMode::phrase };
}

TransportBar::TransportBar()
{
    for (auto* c : std::initializer_list<juce::Component*> { &play, &position, &tempoLabel, &tempo, &snapLabel, &snap, &follow,
                                                             &status, &newButton, &openButton, &saveAsButton })
        addAndMakeVisible (c);

    play.setTooltip ("Play or pause (Space)");
    play.onClick = [this] { if (onPlayPause) onPlayPause(); };

    position.setFont (juce::FontOptions (14.0f));
    position.setColour (juce::Label::textColourId, juce::Colour (0xffe6eaee));
    position.setTooltip ("Bar and beat at the playhead");

    tempo.setInputRestrictions (6, "0123456789.");
    tempo.setJustification (juce::Justification::centred);
    tempo.setTooltip ("Project tempo (BPM)");
    tempo.onReturnKey = [this] { commitTempo(); unfocusAllComponents(); };
    tempo.onFocusLost = [this] { commitTempo(); };
    tempo.onEscapeKey = [this] { setTempo (shownTempo); unfocusAllComponents(); };

    for (int i = 0; i < 4; ++i)
        snap.addItem (snapModeLabel (snapModes[i]), i + 1);
    snap.setTooltip ("What clips snap to (1-4); hold Alt to place freely");
    snap.onChange = [this]
    {
        const int i = snap.getSelectedId() - 1;
        if (i >= 0 && i < 4 && onSnapMode)
            onSnapMode (snapModes[i]);
    };

    follow.setTooltip ("Keep the playhead in view while playing");
    follow.onClick = [this] { if (onFollow) onFollow (follow.getToggleState()); };

    status.setColour (juce::Label::textColourId, juce::Colour (0xff8a949e));
    status.setFont (juce::FontOptions (12.0f));

    newButton.setTooltip ("Start a new project");
    newButton.onClick = [this] { if (onNew) onNew(); };
    openButton.setTooltip ("Open a project");
    openButton.onClick = [this] { if (onOpen) onOpen(); };
    saveAsButton.setTooltip ("Save the project under a new name (projects save themselves as you go)");
    saveAsButton.onClick = [this] { if (onSaveAs) onSaveAs(); };
}

void TransportBar::setPlaying (bool playing)
{
    play.setButtonText (playing ? "Pause" : "Play");
}

void TransportBar::setPosition (double beat)
{
    // Bar.beat, both from 1.
    const auto whole = static_cast<int64_t> (std::floor (std::max (0.0, beat) + 1e-6));
    const auto text = juce::String (whole / 4 + 1) + "." + juce::String (whole % 4 + 1);
    if (position.getText() != text)
        position.setText (text, juce::dontSendNotification);
}

void TransportBar::setTempo (double bpm)
{
    shownTempo = bpm;
    if (! tempo.hasKeyboardFocus (true))
        tempo.setText (juce::String (bpm, std::abs (bpm - std::round (bpm)) < 1e-6 ? 0 : 2), false);
}

void TransportBar::commitTempo()
{
    const double bpm = tempo.getText().getDoubleValue();
    if (bpm >= Project::minTempo && bpm <= Project::maxTempo)
    {
        if (std::abs (bpm - shownTempo) > 1e-9 && onTempo)
            onTempo (bpm);
    }
    else
    {
        setTempo (shownTempo);
    }
}

void TransportBar::setSnapMode (SnapMode mode)
{
    for (int i = 0; i < 4; ++i)
        if (snapModes[i] == mode)
            snap.setSelectedId (i + 1, juce::dontSendNotification);
}

void TransportBar::setFollow (bool on)
{
    follow.setToggleState (on, juce::dontSendNotification);
}

void TransportBar::setStatus (const juce::String& text)
{
    if (status.getText() != text)
        status.setText (text, juce::dontSendNotification);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff2a3036));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (6, 4);
    play.setBounds (area.removeFromLeft (60));
    area.removeFromLeft (6);
    position.setBounds (area.removeFromLeft (56));
    tempoLabel.setBounds (area.removeFromLeft (46));
    tempo.setBounds (area.removeFromLeft (56));
    area.removeFromLeft (6);
    snapLabel.setBounds (area.removeFromLeft (38));
    snap.setBounds (area.removeFromLeft (82));
    area.removeFromLeft (6);
    follow.setBounds (area.removeFromLeft (70));

    saveAsButton.setBounds (area.removeFromRight (72));
    area.removeFromRight (4);
    openButton.setBounds (area.removeFromRight (58));
    area.removeFromRight (4);
    newButton.setBounds (area.removeFromRight (46));
    area.removeFromRight (6);
    status.setBounds (area);
}

} // namespace djmix
