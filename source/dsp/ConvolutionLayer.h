#pragma once

#include "LayerEQ.h"
#include "Saturator.h"
#include "WarmConvolution.h"

// The impulse responses for one layer, as four paths packed into two stereo
// buffers: fromLeft = { L->L, L->R }, fromRight = { R->L, R->R }.
struct LayerIR
{
    juce::AudioBuffer<float> fromLeft, fromRight;
    double sampleRate = 44100.0;
};

// One convolution layer: true-stereo convolution -> drive -> EQ -> level.
//
// The convolver, given a stereo IR, convolves channel n with IR channel
// n, so each input side is duplicated into a stereo buffer and run through
// its own convolver; the two results are summed. That gives all four paths.
// Convolution uses non-uniform partitioning, which is zero latency. A new IR
// is warmed up on the live input and then crossfaded in (see
// WarmConvolution.cpp), so switching is click-free.
class ConvolutionLayer
{
public:
    explicit ConvolutionLayer (spaces_dsp::ConvolutionMessageQueue& queue)
        : fromLeft (spaces_dsp::Convolution::NonUniform { 512 }, queue),
          fromRight (spaces_dsp::Convolution::NonUniform { 512 }, queue) {}

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
        fromLeft.prepare (spec);
        fromRight.prepare (spec);
        scratchLeft.setSize (2, maxBlockSize, false, false, true);
        scratchRight.setSize (2, maxBlockSize, false, false, true);
        saturator.prepare (sampleRate, maxBlockSize);
        eq.prepare (sampleRate);
        level.reset (sampleRate, 0.05);
        level.setCurrentAndTargetValue (level.getTargetValue());
        // prepare() rebuilds the engines from the last IR synchronously, so a
        // layer that already had an IR stays live.
        readyGate.reset (sampleRate, 0.05);
        readyGate.setCurrentAndTargetValue (readyGate.getTargetValue());
        samplesSinceLoad = 0;
    }

    void reset()
    {
        fromLeft.reset();
        fromRight.reset();
        saturator.reset();
        eq.reset();
    }

    // Audio thread. Takes the buffers out of ir (moving an AudioBuffer doesn't allocate).
    void loadIR (LayerIR& ir) noexcept
    {
        using C = spaces_dsp::Convolution;
        fromLeft.loadImpulseResponse (std::move (ir.fromLeft), ir.sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        fromRight.loadImpulseResponse (std::move (ir.fromRight), ir.sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        hasIR = true;
    }

    void setLevel (float gain) noexcept          { level.setTargetValue (gain); }
    void setDrive (float amount01) noexcept      { saturator.setDrive (amount01); }
    void setEQ (const LayerEQ::Settings& s)      { eq.setTargets (s); }

    // Convolves input (2 channels) and adds the layer's output into wet.
    void process (const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& wet, int numSamples) noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            scratchLeft.copyFrom (ch, 0, input, 0, 0, numSamples);
            scratchRight.copyFrom (ch, 0, input, 1, 0, numSamples);
        }

        convolve (fromLeft, scratchLeft, numSamples);
        convolve (fromRight, scratchRight, numSamples);

        for (int ch = 0; ch < 2; ++ch)
            scratchLeft.addFrom (ch, 0, scratchRight, ch, 0, numSamples);

        updateReadyGate (numSamples);

        if (readyGate.getTargetValue() == 0.0f && ! readyGate.isSmoothing())
            return;   // nothing loaded yet

        saturator.process (scratchLeft, numSamples);
        eq.process (scratchLeft, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto g = level.getNextValue() * readyGate.getNextValue();
            wet.addSample (0, i, scratchLeft.getSample (0, i) * g);
            wet.addSample (1, i, scratchLeft.getSample (1, i) * g);
        }
    }

private:
    static void convolve (spaces_dsp::Convolution& conv, juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // Before the first IR arrives the convolvers hold a unit impulse (they
    // would pass the input straight through), so the layer stays muted until
    // a real IR has been installed and its 100 ms crossfade has finished.
    void updateReadyGate (int numSamples) noexcept
    {
        if (! hasIR || readyGate.getTargetValue() > 0.0f)
            return;

        if (fromLeft.getCurrentIRSize() > 1 && fromRight.getCurrentIRSize() > 1)
        {
            samplesSinceLoad += numSamples;

            if (samplesSinceLoad > (int) (sampleRate * 0.11))
                readyGate.setTargetValue (1.0f);
        }
    }

    spaces_dsp::Convolution fromLeft, fromRight;
    juce::AudioBuffer<float> scratchLeft, scratchRight;
    Saturator saturator;
    LayerEQ eq;
    juce::SmoothedValue<float> level, readyGate;
    double sampleRate = 44100.0;
    int samplesSinceLoad = 0;
    bool hasIR = false;
};
