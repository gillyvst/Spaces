#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class SpacesProcessor;

// Factory presets come from each space pack's manifest; user presets are
// files in ~/Music/Spaces/Presets. Previous / next walk the combined list.
// All methods are message-thread only.
class PresetManager : public juce::ChangeBroadcaster
{
public:
    struct Entry
    {
        juce::String name;
        bool isFactory = true;
        int pack = -1, packPreset = -1;   // factory presets
        juce::File file;                  // user presets
    };

    explicit PresetManager (SpacesProcessor&);

    void refresh();

    const juce::Array<Entry>& getEntries() const noexcept { return entries; }
    int getCurrentIndex() const noexcept                  { return currentIndex; }
    juce::String getCurrentName() const;

    void load (int index);
    void next()     { step (1); }
    void previous() { step (-1); }

    // Saves the current settings as a user preset. Returns an error message, or empty on success.
    juce::String saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int index);

    static juce::File getUserPresetFolder();
    static constexpr const char* fileExtension = ".spacespreset";

    // Used when restoring a session: remembers which preset was last loaded.
    void setCurrentByName (const juce::String& name);

private:
    void step (int delta);
    bool loadStateFile (const juce::File&);

    SpacesProcessor& processor;
    juce::Array<Entry> entries;
    int currentIndex = -1;
    juce::String currentName;
};
