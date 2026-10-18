#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "CuePlayer.h"

namespace djmix
{

// Auditions library tracks on Cue. Lives on the message thread: decodes
// tracks on a background thread (the selected one ahead of time, so a click
// plays at once), keeps the last few decoded, and hands them to the player.
//
// When several decodes are asked for, only the latest waits; an older one not
// yet started is dropped.
class AuditionController
{
public:
    // Runs a function on the message thread; tests pass their own.
    using Post = std::function<void (std::function<void()>)>;

    struct Status
    {
        int64_t trackId = 0;
        double start = 0.0;
        double end = 0.0;      // 0 for "to the end of the track"
        double position = 0.0; // seconds into the track
        bool playing = false;
        bool loading = false;  // decoding before it can play
        std::string error;     // why the last request could not play
    };

    static constexpr size_t cacheSize = 2;

    explicit AuditionController (CuePlayer& player, Post post = {});
    ~AuditionController();

    AuditionController (const AuditionController&) = delete;
    AuditionController& operator= (const AuditionController&) = delete;

    // Starts decoding a track in the background, if it is not decoded already.
    void prepare (int64_t trackId, const std::filesystem::path& file);

    // Plays [start, end) of a track (end 0: to the end), as soon as it is decoded.
    void play (int64_t trackId, const std::filesystem::path& file, double start, double end);
    void stop();

    // The current audition, for drawing. Also frees audio the player is done with.
    Status status();

    // For tests: decoded tracks in the cache, most recent first.
    std::vector<std::filesystem::path> cachedFiles() const;

private:
    struct Request
    {
        int64_t trackId = 0;
        std::filesystem::path file;
        double start = 0.0;
        double end = 0.0;
        uint64_t tag = 0;
    };

    struct Cached
    {
        std::filesystem::path file;
        std::shared_ptr<const DecodedAudio> audio;
    };

    std::shared_ptr<const DecodedAudio> cached (const std::filesystem::path& file);
    // By value: callers pass *waiting, which this clears.
    void startPlaying (Request request, std::shared_ptr<const DecodedAudio> audio);
    void decodeInBackground (const std::filesystem::path& file);
    void handleDecoded (const std::filesystem::path& file, std::shared_ptr<DecodedAudio> audio, const std::string& error);
    void workerLoop (std::weak_ptr<AuditionController*> weak);

    CuePlayer& player;
    Post post;

    std::vector<Cached> cache; // most recent first
    std::optional<Request> waiting; // a play request waiting for its decode
    Request current;                // the audition last handed to the player
    uint64_t nextTag = 1;
    std::string lastError;
    std::filesystem::path decoding; // the file being decoded or queued, if any

    std::mutex mutex;
    std::condition_variable wake;
    std::optional<std::filesystem::path> job; // guarded by mutex
    bool quitting = false;                    // guarded by mutex
    std::shared_ptr<AuditionController*> alive;
    std::thread worker;
};

} // namespace djmix
