#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace ui
{
namespace colours
{
inline const juce::Colour bg { 0xff17151f };
inline const juce::Colour panel { 0xff221f2e };
inline const juce::Colour line { 0xff3a3550 };
inline const juce::Colour text { 0xffece8f5 };
inline const juce::Colour dim { 0xff9a93b0 };
inline const juce::Colour wet { 0xfff2a65a };
inline const juce::Colour loop { 0xff5ad1c4 };
inline const juce::Colour centre { 0xffb49cff };
} // namespace colours

class MurmurLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MurmurLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
};

// Rotary knob with a caption above and the value below
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& caption, juce::Colour accent);
    void resized() override;
    void setCaption (const juce::String& c) { label.setText (c, juce::dontSendNotification); }
    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A row of radio buttons bound to a choice parameter
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, juce::Colour accent);
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Big on-screen "footswitch". Toggle style (Wet, Loop) or hold style (Freeze, Dub),
// where hold becomes a latch when the Latch switch is on.
class Footswitch : public juce::Component
{
public:
    enum class Style { Toggle, Hold };
    Footswitch (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& text, juce::Colour accent, Style style);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String text;
    juce::Colour accent;
    Style style;
    bool on = false, pressed = false, holding = false;
};

// Live view of the Micro-Looper buffer
class BufferView : public juce::Component
{
public:
    explicit BufferView (MurmurProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void refresh();

private:
    MurmurProcessor& proc;
    std::vector<float> peaks = std::vector<float> (240, 0.0f);
    float playFrac = 0.0f;
    bool playing = false, dubbing = false;
    float fill = 0.0f;
};

// A labelled switch for the Deep page
class Switch : public juce::Component
{
public:
    Switch (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& name, const juce::String& help);
    void resized() override;

private:
    juce::ToggleButton button;
    juce::Label help;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// A labelled dropdown for the Deep page
class Picker : public juce::Component
{
public:
    Picker (juce::AudioProcessorValueTreeState& s, const juce::String& paramID, const juce::String& name);
    void resized() override;

private:
    juce::Label label;
    juce::ComboBox box;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
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
    void showPage (int page);

    MurmurProcessor& proc;
    ui::MurmurLookAndFeel lnf;

    juce::TextButton panelTab { "Panel" }, deepTab { "Deep" };
    int page = 0;

    // ---- panel page ----
    ui::Segmented wetMode, loopMode, routing;
    ui::Knob time, wetModify, mix, clock, length, loopModify;
    ui::Footswitch wetSwitch, freezeSwitch, loopSwitch, dubSwitch;
    ui::BufferView bufferView;
    juce::Label clockReadout;

    // ---- deep page ----
    ui::Knob width, tone, fade, direct, balance, inGain, outGain, bufferSeconds;
    ui::Picker sync, spreadSolo, stepSet;
    juce::OwnedArray<ui::Switch> switches;

    juce::Rectangle<int> wetArea, centreArea, loopArea, deepArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurmurEditor)
};
