#include "Controls.h"

using namespace juce;

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& state, const String& paramId, const String& captionText, Size s)
    : param (state.getParameter (paramId)), caption (captionText), size (s)
{
    jassert (param != nullptr);

    slider.setSliderStyle (Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setTooltip (param->getName (64));
    addAndMakeVisible (slider);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}

void Knob::resized()
{
    auto area = getLocalBounds();
    const auto textHeight = size == Size::small ? 26 : 32;
    area.removeFromBottom (textHeight);
    const auto side = jmin (area.getWidth(), area.getHeight());
    slider.setBounds (area.withSizeKeepingCentre (side, side));
}

void Knob::paint (Graphics& g)
{
    const auto small = size == Size::small;
    auto text = getLocalBounds().toFloat().removeFromBottom (small ? 26.0f : 32.0f);

    SpacesLookAndFeel::drawEngravedText (g, caption.toUpperCase(), text.removeFromTop (small ? 12.0f : 15.0f),
                                         Justification::centred, SpacesLookAndFeel::font (small ? 10.0f : 12.0f, true, 0.14f));

    g.setColour (slider.isMouseOverOrDragging() ? Palette::ink : Palette::inkSoft);
    g.setFont (SpacesLookAndFeel::font (small ? 10.0f : 11.5f));
    g.drawText (param->getCurrentValueAsText(), text, Justification::centredTop, false);
}

//==============================================================================
LampSwitch::LampSwitch (AudioProcessorValueTreeState& state, const String& paramId, const String& caption)
{
    button.setButtonText (caption);
    button.setTooltip (state.getParameter (paramId)->getName (64));
    addAndMakeVisible (button);
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
}

//==============================================================================
Selector::Selector (const String& captionText) : caption (captionText)
{
    for (auto* b : { &prev, &next })
    {
        b->getProperties().set ("style", "metal");
        addAndMakeVisible (b);
    }

    prev.onClick = [this] { if (onStep) onStep (-1); };
    next.onClick = [this] { if (onStep) onStep (1); };
    prev.setTooltip ("Previous " + caption);
    next.setTooltip ("Next " + caption);
}

void Selector::setItems (const StringArray& names, int newCurrent)
{
    if (names != items || newCurrent != current)
    {
        items = names;
        current = newCurrent;
        repaint();
    }

    prev.setEnabled (items.size() > 1);
    next.setEnabled (items.size() > 1);
}

void Selector::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (78);   // caption
    prev.setBounds (area.removeFromLeft (26).reduced (0, 4));
    next.setBounds (area.removeFromRight (26).reduced (0, 4));
    displayArea = area.reduced (5, 3);
}

void Selector::paint (Graphics& g)
{
    SpacesLookAndFeel::drawEngravedText (g, caption.toUpperCase(), getLocalBounds().toFloat().withWidth (74.0f),
                                         Justification::centredRight, SpacesLookAndFeel::font (11.5f, true, 0.16f));

    SpacesLookAndFeel::drawDisplayWindow (g, displayArea.toFloat());
    g.setColour (Palette::lamp);
    g.setFont (SpacesLookAndFeel::font (15.0f, false, 0.04f));
    const auto text = isPositiveAndBelow (current, items.size()) ? items[current] : String ("-");
    g.drawFittedText (text, displayArea.reduced (8, 0), Justification::centred, 1, 0.8f);
}

void Selector::mouseUp (const MouseEvent& e)
{
    if (! displayArea.contains (e.getPosition()) || items.isEmpty())
        return;

    PopupMenu menu;

    for (int i = 0; i < items.size(); ++i)
        menu.addItem (i + 1, items[i], true, i == current);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (displayArea.getWidth()),
                        [safe = SafePointer<Selector> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0 && safe->onChoose)
                                safe->onChoose (result - 1);
                        });
}

