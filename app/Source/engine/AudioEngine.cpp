#include "AudioEngine.h"

#include <algorithm>

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

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() = default;

void AudioEngine::setRouting (const OutputRouting& r)
{
    masterFirst.store (r.masterFirst);
    cueFirst.store (r.cueFirst);
    splitCue.store (r.splitCue);
}

OutputRouting AudioEngine::routing() const
{
    OutputRouting r;
    r.masterFirst = masterFirst.load();
    r.cueFirst = cueFirst.load();
    r.splitCue = splitCue.load();
    return r;
}

void AudioEngine::prepare (double deviceSampleRate, uint64_t activeOutputChannels, int maxBlockSize)
{
    activeOutputs.store (activeOutputChannels);
    const int block = std::max (maxBlockSize, defaultMaxBlock);
    buses.setSize (4, block);
    cuePlayer.prepare (deviceSampleRate, block);
    masterPlayer.prepare (deviceSampleRate, block);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), toMask (device->getActiveOutputChannels()),
             device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceStopped()
{
    cuePlayer.stopped();
    masterPlayer.stopped();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*,
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

    const auto routes = resolveRouting (routing(), activeOutputs.load());
    auto output = [&] (int index) -> float* { return index >= 0 && index < numOutputs ? outputs[index] : nullptr; };

    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, buses.getNumSamples());
        float* masterL = buses.getWritePointer (0);
        float* masterR = buses.getWritePointer (1);
        float* cueL = buses.getWritePointer (2);
        float* cueR = buses.getWritePointer (3);
        masterPlayer.render (masterL, masterR, n);
        cuePlayer.render (cueL, cueR, n);

        auto add = [&] (int index, const float* bus, float gain)
        {
            if (auto* out = output (index))
                juce::FloatVectorOperations::addWithMultiply (out + done, bus, gain, n);
        };
        if (routes.split)
        {
            add (routes.cueLeft, cueL, 0.5f);
            add (routes.cueLeft, cueR, 0.5f);
            add (routes.masterRight, masterL, 0.5f);
            add (routes.masterRight, masterR, 0.5f);
        }
        else
        {
            add (routes.masterLeft, masterL, 1.0f);
            add (routes.masterRight, masterR, 1.0f);
            add (routes.cueLeft, cueL, 1.0f);
            add (routes.cueRight, cueR, 1.0f);
        }
        done += n;
    }
}

} // namespace djmix
