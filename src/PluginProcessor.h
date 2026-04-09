#pragma once

#include <JuceHeader.h>

class JuiceAudioProcessor final : public juce::AudioProcessor
{
public:
    JuiceAudioProcessor();
    ~JuiceAudioProcessor() override = default;

    using APVTS = juce::AudioProcessorValueTreeState;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return JucePlugin_WantsMidiInput; }
    bool producesMidi() const override { return JucePlugin_ProducesMidiOutput; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    APVTS& getValueTreeState() { return parameters; }
    const APVTS& getValueTreeState() const { return parameters; }

    static APVTS::ParameterLayout createParameterLayout();

private:
    struct TapeChannelState
    {
        float flux = 0.0f;
        float hfMemory = 0.0f;
        float hfShelfLp = 0.0f;
        float tapeHfLp = 0.0f;
        float bleedHp = 0.0f;
        float bleedLp = 0.0f;
        float bleedEnv = 0.0f;
        float wowPhase = 0.0f;
        float flutterPhase = 0.0f;
        float hissLp = 0.0f;
        uint32_t noiseSeed = 0x12345678u;
        int wowWritePosition = 0;
        int printWritePos = 0;
        float azApX = 0.0f;
        float azApY = 0.0f;
        float azToneLp = 0.0f;
    };

    float processTapeSample(float inputSample, TapeChannelState& state, float trackVariance,
                             float ips, float calSaturation, float calHfBoost,
                             float biasParam, float headBumpFreqHz, float headBumpAmt) const;

    APVTS parameters;
    std::vector<TapeChannelState> channelStates;
    std::array<float, 24> trackVarianceTable {};
    std::vector<std::vector<float>> wowDelayBuffers;
    int wowDelayBufferSize = 0;
    std::vector<std::vector<float>> printBuffers;
    int printBufferSize = 0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuiceAudioProcessor)
};
