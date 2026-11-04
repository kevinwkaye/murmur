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
const juce::Colour murkBlob  { 0xff3a46c4 };   // Pebble in Murk: a murkier blue
const juce::Colour murkShade { 0xff1d2266 };
const juce::Colour green     { 0xff5fd38a };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };

juce::Font font (float height, float kerning = 0.04f);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
// "+5TH", "−OCT", "UNISON": the interval for a number of semitones
juce::String intervalName (int semis);
// "REV −OCT −12": what a Slip or Tape speed step shows on its flip card
juce::String stepText (int semis, bool reverse);

// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    const juce::String& getValue() const { return current; }
    bool isFlipping() const { return progress < 1.0f; }
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
    // Shows the value on a small flip card instead of plain text (Slip and Tape steps). Empty = plain text.
    void setStepCard (const juce::String& text);
    const juce::String& getStepCard() const { return stepCard.getValue(); }
    void advance (double dt) { stepCard.advance (dt); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FlipCard stepCard;
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

// The pedal's footswitch on screen. It presses footswitch A or B exactly as the real one
// does, so tap, hold and Latch behave the same with or without pedals.
class Footswitch : public juce::Component
{
public:
    Footswitch (MurmurProcessor&, int sw, const juce::String& name, const juce::String& tapText, const juce::String& holdText);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    MurmurProcessor& proc;
    int sw;
    juce::String name, tapText, holdText;
    bool pressed = false;
};

// A flip card you can click (the mode card, KEEP)
class CardButton : public juce::Component
{
public:
    CardButton();
    void resized() override { card.setBounds (getLocalBounds()); }
    void mouseUp (const juce::MouseEvent&) override;
    FlipCard card;
    std::function<void (const juce::MouseEvent&)> onClick;
};

// One footswitch on the Pedal page: its letter, what it reads now, its tap and hold jobs and Learn.
// Clicking the letter or state card presses it, so the on-screen page is a footswitch too.
class PedalCard : public juce::Component
{
public:
    PedalCard (MurmurProcessor&, int sw);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick (double dt, int tableMode);

private:
    MurmurProcessor& proc;
    int sw;
    FlipCard letter, state, tap, hold;
    juce::TextButton learn { "LEARN" };
    juce::String sends;
    bool pressed = false;
};

// The expression pedal: what it moves, where it is, calibration, Learn and curve
class ExpressionPanel : public juce::Component
{
public:
    explicit ExpressionPanel (MurmurProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick (double dt);

private:
    void targetMenu();
    MurmurProcessor& proc;
    FlipCard target;
    juce::TextButton change { "CHANGE" }, calibrate { "CALIBRATE" }, learn { "LEARN" };
    Pills curve { { "EVEN", "SLOW START", "FAST START" } };
    float shown = -1.0f;
};

// The Pedal page's plain-words overlay: every footswitch action in both modes
class ActionsOverlay : public juce::Component
{
public:
    ActionsOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::TextButton close { "CLOSE" };
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

// Pebble from Flip Fella. Calm in Murmur, grumpy in Murk. Reacts to the audio,
// footswitch presses, the expression pedal, Keeper and mode switches.
class Pebble
{
public:
    explicit Pebble (MurmurProcessor& p) : proc (p) {}
    void update (double now, double dt, bool playing, bool dubbing, bool frozen, bool recording, float level, float look);
    void paint (juce::Graphics&, juce::Rectangle<float> area) const;
    bool isAsleep() const { return asleep; }

private:
    MurmurProcessor& proc;
    float mouth = 0, hop = 0, nod = 0, flip = 0, shake = 0, lean = 0, look = 0, tap = 0;
    float murk = 0;   // 0 Murmur's face, 1 Murk's
    bool playing = false, dubbing = false, frozen = false, recording = false, asleep = false, blinking = false;
    double lastSound = 0, nextBlink = 2.0, blinkUntil = 0, now = 0;
    int lastNote = -1, seenFoot = 0, seenKeep = 0, seenMode = 0;
    bool intoMurk = true;
};

// Pebble and the loop buffer. In Murk's Radio the bars give way to the radio dial.
class BufferStrip : public juce::Component
{
public:
    explicit BufferStrip (MurmurProcessor& p) : proc (p), pebble (p) {}
    void update (double now, double dt);
    void paint (juce::Graphics&) override;

private:
    void paintRadio (juce::Graphics&, juce::Rectangle<float> area) const;
    MurmurProcessor& proc;
    Pebble pebble;
    std::vector<float> peaks = std::vector<float> (44, 0.0f);
    float playFrac = 0, fill = 0, level = 0, look = 0, tuning = 0;
    bool playing = false, dubbing = false, frozen = false, recording = false, radio = false;
    double now = 0;
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

    // Used by the plugin tests: run one display refresh and read what the cards show.
    void refreshNow() { timerCallback(); }
    void showPage (int);
    const ui::FlipCard& tempoCard() const { return bpmCard; }
    const ui::FlipCard& modeCard() const { return modeButton.card; }
    const ui::FlipCard& wetModeCard() const { return wetCard; }
    const ui::FlipCard& loopModeCard() const { return loopCard; }
    const ui::Dial& wetModifyDial() const { return wetModify; }
    const ui::Dial& loopModifyDial() const { return loopModify; }
    const ui::Dial& lengthDial() const { return length; }
    bool overlayShowing() const { return overlay.isVisible(); }
    void showOverlay (bool show);

    enum Page { PanelPage = 0, DeepPage = 1, MotionPage = 2, PedalPage = 3 };

private:
    void timerCallback() override;
    void section (juce::Graphics&, juce::Rectangle<int>) const;
    void updateVisibility();
    void keeperMenu();
    void closeNote (bool save);

    MurmurProcessor& proc;
    ui::FlipLookAndFeel lnf;
    int page = 0, shownMode = -1, pedalTable = -1;
    double lastTick = 0;

    ui::Pills pages { { "PANEL", "DEEP", "MOTION", "PEDAL" } };
    ui::PresetToggle presets;
    ui::CardButton modeButton, keepButton;
    juce::TextEditor noteEditor;
    double noteUntil = 0.0, keptUntil = 0.0;
    int noteNumber = 0, seenKeep = 0;

    // ---- panel ----
    ui::FlipCard wetCard, loopCard, rateCard, khzCard;
    ui::Pills wetMode, loopMode, murkWet, murkLoop, routing;
    ui::Dial time, wetModify, length, loopModify;
    ui::ClockFace clock;
    ui::LabelledSlider mix, glue;
    ui::Footswitch wetSwitch, loopSwitch;
    ui::BufferStrip strip;
    juce::String interval;

    // ---- deep ----
    ui::Dial width, tone, eqTilt, fade, direct, blend, balance, glueDial, crossDial, volume;
    ui::Pills sync, spreadSolo, halfLength, stepSet, crossSource, eqOn;
    juce::TextButton resetHidden { "RESET ALL" };
    juce::OwnedArray<ui::CardToggle> switches, murkSwitches;
    ui::LabelledSlider inGain, outGain, bufferSeconds;
    juce::Array<juce::Component*> murmurDeep, murkDeep;

    // ---- pedal ----
    juce::OwnedArray<ui::PedalCard> pedalCards;
    ui::Pills tableMode { { "MURMUR", "MURK" } };
    juce::TextButton showActions { "SHOW ACTIONS" }, tryBoth { "TRY" }, tryMode { "TRY" }, tryKeep { "TRY" }, keepLearn { "LEARN" },
                     footDevice { "FIND AUTOMATICALLY" }, pedalMap { "OFF" }, keepExport { "EXPORT" }, footDefaults { "DEFAULTS" };
    ui::Pills keeperContext { { "STUDIO", "LIVE" } };
    ui::ExpressionPanel expressionPanel;
    ui::ActionsOverlay overlay;
    juce::Array<juce::Component*> pedalComps;

    // ---- motion ----
    juce::OwnedArray<ui::CardToggle> targets;
    ui::Pills bounce, sweep, modSource, expPolarity, tempoSync, synthGate;
    ui::LabelledSlider rampSpeed, rampWave, expression;
    ui::WaveCards waveCards;
    ui::FlipCard bpmCard, noteCard, octaveCard;
    juce::TextButton tap { "TAP TEMPO" }, rootDown { "-" }, rootUp { "+" };
    ui::CardToggle synthEnable;
    ui::LabelledSlider attack, decay, sustain, release, glide;

    juce::Array<juce::Component*> panelComps, deepComps, motionComps, murmurPanel, murkPanel;
    juce::Rectangle<int> secA, secB, secC, secD;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurmurEditor)
};
