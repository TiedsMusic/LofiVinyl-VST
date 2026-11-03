#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class LoFiVinylProcessor : public juce::AudioProcessor
{
public:
    LoFiVinylProcessor();
    ~LoFiVinylProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LoFi Vinyl"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    using SVF   = juce::dsp::StateVariableTPTFilter<float>;

    // Paramètres (lus de façon atomique dans le thread audio)
    std::atomic<float>* wowParam     = nullptr;
    std::atomic<float>* flutterParam = nullptr;
    std::atomic<float>* lowCutParam  = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* warmthParam  = nullptr;
    std::atomic<float>* crackleParam = nullptr;
    std::atomic<float>* noiseParam   = nullptr;
    std::atomic<float>* mixParam     = nullptr;

    // DSP
    Delay wowDelay { 4096 }, dryDelay { 4096 };
    SVF   highPass, lowPass, warmLow;

    double sr = 44100.0;

    // Modulation
    double wowPhase = 0.0, flutterPhase = 0.0;
    float  drift = 0.0f, driftTarget = 0.0f;
    int    driftCounter = 0;

    // Bruit de vinyle
    juce::Random rng;
    float clickEnv = 0.0f;
    float hissState[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoFiVinylProcessor)
};
