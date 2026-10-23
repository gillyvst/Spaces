#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <complex>

using namespace juce;

//==============================================================================
// Builds IRs or loads ambience loops off the audio thread. There are two of
// these, one for each job, so a slow ambience load (resampling an 80 s loop)
// never holds up an IR switch. Requests are coalesced: only the newest one is
// processed, so dragging Length or flicking through presets never queues up
// a backlog of work.
class SpacesProcessor::Loader final : private Thread
{
public:
    enum class Job { impulseResponses, ambience };

    Loader (SpacesProcessor& p, Job j)
        : Thread (j == Job::ambience ? "Spaces ambience loader" : "Spaces IR loader"), owner (p), job (j)
    {
        startThread (Priority::normal);
    }

    std::atomic<int> loadedGeneration { 0 };

    ~Loader() override { stopThread (5000); }

    void request (const LoadRequest& r)
    {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            pending = r;
            hasPending = true;
        }

        notify();
    }

private:
    struct Source
    {
        AudioBuffer<float> fromLeft, fromRight;   // full length, calibrated
        double sampleRate = 44100.0;
    };

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (-1);

            for (;;)
            {
                LoadRequest r;

                {
                    const std::lock_guard<std::mutex> lock (mutex);

                    if (! hasPending)
                        break;

                    r = pending;
                    hasPending = false;
                }

                if (job == Job::impulseResponses)
                    buildImpulseResponses (r);
                else
                    loadAmbience (r);

                loadedGeneration = r.generation;

                if (threadShouldExit())
                    return;

                Thread::sleep (25);   // rate limit while a control is being dragged
            }
        }
    }

    void buildImpulseResponses (const LoadRequest& r)
    {
        const auto* roomDef = owner.library.getRoom (r.selection.space, r.selection.room);

        if (roomDef != nullptr && isPositiveAndBelow (r.selection.ir, roomDef->irs.size()))
        {
            const auto& def = roomDef->irs.getReference (r.selection.ir);
            const auto key = def.left.getFullPathName() + "|" + def.right.getFullPathName() + "|"
                           + def.file.getFullPathName() + "|" + String ((int) def.swapLR);

            bool directDirty = false, roomDirty = false;

            if (key != builtKey)
            {
                if (! buildSource (def, source))
                    return;

                builtKey = key;
                directDirty = roomDirty = true;
            }

            directDirty = directDirty || r.directLengthMs != builtLength;
            roomDirty = roomDirty || r.roomSizePct != builtSize;

            if (directDirty || roomDirty)
            {
                auto set = std::make_unique<IRSet>();

                if (directDirty)
                {
                    const auto length = msToSamples (jlimit (2.0f, 100.0f, r.directLengthMs));
                    const auto fade = jmin (length / 2, msToSamples (3.0f));
                    set->direct = slice (length, fade);
                    set->hasDirect = true;
                    builtLength = r.directLengthMs;
                }

                if (roomDirty)
                {
                    const auto fraction = jlimit (0.25f, 1.0f, r.roomSizePct / 100.0f);
                    const auto full = source.fromLeft.getNumSamples();
                    const auto length = jmax (1, (int) std::round (full * fraction));
                    const auto fade = fraction >= 0.999f ? msToSamples (5.0f) : length / 4;
                    set->room = slice (length, fade);
                    set->hasRoom = true;
                    builtSize = r.roomSizePct;
                }

                owner.irMailbox.post (std::move (set));
            }
        }
    }

    void loadAmbience (const LoadRequest& r)
    {
        const auto* roomDef = owner.library.getRoom (r.selection.space, r.selection.room);

        if (roomDef != nullptr && r.sampleRate > 0.0)
        {
            const AmbienceDef* amb = isPositiveAndBelow (r.selection.ambience, roomDef->ambiences.size())
                                       ? &roomDef->ambiences.getReference (r.selection.ambience) : nullptr;

            const auto key = (amb != nullptr ? amb->file.getFullPathName() + "|" + String (amb->gainDb) : String ("none"))
                           + "@" + String (r.sampleRate);

            if (key != ambienceKey)
            {
                auto loop = std::make_unique<AmbienceLoop>();

                if (amb != nullptr)
                {
                    loop->audio = owner.cache->getLoop (amb->file, r.sampleRate);
                    loop->gain = Decibels::decibelsToGain (amb->gainDb);
                }

                if (loop->audio == nullptr)   // no ambience in this room: play silence
                {
                    auto silent = std::make_shared<AudioFileData>();
                    silent->buffer.setSize (2, 1);
                    silent->buffer.clear();
                    loop->audio = silent;
                }

                owner.ambience.setLoop (std::move (loop));
                ambienceKey = key;
            }
        }
    }

    int msToSamples (float ms) const { return jmax (1, (int) std::round (ms * 0.001 * source.sampleRate)); }

    // The first `length` samples of the source with a raised-cosine fade-out
    // over the last `fade` samples, so the cut never clicks.
    LayerIR slice (int length, int fade) const
    {
        LayerIR ir;
        ir.sampleRate = source.sampleRate;
        length = jmin (length, source.fromLeft.getNumSamples());
        fade = jlimit (1, length, fade);

        for (auto [dest, src] : { std::pair { &ir.fromLeft, &source.fromLeft }, std::pair { &ir.fromRight, &source.fromRight } })
        {
            dest->setSize (2, length);

            for (int ch = 0; ch < 2; ++ch)
            {
                dest->copyFrom (ch, 0, *src, ch, 0, length);
                auto* data = dest->getWritePointer (ch);

                for (int i = 0; i < fade; ++i)
                {
                    const auto t = (float) (i + 1) / (float) fade;
                    data[length - fade + i] *= 0.5f * (1.0f + std::cos (MathConstants<float>::pi * t));
                }
            }
        }

        return ir;
    }

    bool buildSource (const IRDef& def, Source& result)
    {
        auto& cache = *owner.cache;

        if (def.type == IRDef::Type::trueStereo)
        {
            auto left = cache.getAudio (def.left);
            auto right = cache.getAudio (def.right);

            if (left == nullptr || right == nullptr)
                return false;

            const auto length = jmax (left->buffer.getNumSamples(), right->buffer.getNumSamples());
            result.sampleRate = left->sampleRate;
            result.fromLeft.setSize (2, length);
            result.fromRight.setSize (2, length);
            result.fromLeft.clear();
            result.fromRight.clear();

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto destCh = def.swapLR ? 1 - ch : ch;
                result.fromLeft.copyFrom (destCh, 0, left->buffer, ch, 0, left->buffer.getNumSamples());
                result.fromRight.copyFrom (destCh, 0, right->buffer, ch, 0, right->buffer.getNumSamples());
            }
        }
        else
        {
            auto stereo = cache.getAudio (def.file);

            if (stereo == nullptr)
                return false;

            const auto length = stereo->buffer.getNumSamples();
            result.sampleRate = stereo->sampleRate;
            result.fromLeft.setSize (2, length);
            result.fromRight.setSize (2, length);
            result.fromLeft.clear();
            result.fromRight.clear();

            // L->L and R->R only.
            result.fromLeft.copyFrom (0, 0, stereo->buffer, def.swapLR ? 1 : 0, 0, length);
            result.fromRight.copyFrom (1, 0, stereo->buffer, def.swapLR ? 0 : 1, 0, length);
        }

        // Calibrate every IR to the same loudness, so switching IRs or rooms
        // doesn't jump in level while the slow gain match catches up.
        const auto gain = calibrationGain (result);
        result.fromLeft.applyGain (gain);
        result.fromRight.applyGain (gain);

        return true;
    }

    // The gain that makes the IR's response to centred (mono) pink noise as
    // loud as the input, using the same K-weighting as the gain match. Music
    // is mostly centred and bass-heavy, so this lands much closer to the real
    // gain match result than a plain energy normalisation would.
    static float calibrationGain (const Source& s)
    {
        const auto length = s.fromLeft.getNumSamples();
        const auto order = jmax (8, (int) std::ceil (std::log2 ((double) length)));
        const auto size = 1 << order;
        dsp::FFT fft (order);

        std::array<Biquad, 2> kWeighting;
        kWeighting[0].setCoefficients (Biquad::Shape::highShelf, s.sampleRate, 1681.0, 4.0, 0.707);
        kWeighting[1].setCoefficients (Biquad::Shape::highPass, s.sampleRate, 38.0, 0.0, 0.5);

        const auto magnitudeSquared = [] (const Biquad& b, double w)
        {
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            const auto num = b.c[0] + b.c[1] * z1 + b.c[2] * z2;
            const auto den = 1.0 + b.c[3] * z1 + b.c[4] * z2;
            return std::norm (num / den);
        };

        double response = 0.0, reference = 0.0;
        std::vector<float> data ((size_t) size * 2);

        for (int outCh = 0; outCh < 2; ++outCh)
        {
            std::fill (data.begin(), data.end(), 0.0f);

            for (int i = 0; i < length; ++i)
                data[(size_t) i] = s.fromLeft.getSample (outCh, i) + s.fromRight.getSample (outCh, i);

            fft.performRealOnlyForwardTransform (data.data(), true);

            for (int k = 1; k <= size / 2; ++k)
            {
                const auto freq = k * s.sampleRate / size;

                if (freq < 20.0 || freq > 20000.0)
                    continue;

                const auto w = MathConstants<double>::twoPi * freq / s.sampleRate;
                const auto weight = magnitudeSquared (kWeighting[0], w) * magnitudeSquared (kWeighting[1], w) / freq;   // pink
                const auto re = (double) data[(size_t) (2 * k)], im = (double) data[(size_t) (2 * k + 1)];
                response += weight * (re * re + im * im);
                reference += weight;
            }
        }

        if (response < 1.0e-12)
            return 1.0f;

        // With both layers at their default levels, and the direct sound
        // adding coherently, the wet sum comes out ~6.5 dB above a single
        // calibrated IR (measured on program material across all the Corsica
        // IRs), so take that off here and the gain match starts near 0 dB.
        constexpr double layerSumDb = -6.5;
        return (float) (std::sqrt (reference / response) * std::pow (10.0, layerSumDb / 20.0));
    }

    SpacesProcessor& owner;
    const Job job;
    std::mutex mutex;
    LoadRequest pending;
    bool hasPending = false;

    // Loader-thread state
    Source source;
    String builtKey, ambienceKey;
    float builtLength = -1.0f, builtSize = -1.0f;
};

