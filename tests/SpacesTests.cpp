// Offline checks for the Spaces engine: runs the real processor on generated
// program material and measures what the brief asks for (gain match, clicks,
// allocations on the audio thread, ambience looping, CPU).
//
//   SpacesTests [--quick]

#include "PluginProcessor.h"
#include <atomic>
#include <cstdlib>
#include <new>

//==============================================================================
// Allocation tracking: counts heap use while the audio thread is "inside"
// processBlock.
static thread_local bool inAudioCallback = false;
static std::atomic<int> audioThreadAllocations { 0 }, audioThreadFrees { 0 };

void* operator new (std::size_t size)
{
    if (inAudioCallback)
        ++audioThreadAllocations;

    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size) { return operator new (size); }

void operator delete (void* p) noexcept
{
    if (p != nullptr && inAudioCallback)
        ++audioThreadFrees;

    std::free (p);
}

void operator delete[] (void* p) noexcept                 { operator delete (p); }
void operator delete (void* p, std::size_t) noexcept       { operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept     { operator delete (p); }

//==============================================================================
using namespace juce;

static int failures = 0;

static void check (bool ok, const String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;

    if (! ok)
        ++failures;
}

static void info (const String& what) { std::cout << "        " << what << std::endl; }

//==============================================================================
// Test signals
struct Pink
{
    Random random { 1234 };
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    float next()
    {
        const float white = random.nextFloat() * 2.0f - 1.0f;
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;
        return out * 0.11f;
    }
};

// Something with the dynamics of dance music: kick, bass, hats and a pad.
struct ProgramSignal
{
    double sampleRate;
    long long n = 0;
    Pink pinkL, pinkR;
    Random random { 99 };

    float kickPhase = 0, bassPhase = 0;

    explicit ProgramSignal (double sr) : sampleRate (sr) { pinkR.random.setSeed (777); }

    void next (float& l, float& r)
    {
        const double t = n / sampleRate;
        const double beat = std::fmod (t * 2.0, 1.0);          // 120 bpm
        const double half = std::fmod (t * 4.0 + 0.5, 1.0);    // off-beat hats

        const float kickEnv = (float) std::exp (-beat * 12.0);
        kickPhase += (float) (MathConstants<double>::twoPi * (45.0 + 120.0 * std::exp (-beat * 30.0)) / sampleRate);
        const float kick = std::sin (kickPhase) * kickEnv * 0.7f;

        const float bassEnv = (float) (beat > 0.5 ? std::exp (-(beat - 0.5) * 6.0) : 0.0);
        bassPhase += (float) (MathConstants<double>::twoPi * 55.0 / sampleRate);
        const float bass = std::sin (bassPhase) * bassEnv * 0.4f;

        const float hatEnv = (float) std::exp (-half * 40.0);
        const float hat = (random.nextFloat() * 2.0f - 1.0f) * hatEnv * 0.15f;

        l = kick + bass + hat + pinkL.next() * 0.5f;
        r = kick + bass + hat * 0.8f + pinkR.next() * 0.5f;
        ++n;
    }
};

//==============================================================================
// K-weighted mean-square meter (same weighting as the plugin's gain match).
struct LoudnessMeter
{
    std::array<Biquad, 2> filters;
    double sum = 0.0;
    long long count = 0;

    explicit LoudnessMeter (double sr)
    {
        filters[0].setCoefficients (Biquad::Shape::highShelf, sr, 1681.0, 4.0, 0.707);
        filters[1].setCoefficients (Biquad::Shape::highPass, sr, 38.0, 0.0, 0.5);
    }

    void add (float l, float r)
    {
        for (auto& f : filters) l = f.process (l, 0);
        for (auto& f : filters) r = f.process (r, 1);
        sum += (double) l * l + (double) r * r;
        ++count;
    }

    double db() const { return 10.0 * std::log10 (sum / jmax (1ll, count) / 2.0 + 1e-20); }
    void clear() { sum = 0; count = 0; }
};

//==============================================================================
struct Harness
{
    double sampleRate;
    int blockSize;
    std::unique_ptr<SpacesProcessor> processor;
    AudioBuffer<float> buffer;
    MidiBuffer midi;

    Harness (double sr = 48000.0, int block = 512) : sampleRate (sr), blockSize (block)
    {
        processor = std::make_unique<SpacesProcessor>();
        processor->setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        buffer.setSize (2, blockSize);
    }

    void set (const String& id, float realValue)
    {
        auto* p = processor->getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        processor->pollNow();
    }

    // Renders seconds of audio; fill() produces input, sink() sees output.
    template <typename Fill, typename Sink>
    void render (double seconds, Fill&& fill, Sink&& sink)
    {
        const auto blocks = (int) std::ceil (seconds * sampleRate / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                float l, r;
                fill (l, r);
                buffer.setSample (0, i, l);
                buffer.setSample (1, i, r);
            }

            inAudioCallback = true;
            processor->processBlock (buffer, midi);
            inAudioCallback = false;

            for (int i = 0; i < blockSize; ++i)
                sink (buffer.getSample (0, i), buffer.getSample (1, i));

            if (b % 4 == 0)
                processor->pollNow();   // stands in for the 30 Hz message-thread timer
        }
    }

    void renderSilence (double seconds)
    {
        render (seconds, [] (float& l, float& r) { l = r = 0.0f; }, [] (float, float) {});
    }

    // Waits for the loader, then lets the convolution engines build and crossfade.
    void settle (double seconds = 0.5)
    {
        for (int i = 0; i < 400 && ! processor->isFullyLoaded(); ++i)
        {
            Thread::sleep (5);
            processor->pollNow();
        }

        // Engines are built on JUCE's background thread; give it real time.
        for (int i = 0; i < 10; ++i)
        {
            renderSilence (seconds / 10.0);
            Thread::sleep (20);
        }
    }
};

