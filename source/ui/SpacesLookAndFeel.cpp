#include "SpacesLookAndFeel.h"

using namespace juce;

SpacesLookAndFeel::SpacesLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId, Palette::chassisLight);
    setColour (PopupMenu::textColourId, Palette::cream);
    setColour (PopupMenu::highlightedBackgroundColourId, Palette::lamp.withAlpha (0.85f));
    setColour (PopupMenu::highlightedTextColourId, Palette::chassis);
    setColour (AlertWindow::backgroundColourId, Palette::cream);
    setColour (AlertWindow::textColourId, Palette::ink);
    setColour (AlertWindow::outlineColourId, Palette::brassDark);
    setColour (TextEditor::backgroundColourId, Palette::display);
    setColour (TextEditor::textColourId, Palette::lamp);
    setColour (TextEditor::highlightColourId, Palette::lamp.withAlpha (0.3f));
    setColour (TextEditor::outlineColourId, Palette::brassDark);
    setColour (TextEditor::focusedOutlineColourId, Palette::brass);
    setColour (CaretComponent::caretColourId, Palette::lamp);
    setColour (TextButton::textColourOffId, Palette::ink);
    setColour (TextButton::textColourOnId, Palette::ink);
    setColour (Label::textColourId, Palette::ink);
    setColour (TooltipWindow::textColourId, Palette::cream);
}

Font SpacesLookAndFeel::font (float height, bool bold, float kerning)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain).withKerningFactor (kerning));
}

//==============================================================================
void SpacesLookAndFeel::drawChassis (Graphics& g, Rectangle<float> area)
{
    // Dark warm enamel with a soft pool of light from above.
    g.setGradientFill (ColourGradient (Palette::chassisLight, area.getCentreX(), area.getY(),
                                       Palette::chassis, area.getCentreX(), area.getBottom(), false));
    g.fillRect (area);

    ColourGradient glow (Palette::lamp.withAlpha (0.07f), area.getCentreX(), area.getY() + area.getHeight() * 0.35f,
                         Colours::transparentBlack, area.getCentreX(), area.getBottom() + area.getHeight() * 0.1f, true);
    g.setGradientFill (glow);
    g.fillRect (area);
}

void SpacesLookAndFeel::drawCreamPanel (Graphics& g, Rectangle<float> area, float corner)
{
    // Drop shadow
    for (int i = 4; i > 0; --i)
    {
        g.setColour (Colours::black.withAlpha (0.08f));
        g.fillRoundedRectangle (area.translated (0.0f, (float) i * 0.8f).expanded ((float) i * 0.6f), corner + (float) i);
    }

    g.setGradientFill (ColourGradient (Palette::cream, area.getX(), area.getY(),
                                       Palette::creamShade, area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, corner);

    // Warm light falling from the top
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.18f), area.getCentreX(), area.getY(),
                                       Colours::transparentWhite, area.getCentreX(), area.getY() + area.getHeight() * 0.5f, false));
    g.fillRoundedRectangle (area, corner);

    // Bevel
    g.setColour (Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (area.reduced (1.0f), corner, 1.0f);
    g.setColour (Palette::brassDark.withAlpha (0.6f));
    g.drawRoundedRectangle (area, corner, 1.0f);
}

void SpacesLookAndFeel::drawMetalStrip (Graphics& g, Rectangle<float> area)
{
    g.setGradientFill (ColourGradient (Palette::metalLight, area.getX(), area.getY(),
                                       Palette::metalDark, area.getX(), area.getBottom(), false));
    g.fillRect (area);

    // Brushed grain: fine horizontal lines of varying brightness.
    Random random (42);

    for (float y = area.getY(); y < area.getBottom(); y += 1.0f)
    {
        g.setColour ((random.nextBool() ? Colours::white : Colours::black).withAlpha (random.nextFloat() * 0.06f));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
    }

    g.setColour (Colours::white.withAlpha (0.35f));
    g.drawHorizontalLine ((int) area.getY(), area.getX(), area.getRight());
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine ((int) area.getBottom() - 1, area.getX(), area.getRight());
}