//==============================================================================
static String formatDb (float db)
{
    if (db <= minLevelDb)
        return "-inf dB";

    return (db > 0.0f ? "+" : "") + String (db, 1) + " dB";
}

static String formatHz (float hz)
{
    return hz >= 1000.0f ? String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (hz)) + " Hz";
}

AudioProcessorValueTreeState::ParameterLayout SpacesProcessor::createLayout (const PackLibrary& lib, SpacesProcessor& p)
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    const auto* pack0 = lib.getPack (0);
    const auto* room0 = lib.getRoom (0, 0);

    const auto packDefault = [pack0] (const String& id, float fallback)
    {
        if (pack0 != nullptr && pack0->defaults.contains (id))
            return (float) (double) pack0->defaults[id];
        return fallback;
    };

    // --- Selection. Names come from the manifests, so hosts show "Room 1"
    // rather than a number.
    const auto selectionName = [&p] (const char* which, int value) -> String
    {
        const auto& packs = p.library;
        const auto space = p.readParamIndex (ParamIDs::space);
        const auto index = value - 1;

        if (String (which) == ParamIDs::space)
            if (auto* s = packs.getPack (index))
                return s->name;

        if (auto* r = packs.getRoom (space, String (which) == ParamIDs::room ? index : p.readParamIndex (ParamIDs::room)))
        {
            if (String (which) == ParamIDs::room)
                return r->name;

            if (String (which) == ParamIDs::ir && isPositiveAndBelow (index, r->irs.size()))
                return r->irs.getReference (index).name;

            if (String (which) == ParamIDs::ambience && isPositiveAndBelow (index, r->ambiences.size()))
                return r->ambiences.getReference (index).name;
        }

        return String (value);
    };

    const auto addSelection = [&] (const char* id, const char* name, int max, int def)
    {
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { id, 1 }, name, 1, max, def,
            AudioParameterIntAttributes().withStringFromValueFunction ([selectionName, id] (int v, int) { return selectionName (id, v); })));
    };

    addSelection (ParamIDs::space,    "Space",    maxSpaces,    1);
    addSelection (ParamIDs::room,     "Room",     maxRooms,     1);
    addSelection (ParamIDs::ir,       "IR",       maxIRs,       room0 != nullptr ? room0->defaultIr + 1 : 1);
    addSelection (ParamIDs::ambience, "Ambience", maxAmbiences, room0 != nullptr ? room0->defaultAmbience + 1 : 1);

    // --- Helpers
    const auto dbAttributes = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatDb (v); })
                                                             .withLabel ("dB");
    const auto hzAttributes = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
                                                             .withLabel ("Hz");
    const auto pctAttributes = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })
                                                              .withLabel ("%");
    const auto msAttributes = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; })
                                                             .withLabel ("ms");
    const auto qAttributes = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); });

    const auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                               const AudioParameterFloatAttributes& attributes)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, packDefault (id, def), attributes));
    };

    const auto skewed = [] (float min, float max, float centre, float interval = 0.0f)
    {
        NormalisableRange<float> r (min, max, interval);
        r.setSkewForCentre (centre);
        return r;
    };

    const auto addBool = [&] (const String& id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, packDefault (id, def ? 1.0f : 0.0f) > 0.5f));
    };

    // --- Layers
    for (auto* layer : { &directIDs, &roomIDs })
    {
        const bool isDirect = layer == &directIDs;
        const String n = isDirect ? "Direct " : "Room ";

        addFloat (layer->level(), n + "Level", { minLevelDb, 12.0f, 0.1f }, 0.0f, dbAttributes);

        if (isDirect)
            addFloat (layer->length(), n + "Length", skewed (2.0f, 100.0f, 15.0f, 0.1f), 10.0f, msAttributes);
        else
            addFloat (layer->size(), n + "Size", { 25.0f, 100.0f, 0.1f }, 100.0f, pctAttributes);

        addFloat (layer->drive(),  n + "Drive",          { 0.0f, 100.0f, 0.1f }, 0.0f, pctAttributes);
        addFloat (layer->lsFreq(), n + "Low Shelf Freq", skewed (20.0f, 1000.0f, 140.0f), 100.0f, hzAttributes);
        addFloat (layer->lsGain(), n + "Low Shelf Gain", { -18.0f, 18.0f, 0.1f }, 0.0f, dbAttributes);
        addFloat (layer->b1Freq(), n + "Bell 1 Freq",    skewed (20.0f, 20000.0f, 630.0f), 250.0f, hzAttributes);
        addFloat (layer->b1Gain(), n + "Bell 1 Gain",    { -18.0f, 18.0f, 0.1f }, 0.0f, dbAttributes);
        addFloat (layer->b1Q(),    n + "Bell 1 Q",       skewed (0.1f, 10.0f, 1.0f), 0.7f, qAttributes);
        addFloat (layer->b2Freq(), n + "Bell 2 Freq",    skewed (20.0f, 20000.0f, 630.0f), 2500.0f, hzAttributes);
        addFloat (layer->b2Gain(), n + "Bell 2 Gain",    { -18.0f, 18.0f, 0.1f }, 0.0f, dbAttributes);
        addFloat (layer->b2Q(),    n + "Bell 2 Q",       skewed (0.1f, 10.0f, 1.0f), 0.7f, qAttributes);
        addFloat (layer->hsFreq(), n + "High Shelf Freq", skewed (1000.0f, 20000.0f, 4500.0f), 8000.0f, hzAttributes);
        addFloat (layer->hsGain(), n + "High Shelf Gain", { -18.0f, 18.0f, 0.1f }, 0.0f, dbAttributes);
    }

    // --- Ambience
    addBool (ParamIDs::ambOn, "Ambience On", true);
    addFloat (ParamIDs::ambLevel, "Ambience Level", { minLevelDb, 6.0f, 0.1f }, -6.0f, dbAttributes);

    // --- Global
    addFloat (ParamIDs::mix, "Dry/Wet", { 0.0f, 100.0f, 0.1f }, 100.0f, pctAttributes);
    addBool (ParamIDs::autoGain, "Auto Gain", true);
    addFloat (ParamIDs::outTrim, "Output Trim", { -12.0f, 12.0f, 0.1f }, 0.0f, dbAttributes);
    addBool (ParamIDs::mono, "Mono", false);
    addBool (ParamIDs::bypass, "Bypass", false);

    return layout;
}

