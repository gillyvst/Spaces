#include "PluginEditor.h"

using namespace juce;

namespace Layout
{
    // All in the 1100 x 700 base coordinate space.
    const Rectangle<int> topBar      { 0, 0, 1100, 58 };
    const Rectangle<int> directPanel { 16, 74, 178, 466 };
    const Rectangle<int> hero        { 210, 74, 630, 356 };
    const Rectangle<int> roomSwitch  { 222, 444, 606, 46 };
    const Rectangle<int> irSelector  { 210, 502, 312, 32 };
    const Rectangle<int> ambSelector { 528, 502, 312, 32 };
    const Rectangle<int> roomPanel   { 856, 74, 178, 466 };
    const Rectangle<int> vu          { 1046, 74, 40, 466 };
    const Rectangle<int> bottomPanel { 16, 556, 1070, 128 };
    const Rectangle<int> eqDrawer    { 16, 74, 1070, 466 };
}

//==============================================================================
class SpacesEditor::Content final : public Component
{
public:
    explicit Content (SpacesProcessor& p)
        : state (p.getState()),
          presetBar (p.getPresets()),
          bypass (state, ParamIDs::bypass, "Bypass"),
          directLevel (state, directIDs.level(), "Level"),
          directLength (state, directIDs.length(), "Length"),
          directDrive (state, directIDs.drive(), "Drive"),
          roomLevel (state, roomIDs.level(), "Level"),
          roomSize (state, roomIDs.size(), "Size"),
          roomDrive (state, roomIDs.drive(), "Drive"),
          irSelector ("IR"),
          ambSelector ("Ambience"),
          vu (p),
          ambOn (state, ParamIDs::ambOn, "On"),
          ambLevel (state, ParamIDs::ambLevel, "Level", Knob::Size::medium),
          mix (state, ParamIDs::mix, "Dry / Wet", Knob::Size::medium),
          autoGain (state, ParamIDs::autoGain, "Auto Gain"),
          trim (state, ParamIDs::outTrim, "Output", Knob::Size::medium),
          mono (state, ParamIDs::mono, "Mono"),
          eqDrawer (state)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &bypass, &scaleButton, &directLevel, &directLength,
                                                           &directDrive, &hero, &roomSwitch, &irSelector, &ambSelector,
                                                           &roomLevel, &roomSize, &roomDrive, &vu, &ambOn, &ambLevel, &mix,
                                                           &autoGain, &trim, &mono, &eqButton })
            addAndMakeVisible (c);

        addChildComponent (eqDrawer);

        scaleButton.getProperties().set ("style", "metal");
        scaleButton.setTooltip ("Window size");

        eqButton.setClickingTogglesState (true);
        eqButton.setTooltip ("Show the EQ for both layers");
        eqButton.onClick = [this] { eqDrawer.setVisible (eqButton.getToggleState()); repaint(); };

        directLength.slider.setTooltip ("How much of the start of the IR the Direct layer uses: the PA's tone and first arrival");
        roomSize.slider.setTooltip ("How long the room rings: 100% is the room as recorded, lower is tighter, higher is a bigger, longer space");
        autoGain.button.setTooltip ("Keeps the wet signal as loud as the input, so A/B comparisons are fair");
        mono.button.setTooltip ("Sum to mono, as on a single-speaker system (the ambience stays stereo)");
        hero.setInterceptsMouseClicks (false, false);
    }

    void resized() override
    {
        using namespace Layout;
        presetBar.setBounds (330, 13, 440, 32);
        scaleButton.setBounds (900, 15, 56, 28);
        bypass.setBounds (980, 14, 104, 30);

        const auto layoutModule = [] (Rectangle<int> panel, Knob& a, Knob& b, Knob& c)
        {
            auto area = panel.reduced (14, 0).withTrimmedTop (44).withTrimmedBottom (10);
            const auto h = area.getHeight() / 3;
            a.setBounds (area.removeFromTop (h).reduced (8, 2));
            b.setBounds (area.removeFromTop (h).reduced (8, 2));
            c.setBounds (area.reduced (8, 2));
        };

        layoutModule (directPanel, directLevel, directLength, directDrive);
        layoutModule (roomPanel, roomLevel, roomSize, roomDrive);

        hero.setBounds (Layout::hero);
        roomSwitch.setBounds (Layout::roomSwitch);
        irSelector.setBounds (Layout::irSelector);
        ambSelector.setBounds (Layout::ambSelector);
        vu.setBounds (Layout::vu);

        // Bottom strip: AMBIENCE | OUTPUT | ADVANCED
        auto strip = bottomPanel.reduced (18, 12).withTrimmedTop (18);
        auto ambience = strip.removeFromLeft (250);
        ambOn.setBounds (ambience.removeFromLeft (80).withSizeKeepingCentre (80, 28));
        ambLevel.setBounds (ambience.removeFromLeft (100));

        strip.removeFromLeft (40);
        auto output = strip.removeFromLeft (560);
        mix.setBounds (output.removeFromLeft (100));
        output.removeFromLeft (14);
        autoGain.setBounds (output.removeFromLeft (130).withSizeKeepingCentre (130, 28).translated (0, -10));
        output.removeFromLeft (10);
        trim.setBounds (output.removeFromLeft (100));
        output.removeFromLeft (20);
        mono.setBounds (output.removeFromLeft (100).withSizeKeepingCentre (100, 28));

        eqButton.setBounds (strip.withSizeKeepingCentre (110, 40).withX (bottomPanel.getRight() - 140));
        eqDrawer.setBounds (Layout::eqDrawer.expanded (4));
    }

    void paint (Graphics& g) override
    {
        using LF = SpacesLookAndFeel;
        LF::drawChassis (g, getLocalBounds().toFloat());

        // Top bar
        LF::drawMetalStrip (g, Layout::topBar.toFloat());

        for (auto x : { 12.0f, 1088.0f })
            LF::drawScrew (g, { x, 29.0f }, 5.0f);

        auto logo = Rectangle<float> (30.0f, 10.0f, 260.0f, 38.0f);
        g.setColour (Palette::chassis.withAlpha (0.9f));
        g.fillRoundedRectangle (logo, 3.0f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (logo, 3.0f, 1.0f);
        g.setColour (Palette::cream);
        g.setFont (LF::font (24.0f, true, 0.42f));
        g.drawText ("SPACES", logo.withTrimmedLeft (16.0f), Justification::centredLeft, false);
        g.setColour (Palette::lamp);
        g.setFont (LF::font (11.0f, false, 0.2f));
        g.drawText ("BY GILLY", logo.withTrimmedRight (14.0f), Justification::centredRight, false);

        // Modules
        LF::drawCreamPanel (g, Layout::directPanel.toFloat());
        LF::drawCreamPanel (g, Layout::roomPanel.toFloat());
        drawModuleTitle (g, Layout::directPanel, "Direct", "The PA");
        drawModuleTitle (g, Layout::roomPanel, "Room", "The space");

        // Under the hero
        LF::drawCreamPanel (g, Layout::hero.toFloat().withY (436.0f).withHeight (104.0f), 6.0f);

        // Bottom strip
        LF::drawCreamPanel (g, Layout::bottomPanel.toFloat());
        auto labels = Layout::bottomPanel.toFloat().reduced (18.0f, 8.0f).withHeight (16.0f);
        LF::drawEngravedText (g, "AMBIENCE", labels.withWidth (250.0f), Justification::centredLeft, LF::font (12.0f, true, 0.22f));
        LF::drawEngravedText (g, "OUTPUT", labels.withX (labels.getX() + 290.0f).withWidth (300.0f), Justification::centredLeft, LF::font (12.0f, true, 0.22f));

        g.setColour (Palette::ink.withAlpha (0.2f));
        for (auto x : { 300.0f, 900.0f })
            g.drawVerticalLine ((int) x, (float) Layout::bottomPanel.getY() + 12.0f, (float) Layout::bottomPanel.getBottom() - 12.0f);

        // Live gain-match readout under the Auto Gain lamp
        g.setColour (Palette::inkSoft);
        g.setFont (LF::font (11.0f));
        g.drawText (gainText, autoGain.getBounds().translated (24, 26).withHeight (14), Justification::centredLeft, false);
    }

    void setGainText (const String& text)
    {
        if (text != gainText)
        {
            gainText = text;
            repaint (autoGain.getBounds().translated (24, 26).withHeight (14).expanded (40, 2));
        }
    }

    // The EQ drawer covers the modules and hero.
    class EqDrawer final : public Component
    {
    public:
        explicit EqDrawer (AudioProcessorValueTreeState& s)
            : directStrip (s, directIDs, "Direct EQ"), roomStrip (s, roomIDs, "Room EQ")
        {
            addAndMakeVisible (directStrip);
            addAndMakeVisible (roomStrip);
        }

        void paint (Graphics& g) override
        {
            g.setColour (Palette::chassis);
            g.fillRect (getLocalBounds());
            SpacesLookAndFeel::drawCreamPanel (g, getLocalBounds().toFloat().reduced (4.0f));
            SpacesLookAndFeel::drawEngravedText (g, String (CharPointer_UTF8 ("ADVANCED  \xc2\xb7  LAYER EQ & BALANCE")), getLocalBounds().toFloat().reduced (24.0f, 14.0f).withHeight (18.0f),
                                                 Justification::centredLeft, SpacesLookAndFeel::font (13.0f, true, 0.24f));
            g.setColour (Palette::ink.withAlpha (0.2f));
            g.drawHorizontalLine (getHeight() / 2 + 14, 24.0f, (float) getWidth() - 24.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (24, 18).withTrimmedTop (30);
            directStrip.setBounds (area.removeFromTop (area.getHeight() / 2).reduced (0, 8));
            roomStrip.setBounds (area.reduced (0, 8));
        }

    private:
        LayerEqStrip directStrip, roomStrip;
    };

    AudioProcessorValueTreeState& state;
    PresetBar presetBar;
    TextButton scaleButton { "100%" };
    LampSwitch bypass;
    Knob directLevel, directLength, directDrive, roomLevel, roomSize, roomDrive;
    HeroView hero;
    RoomSwitch roomSwitch;
    Selector irSelector, ambSelector;
    VuMeter vu;
    LampSwitch ambOn;
    Knob ambLevel, mix;
    LampSwitch autoGain;
    Knob trim;
    LampSwitch mono;
    TextButton eqButton { "EQ" };
    EqDrawer eqDrawer;
    String gainText;

private:
    static void drawModuleTitle (Graphics& g, Rectangle<int> panel, const String& title, const String& subtitle)
    {
        auto area = panel.toFloat().reduced (12.0f, 10.0f).withHeight (32.0f);
        SpacesLookAndFeel::drawEngravedText (g, title.toUpperCase(), area.removeFromTop (18.0f), Justification::centred,
                                             SpacesLookAndFeel::font (15.0f, true, 0.3f));
        SpacesLookAndFeel::drawEngravedText (g, subtitle.toUpperCase(), area, Justification::centred,
                                             SpacesLookAndFeel::font (9.5f, false, 0.25f), Palette::inkSoft);
        g.setColour (Palette::ink.withAlpha (0.25f));
        g.drawHorizontalLine (panel.getY() + 46, (float) panel.getX() + 16.0f, (float) panel.getRight() - 16.0f);
    }
};

//==============================================================================
SpacesEditor::SpacesEditor (SpacesProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    tooltips.setLookAndFeel (&lookAndFeel);

    content = std::make_unique<Content> (p);
    content->setSize (baseWidth, baseHeight);
    addAndMakeVisible (*content);

    content->roomSwitch.onSelect = [this] (int i) { processor.selectRoom (i); refreshSelection(); };
    content->irSelector.onStep = [this] (int d) { processor.stepIr (d); refreshSelection(); };
    content->irSelector.onChoose = [this] (int i) { processor.selectIr (i); refreshSelection(); };
    content->ambSelector.onStep = [this] (int d) { processor.stepAmbience (d); refreshSelection(); };
    content->ambSelector.onChoose = [this] (int i) { processor.selectAmbience (i); refreshSelection(); };
    content->scaleButton.onClick = [this] { showScaleMenu(); };

    setResizable (false, false);
    setScale (processor.uiScale);

    shownSelection = {};
    refreshSelection();
    startTimerHz (15);
}

SpacesEditor::~SpacesEditor()
{
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void SpacesEditor::resized()
{
    content->setTransform (AffineTransform::scale (processor.uiScale));
}

void SpacesEditor::setScale (float scale)
{
    processor.uiScale = scale;
    content->scaleButton.setButtonText (String (roundToInt (scale * 100.0f)) + "%");
    setSize (roundToInt (baseWidth * scale), roundToInt (baseHeight * scale));
    resized();
}

void SpacesEditor::showScaleMenu()
{
    PopupMenu menu;

    for (auto s : { 0.75f, 1.0f, 1.25f, 1.5f })
        menu.addItem (roundToInt (s * 100.0f), String (roundToInt (s * 100.0f)) + "%", true, std::abs (s - processor.uiScale) < 0.01f);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&content->scaleButton),
                        [safe = SafePointer<SpacesEditor> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->setScale ((float) result / 100.0f);
                        });
}

