#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/ProjectEdits.h"
#include "ui/ArrangementView.h"

using Catch::Approx;
using djmix::ArrangementView;
using djmix::Clip;
using djmix::Project;
using djmix::SnapMode;

namespace
{
// A six-minute track at 124 BPM from 0.1 s, with a section every 32 bars and
// a waveform at djanalyze's resolution.
std::shared_ptr<djmix::TrackMaterial> longTrack (int64_t id)
{
    auto m = std::make_shared<djmix::TrackMaterial>();
    m->trackId = id;
    m->title = "Long Track";
    m->fileHash = "sha256:" + std::string (64, 'c');
    m->colour = 2;
    auto& a = m->analysis;
    a.durationSec = 360.0;
    a.bpm = 124.0;
    for (double t = 0.1; t < a.durationSec; t += 60.0 / a.bpm)
        a.beats.push_back (t);
    const int numBars = static_cast<int> (a.beats.size() / 4);
    for (int b = 0; b < numBars; ++b)
        a.bars.push_back ({ b + 1, a.beats[static_cast<size_t> (b * 4)], b * 4 });
    for (int s = 0; s * 32 < numBars; ++s)
        a.sections.push_back ({ a.bars[static_cast<size_t> (s * 32)].start, 0.0, s * 32 + 1, std::min (32, numBars - s * 32), std::nullopt });

    djmix::WaveformOverview w;
    w.sampleRate = 44100;
    w.samplesPerPoint = 256;
    for (size_t i = 0; i < static_cast<size_t> (a.durationSec * 44100 / 256); ++i)
    {
        const auto level = static_cast<float> (0.3 + 0.6 * std::exp (-std::fmod (i * 256 / 44100.0, 0.4839) * 12.0));
        w.peak.push_back (level);
        w.rms.push_back (level * 0.5f);
    }
    m->waveform = std::make_shared<djmix::WaveformPyramid> (w);
    return m;
}

// Two layers; one 32-beat clip of track 1 from beat 16 on the first.
Project twoLayers()
{
    auto p = Project::create ("View test");
    p.addLayer();
    Clip c;
    c.trackId = 1;
    c.layerId = p.layers[0].id;
    c.sourceStartBeat = 64.0;
    c.timelineStartBeat = 16.0;
    c.lengthBeats = 32.0;
    return djmix::projectEdits::addClip (p, c);
}

// A view showing beats 0-64 across 640 pixels of lanes (10 pixels a beat),
// that applies its own edits as the app would.
struct Fixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    ArrangementView view;
    std::shared_ptr<djmix::TrackMaterial> material = longTrack (1);
    std::vector<juce::String> edits;

    Fixture()
    {
        view.setMaterialSource ([this] (int64_t id) { return id == 1 ? material : nullptr; });
        view.onEdit = [this] (const Project& next, const juce::String& name)
        {
            edits.push_back (name);
            view.setProject (next);
        };
        view.setBounds (0, 0, 132 + 640, 22 + 2 * 96 + 26 + 10);
        view.setProject (twoLayers());
        view.showBeats (0.0, 64.0);
    }

    const Clip& clip() const { return view.shownProject().clips.front(); }

    juce::MouseEvent mouse (juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {}, bool dragged = false)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return { source, at, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &view, &view, now, down, now, 1, dragged };
    }

    // A drag from `from` to `to`, in steps.
    void drag (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
    {
        const auto buttons = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
        view.mouseDown (mouse (from, from, buttons));
        for (int i = 1; i <= 8; ++i)
            view.mouseDrag (mouse (from + (to - from) * (static_cast<float> (i) / 8.0f), from, buttons, true));
        view.mouseUp (mouse (to, from, mods, true));
    }

    float laneY (int lane) const { return static_cast<float> (view.laneBounds (lane).getCentreY()); }
};
} // namespace

