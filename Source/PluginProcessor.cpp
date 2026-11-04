#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several Murmur copies can be open at once. Footswitches drive one of them at a time:
// the one whose window was last opened or clicked, or the next one along when C+D are tapped.
// (Copied from Prism Patch, the reference for foot control.)
namespace
{
SpinLock focusLock;
Array<MurmurProcessor*> instances;
std::atomic<MurmurProcessor*> focused { nullptr };

bool hasFocus (MurmurProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}

// set while a message from the footswitch device is being handled on the MIDI thread
thread_local bool fromDeviceNow = false;

// footswitch device messages go to the copy that is learning, else the copy in charge
void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    MurmurProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}

double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace

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

    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

MurmurProcessor::~MurmurProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void MurmurProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keeper.prepare (sampleRate);
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
    p.mode = idx (ParamIDs::mode);
    p.mix = f (ParamIDs::mix); p.clock = f (ParamIDs::clock); p.routing = idx (ParamIDs::routing);
    p.wetOn = on (ParamIDs::wetOn); p.freeze = on (ParamIDs::freeze); p.wetMode = idx (ParamIDs::wetMode);
    p.time = f (ParamIDs::time); p.wetModify = f (ParamIDs::wetModify);
    p.loopOn = on (ParamIDs::loopOn); p.overdub = on (ParamIDs::overdub); p.loopMode = idx (ParamIDs::loopMode);
    p.loopRec = on (ParamIDs::loopRec);
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

    p.murkWet = idx (ParamIDs::murkWet); p.murkLoop = idx (ParamIDs::murkLoop);
    p.glue = f (ParamIDs::glue); p.dryGlue = on (ParamIDs::dryGlue);
    p.cross = f (ParamIDs::cross); p.crossSource = idx (ParamIDs::crossSource);
    p.eqTilt = f (ParamIDs::eqTilt); p.eqOn = idx (ParamIDs::eqOn);
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

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    keeper.pushInput (L, R, n);

    // Process in pieces between MIDI events so notes land on time
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            engine.process (L + pos, R + pos, at - pos, p);
            pos = at;
        }
        handleMidi (meta.getMessage(), false);
    }
    if (pos < n)
        engine.process (L + pos, R + pos, n - pos, p);

    keeper.pushOutput (L, R, n);
}

// =====================================================================================
// MIDI: notes drive Synth Mode. Footswitches, expression and Learn come first; the
// original pedal's CC chart only answers on its own channel, off by default, so it can
// never collide with the footswitch (CC 20 to 23) and expression (CC 11) defaults.
// =====================================================================================
void MurmurProcessor::queuePedal (int kind, int a, float v)
{
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = { kind, a, v };
        return;
    }
    const auto scope = pedalFifo.write (1);
    if (scope.blockSize1 > 0) pedalPending[(size_t) scope.startIndex1] = { kind, a, v };
}

void MurmurProcessor::handleDirectMidi (const MidiMessage& m)
{
    // several MIDI devices can call in on their own threads
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void MurmurProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    const int ch = m.getChannel();
    if (! fromDevice)
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
            if (ch == settings.pedalMapChannel || ch == settings.fsChannel[0])
                pendingProgram.store (m.getProgramChangeNumber());
            return;
        }
    }
    if (! m.isController())
        return;

    const int cc = m.getControllerNumber();
    const int v = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (v > 0) queuePedal (Learned, ch * 128 + cc, 0.0f);
        return;
    }

    // Straight from the footswitch device, any channel will do: it's the footswitch's own cable,
    // and some footswitches ignore their channel setting. Through Ableton, the channel must match.
    // When the device is read directly, the same messages arriving through Ableton are ignored,
    // so nothing happens twice.
    if (fromDevice || ! pedalsDirect())
    {
        int foot = -1;
        for (int k = 0; k < 4 && foot < 0; ++k)
            if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
        const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
        const bool expr = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
        if (foot >= 0 || keep || expr)
        {
            if (! fromDevice)
            {
                // The copy Ableton sends footswitch MIDI to takes charge, unless the copy in
                // charge is fed too. A newly loaded copy with no MIDI track can't hold the pedals.
                hostFeedsPedals.store (true);
                const SpinLock::ScopedLockType l (focusLock);
                auto* fc = focused.load();
                if (fc != this && (fc == nullptr || ! fc->hostFeedsPedals.load())) focused.store (this);
                if (focused.load() != this) return;
            }
            if (foot >= 0) queuePedal (v >= 64 ? FootPress : FootRelease, foot, 0.0f);
            else if (keep) { if (v >= 64) queuePedal (KeepCC, 0, 0.0f); }
            else queuePedal (Expression, v, 0.0f);
            return;
        }
    }

    if (fromDevice || settings.pedalMapChannel == 0 || ch != settings.pedalMapChannel)
        return;
    handlePedalMap (cc, v);
}

