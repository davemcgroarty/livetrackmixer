#include "ArrangementView.h"

#include <algorithm>
#include <cmath>

#include "library/TrackColours.h"
#include "model/BeatMap.h"
#include "model/ProjectEdits.h"

namespace djmix
{

namespace
{
constexpr int headerWidth = 132;
constexpr int rulerHeight = 22;
constexpr int scrollBarHeight = 10;
constexpr int addRowHeight = 26;
constexpr int minLaneHeight = 36;
constexpr int maxLaneHeight = 96;
constexpr int edgeZone = 6;           // pixels at a clip's ends that trim it
constexpr double minSpanBeats = 4.0;
constexpr double spareBeats = 64.0;   // room after the last clip
constexpr double minLengthBeats = 256.0;
constexpr double wheelZoomSpeed = 2.5;
constexpr double stretchWarning = 0.08; // design spec: warn beyond 8%

const juce::Colour background { 0xff1e2226 };
const juce::Colour laneColour { 0xff23282d };
const juce::Colour laneAltColour { 0xff262b31 };
const juce::Colour headerColour { 0xff2a3036 };
const juce::Colour textColour { 0xffd8dde2 };
const juce::Colour dimText { 0xff8a949e };
const juce::Colour waveColour { 0xffe6eaee };
const juce::Colour playheadColour { 0xfff4f6f8 };
const juce::Colour phraseMarkColour { 0xffffd166 };
const juce::Colour muteColour { 0xffe0a040 };
const juce::Colour soloColour { 0xff5fbf6a };

juce::String fromUtf8 (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

juce::Colour colourFor (int index)
{
    return juce::Colour (trackPalette[static_cast<size_t> (std::clamp (index, 0, int (trackPalette.size()) - 1))].argb);
}

// Whether a whole beat number starts a bar or phrase.
bool isMultiple (double beat, double step)
{
    return std::llround (beat) % std::llround (step) == 0;
}

// Whole-pixel rectangles filled one by one, which keeps to the renderer's
// integer path (see TrackView).
void fillEach (juce::Graphics& g, const juce::RectangleList<int>& rects)
{
    for (const auto& r : rects)
        g.fillRect (r);
}
} // namespace

ArrangementView::ArrangementView()
{
    setWantsKeyboardFocus (true);
    range = ViewRange (minLengthBeats, minSpanBeats);
    range.show (0.0, 128.0);

    addAndMakeVisible (scrollBar);
    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);
    scrollBar.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff5a646e));
    scrollBar.setColour (juce::ScrollBar::trackColourId, background);
}

ArrangementView::~ArrangementView()
{
    scrollBar.removeListener (this);
}

juce::var ArrangementView::clipDragDescription (int64_t trackId, double sourceStartBeat, double lengthBeats, const juce::String& label)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("djmixClip", true);
    object->setProperty ("trackId", static_cast<juce::int64> (trackId));
    object->setProperty ("sourceStartBeat", sourceStartBeat);
    object->setProperty ("lengthBeats", lengthBeats);
    object->setProperty ("label", label);
    return juce::var (object);
}

//==============================================================================
// State

void ArrangementView::setProject (const Project& p)
{
    project = p;
    preview.reset();
    if (selected && project.clip (*selected) == nullptr)
        selected.reset();
    resized(); // the number of layers sets the lane height
    updateLength();
}

void ArrangementView::setPlayhead (double beat, bool isPlaying)
{
    if (juce::exactlyEqual (beat, playheadBeat) && isPlaying == playing)
        return;
    const float oldX = xForBeat (playheadBeat);
    playheadBeat = beat;
    playing = isPlaying;

    if (following && playing && drag == Drag::none)
    {
        const double start = std::max (0.0, beat - range.span() / 3.0);
        if (std::abs (start - range.start()) * range.pixelsPerSecond() >= 0.5)
        {
            range.show (start, range.span());
            rangeChanged();
            return;
        }
    }

    // Only the old and new playhead, and the phrase mark ahead of it.
    // Wide enough for the playhead's head.
    const auto strip = [this] (float x) { return juce::Rectangle<int> (juce::roundToInt (x) - 7, 0, 15, getHeight()); };
    repaint (strip (oldX));
    repaint (strip (xForBeat (beat)));
    const double nextPhrase = (std::floor (beat / beatsPerPhrase) + 1.0) * beatsPerPhrase;
    repaint (strip (xForBeat (nextPhrase)));
    repaint (strip (xForBeat (nextPhrase - beatsPerPhrase)));
}

