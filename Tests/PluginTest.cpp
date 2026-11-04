// Plugin-level tests: run the real processor and editor with a fake host.
// Usage: murmur_plugin_test

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>
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

struct Rig
{
    FakeHost host;
    MurmurProcessor proc;
    AudioBuffer<float> buffer { 2, 128 };
    MidiBuffer midi;
    std::unique_ptr<MurmurEditor> editor;

    Rig()
    {
        proc.setPlayHead (&host);
        proc.prepareToPlay (48000.0, 128);
    }
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

    // MIDI: the original pedal's CC 17 (wet Modify) and CC 19 (loop Modify), every value 0..127
    {
        Rig r;
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
} // namespace

int main()
{
    ScopedJuceInitialiser_GUI gui;
    tempoDisplayTests();
    pitchReachTests();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
