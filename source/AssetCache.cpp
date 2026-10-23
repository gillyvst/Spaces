#include "AssetCache.h"

using namespace juce;

AssetCache::AssetCache()
{
    formats.registerBasicFormats();
}

static std::shared_ptr<AudioFileData> readAudio (AudioFormatManager& formats, const File& file)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
        return {};

    auto data = std::make_shared<AudioFileData>();
    data->sampleRate = reader->sampleRate;

    const auto numSamples = (int) reader->lengthInSamples;
    data->buffer.setSize (2, numSamples);
    reader->read (&data->buffer, 0, numSamples, 0, true, true);

    if (reader->numChannels == 1)
        data->buffer.copyFrom (1, 0, data->buffer, 0, 0, numSamples);

    return data;
}

std::shared_ptr<const AudioFileData> AssetCache::getAudio (const File& file)
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto key = file.getFullPathName();

    if (auto existing = audio[key].lock())
        return existing;

    std::shared_ptr<const AudioFileData> data = readAudio (formats, file);
    audio[key] = data;
    return data;
}

std::shared_ptr<const AudioFileData> AssetCache::getLoop (const File& file, double targetRate)
{
    const auto key = file.getFullPathName() + "@" + String (targetRate);

    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (auto existing = loops[key].lock())
            return existing;
    }

    auto source = getAudio (file);

    if (source == nullptr)
        return {};

    // Drop a duplicated closing frame.
    auto numSamples = source->buffer.getNumSamples();

    if (numSamples > 1)
    {
        const auto& b = source->buffer;
        const auto last = numSamples - 1;

        if (std::abs (b.getSample (0, 0) - b.getSample (0, last)) < 1.0e-6f
            && std::abs (b.getSample (1, 0) - b.getSample (1, last)) < 1.0e-6f)
            --numSamples;
    }

    AudioBuffer<float> trimmed (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        trimmed.copyFrom (ch, 0, source->buffer, ch, 0, numSamples);

    auto result = std::make_shared<AudioFileData>();
    result->sampleRate = targetRate;
    result->buffer = std::abs (targetRate - source->sampleRate) < 0.5
                       ? std::move (trimmed)
                       : Resampling::resampleLoop (trimmed, targetRate / source->sampleRate);

    const std::lock_guard<std::mutex> lock (mutex);

    if (auto existing = loops[key].lock())   // another instance got there first
        return existing;

    loops[key] = result;
    return result;
}

//==============================================================================
namespace Resampling
{
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;

            if (term < 1.0e-12 * sum)
                break;
        }

        return sum;
    }

    AudioBuffer<float> resampleLoop (const AudioBuffer<float>& input, double ratio)
    {
        const int inLength = input.getNumSamples();
        const int outLength = jmax (1, (int) std::llround (inLength * ratio));

        // Use the exact ratio that makes the output a whole number of samples,
        // so the loop length (and therefore the seam) stays exact.
        const double step = (double) inLength / (double) outLength;   // input samples per output sample
        const double cutoff = 0.94 * jmin (1.0, 1.0 / step);           // relative to input Nyquist

        constexpr int zeroCrossings = 24;
        constexpr int tableResolution = 512;
        constexpr double kaiserBeta = 9.0;

        const double halfWidth = zeroCrossings / cutoff;               // in input samples
        const int tableSize = (int) std::ceil (halfWidth * tableResolution) + 2;

        std::vector<float> table ((size_t) tableSize);
        const double i0Beta = besselI0 (kaiserBeta);

        for (int i = 0; i < tableSize; ++i)
        {
            const double t = (double) i / tableResolution;

            if (t >= halfWidth)
            {
                table[(size_t) i] = 0.0f;
                continue;
            }

            const double x = MathConstants<double>::pi * cutoff * t;
            const double sinc = t == 0.0 ? 1.0 : std::sin (x) / x;
            const double r = t / halfWidth;
            const double window = besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / i0Beta;
            table[(size_t) i] = (float) (cutoff * sinc * window);
        }

        const auto kernel = [&table, tableSize] (double t)
        {
            const double pos = std::abs (t) * tableResolution;
            const int i = (int) pos;

            if (i + 1 >= tableSize)
                return 0.0f;

            const float frac = (float) (pos - i);
            return table[(size_t) i] + frac * (table[(size_t) i + 1] - table[(size_t) i]);
        };

        // Polyphase table: the taps for each of `phases` fractional positions.
        const int reach = (int) std::ceil (halfWidth);
        const int count = 2 * reach;
        constexpr int phases = 4096;
        std::vector<float> polyphase ((size_t) (phases + 1) * (size_t) count);

        for (int ph = 0; ph <= phases; ++ph)
            for (int k = 0; k < count; ++k)
                polyphase[(size_t) ph * (size_t) count + (size_t) k] = kernel ((double) ph / phases + reach - 1 - k);

        AudioBuffer<float> output (input.getNumChannels(), outLength);

        for (int ch = 0; ch < input.getNumChannels(); ++ch)
        {
            const float* in = input.getReadPointer (ch);
            float* out = output.getWritePointer (ch);

            for (int j = 0; j < outLength; ++j)
            {
                const double pos = j * step;
                const int centre = (int) std::floor (pos);
                const int first = centre - reach + 1;
                const auto phase = (int) std::lround ((pos - centre) * phases);
                const float* w = polyphase.data() + (size_t) phase * (size_t) count;
                float sum = 0.0f;

                if (first >= 0 && first + count <= inLength)
                {
                    const float* x = in + first;

                    for (int k = 0; k < count; ++k)
                        sum += x[k] * w[k];
                }
                else
                {
                    for (int k = 0; k < count; ++k)
                    {
                        int index = (first + k) % inLength;
                        if (index < 0)
                            index += inLength;

                        sum += in[index] * w[k];
                    }
                }

                out[j] = sum;
            }
        }

        return output;
    }
}