//==============================================================================
static void testLibrary()
{
    std::cout << "\nSpace packs" << std::endl;
    PackLibrary lib;
    lib.scan (PackLibrary::getSearchFolders());

    for (auto& e : lib.getErrors())
        info ("note: " + e);

    check (lib.getNumPacks() == 1, "one pack found (" + String (lib.getNumPacks()) + ")");

    if (auto* pack = lib.getPack (0))
    {
        check (pack->rooms.size() == 2, "Corsica has 2 rooms");

        for (auto& r : pack->rooms)
            info (r.name + ": " + String (r.irs.size()) + " IRs, " + String (r.ambiences.size()) + " ambience, default IR '"
                  + r.irs[r.defaultIr].name + "'");

        check (pack->rooms[0].irs.size() == 11 && pack->rooms[1].irs.size() == 10, "all IRs resolved to files");
        check (pack->presets.size() >= 3, String (pack->presets.size()) + " factory presets");
    }
}

static void testSelectionAndState()
{
    std::cout << "\nSelection and state" << std::endl;
    Harness h;
    auto& p = *h.processor;

    auto s = p.getSelection();
    check (s.room == 0 && s.ir == p.getLibrary().getRoom (0, 0)->defaultIr, "starts on Room 1 default IR");

    p.selectRoom (1);
    s = p.getSelection();
    const auto* room2 = p.getLibrary().getRoom (0, 1);
    check (s.room == 1 && s.ir == room2->defaultIr && s.ambience == room2->defaultAmbience,
           "switching room loads its default IR and ambience");

    p.stepIr (2);
    p.stepIr (-1);
    check (p.getSelection().ir == (room2->defaultIr + 1) % room2->irs.size(), "IR stepping");

    h.set (directIDs.length(), 33.0f);
    h.set (ParamIDs::mix, 70.0f);

    MemoryBlock state;
    p.getStateInformation (state);

    Harness other;
    other.processor->setStateInformation (state.getData(), (int) state.getSize());
    other.processor->pollNow();
    other.processor->pollNow();

    const auto restored = other.processor->getSelection();
    check (restored == p.getSelection(), "selection restored from saved state");
    check (std::abs (other.processor->getState().getRawParameterValue (directIDs.length())->load() - 33.0f) < 0.05f,
           "parameters restored from saved state");

    auto& presets = p.getPresets();
    presets.load (0);
    check (presets.getCurrentName() == presets.getEntries()[0].name, "factory preset loads: " + presets.getCurrentName());
    presets.next();
    check (presets.getCurrentIndex() == 1, "next preset");
}

