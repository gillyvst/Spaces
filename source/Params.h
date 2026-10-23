#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs. These are saved in sessions and presets, so never rename one.
namespace ParamIDs
{
    // Selection (indices into the loaded space packs, 1-based for display)
    inline constexpr auto space    = "sel_space";
    inline constexpr auto room     = "sel_room";
    inline constexpr auto ir       = "sel_ir";
    inline constexpr auto ambience = "sel_amb";

    // Ambience
    inline constexpr auto ambOn    = "amb_on";
    inline constexpr auto ambLevel = "amb_level";

    // Global
    inline constexpr auto mix      = "mix";
    inline constexpr auto autoGain = "auto_gain";
    inline constexpr auto outTrim  = "out_trim";
    inline constexpr auto mono     = "mono";
    inline constexpr auto bypass   = "bypass";
}

inline constexpr int maxSpaces     = 32;
inline constexpr int maxRooms      = 8;
inline constexpr int maxIRs        = 32;
inline constexpr int maxAmbiences  = 16;

inline constexpr float minLevelDb  = -60.0f;   // treated as -inf

// The two convolution layers share the same parameter set apart from
// Length (direct) and Size (room). IDs are "<prefix>_<name>", e.g. "direct_level".
struct LayerParamIDs
{
    explicit LayerParamIDs (juce::String p) : prefix (std::move (p)) {}

    juce::String id (const char* name) const { return prefix + "_" + name; }

    juce::String level()  const { return id ("level"); }
    juce::String length() const { return id ("length"); }   // direct layer only
    juce::String size()   const { return id ("size"); }     // room layer only
    juce::String drive()  const { return id ("drive"); }
    juce::String pan()    const { return id ("pan"); }

    juce::String lsFreq() const { return id ("eq_ls_freq"); }
    juce::String lsGain() const { return id ("eq_ls_gain"); }
    juce::String b1Freq() const { return id ("eq_b1_freq"); }
    juce::String b1Gain() const { return id ("eq_b1_gain"); }
    juce::String b1Q()    const { return id ("eq_b1_q"); }
    juce::String b2Freq() const { return id ("eq_b2_freq"); }
    juce::String b2Gain() const { return id ("eq_b2_gain"); }
    juce::String b2Q()    const { return id ("eq_b2_q"); }
    juce::String hsFreq() const { return id ("eq_hs_freq"); }
    juce::String hsGain() const { return id ("eq_hs_gain"); }

    juce::String prefix;
};

inline const LayerParamIDs directIDs { "direct" };
inline const LayerParamIDs roomIDs   { "room" };

inline float levelDbToGain (float db) noexcept
{
    return db <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}
