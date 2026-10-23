#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// RBJ-cookbook biquad, transposed direct form II, double-precision state.
// Coefficients can be changed while running without clicks as long as they
// move gradually, which LayerEQ ensures by smoothing the parameters.
struct Biquad
{
    enum class Shape { lowShelf, peak, highShelf, highPass };

    void setCoefficients (Shape shape, double sampleRate, double freq, double gainDb, double q) noexcept
    {
        freq = juce::jlimit (10.0, sampleRate * 0.49, freq);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * freq / sampleRate;
        const double cosw = std::cos (w0), sinw = std::sin (w0);

        double b0, b1, b2, a0, a1, a2;

        if (shape == Shape::highPass)
        {
            const double alpha = sinw / (2.0 * q);
            b0 = (1.0 + cosw) / 2.0;  b1 = -(1.0 + cosw);  b2 = (1.0 + cosw) / 2.0;
            a0 = 1.0 + alpha;         a1 = -2.0 * cosw;    a2 = 1.0 - alpha;
        }
        else if (shape == Shape::peak)
        {
            const double alpha = sinw / (2.0 * q);
            b0 = 1.0 + alpha * A;  b1 = -2.0 * cosw;  b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;  a1 = -2.0 * cosw;  a2 = 1.0 - alpha / A;
        }
        else
        {
            // Shelf slope S = 1 (no resonant bump).
            const double alpha = sinw / 2.0 * std::sqrt (2.0);
            const double sqA2alpha = 2.0 * std::sqrt (A) * alpha;

            if (shape == Shape::lowShelf)
            {
                b0 =        A * ((A + 1) - (A - 1) * cosw + sqA2alpha);
                b1 =  2.0 * A * ((A - 1) - (A + 1) * cosw);
                b2 =        A * ((A + 1) - (A - 1) * cosw - sqA2alpha);
                a0 =             (A + 1) + (A - 1) * cosw + sqA2alpha;
                a1 = -2.0 *     ((A - 1) + (A + 1) * cosw);
                a2 =             (A + 1) + (A - 1) * cosw - sqA2alpha;
            }
            else
            {
                b0 =        A * ((A + 1) + (A - 1) * cosw + sqA2alpha);
                b1 = -2.0 * A * ((A - 1) + (A + 1) * cosw);
                b2 =        A * ((A + 1) + (A - 1) * cosw - sqA2alpha);
                a0 =             (A + 1) - (A - 1) * cosw + sqA2alpha;
                a1 =  2.0 *     ((A - 1) - (A + 1) * cosw);
                a2 =             (A + 1) - (A - 1) * cosw - sqA2alpha;
            }
        }

        c = { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    // Unity filter: used before the first coefficient update.
    void setIdentity() noexcept { c = { 1.0, 0.0, 0.0, 0.0, 0.0 }; }

    inline float process (float x, int channel) noexcept
    {
        auto& s = state[(size_t) channel];
        const double in = x;
        const double out = c[0] * in + s[0];
        s[0] = c[1] * in - c[3] * out + s[1];
        s[1] = c[2] * in - c[4] * out;
        return (float) out;
    }

    void reset() noexcept { state = {}; }

    std::array<double, 5> c { 1.0, 0.0, 0.0, 0.0, 0.0 };
    std::array<std::array<double, 2>, 2> state {};
};

// Four-band EQ (low shelf, two bells, high shelf) for one convolution layer.
class LayerEQ
{
public:
    struct Settings
    {
        float lsFreq = 100.0f,  lsGain = 0.0f;
        float b1Freq = 250.0f,  b1Gain = 0.0f, b1Q = 0.7f;
        float b2Freq = 2500.0f, b2Gain = 0.0f, b2Q = 0.7f;
        float hsFreq = 8000.0f, hsGain = 0.0f;
    };

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        for (auto& s : freqs) s.reset (sampleRate, smoothingSeconds);
        for (auto& s : gains) s.reset (sampleRate, smoothingSeconds);
        for (auto& s : qs)    s.reset (sampleRate, smoothingSeconds);

        initialised = false;
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : bands)
            b.reset();
    }

    void setTargets (const Settings& s) noexcept
    {
        const float f[] { s.lsFreq, s.b1Freq, s.b2Freq, s.hsFreq };
        const float g[] { s.lsGain, s.b1Gain, s.b2Gain, s.hsGain };
        const float q[] { 0.707f,   s.b1Q,    s.b2Q,    0.707f };

        for (size_t i = 0; i < 4; ++i)
        {
            if (initialised)
            {
                freqs[i].setTargetValue (f[i]);
                gains[i].setTargetValue (g[i]);
                qs[i].setTargetValue (q[i]);
            }
            else
            {
                freqs[i].setCurrentAndTargetValue (f[i]);
                gains[i].setCurrentAndTargetValue (g[i]);
                qs[i].setCurrentAndTargetValue (q[i]);
            }
        }

        if (! initialised)
        {
            updateCoefficients (0);
            initialised = true;
        }
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        const auto numChannels = juce::jmin (2, buffer.getNumChannels());

        for (int start = 0; start < numSamples; start += updateInterval)
        {
            const int n = juce::jmin (updateInterval, numSamples - start);

            if (isSmoothing())
                updateCoefficients (n);

            if (isFlat())
                continue;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch, start);

                for (auto& band : bands)
                    for (int i = 0; i < n; ++i)
                        data[i] = band.process (data[i], ch);
            }
        }
    }

private:
    bool isSmoothing() const noexcept
    {
        for (size_t i = 0; i < 4; ++i)
            if (freqs[i].isSmoothing() || gains[i].isSmoothing() || qs[i].isSmoothing())
                return true;
        return false;
    }

    // All gains at exactly 0 dB: every band is an identity filter, so skip
    // the work. The filters keep running for a few more sub-blocks after the
    // gains land on 0 dB so their state has rung out, then the state is
    // cleared so re-enabling starts clean (the gain then ramps up from 0 dB,
    // so that can't click either).
    bool isFlat() noexcept
    {
        for (auto& g : gains)
            if (g.getTargetValue() != 0.0f || g.isSmoothing())
            {
                flatCountdown = 8;
                return false;
            }

        if (flatCountdown > 0)
        {
            if (--flatCountdown == 0)
                reset();

            return false;
        }

        return true;
    }

    void updateCoefficients (int samplesToSkip) noexcept
    {
        static constexpr Biquad::Shape shapes[] { Biquad::Shape::lowShelf, Biquad::Shape::peak,
                                                  Biquad::Shape::peak,     Biquad::Shape::highShelf };

        for (size_t i = 0; i < 4; ++i)
        {
            const auto f = freqs[i].skip (samplesToSkip);
            const auto g = gains[i].skip (samplesToSkip);
            const auto q = qs[i].skip (samplesToSkip);
            bands[i].setCoefficients (shapes[i], sampleRate, f, g, q);
        }
    }

    static constexpr int updateInterval = 16;
    static constexpr double smoothingSeconds = 0.05;

    double sampleRate = 44100.0;
    std::array<Biquad, 4> bands;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, 4> freqs, qs;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, 4> gains;
    bool initialised = false;
    int flatCountdown = 0;
};
