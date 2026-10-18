#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "analysis/Analysis.h"
#include "analysis/WaveformFile.h"
#include "engine/CueMode.h"
#include "library/LibraryDatabase.h"
#include "view/EffectiveAnalysis.h"
#include "view/TapTempo.h"
#include "view/TrackStructure.h"
#include "view/ViewRange.h"
#include "view/WaveformPyramid.h"

namespace djmix
{

// The Track view: one track's waveform over its bar grid, with phrase and
// section boundaries, per-bar bass and high-end blocks in the track colour,
// and the waveform's contrast set by each bar's rhythm score. Hovering shows
// a bar's (or, over the section strip, a section's) four content scores.
//
// Clicking a section in the strip, or a phrase in the waveform, asks for it to
// be auditioned on Cue (onAudition); clicking it again, the Stop button or Esc
// stops (onStopAudition). setAudition() shows what is playing. The cue mode
// button in the header (onCycleCueMode) steps through once, loop and continue.
//
// Mouse wheel or pinch zooms around the pointer, shift-wheel or a sideways
// swipe scrolls, dragging pans; + and - zoom, Home shows the whole track.
//
// Grid and section edits (M3) are hand corrections over the analysis
// (TrackOverrides), sent to onEditOverrides to save: right-click for the edit
// menu, Alt-drag to move the grid, drag a section boundary in the strip,
// Alt+arrows to nudge, D to set the downbeat at the pointer, T to tap tempo.
class TrackView final : public juce::Component,
                        public juce::TooltipClient,
                        private juce::ScrollBar::Listener
{
public:
    TrackView();
    ~TrackView() override;

    // Shows a library track, loading its analysis if it has one. Call again
    // whenever the record may have changed: it reloads only when needed, and
    // keeps the zoom when the same track is reloaded. nullptr clears the view.
    void showTrack (const TrackRecord* track);

    // Shows an analysis that is already loaded.
    void showAnalysis (const TrackAnalysis& analysis,
                       const WaveformOverview& waveform,
                       juce::Colour colour,
                       const juce::String& title,
                       const juce::String& artist);

    // Clears the view and shows `message` in its place.
    void showMessage (const juce::String& message);

    // An audition of this track on Cue, as the view shows it.
    struct Audition
    {
        double start = 0.0;
        double end = 0.0; // 0: to the end of the track
        double position = 0.0;
        bool playing = false;
        bool loading = false;
        juce::String error;

        bool operator== (const Audition&) const = default;
    };

    std::function<void (double start, double end)> onAudition;
    std::function<void()> onStopAudition;
    std::function<void()> onCycleCueMode;

    // An edit to the grid or sections, to save (and undo). `continuing` is true
    // for a later step of the same gesture (more tap-tempo taps), so it can join
    // the same undo step.
    std::function<void (const TrackOverrides& overrides, bool continuing)> onEditOverrides;

    // Shows the track with these corrections (the analysis is kept as loaded).
    void setOverrides (const TrackOverrides& overrides);
    const TrackOverrides& getOverrides() const { return overrides; }
    // The analysis with the corrections applied, as drawn.
    const TrackAnalysis& effective() const { return structure.data(); }

    // What the cue mode button shows.
    void setCueMode (CueMode mode);

    // What is auditioning now; nothing when this track is not.
    void setAudition (std::optional<Audition> audition);

    bool hasAnalysis() const { return loaded; }
    const ViewRange& viewRange() const { return range; }
    void showRange (double start, double span);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::String getTooltip() override;

private:
    // What the view last loaded, to tell whether a record has changed.
    struct Shown
    {
        int64_t id = 0;
        std::filesystem::path analysisFile;
        std::string analyzerVersion;
        AnalysisStatus status = AnalysisStatus::pending;
        int colour = -1;
        TrackOverrides overrides;

        bool operator== (const Shown&) const = default;
    };

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void rangeChanged();
    void zoomAt (float x, double factor);

    void paintHeader (juce::Graphics&, juce::Rectangle<int> area);
    void paintSectionStrip (juce::Graphics&, juce::Rectangle<int> area);
    void paintBars (juce::Graphics&, juce::Rectangle<int> area);
    void paintWaveform (juce::Graphics&, juce::Rectangle<int> area);
    void paintGrid (juce::Graphics&, juce::Rectangle<int> area);
    void paintHover (juce::Graphics&);
    void paintAudition (juce::Graphics&);
    void paintCueModeIcon (juce::Graphics&, juce::Rectangle<float> area) const;

    void handleClick (juce::Point<float> position);
    void showEditMenu (juce::Point<float> position);
    void askForBpm();
    void tapTempo();
    // By value: callers pass *preview, which this clears.
    void commit (TrackOverrides next, bool continuing = false);
    void applyOverrides(); // rebuilds the structure from base + (preview or overrides)
    int boundaryNear (float x) const; // a section (not the first) whose start is under x, or -1
    double timeAt (float x) const { return range.xToTime (x - static_cast<float> (waveArea.getX())); }
    void requestAudition (double start, double end);
    juce::Rectangle<int> playheadArea (const Audition&) const;
    float timeToX (double seconds) const;

    std::optional<Shown> shown;
    bool loaded = false;
    juce::String message { "Select a track in the Library" };
    juce::String title, artist, status;

    TrackStructure structure;
    WaveformPyramid pyramid;
    ViewRange range;
    juce::Colour colour { 0xff4f9fe0 };

    juce::Rectangle<int> headerArea, stripArea, waveArea;
    juce::ScrollBar scrollBar { false };

    std::optional<Audition> audition;
    CueMode cueMode = CueMode::once;
    juce::Rectangle<int> stopButtonArea, modeButtonArea;

    std::optional<juce::Point<float>> hover;
    double dragAnchorTime = 0.0;
    bool dragging = false;

    // M3 editing.
    enum class DragKind
    {
        pan,
        grid,
        boundary
    };
    TrackAnalysis base; // as analysed
    TrackOverrides overrides;
    std::optional<TrackOverrides> preview; // while a grid or boundary drag is under way
    DragKind dragKind = DragKind::pan;
    int dragSection = -1;
    TapTempo taps;
    bool tapping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackView)
};

} // namespace djmix
