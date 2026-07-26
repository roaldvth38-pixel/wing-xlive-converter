#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

class Dolby740AudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    Dolby740AudioProcessor();
    ~Dolby740AudioProcessor() override;

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
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    APVTS& getValueTreeState() { return parameters; }
    const APVTS& getValueTreeState() const { return parameters; }

    float getInputMeterDb(int strip) const noexcept
    {
        const auto index = static_cast<size_t>(juce::jlimit(0, kNumStrips - 1, strip));
        return inputMeterDb[index].load(std::memory_order_relaxed);
    }

    float getOutputMeterDb(int strip) const noexcept
    {
        const auto index = static_cast<size_t>(juce::jlimit(0, kNumStrips - 1, strip));
        return outputMeterDb[index].load(std::memory_order_relaxed);
    }

    float getDetailActivityPct(int strip) const noexcept
    {
        const auto index = static_cast<size_t>(juce::jlimit(0, kNumStrips - 1, strip));
        return detailActivityPct[index].load(std::memory_order_relaxed);
    }

    static APVTS::ParameterLayout createParameterLayout();

private:
    static constexpr int kNumStrips = 2;

    struct DetectorState
    {
        float rmsSq = 1.0e-9f;
        float peak = 0.0f;
    };

    APVTS parameters;
    double currentSampleRate = 44100.0;

    // Dual mono strips with separate detector states.
    std::array<DetectorState, kNumStrips> detectorStates {{
        { 1.0e-9f, 0.0f },
        { 1.0e-9f, 0.0f }
    }};

    // Input filter states per strip for selectable HP/LP filters.
    std::array<float, kNumStrips> hpFilterState { 0.0f, 0.0f };
    std::array<float, kNumStrips> lpFilterState { 0.0f, 0.0f };

    // Analog-style NR split states per strip.
    std::array<float, kNumStrips> nrLowBandState { 0.0f, 0.0f };
    std::array<float, kNumStrips> nrLowBandState2 { 0.0f, 0.0f };
    std::array<float, kNumStrips> nrHighBandLpState { 0.0f, 0.0f };
    std::array<float, kNumStrips> nrHighBandLpState2 { 0.0f, 0.0f };

    // Cascaded one-pole states for cleaner low/mid/high crossover split per strip.
    std::array<float, kNumStrips> lowBandState { 0.0f, 0.0f };
    std::array<float, kNumStrips> lowBandState2 { 0.0f, 0.0f };
    std::array<float, kNumStrips> highBandLpState { 0.0f, 0.0f };
    std::array<float, kNumStrips> highBandLpState2 { 0.0f, 0.0f };

    // Side Chain mode transition smoothing per strip.
    std::array<int, kNumStrips> previousEqMode { 0, 0 };
    std::array<float, kNumStrips> sidechainModeEntryGain { 1.0f, 1.0f };

    // Analog mode drift phases per strip for subtle channel variance.
    std::array<float, kNumStrips> analogDriftPhaseA { 0.0f, 1.7f };
    std::array<float, kNumStrips> analogDriftPhaseB { 2.2f, 3.9f };

    // Slow random modulation states per strip for analog variability.
    std::array<float, kNumStrips> analogNoiseA { 0.0f, 0.0f };
    std::array<float, kNumStrips> analogNoiseB { 0.0f, 0.0f };
    std::array<float, kNumStrips> analogNoiseC { 0.0f, 0.0f };
    std::array<std::uint32_t, kNumStrips> analogNoiseSeed { 0x1f123bb5u, 0x93ab47cdu };

    static constexpr int kAnalogRippleDelaySize = 512;

    // Full-spectrum current-style ripple delay states per strip.
    std::array<std::array<float, kAnalogRippleDelaySize>, kNumStrips> analogRippleDelayBuffer {};
    std::array<int, kNumStrips> analogRippleDelayIndex { 0, 0 };

    // Sowter-style transformer input/output color states per strip.
    std::array<float, kNumStrips> transformerInMagState { 0.0f, 0.0f };
    std::array<float, kNumStrips> transformerInBodyState { 0.0f, 0.0f };
    std::array<float, kNumStrips> transformerInAirState { 0.0f, 0.0f };

    std::array<float, kNumStrips> transformerOutMagState { 0.0f, 0.0f };
    std::array<float, kNumStrips> transformerOutBodyState { 0.0f, 0.0f };
    std::array<float, kNumStrips> transformerOutAirState { 0.0f, 0.0f };

    std::array<float, kNumStrips> inputMeterStateDb { -100.0f, -100.0f };
    std::array<float, kNumStrips> outputMeterStateDb { -100.0f, -100.0f };

    std::array<std::atomic<float>, kNumStrips> inputMeterDb {
        std::atomic<float> { -100.0f },
        std::atomic<float> { -100.0f }
    };

    std::array<std::atomic<float>, kNumStrips> outputMeterDb {
        std::atomic<float> { -100.0f },
        std::atomic<float> { -100.0f }
    };

    std::array<std::atomic<float>, kNumStrips> detailActivityPct {
        std::atomic<float> { 0.0f },
        std::atomic<float> { 0.0f }
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dolby740AudioProcessor)
};
