#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/HeroView.h"
#include "ui/SpacesLookAndFeel.h"

// The editor is laid out at a fixed 1100 x 700 and scaled as a whole
// (75 / 100 / 125 / 150%), so everything stays crisp and in proportion.
class SpacesEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit SpacesEditor (SpacesProcessor&);
    ~SpacesEditor() override;

    void resized() override;

    static constexpr int baseWidth = 1100, baseHeight = 700;

private:
    class Content;

    void timerCallback() override;
    void refreshSelection();
    void setScale (float);
    void showScaleMenu();
    juce::Array<juce::Image> loadHeroFrames (const RoomDef&);

    SpacesProcessor& processor;
    SpacesLookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;
    juce::TooltipWindow tooltips { nullptr, 600 };

    // Last state shown, so the timer only touches what changed
    Selection shownSelection;
    bool shownAmbience = true;
};
