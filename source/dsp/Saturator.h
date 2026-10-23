#pragma once

#include <juce_dsp/juce_dsp.h>

// Soft tanh saturation at 2x oversampling.
//
// Level compensation: the curve is scaled so a signal around -18 dBFS comes
// out at the same level whatever the drive, so turning Drive up mostly adds
// harmonics (and squashes peaks) rather than making things louder.
//
// Drive 0 is a clean bypass: the shaped signal is blended in over the first
// 10% of the range, so at 0 the output is the input (through the oversampling
// filters, which always run so the wet path's delay never changes).
class Saturator
{
public:
    Saturator() : oversampling (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false) {}

    void prepare (double sampleRate, int maxBlockSize)
    {
        oversampling.initProcessing ((size_t) maxBlockSize);
        oversampling.reset();
        drive.reset (sampleRate * 2.0, 0.05);
        drive.setCurrentAndTargetValue (drive.getTargetValue());
    }

    void reset() { oversampling.reset(); }

    void setDrive (float amount01) noexcept { drive.setTargetValue (juce::jlimit (0.0f, 1.0f, amount01)); }

    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
        auto up = oversampling.processSamplesUp (block);
        const auto upSamples = (int) up.getNumSamples();

        if (drive.getTargetValue() > 0.0f || drive.isSmoothing())
        {
            auto* left = up.getChannelPointer (0);
            auto* right = up.getChannelPointer (1);

            for (int i = 0; i < upSamples; ++i)
            {
                const auto d = drive.getNextValue();
                left[i]  = shape (left[i], d);
                right[i] = shape (right[i], d);
            }
        }

        oversampling.processSamplesDown (block);
    }

private:
    static float shape (float x, float amount) noexcept
    {
        constexpr float reference = 0.125f;   // -18 dBFS
        const float pre = std::exp2 (amount * 4.0f);   // up to +24 dB into the curve
        const float makeup = reference / std::tanh (pre * reference);
        const float shaped = std::tanh (pre * x) * makeup;
        const float blend = juce::jmin (1.0f, amount * 10.0f);
        return x + blend * (shaped - x);
    }

    juce::dsp::Oversampling<float> oversampling;
    juce::SmoothedValue<float> drive;
};
