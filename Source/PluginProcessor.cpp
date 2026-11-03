#include "PluginProcessor.h"
#include <cmath>

namespace
{
    constexpr float kBaseDelayMs = 12.0f;   // retard moyen autour duquel le wobble oscille
    constexpr float kMaxDelayMs  = 30.0f;
    constexpr double kTwoPi      = 6.283185307179586;
}

//==============================================================================
LoFiVinylProcessor::LoFiVinylProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    wowParam     = apvts.getRawParameterValue ("wow");
    flutterParam = apvts.getRawParameterValue ("flutter");
    lowCutParam  = apvts.getRawParameterValue ("lowcut");
    highCutParam = apvts.getRawParameterValue ("highcut");
    warmthParam  = apvts.getRawParameterValue ("warmth");
    crackleParam = apvts.getRawParameterValue ("crackle");
    noiseParam   = apvts.getRawParameterValue ("noise");
    mixParam     = apvts.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout LoFiVinylProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto add01 = [&] (const char* id, const char* name, float def)
    {
        params.push_back (std::make_unique<P> (juce::ParameterID { id, 1 }, name,
                                               Range (0.0f, 1.0f, 0.001f), def));
    };

    add01 ("wow",     "Wow",     0.30f);
    add01 ("flutter", "Flutter", 0.20f);

    params.push_back (std::make_unique<P> (juce::ParameterID { "lowcut", 1 }, "Low Cut",
                                           Range (20.0f, 500.0f, 1.0f, 0.4f), 60.0f,
                                           juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    params.push_back (std::make_unique<P> (juce::ParameterID { "highcut", 1 }, "High Cut",
                                           Range (1000.0f, 20000.0f, 1.0f, 0.4f), 6000.0f,
                                           juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    add01 ("warmth",  "Warmth",  0.40f);
    add01 ("crackle", "Crackle", 0.30f);
    add01 ("noise",   "Noise",   0.20f);
    add01 ("mix",     "Mix",     1.00f);

    return { params.begin(), params.end() };
}

//==============================================================================
bool LoFiVinylProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void LoFiVinylProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    const int maxDelay = (int) std::ceil (kMaxDelayMs * 0.001 * sampleRate) + 4;

    wowDelay.setMaximumDelayInSamples (maxDelay);
    wowDelay.prepare (spec);
    wowDelay.reset();

    dryDelay.setMaximumDelayInSamples (maxDelay);
    dryDelay.prepare (spec);
    dryDelay.reset();

    highPass.prepare (spec);
    lowPass.prepare (spec);
    warmLow.prepare (spec);

    highPass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowPass.setType  (juce::dsp::StateVariableTPTFilterType::lowpass);
    warmLow.setType  (juce::dsp::StateVariableTPTFilterType::lowpass);
    warmLow.setCutoffFrequency (300.0f);

    highPass.reset();
    lowPass.reset();
    warmLow.reset();

    wowPhase = flutterPhase = 0.0;
    drift = driftTarget = 0.0f;
    driftCounter = 0;
    clickEnv = 0.0f;
    hissState[0] = hissState[1] = 0.0f;
}

//==============================================================================
void LoFiVinylProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();

    for (int c = numCh; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    // --- Lecture des paramètres
    const float wow     = wowParam->load();
    const float flutter = flutterParam->load();
    const float warmth  = warmthParam->load();
    const float crackle = crackleParam->load();
    const float noise   = noiseParam->load();
    const float mix     = mixParam->load();

    highPass.setCutoffFrequency (lowCutParam->load());
    lowPass.setCutoffFrequency  (juce::jmin (highCutParam->load(), (float) (0.45 * sr)));

    // --- Constantes dérivées (profondeurs en échantillons)
    const float fs         = (float) sr;
    const float baseDelay  = kBaseDelayMs * 0.001f * fs;
    const float wowDepth   = wow     * 0.0030f * fs;   // jusqu'à 3 ms
    const float flutDepth  = flutter * 0.0002f * fs;   // jusqu'à 0.2 ms
    const float driftDepth = wow     * 0.0010f * fs;   // dérive lente aléatoire

    const double wowInc     = kTwoPi * 0.6 / sr;       // ~0.6 Hz
    const double flutterInc = kTwoPi * 7.0 / sr;       // ~7 Hz
    const float  driftCoeff = 1.0f - std::exp (-1.0f / (0.3f * fs));

    const float clickProb = (crackle * 30.0f) / fs;    // jusqu'à ~30 clics/seconde
    const float hissGain  = noise * 0.05f;

    float* data[2] { buffer.getWritePointer (0),
                     numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < n; ++i)
    {
        // --- Modulation du retard (partagée entre L et R, comme un vrai disque)
        wowPhase += wowInc;      if (wowPhase >= kTwoPi)     wowPhase -= kTwoPi;
        flutterPhase += flutterInc; if (flutterPhase >= kTwoPi) flutterPhase -= kTwoPi;

        if (--driftCounter <= 0)
        {
            driftTarget  = rng.nextFloat() * 2.0f - 1.0f;
            driftCounter = (int) (0.15f * fs);
        }
        drift += driftCoeff * (driftTarget - drift);

        const float mod = wowDepth   * (float) std::sin (wowPhase)
                        + flutDepth  * (float) std::sin (flutterPhase)
                        + driftDepth * drift;
        const float delay = baseDelay + mod;

        // --- Crépitements : impulsions aléatoires qui s'éteignent vite
        if (rng.nextFloat() < clickProb)
        {
            const float r = rng.nextFloat();
            clickEnv = r * r * 0.5f;
        }
        const float click = clickEnv * (rng.nextFloat() * 2.0f - 1.0f);
        clickEnv *= 0.8f;

        for (int c = 0; c < numCh; ++c)
        {
            const float in = data[c][i];

            // Signal sec retardé du retard moyen (évite l'effet de peigne avec Mix)
            dryDelay.pushSample (c, in);
            const float dry = dryDelay.popSample (c, baseDelay);

            // Wobble
            wowDelay.pushSample (c, in);
            float y = wowDelay.popSample (c, delay);

            // EQ lo-fi : passe-haut, puis bruit/crépitements, puis passe-bas
            y = highPass.processSample (c, y);

            hissState[c] += 0.3f * ((rng.nextFloat() * 2.0f - 1.0f) - hissState[c]);
            y += click + hissGain * hissState[c];

            y = lowPass.processSample (c, y);

            // Chaleur : boost des graves + saturation douce (gain unitaire en petit signal)
            y += warmth * 0.6f * warmLow.processSample (c, y);
            const float k = 2.5f;
            y = (1.0f - warmth) * y + warmth * std::tanh (k * y) / k;

            data[c][i] = dry * (1.0f - mix) + y * mix;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* LoFiVinylProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void LoFiVinylProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LoFiVinylProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoFiVinylProcessor();
}
