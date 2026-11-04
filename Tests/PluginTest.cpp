// Plugin-level tests: run the real processor and editor with a fake host.
// Usage: murmur_plugin_test

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>
#include <map>
#include <set>

using namespace juce;

namespace
{
struct FakeHost : AudioPlayHead
{
    double bpm = 120.0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        return p;
    }
};

int failures = 0;
void expect (bool ok, const String& what)
{
    std::printf ("%s  %s\n", ok ? "pass" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

void setParam (MurmurProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

// Every test starts from the shipped footswitch defaults: CC 20 to 23 and CC 11 on channel 16
void defaultPedals (MurmurProcessor& p)
{
    auto& s = p.pedals();
    s.resetFootswitches();
    s.expChannel = 16; s.expCC = 11; s.heel = 0; s.toe = 127; s.curve = 0;
    s.pedalMapChannel = 0; s.keeperChannel = 0; s.keeperCC = -1;
    s.footDevice = "none"; // never grab a real MIDI device during tests
    s.save();
    p.setFootDevice ("none");
}

struct Rig
{
    FakeHost host;
    MurmurProcessor proc;
    AudioBuffer<float> buffer { 2, 128 };
    MidiBuffer midi;
    std::unique_ptr<MurmurEditor> editor;
    double clock = 100.0; // the footswitch clock, in seconds, moved by the tests

    Rig()
    {
        proc.setPlayHead (&host);
        proc.prepareToPlay (48000.0, 128);
        defaultPedals (proc);
    }
    float get (const char* id) { return proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getValue()); }
    bool isOn (const char* id) { return get (id) > 0.5f; }
    // a MIDI message arrives through Ableton, then time moves on
    void send (const MidiMessage& m, double after = 0.02)
    {
        midi.clear();
        midi.addEvent (m, 0);
        block();
        midi.clear();
        wait (after);
    }
    void wait (double seconds)
    {
        const double end = clock + seconds;
        while (clock < end) { clock = std::min (end, clock + 0.01); proc.applyPendingChanges (clock); }
    }
    void press (int sw, int channel = 16) { send (MidiMessage::controllerEvent (channel, 20 + sw, 127), 0.0); }
    void release (int sw, int channel = 16) { send (MidiMessage::controllerEvent (channel, 20 + sw, 0), 0.0); }
    void tapSwitch (int sw) { press (sw); wait (0.1); release (sw); wait (0.05); }
    void holdSwitch (int sw, double secs) { press (sw); wait (secs); release (sw); wait (0.05); }
    void comboHold (int a, int b, double secs) { press (a); wait (0.03); press (b); wait (secs); release (a); release (b); wait (0.05); }
    void block (int n = 1) { for (int i = 0; i < n; ++i) { buffer.clear(); proc.processBlock (buffer, midi); } }
    void openEditor (int page)
    {
        editor.reset (dynamic_cast<MurmurEditor*> (proc.createEditor()));
        editor->showPage (page);
        editor->refreshNow();
    }
    String shown() { editor->refreshNow(); return editor->tempoCard().getValue(); }
};

// ---- Bug 1: the tempo display must always show the tempo Murmur runs at ----
void tempoDisplayTests()
{
    {
        Rig r;
        r.openEditor (2);
        r.block();
        expect (r.shown() == "--", "sync off shows -- (got " + r.shown() + ")");

        r.host.bpm = 95.0;
        r.block();                                      // Ableton keeps running with Sync off
        expect (r.shown() == "--", "Ableton's tempo is not shown while sync is off (got " + r.shown() + ")");
        setParam (r.proc, ParamIDs::tempoSync, 1.0f);   // click Sync to Ableton
        expect (r.shown() == "95", "turning sync on shows Ableton's tempo straight away, with no new audio block (got " + r.shown() + ")");
        expect (r.editor->tempoCard().isFlipping(), "the card flips when sync turns on");

        r.block();
        expect (r.proc.currentBpm() == 95.0f, "the engine runs at Ableton's tempo");
        r.host.bpm = 140.0;
        r.block();
        expect (r.shown() == "140", "follows Ableton tempo changes while synced (got " + r.shown() + ")");
        expect (r.editor->tempoCard().isFlipping(), "the card flips on a tempo change");

        setParam (r.proc, ParamIDs::tapBpm, 100.0f);
        setParam (r.proc, ParamIDs::tempoSync, 2.0f);   // tap tempo
        expect (r.shown() == "100", "switching to tap shows the tap tempo straight away (got " + r.shown() + ")");

        setParam (r.proc, ParamIDs::tempoSync, 0.0f);   // sync off
        expect (r.shown() == "--", "turning sync off shows -- straight away (got " + r.shown() + ")");
        r.block();
        expect (r.proc.currentBpm() == 0.0f, "the engine stops syncing");
    }
    {
        // A set saved with sync on, reopened: the host tempo is known as soon as audio runs,
        // and the card must show it whatever page was open.
        Rig saved;
        setParam (saved.proc, ParamIDs::tempoSync, 1.0f);
        MemoryBlock state;
        saved.proc.getStateInformation (state);

        Rig r;
        r.host.bpm = 87.0;
        r.proc.setStateInformation (state.getData(), (int) state.getSize());
        r.block();
        r.openEditor (0);           // opens on the Panel page
        r.editor->showPage (2);     // then the Motion page
        expect (r.shown() == "87", "a set loaded with sync on shows Ableton's tempo (got " + r.shown() + ")");

        r.host.bpm = 120.0;
        r.editor->showPage (0);
        r.block();
        r.editor->refreshNow();     // a tempo change while another page is open
        r.editor->showPage (2);
        expect (r.shown() == "120", "a tempo change on another page is shown on return (got " + r.shown() + ")");
    }
}
// ---- Bug 2: knob, MIDI and expression all reach every Slip and Tape step ----
void pitchReachTests()
{
    using murmur::WetChannel;
    using murmur::LoopChannel;
    auto slipKey = [] (float m) { const auto s = WetChannel::slipStep (m); return (s.reverse ? 100 : 0) + s.semis; };

    // The parameter itself spans the whole range
    {
        Rig r;
        auto* prm = r.proc.apvts.getParameter (ParamIDs::wetModify);
        const float lo = prm->convertFrom0to1 (0.0f), hi = prm->convertFrom0to1 (1.0f);
        const auto a = WetChannel::slipStep (lo), b = WetChannel::slipStep (hi);
        expect (a.reverse && a.semis == 12 && ! b.reverse && b.semis == 12,
                "Modify parameter runs from 2x reverse to 2x forward");
    }

    // The knob: sweep the parameter in small steps, as dragging the dial does
    {
        std::set<int> slip;
        std::set<float> tape;
        for (int i = 0; i <= 1000; ++i)
        {
            slip.insert (slipKey ((float) i / 1000.0f));
            tape.insert (LoopChannel::tapeRate ((float) i / 1000.0f));
        }
        expect (slip.size() == 50, "knob reaches all 50 Slip steps, -12 to +12 both ways (got " + String ((int) slip.size()) + ")");
        expect (tape.size() == 8, "knob reaches all 8 Tape steps (got " + String ((int) tape.size()) + ")");
    }

    // MIDI: the original pedal's CC 17 (wet Modify) and CC 19 (loop Modify), every value 0..127,
    // on the original pedal MIDI map's own channel
    {
        Rig r;
        r.proc.setPedalMapChannel (1);
        std::set<int> slip;
        std::set<float> tape;
        for (int v = 0; v <= 127; ++v)
        {
            r.midi.clear();
            r.midi.addEvent (MidiMessage::controllerEvent (1, 17, v), 0);
            r.midi.addEvent (MidiMessage::controllerEvent (1, 19, v), 0);
            r.block();
            r.proc.applyPendingChanges();
            slip.insert (slipKey (r.proc.apvts.getRawParameterValue (ParamIDs::wetModify)->load()));
            tape.insert (LoopChannel::tapeRate (r.proc.apvts.getRawParameterValue (ParamIDs::loopModify)->load()));
        }
        r.midi.clear();
        expect (slip.size() == 50, "MIDI CC 17 reaches all 50 Slip steps (got " + String ((int) slip.size()) + ")");
        expect (tape.size() == 8, "MIDI CC 19 reaches all 8 Tape steps (got " + String ((int) tape.size()) + ")");
    }

    // Expression: heel to toe sweeps the assigned Modify knobs through every step
    {
        Rig r;
        setParam (r.proc, ParamIDs::modSource, 1.0f);   // driven by expression
        setParam (r.proc, ParamIDs::rampWetMod, 1.0f);
        setParam (r.proc, ParamIDs::rampLoopMod, 1.0f);
        setParam (r.proc, ParamIDs::wetModify, 1.0f);   // knob at the top, so heel to toe covers 0..1
        setParam (r.proc, ParamIDs::loopModify, 1.0f);
        std::set<int> slip;
        std::set<float> tape;
        for (int i = 0; i <= 400; ++i)
        {
            setParam (r.proc, ParamIDs::expression, (float) i / 400.0f);
            r.block();
            slip.insert (slipKey (r.proc.getEngine().uiWetMod.load()));
            tape.insert (LoopChannel::tapeRate (r.proc.getEngine().uiLoopMod.load()));
        }
        expect (slip.size() == 50, "expression reaches all 50 Slip steps (got " + String ((int) slip.size()) + ")");
        expect (tape.size() == 8, "expression reaches all 8 Tape steps (got " + String ((int) tape.size()) + ")");
    }

    // The flip cards name the step
    {
        Rig r;
        r.openEditor (0);
        const String minus = "-";
        setParam (r.proc, ParamIDs::wetMode, 2.0f);       // Slip
        setParam (r.proc, ParamIDs::loopMode, 1.0f);      // Tape
        struct Case { float m; String want; };
        for (auto c : { Case { (37 + 7 + 0.5f) / 50.0f, "FWD +5TH +7" },
                        Case { (12 + 12 + 0.5f) / 50.0f, "REV " + minus + "OCT " + minus + "12" },
                        Case { (0 + 0.5f) / 50.0f, "REV +OCT +12" },
                        Case { (37 + 0.5f) / 50.0f, "FWD UNISON 0" } })
        {
            setParam (r.proc, ParamIDs::wetModify, c.m);
            r.editor->refreshNow();
            const auto got = r.editor->wetModifyDial().getStepCard();
            expect (got == c.want, "Slip card shows " + c.want + " (got " + got + ")");
        }
        for (auto c : { Case { 3.5f / 8.0f, "REV " + minus + "OCT " + minus + "12" },
                        Case { 7.5f / 8.0f, "FWD +2 OCT +24" } })
        {
            setParam (r.proc, ParamIDs::loopModify, c.m);
            r.editor->refreshNow();
            const auto got = r.editor->loopModifyDial().getStepCard();
            expect (got == c.want, "Tape card shows " + c.want + " (got " + got + ")");
        }
    }
}

// ---- Foot control: footswitches A to D, combos, expression, Learn and the clash audit ----
void footswitchTests()
{
    {
        Rig r;
        // A: tap for wet, hold for freeze
        r.tapSwitch (0);
        expect (r.isOn (ParamIDs::wetOn), "A tap turns the wet channel on");
        r.holdSwitch (0, 0.6);
        expect (r.isOn (ParamIDs::wetOn) && ! r.isOn (ParamIDs::freeze), "A hold freezes while held, then lets go");
        r.press (0); r.wait (0.6);
        expect (r.isOn (ParamIDs::freeze), "A held past 0.45 s freezes");
        r.release (0); r.wait (0.05);
        r.tapSwitch (0);
        expect (! r.isOn (ParamIDs::wetOn), "A tap again turns the wet channel off");

        // B: loop record, then play, then stop; hold to overdub
        r.tapSwitch (1);
        expect (r.isOn (ParamIDs::loopRec) && ! r.isOn (ParamIDs::loopOn), "B tap starts recording a loop");
        r.block (40);
        r.tapSwitch (1);
        expect (! r.isOn (ParamIDs::loopRec) && r.isOn (ParamIDs::loopOn), "B tap again plays the recorded loop");
        r.press (1); r.wait (0.6);
        expect (r.isOn (ParamIDs::overdub), "B hold overdubs");
        r.release (1); r.wait (0.05);
        expect (! r.isOn (ParamIDs::overdub), "letting go of B stops the overdub");
        r.tapSwitch (1);
        expect (! r.isOn (ParamIDs::loopOn) && ! r.isOn (ParamIDs::loopRec), "B tap a third time stops the loop");

        // C: tap tempo, hold to save. D: next and previous preset slot
        r.tapSwitch (2);
        r.tapSwitch (2);
        expect (r.proc.apvts.getRawParameterValue (ParamIDs::tempoSync)->load() > 1.5f, "C taps set a tap tempo");
        r.tapSwitch (3);
        expect (std::lround (r.get (ParamIDs::presetSlot)) == 2, "D tap goes to the next preset slot (Live to B)");
        r.holdSwitch (3, 0.6);
        expect (std::lround (r.get (ParamIDs::presetSlot)) == 1, "D hold goes back a slot (B to Live)");
        r.holdSwitch (3, 0.6);
        setParam (r.proc, ParamIDs::time, 0.123f);
        r.holdSwitch (2, 0.6);
        expect (r.proc.hasPresetSlot (0), "C hold saves to the current slot (A)");

        // A+B: both channels on, then both off
        r.press (0); r.wait (0.03); r.press (1); r.wait (0.1); r.release (0); r.release (1); r.wait (0.05);
        expect (r.isOn (ParamIDs::wetOn) && r.isOn (ParamIDs::loopOn), "A+B turns both channels on");
        r.press (0); r.wait (0.03); r.press (1); r.wait (0.1); r.release (0); r.release (1); r.wait (0.05);
        expect (! r.isOn (ParamIDs::wetOn) && ! r.isOn (ParamIDs::loopOn), "A+B again turns both off");
    }

    // B+C: a one-second hold switches mode; a tap or a short hold never does
    {
        Rig r;
        r.press (1); r.wait (0.03); r.press (2); r.wait (0.1); r.release (1); r.release (2); r.wait (0.2);
        expect (r.proc.currentMode() == 0, "B+C tapped does not switch mode");
        r.comboHold (1, 2, 0.6);
        expect (r.proc.currentMode() == 0, "B+C held 0.6 s does not switch mode");
        expect (! r.isOn (ParamIDs::loopRec) && ! r.isOn (ParamIDs::loopOn) && std::lround (r.get (ParamIDs::tempoSync)) == 0,
                "B+C presses don't fire B's or C's own jobs");
        const int pulses = r.proc.uiModePulse.load();
        r.comboHold (1, 2, 1.1);
        expect (r.proc.currentMode() == 1, "B+C held for a second switches to Murk");
        expect (r.proc.uiModePulse.load() == pulses + 1, "Pebble is told about the switch");
        r.comboHold (2, 1, 1.1);
        expect (r.proc.currentMode() == 0, "B+C held again switches back to Murmur (either order)");
    }

    // C+D: hold for Keeper. Timings match every other Flip Fella plugin.
    {
        Rig r;
        const int before = r.proc.uiKeepPulse.load();
        r.comboHold (2, 3, 0.5);
        expect (r.proc.uiKeepPulse.load() == before, "C+D held half a second does not keep");
        r.comboHold (2, 3, 1.1);
        expect (r.proc.uiKeepPulse.load() == before + 1, "C+D held a second runs Keeper");
        expect (FootswitchLogic::kHoldSeconds == 0.45 && FootswitchLogic::kComboWindow == 0.12 && FootswitchLogic::kComboHoldSeconds == 1.0,
                "hold 0.45 s, combo window 0.12 s, combo hold 1 s, as in Prism Patch");
    }

    // Clash audit: the footswitch and expression defaults change nothing but their own jobs,
    // with the original pedal map off and with it on another channel.
    for (int mapChannel : { 0, 1 })
    {
        Rig r;
        r.proc.setPedalMapChannel (mapChannel);
        setParam (r.proc, ParamIDs::expTarget, (float) names::expTargetIndex ("clock"));
        std::map<String, float> before;
        for (auto* prm : r.proc.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm)) before[rp->getParameterID()] = rp->getValue();
        for (int sw = 0; sw < 4; ++sw) { r.press (sw); r.wait (0.02); r.release (sw); r.wait (0.6); }
        r.send (MidiMessage::controllerEvent (16, 11, 40));
        static const StringArray jobs { ParamIDs::wetOn, ParamIDs::freeze, ParamIDs::loopOn, ParamIDs::loopRec, ParamIDs::overdub,
                                        ParamIDs::tempoSync, ParamIDs::tapBpm, ParamIDs::presetSlot, ParamIDs::clock };
        StringArray changed;
        for (auto* prm : r.proc.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                if (std::abs (before[rp->getParameterID()] - rp->getValue()) > 1e-6f && ! jobs.contains (rp->getParameterID()))
                    changed.add (rp->getParameterID());
        expect (changed.isEmpty(), "CC 20 to 23 and CC 11 on channel 16 touch only their own jobs, original map "
                                   + String (mapChannel == 0 ? "off" : "on channel 1") + (changed.isEmpty() ? String() : " (also moved " + changed.joinIntoString (", ") + ")"));
        if (mapChannel == 1)
        {
            r.send (MidiMessage::controllerEvent (1, 20, 127));
            expect (r.get (ParamIDs::rampSpeed) > 0.99f, "the original map still works on its own channel (CC 20 is Ramp Speed there)");
        }
    }

    // Learn: a footswitch on another controller is picked up by pressing it once
    {
        Rig r;
        r.proc.learnFootswitch (0);
        r.send (MidiMessage::controllerEvent (1, 80, 127));
        r.send (MidiMessage::controllerEvent (1, 80, 0));
        expect (r.proc.pedals().fsChannel[0] == 1 && r.proc.pedals().fsCC[0] == 80, "Learn remaps footswitch A");
        r.press (0, 16); r.wait (0.1); r.release (0, 16); r.wait (0.05);
        expect (! r.isOn (ParamIDs::wetOn), "the old CC no longer works for A");
        r.send (MidiMessage::controllerEvent (1, 80, 127)); r.wait (0.1); r.send (MidiMessage::controllerEvent (1, 80, 0));
        expect (r.isOn (ParamIDs::wetOn), "the learned switch works for A");
        defaultPedals (r.proc);
    }
}

// ---- Expression: per mode and per preset, through the shared calibration ----
void expressionTests()
{
    Rig r;
    expect (std::lround (r.get (ParamIDs::expTarget)) == names::expTargetIndex ("clock"), "Murmur's expression default is Clock");
    r.send (MidiMessage::controllerEvent (16, 11, 0));
    expect (r.get (ParamIDs::clock) < 0.01f, "heel puts Clock at the bottom");
    r.send (MidiMessage::controllerEvent (16, 11, 127));
    expect (r.get (ParamIDs::clock) > 0.99f, "toe puts Clock at the top");

    // calibration fixes a reversed, short-range pedal (it sends 110 at the heel, 20 at the toe)
    r.proc.pedals().heel = 110; r.proc.pedals().toe = 20;
    r.send (MidiMessage::controllerEvent (16, 11, 110));
    expect (r.get (ParamIDs::clock) < 0.01f, "a reversed pedal's heel is still the bottom after calibration");
    r.send (MidiMessage::controllerEvent (16, 11, 20));
    expect (r.get (ParamIDs::clock) > 0.99f, "and its toe still the top");
    r.proc.pedals().heel = 0; r.proc.pedals().toe = 127;

    setParam (r.proc, ParamIDs::expTarget, (float) names::expTargetIndex ("mix"));
    r.proc.switchMode();
    expect (std::lround (r.get (ParamIDs::expTarget)) == names::expTargetIndex ("glue"), "Murk's expression default is Glue");
    r.send (MidiMessage::controllerEvent (16, 11, 127));
    expect (r.get (ParamIDs::glue) > 0.99f, "in Murk the pedal moves Glue");
    r.proc.switchMode();
    expect (std::lround (r.get (ParamIDs::expTarget)) == names::expTargetIndex ("mix"), "Murmur remembers its own expression choice (Mix)");
}

// ---- Modes: each keeps its own settings; presets and sets save the mode ----
void modeTests()
{
    {
        Rig r;
        r.openEditor (0);
        expect (r.editor->modeCard().getValue() == "MURMUR", "the mode card reads MURMUR");
        setParam (r.proc, ParamIDs::time, 0.2f);
        setParam (r.proc, ParamIDs::wetOn, 1.0f);
        r.proc.switchMode();
        r.editor->refreshNow();
        expect (r.editor->modeCard().getValue() == "MURK" && r.editor->modeCard().isFlipping(), "switching flips the mode card to MURK");
        expect (r.editor->wetModeCard().getValue() == "SOUP" && r.editor->loopModeCard().getValue() == "RADIO", "the channel cards relabel for Murk");
        expect (std::abs (r.get (ParamIDs::time) - 0.5f) < 1e-3f, "Murk starts from its own defaults the first time");
        expect (r.isOn (ParamIDs::wetOn), "a channel that was on stays on across the switch");
        setParam (r.proc, ParamIDs::time, 0.8f);
        r.proc.switchMode();
        expect (std::abs (r.get (ParamIDs::time) - 0.2f) < 1e-3f, "back in Murmur, Time is where Murmur left it");
        r.proc.switchMode();
        expect (std::abs (r.get (ParamIDs::time) - 0.8f) < 1e-3f, "and Murk's own Time comes back too");

        // automation or the host moving the Mode parameter does the same swap
        setParam (r.proc, ParamIDs::mode, 0.0f);
        r.wait (0.02);
        expect (std::abs (r.get (ParamIDs::time) - 0.2f) < 1e-3f, "moving the Mode parameter swaps the settings too");

        // a Murk loop mode for every flip card, and Radio's station on the Modify card
        setParam (r.proc, ParamIDs::mode, 1.0f);
        r.wait (0.02);
        setParam (r.proc, ParamIDs::loopModify, 0.25f);
        r.editor->refreshNow();
        expect (r.editor->loopModifyDial().getStepCard() == "AMBIENT", "Radio's Modify card names the station (got " + r.editor->loopModifyDial().getStepCard() + ")");
        setParam (r.proc, ParamIDs::murkWet, 2.0f);
        setParam (r.proc, ParamIDs::wetModify, (26.0f + 0.5f) / 48.0f);
        r.editor->refreshNow();
        expect (r.editor->wetModifyDial().getStepCard() == "+OCT", "Flip's Modify card names the chord (got " + r.editor->wetModifyDial().getStepCard() + ")");
    }

    // Presets save the mode and both modes' settings
    {
        Rig r;
        setParam (r.proc, ParamIDs::time, 0.3f);           // Murmur's Time
        r.proc.switchMode();
        setParam (r.proc, ParamIDs::time, 0.7f);           // Murk's Time
        setParam (r.proc, ParamIDs::presetSlot, 0.0f);     // to A
        r.wait (0.02);
        setParam (r.proc, ParamIDs::time, 0.7f);
        r.proc.savePresetSlot (0);
        setParam (r.proc, ParamIDs::presetSlot, 1.0f);     // back to Live
        r.wait (0.02);
        r.proc.switchMode();                                // to Murmur
        setParam (r.proc, ParamIDs::time, 0.9f);
        setParam (r.proc, ParamIDs::presetSlot, 0.0f);     // load A
        r.wait (0.02);
        expect (r.proc.currentMode() == 1, "loading preset A brings back its mode (Murk)");
        expect (std::abs (r.get (ParamIDs::time) - 0.7f) < 1e-3f, "and Murk's settings from the preset");
        r.proc.switchMode();
        expect (std::abs (r.get (ParamIDs::time) - 0.3f) < 1e-3f, "the preset also holds Murmur's settings");
    }

    // A set saved in Murk opens in Murk, with both modes' settings
    {
        Rig saved;
        setParam (saved.proc, ParamIDs::time, 0.15f);
        saved.proc.switchMode();
        setParam (saved.proc, ParamIDs::time, 0.85f);
        MemoryBlock state;
        saved.proc.getStateInformation (state);

        Rig r;
        r.proc.setStateInformation (state.getData(), (int) state.getSize());
        r.wait (0.02);
        expect (r.proc.currentMode() == 1 && std::abs (r.get (ParamIDs::time) - 0.85f) < 1e-3f, "a set saved in Murk opens in Murk");
        r.proc.switchMode();
        expect (std::abs (r.get (ParamIDs::time) - 0.15f) < 1e-3f, "with Murmur's settings kept as well");
    }

    // The Pedal page and its plain-words overlay
    {
        Rig r;
        r.openEditor (MurmurEditor::PedalPage);
        r.editor->showOverlay (true);
        expect (r.editor->overlayShowing(), "the Pedal page shows the plain-words overlay");
        r.editor->showPage (MurmurEditor::PanelPage);
        expect (! r.editor->overlayShowing(), "and hides it on leaving the page");
        for (int m = 0; m < 2; ++m)
            for (int sw = 0; sw < 4; ++sw)
                expect (MurmurProcessor::tapJob (m, sw) == MurmurProcessor::tapJob (1 - m, sw), "footswitch " + String::charToString ((juce_wchar) ('A' + sw)) + " has the same tap job in both modes");
    }
}
} // namespace

// Renders every page in both modes to PNG files, so the look can be checked against the mockup
void snapshots (const File& dir)
{
    dir.createDirectory();
    Rig r;
    r.openEditor (0);
    setParam (r.proc, ParamIDs::wetOn, 1.0f);
    for (int mode = 0; mode < 2; ++mode)
    {
        if (r.proc.currentMode() != mode) r.proc.switchMode();
        if (mode == 1) { setParam (r.proc, ParamIDs::wetOn, 1.0f); setParam (r.proc, ParamIDs::loopModify, 0.25f); }
        for (int page = 0; page < 4; ++page)
        {
            r.editor->showPage (page);
            for (int k = 0; k < 40; ++k) { r.block(); r.editor->refreshNow(); Thread::sleep (8); }
            auto img = r.editor->createComponentSnapshot (r.editor->getLocalBounds(), true, 1.0f);
            static const char* names[4] = { "panel", "deep", "motion", "pedal" };
            PNGImageFormat png;
            FileOutputStream out (dir.getChildFile (String (mode == 1 ? "murk_" : "murmur_") + names[page] + ".png"));
            out.setPosition (0); out.truncate();
            png.writeImageToStream (img, out);
        }
    }
    r.editor->showPage (3);
    r.editor->showOverlay (true);
    r.editor->refreshNow();
    auto img = r.editor->createComponentSnapshot (r.editor->getLocalBounds(), true, 1.0f);
    PNGImageFormat png;
    FileOutputStream out (dir.getChildFile ("pedal_overlay.png"));
    out.setPosition (0); out.truncate();
    png.writeImageToStream (img, out);
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && String (argv[1]) == "--snapshots")
    {
        snapshots (File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        return 0;
    }
    tempoDisplayTests();
    pitchReachTests();
    footswitchTests();
    expressionTests();
    modeTests();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
