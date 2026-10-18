#include "CuePlayer.h"

#include <algorithm>
#include <climits>


namespace djmix
{

namespace
{
constexpr int defaultMaxBlock = 4096;
} // namespace

CuePlayer::CuePlayer() = default;

CuePlayer::~CuePlayer() = default;

//==============================================================================
// Message thread

bool CuePlayer::play (std::shared_ptr<const DecodedAudio> audio,
                      double startSeconds,
                      double endSeconds,
                      uint64_t tag,
                      CueMode mode)
{
    if (audio == nullptr || ! deviceRunning.load())
        return false;

    Command c;
    c.type = Command::Type::play;
    c.audio = audio.get();
    c.startSeconds = startSeconds;
    c.endSeconds = endSeconds;
    c.tag = tag;
    c.mode = mode;
    c.serial = nextSerial++;

    // Keep the audio alive until the audio thread is done with it; one entry
    // per buffer, carrying the latest command that names it.
    const auto existing = std::find_if (owned.begin(), owned.end(), [&] (const Owned& o) { return o.audio == audio; });
    if (existing != owned.end())
        existing->serial = c.serial;
    else
        owned.push_back ({ c.serial, std::move (audio) });

    push (c);
    return true;
}

void CuePlayer::stop()
{
    Command c;
    c.type = Command::Type::stop;
    c.serial = nextSerial++;
    push (c);
}

void CuePlayer::setMode (CueMode mode)
{
    Command c;
    c.type = Command::Type::setMode;
    c.mode = mode;
    c.serial = nextSerial++;
    push (c);
}

void CuePlayer::push (const Command& command)
{
    // If the audio thread has stopped reading (no device), the oldest
    // commands are simply lost when the FIFO is full: a later command would
    // replace their effect anyway.
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        commands[static_cast<size_t> (scope.startIndex1)] = command;
    else if (scope.blockSize2 > 0)
        commands[static_cast<size_t> (scope.startIndex2)] = command;
}

CuePlayer::State CuePlayer::state() const
{
    State s;
    s.playing = stateIsPlaying.load();
    s.tag = stateTag.load();
    s.position = statePosition.load();
    return s;
}

void CuePlayer::releaseUnused()
{
    const auto consumed = lastConsumed.load();
    const bool running = deviceRunning.load();
    std::erase_if (owned, [&] (const Owned& o)
    {
        const auto* p = o.audio.get();
        const bool usedByAudio = inUse[0].load() == p || inUse[1].load() == p;
        // Without a running device nothing can be reading it.
        return ! running || (o.serial <= consumed && ! usedByAudio);
    });
}

//==============================================================================
// Audio thread

void CuePlayer::prepare (double deviceSampleRate, int maxBlockSize)
{
    rate = deviceSampleRate;
    scratch.setSize (2, std::max (maxBlockSize, defaultMaxBlock));

    // Anything queued while no device ran is stale.
    {
        const auto stale = fifo.read (fifo.getNumReady());
        auto last = lastConsumed.load();
        for (int i = 0; i < stale.blockSize1; ++i)
            last = std::max (last, commands[static_cast<size_t> (stale.startIndex1 + i)].serial);
        for (int i = 0; i < stale.blockSize2; ++i)
            last = std::max (last, commands[static_cast<size_t> (stale.startIndex2 + i)].serial);
        lastConsumed.store (last);
    }
    for (auto& v : voices)
        v.audio = nullptr;
    for (auto& p : inUse)
        p.store (nullptr);
    publishState();
    deviceRunning.store (true);
}

void CuePlayer::stopped()
{
    deviceRunning.store (false);
    for (auto& v : voices)
        v.audio = nullptr;
    for (auto& p : inUse)
        p.store (nullptr);
    publishState();
}

void CuePlayer::handleCommands()
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
            auto& v = voices[static_cast<size_t> (current)];
            switch (c.type)
            {
                case Command::Type::play:
                {
                    const auto& audio = *c.audio;
                    const auto length = static_cast<int64_t> (audio.samples.getNumSamples());
                    const auto toFrame = [&] (double seconds)
                    { return std::clamp (static_cast<int64_t> (seconds * audio.sampleRate), int64_t { 0 }, length); };
                    const auto from = toFrame (c.startSeconds);
                    const auto to = c.endSeconds > c.startSeconds ? toFrame (c.endSeconds) : length;
                    startVoice (c.audio, from, to, c.mode, c.tag);
                    break;
                }
                case Command::Type::stop:
                    v.fadeTarget = 0.0f;
                    break;
                case Command::Type::setMode:
                    if (v.active() && v.fadeTarget > 0.0f)
                        applyMode (v, c.mode);
                    break;
            }
            lastConsumed.store (c.serial);
        }
    };
    run (scope.startIndex1, scope.blockSize1);
    run (scope.startIndex2, scope.blockSize2);
}

void CuePlayer::startVoice (const DecodedAudio* audio, int64_t start, int64_t end, CueMode mode, uint64_t tag)
{
    // What was playing fades out in its slot; the new audition takes the other.
    voices[static_cast<size_t> (current)].fadeTarget = 0.0f;
    current = 1 - current;

    // Start one fade early, so the fade-in is over by the first beat: a cue's
    // opening hit plays at full level, and each pass of a loop lands on time.
    auto& v = voices[static_cast<size_t> (current)];
    v.audio = audio;
    v.readIndex = std::max (int64_t { 0 }, start - fadeFrames (*audio));
    v.rangeStart = start;
    v.rangeEnd = end;
    v.fade = 0.0f;
    v.fadeTarget = 1.0f;
    v.tag = tag;
    for (auto& i : v.interpolators)
        i.reset();
    applyMode (v, mode);
}

