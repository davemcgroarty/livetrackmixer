#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <map>
#include <numeric>
#include <vector>

#include "PostQueue.h"
#include "SyntheticAudio.h"
#include "engine/ArrangementPlayer.h"
#include "engine/MasterPlayer.h"

using Catch::Approx;
using djmix::ArrangementPlayer;
using djmix::Clip;
using djmix::MasterPlayer;
using djmix::Project;
using ClipState = djmix::ArrangementPlayer::ClipState;
namespace test = djmix::test;
namespace fs = std::filesystem;

namespace
{
constexpr double rate = 44100.0;
constexpr int block = 512;
const int fade = static_cast<int> (std::lround (MasterPlayer::fadeSeconds * rate));

// Tracks the "library" knows: decoded audio and beat grids by file.
struct Library
{
    struct Track
    {
        std::shared_ptr<djmix::DecodedAudio> audio;
        std::vector<double> beats;
    };
    std::map<fs::path, Track> tracks;
    std::map<int64_t, fs::path> files; // by track id

    void add (int64_t id, std::shared_ptr<djmix::DecodedAudio> audio, std::vector<double> beats)
    {
        const fs::path file = "track" + std::to_string (id) + ".wav";
        tracks[file] = { std::move (audio), std::move (beats) };
        files[id] = file;
    }

    ArrangementPlayer::Resolve resolver()
    {
        return [this] (const Clip& clip) -> std::optional<ArrangementPlayer::TrackSource>
        {
            const auto it = files.find (clip.trackId);
            if (it == files.end())
                return std::nullopt;
            const auto track = tracks.find (it->second);
            return ArrangementPlayer::TrackSource { it->second, track != tracks.end() ? track->second.beats : std::vector<double> {} };
        };
    }

    djmix::RenderCache::Decode decoder()
    {
        return [this] (const fs::path& file, std::string& error) -> std::shared_ptr<djmix::DecodedAudio>
        {
            const auto it = tracks.find (file);
            if (it == tracks.end())
            {
                error = "cannot open " + file.string();
                return nullptr;
            }
            return it->second.audio;
        };
    }
};

Clip clipOf (int64_t id, int64_t trackId, int64_t layerId, double sourceStart, double timelineStart, double length)
{
    Clip c;
    c.id = id;
    c.trackId = trackId;
    c.fileHash = "sha256:" + std::string (64, '0');
    c.layerId = layerId;
    c.sourceStartBeat = sourceStart;
    c.timelineStartBeat = timelineStart;
    c.lengthBeats = length;
    return c;
}

// The Master bus, left channel only, run for `numSamples` while handing
// over whatever the render workers post.
std::vector<float> play (MasterPlayer& master, test::PostQueue& queue, int numSamples)
{
    std::vector<float> left (static_cast<size_t> (numSamples)), right (static_cast<size_t> (block));
    for (int done = 0; done < numSamples; done += block)
    {
        while (queue.runNext (0.0))
        {
        }
        const int n = std::min (block, numSamples - done);
        master.render (left.data() + done, right.data(), n);
    }
    return left;
}

// Hands over posted renders until every clip has its own.
bool waitForRenders (ArrangementPlayer& player, test::PostQueue& queue, double seconds)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
    while (! player.allReady() && std::chrono::steady_clock::now() < until)
        queue.runNext (0.1);
    return player.allReady();
}

// Plays the project from the start for `seconds` and returns Master's left
// channel; output sample i is timeline sample i - fade.
std::vector<float> playFromStart (ArrangementPlayer& player, MasterPlayer& master, test::PostQueue& queue, double seconds)
{
    player.pause();
    play (master, queue, 4 * block);
    player.seek (0.0);
    play (master, queue, block);
    player.play();
    return play (master, queue, static_cast<int> (seconds * rate));
}

// How far each beat's click is from where the project grid puts it, in seconds.
std::vector<double> clickErrors (const std::vector<float>& left, double tempo, int numBeats)
{
    std::vector<double> errors;
    for (int beat = 0; beat < numBeats; ++beat)
    {
        const double expected = beat * 60.0 / tempo + fade / rate;
        const auto onset = test::onsetNear (left, rate, expected);
        if (! onset)
        {
            FAIL_CHECK ("no click near beat " << beat);
            continue;
        }
        errors.push_back (*onset - expected);
    }
    return errors;
}