//==============================================================================
static PackLibrary scanLibrary()
{
    PackLibrary lib;
    lib.scan (PackLibrary::getSearchFolders());

    for (auto& e : lib.getErrors())
    {
        DBG ("Spaces: " << e);
        ignoreUnused (e);
    }

    return lib;
}

SpacesProcessor::SpacesProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      library (scanLibrary()),
      apvts (*this, nullptr, "SpacesState", createLayout (library, *this)),
      presets (*this),
      direct (convolutionQueue),
      room (convolutionQueue)
{
    const auto raw = [this] (const String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (auto [params, ids] : { std::pair { &directParams, &directIDs }, std::pair { &roomParams, &roomIDs } })
    {
        params->level  = raw (ids->level());
        params->shape  = raw (ids == &directIDs ? ids->length() : ids->size());
        params->drive  = raw (ids->drive());
        params->lsFreq = raw (ids->lsFreq());
        params->lsGain = raw (ids->lsGain());
        params->b1Freq = raw (ids->b1Freq());
        params->b1Gain = raw (ids->b1Gain());
        params->b1Q    = raw (ids->b1Q());
        params->b2Freq = raw (ids->b2Freq());
        params->b2Gain = raw (ids->b2Gain());
        params->b2Q    = raw (ids->b2Q());
        params->hsFreq = raw (ids->hsFreq());
        params->hsGain = raw (ids->hsGain());
    }

    ambOn    = raw (ParamIDs::ambOn);
    ambLevel = raw (ParamIDs::ambLevel);
    mix      = raw (ParamIDs::mix);
    autoGain = raw (ParamIDs::autoGain);
    outTrim  = raw (ParamIDs::outTrim);
    mono     = raw (ParamIDs::mono);
    bypass   = raw (ParamIDs::bypass);

    irLoader = std::make_unique<Loader> (*this, Loader::Job::impulseResponses);
    ambienceLoader = std::make_unique<Loader> (*this, Loader::Job::ambience);

    applied = clampSelection ({ readParamIndex (ParamIDs::space), readParamIndex (ParamIDs::room),
                                readParamIndex (ParamIDs::ir),    readParamIndex (ParamIDs::ambience) });
    presets.refresh();

    // A fresh instance starts out identical to the first factory preset
    // (the space's defaults), so show that name rather than "Init".
    if (! presets.getEntries().isEmpty())
        presets.setCurrentByName (presets.getEntries().getFirst().name);

    timerCallback();
    startTimerHz (30);
}

SpacesProcessor::~SpacesProcessor()
{
    stopTimer();
    irLoader = nullptr;
    ambienceLoader = nullptr;
}

//==============================================================================
bool SpacesProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();

    return out == AudioChannelSet::stereo()
        && (in == AudioChannelSet::stereo() || in == AudioChannelSet::mono());
}

