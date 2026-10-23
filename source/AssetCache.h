#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>
#include <mutex>

struct AudioFileData
{
    juce::AudioBuffer<float> buffer;   // always 2 channels
    double sampleRate = 44100.0;
};

// Decoded audio shared between all plugin instances in the process, so
// several instances of Spaces don't each hold their own copy of the
// ambience loops. Entries are dropped once no instance uses them.
// Never call from the audio thread: it reads files and allocates.
class AssetCache
{
public:
    AssetCache();

    // A file decoded to stereo float at its native sample rate.
    std::shared_ptr<const AudioFileData> getAudio (const juce::File& file);

    // A seamless loop resampled to targetRate. If the file's last frame repeats
    // its first one (a common export artefact) it is dropped, so the loop
    // doesn't stutter by one sample at the seam.
    std::shared_ptr<const AudioFileData> getLoop (const juce::File& file, double targetRate);

private:
    std::mutex mutex;
    juce::AudioFormatManager formats;
    std::map<juce::String, std::weak_ptr<const AudioFileData>> audio, loops;
};

namespace Resampling
{
    // Band-limited (windowed-sinc) resampling that treats the input as
    // circular, so a perfect loop stays a perfect loop at the new rate.
    juce::AudioBuffer<float> resampleLoop (const juce::AudioBuffer<float>& input, double ratio);
}