//==============================================================================
void RoomSwitch::setRooms (const StringArray& names, int current)
{
    if (buttons.size() != names.size())
    {
        buttons.clear();

        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = buttons.add (new TextButton (names[i]));
            b->getProperties().set ("style", "room");
            b->setClickingTogglesState (false);
            b->onClick = [this, i] { if (onSelect) onSelect (i); };
            addAndMakeVisible (b);
        }

        resized();
    }

    for (int i = 0; i < buttons.size(); ++i)
    {
        buttons[i]->setButtonText (names[i]);
        buttons[i]->setToggleState (i == current, dontSendNotification);
    }
}

void RoomSwitch::resized()
{
    if (buttons.isEmpty())
        return;

    auto area = getLocalBounds();
    const auto gap = 14;
    const auto width = (area.getWidth() - gap * (buttons.size() - 1)) / buttons.size();

    for (auto* b : buttons)
    {
        b->setBounds (area.removeFromLeft (width));
        area.removeFromLeft (gap);
    }
}

//==============================================================================
VuMeter::VuMeter (SpacesProcessor& p) : processor (p)
{
    startTimerHz (30);
}

// Classic VU scale: deflection is proportional to voltage, so the low end of
// the scale is compressed. -20 VU at the bottom, +3 VU at the top.
float VuMeter::positionForVu (float vu)
{
    const auto lo = std::pow (10.0f, -20.0f / 20.0f), hi = std::pow (10.0f, 3.0f / 20.0f);
    return jlimit (0.0f, 1.04f, (std::pow (10.0f, vu / 20.0f) - lo) / (hi - lo));
}

void VuMeter::timerCallback()
{
    bool changed = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        // The processor's level already has VU ballistics. 0 VU = -18 dBFS
        // RMS sine, whose rectified average is 0.9 x RMS.
        const auto average = processor.getVuLevel (ch);
        const auto vu = Decibels::gainToDecibels (average / 0.9f, -60.0f) + 18.0f;
        const auto target = average > 0.0f ? positionForVu (vu) : 0.0f;

        if (std::abs (target - needle[ch]) > 0.001f)
        {
            needle[ch] = target;
            changed = true;
        }

        if (vu > 3.0f)
        {
            peakLamp = true;
            peakHold = 20;
        }
    }

    if (peakHold > 0 && --peakHold == 0)
    {
        peakLamp = false;
        changed = true;
    }

    if (changed)
        repaint();
}

void VuMeter::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Housing
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (area, 5.0f);
    auto face = area.reduced (4.0f);

    // Backlit scale
    g.setGradientFill (ColourGradient (Colour (0xfff7e3b0), face.getCentreX(), face.getCentreY(),
                                       Colour (0xffc99d55), face.getX(), face.getY(), true));
    g.fillRoundedRectangle (face, 3.0f);

    auto label = face.removeFromBottom (20.0f);
    auto lampArea = face.removeFromTop (16.0f);
    auto scale = face.reduced (2.0f, 6.0f);

    const auto yFor = [&scale] (float pos) { return scale.getBottom() - pos * scale.getHeight(); };

    // Red zone above 0 VU
    g.setColour (Palette::redline.withAlpha (0.85f));
    g.fillRect (Rectangle<float> (scale.getRight() - 5.0f, yFor (positionForVu (3.0f)), 4.0f,
                                  yFor (positionForVu (0.0f)) - yFor (positionForVu (3.0f))));

    // Scale marks
    const float marks[] { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
    g.setFont (SpacesLookAndFeel::font (8.5f, true));

    for (auto m : marks)
    {
        const auto y = yFor (positionForVu (m));
        const auto major = m == -20 || m == -10 || m == -5 || m == 0 || m == 3;
        g.setColour (m > 0 ? Palette::redline : Palette::ink);
        g.drawLine (scale.getRight() - (major ? 10.0f : 6.0f), y, scale.getRight() - 1.0f, y, 1.0f);

        if (major)
            g.drawText (m > 0 ? "+" + String ((int) m) : String ((int) m), Rectangle<float> (scale.getX() - 2.0f, y - 6.0f, scale.getWidth() - 10.0f, 12.0f),
                        Justification::centredLeft, false);
    }

    // Needles: left and right as two pointers
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto y = yFor (needle[ch]);
        Path p;
        const auto x0 = scale.getX() + scale.getWidth() * (ch == 0 ? 0.42f : 0.58f);
        p.addTriangle (x0, y, scale.getRight() + 2.0f, y - 1.2f, scale.getRight() + 2.0f, y + 1.2f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillPath (p, AffineTransform::translation (1.0f, 1.5f));
        g.setColour (Palette::chassis);
        g.fillPath (p);
    }

    // Glass sheen
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.25f), face.getX(), face.getY(),
                                       Colours::transparentWhite, face.getRight(), face.getCentreY(), false));
    g.fillRoundedRectangle (area.reduced (4.0f).withTrimmedBottom (20.0f), 3.0f);

    SpacesLookAndFeel::drawLamp (g, lampArea.withSizeKeepingCentre (10.0f, 10.0f), peakLamp);

    g.setColour (Palette::ink);
    g.setFont (SpacesLookAndFeel::font (11.0f, true, 0.2f));
    g.drawText ("VU", label, Justification::centred, false);
}

