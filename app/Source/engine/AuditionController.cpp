#include "AuditionController.h"

#include <juce_events/juce_events.h>

#include "TrackDecoder.h"

namespace djmix
{

AuditionController::AuditionController (CuePlayer& p, Post postFunction)
    : player (p),
      post (std::move (postFunction)),
      alive (std::make_shared<AuditionController*> (this))
{
    if (! post)
        post = [] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); };
    worker = std::thread ([this, weak = std::weak_ptr<AuditionController*> (alive)] { workerLoop (weak); });
}

AuditionController::~AuditionController()
{
    alive.reset();
    {
        std::lock_guard lock (mutex);
        quitting = true;
    }
    wake.notify_all();
    worker.join();
}

void AuditionController::prepare (int64_t, const std::filesystem::path& file)
{
    // A play request waiting for its own decode goes first.
    if (cached (file) != nullptr || decoding == file || waiting.has_value())
        return;
    decodeInBackground (file);
}

void AuditionController::play (int64_t trackId, const std::filesystem::path& file, double start, double end)
{
    const Request request { trackId, file, start, end, nextTag++ };
    lastError.clear();

    if (auto audio = cached (file))
    {
        startPlaying (request, std::move (audio));
        return;
    }

    // Silence whatever was playing rather than let it run on while this loads.
    player.stop();
    waiting = request;
    current = request;
    if (decoding != file)
        decodeInBackground (file);
}

void AuditionController::stop()
{
    waiting.reset();
    player.stop();
}

AuditionController::Status AuditionController::status()
{
    player.releaseUnused();

    Status s;
    const auto& request = waiting ? *waiting : current;
    s.trackId = request.trackId;
    s.start = request.start;
    s.end = request.end;
    s.position = request.start;
    s.loading = waiting.has_value();
    s.error = lastError;

    const auto state = player.state();
    if (! s.loading && current.tag != 0 && state.tag == current.tag)
    {
        s.playing = state.playing;
        s.position = state.position;
    }
    return s;
}

std::vector<std::filesystem::path> AuditionController::cachedFiles() const
{
    std::vector<std::filesystem::path> files;
    for (const auto& c : cache)
        files.push_back (c.file);
    return files;
}

std::shared_ptr<const DecodedAudio> AuditionController::cached (const std::filesystem::path& file)
{
    const auto it = std::find_if (cache.begin(), cache.end(), [&] (const Cached& c) { return c.file == file; });
    if (it == cache.end())
        return nullptr;
    std::rotate (cache.begin(), it, it + 1); // most recently used first
    return cache.front().audio;
}

void AuditionController::startPlaying (Request request, std::shared_ptr<const DecodedAudio> audio)
{
    waiting.reset();
    current = request;
    if (! player.play (std::move (audio), request.start, request.end, request.tag))
        lastError = "No audio output is open. Choose one in Settings.";
}

void AuditionController::decodeInBackground (const std::filesystem::path& file)
{
    decoding = file;
    {
        std::lock_guard lock (mutex);
        job = file; // replaces a job not yet started
    }
    wake.notify_one();
}

void AuditionController::handleDecoded (const std::filesystem::path& file,
                                        std::shared_ptr<DecodedAudio> audio,
                                        const std::string& error)
{
    if (decoding == file)
        decoding.clear();

    if (audio != nullptr)
    {
        cache.insert (cache.begin(), { file, audio });
        if (cache.size() > cacheSize)
            cache.resize (cacheSize);
    }

    if (waiting && waiting->file == file)
    {
        if (audio != nullptr)
        {
            startPlaying (*waiting, std::move (audio));
        }
        else
        {
            lastError = "This track can't be played: " + error + ".";
            waiting.reset();
        }
    }
}

void AuditionController::workerLoop (std::weak_ptr<AuditionController*> weak)
{
    for (;;)
    {
        std::filesystem::path file;
        {
            std::unique_lock lock (mutex);
            wake.wait (lock, [this] { return quitting || job.has_value(); });
            if (quitting)
                return;
            file = *job;
            job.reset();
        }

        std::string error;
        auto audio = TrackDecoder::decode (file, error);
        post ([weak, file, audio = std::move (audio), error]() mutable
        {
            if (auto self = weak.lock())
                (*self)->handleDecoded (file, std::move (audio), error);
        });
    }
}

} // namespace djmix