// The original pedal's MIDI chart, so templates made for it still work
void MurmurProcessor::handlePedalMap (int cc, int v)
{
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
        case 21: queueValue (currentMode() == 1 ? ParamIDs::murkWet : ParamIDs::wetMode, three()); break;
        case 22: queueValue (ParamIDs::routing, three()); break;
        case 23: queueValue (currentMode() == 1 ? ParamIDs::murkLoop : ParamIDs::loopMode, three()); break;
        case 24: queueValue (ParamIDs::width, k * 2.0f); break;
        case 25: queueValue (ParamIDs::rampWave, k); break;
        case 26: queueValue (ParamIDs::fade, k); break;
        case 27: if (currentMode() == 1) queueValue (ParamIDs::eqTilt, k * 2.0f - 1.0f);
                 else queueValue (ParamIDs::tone, 1000.0f + 19000.0f * std::pow (k, 1.0f / 0.3f));
                 break;
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
        case 71: queueValue (currentMode() == 1 ? ParamIDs::dryGlue : ParamIDs::classic, sw()); break;
        case 72: queueValue (ParamIDs::miso, sw()); break;
        case 73: queueValue (ParamIDs::spread, sw()); break;
        case 74: queueValue (ParamIDs::dryKill, sw()); break;
        case 75: queueValue (ParamIDs::trails, sw()); break;
        case 76: queueValue (ParamIDs::latch, sw()); break;
        case 77: queueValue (currentMode() == 1 ? ParamIDs::halfLength : ParamIDs::noDub, sw()); break;
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

void MurmurProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
        p->endChangeGesture();
    }
}

void MurmurProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = nowSeconds();
}

// =====================================================================================
// Message thread housekeeping
// =====================================================================================
void MurmurProcessor::timerCallback()
{
    applyPendingChanges (nowSeconds());
}

void MurmurProcessor::applyPendingChanges()
{
    applyPendingChanges (nowSeconds());
}

void MurmurProcessor::drainPedal (AbstractFifo& q, std::array<PedalMsg, 256>& from, double now)
{
    const auto scope = q.read (q.getNumReady());
    auto apply = [&] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& m = from[(size_t) i];
            switch (m.kind)
            {
                case FootPress: footswitches.press (m.a, now); break;
                case FootRelease: footswitches.release (m.a, now); break;
                case KeepCC: keepNow(); break;
                case Expression:
                {
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = m.a;
                        calLo = jmin (calLo, m.a);
                        calHi = jmax (calHi, m.a);
                        uiExp.store ((float) m.a / 127.0f);
                        break;
                    }
                    const float v = settings.expressionValue (m.a);
                    uiExp.store (v);
                    const auto& targets = names::expTargets();
                    const auto& t = targets[(size_t) jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget))];
                    if (String (t.id) == ParamIDs::expression && idx (ParamIDs::modSource) != 1)
                        setNow (ParamIDs::modSource, 1.0f); // moving the pedal hands the ramp targets to it
                    setNorm (t.id, v);
                    break;
                }
                case Learned:
                {
                    const int ch = m.a / 128, cc = m.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " LEARNED");
                    }
                    else if (what == LearnKeeper)
                    {
                        settings.keeperChannel = ch;
                        settings.keeperCC = cc;
                        say ("KEEPER SWITCH LEARNED");
                    }
                    else if (what == LearnExpression)
                    {
                        settings.expChannel = ch;
                        settings.expCC = cc;
                        say ("EXPRESSION LEARNED");
                    }
                    settings.save();
                    settingsStamp = PedalSettings::file().getLastModificationTime();
                    break;
                }
                default: break;
            }
        }
    };
    apply (scope.startIndex1, scope.blockSize1);
    apply (scope.startIndex2, scope.blockSize2);
}

