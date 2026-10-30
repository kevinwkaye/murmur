#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Every parameter Murmur exposes. All are automatable and saved with the session,
// but none needs host automation to be useful.
namespace ParamIDs
{
// front panel
inline constexpr auto mix = "mix";
inline constexpr auto clock = "clock";
inline constexpr auto routing = "routing";
inline constexpr auto wetOn = "wetOn";
inline constexpr auto freeze = "freeze";
inline constexpr auto wetMode = "wetMode";
inline constexpr auto time = "time";
inline constexpr auto wetModify = "wetModify";
inline constexpr auto loopOn = "loopOn";
inline constexpr auto overdub = "overdub";
inline constexpr auto loopMode = "loopMode";
inline constexpr auto length = "length";
inline constexpr auto loopModify = "loopModify";

// hidden options
inline constexpr auto width = "width";
inline constexpr auto tone = "tone";
inline constexpr auto fade = "fade";
inline constexpr auto direct = "direct";
inline constexpr auto balance = "balance";
inline constexpr auto sync = "sync";
inline constexpr auto halfLength = "halfLength";
inline constexpr auto spreadSolo = "spreadSolo";

// switches
inline constexpr auto miso = "miso";
inline constexpr auto spread = "spread";
inline constexpr auto dryKill = "dryKill";
inline constexpr auto trails = "trails";
inline constexpr auto latch = "latch";
inline constexpr auto noDub = "noDub";
inline constexpr auto smoothClock = "smoothClock";
inline constexpr auto classic = "classic";

// plugin extras
inline constexpr auto inGain = "inGain";
inline constexpr auto outGain = "outGain";
inline constexpr auto bufferSeconds = "bufferSeconds";
inline constexpr auto stepSet = "stepSet";
} // namespace ParamIDs

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    using B = AudioParameterBool;
    using C = AudioParameterChoice;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto knob = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<P> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def, pct));
    };
    auto toggle = [&] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<B> (ParameterID { id, 1 }, name, def));
    };
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    {
        layout.add (std::make_unique<C> (ParameterID { id, 1 }, name, items, def));
    };

    // ---- front panel ----
    knob (ParamIDs::mix, "Mix", 0.5f);
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::clock, 1 }, "Clock", NormalisableRange<float> (0.0f, 1.0f), 1.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                     {
                                         const float hz = 64000.0f * std::pow (2.0f, -5.0f * (1.0f - v));
                                         return String (hz / 1000.0f, 1) + " kHz";
                                     })));
    choice (ParamIDs::routing, "Routing", { "In", "Both", "Loop" }, 1);
    toggle (ParamIDs::wetOn, "Wet On", false);
    toggle (ParamIDs::freeze, "Freeze", false);
    choice (ParamIDs::wetMode, "Wet Mode", { "Reverb", "Delay", "Slip" }, 0);
    knob (ParamIDs::time, "Time", 0.5f);
    knob (ParamIDs::wetModify, "Wet Modify", 0.5f);
    toggle (ParamIDs::loopOn, "Loop On", false);
    toggle (ParamIDs::overdub, "Overdub", false);
    choice (ParamIDs::loopMode, "Loop Mode", { "Env", "Tape", "Stretch" }, 1);
    knob (ParamIDs::length, "Length", 1.0f);
    knob (ParamIDs::loopModify, "Loop Modify", 0.7f);

    // ---- hidden options ----
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::width, 1 }, "Stereo Width", NormalisableRange<float> (0.0f, 2.0f), 1.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::tone, 1 }, "Tone", NormalisableRange<float> (1000.0f, 20000.0f, 0.0f, 0.3f), 20000.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 19500.0f ? String ("Open") : String (v / 1000.0f, 1) + " kHz"; })));
    knob (ParamIDs::fade, "Fade", 1.0f);
    knob (ParamIDs::direct, "Direct Micro-Loop", 0.0f);
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::balance, 1 }, "Level Balance", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                     {
                                         if (std::abs (v) < 0.01f) return String ("Even");
                                         return v < 0 ? "Wet " + String (12.0f * v, 1) + " dB" : "Loop " + String (-12.0f * v, 1) + " dB";
                                     })));
    choice (ParamIDs::sync, "Channel Sync", { "Unsynced", "Loop follows Wet", "Wet follows Loop" }, 0);
    toggle (ParamIDs::halfLength, "Half Loop Length", false);
    choice (ParamIDs::spreadSolo, "Spread Solo", { "Both", "Wet only", "Loop only" }, 0);

    // ---- switches ----
    toggle (ParamIDs::miso, "MISO", false);
    toggle (ParamIDs::spread, "Spread", false);
    toggle (ParamIDs::dryKill, "Dry Kill", false);
    toggle (ParamIDs::trails, "Trails", true);
    toggle (ParamIDs::latch, "Latch", false);
    toggle (ParamIDs::noDub, "No Dub (Refresh)", false);
    toggle (ParamIDs::smoothClock, "Smooth Clock", false);
    toggle (ParamIDs::classic, "Classic", false);

    // ---- plugin extras ----
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::inGain, 1 }, "Input Gain", NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::outGain, 1 }, "Output Gain", NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::bufferSeconds, 1 }, "Max Buffer", NormalisableRange<float> (1.0f, 32.0f, 0.0f, 0.5f), 2.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; })));
    choice (ParamIDs::stepSet, "Clock Steps", { "Octaves", "Fourths & fifths", "Semitones" }, 1);

    return layout;
}