double SpacesProcessor::getTailLengthSeconds() const
{
    // The ambience never stops, so tell hosts the tail is endless and they
    // keep calling processBlock when the transport is stopped. VST3 has an
    // explicit "infinite" value; for AU a very long tail does the same job.
    if (wrapperType == wrapperType_AudioUnit)
        return 24.0 * 60.0 * 60.0;

    return std::numeric_limits<double>::infinity();
}

AudioProcessorParameter* SpacesProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

void SpacesProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlockSize = jmax (1, samplesPerBlock);
    currentSampleRate = sampleRate;

    dryBuffer.setSize (2, maxBlockSize, false, false, true);
    wetBuffer.setSize (2, maxBlockSize, false, false, true);

    direct.prepare (sampleRate, maxBlockSize);
    room.prepare (sampleRate, maxBlockSize);
    gainMatch.prepare (sampleRate);
    ambience.prepare (sampleRate);

    for (auto* s : { &mixSmoothed, &monoSmoothed, &trimSmoothed, &bypassSmoothed })
        s->reset (sampleRate, 0.05);

    mixSmoothed.setCurrentAndTargetValue (mix->load() * 0.01f);
    monoSmoothed.setCurrentAndTargetValue (mono->load() > 0.5f ? 1.0f : 0.0f);
    trimSmoothed.setCurrentAndTargetValue (Decibels::decibelsToGain (outTrim->load()));
    bypassSmoothed.setCurrentAndTargetValue (bypass->load() > 0.5f ? 1.0f : 0.0f);

    // VU ballistics: two cascaded one-pole filters on the rectified signal,
    // which together reach 99% in roughly 300 ms.
    vuCoefficient = 1.0 - std::exp (-1.0 / (0.065 * sampleRate));
    std::memset (vuState, 0, sizeof (vuState));

    updateDspTargets();

    // Make sure the ambience exists at this sample rate.
    if (MessageManager::getInstanceWithoutCreating() != nullptr
        && MessageManager::getInstance()->isThisTheMessageThread())
        timerCallback();
}