static void testGainMatch (bool quick)
{
    std::cout << "\nAuto gain match (program material, ambience off)" << std::endl;

    Harness reference;
    const auto& lib = reference.processor->getLibrary();
    double worst = 0.0, worstPump = 0.0;
    String worstName;

    for (int roomIndex = 0; roomIndex < 2; ++roomIndex)
    {
        const auto* roomDef = lib.getRoom (0, roomIndex);

        for (int ir = 0; ir < roomDef->irs.size(); ++ir)
        {
            if (quick && ir > 1)
                break;

            Harness h;
            h.set (ParamIDs::ambOn, 0.0f);
            h.processor->applySelection ({ 0, roomIndex, ir, 0 });
            h.settle();

            ProgramSignal signal (h.sampleRate);
            LoudnessMeter in (h.sampleRate), out (h.sampleRate);
            float lastL = 0, lastR = 0;
            auto fill = [&] (float& l, float& r) { signal.next (l, r); lastL = l; lastR = r; };

            // Let the meter settle, then measure.
            h.render (8.0, fill, [] (float, float) {});

            float minCorr = 100, maxCorr = -100;
            const auto blocksPerCheck = (int) (h.sampleRate * 0.1 / h.blockSize);
            int counter = 0;

            h.render (6.0, [&] (float& l, float& r) { fill (l, r); in.add (l, r); },
                      [&] (float l, float r)
                      {
                          out.add (l, r);

                          if (++counter % (blocksPerCheck * h.blockSize) == 0)
                          {
                              const auto c = h.processor->getGainMatchDb();
                              minCorr = jmin (minCorr, c);
                              maxCorr = jmax (maxCorr, c);
                          }
                      });

            const auto diff = out.db() - in.db();
            const auto pump = maxCorr - minCorr;
            info (roomDef->name + " / " + roomDef->irs[ir].name + ": out-in " + String (diff, 2) + " dB, correction "
                  + String (h.processor->getGainMatchDb(), 1) + " dB, correction swing " + String (pump, 2) + " dB");

            if (std::abs (diff) > std::abs (worst)) { worst = diff; worstName = roomDef->irs[ir].name; }
            worstPump = jmax (worstPump, (double) pump);
        }
    }

    check (std::abs (worst) <= 1.0, "wet output within 1 dB of input (worst " + String (worst, 2) + " dB, " + worstName + ")");
    check (worstPump <= 1.0, "correction moves less than 1 dB while playing (worst swing " + String (worstPump, 2) + " dB)");
}

//==============================================================================
// Click detection: feed a steady low sine, so the output should contain
// nothing above a few kHz. A click is broadband, so it shows up as a spike in
// the high-passed output during a switch.
struct ClickProbe
{
    Biquad hp1, hp2;
    float peak = 0.0f;

    explicit ClickProbe (double sr)
    {
        hp1.setCoefficients (Biquad::Shape::highPass, sr, 4000.0, 0.0, 0.707);
        hp2.setCoefficients (Biquad::Shape::highPass, sr, 4000.0, 0.0, 0.707);
    }

    void add (float l, float r)
    {
        peak = jmax (peak, std::abs (hp2.process (hp1.process (l, 0), 0)), std::abs (hp2.process (hp1.process (r, 1), 1)));
    }
};

