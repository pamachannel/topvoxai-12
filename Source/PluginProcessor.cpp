#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace topvox;

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& s = spec (i);
        const juce::String unit = juce::String::fromUTF8 (s.unit);
        const int dec = s.dec;

        juce::NormalisableRange<float> range (s.mn, s.mx, s.step, s.skew);

        auto attrs = juce::AudioParameterFloatAttributes()
                         .withStringFromValueFunction ([unit, dec] (float v, int) { return juce::String (v, dec) + unit; })
                         .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { s.id, 1 },
                                                                 juce::String::fromUTF8 (s.name),
                                                                 range, s.def, attrs));
    }
    return layout;
}

TopVoxProcessor::TopVoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TOPVOX_VOICE", createLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        raw[(size_t) i] = apvts.getRawParameterValue (spec (i).id);

    apvts.state.setProperty ("style", 1, nullptr);
}

void TopVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate);
    analyzer.prepare (sampleRate);
    scratchR.assign ((size_t) juce::jmax (samplesPerBlock, 8192), 0.f);
}

bool TopVoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void TopVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int total = buffer.getNumSamples();
    if (numCh < 1 || total < 1) return;

    for (int ch = 2; ch < numCh; ++ch)
        buffer.clear (ch, 0, total);

    Params p;
    for (int i = 0; i < kNumParams; ++i)
        p[i] = raw[(size_t) i]->load();

    float* l = buffer.getWritePointer (0);
    float* r = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    const int chunkMax = (int) scratchR.size();

    for (int off = 0; off < total; off += chunkMax)
    {
        const int n = juce::jmin (chunkMax, total - off);
        float* pl = l + off;
        float* pr = r != nullptr ? r + off : scratchR.data();

        if (r == nullptr)
            std::copy (pl, pl + n, pr);          // entrée mono : on duplique sur un canal interne

        analyzer.feed (pl, pr, n);               // analyse de la voix à l'entrée
        chain.process (pl, pr, n, p);

        if (r == nullptr)
            for (int i = 0; i < n; ++i)          // sortie mono : somme des deux canaux
                pl[i] = 0.5f * (pl[i] + pr[i]);
    }
}

juce::AudioProcessorEditor* TopVoxProcessor::createEditor()
{
    return new TopVoxEditor (*this);
}

void TopVoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TopVoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void TopVoxProcessor::applyParams (const Params& p)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (auto* prm = apvts.getParameter (spec (i).id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (p[i]));
            prm->endChangeGesture();
        }
    }
}

void TopVoxProcessor::applyStyle (int style)
{
    applyParams (stylePreset (style));
}

bool TopVoxProcessor::runAutoMix (int style)
{
    const auto rep = analyzer.report();
    if (! rep.valid)
        return false;
    applyParams (autoMix (rep, style));
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TopVoxProcessor();
}