void SpacesProcessor::reset()
{
    direct.reset();
    room.reset();
    gainMatch.reset();
}

//==============================================================================
void SpacesProcessor::updateDspTargets() noexcept
{
    for (auto [layer, params] : { std::pair { &direct, &directParams }, std::pair { &room, &roomParams } })
    {
        layer->setLevel (levelDbToGain (params->level->load()));
        layer->setDrive (params->drive->load() * 0.01f);

        LayerEQ::Settings eq;
        eq.lsFreq = params->lsFreq->load();  eq.lsGain = params->lsGain->load();
        eq.b1Freq = params->b1Freq->load();  eq.b1Gain = params->b1Gain->load();  eq.b1Q = params->b1Q->load();
        eq.b2Freq = params->b2Freq->load();  eq.b2Gain = params->b2Gain->load();  eq.b2Q = params->b2Q->load();
        eq.hsFreq = params->hsFreq->load();  eq.hsGain = params->hsGain->load();
        layer->setEQ (eq);
    }

    ambience.setLevel (ambOn->load() > 0.5f ? levelDbToGain (ambLevel->load()) : 0.0f);
    gainMatch.setEnabled (autoGain->load() > 0.5f);
    mixSmoothed.setTargetValue (mix->load() * 0.01f);
    monoSmoothed.setTargetValue (mono->load() > 0.5f ? 1.0f : 0.0f);
    trimSmoothed.setTargetValue (Decibels::decibelsToGain (outTrim->load()));
    bypassSmoothed.setTargetValue (bypass->load() > 0.5f ? 1.0f : 0.0f);
}

