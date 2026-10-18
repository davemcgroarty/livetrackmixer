#include "AudioOutput.h"

namespace djmix
{

AudioOutput::AudioOutput (AppSettings& s)
    : settings (s)
{
    const auto saved = settings.audioDeviceState();
    error = deviceManager.initialise (0, 2, saved.get(), true);

    if (const auto routing = settings.outputRouting())
    {
        audioEngine.setRouting (*routing);
        routingChosen = true;
    }
    else
    {
        audioEngine.setRouting (OutputRouting::defaultFor (numOutputs()));
    }
    audioEngine.cue().setGain (settings.cueVolume());

    enableNeededChannels();
    deviceManager.addAudioCallback (&audioEngine);
    deviceManager.addChangeListener (this);
}

AudioOutput::~AudioOutput()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&audioEngine);
    deviceManager.closeAudioDevice();
}

juce::String AudioOutput::deviceName() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getName();
    return {};
}

int AudioOutput::numOutputs() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getOutputChannelNames().size();
    return 0;
}

void AudioOutput::setRouting (const OutputRouting& routing)
{
    audioEngine.setRouting (routing);
    routingChosen = true;
    settings.setOutputRouting (routing);
    enableNeededChannels();
}

void AudioOutput::setCueVolume (float gain)
{
    audioEngine.cue().setGain (gain);
    settings.setCueVolume (gain);
}

void AudioOutput::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A device the user picked in Settings: until a routing is chosen, follow
    // the new device's default.
    if (! routingChosen)
        audioEngine.setRouting (OutputRouting::defaultFor (numOutputs()));
    enableNeededChannels();

    const auto state = deviceManager.createStateXml();
    settings.setAudioDeviceState (state.get());
}

void AudioOutput::enableNeededChannels()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto available = device->getOutputChannelNames().size();
    const auto needed = channelsNeeded (audioEngine.routing());

    bool changed = false;
    for (int ch = 0; ch < std::min (available, 64); ++ch)
    {
        if (((needed >> ch) & 1u) != 0 && ! setup.outputChannels[ch])
        {
            setup.outputChannels.setBit (ch);
            changed = true;
        }
    }
    if (changed)
    {
        // Calls back into changeListenerCallback, which then finds nothing to change.
        setup.useDefaultOutputChannels = false;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
}

} // namespace djmix
