#include "MainComponent.h"

#include "SettingsComponent.h"

namespace djmix
{

MainComponent::MainComponent (AppSettings& s, LibraryController& c)
    : settings (s), controller (c), library (c)
{
    addAndMakeVisible (library);
    addAndMakeVisible (divider);
    addAndMakeVisible (trackView);
    addAndMakeVisible (arrangement);

    library.onOpenSettings = [this] { SettingsComponent::show (settings, controller); };
    library.onSelectionChanged = [this] (int64_t id)
    {
        const auto* track = controller.findTrack (id);
        trackView.text = track != nullptr ? "Track view: " + juce::String::fromUTF8 (track->title.c_str())
                                          : "Track view: select a track";
        trackView.repaint();
    };

    layout.setItemLayout (0, 360, -0.8, -0.42); // library
    layout.setItemLayout (1, 6, 6, 6);          // divider
    layout.setItemLayout (2, 300, -1.0, -0.58); // track view and arrangement

    setSize (1280, 800);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d21));
}

void MainComponent::resized()
{
    juce::Component dummy; // the right-hand column is laid out below
    juce::Component* columns[] = { &library, &divider, &dummy };
    layout.layOutComponents (columns, 3, 0, 0, getWidth(), getHeight(), false, true);

    auto right = dummy.getBounds().reduced (6);
    trackView.setBounds (right.removeFromTop (right.getHeight() * 2 / 5));
    right.removeFromTop (6);
    arrangement.setBounds (right);
}

void MainComponent::Placeholder::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff23282d));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (juce::Colours::grey);
    g.setFont (15.0f);
    g.drawFittedText (text, getLocalBounds().reduced (12), juce::Justification::centred, 2);
}

} // namespace djmix