void MurmurProcessor::applyPendingChanges (double now)
{
    // pedal settings changed by another copy, or another Flip Fella plugin: pick them up
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && hasFocus (this)) say ("FOOTSWITCHES READY");
        }
    }

    // MIDI CC changes (the original pedal map)
    {
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
    }

    drainPedal (pedalFifo, pedalPending, now);
    drainPedal (directFifo, directPending, now);

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    if (on (ParamIDs::keeper))
    {
        setNow (ParamIDs::keeper, 0.0f);
        keepNow();
    }

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            // whichever end the pedal started at is the heel
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            settings.save();
            settingsStamp = PedalSettings::file().getLastModificationTime();
            say ("EXPRESSION CALIBRATED");
        }
        else
            say ("NO MOVEMENT, TRY AGAIN");
    }

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

    // Mode switched by the mode card, a footswitch, automation or a preset
    const int mode = idx (ParamIDs::mode);
    if (mode != lastMode)
        onModeChanged (lastMode, mode);

    // A recording that fills the whole buffer closes itself and starts playing
    if (on (ParamIDs::loopRec) && engine.recordingFull())
    {
        setNow (ParamIDs::loopRec, 0.0f);
        setNow (ParamIDs::loopOn, 1.0f);
        say ("LOOP FULL, PLAYING");
    }
}

// =====================================================================================
// Modes. One set of controls; each mode keeps its own copy of their settings.
// =====================================================================================
bool MurmurProcessor::inModeStash (const String& id)
{
    // channel states and the things you set once for your rig stay put across a switch
    static const StringArray shared { ParamIDs::mode, ParamIDs::tempoSync, ParamIDs::inGain };
    return inSnapshot (id) && ! shared.contains (id);
}

ValueTree MurmurProcessor::modeStash (int mode) const
{
    const auto modes = apvts.state.getChildWithName ("Modes");
    for (const auto& child : modes)
        if ((int) child.getProperty ("mode", -1) == mode)
            return child;
    return {};
}

void MurmurProcessor::storeModeStash (int mode, const ValueTree& stash)
{
    auto modes = apvts.state.getOrCreateChildWithName ("Modes", nullptr);
    for (int i = modes.getNumChildren(); --i >= 0;)
        if ((int) modes.getChild (i).getProperty ("mode", -1) == mode)
            modes.removeChild (i, nullptr);
    auto copy = stash.createCopy();
    copy.setProperty ("mode", mode, nullptr);
    modes.appendChild (copy, nullptr);
}

void MurmurProcessor::onModeChanged (int from, int to)
{
    lastMode = to;
    if (applyingSnapshot) return;

    // keep the mode you're leaving exactly as it was
    ValueTree outgoing ("ModeStash");
    for (auto* p : params)
        if (inModeStash (p->getParameterID()))
            outgoing.setProperty (Identifier (p->getParameterID()), p->convertFrom0to1 (p->getValue()), nullptr);
    storeModeStash (from, outgoing);

    // and bring back the one you're entering, or its defaults the first time
    applyingSnapshot = true;
    const auto incoming = modeStash (to);
    for (auto* p : params)
    {
        const auto id = p->getParameterID();
        if (! inModeStash (id)) continue;
        if (incoming.isValid() && incoming.hasProperty (Identifier (id)))
            setNow (id, (float) incoming.getProperty (Identifier (id)));
        else if (! incoming.isValid())
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }
    if (! incoming.isValid())
        setNow (ParamIDs::expTarget, (float) names::defaultExpTarget (to));
    applyingSnapshot = false;

    uiModePulse.fetch_add (1);
    say (to == 1 ? "MURK" : "MURMUR");
}

void MurmurProcessor::switchMode()
{
    focused.store (this);
    setNow (ParamIDs::mode, currentMode() == 1 ? 0.0f : 1.0f);
    applyPendingChanges (nowSeconds());
}

// =====================================================================================
// Footswitches. Every job is the same in both modes; only the sound changes.
//   A  tap: wet on or off            hold: freeze (latches with Latch)
//   B  tap: loop record, play, stop  hold: overdub (latches with Latch)
//   C  tap: tap tempo                hold: save to the current preset slot
//   D  tap: next preset slot         hold: previous preset slot
//   A+B tap: both channels           B+C hold 1 s: switch mode    C+D hold 1 s: Keeper
//   C+D tap: hand the footswitches to the next Murmur
// =====================================================================================
String MurmurProcessor::tapJob (int, int sw)
{
    static const char* jobs[4] = { "WET ON / OFF", "REC / PLAY / STOP", "TAP TEMPO", "NEXT PRESET" };
    return jobs[jlimit (0, 3, sw)];
}