void SpacesLookAndFeel::drawScrew (Graphics& g, Point<float> c, float r)
{
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (0.0f, 1.0f)));
    g.setGradientFill (ColourGradient (Palette::metalLight.brighter (0.3f), c.x - r, c.y - r,
                                       Palette::metalDark.darker (0.3f), c.x + r, c.y + r, false));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (Colours::black.withAlpha (0.55f));
    Path slot;
    slot.addRectangle (-r * 0.75f, -r * 0.12f, r * 1.5f, r * 0.24f);
    g.fillPath (slot, AffineTransform::rotation (0.6f).translated (c));
}

void SpacesLookAndFeel::drawEngravedText (Graphics& g, const String& text, Rectangle<float> area,
                                          Justification j, const Font& f, Colour colour)
{
    g.setFont (f);
    g.setColour (Colours::white.withAlpha (0.45f));
    g.drawText (text, area.translated (0.0f, 1.0f), j, false);
    g.setColour (colour);
    g.drawText (text, area, j, false);
}

void SpacesLookAndFeel::drawDisplayWindow (Graphics& g, Rectangle<float> area)
{
    g.setColour (Colours::white.withAlpha (0.4f));
    g.drawRoundedRectangle (area.translated (0.0f, 1.0f), 3.0f, 1.0f);
    g.setGradientFill (ColourGradient (Palette::display.brighter (0.15f), area.getX(), area.getY(),
                                       Palette::display, area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (area, 3.0f, 1.0f);

    // Faint glass reflection
    g.setColour (Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (area.withHeight (area.getHeight() * 0.45f).reduced (2.0f, 1.0f), 2.0f);
}

void SpacesLookAndFeel::drawLamp (Graphics& g, Rectangle<float> area, bool on)
{
    const auto c = area.getCentre();
    const auto r = jmin (area.getWidth(), area.getHeight()) * 0.5f;

    if (on)
    {
        ColourGradient halo (Palette::lamp.withAlpha (0.45f), c.x, c.y, Palette::lamp.withAlpha (0.0f), c.x + r * 2.2f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (Rectangle<float> (r * 4.4f, r * 4.4f).withCentre (c));
    }

    // Chrome bezel
    g.setGradientFill (ColourGradient (Palette::metalLight.brighter (0.4f), c.x, c.y - r, Palette::metalDark.darker (0.4f), c.x, c.y + r, false));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

    const auto lens = Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c);
    g.setGradientFill (on ? ColourGradient (Colour (0xfffff2c8), c.x - r * 0.2f, c.y - r * 0.3f, Palette::lamp.darker (0.2f), c.x + r, c.y + r, true)
                          : ColourGradient (Palette::lampOff.brighter (0.25f), c.x - r * 0.2f, c.y - r * 0.3f, Palette::lampOff.darker (0.6f), c.x + r, c.y + r, true));
    g.fillEllipse (lens);
    g.setColour (Colours::white.withAlpha (on ? 0.6f : 0.18f));
    g.fillEllipse (lens.reduced (lens.getWidth() * 0.3f).translated (-r * 0.18f, -r * 0.22f).withHeight (lens.getHeight() * 0.22f));
}

//==============================================================================
void SpacesLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                                          float startAngle, float endAngle, Slider& slider)
{
    const auto area = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto size = jmin (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const auto outer = size * 0.5f;
    const auto skirt = outer * 0.78f;
    const auto cap = outer * 0.56f;
    const auto angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = slider.isEnabled();

    // Scale ticks printed on the panel
    g.setColour (Palette::ink.withAlpha (enabled ? 0.75f : 0.3f));
    const int ticks = 11;

    for (int i = 0; i < ticks; ++i)
    {
        const auto a = startAngle + (float) i / (float) (ticks - 1) * (endAngle - startAngle);
        const auto major = i == 0 || i == ticks - 1 || i == ticks / 2;
        const auto r1 = outer * (major ? 0.84f : 0.88f), r2 = outer * 0.98f;
        g.drawLine (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), c.x + r2 * std::sin (a), c.y - r2 * std::cos (a),
                    major ? 1.6f : 1.0f);
    }

    // Shadow
    for (int i = 5; i > 0; --i)
    {
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillEllipse (Rectangle<float> (skirt * 2.0f + (float) i * 1.6f, skirt * 2.0f + (float) i * 1.6f)
                           .withCentre (c.translated (0.0f, skirt * 0.12f + (float) i * 0.4f)));
    }

    // Skirt: a fluted bakelite ring
    g.setGradientFill (ColourGradient (Colour (0xff4a3a30), c.x, c.y - skirt, Colour (0xff120d0a), c.x, c.y + skirt, false));
    g.fillEllipse (Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (c));

    g.setColour (Colours::black.withAlpha (0.35f));
    for (int i = 0; i < 36; ++i)
    {
        const auto a = angle + (float) i * MathConstants<float>::twoPi / 36.0f;
        g.drawLine (c.x + cap * 1.02f * std::sin (a), c.y - cap * 1.02f * std::cos (a),
                    c.x + skirt * 0.97f * std::sin (a), c.y - skirt * 0.97f * std::cos (a), 0.8f);
    }

    // Cap: domed, with a warm highlight from the lamp above
    g.setGradientFill (ColourGradient (Colour (0xff6a5242), c.x - cap * 0.35f, c.y - cap * 0.45f,
                                       Colour (0xff17110e), c.x + cap * 0.5f, c.y + cap * 0.8f, true));
    g.fillEllipse (Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (c));
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (c), 1.0f);

    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.22f), c.x - cap * 0.3f, c.y - cap * 0.6f,
                                       Colours::transparentWhite, c.x, c.y, false));
    g.fillEllipse (Rectangle<float> (cap * 1.3f, cap * 0.9f).withCentre (c.translated (-cap * 0.15f, -cap * 0.4f)));

    // Pointer: an inlaid cream line across skirt and cap
    Path pointer;
    const auto w = jmax (1.8f, outer * 0.075f);
    pointer.addRoundedRectangle (-w * 0.5f, -skirt * 0.96f, w, skirt * 0.62f, w * 0.5f);
    g.setColour (enabled ? Palette::cream : Palette::cream.withAlpha (0.4f));
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (c));
}