TEST_CASE ("The Arrangement maps beats to pixels and clips to lanes", "[ui][arrangement]")
{
    Fixture f;
    CHECK (f.view.xForBeat (0.0) == Approx (132.0f));
    CHECK (f.view.xForBeat (16.0) == Approx (292.0f));
    CHECK (f.view.beatAtX (452.0f) == Approx (32.0));
    CHECK (f.view.laneAtY (5.0f) == -1);
    CHECK (f.view.laneAtY (f.laneY (0)) == 0);
    CHECK (f.view.laneAtY (f.laneY (1)) == 1);
    CHECK (f.view.laneAtY (static_cast<float> (f.view.laneBounds (1).getBottom() + 5)) == 2); // below the layers

    const auto bounds = f.view.clipBounds (f.clip());
    CHECK (bounds.getX() == 292);
    CHECK (bounds.getWidth() == 320);
    CHECK (f.view.laneBounds (0).contains (bounds.getCentre()));
}

TEST_CASE ("Dragging a clip moves it by whole bars, or freely with Alt", "[ui][arrangement]")
{
    Fixture f;
    const auto id = f.clip().id;
    const float y = f.laneY (0);

    // 57 pixels right is 5.7 beats: with bar snap the start goes from 16 to 20.
    f.drag ({ 400.0f, y }, { 457.0f, y });
    CHECK (f.clip().timelineStartBeat == 20.0);
    CHECK (f.clip().sourceStartBeat == 64.0);
    CHECK (f.view.selectedClip() == id);
    CHECK (f.edits.back() == "Move clip");

    // Down a lane, and with Alt off the grid.
    f.drag ({ 400.0f, y }, { 413.0f, f.laneY (1) }, juce::ModifierKeys::altModifier);
    CHECK (f.clip().layerId == f.view.shownProject().layers[1].id);
    CHECK (f.clip().timelineStartBeat == Approx (21.3));

    // Phrase snap: to beat 32 or 0, whichever is nearer.
    f.view.setSnapMode (SnapMode::phrase);
    f.drag ({ 400.0f, f.laneY (1) }, { 500.0f, f.laneY (1) });
    CHECK (f.clip().timelineStartBeat == 32.0);
}

TEST_CASE ("Dragging a clip's edges trims it, within its track", "[ui][arrangement]")
{
    Fixture f;
    const float y = f.laneY (0);

    // The start edge (x 292) to beat 24: the music under the end stays put.
    f.drag ({ 293.0f, y }, { 372.0f, y });
    CHECK (f.clip().timelineStartBeat == 24.0);
    CHECK (f.clip().sourceStartBeat == 72.0);
    CHECK (f.clip().timelineEndBeat() == 48.0);
    CHECK (f.edits.back() == "Trim clip");

    // The end edge (x 612) out to beat 56.
    f.drag ({ 611.0f, y }, { 690.0f, y });
    CHECK (f.clip().timelineEndBeat() == 56.0);

    // The start no further back than the start of the track (source beat
    // -0.2 or so, before the first beat): it stops at timeline beat 0 here
    // anyway, but a clip from the track's first beat cannot reach earlier.
    auto p = f.view.shownProject();
    p.clips.front().sourceStartBeat = 0.0;
    p.clips.front().timelineStartBeat = 32.0;
    p.clips.front().lengthBeats = 16.0;
    f.view.setProject (p);
    f.view.setSnapMode (SnapMode::off);
    f.drag ({ 453.0f, y }, { 300.0f, y });
    CHECK (f.clip().timelineStartBeat == Approx (32.0 - 0.1 / (60.0 / 124.0)).margin (0.01));
}

TEST_CASE ("Clicking an empty lane or the ruler moves the playhead, on the snap grid", "[ui][arrangement]")
{
    Fixture f;
    double seekedTo = -1.0;
    f.view.onSeek = [&] (double beat) { seekedTo = beat; };

    const juce::Point<float> lane { 132.0f + 333.0f, f.laneY (1) };
    f.view.mouseDown (f.mouse (lane, lane, juce::ModifierKeys::leftButtonModifier));
    f.view.mouseUp (f.mouse (lane, lane));
    CHECK (seekedTo == 32.0); // beat 33.3, to the bar
    CHECK_FALSE (f.view.selectedClip());

    const juce::Point<float> ruler { 132.0f + 333.0f, 10.0f };
    f.view.setSnapMode (SnapMode::beat);
    f.view.mouseDown (f.mouse (ruler, ruler, juce::ModifierKeys::leftButtonModifier));
    CHECK (seekedTo == 33.0);
    CHECK (f.view.playhead() == 33.0);
}

