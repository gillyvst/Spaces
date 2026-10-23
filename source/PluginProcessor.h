#pragma once

#include "AssetCache.h"
#include "Params.h"
#include "PresetManager.h"
#include "SpacePack.h"
#include "dsp/AmbiencePlayer.h"
#include "dsp/ConvolutionLayer.h"
#include "dsp/GainMatch.h"

// Both layers' IRs, built together from the same capture.
struct IRSet
{
    LayerIR direct, room;
    bool hasDirect = false, hasRoom = false;
};

// Which space / room / IR / ambience is selected (0-based, -1 = none).
struct Selection
{
    int space = -1, room = -1, ir = -1, ambience = -1;

    bool operator== (const Selection&) const = default;
};

class SpacesProcessor final : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    SpacesProcessor();
    ~SpacesProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    // Message-thread API used by the editor and preset manager.
    const PackLibrary& getLibrary() const noexcept { return library; }
    PresetManager& getPresets() noexcept           { return presets; }
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }

    Selection getSelection() const;
    const SpacePack* getCurrentSpace() const;
    const RoomDef* getCurrentRoom() const;

    void selectRoom (int roomIndex);               // also picks the room's default IR + ambience
    void selectIr (int irIndex);
    void selectAmbience (int ambienceIndex);
    void stepIr (int delta);
    void stepAmbience (int delta);

    // Sets the whole selection at once (presets, restore).
    void applySelection (const Selection&);

    // Resets every parameter to its default, then applies the space's
    // manifest defaults. Selection parameters are left alone.
    void resetParametersToDefaults (const SpacePack* space);

    juce::ValueTree createStateTree();
    void restoreStateTree (const juce::ValueTree&);

    float getVuLevel (int channel) const noexcept   { return vuLevel[(size_t) juce::jlimit (0, 1, channel)].load(); }
    float getGainMatchDb() const noexcept           { return gainMatchDb.load(); }
    bool isAmbienceAudible() const;

    float uiScale = 1.0f;   // saved with the session

    // Test hooks
    bool isFullyLoaded() const noexcept;
    void pollNow() { timerCallback(); }

private:
    //==========================================================================
    struct LoadRequest
    {
        Selection selection;
        float directLengthMs = 10.0f, roomSizePct = 100.0f;
        double sampleRate = 0.0;
        int generation = 0;
    };

    class Loader;

    void timerCallback() override;
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;
    void updateDspTargets() noexcept;
    void setParamIndex (const juce::String& id, int zeroBasedIndex);
    int readParamIndex (const char* id) const;
    Selection clampSelection (Selection) const;
    Selection currentParamSelection() const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const PackLibrary&, SpacesProcessor&);

    //==========================================================================
    PackLibrary library;
    juce::SharedResourcePointer<AssetCache> cache;
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    spaces_dsp::ConvolutionMessageQueue convolutionQueue;
    ConvolutionLayer direct, room;
    GainMatch gainMatch;
    AmbiencePlayer ambience;
    Mailbox<IRSet> irMailbox;
    std::unique_ptr<Loader> irLoader, ambienceLoader;

    juce::AudioBuffer<float> dryBuffer, wetBuffer;
    juce::SmoothedValue<float> mixSmoothed, monoSmoothed, trimSmoothed, bypassSmoothed;
    double vuState[2][2] {};
    double vuCoefficient = 0.0;
    std::array<std::atomic<float>, 2> vuLevel {};
    std::atomic<float> gainMatchDb { 0.0f };
    std::atomic<double> currentSampleRate { 0.0 };
    int maxBlockSize = 512;

    Selection applied;
    std::atomic<int> requestedGeneration { 0 };
    LoadRequest lastRequest;

    struct LayerParams
    {
        std::atomic<float>* level, *shape, *drive, *pan;
        std::atomic<float>* lsFreq, *lsGain, *b1Freq, *b1Gain, *b1Q, *b2Freq, *b2Gain, *b2Q, *hsFreq, *hsGain;
    };

    LayerParams directParams {}, roomParams {};
    std::atomic<float>* ambOn = nullptr, *ambLevel = nullptr, *mix = nullptr, *autoGain = nullptr,
                      *outTrim = nullptr, *mono = nullptr, *bypass = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacesProcessor)
};