//==============================================================================
PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    for (auto* b : { &prev, &next, &save })
    {
        b->getProperties().set ("style", "metal");
        addAndMakeVisible (b);
    }

    prev.onClick = [this] { presets.previous(); };
    next.onClick = [this] { presets.next(); };
    save.onClick = [this] { askToSave(); };
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    save.setTooltip ("Save the current settings as a preset");
    presets.addChangeListener (this);
}

PresetBar::~PresetBar()
{
    presets.removeChangeListener (this);
}

void PresetBar::changeListenerCallback (ChangeBroadcaster*) { repaint(); }

void PresetBar::resized()
{
    auto area = getLocalBounds();
    save.setBounds (area.removeFromRight (56).reduced (0, 4));
    area.removeFromRight (8);
    prev.setBounds (area.removeFromLeft (28).reduced (0, 4));
    next.setBounds (area.removeFromRight (28).reduced (0, 4));
    displayArea = area.reduced (6, 2);
}

void PresetBar::paint (Graphics& g)
{
    SpacesLookAndFeel::drawDisplayWindow (g, displayArea.toFloat());
    auto name = presets.getCurrentName();
    g.setColour (Palette::lamp);
    g.setFont (SpacesLookAndFeel::font (15.0f, true, 0.06f));
    g.drawFittedText (name.isEmpty() ? String ("Init") : name, displayArea.reduced (24, 0), Justification::centred, 1, 0.8f);

    // Menu arrow
    Path arrow;
    const auto c = Point<float> ((float) displayArea.getRight() - 14.0f, (float) displayArea.getCentreY());
    arrow.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
    g.setColour (Palette::lamp.withAlpha (0.7f));
    g.fillPath (arrow);
}

void PresetBar::mouseUp (const MouseEvent& e)
{
    if (displayArea.contains (e.getPosition()))
        showMenu();
}

void PresetBar::showMenu()
{
    PopupMenu menu, factory, user;
    const auto& entries = presets.getEntries();

    for (int i = 0; i < entries.size(); ++i)
        (entries[i].isFactory ? factory : user).addItem (i + 1, entries[i].name, true, i == presets.getCurrentIndex());

    menu.addSectionHeader ("Factory");
    for (PopupMenu::MenuItemIterator it (factory); it.next();)
        menu.addItem (it.getItem());

    menu.addSectionHeader ("User");

    if (user.getNumItems() == 0)
        menu.addItem (-100, "(none saved yet)", false);
    else
        for (PopupMenu::MenuItemIterator it (user); it.next();)
            menu.addItem (it.getItem());

    menu.addSeparator();
    menu.addItem (-1, "Save as...");

    const auto current = presets.getCurrentIndex();
    menu.addItem (-2, "Delete \"" + presets.getCurrentName() + "\"",
                  isPositiveAndBelow (current, entries.size()) && ! entries[current].isFactory);
    menu.addItem (-3, "Show presets folder");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (displayArea.getWidth()),
                        [safe = SafePointer<PresetBar> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& p = safe->presets;

                            if (result > 0)                p.load (result - 1);
                            else if (result == -1)         safe->askToSave();
                            else if (result == -2)         p.deleteUserPreset (p.getCurrentIndex());
                            else if (result == -3)
                            {
                                PresetManager::getUserPresetFolder().createDirectory();
                                PresetManager::getUserPresetFolder().revealToUser();
                            }
                        });
}

