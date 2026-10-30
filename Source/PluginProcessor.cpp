#include "PluginProcessor.h"
#include "PluginEditor.h"

MurmurProcessor::MurmurProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MurmurState", createParameterLayout())
{
    auto bind = [this] (Raw& r, const char* id) { r.p = apvts.getRawParameterValue (id); jassert (r.p != nullptr); };
    bind (this->mix, ParamIDs::mix); bind (this->clock, ParamIDs::clock); bind (this->routing, ParamIDs::routing);
    bind (this->wetOn, ParamIDs::wetOn); bind (this->freeze, ParamIDs::freeze); bind (this->wetMode, ParamIDs::wetMode);
    bind (this->time, ParamIDs::time); bind (this->wetModify, ParamIDs::wetModify);
    bind (this->loopOn, ParamIDs::loopOn); bind (this->overdub, ParamIDs::overdub); bind (this->loopMode, ParamIDs::loopMode);
    bind (this->length, ParamIDs::length); bind (this->loopModify, ParamIDs::loopModify);
    bind (this->width, ParamIDs::width); bind (this->tone, ParamIDs::tone); bind (this->fade, ParamIDs::fade);
    bind (this->direct, ParamIDs::direct); bind (this->balance, ParamIDs::balance); bind (this->sync, ParamIDs::sync);
    bind (this->halfLength, ParamIDs::halfLength); bind (this->spreadSolo, ParamIDs::spreadSolo);
    bind (this->miso, ParamIDs::miso); bind (this->spread, ParamIDs::spread); bind (this->dryKill, ParamIDs::dryKill);
    bind (this->trails, ParamIDs::trails); bind (this->noDub, ParamIDs::noDub); bind (this->smoothClock, ParamIDs::smoothClock);
    bind (this->classic, ParamIDs::classic);
    bind (this->inGain, ParamIDs::inGain); bind (this->outGain, ParamIDs::outGain);
    bind (this->bufferSeconds, ParamIDs::bufferSeconds); bind (this->stepSet, ParamIDs::stepSet);
}

void MurmurProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    work.setSize (2, std::max (samplesPerBlock, 1));
}

bool MurmurProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

murmur::EngineParams MurmurProcessor::readParams() const
{
    murmur::EngineParams p;
    p.mix = mix.get(); p.clock = clock.get(); p.routing = routing.idx();
    p.wetOn = wetOn.on(); p.freeze = freeze.on(); p.wetMode = wetMode.idx();
    p.time = time.get(); p.wetModify = wetModify.get();
    p.loopOn = loopOn.on(); p.overdub = overdub.on(); p.loopMode = loopMode.idx();
    p.length = length.get(); p.loopModify = loopModify.get();
    p.width = width.get(); p.toneHz = tone.get(); p.fade = fade.get(); p.direct = direct.get();
    p.balance = balance.get(); p.sync = sync.idx(); p.halfLength = halfLength.on(); p.spreadSolo = spreadSolo.idx();
    p.miso = miso.on(); p.spread = spread.on(); p.dryKill = dryKill.on(); p.trails = trails.on();
    p.noDub = noDub.on(); p.smoothClock = smoothClock.on(); p.classic = classic.on();
    p.inGainDb = inGain.get(); p.outGainDb = outGain.get();
    p.bufferSeconds = bufferSeconds.get(); p.stepSet = stepSet.idx();
    return p;
}

void MurmurProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inChans = getTotalNumInputChannels();
    const int outChans = getTotalNumOutputChannels();
    if (n == 0 || outChans < 2)
        return;

    // A mono input feeds both sides
    if (inChans == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    const auto p = readParams();
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, p);
}

juce::AudioProcessorEditor* MurmurProcessor::createEditor()
{
    return new MurmurEditor (*this);
}

void MurmurProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MurmurProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MurmurProcessor();
}