TEST_CASE ("Sections dropped on the Arrangement become clips, on a new layer below the others", "[ui][arrangement]")
{
    Fixture f;
    const auto description = ArrangementView::clipDragDescription (1, 128.0, 32.0, "Bars 33-40");
    juce::Component source;

    // Over the second lane at beat 41.7: it would start at beat 40.
    const juce::DragAndDropTarget::SourceDetails over { description, &source, { 132 + 417, static_cast<int> (f.laneY (1)) } };
    REQUIRE (f.view.isInterestedInDragSource (over));
    f.view.itemDragMove (over);
    f.view.itemDropped (over);
    REQUIRE (f.view.shownProject().clips.size() == 2);
    const auto& added = f.view.shownProject().clips.back();
    CHECK (added.timelineStartBeat == 40.0);
    CHECK (added.sourceStartBeat == 128.0);
    CHECK (added.lengthBeats == 32.0);
    CHECK (added.layerId == f.view.shownProject().layers[1].id);
    CHECK (added.fileHash == f.material->fileHash);
    CHECK (added.label == "Bars 33-40");
    CHECK (f.view.selectedClip() == added.id);

    // Below the last lane: a third layer for it.
    const juce::DragAndDropTarget::SourceDetails below { description, &source, { 132 + 100, f.view.laneBounds (1).getBottom() + 8 } };
    f.view.itemDropped (below);
    CHECK (f.view.shownProject().layers.size() == 3);
    CHECK (f.view.shownProject().clips.back().layerId == f.view.shownProject().layers[2].id);

    // A whole track from the Library: from its first downbeat, in whole bars.
    auto whole = ArrangementView::clipDragDescription (1, 0.0, 0.0, {});
    whole.getDynamicObject()->setProperty ("wholeTrack", true);
    f.view.itemDropped ({ whole, &source, { 132 + 10, static_cast<int> (f.laneY (0)) } });
    const auto& track = f.view.shownProject().clips.back();
    CHECK (track.sourceStartBeat == 0.0);
    CHECK (track.lengthBeats == Approx (740.0)); // 6 minutes at 124 BPM, less the part bar
    CHECK (std::fmod (track.lengthBeats, 4.0) == 0.0);

    // Anything else is not for the Arrangement.
    CHECK_FALSE (f.view.isInterestedInDragSource ({ juce::var ("files"), &source, {} }));
}

