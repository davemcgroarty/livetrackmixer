#include "MasterPlayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace djmix
{

namespace
{
constexpr int defaultMaxBlock = 4096;
}

MasterPlayer::MasterPlayer() = default;

MasterPlayer::~MasterPlayer() = default;

//==============================================================================
// Message thread

void MasterPlayer::publish (std::shared_ptr<PlaybackSnapshot> snapshot)
{
    if (snapshot == nullptr)
        return;
    snapshot->serial = nextSerial++;

    const bool replacing = ! held.empty();
    held.push_back ({ snapshot, std::numeric_limits<uint64_t>::max() });
    newest = snapshot;
    current.store (snapshot.get());

    // A block that loaded the old one began before the store above, so once
    // the count moves past this, any such block has finished.
    if (replacing)
        held[held.size() - 2].replacedAfterBlock = blocksDone.load();
    releaseUnused();
}

void MasterPlayer::play()
{
    push ({ Command::Type::play, 0.0 });
}

void MasterPlayer::pause()
{
    push ({ Command::Type::pause, 0.0 });
}

void MasterPlayer::seek (double beat)
{
    push ({ Command::Type::seek, beat });
}

void MasterPlayer::push (const Command& command)
{
    // With no device reading, the oldest commands are lost when the FIFO is
    // full; later ones decide the transport anyway.
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        commands[static_cast<size_t> (scope.startIndex1)] = command;
    else if (scope.blockSize2 > 0)
        commands[static_cast<size_t> (scope.startIndex2)] = command;
}

MasterPlayer::State MasterPlayer::state() const
{
    return { statePlaying.load(), stateBeat.load() };
}

void MasterPlayer::releaseUnused()
{
    const bool running = deviceRunning.load();
    const auto done = blocksDone.load();
    const auto oldest = oldestInUse.load();
    std::erase_if (held, [&] (const Held& h)
    {
        if (h.snapshot == newest)
            return false;
        if (! running)
            return true;
        return done > h.replacedAfterBlock && (oldest == 0 || h.snapshot->serial < oldest);
    });
}

//==============================================================================
// Audio thread

void MasterPlayer::prepare (double sampleRate, int maxBlockSize)
{
    const double beat = stateBeat.load();
    rate = sampleRate;
    fadeLength = std::max (1, static_cast<int> (std::lround (fadeSeconds * rate)));
    scratch.setSize (4, std::max (maxBlockSize, defaultMaxBlock));

    now = nullptr;
    prev = nullptr;
    crossfadeLeft = 0;
    playing = false;
    fade = 0.0f;
    fadeTarget = 0.0f;
    seekPending = false;
    position = std::llround (beat * samplesPerBeat());

    deviceRate.store (sampleRate);
    publishState();
    deviceRunning.store (true);
}

void MasterPlayer::stopped()
{
    deviceRunning.store (false);
    playing = false;
    seekPending = false;
    now = nullptr;
    prev = nullptr;
    crossfadeLeft = 0;
    publishState();
}

double MasterPlayer::samplesPerBeat() const
{
    return (rate > 0.0 ? rate : 44100.0) * 60.0 / positionTempo;
}

void MasterPlayer::handleCommands()
{
    const auto ready = fifo.getNumReady();
    if (ready == 0)
        return;

    const auto scope = fifo.read (ready);
    auto run = [this] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto& c = commands[static_cast<size_t> (start + i)];
            switch (c.type)
            {
                case Command::Type::play:
                    if (! playing)
                    {
                        // One fade early, so the first beat plays at full level.
                        playing = true;
                        fade = 0.0f;
                        position -= fadeLength;
                    }
                    fadeTarget = 1.0f;
                    break;
                case Command::Type::pause:
                    fadeTarget = 0.0f;
                    seekPending = false;
                    break;
                case Command::Type::seek:
                    if (playing && fade > 0.0f)
                    {
                        // Fade out, jump, fade back in.
                        seekPending = true;
                        seekBeat = c.beat;
                        fadeTarget = 0.0f;
                    }
                    else
                    {
                        position = std::llround (c.beat * samplesPerBeat()) - (playing ? fadeLength : 0);
                    }
                    break;
            }
        }
    };
    run (scope.startIndex1, scope.blockSize1);
    run (scope.startIndex2, scope.blockSize2);
}

void MasterPlayer::takeLatest()
{
    const auto* latestSnapshot = current.load();
    if (latestSnapshot == now || latestSnapshot == nullptr || crossfadeLeft > 0)
        return;

    // Crossfade from the old snapshot, if it was sounding.
    if (now != nullptr && playing && juce::exactlyEqual (now->sampleRate, rate))
    {
        prev = now;
        prevPosition = position;
        crossfadeLeft = fadeLength;
    }
    else
    {
        prev = nullptr;
    }

    // Same beat, new tempo.
    if (! juce::exactlyEqual (latestSnapshot->tempo, positionTempo))
    {
        position = std::llround (static_cast<double> (position) * positionTempo / latestSnapshot->tempo);
        positionTempo = latestSnapshot->tempo;
    }
    now = latestSnapshot;
}

