#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/ArrangementPlayer.h"
#include "library/TrackCatalog.h"
#include "model/Project.h"
#include "model/Snap.h"
#include "view/ViewRange.h"

namespace djmix
{

// The Arrangement: a timeline in bars at project tempo, with layers stacked
// vertically and clips on them. Clips show their waveform and section lines
// in their track's colour.
//
// Drag a clip to move it (to another layer too), or its edges to trim it;
// both snap to the snap mode (hold Alt to place freely). Click the ruler or
// an empty lane to move the playhead. Drop a section, phrase or track here to
// add a clip; dropping below the last layer adds a layer for it.
//
// Keys: Space plays and pauses, S splits at the playhead, Delete removes the
// selected clip, 1-4 choose the snap mode, + and - zoom, Home shows the whole
// arrangement.
//
// The view never changes the project itself: each edit goes to onEdit, and
// comes back through setProject once applied (so it can be undone).
class ArrangementView final : public juce::Component,
                              public juce::DragAndDropTarget,
                              private juce::ScrollBar::Listener
{
public:
    using MaterialFor = std::function<std::shared_ptr<const TrackMaterial> (int64_t trackId)>;
    using ClipStateFor = std::function<ArrangementPlayer::ClipState (int64_t clipId)>;

    ArrangementView();
    ~ArrangementView() override;

    void setMaterialSource (MaterialFor source) { materialFor = std::move (source); }
    void setClipStateSource (ClipStateFor source) { clipStateFor = std::move (source); }

    void setProject (const Project& project);
    const Project& shownProject() const { return project; }

    void setPlayhead (double beat, bool playing);
    double playhead() const { return playheadBeat; }

    void setSnapMode (SnapMode mode);
    SnapMode snapMode() const { return snap; }

    // Shown in the corner above the layer names: the project's name.
    void setTitle (const juce::String& text);

    // Keeps the playhead a third of the way across while playing.
    void setFollow (bool follow);
    bool isFollowing() const { return following; }

    std::optional<int64_t> selectedClip() const { return selected; }
    void selectClip (std::optional<int64_t> clipId);

    // A change the user made, to apply. `name` describes it for undo.
    std::function<void (const Project& next, const juce::String& name)> onEdit;
    std::function<void (double beat)> onSeek;
    std::function<void()> onTogglePlay;
    std::function<void (SnapMode)> onSnapModeChanged;
    std::function<void (bool)> onFollowChanged;

    // What a drag from the Track view or Library carries to make a clip.
    static juce::var clipDragDescription (int64_t trackId, double sourceStartBeat, double lengthBeats, const juce::String& label);

    // Geometry, for drawing and tests.
    double beatAtX (float x) const;
    float xForBeat (double beat) const;
    int laneAtY (float y) const; // index into the layers; -1 above, numLayers below
    juce::Rectangle<int> laneBounds (int lane) const;
    juce::Rectangle<int> clipBounds (const Clip& clip) const;
    const ViewRange& viewRange() const { return range; }
    void showBeats (double start, double span);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    enum class Drag
    {
        none,
        pan,
        seek,
        move,
        trimStart,
        trimEnd
    };

    // A clip to be added by a drop, where it would go.
    struct Drop
    {
        Clip clip;     // layerId 0: on a new layer
        int lane = 0;
    };

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void rangeChanged();
    void updateLength();
    void zoomAt (float x, double factor);
    void stopFollowing();

    void paintRuler (juce::Graphics&, juce::Rectangle<int> area);
    void paintGrid (juce::Graphics&, juce::Rectangle<int> area);
    void paintHeaders (juce::Graphics&);
    void paintClip (juce::Graphics&, const Clip& clip, juce::Rectangle<int> bounds);
    void paintClipWaveform (juce::Graphics&, const Clip& clip, const TrackMaterial& material, juce::Rectangle<int> bounds);
    void paintPlayhead (juce::Graphics&);

    const Project& current() const { return preview ? *preview : project; }
    const Clip* clipAt (juce::Point<float> position) const;
    double snapped (double beat, const juce::ModifierKeys& mods) const;
    std::optional<Drop> dropFor (const SourceDetails& details) const;
    void edit (const Project& next, const juce::String& name);
    void splitAtPlayhead();
    void removeSelected();
    void showLayerMenu (int lane);
    void showClipMenu (const Clip& clip);

    // The source beats of a clip's track that hold audio, if known.
    std::optional<std::pair<double, double>> sourceLimits (int64_t trackId) const;

    // Header buttons for a lane: mute and solo.
    juce::Rectangle<int> muteButton (int lane) const;
    juce::Rectangle<int> soloButton (int lane) const;
    juce::Rectangle<int> addLayerButton() const;

    Project project;
    std::optional<Project> preview; // while a clip is dragged
    MaterialFor materialFor;
    ClipStateFor clipStateFor;

    ViewRange range;
    juce::Rectangle<int> rulerArea, headerArea, lanesArea;
    juce::ScrollBar scrollBar { false };
    int laneHeight = 64;
    juce::String title;

    double playheadBeat = 0.0;
    bool playing = false;
    bool following = true;
    SnapMode snap = SnapMode::bar;
    std::optional<int64_t> selected;

    Drag drag = Drag::none;
    bool dragging = false;
    double dragAnchorBeat = 0.0;
    int64_t dragClip = 0;
    Clip dragOriginal;
    std::optional<Drop> drop;
    std::optional<juce::Point<float>> hover;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace djmix