String MurmurProcessor::holdJob (int, int sw)
{
    static const char* jobs[4] = { "FREEZE", "OVERDUB", "SAVE PRESET", "PREVIOUS PRESET" };
    return jobs[jlimit (0, 3, sw)];
}

String MurmurProcessor::footState (int sw) const
{
    switch (sw)
    {
        case 0: return on (ParamIDs::wetOn) ? (on (ParamIDs::freeze) ? "FROZEN" : "WET ON") : "WET OFF";
        case 1: return on (ParamIDs::loopRec) ? "REC" : (on (ParamIDs::loopOn) ? (on (ParamIDs::overdub) ? "DUBBING" : "PLAYING") : "READY");
        case 2: { const float b = currentBpm(); return b > 0.0f ? String (roundToInt (b)) : String ("TAP"); }
        default: { const int s = idx (ParamIDs::presetSlot); return s == 0 ? "A" : (s == 2 ? "B" : "LIVE"); }
    }
}

void MurmurProcessor::footDown (int sw)
{
    focused.store (this);
    footswitches.press (sw, nowSeconds());
}

void MurmurProcessor::footUp (int sw)
{
    footswitches.release (sw, nowSeconds());
}

void MurmurProcessor::loopRecordTap()
{
    if (on (ParamIDs::loopOn))
    {
        setNow (ParamIDs::loopOn, 0.0f);
        setNow (ParamIDs::overdub, 0.0f);
        say ("LOOP STOPPED");
    }
    else if (on (ParamIDs::loopRec))
    {
        setNow (ParamIDs::loopRec, 0.0f);
        setNow (ParamIDs::loopOn, 1.0f);
        say ("LOOP PLAYING");
    }
    else
    {
        setNow (ParamIDs::loopRec, 1.0f);
        say ("RECORDING: TAP B TO PLAY");
    }
}

void MurmurProcessor::stepPreset (int direction)
{
    const int slot = idx (ParamIDs::presetSlot);
    const int next = (slot + (direction > 0 ? 1 : 2)) % 3; // A, LIVE, B, round again
    setNow (ParamIDs::presetSlot, (float) next);
    say (next == 0 ? "PRESET A" : (next == 2 ? "PRESET B" : "LIVE"));
}

void MurmurProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD || e == E::ComboBC) uiFootPulse.fetch_add (1);
    const bool latch = on (ParamIDs::latch);

    switch (e)
    {
        case E::ComboHoldCD:   // Keeper, on the plugin you were playing, so take the footswitches back
            focused.store (this);
            keepNow();
            return;
        case E::ComboCD:       // hand the footswitches to the next Murmur
        {
            const SpinLock::ScopedLockType l (focusLock);
            if (instances.size() > 1)
            {
                const int i = instances.indexOf (this);
                auto* next = instances[(i + 1) % instances.size()];
                focused.store (next);
                next->say ("FOOTSWITCHES HERE");
            }
            return;
        }
        case E::ComboHoldBC:   // switch mode, never on a tap
            switchMode();
            return;
        case E::ComboBC:
        case E::ComboHoldAB:
            return;
        case E::ComboAB:       // both channels on or off together
        {
            const bool anyOn = on (ParamIDs::wetOn) || on (ParamIDs::loopOn) || on (ParamIDs::loopRec);
            if (anyOn)
            {
                for (auto* id : { ParamIDs::wetOn, ParamIDs::loopOn, ParamIDs::loopRec, ParamIDs::freeze, ParamIDs::overdub })
                    setNow (id, 0.0f);
                say ("BOTH OFF");
            }
            else
            {
                setNow (ParamIDs::wetOn, 1.0f);
                setNow (ParamIDs::loopOn, 1.0f); // loops what it has just heard, like the pedal
                say ("BOTH ON");
            }
            return;
        }
        default: break;
    }

    if (sw == 0) // A: wet on or off, hold to freeze
    {
        if (e == E::Tap)
        {
            const bool turnOn = ! on (ParamIDs::wetOn);
            setNow (ParamIDs::wetOn, turnOn ? 1.0f : 0.0f);
            if (! turnOn) setNow (ParamIDs::freeze, 0.0f);
            say (turnOn ? "WET ON" : "WET OFF");
        }
        else if (e == E::HoldStart)
        {
            const bool fz = latch ? ! on (ParamIDs::freeze) : true;
            setNow (ParamIDs::freeze, fz ? 1.0f : 0.0f);
            if (! on (ParamIDs::wetOn)) setNow (ParamIDs::wetOn, 1.0f);
            say (fz ? "FROZEN" : "THAWED");
        }
        else if (e == E::HoldEnd && ! latch)
            setNow (ParamIDs::freeze, 0.0f);
    }
    else if (sw == 1) // B: loop record, play, stop; hold to overdub
    {
        if (e == E::Tap)
            loopRecordTap();
        else if (e == E::HoldStart)
        {
            if (on (ParamIDs::loopRec)) setNow (ParamIDs::loopRec, 0.0f);
            if (! on (ParamIDs::loopOn)) setNow (ParamIDs::loopOn, 1.0f);
            const bool dub = latch ? ! on (ParamIDs::overdub) : true;
            setNow (ParamIDs::overdub, dub ? 1.0f : 0.0f);
            say (dub ? "OVERDUB" : "OVERDUB OFF");
        }
        else if (e == E::HoldEnd && ! latch)
            setNow (ParamIDs::overdub, 0.0f);
    }
    else if (sw == 2) // C: tap tempo, hold to save
    {
        if (e == E::Tap)
            tapTempo();
        else if (e == E::HoldStart)
        {
            const int slot = idx (ParamIDs::presetSlot);
            if (slot == 1) say ("PICK A OR B TO SAVE");
            else { savePresetSlot (slot); say (slot == 0 ? "SAVED TO A" : "SAVED TO B"); }
        }
    }
    else if (sw == 3) // D: next preset, hold for previous
    {
        if (e == E::Tap) stepPreset (1);
        else if (e == E::HoldStart) stepPreset (-1);
    }
}

// =====================================================================================
// Learn and calibration
// =====================================================================================
void MurmurProcessor::learnFootswitch (int sw)
{
    focused.store (this);
    learnTarget.store (sw);
    say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw)));
}

void MurmurProcessor::learnExpression()
{
    focused.store (this);
    learnTarget.store (LearnExpression);
    say ("MOVE THE EXPRESSION PEDAL");
}

void MurmurProcessor::learnKeeper()
{
    focused.store (this);
    learnTarget.store (LearnKeeper);
    say ("PRESS YOUR KEEPER SWITCH");
}

void MurmurProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = nowSeconds() + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void MurmurProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

void MurmurProcessor::setPedalMapChannel (int channel)
{
    settings.pedalMapChannel = jlimit (0, 16, channel);
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

void MurmurProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setWanted (device);
}

void MurmurProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

