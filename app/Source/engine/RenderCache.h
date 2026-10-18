#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ClipRenderer.h"

namespace djmix
{

// Clip renders, made on background threads (design spec, "render cache").
// Lives on the message thread: request() says what the arrangement needs,
// workers decode and stretch it, and each finished render is handed back on
// the message thread, where onRenderReady is called.
//
// Each render covers a beat either side of the clip it was asked for, so a
// clip can fade in before its first beat, and moving a clip along the
// timeline or trimming it a little needs no new render.
class RenderCache
{
public:
    // Runs a function on the message thread; tests pass their own.
    using Post = std::function<void (std::function<void()>)>;
    // Decodes a file (TrackDecoder::decode); tests pass their own.
    using Decode = std::function<std::shared_ptr<DecodedAudio> (const std::filesystem::path&, std::string& error)>;

    static constexpr double marginBeats = 1.0;

    struct Job
    {
        std::filesystem::path file;
        std::vector<double> beats; // the track's effective beat grid
        double fromBeat = 0.0;     // the source beats the clip plays
        double toBeat = 0.0;
        RenderSettings settings;
    };

    // `numWorkers` 0: about half the cores, at most four.
    explicit RenderCache (Post post = {}, Decode decode = {}, int numWorkers = 0);
    ~RenderCache();

    RenderCache (const RenderCache&) = delete;
    RenderCache& operator= (const RenderCache&) = delete;

    // A ready render for the job, at exactly its settings.
    std::shared_ptr<const RenderedClip> find (const Job& job) const;

    // A ready render of the same stretch (or part of it), perhaps at another
    // tempo or transposition, to play until the right one is ready. Null if
    // there is none.
    std::shared_ptr<const RenderedClip> findStandIn (const Job& job) const;

    // Renders these, in this order, unless they are ready or already being
    // rendered. Jobs asked for earlier and not yet started are dropped, and
    // renders under way that are no longer wanted are cancelled.
    void request (const std::vector<Job>& jobs);

    // Jobs queued or rendering.
    int pending() const;

    // Why the last render of this file failed, if it did. A failed file is
    // not tried again.
    std::optional<std::string> errorFor (const std::filesystem::path& file) const;

    // Drops ready renders that nothing else holds, beyond the `keep` most recent.
    void trim (size_t keep);
    size_t numReady() const { return ready.size(); }

    std::function<void()> onRenderReady;

private:
    struct Ready
    {
        std::filesystem::path file;
        uint64_t grid = 0;
        std::shared_ptr<const RenderedClip> render;
    };

    struct Task
    {
        Job job;
        uint64_t grid = 0;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };

    static uint64_t gridHash (const std::vector<double>& beats);
    static bool sameRender (const Task& a, const Job& job, uint64_t grid);
    std::shared_ptr<const DecodedAudio> decoded (const std::filesystem::path& file, std::string& error);
    void workerLoop (std::weak_ptr<RenderCache*> weak);
    void handleDone (const Task& task, std::shared_ptr<RenderedClip> render, const std::string& error);

    Post post;
    Decode decode;

    // Message thread only.
    std::vector<Ready> ready; // most recent last
    std::map<std::filesystem::path, std::string> errors;

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Task> queue;     // guarded by mutex
    std::vector<Task> running;  // guarded by mutex
    bool quitting = false;      // guarded by mutex

    // Decoded tracks the workers share, the last few kept.
    std::mutex decodeMutex;
    std::condition_variable decodeDone;
    std::vector<std::pair<std::filesystem::path, std::shared_ptr<const DecodedAudio>>> decodedTracks; // most recent last
    std::vector<std::filesystem::path> decoding;

    std::shared_ptr<RenderCache*> alive;
    std::vector<std::thread> workers;
};

} // namespace djmix
