#include "MainComponent.h"

#include "SettingsComponent.h"

namespace djmix
{

namespace
{
// A grid or section edit to one track, undoable.
class OverridesEdit final : public juce::UndoableAction
{
public:
    OverridesEdit (LibraryController& c, int64_t id, TrackOverrides from, TrackOverrides to)
        : controller (c), trackId (id), before (std::move (from)), after (std::move (to))
    {
    }

    bool perform() override { return apply (after); }
    bool undo() override { return apply (before); }

private:
    bool apply (const TrackOverrides& overrides)
    {
        // The track may have been removed since.
        if (controller.findTrack (trackId) == nullptr)
            return false;
        controller.setOverrides (trackId, overrides);
        return true;
    }

    LibraryController& controller;
    int64_t trackId;
    TrackOverrides before, after;
};
} // namespace

MainComponent::MainComponent (AppSettings& s, LibraryController& c, AudioOutput& a, AuditionController& au)
    : settings (s), controller (c), audio (a), audition (au), library (c)
{
    addAndMakeVisible (library);
    addAndMakeVisible (divider);
    addAndMakeVisible (trackView);
    addAndMakeVisible (arrangement);

    library.onOpenSettings = [this] { SettingsComponent::show (settings, controller, audio); };
    library.onSelectionChanged = [this] (int64_t id)
    {
        selectedTrack = id;
        const auto* track = controller.findTrack (id);
        trackView.showTrack (track);
        if (track != nullptr)
            audition.prepare (track->id, track->path); // decode ahead, so a click plays at once
    };

    // Auditions: sections and phrases from the Track view, whole tracks from the Library.
    trackView.onAudition = [this] (double start, double end)
    {
        if (const auto* track = controller.findTrack (selectedTrack))
            audition.play (track->id, track->path, start, end);
    };
    trackView.onStopAudition = [this] { audition.stop(); };
    trackView.onEditOverrides = [this] (const TrackOverrides& next, bool continuing)
    {
        const auto* track = controller.findTrack (selectedTrack);
        if (track == nullptr)
            return;
        if (! continuing)
            undo.beginNewTransaction();
        undo.perform (new OverridesEdit (controller, track->id, track->overrides, next));
    };
    audition.setMode (settings.cueMode());
    trackView.setCueMode (settings.cueMode());
    trackView.onCycleCueMode = [this]
    {
        const auto mode = nextCueMode (audition.mode());
        audition.setMode (mode);
        settings.setCueMode (mode);
        trackView.setCueMode (mode);
    };
    library.onAuditionTrack = [this] (int64_t id)
    {
        const auto now = audition.status();
        if (now.trackId == id && (now.playing || now.loading) && juce::exactlyEqual (now.start, 0.0)
            && juce::exactlyEqual (now.end, 0.0))
            audition.stop(); // double-clicking it again stops it
        else if (const auto* track = controller.findTrack (id))
            audition.play (id, track->path, 0.0, 0.0);
    };
    startTimerHz (60);
    controller.addChangeListener (this);

    layout.setItemLayout (0, 360, -0.8, -0.42); // library
    layout.setItemLayout (1, 6, 6, 6);          // divider
    layout.setItemLayout (2, 300, -1.0, -0.58); // track view and arrangement

    setSize (1280, 800);
}

MainComponent::~MainComponent()
{
    stopTimer();
    controller.removeChangeListener (this);
}

void MainComponent::timerCallback()
{
    const auto now = audition.status();
    const bool showing = now.trackId != 0 && now.trackId == selectedTrack
                         && (now.playing || now.loading || ! now.error.empty());
    if (! showing)
    {
        trackView.setAudition (std::nullopt);
        return;
    }
    TrackView::Audition a;
    a.start = now.start;
    a.end = now.end;
    a.position = now.position;
    a.playing = now.playing;
    a.loading = now.loading;
    a.error = juce::String::fromUTF8 (now.error.c_str());
    trackView.setAudition (a);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Esc stops an audition wherever the keyboard focus is.
    if (key == juce::KeyPress::escapeKey)
    {
        audition.stop();
        return true;
    }

    // Undo and redo grid and section edits: Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z or Ctrl+Y.
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        if (mods.isShiftDown())
            undo.redo();
        else
            undo.undo();
        return true;
    }
    if (mods.isCommandDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y'))
    {
        undo.redo();
        return true;
    }
    return false;
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The selected track may have finished (re-)analysis, changed colour or gone.
    if (selectedTrack != 0)
        trackView.showTrack (controller.findTrack (selectedTrack));
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
    trackView.setBounds (right.removeFromTop (right.getHeight() / 2));
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