void PresetBar::askToSave()
{
    saveDialog = std::make_unique<AlertWindow> ("Save preset", "Name for this preset:", MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", presets.getCurrentName());
    saveDialog->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

    saveDialog->enterModalState (true, ModalCallbackFunction::create ([safe = SafePointer<PresetBar> (this)] (int result)
    {
        if (safe == nullptr || safe->saveDialog == nullptr)
            return;

        const auto name = safe->saveDialog->getTextEditorContents ("name");
        safe->saveDialog->setVisible (false);

        if (result == 1)
        {
            const auto error = safe->presets.saveUserPreset (name);

            if (error.isNotEmpty())
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save preset", error);
        }
    }), false);
}

//==============================================================================
LayerEqStrip::LayerEqStrip (AudioProcessorValueTreeState& state, const LayerParamIDs& ids, const String& titleText)
    : title (titleText)
{
    const auto add = [&] (const String& id, const String& caption)
    {
        addAndMakeVisible (knobs.add (new Knob (state, id, caption, Knob::Size::small)));
    };

    add (ids.lsFreq(), "Freq");  add (ids.lsGain(), "Gain");
    add (ids.b1Freq(), "Freq");  add (ids.b1Gain(), "Gain");  add (ids.b1Q(), "Q");
    add (ids.b2Freq(), "Freq");  add (ids.b2Gain(), "Gain");  add (ids.b2Q(), "Q");
    add (ids.hsFreq(), "Freq");  add (ids.hsGain(), "Gain");
}

static const int eqGroupSizes[] { 2, 3, 3, 2 };
static const char* const eqGroupNames[] { "Low Shelf", "Bell 1", "Bell 2", "High Shelf" };

void LayerEqStrip::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (110);   // title
    area.removeFromTop (22);     // group names
    area = area.withSizeKeepingCentre (area.getWidth(), jmin (area.getHeight(), 96));

    const auto gap = 18;
    const auto knobWidth = (area.getWidth() - gap * 3) / 10;
    int k = 0;

    for (auto groupSize : eqGroupSizes)
    {
        for (int i = 0; i < groupSize; ++i)
            knobs[k++]->setBounds (area.removeFromLeft (knobWidth));

        area.removeFromLeft (gap);
    }
}

void LayerEqStrip::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    SpacesLookAndFeel::drawEngravedText (g, title.toUpperCase(), area.removeFromLeft (104.0f), Justification::centredLeft,
                                         SpacesLookAndFeel::font (13.0f, true, 0.18f));
    area.removeFromLeft (6.0f);

    const auto gap = 18.0f;
    const auto knobWidth = (area.getWidth() - gap * 3.0f) / 10.0f;

    for (int i = 0; i < 4; ++i)
    {
        const auto width = knobWidth * (float) eqGroupSizes[i];
        auto group = area.removeFromLeft (width);
        area.removeFromLeft (gap);

        auto header = group.removeFromTop (16.0f);
        SpacesLookAndFeel::drawEngravedText (g, String (eqGroupNames[i]).toUpperCase(), header, Justification::centred,
                                             SpacesLookAndFeel::font (10.0f, true, 0.16f), Palette::inkSoft);
        g.setColour (Palette::ink.withAlpha (0.25f));
        g.drawHorizontalLine ((int) header.getBottom(), group.getX() + 6.0f, group.getRight() - 6.0f);
    }
}
