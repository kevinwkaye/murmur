#pragma once

#include "Params.h"
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
    float currentBpm() const { return uiBpm.load(); } // 0 when not synced

private:
    void timerCallback() override;
    murmur::EngineParams readParams() const;
    void handleMidi (const juce::MidiMessage&);
    void queueValue (const juce::String& id, float plainValue);
    void setNow (const juce::String& id, float plainValue);
    juce::ValueTree snapshotOf() const;
    void applySnapshot (const juce::ValueTree&);
    static bool inSnapshot (const juce::String& id);

    murmur::MurmurEngine engine;

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

    std::atomic<int> pendingTaps { 0 }, pendingProgram { -1 };
    std::atomic<float> uiBpm { 0.0f };
    juce::Array<double> tapTimes;
    float lastClock = -1.0f;
    int lastSlot = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurmurProcessor)
};
