#include "RenderCache.h"

#include <algorithm>
#include <cstring>

#include <juce_events/juce_events.h>

#include "TrackDecoder.h"

namespace djmix
{

namespace
{
constexpr size_t decodedKept = 2;

bool covers (const RenderedClip& render, const RenderCache::Job& job)
{
    return render.covers (job.fromBeat, job.toBeat);
}
} // namespace

RenderCache::RenderCache (Post postFunction, Decode decodeFunction, int numWorkers)
    : post (std::move (postFunction)),
      decode (std::move (decodeFunction)),
      alive (std::make_shared<RenderCache*> (this))
{
    if (! post)
        post = [] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); };
    if (! decode)
        decode = [] (const std::filesystem::path& file, std::string& error) { return TrackDecoder::decode (file, error); };
    if (numWorkers <= 0)
        numWorkers = std::clamp (static_cast<int> (std::thread::hardware_concurrency()) / 2, 1, 4);

    for (int i = 0; i < numWorkers; ++i)
        workers.emplace_back ([this, weak = std::weak_ptr<RenderCache*> (alive)] { workerLoop (weak); });
}

RenderCache::~RenderCache()
{
    alive.reset();
    {
        std::lock_guard lock (mutex);
        quitting = true;
        for (auto& t : running)
            t.cancelled->store (true);
    }
    wake.notify_all();
    for (auto& w : workers)
        w.join();
}

uint64_t RenderCache::gridHash (const std::vector<double>& beats)
{
    // FNV-1a over the beat times.
    uint64_t h = 14695981039346656037ull;
    for (const double b : beats)
    {
        uint64_t bits = 0;
        std::memcpy (&bits, &b, sizeof bits);
        for (int i = 0; i < 8; ++i)
        {
            h ^= (bits >> (i * 8)) & 0xff;
            h *= 1099511628211ull;
        }
    }
    return h ^ beats.size();
}

bool RenderCache::sameRender (const Task& t, const Job& job, uint64_t grid)
{
    return t.grid == grid && t.job.file == job.file && t.job.settings == job.settings
        && t.job.fromBeat <= job.fromBeat + 1e-9 && t.job.toBeat >= job.toBeat - 1e-9;
}

std::shared_ptr<const RenderedClip> RenderCache::find (const Job& job) const
{
    const auto grid = gridHash (job.beats);
    for (auto it = ready.rbegin(); it != ready.rend(); ++it)
        if (it->grid == grid && it->file == job.file && it->render->settings == job.settings && covers (*it->render, job))
            return it->render;
    return nullptr;
}

std::shared_ptr<const RenderedClip> RenderCache::findStandIn (const Job& job) const
{
    // At the same rate, of the same stretch or part of it. One that covers
    // it all is best, then one at the same transposition, then the newest.
    const auto grid = gridHash (job.beats);
    std::shared_ptr<const RenderedClip> best;
    int bestScore = -1;
    for (auto it = ready.rbegin(); it != ready.rend(); ++it)
    {
        const auto& r = *it->render;
        const bool overlaps = r.fromBeat < job.toBeat && r.toBeat > job.fromBeat;
        if (it->grid != grid || it->file != job.file || ! juce::exactlyEqual (r.settings.sampleRate, job.settings.sampleRate) || ! overlaps)
            continue;
        const int score = (covers (r, job) ? 2 : 0) + (r.settings.transposeSemitones == job.settings.transposeSemitones ? 1 : 0);
        if (score > bestScore)
        {
            best = it->render;
            bestScore = score;
        }
    }
    return best;
}

void RenderCache::request (const std::vector<Job>& jobs)
{
    {
        std::lock_guard lock (mutex);
        queue.clear();

        std::vector<bool> wanted (running.size(), false);
        for (const auto& job : jobs)
        {
            if (errors.count (job.file) != 0 || find (job) != nullptr)
                continue;

            const auto grid = gridHash (job.beats);
            bool underWay = false;
            for (size_t i = 0; i < running.size(); ++i)
            {
                if (! running[i].cancelled->load() && sameRender (running[i], job, grid))
                {
                    wanted[i] = true;
                    underWay = true;
                }
            }
            const bool queued = std::any_of (queue.begin(), queue.end(), [&] (const Task& t) { return sameRender (t, job, grid); });
            if (underWay || queued)
                continue;

            Task t { job, grid, std::make_shared<std::atomic<bool>> (false) };
            t.job.fromBeat -= marginBeats;
            t.job.toBeat += marginBeats;
            queue.push_back (std::move (t));
        }

        for (size_t i = 0; i < running.size(); ++i)
            if (! wanted[i])
                running[i].cancelled->store (true);
    }
    wake.notify_all();
}

