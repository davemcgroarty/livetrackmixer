#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "CuePlayer.h"
#include "MasterPlayer.h"
#include "OutputRouting.h"

namespace djmix
{

// The audio device callback: mixes the Master bus (the arrangement) and the
// Cue bus (auditions) and sends each to its outputs, as the routing says.
// With split cue, Cue goes in mono to the left of the Master pair and Master
// in mono to the right.
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    CuePlayer& cue() { return cuePlayer; }
    const CuePlayer& cue() const { return cuePlayer; }
    MasterPlayer& master() { return masterPlayer; }

    void setRouting (const OutputRouting& routing);
    OutputRouting routing() const;

    // Where Master and Cue go on the running device, after any fallback to split cue.
    ResolvedRouting resolvedRouting() const { return resolveRouting (routing(), activeOutputs.load()); }

    // juce::AudioIODeviceCallback (audio thread)
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // For tests: what aboutToStart takes from a device.
    void prepare (double deviceSampleRate, uint64_t activeOutputChannels, int maxBlockSize);

private:
    CuePlayer cuePlayer;
    MasterPlayer masterPlayer;
    juce::AudioBuffer<float> buses; // Master left and right, Cue left and right

    std::atomic<uint64_t> activeOutputs { 0 };
    std::atomic<int> masterFirst { 0 };
    std::atomic<int> cueFirst { 2 };
    std::atomic<bool> splitCue { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace djmix