void ArrangementView::setTitle (const juce::String& text)
{
    title = text;
    repaint (0, 0, headerWidth, rulerHeight);
}

void ArrangementView::setSnapMode (SnapMode mode)
{
    snap = mode;
}

void ArrangementView::setFollow (bool follow)
{
    following = follow;
}

void ArrangementView::stopFollowing()
{
    if (! following || ! playing)
        return;
    following = false;
    if (onFollowChanged)
        onFollowChanged (false);
}

void ArrangementView::selectClip (std::optional<int64_t> clipId)
{
    if (selected == clipId)
        return;
    selected = clipId;
    repaint();
}

void ArrangementView::edit (const Project& next, const juce::String& name)
{
    if (next == project)
        return;
    if (onEdit)
        onEdit (next, name);
}

//==============================================================================
// Geometry

double ArrangementView::beatAtX (float x) const
{
    return range.xToTime (x - static_cast<float> (lanesArea.getX()));
}

float ArrangementView::xForBeat (double beat) const
{
    return static_cast<float> (lanesArea.getX() + range.timeToX (beat));
}

int ArrangementView::laneAtY (float y) const
{
    if (y < static_cast<float> (lanesArea.getY()))
        return -1;
    const int lane = static_cast<int> ((y - static_cast<float> (lanesArea.getY())) / static_cast<float> (laneHeight));
    return std::min (lane, static_cast<int> (current().layers.size()));
}

juce::Rectangle<int> ArrangementView::laneBounds (int lane) const
{
    return { lanesArea.getX(), lanesArea.getY() + lane * laneHeight, lanesArea.getWidth(), laneHeight };
}

juce::Rectangle<int> ArrangementView::clipBounds (const Clip& clip) const
{
    const auto& layers = current().layers;
    const auto it = std::find_if (layers.begin(), layers.end(), [&] (const Layer& l) { return l.id == clip.layerId; });
    if (it == layers.end())
        return {};
    const auto lane = laneBounds (static_cast<int> (it - layers.begin()));
    const int x0 = juce::roundToInt (xForBeat (clip.timelineStartBeat));
    const int x1 = juce::roundToInt (xForBeat (clip.timelineEndBeat()));
    return { x0, lane.getY() + 2, std::max (1, x1 - x0), lane.getHeight() - 4 };
}

juce::Rectangle<int> ArrangementView::muteButton (int lane) const
{
    const auto top = lanesArea.getY() + lane * laneHeight;
    return { headerArea.getX() + 8, top + std::min (laneHeight - 22, 26), 24, 18 };
}

juce::Rectangle<int> ArrangementView::soloButton (int lane) const
{
    return muteButton (lane).translated (28, 0);
}

juce::Rectangle<int> ArrangementView::addLayerButton() const
{
    const int top = lanesArea.getY() + static_cast<int> (current().layers.size()) * laneHeight + 4;
    return { headerArea.getX() + 8, top, headerWidth - 16, addRowHeight - 8 };
}

const Clip* ArrangementView::clipAt (juce::Point<float> position) const
{
    // The topmost (last drawn) clip under the point.
    const auto& clips = current().clips;
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
        if (clipBounds (*it).toFloat().contains (position))
            return &*it;
    return nullptr;
}

double ArrangementView::snapped (double beat, const juce::ModifierKeys& mods) const
{
    return mods.isAltDown() ? beat : snapBeat (beat, snap);
}

std::optional<std::pair<double, double>> ArrangementView::sourceLimits (int64_t trackId) const
{
    const auto material = materialFor ? materialFor (trackId) : nullptr;
    if (material == nullptr || material->analysis.beats.empty())
        return std::nullopt;
    const auto& beats = material->analysis.beats;
    return std::pair { beatAtTime (beats, 0.0), beatAtTime (beats, material->analysis.durationSec) };
}

void ArrangementView::showBeats (double start, double span)
{
    range.show (start, span);
    rangeChanged();
}