// S-shaped easing for on/off crossfades (mono, bypass): no corner at either
// end of the ramp, so toggling is smoother than a straight linear fade.
static inline float smoothstep (float x) noexcept { return x * x * (3.0f - 2.0f * x); }

void SpacesProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    for (int start = 0; start < numSamples; start += maxBlockSize)
        processChunk (buffer, start, jmin (maxBlockSize, numSamples - start));
}

void SpacesProcessor::processChunk (AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    if (auto* set = irMailbox.fetch())
    {
        if (set->hasDirect) direct.loadIR (set->direct);
        if (set->hasRoom)   room.loadIR (set->room);
        irMailbox.retire (set);
    }

    updateDspTargets();

    for (int ch = 0; ch < 2; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, jmin (ch, buffer.getNumChannels() - 1), start, numSamples);

    wetBuffer.clear (0, numSamples);
    direct.process (dryBuffer, wetBuffer, numSamples);
    room.process (dryBuffer, wetBuffer, numSamples);

    gainMatch.process (dryBuffer, wetBuffer, numSamples);
    gainMatchDb = gainMatch.getCorrectionDb();

    // Dry/wet, then mono (ambience comes after both, so it stays stereo and
    // dry/wet never changes it).
    auto* outL = wetBuffer.getWritePointer (0);
    auto* outR = wetBuffer.getWritePointer (1);
    const auto* dryL = dryBuffer.getReadPointer (0);
    const auto* dryR = dryBuffer.getReadPointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto m = mixSmoothed.getNextValue();
        auto l = dryL[i] + m * (outL[i] - dryL[i]);
        auto r = dryR[i] + m * (outR[i] - dryR[i]);

        const auto mo = smoothstep (monoSmoothed.getNextValue());
        const auto sum = 0.5f * (l + r);
        outL[i] = l + mo * (sum - l);
        outR[i] = r + mo * (sum - r);
    }

    ambience.process (wetBuffer, numSamples);

    // Output trim, bypass crossfade, VU
    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = trimSmoothed.getNextValue();
        const auto b = smoothstep (bypassSmoothed.getNextValue());
        const float out[2] { outL[i] * t, outR[i] * t };
        const float dry[2] { dryL[i], dryR[i] };

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto y = out[ch] + b * (dry[ch] - out[ch]);
            (ch == 0 ? outL : outR)[i] = y;

            auto& s = vuState[ch];
            s[0] += vuCoefficient * (std::abs (y) - s[0]);
            s[1] += vuCoefficient * (s[0] - s[1]);
        }
    }

    vuLevel[0] = (float) vuState[0][1];
    vuLevel[1] = (float) vuState[1][1];

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, start, wetBuffer, jmin (ch, 1), 0, numSamples);
}

