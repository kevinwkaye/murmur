#pragma once

#include "Params.h"
#include "engine/MurmurEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

class MurmurProcessor : public juce::AudioProcessor
{
public:
    MurmurProcessor();
    ~MurmurProcessor() override = default;

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

private:
    murmur::EngineParams readParams() const;

    murmur::MurmurEngine engine;
    juce::AudioBuffer<float> work;

    struct Raw
    {
        std::atomic<float>* p = nullptr;
        float get() const { return p->load (std::memory_order_relaxed); }
        bool on() const { return get() > 0.5f; }
        int idx() const { return (int) std::lround (get()); }
    };
    Raw mix, clock, routing, wetOn, freeze, wetMode, time, wetModify, loopOn, overdub, loopMode, length, loopModify,
        width, tone, fade, direct, balance, sync, halfLength, spreadSolo,
        miso, spread, dryKill, trails, noDub, smoothClock, classic,
        inGain, outGain, bufferSeconds, stepSet;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurmurProcessor)
};