void ArrangementView::resized()
{
    auto area = getLocalBounds();
    scrollBar.setBounds (area.removeFromBottom (scrollBarHeight).withTrimmedLeft (headerWidth).reduced (2, 1));
    rulerArea = area.removeFromTop (rulerHeight).withTrimmedLeft (headerWidth);
    headerArea = area.removeFromLeft (headerWidth);
    lanesArea = area;

    const int layers = std::max (1, static_cast<int> (current().layers.size()));
    laneHeight = std::clamp ((lanesArea.getHeight() - addRowHeight) / layers, minLaneHeight, maxLaneHeight);
    range.setWidth (lanesArea.getWidth());
    rangeChanged();
}

void ArrangementView::updateLength()
{
    const double length = std::max (minLengthBeats, current().lengthBeats() + spareBeats);
    if (! juce::exactlyEqual (length, range.duration()))
    {
        const double start = range.start(), span = range.span();
        range = ViewRange (length, minSpanBeats);
        range.setWidth (lanesArea.getWidth());
        range.show (start, span);
    }
    rangeChanged();
}

void ArrangementView::rangeChanged()
{
    scrollBar.setRangeLimits (0.0, range.duration(), juce::dontSendNotification);
    scrollBar.setCurrentRange (range.start(), range.span(), juce::dontSendNotification);
    repaint();
}

void ArrangementView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    stopFollowing();
    range.show (newRangeStart, range.span());
    repaint();
}

void ArrangementView::zoomAt (float x, double factor)
{
    range.zoomAt (x - static_cast<float> (lanesArea.getX()), factor);
    rangeChanged();
}

//==============================================================================
// Painting

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (background);
    const auto& p = current();

    for (int lane = 0; lane < static_cast<int> (p.layers.size()); ++lane)
    {
        g.setColour (lane % 2 == 0 ? laneColour : laneAltColour);
        g.fillRect (laneBounds (lane));
    }
    paintGrid (g, lanesArea);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (lanesArea);
        for (const auto& clip : p.clips)
        {
            const auto bounds = clipBounds (clip);
            if (bounds.intersects (lanesArea))
                paintClip (g, clip, bounds);
        }

        if (drop)
        {
            // Where a dropped clip would go: on its lane, or a new one below.
            const int x0 = juce::roundToInt (xForBeat (drop->clip.timelineStartBeat));
            const int x1 = juce::roundToInt (xForBeat (drop->clip.timelineEndBeat()));
            const auto lane = laneBounds (drop->lane);
            const juce::Rectangle<int> ghost { x0, lane.getY() + 2, std::max (2, x1 - x0), laneHeight - 4 };
            const auto material = materialFor ? materialFor (drop->clip.trackId) : nullptr;
            const auto colour = material != nullptr ? colourFor (material->colour) : textColour;
            g.setColour (colour.withAlpha (0.3f));
            g.fillRect (ghost);
            g.setColour (colour);
            g.drawRect (ghost, 2);
        }
    }

    paintRuler (g, rulerArea);
    paintHeaders (g);
    paintPlayhead (g);
}

void ArrangementView::paintGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    const double pixelsPerBeat = range.pixelsPerSecond();
    const double step = pixelsPerBeat >= 10.0 ? 1.0 : (pixelsPerBeat * beatsPerBar >= 8.0 ? beatsPerBar : beatsPerPhrase);
    const auto top = static_cast<float> (area.getY());
    const auto bottom = static_cast<float> (area.getBottom());

    for (double beat = std::floor (range.start() / step) * step; beat <= range.end(); beat += step)
    {
        const float x = xForBeat (beat);
        const bool phrase = isMultiple (beat, beatsPerPhrase);
        const bool bar = isMultiple (beat, beatsPerBar);
        g.setColour (juce::Colours::white.withAlpha (phrase ? 0.22f : (bar ? 0.1f : 0.04f)));
        g.fillRect (juce::Rectangle<float> (std::round (x), top, phrase ? 2.0f : 1.0f, bottom - top));
    }
}