double mean (const std::vector<double>& v, size_t from, size_t to)
{
    return std::accumulate (v.begin() + static_cast<long> (from), v.begin() + static_cast<long> (to), 0.0)
         / static_cast<double> (to - from);
}
} // namespace

TEST_CASE ("Two 124 BPM tracks at 128 BPM stay on the beat for 5 minutes", "[arrangement][slow]")
{
    // The M4 test: clicks on every beat of two 124 BPM tracks with different
    // first beats, on two layers, the second from its 33rd beat. At 128 BPM,
    // five minutes is 640 beats.
    test::PostQueue queue;
    Library library;
    library.add (1, test::clickTrack (124.0, 0.1, 330.0, 2000.0), test::beatGrid (124.0, 0.1, 330.0));
    library.add (2, test::clickTrack (124.0, 0.37, 340.0, 3000.0), test::beatGrid (124.0, 0.37, 340.0));

    MasterPlayer master;
    master.prepare (rate, block);
    ArrangementPlayer player (master, library.resolver(), queue.poster(), library.decoder(), 2);

    auto project = Project::create ("Alignment", 128.0);
    const auto second = project.addLayer();
    project.clips.push_back (clipOf (10, 1, project.layers[0].id, 0.0, 0.0, 640.0));
    project.clips.push_back (clipOf (11, 2, second, 32.0, 0.0, 640.0));
    player.setProject (project);
    CHECK (player.clipState (10) == ClipState::rendering);
    REQUIRE (waitForRenders (player, queue, 600.0));

    // Each layer on its own, so each click can be found.
    std::vector<std::vector<double>> errors;
    for (const int heard : { 0, 1 })
    {
        project.layers[0].mute = heard != 0;
        project.layers[1].mute = heard != 1;
        player.setProject (project);
        const auto left = playFromStart (player, master, queue, 300.5);
        errors.push_back (clickErrors (left, 128.0, 640));
    }
    REQUIRE (errors[0].size() == 640);
    REQUIRE (errors[1].size() == 640);

    // Every click within 3 ms of its beat (stretching blurs where a click
    // starts by a millisecond or so), and the two layers within 3 ms of each other.
    for (size_t beat = 0; beat < 640; ++beat)
    {
        INFO ("beat " << beat);
        CHECK (std::abs (errors[0][beat]) < 0.003);
        CHECK (std::abs (errors[1][beat]) < 0.003);
        CHECK (std::abs (errors[0][beat] - errors[1][beat]) < 0.003);
    }

    // No drift: the last minute as close to the beat as the first.
    for (const auto& e : errors)
        CHECK (std::abs (mean (e, 512, 640) - mean (e, 0, 128)) < 0.001);
}

TEST_CASE ("After a tempo change a clip plays a stand-in on the beat until its render is ready", "[arrangement]")
{
    test::PostQueue queue;
    Library library;
    library.add (1, test::sineTrack (440.0, 30.0), test::beatGrid (124.0, 0.0, 30.0));

    MasterPlayer master;
    master.prepare (rate, block);
    ArrangementPlayer player (master, library.resolver(), queue.poster(), library.decoder(), 1);

    auto project = Project::create ("Tempo", 124.0);
    project.clips.push_back (clipOf (5, 1, project.layers[0].id, 4.0, 0.0, 32.0));
    player.setProject (project);
    REQUIRE (waitForRenders (player, queue, 120.0));
    player.play();
    auto before = play (master, queue, static_cast<int> (rate));

    project.tempo = 130.0;
    player.setProject (project);
    CHECK (player.clipState (5) == ClipState::standIn);
    CHECK (master.latest()->voices.at (0).step == Approx (130.0 / 124.0));
    const double beat = player.state().beat;

    // Played until the render arrives (at about real time), and a little after.
    std::vector<float> during;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (120);
    while (player.clipState (5) != ClipState::ready && std::chrono::steady_clock::now() < until)
    {
        const auto more = play (master, queue, block);
        during.insert (during.end(), more.begin(), more.end());
        queue.runNext (static_cast<double> (block) / rate);
    }
    REQUIRE (player.clipState (5) == ClipState::ready);
    const auto after = play (master, queue, static_cast<int> (rate / 2));
    CHECK (player.state().beat == Approx (beat + (during.size() + after.size()) / (rate * 60.0 / 130.0)).margin (0.001));

    // No click at either switch.
    before.insert (before.end(), during.begin(), during.end());
    before.insert (before.end(), after.begin(), after.end());
    CHECK (test::maxStep (before) < 0.04f);
}

