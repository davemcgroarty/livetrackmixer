#include "MainComponent.h"

namespace djmix
{

MainComponent::MainComponent()
{
    setSize (1280, 800);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
}

} // namespace djmix