//==============================================================================
// Buttons. A button's "style" property picks the look:
//   "room"  - big illuminated push-button (room switch)
//   "metal" - small brushed-metal key (arrows, menus)
//   default - cream cap push-button
void SpacesLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    const auto style = b.getProperties()["style"].toString();
    auto area = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();

    if (down)
        area = area.translated (0.0f, 1.0f);

    if (style == "metal")
    {
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (area.translated (0.0f, 1.0f), 3.0f);
        g.setGradientFill (ColourGradient (Palette::metalLight.brighter (highlighted ? 0.25f : 0.1f), area.getX(), area.getY(),
                                           Palette::metalDark, area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (area, 3.0f, 1.0f);
        return;
    }

    const float corner = style == "room" ? 5.0f : 3.5f;

    // Recess in the panel
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (area.expanded (1.5f), corner + 1.0f);

    if (on)
    {
        // Lit from behind
        g.setGradientFill (ColourGradient (Colour (0xfffff0c8), area.getCentreX(), area.getCentreY(),
                                           Palette::lamp.darker (0.15f), area.getX(), area.getY(), true));
        g.fillRoundedRectangle (area, corner);

        ColourGradient bloom (Palette::lamp.withAlpha (0.35f), area.getCentreX(), area.getCentreY(),
                              Palette::lamp.withAlpha (0.0f), area.getX() - 12.0f, area.getCentreY(), true);
        g.setGradientFill (bloom);
        g.fillRoundedRectangle (area.expanded (10.0f), corner + 8.0f);
    }
    else
    {
        g.setGradientFill (ColourGradient (Palette::cream.brighter (highlighted ? 0.08f : 0.0f), area.getX(), area.getY(),
                                           Palette::creamShade.darker (0.12f), area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle (area, corner);
    }

    g.setColour (Colours::white.withAlpha (on ? 0.5f : 0.65f));
    g.drawRoundedRectangle (area.reduced (1.0f), corner, 1.0f);
    g.setColour (Palette::brassDark.withAlpha (0.8f));
    g.drawRoundedRectangle (area, corner, 1.0f);
}

void SpacesLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool down)
{
    const auto style = b.getProperties()["style"].toString();
    auto area = b.getLocalBounds().toFloat().reduced (4.0f, 2.0f);

    if (down)
        area = area.translated (0.0f, 1.0f);

    const auto height = style == "room" ? jmin (20.0f, area.getHeight() * 0.42f) : jmin (13.0f, area.getHeight() * 0.62f);
    const auto f = font (height, true, style == "room" ? 0.18f : 0.08f);

    if (style == "metal")
    {
        g.setFont (f);
        g.setColour (Palette::chassis);
        g.drawText (b.getButtonText(), area, Justification::centred, false);
        return;
    }

    drawEngravedText (g, b.getButtonText().toUpperCase(), area, Justification::centred, f,
                      b.getToggleState() ? Palette::ink.darker (0.4f) : Palette::ink);
}

void SpacesLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    // A panel lamp with its label to the right; the whole thing is the switch.
    auto area = b.getLocalBounds().toFloat();
    const auto lampSize = jmin (18.0f, area.getHeight() - 4.0f);
    auto lampArea = area.removeFromLeft (lampSize + 6.0f).withSizeKeepingCentre (lampSize, lampSize);

    drawLamp (g, lampArea, b.getToggleState());

    if (highlighted)
    {
        g.setColour (Palette::lamp.withAlpha (0.25f));
        g.drawEllipse (lampArea.expanded (2.5f), 1.0f);
    }

    drawEngravedText (g, b.getButtonText().toUpperCase(), area.withTrimmedLeft (4.0f), Justification::centredLeft,
                      font (12.0f, true, 0.12f));
}

//==============================================================================
void SpacesLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (Palette::chassisLight);
    g.setColour (Palette::brassDark);
    g.drawRect (0, 0, width, height, 1);
}

void SpacesLookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive,
                                           bool isHighlighted, bool isTicked, bool hasSubMenu, const String& text,
                                           const String& shortcutKeyText, const Drawable* icon, const Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (Palette::brassDark.withAlpha (0.7f));
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (findColour (PopupMenu::highlightedBackgroundColourId));
        g.fillRect (area.reduced (2, 1));
    }

    auto r = area.reduced (10, 0);

    if (isTicked)
    {
        const auto lampArea = r.removeFromLeft (14).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        g.setColour (isHighlighted ? Palette::chassis : Palette::lamp);
        g.fillEllipse (lampArea);
    }
    else
    {
        r.removeFromLeft (14);
    }

    g.setFont (getPopupMenuFont());
    g.setColour (! isActive ? Palette::cream.withAlpha (0.35f)
                            : textColour != nullptr ? *textColour
                            : isHighlighted ? Palette::chassis : Palette::cream);
    g.drawText (text, r.withTrimmedLeft (4), Justification::centredLeft, true);

    if (hasSubMenu)
        g.drawText (">", r, Justification::centredRight, false);

    ignoreUnused (shortcutKeyText, icon);
}

Font SpacesLookAndFeel::getPopupMenuFont()          { return font (14.0f); }
Font SpacesLookAndFeel::getAlertWindowMessageFont() { return font (14.0f); }

void SpacesLookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    g.fillAll (Palette::chassisLight);
    g.setColour (Palette::brassDark);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (Palette::cream);
    g.setFont (font (13.0f));
    g.drawFittedText (text, Rectangle<int> (width, height).reduced (6, 3), Justification::centredLeft, 3);
}
