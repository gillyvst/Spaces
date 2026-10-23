#pragma once

#include "LayerEQ.h"
#include <juce_audio_basics/juce_audio_basics.h>

// Slow loudness matching of the wet signal to the input.
//
// Both signals are K-weighted (BS.1770 pre-filter) and their mean square is
// taken over a sliding ~3 s window, built from 100 ms segments. The dB
// difference becomes a gain for the wet signal, smoothed with a ~1.5 s time
// constant and limited to +/-18 dB. While the input is below -50 dBFS the
// correction is frozen, so it doesn't swell up in silence or between songs.
class GainMatch
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        segmentLength = juce::jmax (1, (int) std::round (sampleRate * segmentSeconds));

        for (auto* w : { &inputWeighting, &wetWeighting })
        {
            (*w)[0].setCoefficients (Biquad::Shape::highShelf, sampleRate, 1681.0, 4.0, 0.707);
            (*w)[1].setCoefficients (Biquad::Shape::highPass, sampleRate, 38.0, 0.0, 0.5);
        }

        reset();
    }

    void reset()
    {
        for (auto* w : { &inputWeighting, &wetWeighting })
            for (auto& f : *w)
                f.reset();

        segments.fill ({});
        segmentIndex = 0;
        segmentsFilled = 0;
        segmentPosition = 0;
        currentSegment = {};
        correctionDb = 0.0;
        validSeconds = 0.0;
        gain.reset (sampleRate, 0.02);
        gain.setCurrentAndTargetValue (1.0f);
    }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    // Feed the dry input and the wet signal (before correction), then apply
    // the correction to the wet buffer in place.
    void process (const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& wet, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            double inSq = 0.0, wetSq = 0.0;

            for (int ch = 0; ch < 2; ++ch)
            {
                auto x = input.getSample (ch, i);
                auto y = wet.getSample (ch, i);

                for (auto& f : inputWeighting) x = f.process (x, ch);
                for (auto& f : wetWeighting)   y = f.process (y, ch);

                inSq += (double) x * x;
                wetSq += (double) y * y;
            }

            currentSegment.input += inSq;
            currentSegment.wet += wetSq;

            if (++segmentPosition >= segmentLength)
                finishSegment();
        }

        // Ease towards the target once per block.
        const double blockSeconds = numSamples / sampleRate;

        if (! enabled)
        {
            correctionDb *= std::exp (-blockSeconds / 0.15);
        }
        else if (hasValidTarget)
        {
            // Settle faster for the first few seconds after a (re)start.
            validSeconds += blockSeconds;
            const auto tau = validSeconds < 3.0 ? 0.4 : timeConstant;
            correctionDb += (targetDb - correctionDb) * (1.0 - std::exp (-blockSeconds / tau));
        }

        gain.setTargetValue (juce::Decibels::decibelsToGain ((float) correctionDb));

        for (int ch = 0; ch < 2; ++ch)
        {
            auto copy = gain;
            copy.applyGain (wet.getWritePointer (ch), numSamples);
        }

        gain.skip (numSamples);
    }

    float getCorrectionDb() const noexcept { return (float) correctionDb; }

private:
    struct Segment { double input = 0.0, wet = 0.0; };

    void finishSegment() noexcept
    {
        segments[(size_t) segmentIndex] = currentSegment;
        segmentIndex = (segmentIndex + 1) % numSegments;
        segmentsFilled = juce::jmin (segmentsFilled + 1, numSegments);
        currentSegment = {};
        segmentPosition = 0;

        double inSum = 0.0, wetSum = 0.0;

        for (int i = 0; i < segmentsFilled; ++i)
        {
            inSum += segments[(size_t) i].input;
            wetSum += segments[(size_t) i].wet;
        }

        const double count = (double) segmentsFilled * segmentLength * 2.0;
        const double inDb = 10.0 * std::log10 (inSum / count + 1.0e-20);
        const double wetDb = 10.0 * std::log10 (wetSum / count + 1.0e-20);

        // Freeze when the input is quiet, or before there's ~1 s of data.
        hasValidTarget = segmentsFilled >= 10 && inDb > freezeBelowDb && wetDb > -100.0;

        if (hasValidTarget)
            targetDb = juce::jlimit (-maxCorrectionDb, maxCorrectionDb, inDb - wetDb);
    }

    static constexpr double segmentSeconds = 0.1;
    static constexpr int numSegments = 30;           // 3 s window
    static constexpr double timeConstant = 1.5;
    static constexpr double maxCorrectionDb = 18.0;
    static constexpr double freezeBelowDb = -50.0;

    double sampleRate = 44100.0;
    std::array<Biquad, 2> inputWeighting, wetWeighting;
    std::array<Segment, numSegments> segments {};
    Segment currentSegment;
    int segmentLength = 4410, segmentIndex = 0, segmentsFilled = 0, segmentPosition = 0;
    double correctionDb = 0.0, targetDb = 0.0, validSeconds = 0.0;
    bool enabled = true, hasValidTarget = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain;
};