static void testClicks (bool quick)
{
    std::cout << "\nClick-free switching (220 Hz sine in, ambience off)" << std::endl;

    Harness h;
    auto& p = *h.processor;
    h.set (ParamIDs::ambOn, 0.0f);
    h.set (ParamIDs::autoGain, 0.0f);   // keeps the level steady between switches
    h.settle();

    double phase = 0.0;
    const auto inc = MathConstants<double>::twoPi * 220.0 / h.sampleRate;
    auto sine = [&] (float& l, float& r) { l = r = (float) (0.25 * std::sin (phase)); phase += inc; };

    // Baseline: the probe's level for the steady state.
    ClickProbe baseline (h.sampleRate);
    h.render (1.0, sine, [&] (float l, float r) { baseline.add (l, r); });
    baseline.peak = 0.0f;
    h.render (1.0, sine, [&] (float l, float r) { baseline.add (l, r); });
    const auto floorLevel = jmax (baseline.peak, 1.0e-6f);
    info ("steady-state HF level " + String (Decibels::gainToDecibels (floorLevel), 1) + " dBFS");

    struct Action { const char* name; std::function<void()> run; };
    std::vector<Action> actions {
        { "room 1 -> room 2",  [&] { p.selectRoom (1); } },
        { "room 2 -> room 1",  [&] { p.selectRoom (0); } },
        { "next IR",           [&] { p.stepIr (1); } },
        { "next IR again",     [&] { p.stepIr (1); } },
        { "next preset",       [&] { p.getPresets().next(); h.set (ParamIDs::ambOn, 0.0f); h.set (ParamIDs::autoGain, 0.0f); } },
        { "next preset again", [&] { p.getPresets().next(); h.set (ParamIDs::ambOn, 0.0f); h.set (ParamIDs::autoGain, 0.0f); } },
        { "direct length 10 -> 60 ms", [&] { h.set (directIDs.length(), 60.0f); } },
        { "room size 100 -> 40 %",     [&] { h.set (roomIDs.size(), 40.0f); } },
        { "mono on",           [&] { h.set (ParamIDs::mono, 1.0f); } },
        { "mono off",          [&] { h.set (ParamIDs::mono, 0.0f); } },
        { "bypass on",         [&] { h.set (ParamIDs::bypass, 1.0f); } },
        { "bypass off",        [&] { h.set (ParamIDs::bypass, 0.0f); } },
        { "dry/wet 100 -> 0 %", [&] { h.set (ParamIDs::mix, 0.0f); } },
        { "dry/wet 0 -> 100 %", [&] { h.set (ParamIDs::mix, 100.0f); } },
        { "drive 0 -> 50 %",   [&] { h.set (directIDs.drive(), 50.0f); } },
        { "drive 50 -> 0 %",   [&] { h.set (directIDs.drive(), 0.0f); } },
        { "EQ bell +12 dB",    [&] { h.set (roomIDs.b1Gain(), 12.0f); } },
        { "EQ bell 0 dB",      [&] { h.set (roomIDs.b1Gain(), 0.0f); } },
    };

    float worst = 0.0f;
    String worstName;
    ignoreUnused (quick);

    // One continuous probe, so its own filters never see a start-up step.
    ClickProbe probe (h.sampleRate);
    h.render (0.5, sine, [&] (float l, float r) { probe.add (l, r); });

    for (auto& a : actions)
    {
        probe.peak = 0.0f;
        a.run();
        h.render (0.6, sine, [&] (float l, float r) { probe.add (l, r); });

        for (int i = 0; i < 6; ++i)   // let any background IR build finish, still feeding the probe
        {
            h.render (0.05, sine, [&] (float l, float r) { probe.add (l, r); });
            Thread::sleep (20);
            h.processor->pollNow();
        }

        // Drive adds harmonics of 220 Hz by design, so judge it by its own steady state.
        const auto ratioDb = Decibels::gainToDecibels (probe.peak / floorLevel);
        info (String (a.name) + ": HF peak " + String (Decibels::gainToDecibels (probe.peak), 1) + " dBFS ("
              + String (ratioDb, 1) + " dB over steady state)");

        const auto peakDb = Decibels::gainToDecibels (probe.peak, -200.0f);

        if (worstName.isEmpty() || peakDb > worst)
        {
            worst = peakDb;
            worstName = a.name;
        }
    }

    // A hard (unfaded) switch on this -12 dBFS sine shows up at around
    // -20 dBFS in this band. Require everything to stay below -65 dBFS
    // (over 50 dB under the signal).
    check (worst < -65.0f, "no clicks (worst: " + worstName + ", " + String (worst, 1) + " dBFS)");
}

static void testAllocations()
{
    std::cout << "\nRealtime safety" << std::endl;
    Harness h;
    auto& p = *h.processor;
    h.settle();

    ProgramSignal signal (h.sampleRate);
    auto fill = [&] (float& l, float& r) { signal.next (l, r); };

    audioThreadAllocations = 0;
    audioThreadFrees = 0;

    for (int i = 0; i < 12; ++i)
    {
        if (i % 3 == 0) p.selectRoom (i % 2);
        if (i % 3 == 1) p.stepIr (1);
        if (i % 3 == 2) p.getPresets().next();
        h.set (directIDs.length(), 5.0f + (float) i * 7.0f);
        h.render (0.25, fill, [] (float, float) {});
        h.settle (0.1);
    }

    check (audioThreadAllocations == 0 && audioThreadFrees == 0,
           "no heap allocation or freeing inside processBlock while switching ("
               + String (audioThreadAllocations.load()) + " allocs, " + String (audioThreadFrees.load()) + " frees)");
}

