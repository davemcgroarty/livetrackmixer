#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/ProjectEdits.h"
#include "model/Snap.h"

using Catch::Approx;
using djmix::Clip;
using djmix::Project;
using djmix::SnapMode;
namespace edits = djmix::projectEdits;

namespace
{
// One layer with a 64-beat clip from timeline beat 16, source beat 32.
Project withClip (int64_t* clipId)
{
    auto p = Project::create ("Edits");
    Clip c;
    c.trackId = 7;
    c.layerId = p.layers[0].id;
    c.sourceStartBeat = 32.0;
    c.timelineStartBeat = 16.0;
    c.lengthBeats = 64.0;
    c.label = "Drop";
    return edits::addClip (p, c, clipId);
}
} // namespace

TEST_CASE ("Beats snap to the nearest beat, bar or phrase", "[project][snap]")
{
    CHECK (djmix::snapBeat (5.4, SnapMode::off) == 5.4);
    CHECK (djmix::snapBeat (5.4, SnapMode::beat) == 5.0);
    CHECK (djmix::snapBeat (5.6, SnapMode::beat) == 6.0);
    CHECK (djmix::snapBeat (5.9, SnapMode::bar) == 4.0);
    CHECK (djmix::snapBeat (6.1, SnapMode::bar) == 8.0);
    CHECK (djmix::snapBeat (47.0, SnapMode::phrase) == 32.0);
    CHECK (djmix::snapBeat (49.0, SnapMode::phrase) == 64.0);
    CHECK (djmix::snapModeFromKey ("bar") == SnapMode::bar);
    CHECK_FALSE (djmix::snapModeFromKey ("bars"));
    CHECK (std::string (djmix::snapModeLabel (SnapMode::phrase)) == "Phrase");
}

TEST_CASE ("Clips are added, moved between layers and removed", "[project][edit]")
{
    int64_t id = 0;
    auto p = withClip (&id);
    REQUIRE (p.clip (id) != nullptr);
    CHECK (id == 2);

    // A clip on a layer that does not exist is refused.
    Clip stray;
    stray.layerId = 99;
    stray.lengthBeats = 4.0;
    CHECK (edits::addClip (p, stray) == p);

    int64_t layer = 0;
    p = edits::addLayer (p, &layer);
    p = edits::moveClip (p, id, layer, 40.0);
    CHECK (p.clip (id)->layerId == layer);
    CHECK (p.clip (id)->timelineStartBeat == 40.0);
    CHECK (p.clip (id)->sourceStartBeat == 32.0); // the music it plays stays the same
    CHECK (edits::moveClip (p, id, layer, -8.0).clip (id)->timelineStartBeat == 0.0);
    CHECK (edits::moveClip (p, id, 99, 0.0) == p);

    CHECK (edits::removeClips (p, { id }).clips.empty());
    CHECK (edits::removeClips (p, { 99 }) == p);
}

TEST_CASE ("Trimming a clip's start keeps its end in place in the music", "[project][edit]")
{
    int64_t id = 0;
    const auto p = withClip (&id); // timeline 16-80, source from 32

    auto t = edits::trimClipStart (p, id, 24.0);
    CHECK (t.clip (id)->timelineStartBeat == 24.0);
    CHECK (t.clip (id)->sourceStartBeat == 40.0);
    CHECK (t.clip (id)->timelineEndBeat() == 80.0);

    t = edits::trimClipStart (p, id, 8.0); // earlier: more of the source before
    CHECK (t.clip (id)->sourceStartBeat == 24.0);
    CHECK (t.clip (id)->lengthBeats == 72.0);

    t = edits::trimClipStart (p, id, 200.0); // past the end: the shortest clip
    CHECK (t.clip (id)->lengthBeats == Approx (edits::minClipLength));

    t = edits::trimClipEnd (p, id, 48.0);
    CHECK (t.clip (id)->timelineStartBeat == 16.0);
    CHECK (t.clip (id)->sourceStartBeat == 32.0);
    CHECK (t.clip (id)->lengthBeats == 32.0);
    CHECK (edits::trimClipEnd (p, id, 0.0).clip (id)->lengthBeats == Approx (edits::minClipLength));
}

TEST_CASE ("Splitting a clip makes two that play on as one", "[project][edit]")
{
    int64_t id = 0;
    const auto p = withClip (&id);
    int64_t second = 0;
    const auto s = edits::splitClip (p, id, 48.0, &second);
    REQUIRE (s.clips.size() == 2);
    CHECK (s.clips[1].id == second);
    CHECK (s.clip (id)->lengthBeats == 32.0);
    CHECK (s.clip (second)->timelineStartBeat == 48.0);
    CHECK (s.clip (second)->sourceStartBeat == 64.0);
    CHECK (s.clip (second)->lengthBeats == 32.0);
    CHECK (s.clip (second)->label == "Drop");

    CHECK (edits::splitClip (p, id, 16.0) == p); // at its edge
    CHECK (edits::splitClip (p, id, 100.0) == p); // outside it

    CHECK (edits::clipsAt (s, 48.0) == std::vector<int64_t> { second }); // the first ends there
    CHECK (edits::clipsAt (s, 47.0) == std::vector<int64_t> { id });
    CHECK (edits::clipsAt (s, 10.0).empty());
}

TEST_CASE ("Layers are added, changed and removed with their clips", "[project][edit]")
{
    int64_t id = 0;
    auto p = withClip (&id);
    const auto first = p.layers[0].id;
    CHECK (edits::removeLayer (p, first) == p); // the last layer stays

    int64_t second = 0;
    p = edits::addLayer (p, &second);
    auto layer = *p.layer (second);
    layer.solo = true;
    layer.name = "Vocals";
    p = edits::setLayer (p, layer);
    CHECK (p.layer (second)->solo);
    CHECK (p.layer (second)->name == "Vocals");

    p = edits::removeLayer (p, first);
    CHECK (p.layers.size() == 1);
    CHECK (p.clips.empty()); // its clip went with it

    CHECK (edits::setTempo (p, 300.0).tempo == Project::maxTempo);
    CHECK (edits::setTempo (p, 126.5).tempo == 126.5);
}
