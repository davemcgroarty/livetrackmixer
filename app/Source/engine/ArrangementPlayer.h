#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "MasterPlayer.h"
#include "RenderCache.h"
#include "model/Project.h"

namespace djmix
{

// Plays a project on Master. Lives on the message thread: after each edit
// (setProject) it works out the render each clip needs, asks the render cache
// for any not ready, and publishes a PlaybackSnapshot of what can play now.
// A clip whose render is not ready plays a stand-in: a render of the same
// stretch at another tempo, sped up or slowed down to stay on the beat, or of
// part of it (before the clip was lengthened); with none it stays silent.
// Each render that arrives publishes a new snapshot.
class ArrangementPlayer
{
public:
    // A clip's track: its file and effective beat grid. Nothing if the track
    // is not in the library (or has no analysis).
    struct TrackSource
    {
        std::filesystem::path file;
        std::vector<double> beats;
    };
    using Resolve = std::function<std::optional<TrackSource> (const Clip&)>;

    enum class ClipState
    {
        ready,     // playing its own render
        standIn,   // playing another render while its own is made
        rendering, // silent until its render is made
        missing,   // its track or file could not be found or read
    };

    ArrangementPlayer (MasterPlayer& master,
                       Resolve resolve,
                       RenderCache::Post post = {},
                       RenderCache::Decode decode = {},
                       int renderWorkers = 0);
    ~ArrangementPlayer();

    ArrangementPlayer (const ArrangementPlayer&) = delete;
    ArrangementPlayer& operator= (const ArrangementPlayer&) = delete;

    void setProject (const Project& project);
    const Project& project() const { return current; }

    // Rebuilds as if the project had changed: after the device's sample rate
    // or a track's grid changes.
    void refresh();

    void play() { master.play(); }
    void pause() { master.pause(); }
    void seek (double beat) { master.seek (beat); }

    // The transport. Also frees snapshots the audio thread is done with.
    MasterPlayer::State state();

    ClipState clipState (int64_t clipId) const;
    bool allReady() const;
    int rendersPending() const { return cache.pending(); }
    RenderCache& renderCache() { return cache; }

    // Called after a render arrives and the snapshot is rebuilt.
    std::function<void()> onChanged;

private:
    void rebuild();

    MasterPlayer& master;
    Resolve resolve;
    RenderCache cache;
    Project current;
    std::vector<std::pair<int64_t, ClipState>> states;
};

} // namespace djmix