static void testAmbience()
{
    std::cout << "\nAmbience" << std::endl;
    Harness h;
    auto& p = *h.processor;
    p.applySelection ({ 0, 1, 0, 0 });   // Room 2, 80 s loop
    h.set (ParamIDs::ambLevel, 0.0f);
    h.settle();

    const auto* amb = &p.getLibrary().getRoom (0, 1)->ambiences.getReference (0);
    const auto loop = juce::SharedResourcePointer<AssetCache>()->getLoop (amb->file, h.sampleRate);
    const auto loopLength = loop->buffer.getNumSamples();
    info ("loop length at 48 kHz: " + String (loopLength) + " samples (" + String (loopLength / h.sampleRate, 3) + " s)");

    // With silent input the output is pure ambience. Record ~2 loops.
    std::vector<float> out;
    out.reserve ((size_t) (loopLength * 2 + h.sampleRate * 2));
    h.render ((loopLength * 2.0) / h.sampleRate + 1.0, [] (float& l, float& r) { l = r = 0.0f; },
              [&] (float l, float) { out.push_back (l); });

    double sumSq = 0;
    for (auto v : out) sumSq += (double) v * v;
    const auto rmsDb = 10.0 * std::log10 (sumSq / (double) out.size() + 1e-20);
    check (rmsDb > -60.0, "ambience plays with no input: " + String (rmsDb, 1) + " dBFS RMS");

    // The output is periodic in loopLength: find a seam and compare the jump
    // there with ordinary sample-to-sample steps.
    std::vector<float> steps;
    for (size_t i = 1; i < out.size(); ++i)
        steps.push_back (std::abs (out[i] - out[i - 1]));

    auto sorted = steps;
    std::nth_element (sorted.begin(), sorted.begin() + (long) (sorted.size() * 999 / 1000), sorted.end());
    const auto p999 = sorted[sorted.size() * 999 / 1000];

    // The output is periodic, so out[n] == out[n + loopLength] away from start-up.
    double maxPeriodicError = 0;
    for (size_t i = (size_t) h.sampleRate; i + (size_t) loopLength < out.size(); i += 7)
        maxPeriodicError = jmax (maxPeriodicError, (double) std::abs (out[i] - out[i + (size_t) loopLength]));

    check (maxPeriodicError < 1.0e-4, "loops sample-accurately (max error between passes " + String (maxPeriodicError, 7) + ")");

    float seamWorst = 0;
    for (size_t i = (size_t) h.sampleRate; i < out.size(); ++i)
        if ((i + 1) % (size_t) loopLength < 4)   // around each wrap point (phase is arbitrary; check all candidates)
            seamWorst = jmax (seamWorst, steps[i - 1]);

    info ("largest step near a wrap " + String (seamWorst, 5) + ", 99.9th percentile step " + String (p999, 5));

    // Ambience off: level fades out.
    h.set (ParamIDs::ambOn, 0.0f);
    double offSq = 0;
    h.render (0.5, [] (float& l, float& r) { l = r = 0.0f; }, [] (float, float) {});
    h.render (0.5, [] (float& l, float& r) { l = r = 0.0f; }, [&] (float l, float) { offSq += (double) l * l; });
    check (offSq < 1.0e-12, "ambience off is silent");
}

static void testPerformance()
{
    std::cout << "\nCPU" << std::endl;

    for (auto [sr, block] : { std::pair { 48000.0, 512 }, std::pair { 48000.0, 64 }, std::pair { 96000.0, 256 } })
    {
        Harness h (sr, block);
        h.processor->applySelection ({ 0, 0, 7, 0 });   // a long IR
        h.set (directIDs.drive(), 30.0f);
        h.set (roomIDs.drive(), 30.0f);
        h.set (roomIDs.b1Gain(), 3.0f);
        h.settle();

        ProgramSignal signal (sr);
        const auto start = Time::getHighResolutionTicks();
        const double seconds = 10.0;
        h.render (seconds, [&] (float& l, float& r) { signal.next (l, r); }, [] (float, float) {});
        const auto elapsed = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - start);
        info (String (sr / 1000.0, 1) + " kHz, " + String (block) + "-sample blocks: " + String (100.0 * elapsed / seconds, 1)
              + "% of one core");
    }
}

