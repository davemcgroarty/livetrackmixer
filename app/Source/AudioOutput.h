#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "AppSettings.h"
#include "engine/AudioEngine.h"

namespace djmix
{

// The audio device and what plays through it: opens the device saved in the
// settings (or the system default), keeps the channels the Master and Cue
// routing needs switched on, and saves any change the user makes.
class AudioOutput final : private juce::ChangeListener
{
public:
    explicit AudioOutput (AppSettings& settings);
    ~AudioOutput() override;

    juce::AudioDeviceManager& devices() { return deviceManager; }
    AudioEngine& engine() { return audioEngine; }
    CuePlayer& cue() { return audioEngine.cue(); }
    MasterPlayer& master() { return audioEngine.master(); }

    bool isOpen() const { return deviceManager.getCurrentAudioDevice() != nullptr; }
    juce::String deviceName() const;
    int numOutputs() const;

    // Why no device opened, if none did.
    const juce::String& openError() const { return error; }

    OutputRouting routing() const { return audioEngine.routing(); }
    void setRouting (const OutputRouting& routing);

    float cueVolume() const { return audioEngine.cue().getGain(); }
    void setCueVolume (float gain);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void enableNeededChannels();

    AppSettings& settings;
    juce::AudioDeviceManager deviceManager;
    AudioEngine audioEngine;
    juce::String error;
    bool routingChosen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioOutput)
};

} // namespace djmix
