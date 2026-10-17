#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "analysis/Analysis.h"
#include "analysis/WaveformFile.h"
#include "library/LibraryDatabase.h"
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
// Mouse wheel or pinch zooms around the pointer, shift-wheel or a sideways
// swipe scrolls, dragging pans; + and - zoom, Home shows the whole track.
class TrackView final : public juce::Component,
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // What the view last loaded, to tell whether a record has changed.
    struct Shown
    {
        int64_t id = 0;
        std::filesystem::path analysisFile;
        std::string analyzerVersion;
        AnalysisStatus status = AnalysisStatus::pending;
        int colour = -1;

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

    std::optional<juce::Point<float>> hover;
    double dragAnchorTime = 0.0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackView)
};

} // namespace djmix
