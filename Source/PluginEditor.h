#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette, shared across all of Kevin's builds.
namespace ff
{
const juce::Colour bg        { 0xff050505 };
const juce::Colour face      { 0xff0d0d0d };
const juce::Colour housing   { 0xff060606 };
const juce::Colour card      { 0xff1b1b1b };
const juce::Colour cardHi    { 0xff252525 };
const juce::Colour cardLow   { 0xff141414 };
const juce::Colour ink       { 0xfff3f0e8 };
const juce::Colour dim       { 0xffa29e95 };
const juce::Colour faint     { 0xff5a5852 };
const juce::Colour line      { 0xff3a3a3a };
const juce::Colour ring      { 0xff1a1a1a };
const juce::Colour pin       { 0xff3c3c3c };
const juce::Colour red       { 0xffe8392f };
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };

juce::Font font (float height, float kerning = 0.04f);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 48.0f;
};

// Rounded pill buttons. Bound to a choice parameter, or free-standing (page tabs).
class Pills : public juce::Component
{
public:
    Pills (juce::StringArray labels);
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setSelected (int i) { selected = i; repaint(); }
    std::function<void (int)> onPick;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A small flip card that reads ON or OFF, with its name beside it (or inside it).
class CardToggle : public juce::Component
{
public:
    enum class Style { OnOffBeside, NameInside };
    CardToggle (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name, Style);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String name;
    Style style;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
};

// A knob drawn as a small clock face, with its name above and value below.
class Dial : public juce::Component
{
public:
    Dial (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void setModulated (float position, bool active) { modPos = position; modActive = active; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    float modPos = 0.0f;
    bool modActive = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A Pitch Polish style slider with its name on the left and value on the right.
class LabelledSlider : public juce::Component
{
public:
    LabelledSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;

private:
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The Clock: a Karlsson dial with the pedal's 11 stops and the red second hand as the needle.
class ClockFace : public juce::Component
{
public:
    ClockFace (juce::AudioProcessorValueTreeState&, MurmurProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    MurmurProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float value = 1.0f, dragStart = 1.0f;
};

// The pedal's footswitch: tap toggles the channel, hold freezes or overdubs.
class Footswitch : public juce::Component, private juce::Timer
{
public:
    Footswitch (juce::AudioProcessorValueTreeState&, const juce::String& mainId, const juce::String& holdId,
                const juce::String& name, const juce::String& tapText, const juce::String& holdText, const juce::String& holdingText);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void set (juce::RangedAudioParameter*, bool);
    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter *mainParam = nullptr, *holdParam = nullptr;
    juce::String name, tapText, holdText, holdingText;
    bool pressed = false, held = false;
    double downAt = 0;
};

// A / LIVE / B, like the pedal's preset toggle. Hold A or B to save into it.
class PresetToggle : public juce::Component, private juce::Timer
{
public:
    PresetToggle (MurmurProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> cell (int i) const;
    MurmurProcessor& proc;
    int pressedCell = -1;
    double downAt = 0, savedFlashUntil = 0;
    int savedCell = -1;
    bool saved = false;
};

// Ramping waveform cards: Triangle, Square, Sine, Random, Smooth random.
class WaveCards : public juce::Component
{
public:
    WaveCards (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float value = 0.0f;
};

// Pebble and the loop buffer.
class BufferStrip : public juce::Component
{
public:
    explicit BufferStrip (MurmurProcessor& p) : proc (p) {}
    void update (double now, double dt);
    void paint (juce::Graphics&) override;

private:
    void paintPebble (juce::Graphics&, juce::Rectangle<float> area);
    MurmurProcessor& proc;
    std::vector<float> peaks = std::vector<float> (44, 0.0f);
    float playFrac = 0, fill = 0, level = 0, look = 0, mouth = 0, hop = 0;
    bool playing = false, dubbing = false, frozen = false, asleep = false, blinking = false;
    double lastSound = 0, nextBlink = 2.0, blinkUntil = 0, now = 0;
    int lastNote = -1;
    juce::String status { "LISTENING" }, detail;
};
} // namespace ui

class MurmurEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MurmurEditor (MurmurProcessor&);
    ~MurmurEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int);
    void section (juce::Graphics&, juce::Rectangle<int>) const;

    MurmurProcessor& proc;
    ui::FlipLookAndFeel lnf;
    int page = 0;
    double lastTick = 0;

    ui::Pills pages { { "PANEL", "DEEP", "MOTION" } };
    ui::PresetToggle presets;

    // ---- panel ----
    ui::FlipCard wetCard, loopCard, rateCard, khzCard;
    ui::Pills wetMode, loopMode, routing;
    ui::Dial time, wetModify, length, loopModify;
    ui::ClockFace clock;
    ui::LabelledSlider mix;
    ui::Footswitch wetSwitch, loopSwitch;
    ui::BufferStrip strip;
    juce::String interval;

    // ---- deep ----
    ui::Dial width, tone, fade, direct, balance;
    ui::Pills sync, spreadSolo, halfLength, stepSet;
    juce::TextButton resetHidden { "RESET ALL" };
    juce::OwnedArray<ui::CardToggle> switches;
    ui::LabelledSlider inGain, outGain, bufferSeconds;

    // ---- motion ----
    juce::OwnedArray<ui::CardToggle> targets;
    ui::Pills bounce, sweep, modSource, expPolarity, tempoSync, synthGate;
    ui::LabelledSlider rampSpeed, rampWave, expression;
    ui::WaveCards waveCards;
    ui::FlipCard bpmCard, noteCard, octaveCard;
    juce::TextButton tap { "TAP TEMPO" }, rootDown { "-" }, rootUp { "+" };
    ui::CardToggle synthEnable;
    ui::LabelledSlider attack, decay, sustain, release, glide;

    juce::Array<juce::Component*> panelComps, deepComps, motionComps;
    juce::Rectangle<int> secA, secB, secC, secD;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurmurEditor)
};