void ArrangementView::paintRuler (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (headerColour);
    g.fillRect (area);
    g.setColour (background);
    g.fillRect (area.removeFromBottom (1));

    // Bar numbers, as close together as fits.
    const double pixelsPerBar = range.pixelsPerSecond() * beatsPerBar;
    int barsPerLabel = 1;
    while (barsPerLabel * pixelsPerBar < 44.0 && barsPerLabel < 4096)
        barsPerLabel *= 2;
    const double step = barsPerLabel * beatsPerBar;

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setFont (juce::FontOptions (11.0f));
    for (double beat = std::floor (range.start() / step) * step; beat <= range.end(); beat += step)
    {
        const float x = xForBeat (beat);
        const bool phrase = isMultiple (beat, beatsPerPhrase);
        g.setColour (phrase ? textColour : dimText);
        g.fillRect (juce::Rectangle<float> (std::round (x), static_cast<float> (area.getBottom() - (phrase ? 9 : 5)), 1.0f,
                                            phrase ? 9.0f : 5.0f));
        const int bar = static_cast<int> (std::lround (beat / beatsPerBar)) + 1;
        g.drawText (juce::String (bar), juce::Rectangle<float> (x + 3.0f, static_cast<float> (area.getY()), 40.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }
}

void ArrangementView::paintHeaders (juce::Graphics& g)
{
    const auto& p = current();
    g.setColour (headerColour);
    g.fillRect (headerArea);
    g.fillRect (juce::Rectangle<int> (0, 0, headerWidth, rulerHeight));
    g.setColour (textColour);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title, juce::Rectangle<int> (8, 0, headerWidth - 12, rulerHeight), juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (12.0f));
    for (int lane = 0; lane < static_cast<int> (p.layers.size()); ++lane)
    {
        const auto& layer = p.layers[static_cast<size_t> (lane)];
        const auto top = lanesArea.getY() + lane * laneHeight;
        juce::Rectangle<int> row { headerArea.getX(), top, headerWidth, laneHeight };
        g.setColour (background);
        g.fillRect (row.removeFromBottom (1));

        g.setColour (isAudible (p, layer) ? textColour : dimText);
        g.drawText (fromUtf8 (layer.name), row.reduced (8, 4).removeFromTop (18), juce::Justification::centredLeft, true);

        auto button = [&] (juce::Rectangle<int> r, const char* text, bool on, juce::Colour onColour)
        {
            g.setColour (on ? onColour : juce::Colour (0xff3a424a));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (on ? juce::Colours::black : textColour);
            g.drawText (text, r, juce::Justification::centred, false);
        };
        button (muteButton (lane), "M", layer.mute, muteColour);
        button (soloButton (lane), "S", layer.solo, soloColour);
    }

    const auto add = addLayerButton();
    if (add.getBottom() <= headerArea.getBottom())
    {
        g.setColour (juce::Colour (0xff3a424a));
        g.drawRoundedRectangle (add.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (dimText);
        g.drawText ("+ Layer", add, juce::Justification::centred, false);
    }

    // A dashed edge under the layers: drop below it for a new layer.
    if (drop && drop->lane >= static_cast<int> (p.layers.size()))
    {
        g.setColour (phraseMarkColour.withAlpha (0.6f));
        g.drawRect (laneBounds (drop->lane).withHeight (laneHeight), 1);
    }
}

void ArrangementView::paintClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<int> bounds)
{
    const auto material = materialFor ? materialFor (clip.trackId) : nullptr;
    const auto colour = material != nullptr ? colourFor (material->colour) : juce::Colour (0xff7a848e);

    g.setColour (colour.withAlpha (0.3f));
    g.fillRect (bounds);
    if (material != nullptr)
        paintClipWaveform (g, clip, *material, bounds);
    g.setColour (colour.withAlpha (0.95f));
    g.drawRect (bounds, 1);

    // Its name, and how far it is stretched if that is far.
    if (bounds.getWidth() > 24)
    {
        juce::String name = material != nullptr ? fromUtf8 (material->title) : "Track " + juce::String (clip.trackId);
        if (! clip.label.empty())
            name << " - " << fromUtf8 (clip.label);
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        auto text = bounds.reduced (4, 2).removeFromTop (14);
        if (material != nullptr && material->analysis.bpm > 0.0)
        {
            const double stretch = project.tempo / material->analysis.bpm - 1.0;
            if (std::abs (stretch) > stretchWarning && text.getWidth() > 80)
            {
                g.setColour (phraseMarkColour);
                g.drawText ((stretch > 0 ? "+" : "") + juce::String (juce::roundToInt (stretch * 100.0)) + "%",
                            text.removeFromRight (40), juce::Justification::centredRight, false);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
            }
        }
        g.drawText (name, text, juce::Justification::centredLeft, true);
    }

    // Not playing as it should yet.
    const auto state = clipStateFor ? clipStateFor (clip.id) : ArrangementPlayer::ClipState::ready;
    if (state == ArrangementPlayer::ClipState::rendering || state == ArrangementPlayer::ClipState::missing)
    {
        const bool missing = state == ArrangementPlayer::ClipState::missing;
        g.setColour (missing ? juce::Colour (0x66c03030) : juce::Colour (0x66000000));
        g.fillRect (bounds);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (missing ? "Track missing" : "Rendering...", bounds.reduced (4), juce::Justification::centred, true);
    }
    else if (state == ArrangementPlayer::ClipState::standIn && bounds.getWidth() > 80)
    {
        g.setColour (dimText);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Updating...", bounds.reduced (4, 2), juce::Justification::bottomRight, false);
    }

    if (selected == clip.id)
    {
        g.setColour (juce::Colours::white);
        g.drawRect (bounds.expanded (1), 2);
    }
}

void ArrangementView::paintClipWaveform (juce::Graphics& g, const Clip& clip, const TrackMaterial& material, juce::Rectangle<int> bounds)
{
    const auto& beats = material.analysis.beats;
    const auto& wave = *material.waveform;
    const auto visible = bounds.getIntersection (lanesArea);
    const double beatsPerPixel = range.span() / range.pixelWidth();

    // Section starts, as lines.
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    for (const auto& section : material.analysis.sections)
    {
        if (section.startBar < 1 || static_cast<size_t> (section.startBar) > material.analysis.bars.size())
            continue;
        const double sourceBeat = material.analysis.bars[static_cast<size_t> (section.startBar - 1)].firstBeat;
        const double beat = clip.timelineStartBeat + (sourceBeat - clip.sourceStartBeat);
        if (beat <= clip.timelineStartBeat || beat >= clip.timelineEndBeat())
            continue;
        g.fillRect (juce::roundToInt (xForBeat (beat)), bounds.getY() + 1, 1, bounds.getHeight() - 2);
    }

    if (wave.empty())
        return;
    const float scale = 1.0f / std::max (wave.maxPeak(), 0.05f);
    const int centre = bounds.getCentreY() + 6; // below the name
    const float half = static_cast<float> (bounds.getHeight() - 16) * 0.5f;
    juce::RectangleList<int> peaks, rmses;
    for (int x = visible.getX(); x < visible.getRight(); ++x)
    {
        const double beat = beatAtX (static_cast<float> (x));
        const double sourceBeat = beat - clip.timelineStartBeat + clip.sourceStartBeat;
        const double t0 = timeAtBeat (beats, sourceBeat), t1 = timeAtBeat (beats, sourceBeat + beatsPerPixel);
        if (t1 <= 0.0 || t0 >= wave.duration())
            continue;
        const auto column = wave.column (t0, t1, std::max (t1 - t0, 1e-4));
        const int peak = std::max (juce::roundToInt (std::min (column.peak * scale, 1.0f) * half), 1);
        const int rms = juce::roundToInt (std::min (column.rms * scale, 1.0f) * half);
        peaks.addWithoutMerging ({ x, centre - peak, 1, 2 * peak });
        if (rms > 0)
            rmses.addWithoutMerging ({ x, centre - rms, 1, 2 * rms });
    }
    g.setColour (waveColour.withAlpha (0.3f));
    fillEach (g, peaks);
    g.setColour (waveColour.withAlpha (0.75f));
    fillEach (g, rmses);
}

void ArrangementView::paintPlayhead (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (lanesArea.getUnion (rulerArea));
    const auto top = static_cast<float> (rulerArea.getY());
    const auto bottom = static_cast<float> (lanesArea.getBottom());

    // While playing, the next phrase boundary is marked.
    if (playing)
    {
        const double next = (std::floor (playheadBeat / beatsPerPhrase) + 1.0) * beatsPerPhrase;
        g.setColour (phraseMarkColour.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (std::round (xForBeat (next)), top, 2.0f, bottom - top));
    }

    const float x = std::round (xForBeat (playheadBeat));
    g.setColour (playheadColour);
    g.fillRect (juce::Rectangle<float> (x, top, 1.0f, bottom - top));
    juce::Path head;
    head.addTriangle (x - 5.0f, top, x + 6.0f, top, x + 0.5f, top + 7.0f);
    g.fillPath (head);
}

//==============================================================================
// Mouse and keys

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    if (isShowing())
        grabKeyboardFocus();
    const auto pos = e.position;
    drag = Drag::none;
    dragging = false;
    const auto& p = current();

    if (headerArea.contains (e.getPosition()))
    {
        if (addLayerButton().contains (e.getPosition()))
        {
            edit (projectEdits::addLayer (project), "Add layer");
            return;
        }
        const int lane = laneAtY (pos.y);
        if (lane < 0 || lane >= static_cast<int> (p.layers.size()))
            return;
        auto layer = p.layers[static_cast<size_t> (lane)];
        if (e.mods.isPopupMenu())
            showLayerMenu (lane);
        else if (muteButton (lane).contains (e.getPosition()))
        {
            layer.mute = ! layer.mute;
            edit (projectEdits::setLayer (project, layer), layer.mute ? "Mute layer" : "Unmute layer");
        }
        else if (soloButton (lane).contains (e.getPosition()))
        {
            layer.solo = ! layer.solo;
            edit (projectEdits::setLayer (project, layer), layer.solo ? "Solo layer" : "Unsolo layer");
        }
        return;
    }

    if (rulerArea.contains (e.getPosition()))
    {
        drag = Drag::seek;
        const double beat = std::max (0.0, snapped (beatAtX (pos.x), e.mods));
        playheadBeat = beat;
        if (onSeek)
            onSeek (beat);
        repaint();
        return;
    }

    if (! lanesArea.contains (e.getPosition()))
        return;

    dragAnchorBeat = beatAtX (pos.x);
    if (const auto* clip = clipAt (pos))
    {
        selectClip (clip->id);
        if (e.mods.isPopupMenu())
        {
            showClipMenu (*clip);
            return;
        }
        const auto bounds = clipBounds (*clip);
        const bool canTrim = bounds.getWidth() > 3 * edgeZone;
        dragClip = clip->id;
        dragOriginal = *clip;
        if (canTrim && pos.x < static_cast<float> (bounds.getX() + edgeZone))
            drag = Drag::trimStart;
        else if (canTrim && pos.x > static_cast<float> (bounds.getRight() - edgeZone))
            drag = Drag::trimEnd;
        else
            drag = Drag::move;
        return;
    }

    selectClip (std::nullopt);
    drag = Drag::pan; // or, if it is only a click, move the playhead there
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none)
        return;
    if (! dragging && drag != Drag::seek && e.getDistanceFromDragStart() < 3)
        return;
    dragging = true;
    const auto pos = e.position;
    const double beat = beatAtX (pos.x);

    switch (drag)
    {
        case Drag::none:
            break;
        case Drag::seek:
            playheadBeat = std::max (0.0, snapped (beat, e.mods));
            if (onSeek)
                onSeek (playheadBeat);
            repaint();
            return;
        case Drag::pan:
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            stopFollowing();
            range.show (dragAnchorBeat - (pos.x - static_cast<float> (lanesArea.getX())) / range.pixelsPerSecond(), range.span());
            rangeChanged();
            return;
        case Drag::move:
        {
            const double start = snapped (dragOriginal.timelineStartBeat + (beat - dragAnchorBeat), e.mods);
            const int lanes = static_cast<int> (project.layers.size());
            const int lane = std::clamp (laneAtY (pos.y), 0, lanes - 1);
            preview = projectEdits::moveClip (project, dragClip, project.layers[static_cast<size_t> (lane)].id, start);
            break;
        }
        case Drag::trimStart:
        {
            double start = snapped (beat, e.mods);
            if (const auto limits = sourceLimits (dragOriginal.trackId))
                start = std::max (start, dragOriginal.timelineStartBeat - (dragOriginal.sourceStartBeat - limits->first));
            preview = projectEdits::trimClipStart (project, dragClip, start);
            break;
        }
        case Drag::trimEnd:
        {
            double end = snapped (beat, e.mods);
            if (const auto limits = sourceLimits (dragOriginal.trackId))
                end = std::min (end, dragOriginal.timelineStartBeat + (limits->second - dragOriginal.sourceStartBeat));
            preview = projectEdits::trimClipEnd (project, dragClip, end);
            break;
        }
    }
    updateLength();
}