//==============================================================================
int SpacesProcessor::readParamIndex (const char* id) const
{
    if (auto* p = apvts.getRawParameterValue (id))
        return roundToInt (p->load()) - 1;
    return 0;
}

void SpacesProcessor::setParamIndex (const String& id, int zeroBasedIndex)
{
    if (auto* p = apvts.getParameter (id))
    {
        const auto value = p->convertTo0to1 ((float) (zeroBasedIndex + 1));

        if (std::abs (p->getValue() - value) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            p->endChangeGesture();
        }
    }
}

Selection SpacesProcessor::clampSelection (Selection s) const
{
    if (library.getNumPacks() == 0)
        return {};

    s.space = jlimit (0, library.getNumPacks() - 1, s.space);
    const auto& pack = *library.getPack (s.space);
    s.room = jlimit (0, pack.rooms.size() - 1, s.room);
    const auto& r = pack.rooms.getReference (s.room);
    s.ir = jlimit (0, r.irs.size() - 1, s.ir);
    s.ambience = r.ambiences.isEmpty() ? -1 : jlimit (0, r.ambiences.size() - 1, s.ambience);
    return s;
}

Selection SpacesProcessor::getSelection() const { return applied; }

const SpacePack* SpacesProcessor::getCurrentSpace() const { return library.getPack (applied.space); }
const RoomDef* SpacesProcessor::getCurrentRoom() const    { return library.getRoom (applied.space, applied.room); }

bool SpacesProcessor::isAmbienceAudible() const
{
    return ambOn->load() > 0.5f && ambLevel->load() > minLevelDb && bypass->load() < 0.5f;
}

void SpacesProcessor::timerCallback()
{
    auto wanted = clampSelection ({ readParamIndex (ParamIDs::space), readParamIndex (ParamIDs::room),
                                    readParamIndex (ParamIDs::ir),    readParamIndex (ParamIDs::ambience) });

    // A new room (chosen in the UI or by automation) starts on that room's
    // default IR and ambience, unless the change came with its own IR, as
    // it does from a preset or a restored session.
    if ((wanted.space != applied.space || wanted.room != applied.room)
        && applied.room >= 0
        && Time::getMillisecondCounter() > suppressRoomDefaultsUntil)
    {
        if (auto* r = library.getRoom (wanted.space, wanted.room))
        {
            wanted.ir = r->defaultIr;
            wanted.ambience = r->ambiences.isEmpty() ? -1 : r->defaultAmbience;
            setParamIndex (ParamIDs::ir, wanted.ir);
            setParamIndex (ParamIDs::ambience, jmax (0, wanted.ambience));
        }
    }

    applied = wanted;

    LoadRequest r;
    r.selection = applied;
    r.directLengthMs = directParams.shape->load();
    r.roomSizePct = roomParams.shape->load();
    r.sampleRate = currentSampleRate.load();

    if (! (r.selection == lastRequest.selection) || r.directLengthMs != lastRequest.directLengthMs
        || r.roomSizePct != lastRequest.roomSizePct || r.sampleRate != lastRequest.sampleRate)
    {
        r.generation = ++requestedGeneration;
        lastRequest = r;
        irLoader->request (r);
        ambienceLoader->request (r);
    }

    irMailbox.collectGarbage();
    ambience.collectGarbage();
}

//==============================================================================
void SpacesProcessor::selectRoom (int roomIndex)
{
    setParamIndex (ParamIDs::room, roomIndex);
    timerCallback();
}

void SpacesProcessor::selectIr (int irIndex)
{
    setParamIndex (ParamIDs::ir, irIndex);
    timerCallback();
}

void SpacesProcessor::selectAmbience (int ambienceIndex)
{
    setParamIndex (ParamIDs::ambience, ambienceIndex);
    timerCallback();
}

void SpacesProcessor::stepIr (int delta)
{
    if (auto* r = getCurrentRoom())
    {
        const auto n = r->irs.size();
        selectIr (((applied.ir + delta) % n + n) % n);
    }
}

void SpacesProcessor::stepAmbience (int delta)
{
    if (auto* r = getCurrentRoom(); r != nullptr && ! r->ambiences.isEmpty())
    {
        const auto n = r->ambiences.size();
        selectAmbience (((applied.ambience + delta) % n + n) % n);
    }
}

