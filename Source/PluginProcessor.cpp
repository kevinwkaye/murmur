#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

MurmurProcessor::MurmurProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MurmurState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            paramIndex[rp->getParameterID()] = params.size();
            params.add (rp);
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
        }
    startTimerHz (30);
}

MurmurProcessor::~MurmurProcessor()
{
    stopTimer();
}

void MurmurProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

bool MurmurProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo())
        return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

murmur::EngineParams MurmurProcessor::readParams() const
{
    murmur::EngineParams p;
    p.mix = f (ParamIDs::mix); p.clock = f (ParamIDs::clock); p.routing = idx (ParamIDs::routing);
    p.wetOn = on (ParamIDs::wetOn); p.freeze = on (ParamIDs::freeze); p.wetMode = idx (ParamIDs::wetMode);
    p.time = f (ParamIDs::time); p.wetModify = f (ParamIDs::wetModify);
    p.loopOn = on (ParamIDs::loopOn); p.overdub = on (ParamIDs::overdub); p.loopMode = idx (ParamIDs::loopMode);
    p.length = f (ParamIDs::length); p.loopModify = f (ParamIDs::loopModify);
    p.width = f (ParamIDs::width); p.toneHz = f (ParamIDs::tone); p.fade = f (ParamIDs::fade); p.direct = f (ParamIDs::direct);
    p.balance = f (ParamIDs::balance); p.sync = idx (ParamIDs::sync); p.halfLength = on (ParamIDs::halfLength); p.spreadSolo = idx (ParamIDs::spreadSolo);
    p.miso = on (ParamIDs::miso); p.spread = on (ParamIDs::spread); p.dryKill = on (ParamIDs::dryKill); p.trails = on (ParamIDs::trails);
    p.noDub = on (ParamIDs::noDub); p.smoothClock = on (ParamIDs::smoothClock); p.classic = on (ParamIDs::classic);
    p.inGainDb = f (ParamIDs::inGain); p.outGainDb = f (ParamIDs::outGain);
    p.bufferSeconds = f (ParamIDs::bufferSeconds); p.stepSet = idx (ParamIDs::stepSet);

    p.rampTime = on (ParamIDs::rampTime); p.rampWetMod = on (ParamIDs::rampWetMod); p.rampClock = on (ParamIDs::rampClock);
    p.rampLoopMod = on (ParamIDs::rampLoopMod); p.rampLength = on (ParamIDs::rampLength);
    p.bounce = on (ParamIDs::bounce); p.sweepTop = idx (ParamIDs::sweep) == 1;
    p.rampSpeed = f (ParamIDs::rampSpeed); p.rampWave = f (ParamIDs::rampWave);
    p.modSource = idx (ParamIDs::modSource); p.expression = f (ParamIDs::expression); p.expReverse = idx (ParamIDs::expPolarity) == 1;

    p.synthGate = idx (ParamIDs::synthGate);
    p.attack = f (ParamIDs::attack) * 0.001f; p.decay = f (ParamIDs::decay) * 0.001f; p.sustain = f (ParamIDs::sustain);
    p.release = f (ParamIDs::release) * 0.001f; p.glide = f (ParamIDs::glide) * 0.001f;
    p.synthRoot = idx (ParamIDs::synthRoot);
    return p;
}

void MurmurProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || getTotalNumOutputChannels() < 2)
        return;

    if (getTotalNumInputChannels() == 1) // a mono input feeds both sides
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    auto p = readParams();

    // Remember Ableton's tempo on every block, whatever the Sync setting, so the
    // moment Sync turns on both the engine and the display already know it.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                hostBpm.store ((float) *b);
    p.bpm = currentBpm();

    // Synth Mode ends when you move the Clock, like the pedal
    if (lastClock >= 0.0f && std::abs (p.clock - lastClock) > 0.004f && engine.isSynthActive())
        engine.synthExit();
    lastClock = p.clock;
    if (! on (ParamIDs::synthEnable) && engine.isSynthActive())
        engine.synthExit();

    // Process in pieces between MIDI events so notes land on time
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            engine.process (L + pos, R + pos, at - pos, p);
            pos = at;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < n)
        engine.process (L + pos, R + pos, n - pos, p);
}

