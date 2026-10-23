#include "HeroView.h"
#include "SpacesLookAndFeel.h"

using namespace juce;

HeroView::HeroView()
{
    setOpaque (true);

    // Film grain, generated once and tiled over the photo.
    grain = Image (Image::ARGB, 256, 256, true);
    Random random (7);
    Image::BitmapData data (grain, Image::BitmapData::writeOnly);

    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
        {
            const auto v = (uint8) random.nextInt (256);
            data.setPixelColour (x, y, Colour (v, v, v, (uint8) 14));
        }
}

void HeroView::setStill (const Image& image)
{
    Array<Image> one;
    one.add (image);
    setFrames (one, 0.0);
}

void HeroView::setFrames (Array<Image> newFrames, double framesPerSecond)
{
    previous = currentFrame();
    frames = std::move (newFrames);
    fps = framesPerSecond;
    frameIndex = 0;
    frameClock = 0.0;
    startTransition();
}

void HeroView::setCaption (const String& newTitle, const String& newSubtitle)
{
    title = newTitle;
    subtitle = newSubtitle;
    repaint();
}

void HeroView::setAmbienceActive (bool active)
{
    if (ambienceActive != active)
    {
        ambienceActive = active;
        lastTick = Time::getMillisecondCounterHiRes();

        if (active && frames.size() > 1)
            startTimerHz (60);

        repaint();
    }
}

const Image& HeroView::currentFrame() const
{
    static const Image empty;
    return frames.isEmpty() ? empty : frames.getReference (frameIndex % frames.size());
}

void HeroView::startTransition()
{
    transition = previous.isValid() ? 0.0f : 1.0f;
    lastTick = Time::getMillisecondCounterHiRes();
    startTimerHz (60);
    repaint();
}

void HeroView::timerCallback()
{
    const auto now = Time::getMillisecondCounterHiRes();
    const auto dt = (now - lastTick) * 0.001;
    lastTick = now;

    bool busy = false;

    if (transition < 1.0f)
    {
        transition = jmin (1.0f, transition + (float) (dt / 0.45));
        busy = true;

        if (transition >= 1.0f)
            previous = {};
    }

    if (frames.size() > 1 && fps > 0.0 && ambienceActive)
    {
        frameClock += dt * fps;
        const auto advance = (int) frameClock;

        if (advance > 0)
        {
            frameIndex = (frameIndex + advance) % frames.size();
            frameClock -= advance;
        }

        busy = true;
    }

    if (! busy)
        stopTimer();

    repaint();
}

void HeroView::resized() {}

void HeroView::drawPhoto (Graphics& g, const Image& image, Rectangle<float> area, float alpha) const
{
    if (! image.isValid())
        return;

    // Cover-fit: fill the frame, cropping the longer side.
    const auto scale = jmax (area.getWidth() / (float) image.getWidth(), area.getHeight() / (float) image.getHeight());
    const auto w = (float) image.getWidth() * scale, h = (float) image.getHeight() * scale;
    const auto dest = Rectangle<float> (w, h).withCentre (area.getCentre());

    g.setOpacity (alpha);
    g.drawImage (image, dest, RectanglePlacement::stretchToFit);
    g.setOpacity (1.0f);
}

void HeroView::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto bezel = 8.0f;
    const auto photo = bounds.reduced (bezel);

    // Brass bezel
    g.setGradientFill (ColourGradient (Palette::brass.brighter (0.3f), bounds.getX(), bounds.getY(),
                                       Palette::brassDark, bounds.getRight(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);
    g.setColour (Colours::white.withAlpha (0.3f));
    g.drawRoundedRectangle (bounds.reduced (1.5f), 6.0f, 1.0f);

    // Photo
    g.saveState();
    Path clip;
    clip.addRoundedRectangle (photo, 3.0f);
    g.reduceClipRegion (clip);
    g.setColour (Colours::black);
    g.fillRect (photo);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);

    if (previous.isValid() && transition < 1.0f)
        drawPhoto (g, previous, photo, 1.0f);

    drawPhoto (g, currentFrame(), photo, transition < 1.0f ? transition * transition * (3.0f - 2.0f * transition) : 1.0f);

    // Warm grade, grain and vignette: makes a modest-resolution photo feel
    // like an old print rather than a soft JPEG.
    g.setColour (Colour (0xff3a1c08).withAlpha (0.16f));
    g.fillRect (photo);
    g.setTiledImageFill (grain, 0, 0, 1.0f);
    g.fillRect (photo);

    ColourGradient vignette (Colours::transparentBlack, photo.getCentreX(), photo.getCentreY(),
                             Colours::black.withAlpha (0.7f), photo.getX(), photo.getY(), true);
    vignette.addColour (0.55, Colours::transparentBlack);
    g.setGradientFill (vignette);
    g.fillRect (photo);

    if (! ambienceActive)
    {
        // The room is "paused": dim it slightly.
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRect (photo);
    }

    // Caption plate
    if (title.isNotEmpty())
    {
        const auto titleFont = SpacesLookAndFeel::font (15.0f, true, 0.2f);
        const auto subFont = SpacesLookAndFeel::font (11.0f, false, 0.25f);
        const auto width = jmax (GlyphArrangement::getStringWidth (titleFont, title.toUpperCase()),
                                 GlyphArrangement::getStringWidth (subFont, subtitle.toUpperCase())) + 32.0f;
        const auto plate = Rectangle<float> (photo.getX() + 14.0f, photo.getBottom() - 58.0f, width, 44.0f);

        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (Palette::brass.withAlpha (0.7f));
        g.drawRoundedRectangle (plate, 3.0f, 1.0f);
        g.setColour (Palette::cream);
        g.setFont (titleFont);
        g.drawText (title.toUpperCase(), plate.withTrimmedLeft (16.0f).withHeight (26.0f).translated (0.0f, 4.0f), Justification::centredLeft);
        g.setColour (Palette::lamp.withAlpha (0.9f));
        g.setFont (subFont);
        g.drawText (subtitle.toUpperCase(), plate.withTrimmedLeft (16.0f).withTrimmedTop (24.0f).withHeight (16.0f), Justification::centredLeft);
    }

    g.restoreState();

    // Inner shadow at the top edge of the glass
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.5f), photo.getX(), photo.getY(),
                                       Colours::transparentBlack, photo.getX(), photo.getY() + 14.0f, false));
    g.fillRect (photo.withHeight (14.0f));
}