void ArrangementView::mouseUp (const juce::MouseEvent& e)
{
    const auto kind = drag;
    const bool wasDrag = dragging;
    drag = Drag::none;
    dragging = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);

    if (kind == Drag::pan && ! wasDrag && e.mouseWasClicked())
    {
        const double beat = std::max (0.0, snapped (beatAtX (e.position.x), e.mods));
        playheadBeat = beat;
        if (onSeek)
            onSeek (beat);
        repaint();
        return;
    }

    if (preview && wasDrag)
    {
        const auto next = *preview;
        preview.reset();
        edit (next, kind == Drag::move ? "Move clip" : "Trim clip");
    }
    preview.reset();
    updateLength();
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    hover = e.position;
    auto pointer = juce::MouseCursor::NormalCursor;
    if (const auto* clip = clipAt (e.position))
    {
        const auto bounds = clipBounds (*clip);
        if (bounds.getWidth() > 3 * edgeZone
            && (e.position.x < static_cast<float> (bounds.getX() + edgeZone) || e.position.x > static_cast<float> (bounds.getRight() - edgeZone)))
            pointer = juce::MouseCursor::LeftRightResizeCursor;
    }
    setMouseCursor (pointer);
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float sign = wheel.isReversed ? -1.0f : 1.0f;
    const float dx = wheel.deltaX * sign, dy = wheel.deltaY * sign;
    if (e.mods.isShiftDown() || std::abs (dx) > std::abs (dy))
    {
        const float delta = std::abs (dx) > std::abs (dy) ? dx : dy;
        stopFollowing();
        range.scrollBy (-delta * range.span() * 0.5);
        rangeChanged();
        return;
    }
    zoomAt (e.position.x, std::exp (dy * wheelZoomSpeed));
}

