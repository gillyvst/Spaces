#pragma once

#include "../PluginProcessor.h"
#include "SpacesLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>

// A bakelite knob with its name engraved below and the current value under that.
class Knob final : public juce::Component
{
public:
    enum class Size { large, medium, small };

    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, Size = Size::large);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    juce::RangedAudioParameter* param;
    juce::String caption;
    Size size;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A panel lamp that is also its own switch.
class LampSwitch final : public juce::Component
{
public:
    LampSwitch (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// ‹ name › with a menu of all items when the name is clicked.
class Selector final : public juce::Component
{
public:
    explicit Selector (const juce::String& caption);

    void setItems (const juce::StringArray& names, int current);

    std::function<void (int delta)> onStep;
    std::function<void (int index)> onChoose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String caption;
    juce::StringArray items;
    int current = -1;
    juce::TextButton prev { "<" }, next { ">" };
    juce::Rectangle<int> displayArea;
};

// The big Room 1 / Room 2 / ... selector.
class RoomSwitch final : public juce::Component
{
public:
    void setRooms (const juce::StringArray& names, int current);
    std::function<void (int index)> onSelect;

    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
};

// Small vertical edgewise VU meter (0 VU = -18 dBFS).
class VuMeter final : public juce::Component,
                      private juce::Timer
{
public:
    explicit VuMeter (SpacesProcessor&);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    static float positionForVu (float vu);

    SpacesProcessor& processor;
    float needle[2] { 0.0f, 0.0f };   // 0..1 deflection
    bool peakLamp = false;
    int peakHold = 0;
};

// Preset prev / next / menu / save.
class PresetBar final : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit PresetBar (PresetManager&);
    ~PresetBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showMenu();
    void askToSave();

    PresetManager& presets;
    juce::TextButton prev { "<" }, next { ">" }, save { "Save" };
    juce::Rectangle<int> displayArea;
    std::unique_ptr<juce::AlertWindow> saveDialog;
};

// One layer's four-band EQ, as a row of small knobs.
class LayerEqStrip final : public juce::Component
{
public:
    LayerEqStrip (juce::AudioProcessorValueTreeState&, const LayerParamIDs&, const juce::String& title);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    juce::OwnedArray<Knob> knobs;   // LS f/g, B1 f/g/q, B2 f/g/q, HS f/g
};