int RenderCache::pending() const
{
    std::lock_guard lock (mutex);
    int n = static_cast<int> (queue.size());
    for (const auto& t : running)
        if (! t.cancelled->load())
            ++n;
    return n;
}

std::optional<std::string> RenderCache::errorFor (const std::filesystem::path& file) const
{
    const auto it = errors.find (file);
    if (it == errors.end())
        return std::nullopt;
    return it->second;
}

void RenderCache::trim (size_t keep)
{
    size_t unused = 0;
    for (auto it = ready.rbegin(); it != ready.rend(); ++it)
        if (it->render.use_count() == 1)
            ++unused;

    for (auto it = ready.begin(); it != ready.end() && unused > keep;)
    {
        if (it->render.use_count() == 1)
        {
            it = ready.erase (it);
            --unused;
        }
        else
        {
            ++it;
        }
    }
}

std::shared_ptr<const DecodedAudio> RenderCache::decoded (const std::filesystem::path& file, std::string& error)
{
    std::unique_lock lock (decodeMutex);
    for (;;)
    {
        const auto it = std::find_if (decodedTracks.begin(), decodedTracks.end(), [&] (const auto& d) { return d.first == file; });
        if (it != decodedTracks.end())
        {
            auto audio = it->second;
            std::rotate (it, it + 1, decodedTracks.end()); // most recent last
            return audio;
        }
        // Another worker is decoding it: wait for that rather than decode it twice.
        if (std::find (decoding.begin(), decoding.end(), file) == decoding.end())
            break;
        decodeDone.wait (lock);
    }

    decoding.push_back (file);
    lock.unlock();
    std::shared_ptr<const DecodedAudio> audio = decode (file, error);
    lock.lock();

    std::erase (decoding, file);
    if (audio != nullptr)
    {
        decodedTracks.emplace_back (file, audio);
        if (decodedTracks.size() > decodedKept)
            decodedTracks.erase (decodedTracks.begin());
    }
    decodeDone.notify_all();
    return audio;
}

void RenderCache::workerLoop (std::weak_ptr<RenderCache*> weak)
{
    for (;;)
    {
        Task task;
        {
            std::unique_lock lock (mutex);
            wake.wait (lock, [this] { return quitting || ! queue.empty(); });
            if (quitting)
                return;
            task = std::move (queue.front());
            queue.pop_front();
            running.push_back (task);
        }

        std::string error;
        std::shared_ptr<RenderedClip> render;
        if (const auto audio = decoded (task.job.file, error))
        {
            render = renderClip (*audio, task.job.beats, task.job.fromBeat, task.job.toBeat, task.job.settings,
                                 task.cancelled.get());
        }

        post ([weak, task, render = std::move (render), error]() mutable
        {
            if (auto self = weak.lock())
                (*self)->handleDone (task, std::move (render), error);
        });
    }
}

void RenderCache::handleDone (const Task& task, std::shared_ptr<RenderedClip> render, const std::string& error)
{
    // Still counted as running until here, so pending() never misses a
    // render that is finished but not yet handed over.
    {
        std::lock_guard lock (mutex);
        std::erase_if (running, [&] (const Task& t) { return t.cancelled == task.cancelled; });
    }

    if (render == nullptr)
    {
        // Cancelled, or the file could not be read.
        if (! error.empty())
            errors[task.job.file] = error;
        else if (task.cancelled->load())
            return;
    }
    else
    {
        ready.push_back ({ task.job.file, task.grid, std::move (render) });
    }
    if (onRenderReady)
        onRenderReady();
}

} // namespace djmix