void SpacesEditor::timerCallback()
{
    refreshSelection();

    const auto ambience = processor.isAmbienceAudible();

    if (ambience != shownAmbience)
    {
        shownAmbience = ambience;
        content->hero.setAmbienceActive (ambience);
    }

    const auto autoGainOn = processor.getState().getRawParameterValue (ParamIDs::autoGain)->load() > 0.5f;
    const auto db = processor.getGainMatchDb();
    content->setGainText (autoGainOn ? (db >= 0.0f ? "+" : "") + String (db, 1) + " dB" : String());
}

Array<Image> SpacesEditor::loadHeroFrames (const RoomDef& room)
{
    Array<Image> frames;

    if (room.hero.frameFolder.isDirectory())
    {
        auto files = room.hero.frameFolder.findChildFiles (File::findFiles, false, "*.jpg;*.jpeg;*.png");
        files.sort();

        for (auto& f : files)
            if (auto img = ImageCache::getFromFile (f); img.isValid())
                frames.add (img);
    }

    if (frames.isEmpty())
        if (auto img = ImageCache::getFromFile (room.hero.still); img.isValid())
            frames.add (img);

    return frames;
}

void SpacesEditor::refreshSelection()
{
    const auto sel = processor.getSelection();
    const auto* space = processor.getCurrentSpace();
    const auto* room = processor.getCurrentRoom();

    if (space == nullptr || room == nullptr)
    {
        content->hero.setCaption ("No spaces installed", "Reinstall Spaces, or add packs to " + PackLibrary::getUserPacksFolder().getFullPathName());
        return;
    }

    if (sel == shownSelection)
        return;

    const auto roomChanged = sel.space != shownSelection.space || sel.room != shownSelection.room;
    shownSelection = sel;

    StringArray roomNames, irNames, ambNames;

    for (auto& r : space->rooms)       roomNames.add (r.name);
    for (auto& i : room->irs)          irNames.add (i.name);
    for (auto& a : room->ambiences)    ambNames.add (a.name);

    content->roomSwitch.setRooms (roomNames, sel.room);
    content->irSelector.setItems (irNames, sel.ir);
    content->ambSelector.setItems (ambNames, sel.ambience);

    if (roomChanged)
    {
        const auto frames = loadHeroFrames (*room);

        if (frames.size() > 1)
            content->hero.setFrames (frames, room->hero.fps);
        else
            content->hero.setStill (frames.isEmpty() ? Image() : frames.getFirst());

        content->hero.setCaption (space->name + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + room->name, space->location);
    }
}
