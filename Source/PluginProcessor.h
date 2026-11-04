#pragma once

#include "Params.h"
#include "PedalControl.h"
#include "Keeper.h"
#include "engine/MurmurEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class MurmurProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MurmurProcessor();
    ~MurmurProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Murmur"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const murmur::MurmurEngine& getEngine() const { return engine; }

    // ---- called from the editor (message thread) ----
    void tapTempo();
    void savePresetSlot (int slot);           // 0 = A, 2 = B, like holding the pedal's footswitches
    bool hasPresetSlot (int slot) const;
    void resetHiddenOptions();
    float currentBpm() const; // the tempo Murmur runs at, 0 when not synced (safe on any thread)
    int currentMode() const { return idx (ParamIDs::mode); }
    void switchMode();                        // Murmur <-> Murk, each keeping its own settings

    // Footswitches A to D: the on-screen buttons press them exactly as the real ones do
    void footDown (int sw);
    void footUp (int sw);
    static juce::String tapJob (int mode, int sw);    // what each footswitch does, in plain words
    static juce::String holdJob (int mode, int sw);
    juce::String footState (int sw) const;            // what a footswitch's card reads right now

    // footswitch and expression setup, shared with every Flip Fella plugin
    PedalSettings& pedals() { return settings; }
    void learnFootswitch (int sw);
    void learnExpression();
    void learnKeeper();
    void calibrateExpression();
    void resetFootswitches();
    void cancelLearn() { learnTarget.store (LearnNone); }
    void setPedalMapChannel (int channel);
    void setFootDevice (const juce::String& device);
    void setKeeperLive (bool live);
    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    bool pedalsDirect() const { return direct->connected(); }
    juce::String footInputStatus() const { return direct->status(); }
    bool isLearning() const { return learnTarget.load() != LearnNone; }
    int learningWhat() const { return learnTarget.load(); }
    bool isCalibrating() const { return calibrating; }
    enum { LearnNone = -1, LearnExpression = 4, LearnKeeper = 5 };

    // Keeper: log this moment, the last 8 seconds of audio included
    void keepNow();
    void addKeeperNote (int number, const juce::String& note) { keeper.addNote (number, note); }
    juce::File exportKeepers();
    void openKeepersFolder();
    int lastKeeper() const { return keeper.lastNumber(); }

    // Applies MIDI CC changes, footswitches, taps and preset switches queued by other threads.
    // The 100 Hz timer calls it; tests call it directly with their own clock.
    void applyPendingChanges();
    void applyPendingChanges (double now);

    // ---- what the panel and Pebble show ----
    float expressionLevel() const { return uiExp.load(); }
    double lastExpressionMove() const { return expMovedAt; }
    std::atomic<int> uiFootPulse { 0 };       // counts footswitch presses: Pebble taps its foot
    std::atomic<int> uiFootDown { 0 };        // footswitches held now, one bit each
    std::atomic<int> uiKeepPulse { 0 };       // counts Keepers: Pebble nods
    std::atomic<int> uiModePulse { 0 };       // counts mode switches: Pebble reacts
    juce::String uiMessage;                   // a short line for the status card
    double uiMessageAt = 0.0;

private:
    void timerCallback() override;
    murmur::EngineParams readParams() const;
    void handleMidi (const juce::MidiMessage&, bool fromDevice);
    void handlePedalMap (int cc, int v);
    void queueValue (const juce::String& id, float plainValue);
    void queuePedal (int kind, int a, float v);
    void setNow (const juce::String& id, float plainValue);
    void setNorm (const juce::String& id, float v01);
    void say (const juce::String& s);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void loopRecordTap();
    void stepPreset (int direction);

    juce::ValueTree snapshotOf() const;
    void applySnapshot (const juce::ValueTree&);
    static bool inSnapshot (const juce::String& id);
    static bool inModeStash (const juce::String& id);
    juce::ValueTree modeStash (int mode) const;
    void storeModeStash (int mode, const juce::ValueTree& stash);
    void onModeChanged (int from, int to);

    murmur::MurmurEngine engine;
    Keeper keeper;

    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return f (id) > 0.5f; }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // MIDI CC changes are made on the message thread
    struct Pending { int param = 0; float value = 0; };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    juce::Array<juce::RangedAudioParameter*> params;
    std::map<juce::String, int> paramIndex;

    // footswitch, expression and Learn messages, from Ableton and from the footswitch device
    struct PedalMsg { int kind = 0; int a = 0; float v = 0; };
    enum PedalKind { FootPress, FootRelease, Expression, Learned, KeepCC };
    juce::AbstractFifo pedalFifo { 256 }, directFifo { 256 };
    std::array<PedalMsg, 256> pedalPending, directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    juce::Time settingsStamp;
    bool wasDirect = false;
    double nextSettingsCheck = 0.0;
    void drainPedal (juce::AbstractFifo&, std::array<PedalMsg, 256>&, double now);

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1;
    double expMovedAt = -100.0;
    std::atomic<float> uiExp { 0.0f };

    std::atomic<int> pendingTaps { 0 }, pendingProgram { -1 };
    std::atomic<float> hostBpm { 0.0f }; // Ableton's last reported tempo
    juce::Array<double> tapTimes;
    float lastClock = -1.0f;
    int lastSlot = 1, lastMode = 0;
    bool applyingSnapshot = false;

public:
    std::atomic<bool> hostFeedsPedals { false };  // Ableton routes footswitch MIDI to this copy

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurmurProcessor)
};