TEST_CASE ("Moving a clip along the timeline needs no new render", "[arrangement]")
{
    test::PostQueue queue;
    Library library;
    library.add (1, test::sineTrack (440.0, 30.0), test::beatGrid (124.0, 0.0, 30.0));

    MasterPlayer master;
    master.prepare (rate, block);
    ArrangementPlayer player (master, library.resolver(), queue.poster(), library.decoder(), 1);

    auto project = Project::create ("Move", 124.0);
    project.clips.push_back (clipOf (5, 1, project.layers[0].id, 8.0, 0.0, 16.0));
    player.setProject (project);
    REQUIRE (waitForRenders (player, queue, 120.0));

    project.clips[0].timelineStartBeat = 64.0;
    player.setProject (project);
    CHECK (player.clipState (5) == ClipState::ready);
    project.clips[0].lengthBeats = 16.5; // within the beat either side it was rendered with
    project.clips[0].sourceStartBeat = 7.5;
    player.setProject (project);
    CHECK (player.clipState (5) == ClipState::ready);
    CHECK (player.rendersPending() == 0);
    CHECK (player.renderCache().numReady() == 1);

    // A longer stretch does need one; until it is ready, the part rendered plays.
    project.clips[0].lengthBeats = 24.0;
    player.setProject (project);
    CHECK (player.clipState (5) == ClipState::standIn);
    CHECK (player.rendersPending() == 1);
    CHECK (master.latest()->voices.size() == 1);
}

TEST_CASE ("Back-to-back clips on a layer join without a gap or a click", "[arrangement]")
{
    test::PostQueue queue;
    Library library;
    library.add (1, test::sineTrack (440.0, 30.0), test::beatGrid (124.0, 0.0, 30.0));

    MasterPlayer master;
    master.prepare (rate, block);
    ArrangementPlayer player (master, library.resolver(), queue.poster(), library.decoder(), 2);

    auto project = Project::create ("Join", 124.0);
    project.clips.push_back (clipOf (5, 1, project.layers[0].id, 0.0, 0.0, 8.0));
    project.clips.push_back (clipOf (6, 1, project.layers[0].id, 8.0, 8.0, 8.0));
    player.setProject (project);
    REQUIRE (waitForRenders (player, queue, 120.0));

    const auto left = playFromStart (player, master, queue, 6.0);
    const auto join = static_cast<size_t> (8.0 * 60.0 / 124.0 * rate) + static_cast<size_t> (fade);
    CHECK (test::maxStep (left, join - 2000, join + 2000) < 0.04f);
    float quietest = 1.0f; // the level, a cycle at a time, across the join
    for (size_t at = join - 1000; at < join + 1000; at += 50)
    {
        float peak = 0.0f;
        for (size_t i = at; i < at + 101; ++i)
            peak = std::max (peak, std::abs (left[i]));
        quietest = std::min (quietest, peak);
    }
    CHECK (quietest > 0.4f);
}

TEST_CASE ("Clips whose track cannot be found or read are reported", "[arrangement]")
{
    test::PostQueue queue;
    Library library;
    library.add (1, test::sineTrack (440.0, 10.0), test::beatGrid (124.0, 0.0, 10.0));
    library.files[2] = "deleted.wav"; // in the library, but the file is gone

    MasterPlayer master;
    master.prepare (rate, block);
    ArrangementPlayer player (master, library.resolver(), queue.poster(), library.decoder(), 1);

    auto project = Project::create ("Missing", 124.0);
    project.clips.push_back (clipOf (5, 1, project.layers[0].id, 0.0, 0.0, 8.0));
    project.clips.push_back (clipOf (6, 2, project.layers[0].id, 0.0, 8.0, 8.0));
    project.clips.push_back (clipOf (7, 3, project.layers[0].id, 0.0, 16.0, 8.0)); // not in the library
    player.setProject (project);
    CHECK (player.clipState (7) == ClipState::missing);

    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (60);
    while ((player.clipState (5) != ClipState::ready || player.clipState (6) != ClipState::missing)
           && std::chrono::steady_clock::now() < until)
        queue.runNext (0.1);
    CHECK (player.clipState (5) == ClipState::ready);
    CHECK (player.clipState (6) == ClipState::missing);
    CHECK (player.renderCache().errorFor ("deleted.wav") == "cannot open deleted.wav");
    CHECK_FALSE (player.allReady());
}
