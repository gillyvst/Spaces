#pragma once

#include "../AssetCache.h"
#include "Mailbox.h"

// One ambience loop, ready for playback at the current sample rate.
struct AmbienceLoop
{
    std::shared_ptr<const AudioFileData> audio;
    float gain = 1.0f;   // the manifest's per-file trim
};

// Plays the room's ambience loop continuously from its own sample counter,
// independent of the host transport. Loops are pre-trimmed, so they wrap
// sample-accurately with no crossfade. Switching to a different loop
// crossfades between the two.
class AmbiencePlayer
{
public:
    ~AmbiencePlayer()
    {
        delete current;
        delete next;
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        level.reset (sampleRate, 0.05);
        level.setCurrentAndTargetValue (level.getTargetValue());
    }

    // Non-realtime: queue a new loop.
    void setLoop (std::unique_ptr<AmbienceLoop> loop) { mailbox.post (std::move (loop)); }
    void collectGarbage()                             { mailbox.collectGarbage(); }

    void setLevel (float gain) noexcept { level.setTargetValue (gain); }

    // Adds the ambience into out (2 channels).
    void process (juce::AudioBuffer<float>& out, int numSamples) noexcept
    {
        if (next == nullptr)
        {
            if (auto* incoming = mailbox.fetch())
            {
                if (current == nullptr)
                {
                    current = incoming;
                    position = 0;
                }
                else
                {
                    next = incoming;
                    nextPosition = position % length (next);
                    fadePosition = 0;
                    fadeLength = juce::jmax (1, (int) (sampleRate * crossfadeSeconds));
                }
            }
        }

        if (current == nullptr)
            return;

        const bool silent = level.getTargetValue() == 0.0f && ! level.isSmoothing();

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = level.getNextValue();
            float left, right;
            read (*current, position, left, right);

            if (next != nullptr)
            {
                float nl, nr;
                read (*next, nextPosition, nl, nr);

                // Equal-power crossfade: the loops are unrelated recordings.
                const float t = (float) fadePosition / (float) fadeLength;
                const float a = std::cos (t * juce::MathConstants<float>::halfPi);
                const float b = std::sin (t * juce::MathConstants<float>::halfPi);
                left = left * a + nl * b;
                right = right * a + nr * b;

                if (++fadePosition >= fadeLength)
                {
                    mailbox.retire (current);
                    current = next;
                    position = nextPosition;
                    next = nullptr;
                }
            }

            if (! silent)
            {
                out.addSample (0, i, left * g);
                out.addSample (1, i, right * g);
            }
        }
    }

    bool hasLoop() const noexcept { return current != nullptr; }

private:
    static int length (const AmbienceLoop* loop) noexcept { return loop->audio->buffer.getNumSamples(); }

    // Reads one frame and advances the read position, wrapping at the loop end.
    static void read (const AmbienceLoop& loop, int& pos, float& left, float& right) noexcept
    {
        const auto& b = loop.audio->buffer;
        left = b.getSample (0, pos) * loop.gain;
        right = b.getSample (1, pos) * loop.gain;

        if (++pos >= b.getNumSamples())
            pos = 0;
    }

    static constexpr double crossfadeSeconds = 0.3;

    Mailbox<AmbienceLoop> mailbox;
    AmbienceLoop* current = nullptr;
    AmbienceLoop* next = nullptr;
    int position = 0, nextPosition = 0, fadePosition = 0, fadeLength = 1;
    double sampleRate = 44100.0;
    juce::SmoothedValue<float> level;
};