TEST_CASE ("Arrangement keys split, delete, snap and play", "[ui][arrangement]")
{
    Fixture f;
    int toggles = 0;
    f.view.onTogglePlay = [&] { ++toggles; };
    std::optional<SnapMode> snap;
    f.view.onSnapModeChanged = [&] (SnapMode mode) { snap = mode; };

    CHECK (f.view.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (toggles == 1);

    // S splits whatever is under the playhead.
    f.view.setPlayhead (24.0, false);
    CHECK (f.view.keyPressed (juce::KeyPress ('s', {}, 's')));
    REQUIRE (f.view.shownProject().clips.size() == 2);
    CHECK (f.view.shownProject().clips[1].timelineStartBeat == 24.0);
    CHECK (f.edits.back() == "Split clip");

    // Delete removes the selected clip only.
    f.view.selectClip (f.view.shownProject().clips[1].id);
    CHECK (f.view.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (f.view.shownProject().clips.size() == 1);
    CHECK_FALSE (f.view.selectedClip());

    CHECK (f.view.keyPressed (juce::KeyPress ('4', {}, '4')));
    CHECK (snap == SnapMode::phrase);
    CHECK (f.view.snapMode() == SnapMode::phrase);
    CHECK (f.view.keyPressed (juce::KeyPress ('1', {}, '1')));
    CHECK (f.view.snapMode() == SnapMode::off);

    // Zoom in and out around the middle.
    const double span = f.view.viewRange().span();
    CHECK (f.view.keyPressed (juce::KeyPress ('+', {}, '+')));
    CHECK (f.view.viewRange().span() == Approx (span / 2.0));
    CHECK (f.view.keyPressed (juce::KeyPress ('-', {}, '-')));
    CHECK (f.view.viewRange().span() == Approx (span));
}

TEST_CASE ("Mute and solo are buttons in each layer's header", "[ui][arrangement]")
{
    Fixture f;
    const auto lane = f.view.laneBounds (0);
    // The M button sits 8 pixels in, 26 down (see ArrangementView::muteButton).
    const juce::Point<float> mute { 18.0f, static_cast<float> (lane.getY() + 30) };
    f.view.mouseDown (f.mouse (mute, mute, juce::ModifierKeys::leftButtonModifier));
    CHECK (f.view.shownProject().layers[0].mute);
    CHECK (f.edits.back() == "Mute layer");
    const juce::Point<float> solo { 46.0f, static_cast<float> (f.view.laneBounds (1).getY() + 30) };
    f.view.mouseDown (f.mouse (solo, solo, juce::ModifierKeys::leftButtonModifier));
    CHECK (f.view.shownProject().layers[1].solo);
}

TEST_CASE ("While following, the playhead stays a third of the way across", "[ui][arrangement]")
{
    Fixture f;
    f.view.setFollow (true);
    f.view.setPlayhead (100.0, true);
    CHECK (f.view.viewRange().start() == Approx (100.0 - 64.0 / 3.0));

    // Scrolling by hand stops following, and says so.
    std::optional<bool> followed;
    f.view.onFollowChanged = [&] (bool on) { followed = on; };
    f.view.mouseWheelMove (f.mouse ({ 400.0f, 100.0f }, { 400.0f, 100.0f }, juce::ModifierKeys::shiftModifier),
                           juce::MouseWheelDetails { 0.0f, 0.2f, false, false, false });
    CHECK (followed == false);
    CHECK_FALSE (f.view.isFollowing());
    const double start = f.view.viewRange().start();
    f.view.setPlayhead (110.0, true);
    CHECK (f.view.viewRange().start() == start);
}

TEST_CASE ("The Arrangement draws a full screen of clips within a 60 fps frame", "[ui][arrangement][performance]")
{
    // Four layers, each with four 80-beat clips back to back, staggered.
    Fixture f;
    f.view.setBounds (0, 0, 1600, 500);
    auto p = Project::create ("Busy");
    for (int i = 0; i < 3; ++i)
        p.addLayer();
    for (int i = 0; i < 16; ++i)
    {
        Clip c;
        c.trackId = 1;
        c.layerId = p.layers[static_cast<size_t> (i % 4)].id;
        c.sourceStartBeat = 32.0 * i;
        c.timelineStartBeat = 80.0 * (i / 4) + 8.0 * (i % 4);
        c.lengthBeats = 80.0;
        p = djmix::projectEdits::addClip (p, c);
    }
    f.view.setProject (p);
    f.view.showBeats (0.0, 320.0);

    juce::Image image (juce::Image::ARGB, f.view.getWidth(), f.view.getHeight(), true);
    auto paint = [&] (int frames)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i)
        {
            juce::Graphics g (image);
            f.view.paintEntireComponent (g, true);
        }
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
        return elapsed.count() / frames;
    };
    paint (2);
    const double ms = paint (20);
    WARN ("Arrangement, 16 clips: " << ms << " ms per frame");
    if (const char* dir = std::getenv ("DJMIX_SNAPSHOT_DIR"))
    {
        const auto file = juce::File (dir).getChildFile ("arrangement.png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }
    // As for the Track view, this only catches gross regressions: about 5 ms
    // in a Release build. Unoptimised builds draw this many clips several
    // times slower, so they get more room.
#if defined(NDEBUG)
    CHECK (ms < 50.0);
#else
    CHECK (ms < 200.0);
#endif
}