// ---- MIDI: notes drive Synth Mode, CCs follow the pedal's MIDI manual ----
void MurmurProcessor::handleMidi (const MidiMessage& m)
{

    if (m.isNoteOn())
    {
        if (on (ParamIDs::synthEnable))
            engine.synthNoteOn (m.getNoteNumber(), m.getFloatVelocity());
        return;
    }
    if (m.isNoteOff())
    {
        engine.synthNoteOff (m.getNoteNumber());
        return;
    }
    if (m.isPitchWheel())
    {
        engine.synthPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f * 4.0f);
        return;
    }
    if (m.isProgramChange())
    {
        pendingProgram.store (m.getProgramChangeNumber());
        return;
    }
    if (! m.isController())
        return;

    const int cc = m.getControllerNumber();
    const int v = m.getControllerValue();
    const float k = (float) v / 127.0f;
    auto three = [v] { return v <= 1 ? 0.0f : (v == 2 ? 1.0f : 2.0f); };
    auto sw = [v] { return v > 0 ? 1.0f : 0.0f; };

    switch (cc)
    {
        case 14: queueValue (ParamIDs::time, k); break;
        case 15: queueValue (ParamIDs::mix, k); break;
        case 16: queueValue (ParamIDs::length, k); break;
        case 17: queueValue (ParamIDs::wetModify, k); break;
        case 18: queueValue (ParamIDs::clock, k); break;
        case 19: queueValue (ParamIDs::loopModify, k); break;
        case 20: queueValue (ParamIDs::rampSpeed, k); break;
        case 21: queueValue (ParamIDs::wetMode, three()); break;
        case 22: queueValue (ParamIDs::routing, three()); break;
        case 23: queueValue (ParamIDs::loopMode, three()); break;
        case 24: queueValue (ParamIDs::width, k * 2.0f); break;
        case 25: queueValue (ParamIDs::rampWave, k); break;
        case 26: queueValue (ParamIDs::fade, k); break;
        case 27: queueValue (ParamIDs::tone, 1000.0f + 19000.0f * std::pow (k, 1.0f / 0.3f)); break;
        case 28: queueValue (ParamIDs::balance, k * 2.0f - 1.0f); break;
        case 29: queueValue (ParamIDs::direct, k); break;
        case 31: queueValue (ParamIDs::sync, v <= 1 ? 1.0f : (v == 2 ? 0.0f : 2.0f)); break;
        case 32: queueValue (ParamIDs::spreadSolo, v <= 1 ? 1.0f : (v == 2 ? 0.0f : 2.0f)); break;
        case 33: queueValue (ParamIDs::halfLength, v <= 1 ? 1.0f : 0.0f); break;
        case 51: queueValue (ParamIDs::tempoSync, v > 0 ? 1.0f : 0.0f); break;
        case 57: queueValue (ParamIDs::synthRoot, (float) jlimit (24, 96, 12 * jlimit (2, 8, v))); break;
        case 58: queueValue (ParamIDs::synthGate, v == 0 ? 0.0f : (v == 1 ? 1.0f : 2.0f)); break;
        case 59: engine.synthExit(); break;
        case 61: queueValue (ParamIDs::rampTime, sw()); break;
        case 62: queueValue (ParamIDs::rampWetMod, sw()); break;
        case 63: queueValue (ParamIDs::rampClock, sw()); break;
        case 64: queueValue (ParamIDs::rampLoopMod, sw()); break;
        case 65: queueValue (ParamIDs::rampLength, sw()); break;
        case 66: queueValue (ParamIDs::bounce, sw()); break;
        case 67: queueValue (ParamIDs::sweep, v > 0 ? 1.0f : 0.0f); break;
        case 68: queueValue (ParamIDs::expPolarity, sw()); break;
        case 71: queueValue (ParamIDs::classic, sw()); break;
        case 72: queueValue (ParamIDs::miso, sw()); break;
        case 73: queueValue (ParamIDs::spread, sw()); break;
        case 74: queueValue (ParamIDs::dryKill, sw()); break;
        case 75: queueValue (ParamIDs::trails, sw()); break;
        case 76: queueValue (ParamIDs::latch, sw()); break;
        case 77: queueValue (ParamIDs::noDub, sw()); break;
        case 78: queueValue (ParamIDs::smoothClock, sw()); break;
        case 80: queueValue (ParamIDs::attack, 1.0f + 3999.0f * std::pow (k, 1.0f / 0.35f)); break;
        case 81: queueValue (ParamIDs::decay, 5.0f + 3995.0f * std::pow (k, 1.0f / 0.35f)); break;
        case 82: queueValue (ParamIDs::sustain, k); break;
        case 83: queueValue (ParamIDs::release, 5.0f + 7995.0f * std::pow (k, 1.0f / 0.35f)); break;
        case 84: queueValue (ParamIDs::glide, 2000.0f * std::pow (k, 1.0f / 0.35f)); break;
        case 100: queueValue (ParamIDs::expression, k); break;
        case 102: queueValue (ParamIDs::wetOn, sw()); break;
        case 103: queueValue (ParamIDs::loopOn, sw()); break;
        case 105: queueValue (ParamIDs::freeze, sw()); break;
        case 106: queueValue (ParamIDs::overdub, sw()); break;
        case 93:
        case 107: if (v > 0) ++pendingTaps; break;
        default: break;
    }
}

void MurmurProcessor::queueValue (const String& id, float plainValue)
{
    const auto it = paramIndex.find (id);
    if (it == paramIndex.end())
        return;
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        pending[(size_t) scope.startIndex1] = { it->second, plainValue };
}

