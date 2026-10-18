#include "CuePlayer.h"

#include <algorithm>
#include <climits>

#include "RealtimeGuard.h"

namespace djmix
{

namespace
{
constexpr int defaultMaxBlock = 4096;

uint64_t toMask (const juce::BigInteger& bits)
{
    uint64_t mask = 0;
    for (int i = 0; i < 64; ++i)
        if (bits[i])
            mask |= uint64_t { 1 } << i;
    return mask;
}
} // namespace

CuePlayer::CuePlayer() = default;

CuePlayer::~CuePlayer() = default;

//==============================================================================
// Message thread

bool CuePlayer::play (std::shared_ptr<const DecodedAudio> audio, double startSeconds, double endSeconds, uint64_t tag)
{
    if (audio == nullptr || ! deviceRunning.load())
        return false;

    Command c;
    c.type = Command::Type::play;
    c.audio = audio.get();
    c.startSeconds = startSeconds;
    c.endSeconds = endSeconds;
    c.tag = tag;
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

void CuePlayer::setRouting (const OutputRouting& r)
{
    masterFirst.store (r.masterFirst);
    cueFirst.store (r.cueFirst);
    splitCue.store (r.splitCue);
}

OutputRouting CuePlayer::routing() const
{
    OutputRouting r;
    r.masterFirst = masterFirst.load();
    r.cueFirst = cueFirst.load();
    r.splitCue = splitCue.load();
    return r;
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

void CuePlayer::prepare (double deviceSampleRate, uint64_t activeOutputChannels, int maxBlockSize)
{
    rate = deviceSampleRate;
    deviceRate.store (deviceSampleRate);
    activeOutputs.store (activeOutputChannels);
    scratch.setSize (4, std::max (maxBlockSize, defaultMaxBlock));

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

void CuePlayer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), toMask (device->getActiveOutputChannels()),
             device->getCurrentBufferSizeSamples());
}

void CuePlayer::audioDeviceStopped()
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
            if (c.type == Command::Type::play)
                startVoice (c);
            else
                voices[static_cast<size_t> (current)].fadeTarget = 0.0f;
            lastConsumed.store (c.serial);
        }
    };
    run (scope.startIndex1, scope.blockSize1);
    run (scope.startIndex2, scope.blockSize2);
}

void CuePlayer::startVoice (const Command& c)
{
    // What was playing fades out in its slot; the new audition takes the other.
    voices[static_cast<size_t> (current)].fadeTarget = 0.0f;
    current = 1 - current;

    auto& v = voices[static_cast<size_t> (current)];
    const auto& audio = *c.audio;
    const auto length = static_cast<int64_t> (audio.samples.getNumSamples());
    const auto toFrame = [&] (double seconds) { return std::clamp (static_cast<int64_t> (seconds * audio.sampleRate), int64_t { 0 }, length); };

    v.audio = c.audio;
    v.readIndex = toFrame (c.startSeconds);
    v.endIndex = c.endSeconds > c.startSeconds ? toFrame (c.endSeconds) : length;
    v.fade = 0.0f;
    v.fadeTarget = 1.0f;
    v.tag = c.tag;
    for (auto& i : v.interpolators)
        i.reset();
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

void CuePlayer::audioDeviceIOCallbackWithContext (const float* const*,
                                                  int,
                                                  float* const* outputs,
                                                  int numOutputs,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&)
{
    rt::ScopedRealtimeThread realtime;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    handleCommands();
    if (rate <= 0.0 || (! voices[0].active() && ! voices[1].active()))
    {
        publishState();
        return;
    }

    const auto routes = resolveRouting (routing(), activeOutputs.load());
    const float cueGain = gain.load();
    auto output = [&] (int index) -> float* { return index >= 0 && index < numOutputs ? outputs[index] : nullptr; };

    // In chunks no longer than the scratch buffer.
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, scratch.getNumSamples());
        float* mixL = scratch.getWritePointer (0);
        float* mixR = scratch.getWritePointer (1);
        float* voiceL = scratch.getWritePointer (2);
        float* voiceR = scratch.getWritePointer (3);
        juce::FloatVectorOperations::clear (mixL, n);
        juce::FloatVectorOperations::clear (mixR, n);

        for (auto& v : voices)
        {
            if (! v.active())
                continue;
            renderVoice (v, voiceL, voiceR, n);
            juce::FloatVectorOperations::add (mixL, voiceL, n);
            juce::FloatVectorOperations::add (mixR, voiceR, n);
        }

        if (routes.split)
        {
            if (auto* out = output (routes.cueLeft))
            {
                juce::FloatVectorOperations::addWithMultiply (out + done, mixL, 0.5f * cueGain, n);
                juce::FloatVectorOperations::addWithMultiply (out + done, mixR, 0.5f * cueGain, n);
            }
        }
        else
        {
            if (auto* out = output (routes.cueLeft))
                juce::FloatVectorOperations::addWithMultiply (out + done, mixL, cueGain, n);
            if (auto* out = output (routes.cueRight))
                juce::FloatVectorOperations::addWithMultiply (out + done, mixR, cueGain, n);
        }
        done += n;
    }

    publishState();
}

} // namespace djmix