void SpacesProcessor::applySelection (const Selection& s)
{
    suppressRoomDefaultsUntil = Time::getMillisecondCounter() + 500;
    const auto c = clampSelection (s);
    setParamIndex (ParamIDs::space, c.space);
    setParamIndex (ParamIDs::room, c.room);
    setParamIndex (ParamIDs::ir, c.ir);
    setParamIndex (ParamIDs::ambience, jmax (0, c.ambience));

    if (MessageManager::getInstance()->isThisTheMessageThread())
    {
        applied = c;   // so the poll below doesn't treat it as a room change
        timerCallback();
    }
}

void SpacesProcessor::resetParametersToDefaults (const SpacePack* space)
{
    static const StringArray selectionIds { ParamIDs::space, ParamIDs::room, ParamIDs::ir, ParamIDs::ambience, ParamIDs::bypass };

    for (auto* p : getParameters())
    {
        auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p);

        if (withId == nullptr || selectionIds.contains (withId->paramID))
            continue;

        auto* ranged = dynamic_cast<RangedAudioParameter*> (p);
        float value = p->getDefaultValue();

        if (space != nullptr && ranged != nullptr && space->defaults.contains (withId->paramID))
            value = ranged->convertTo0to1 ((float) (double) space->defaults[withId->paramID]);

        if (std::abs (p->getValue() - value) > 1.0e-6f)
            p->setValueNotifyingHost (value);
    }
}

//==============================================================================
ValueTree SpacesProcessor::createStateTree()
{
    auto state = apvts.copyState();

    // Store the selection by ID as well as by index, so sessions still open
    // on the right room/IR if a pack's lists are reordered or extended.
    if (auto* space = getCurrentSpace())
    {
        state.setProperty ("spaceId", space->id, nullptr);

        if (auto* r = getCurrentRoom())
        {
            state.setProperty ("roomId", r->id, nullptr);

            if (isPositiveAndBelow (applied.ir, r->irs.size()))
                state.setProperty ("irId", r->irs.getReference (applied.ir).id, nullptr);

            if (isPositiveAndBelow (applied.ambience, r->ambiences.size()))
                state.setProperty ("ambienceId", r->ambiences.getReference (applied.ambience).id, nullptr);
        }
    }

    state.setProperty ("preset", presets.getCurrentName(), nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    state.setProperty ("version", SPACES_VERSION_STRING, nullptr);
    return state;
}

void SpacesProcessor::restoreStateTree (const ValueTree& tree)
{
    if (! tree.hasType (apvts.state.getType()))
        return;

    suppressRoomDefaultsUntil = Time::getMillisecondCounter() + 500;
    apvts.replaceState (tree.createCopy());

    // Resolve the saved IDs back to indices.
    Selection s { readParamIndex (ParamIDs::space), readParamIndex (ParamIDs::room),
                  readParamIndex (ParamIDs::ir),    readParamIndex (ParamIDs::ambience) };

    if (auto space = library.indexOfPack (tree["spaceId"].toString()); space >= 0)
    {
        s.space = space;
        const auto& pack = *library.getPack (space);

        if (auto r = pack.indexOfRoom (tree["roomId"].toString()); r >= 0)
        {
            s.room = r;
            const auto& roomDef = pack.rooms.getReference (r);

            if (auto i = roomDef.indexOfIr (tree["irId"].toString()); i >= 0)
                s.ir = i;

            if (auto a = roomDef.indexOfAmbience (tree["ambienceId"].toString()); a >= 0)
                s.ambience = a;
        }
    }

    if (tree.hasProperty ("uiScale"))   // user presets don't carry it
        uiScale = jlimit (0.5f, 2.0f, (float) tree["uiScale"]);
    applySelection (s);

    if (MessageManager::getInstance()->isThisTheMessageThread())
        presets.setCurrentByName (tree["preset"].toString());
}

void SpacesProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacesProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreStateTree (ValueTree::fromXml (*xml));
}

bool SpacesProcessor::isFullyLoaded() const noexcept
{
    const auto wanted = requestedGeneration.load();
    return irLoader->loadedGeneration.load() == wanted && ambienceLoader->loadedGeneration.load() == wanted;
}

//==============================================================================
AudioProcessorEditor* SpacesProcessor::createEditor()
{
    return new SpacesEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacesProcessor();
}
