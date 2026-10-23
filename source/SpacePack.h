#pragma once

#include <juce_core/juce_core.h>

// A space pack is a folder holding a space.json manifest plus the audio and
// image files it references. Packs are found by scanning a list of folders,
// so new spaces are added by dropping in a folder: no code changes.

struct IRDef
{
    enum class Type { trueStereo, stereo };

    juce::String id, name;
    Type type = Type::trueStereo;
    juce::File left, right;   // trueStereo: the left / right speaker captures
    juce::File file;          // stereo: a single file, L->L and R->R
    bool swapLR = false;      // swap mic channels (e.g. mics facing the crowd)
};

struct AmbienceDef
{
    juce::String id, name;
    juce::File file;
    float gainDb = 0.0f;
};

struct HeroDef
{
    juce::File still;
    juce::File frameFolder;   // optional image sequence (future animated hero)
    double fps = 24.0;
};

struct RoomDef
{
    juce::String id, name;
    HeroDef hero;
    juce::Array<IRDef> irs;
    juce::Array<AmbienceDef> ambiences;
    int defaultIr = 0, defaultAmbience = 0;

    int indexOfIr (const juce::String& irId) const;
    int indexOfAmbience (const juce::String& ambId) const;
};

struct PackPreset
{
    juce::String name, roomId, irId, ambienceId;
    juce::NamedValueSet params;   // param ID -> value in real units
};

struct SpacePack
{
    juce::String id, name, location;
    juce::File folder;
    juce::Array<RoomDef> rooms;
    juce::NamedValueSet defaults;   // param ID -> value in real units
    juce::Array<PackPreset> presets;

    int indexOfRoom (const juce::String& roomId) const;

    // Returns an error message, or an empty string on success.
    static juce::String load (const juce::File& manifest, SpacePack& result);
};

class PackLibrary
{
public:
    // Folders searched for packs, in priority order (first pack with a given
    // id wins, so a user-installed copy overrides the bundled one).
    static juce::Array<juce::File> getSearchFolders();
    static juce::File getUserPacksFolder();

    void scan (const juce::Array<juce::File>& folders);

    const juce::Array<SpacePack>& getPacks() const noexcept { return packs; }
    int getNumPacks() const noexcept                      { return packs.size(); }
    const SpacePack* getPack (int index) const noexcept   { return isPositiveAndBelow (index, packs.size()) ? &packs.getReference (index) : nullptr; }
    const RoomDef* getRoom (int pack, int room) const noexcept;
    int indexOfPack (const juce::String& packId) const;

    const juce::StringArray& getErrors() const noexcept   { return errors; }

private:
    static bool isPositiveAndBelow (int v, int n) noexcept { return v >= 0 && v < n; }

    juce::Array<SpacePack> packs;
    juce::StringArray errors;
};