// =====================================================================================
// Keeper
// =====================================================================================
void MurmurProcessor::keepNow()
{
    const int mode = currentMode();
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "Murmur");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("mode", mode == 1 ? "Murk" : "Murmur");
    e->setProperty ("recreates", mode == 1 ? "Chase Bliss Bad Mood" : "Chase Bliss MOOD MKII");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    const int slot = idx (ParamIDs::presetSlot);
    e->setProperty ("preset", slot == 0 ? "A" : (slot == 2 ? "B" : "Live"));
    e->setProperty ("tempo", currentBpm() > 0.0f ? currentBpm() : hostBpm.load());
    e->setProperty ("tempoFromHost", idx (ParamIDs::tempoSync) == 1);

    // what the panel reads, in words
    static const char* wetNames[2][3] = { { "Reverb", "Delay", "Slip" }, { "Soup", "Relay", "Flip" } };
    static const char* loopNames[2][3] = { { "Env", "Tape", "Stretch" }, { "Burst", "Radio", "Mask" } };
    DynamicObject::Ptr panel = new DynamicObject();
    panel->setProperty ("wet", wetNames[mode][jlimit (0, 2, idx (mode == 1 ? ParamIDs::murkWet : ParamIDs::wetMode))]);
    panel->setProperty ("wetOn", on (ParamIDs::wetOn));
    panel->setProperty ("frozen", on (ParamIDs::freeze));
    panel->setProperty ("loop", loopNames[mode][jlimit (0, 2, idx (mode == 1 ? ParamIDs::murkLoop : ParamIDs::loopMode))]);
    panel->setProperty ("loopOn", on (ParamIDs::loopOn));
    panel->setProperty ("recording", on (ParamIDs::loopRec));
    panel->setProperty ("clockHz", engine.uiClockHz.load());
    e->setProperty ("panel", var (panel.get()));

    DynamicObject::Ptr exp = new DynamicObject();
    const auto& targets = names::expTargets();
    exp->setProperty ("target", targets[(size_t) jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget))].label);
    exp->setProperty ("position", uiExp.load());
    exp->setProperty ("movedSecondsAgo", nowSeconds() - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    // every parameter, hidden ones included, as value and as shown on the panel
    DynamicObject::Ptr ps = new DynamicObject();
    for (auto* rp : params)
    {
        if (rp->getParameterID() == ParamIDs::keeper) continue;
        DynamicObject::Ptr pv = new DynamicObject();
        pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
        pv->setProperty ("shown", rp->getCurrentValueAsText());
        ps->setProperty (rp->getParameterID(), var (pv.get()));
    }
    e->setProperty ("parameters", var (ps.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File MurmurProcessor::exportKeepers()
{
    const auto file = keeper.exportZip();
    if (file.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); file.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return file;
}

void MurmurProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

// =====================================================================================
// Presets A and B: each saves the mode and both modes' settings
// =====================================================================================
bool MurmurProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::wetOn, ParamIDs::loopOn, ParamIDs::loopRec, ParamIDs::freeze, ParamIDs::overdub,
                                        ParamIDs::presetSlot, ParamIDs::expression, ParamIDs::tapBpm, ParamIDs::keeper,
                                        ParamIDs::synthEnable, ParamIDs::synthGate, ParamIDs::attack, ParamIDs::decay, ParamIDs::sustain,
                                        ParamIDs::release, ParamIDs::glide, ParamIDs::synthRoot };
    return ! excluded.contains (id);
}

ValueTree MurmurProcessor::snapshotOf() const
{
    ValueTree t ("Snapshot");
    for (auto* p : params)
        if (inSnapshot (p->getParameterID()))
            t.setProperty (Identifier (p->getParameterID()), p->convertFrom0to1 (p->getValue()), nullptr);
    const auto other = modeStash (1 - idx (ParamIDs::mode));
    if (other.isValid())
    {
        ValueTree o ("OtherMode");
        o.copyPropertiesFrom (other, nullptr);
        t.appendChild (o, nullptr);
    }
    return t;
}

void MurmurProcessor::applySnapshot (const ValueTree& t)
{
    applyingSnapshot = true;
    const Identifier modeId (ParamIDs::mode);
    if (t.hasProperty (modeId))
    {
        setNow (ParamIDs::mode, (float) t.getProperty (modeId));
        lastMode = (int) std::lround ((double) t.getProperty (modeId));
    }
    for (auto* p : params)
    {
        const Identifier id (p->getParameterID());
        if (p->getParameterID() != ParamIDs::mode && inSnapshot (p->getParameterID()) && t.hasProperty (id))
            setNow (p->getParameterID(), (float) t.getProperty (id));
    }
    const auto other = t.getChildWithName ("OtherMode");
    if (other.isValid())
    {
        ValueTree o ("ModeStash");
        o.copyPropertiesFrom (other, nullptr);
        storeModeStash (1 - lastMode, o);
    }
    applyingSnapshot = false;
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
    for (const char* id : { ParamIDs::width, ParamIDs::tone, ParamIDs::fade, ParamIDs::direct, ParamIDs::balance, ParamIDs::sync,
                            ParamIDs::halfLength, ParamIDs::spreadSolo, ParamIDs::rampWave, ParamIDs::glue, ParamIDs::cross,
                            ParamIDs::crossSource, ParamIDs::eqTilt, ParamIDs::eqOn, ParamIDs::dryGlue })
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
    const double now = nowSeconds();
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
        say ("TEMPO " + String (roundToInt (jlimit (40.0, 240.0, 60.0 / avg))) + " BPM");
    }
    else say ("TAP AGAIN FOR TEMPO");
}

AudioProcessorEditor* MurmurProcessor::createEditor()
{
    focused.store (this); // opening a copy's window gives it the footswitches
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
            lastMode = idx (ParamIDs::mode);       // or swap the mode's settings
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MurmurProcessor();
}