void ArrangementView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomAt (e.position.x, scaleFactor);
}

bool ArrangementView::keyPressed (const juce::KeyPress& key)
{
    const auto c = key.getTextCharacter();
    const auto centre = static_cast<float> (lanesArea.getCentreX());

    if (key == juce::KeyPress::spaceKey)
    {
        if (onTogglePlay)
            onTogglePlay();
        return true;
    }
    if (c == 's' || c == 'S')
    {
        splitAtPlayhead();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        removeSelected();
        return true;
    }
    if (c >= '1' && c <= '4')
    {
        const SnapMode modes[] = { SnapMode::off, SnapMode::beat, SnapMode::bar, SnapMode::phrase };
        setSnapMode (modes[c - '1']);
        if (onSnapModeChanged)
            onSnapModeChanged (snap);
        return true;
    }
    if (c == '+' || c == '=')
    {
        zoomAt (centre, 2.0);
        return true;
    }
    if (c == '-')
    {
        zoomAt (centre, 0.5);
        return true;
    }
    if (key == juce::KeyPress::homeKey)
    {
        stopFollowing();
        range.showAll();
        rangeChanged();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && selected)
    {
        selectClip (std::nullopt);
        return true;
    }
    return false;
}

void ArrangementView::splitAtPlayhead()
{
    // The selected clip if the playhead is in it, or else every clip there.
    auto ids = projectEdits::clipsAt (project, playheadBeat);
    if (selected && std::find (ids.begin(), ids.end(), *selected) != ids.end())
        ids = { *selected };
    Project next = project;
    for (const auto id : ids)
        next = projectEdits::splitClip (next, id, playheadBeat);
    edit (next, "Split clip");
}

