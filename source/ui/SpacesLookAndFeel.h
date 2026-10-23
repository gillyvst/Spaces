#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Palette and drawing for the Spaces UI: cream enamel panels, brushed metal,
// bakelite knobs and amber lamps, in the spirit of 1960s studio hardware.
namespace Palette
{
    inline const juce::Colour chassis      { 0xff1d1916 };
    inline const juce::Colour chassisLight { 0xff2c2622 };
    inline const juce::Colour cream        { 0xffeadfc6 };
    inline const juce::Colour creamShade   { 0xffd6c7a8 };
    inline const juce::Colour ink          { 0xff3b2f26 };
    inline const juce::Colour inkSoft      { 0xff76665a };
    inline const juce::Colour metalLight   { 0xffb9b3a8 };
    inline const juce::Colour metalDark    { 0xff6d675f };
    inline const juce::Colour brass        { 0xffb48f55 };
    inline const juce::Colour brassDark    { 0xff6e5532 };
    inline const juce::Colour lamp         { 0xffffb54a };
    inline const juce::Colour lampOff      { 0xff4a2e22 };
    inline const juce::Colour redline      { 0xffb43a2a };
    inline const juce::Colour display      { 0xff14100d };
}

class SpacesLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SpacesLookAndFeel();

    static juce::Font font (float height, bool bold = false, float kerning = 0.0f);

    // Panels and decoration
    static void drawChassis (juce::Graphics&, juce::Rectangle<float> area);
    static void drawCreamPanel (juce::Graphics&, juce::Rectangle<float> area, float corner = 6.0f);
    static void drawMetalStrip (juce::Graphics&, juce::Rectangle<float> area);
    static void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius);
    static void drawEngravedText (juce::Graphics&, const juce::String&, juce::Rectangle<float> area,
                                  juce::Justification, const juce::Font&, juce::Colour = Palette::ink);
    static void drawDisplayWindow (juce::Graphics&, juce::Rectangle<float> area);
    static void drawLamp (juce::Graphics&, juce::Rectangle<float> area, bool on);

    // LookAndFeel overrides
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Font getAlertWindowMessageFont() override;
};
