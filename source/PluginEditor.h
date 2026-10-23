#pragma once
#include "PluginProcessor.h"
class SpacesEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SpacesEditor (SpacesProcessor& p) : AudioProcessorEditor (p) { setSize (1100, 700); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
};