void MasterPlayer::mix (const PlaybackSnapshot& snapshot, int64_t from, float* left, float* right, int numSamples)
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);
    if (! juce::exactlyEqual (snapshot.sampleRate, rate))
        return;

    const int64_t to = from + numSamples;
    const float perSample = 1.0f / static_cast<float> (fadeLength);
    for (const auto& v : snapshot.voices)
    {
        const int64_t soundFrom = v.start - fadeLength;
        const int64_t lo = std::max (from, soundFrom);
        const int64_t hi = std::min (to, v.end);
        if (lo >= hi || v.render == nullptr)
            continue;
        const float g = v.gain * snapshot.layerGains[static_cast<size_t> (v.layer)];
        if (g <= 0.0f)
            continue;

        const auto& src = v.render->samples;
        const int64_t frames = src.getNumSamples();
        const float* srcL = src.getReadPointer (0);
        const float* srcR = src.getReadPointer (src.getNumChannels() > 1 ? 1 : 0);
        auto envelope = [&] (int64_t t)
        {
            const float in = static_cast<float> (t - soundFrom) * perSample;
            const float out = static_cast<float> (v.end - t) * perSample;
            return g * std::min ({ 1.0f, in, out });
        };

        if (juce::exactlyEqual (v.step, 1.0))
        {
            const int64_t offset = std::llround (v.frameAtStart) - v.start;
            const int64_t first = std::max (lo, -offset);
            const int64_t last = std::min (hi, frames - offset);
            for (int64_t t = first; t < last; ++t)
            {
                const auto out = static_cast<size_t> (t - from);
                const auto in = static_cast<size_t> (t + offset);
                const float e = envelope (t);
                left[out] += srcL[in] * e;
                right[out] += srcR[in] * e;
            }
        }
        else
        {
            // A render for another tempo, played faster or slower to stay on
            // the beat until the right one is ready.
            for (int64_t t = lo; t < hi; ++t)
            {
                const double f = v.frameAtStart + static_cast<double> (t - v.start) * v.step;
                const auto i = static_cast<int64_t> (std::floor (f));
                if (i < 0 || i + 1 >= frames)
                    continue;
                const auto frac = static_cast<float> (f - static_cast<double> (i));
                const auto out = static_cast<size_t> (t - from);
                const auto in = static_cast<size_t> (i);
                const float e = envelope (t);
                left[out] += (srcL[in] + frac * (srcL[in + 1] - srcL[in])) * e;
                right[out] += (srcR[in] + frac * (srcR[in + 1] - srcR[in])) * e;
            }
        }
    }
}

void MasterPlayer::render (float* left, float* right, int numSamples)
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);

    handleCommands();
    takeLatest();

    const float step = 1.0f / static_cast<float> (fadeLength);
    for (int done = 0; done < numSamples && playing;)
    {
        // In chunks that end where a fade-out finishes (to stop or jump
        // there) and where a crossfade ends.
        int n = std::min (numSamples - done, scratch.getNumSamples());
        if (fade > fadeTarget)
            n = std::min (n, std::max (1, static_cast<int> (std::ceil ((fade - fadeTarget) / step))));
        if (crossfadeLeft > 0)
            n = std::min (n, crossfadeLeft);

        float* nowL = scratch.getWritePointer (0);
        float* nowR = scratch.getWritePointer (1);
        if (now != nullptr)
            mix (*now, position, nowL, nowR, n);
        else
        {
            juce::FloatVectorOperations::clear (nowL, n);
            juce::FloatVectorOperations::clear (nowR, n);
        }

        if (crossfadeLeft > 0 && prev != nullptr)
        {
            float* prevL = scratch.getWritePointer (2);
            float* prevR = scratch.getWritePointer (3);
            mix (*prev, prevPosition, prevL, prevR, n);
            for (int i = 0; i < n; ++i)
            {
                const float w = static_cast<float> (crossfadeLeft - i) / static_cast<float> (fadeLength + 1);
                nowL[i] += (prevL[i] - nowL[i]) * w;
                nowR[i] += (prevR[i] - nowR[i]) * w;
            }
        }

        for (int i = 0; i < n; ++i)
        {
            if (fade < fadeTarget)
                fade = std::min (fadeTarget, fade + step);
            else if (fade > fadeTarget)
                fade = std::max (fadeTarget, fade - step);
            left[done + i] = nowL[i] * fade;
            right[done + i] = nowR[i] * fade;
        }

        position += n;
        prevPosition += n;
        if (crossfadeLeft > 0)
        {
            crossfadeLeft -= n;
            if (crossfadeLeft <= 0)
            {
                crossfadeLeft = 0;
                prev = nullptr;
            }
        }
        done += n;

        if (fade <= 0.0f && fadeTarget <= 0.0f)
        {
            prev = nullptr;
            crossfadeLeft = 0;
            if (seekPending)
            {
                seekPending = false;
                position = std::llround (seekBeat * samplesPerBeat()) - fadeLength;
                fadeTarget = 1.0f;
            }
            else
            {
                playing = false;
            }
        }
    }

    // A snapshot that came in during a crossfade waits for it to finish.
    takeLatest();
    publishState();
    blocksDone.fetch_add (1);
}

void MasterPlayer::publishState()
{
    statePlaying.store (playing && (fadeTarget > 0.0f || seekPending));
    stateBeat.store (static_cast<double> (position) / samplesPerBeat());

    uint64_t oldest = 0;
    for (const auto* s : { now, prev })
        if (s != nullptr)
            oldest = oldest == 0 ? s->serial : std::min (oldest, s->serial);
    oldestInUse.store (oldest);
}

} // namespace djmix
