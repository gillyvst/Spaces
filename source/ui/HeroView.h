#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The big photo of the current room. Shows a still image or, later, a looping
// frame sequence (the animated hero), crossfading when the room changes.
// Animation only runs while the room's ambience is audible, so the space
// "pauses" when the ambience is switched off.
class HeroView final : public juce::Component,
                       private juce::Timer
{
public:
    HeroView();

    void setStill (const juce::Image&);
    void setFrames (juce::Array<juce::Image> frames, double framesPerSecond);
    void setCaption (const juce::String& title, const juce::String& subtitle);
    void setAmbienceActive (bool active);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void startTransition();
    const juce::Image& currentFrame() const;
    void drawPhoto (juce::Graphics&, const juce::Image&, juce::Rectangle<float> area, float alpha) const;

    juce::Array<juce::Image> frames;
    juce::Image previous, grain;
    double fps = 24.0;
    int frameIndex = 0;
    bool ambienceActive = true;
    float transition = 1.0f;   // 0 -> 1 while crossfading from previous
    double lastTick = 0.0, frameClock = 0.0;
    juce::String title, subtitle;
};