void ArrangementView::removeSelected()
{
    if (! selected)
        return;
    const auto id = *selected;
    selected.reset();
    edit (projectEdits::removeClips (project, { id }), "Delete clip");
}

void ArrangementView::showLayerMenu (int lane)
{
    const auto layer = project.layers[static_cast<size_t> (lane)];
    juce::PopupMenu menu;
    menu.addItem ("Rename...", [safe = juce::Component::SafePointer<ArrangementView> (this), layer]
    {
        if (safe == nullptr)
            return;
        auto* window = new juce::AlertWindow ("Rename layer", {}, juce::MessageBoxIconType::NoIcon, safe.getComponent());
        window->addTextEditor ("name", fromUtf8 (layer.name));
        window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, layer] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (safe == nullptr || result != 1 || name.isEmpty())
                return;
            auto renamed = layer;
            renamed.name = name.toStdString();
            safe->edit (projectEdits::setLayer (safe->project, renamed), "Rename layer");
        }), true);
    });
    menu.addItem ("Add layer", [this] { edit (projectEdits::addLayer (project), "Add layer"); });
    menu.addItem ("Remove layer", project.layers.size() > 1, false,
                  [this, id = layer.id] { edit (projectEdits::removeLayer (project, id), "Remove layer"); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ArrangementView::showClipMenu (const Clip& clip)
{
    const bool under = clip.timelineStartBeat < playheadBeat && clip.timelineEndBeat() > playheadBeat;
    juce::PopupMenu menu;
    menu.addItem ("Split at playhead", under, false, [this, id = clip.id]
    {
        edit (projectEdits::splitClip (project, id, playheadBeat), "Split clip");
    });
    menu.addItem ("Delete", [this, id = clip.id]
    {
        selected.reset();
        edit (projectEdits::removeClips (project, { id }), "Delete clip");
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
// Dropping sections, phrases and tracks

bool ArrangementView::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.getProperty ("djmixClip", false);
}

std::optional<ArrangementView::Drop> ArrangementView::dropFor (const SourceDetails& details) const
{
    const auto& d = details.description;
    const auto trackId = static_cast<int64_t> (static_cast<juce::int64> (d.getProperty ("trackId", 0)));
    double sourceStart = d.getProperty ("sourceStartBeat", 0.0);
    double length = d.getProperty ("lengthBeats", 0.0);
    if (d.getProperty ("wholeTrack", false))
    {
        // From the first downbeat to the end, in whole bars.
        const auto material = materialFor ? materialFor (trackId) : nullptr;
        if (material == nullptr || material->analysis.bars.empty())
            return std::nullopt;
        const auto& a = material->analysis;
        sourceStart = a.bars.front().firstBeat;
        length = std::floor ((beatAtTime (a.beats, a.durationSec) - sourceStart) / beatsPerBar) * beatsPerBar;
    }
    if (trackId == 0 || ! (length > 0.0))
        return std::nullopt;

    Drop target;
    target.clip.trackId = trackId;
    target.clip.sourceStartBeat = sourceStart;
    target.clip.lengthBeats = length;
    target.clip.label = d.getProperty ("label", "").toString().toStdString();
    target.clip.timelineStartBeat = std::max (0.0, snapped (beatAtX (static_cast<float> (details.localPosition.x)),
                                                             juce::ModifierKeys::currentModifiers));
    const int layers = static_cast<int> (project.layers.size());
    target.lane = std::clamp (laneAtY (static_cast<float> (details.localPosition.y)), 0, layers);
    target.clip.layerId = target.lane < layers ? project.layers[static_cast<size_t> (target.lane)].id : 0;
    return target;
}

void ArrangementView::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void ArrangementView::itemDragMove (const SourceDetails& details)
{
    drop = dropFor (details);
    repaint();
}

void ArrangementView::itemDragExit (const SourceDetails&)
{
    drop.reset();
    repaint();
}

void ArrangementView::itemDropped (const SourceDetails& details)
{
    auto d = dropFor (details);
    drop.reset();
    repaint();
    if (! d)
        return;

    if (const auto material = materialFor ? materialFor (d->clip.trackId) : nullptr)
        d->clip.fileHash = material->fileHash;
    Project next = project;
    if (d->clip.layerId == 0)
        next = projectEdits::addLayer (next, &d->clip.layerId);
    int64_t id = 0;
    next = projectEdits::addClip (next, d->clip, &id);
    selected = id;
    if (isShowing())
        grabKeyboardFocus();
    edit (next, "Add clip");
}

} // namespace djmix