void MurmurProcessor::setNow (const String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

// ---- message thread housekeeping ----
void MurmurProcessor::timerCallback()
{
    applyPendingChanges();
}

void MurmurProcessor::applyPendingChanges()
{
    // MIDI CC changes
    const auto scope = fifo.read (fifo.getNumReady());
    auto apply = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            auto* p = params[pending[(size_t) i].param];
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (pending[(size_t) i].value));
            p->endChangeGesture();
        }
    };
    apply (scope.startIndex1, scope.blockSize1);
    apply (scope.startIndex2, scope.blockSize2);

    for (int t = pendingTaps.exchange (0); t > 0; --t)
        tapTempo();

    // Program change 0 = live, 1 = right slot (B), 2 = left slot (A), as on the pedal
    const int pc = pendingProgram.exchange (-1);
    if (pc == 0) setNow (ParamIDs::presetSlot, 1.0f);
    if (pc == 1) setNow (ParamIDs::presetSlot, 2.0f);
    if (pc == 2) setNow (ParamIDs::presetSlot, 0.0f);

    // Preset toggle: leaving Live keeps your live settings, coming back restores them
    const int slot = idx (ParamIDs::presetSlot);
    if (slot != lastSlot)
    {
        auto store = apvts.state.getOrCreateChildWithName ("Snapshots", nullptr);
        if (lastSlot == 1)
        {
            for (int i = store.getNumChildren(); --i >= 0;)
                if (store.getChild (i).getProperty ("name").toString() == "Live")
                    store.removeChild (i, nullptr);
            auto live = snapshotOf();
            live.setProperty ("name", "Live", nullptr);
            store.appendChild (live, nullptr);
        }
        const char* name = slot == 0 ? "A" : (slot == 2 ? "B" : "Live");
        for (const auto& child : store)
            if (child.getProperty ("name").toString() == name)
            {
                applySnapshot (child);
                break;
            }
        lastSlot = slot;
    }
}

bool MurmurProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::wetOn, ParamIDs::loopOn, ParamIDs::freeze, ParamIDs::overdub, ParamIDs::presetSlot, ParamIDs::expression, ParamIDs::tapBpm,
                                        ParamIDs::synthEnable, ParamIDs::synthGate, ParamIDs::attack, ParamIDs::decay, ParamIDs::sustain, ParamIDs::release, ParamIDs::glide, ParamIDs::synthRoot };
    return ! excluded.contains (id);
}

ValueTree MurmurProcessor::snapshotOf() const
{
    ValueTree t ("Snapshot");
    for (auto* p : params)
        if (inSnapshot (p->getParameterID()))
            t.setProperty (Identifier (p->getParameterID()), p->convertFrom0to1 (p->getValue()), nullptr);
    return t;
}

void MurmurProcessor::applySnapshot (const ValueTree& t)
{
    for (auto* p : params)
    {
        const Identifier id (p->getParameterID());
        if (inSnapshot (p->getParameterID()) && t.hasProperty (id))
            setNow (p->getParameterID(), (float) t.getProperty (id));
    }
}

void MurmurProcessor::savePresetSlot (int slot)
{
    if (slot != 0 && slot != 2)
        return;
    const String name = slot == 0 ? "A" : "B";
    auto store = apvts.state.getOrCreateChildWithName ("Snapshots", nullptr);
    for (int i = store.getNumChildren(); --i >= 0;)
        if (store.getChild (i).getProperty ("name").toString() == name)
            store.removeChild (i, nullptr);
    auto snap = snapshotOf();
    snap.setProperty ("name", name, nullptr);
    store.appendChild (snap, nullptr);
}

bool MurmurProcessor::hasPresetSlot (int slot) const
{
    const String name = slot == 0 ? "A" : (slot == 2 ? "B" : "Live");
    const auto store = apvts.state.getChildWithName ("Snapshots");
    for (const auto& child : store)
        if (child.getProperty ("name").toString() == name)
            return true;
    return false;
}

void MurmurProcessor::resetHiddenOptions()
{
    for (const char* id : { ParamIDs::width, ParamIDs::tone, ParamIDs::fade, ParamIDs::direct, ParamIDs::balance, ParamIDs::sync, ParamIDs::halfLength, ParamIDs::spreadSolo, ParamIDs::rampWave })
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

float MurmurProcessor::currentBpm() const
{
    // The one place that decides the tempo Murmur runs at: the engine and the display both ask here.
    switch (idx (ParamIDs::tempoSync))
    {
        case 1:  return hostBpm.load();          // Ableton (0 until Ableton has reported a tempo)
        case 2:  return f (ParamIDs::tapBpm);    // tap tempo
        default: return 0.0f;                    // not synced
    }
}

void MurmurProcessor::tapTempo()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    if (! tapTimes.isEmpty() && now - tapTimes.getLast() > 2.0)
        tapTimes.clear();
    tapTimes.add (now);
    while (tapTimes.size() > 5)
        tapTimes.remove (0);
    if (tapTimes.size() >= 2)
    {
        const double avg = (tapTimes.getLast() - tapTimes.getFirst()) / (tapTimes.size() - 1);
        setNow (ParamIDs::tapBpm, (float) jlimit (40.0, 240.0, 60.0 / avg));
        if (idx (ParamIDs::tempoSync) != 2)
            setNow (ParamIDs::tempoSync, 2.0f);
    }
}

AudioProcessorEditor* MurmurProcessor::createEditor()
{
    return new MurmurEditor (*this);
}

void MurmurProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MurmurProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            lastSlot = idx (ParamIDs::presetSlot); // don't re-apply a preset on load
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MurmurProcessor();
}