void CuePlayer::applyMode (Voice& v, CueMode mode)
{
    v.mode = mode;
    const auto length = static_cast<int64_t> (v.audio->samples.getNumSamples());
    if (mode == CueMode::continueTrack)
    {
        v.endIndex = length;
    }
    else if (v.readIndex < v.rangeEnd)
    {
        v.endIndex = v.rangeEnd;
    }
    else
    {
        // Already past the end of its stretch (it was continuing): once fades
        // out where it is, loop goes back to the start on the next block.
        v.endIndex = length;
        if (mode == CueMode::once)
            v.fadeTarget = 0.0f;
    }
}

int64_t CuePlayer::fadeFrames (const DecodedAudio& audio) const
{
    return static_cast<int64_t> (std::ceil (fadeSeconds * audio.sampleRate));
}

int CuePlayer::samplesUntilLoop (const Voice& v) const
{
    // Output samples until the voice is one fade from the end of its stretch.
    // The next pass starts there, one fade before the stretch's start, so the
    // two cross over the last and first few milliseconds and the loop is as
    // long as the stretch.
    const double ratio = v.audio->sampleRate / rate;
    const auto loopAt = std::max (v.rangeEnd - fadeFrames (*v.audio), v.rangeStart + 1);
    const auto frames = loopAt - v.readIndex;
    if (frames <= 0)
        return 0;
    return static_cast<int> (std::min<double> (std::ceil (static_cast<double> (frames) / ratio), INT_MAX));
}

void CuePlayer::renderVoice (Voice& v, float* left, float* right, int numSamples)
{
    const auto available64 = v.endIndex - v.readIndex;
    if (available64 <= 0)
    {
        v.audio = nullptr;
        return;
    }

    const auto& src = v.audio->samples;
    const int available = static_cast<int> (std::min<int64_t> (available64, INT_MAX));
    const auto index = static_cast<int> (v.readIndex);
    const float* l = src.getReadPointer (0, index);
    const float* r = src.getReadPointer (src.getNumChannels() > 1 ? 1 : 0, index);

    const double ratio = v.audio->sampleRate / rate;
    const int used = v.interpolators[0].process (ratio, l, left, numSamples, available, 0);
    v.interpolators[1].process (ratio, r, right, numSamples, available, 0);

    // Fade in or out over fadeSeconds, and down to silence at the end of the
    // range, so that no start, stop, switch or end clicks.
    const double fadeSamples = fadeSeconds * rate;
    const auto step = static_cast<float> (1.0 / fadeSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        if (v.fade < v.fadeTarget)
            v.fade = std::min (v.fadeTarget, v.fade + step);
        else if (v.fade > v.fadeTarget)
            v.fade = std::max (v.fadeTarget, v.fade - step);

        const double outputLeft = (available - i * ratio) / ratio;
        const auto endGain = static_cast<float> (std::clamp (outputLeft / fadeSamples, 0.0, 1.0));
        const float g = std::min (v.fade, endGain);
        left[i] *= g;
        right[i] *= g;
    }

    v.readIndex += used;
    if (v.readIndex >= v.endIndex || (v.fadeTarget <= 0.0f && v.fade <= 0.0f))
        v.audio = nullptr;
}

void CuePlayer::publishState()
{
    const auto& v = voices[static_cast<size_t> (current)];
    const bool playing = v.active() && v.fadeTarget > 0.0f;
    stateIsPlaying.store (playing);
    if (v.active())
    {
        stateTag.store (v.tag);
        statePosition.store (static_cast<double> (v.readIndex) / v.audio->sampleRate);
    }
    inUse[0].store (voices[0].audio);
    inUse[1].store (voices[1].audio);
}

void CuePlayer::render (float* left, float* right, int numSamples)
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);

    handleCommands();
    if (rate <= 0.0 || (! voices[0].active() && ! voices[1].active()))
    {
        publishState();
        return;
    }

    // In chunks no longer than the scratch buffer, and split where a looping
    // audition goes back to its start, so the loop is sample-accurate.
    for (int done = 0; done < numSamples;)
    {
        int n = std::min (numSamples - done, scratch.getNumSamples());
        auto& now = voices[static_cast<size_t> (current)];
        if (now.active() && now.mode == CueMode::loop && now.fadeTarget > 0.0f)
        {
            const int untilLoop = samplesUntilLoop (now);
            if (untilLoop == 0)
            {
                startVoice (now.audio, now.rangeStart, now.rangeEnd, CueMode::loop, now.tag);
                continue;
            }
            n = std::min (n, untilLoop);
        }

        float* voiceL = scratch.getWritePointer (0);
        float* voiceR = scratch.getWritePointer (1);
        for (auto& v : voices)
        {
            if (! v.active())
                continue;
            renderVoice (v, voiceL, voiceR, n);
            juce::FloatVectorOperations::add (left + done, voiceL, n);
            juce::FloatVectorOperations::add (right + done, voiceR, n);
        }
        done += n;
    }

    const float cueGain = gain.load();
    juce::FloatVectorOperations::multiply (left, cueGain, numSamples);
    juce::FloatVectorOperations::multiply (right, cueGain, numSamples);
    publishState();
}

} // namespace djmix