static void testSampleRates()
{
    std::cout << "\nSample rates" << std::endl;

    for (auto sr : { 44100.0, 88200.0, 96000.0, 192000.0 })
    {
        Harness h (sr, 256);
        h.settle();
        ProgramSignal signal (sr);
        bool finite = true;
        double sumSq = 0;
        h.render (2.0, [&] (float& l, float& r) { signal.next (l, r); },
                  [&] (float l, float r) { finite = finite && std::isfinite (l) && std::isfinite (r); sumSq += (double) l * l; });
        check (finite && sumSq > 0.0, String (sr / 1000.0, 1) + " kHz renders");
    }
}

//==============================================================================
static void writeWav (const File& f, const AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (f);
    auto w = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Renders a few signals to WAV files for listening / offline analysis.
static void dump (const File& folder)
{
    folder.createDirectory();

    for (auto [name, ambienceOn, sine] : { std::tuple { "sine-wet", false, true }, std::tuple { "program-wet", false, false },
                                           std::tuple { "program-with-ambience", true, false } })
    {
        Harness h;
        h.set (ParamIDs::ambOn, ambienceOn ? 1.0f : 0.0f);
        h.settle();
        ProgramSignal signal (h.sampleRate);
        double phase = 0;
        AudioBuffer<float> in (2, (int) (h.sampleRate * 10)), out (2, in.getNumSamples());
        int n = 0, m = 0;
        h.render (10.0, [&] (float& l, float& r)
                  {
                      if (sine) { l = r = (float) (0.25 * std::sin (phase)); phase += MathConstants<double>::twoPi * 220.0 / h.sampleRate; }
                      else signal.next (l, r);
                      if (n < in.getNumSamples()) { in.setSample (0, n, l); in.setSample (1, n, r); ++n; }
                  },
                  [&] (float l, float r) { if (m < out.getNumSamples()) { out.setSample (0, m, l); out.setSample (1, m, r); ++m; } });
        writeWav (folder.getChildFile (String (name) + "-in.wav"), in, h.sampleRate);
        writeWav (folder.getChildFile (String (name) + "-out.wav"), out, h.sampleRate);
    }
}

// Renders the editor to PNGs (default view, Room 2, EQ drawer, 150%).
static void screenshots (const File& folder)
{
    folder.createDirectory();
    Harness h;
    std::unique_ptr<AudioProcessorEditor> editor (h.processor->createEditor());
    editor->setVisible (true);

    const auto shoot = [&] (const String& name)
    {
        for (int i = 0; i < 20; ++i)
        {
            MessageManager::getInstance()->runDispatchLoopUntil (30);
            h.renderSilence (0.03);
        }

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        auto file = folder.getChildFile (name + ".png");
        file.deleteFile();
        FileOutputStream out (file);
        PNGImageFormat().writeImageToStream (image, out);
        std::cout << "wrote " << file.getFullPathName() << std::endl;
    };

    shoot ("ui-room1");
    h.processor->selectRoom (1);
    shoot ("ui-room2");

    for (auto* c : editor->getChildren().getFirst()->getChildren())
        if (auto* b = dynamic_cast<TextButton*> (c); b != nullptr && b->getButtonText() == "EQ")
            b->triggerClick();

    shoot ("ui-eq");
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const bool quick = argc > 1 && String (argv[1]) == "--quick";

    setenv ("SPACES_PACKS_DIR", SPACES_TEST_PACKS_DIR, 1);
    // Keep test runs away from the real user folders.
    setenv ("HOME", File::getSpecialLocation (File::tempDirectory).getChildFile ("spaces-test-home").getFullPathName().toRawUTF8(), 1);

    if (argc > 2 && String (argv[1]) == "--screenshots")
    {
        setenv ("SPACES_PACKS_DIR", SPACES_TEST_PACKS_DIR, 1);
        screenshots (File (argv[2]));
        return 0;
    }

    if (argc > 2 && String (argv[1]) == "--dump")
    {
        dump (File (argv[2]));
        return 0;
    }

    testLibrary();
    testSelectionAndState();
    testAmbience();
    testAllocations();
    testClicks (quick);
    testGainMatch (quick);
    testSampleRates();
    testPerformance();

    std::cout << "\n" << (failures == 0 ? "All checks passed" : String (failures) + " check(s) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
