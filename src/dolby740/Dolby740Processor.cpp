#include "Dolby740Processor.h"
#include "Dolby740Editor.h"

#include <cmath>

namespace
{
constexpr std::array<const char*, 2> thresholdIds { "thresholdA", "thresholdB" };
constexpr std::array<const char*, 2> sourceNrIds { "sourceNrA", "sourceNrB" };
constexpr std::array<const char*, 2> lowBoostIds { "lowBoostA", "lowBoostB" };
constexpr std::array<const char*, 2> midBoostIds { "midBoostA", "midBoostB" };
constexpr std::array<const char*, 2> highBoostIds { "highBoostA", "highBoostB" };
constexpr std::array<const char*, 2> lowMidXoverIds { "lowMidXoverA", "lowMidXoverB" };
constexpr std::array<const char*, 2> midHighXoverIds { "midHighXoverA", "midHighXoverB" };
constexpr std::array<const char*, 2> eqModeIds { "eqModeA", "eqModeB" };
constexpr std::array<const char*, 2> hpFilterIds { "hpFilterA", "hpFilterB" };
constexpr std::array<const char*, 2> lpFilterIds { "lpFilterA", "lpFilterB" };
constexpr std::array<const char*, 2> outputGainIds { "outputGainA", "outputGainB" };

constexpr auto stereoLinkId = "stereoLink";
constexpr auto analogModeId = "analogMode";
constexpr auto analogInOutStageId = "analogInOutStage";

// Threshold is a direct dBFS control for practical DAW-side calibration.
}

Dolby740AudioProcessor::Dolby740AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
    , parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

Dolby740AudioProcessor::~Dolby740AudioProcessor() = default;

Dolby740AudioProcessor::APVTS::ParameterLayout Dolby740AudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addStripParameters = [&params](int strip)
    {
        const auto index = static_cast<size_t>(strip);
        const juce::String tag = strip == 0 ? "A" : "B";

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(thresholdIds[index], 1),
            tag + " Threshold",
            juce::NormalisableRange<float>(-60.0f, -40.0f, 0.1f),
            -40.0f,
            juce::AudioParameterFloatAttributes().withLabel("dBFS")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(sourceNrIds[index], 1),
            tag + " Source NR",
            juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f),
            8.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(lowBoostIds[index], 1),
            tag + " Low Boost",
            juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(midBoostIds[index], 1),
            tag + " Mid Boost",
            juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(highBoostIds[index], 1),
            tag + " High Boost",
            juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(lowMidXoverIds[index], 1),
            tag + " Low-Mid Crossover",
            juce::NormalisableRange<float>(75.0f, 1000.0f, 1.0f, 0.45f),
            300.0f,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(midHighXoverIds[index], 1),
            tag + " Mid-High Crossover",
            juce::NormalisableRange<float>(500.0f, 8000.0f, 1.0f, 0.45f),
            2000.0f,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(eqModeIds[index], 1),
            tag + " EQ Mode",
            juce::StringArray { "In", "Side Chain", "Out" },
            0));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(hpFilterIds[index], 1),
            tag + " Highpass Filter",
            juce::StringArray { "100 Hz", "200 Hz" },
            0));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(lpFilterIds[index], 1),
            tag + " Lowpass Filter",
            juce::StringArray { "4 kHz", "8 kHz" },
            1));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(outputGainIds[index], 1),
            tag + " Output",
            juce::NormalisableRange<float>(-14.0f, 6.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
    };

    addStripParameters(0);
    addStripParameters(1);

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(stereoLinkId, 1),
        "Stereo Link",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(analogModeId, 1),
        "Analog Mode",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(analogInOutStageId, 1),
        "Analog In/Out Stage",
        false));

    return { params.begin(), params.end() };
}

void Dolby740AudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (int strip = 0; strip < kNumStrips; ++strip)
    {
        detectorStates[static_cast<size_t>(strip)].rmsSq = 1.0e-9f;
        detectorStates[static_cast<size_t>(strip)].peak = 0.0f;

        hpFilterState[static_cast<size_t>(strip)] = 0.0f;
        lpFilterState[static_cast<size_t>(strip)] = 0.0f;
        nrLowBandState[static_cast<size_t>(strip)] = 0.0f;
        nrLowBandState2[static_cast<size_t>(strip)] = 0.0f;
        nrHighBandLpState[static_cast<size_t>(strip)] = 0.0f;
        nrHighBandLpState2[static_cast<size_t>(strip)] = 0.0f;
        lowBandState[static_cast<size_t>(strip)] = 0.0f;
        lowBandState2[static_cast<size_t>(strip)] = 0.0f;
        highBandLpState[static_cast<size_t>(strip)] = 0.0f;
        highBandLpState2[static_cast<size_t>(strip)] = 0.0f;
        previousEqMode[static_cast<size_t>(strip)] = 0;
        sidechainModeEntryGain[static_cast<size_t>(strip)] = 1.0f;
        analogDriftPhaseA[static_cast<size_t>(strip)] = strip == 0 ? 0.0f : 1.7f;
        analogDriftPhaseB[static_cast<size_t>(strip)] = strip == 0 ? 2.2f : 3.9f;
        analogNoiseA[static_cast<size_t>(strip)] = 0.0f;
        analogNoiseB[static_cast<size_t>(strip)] = 0.0f;
        analogNoiseC[static_cast<size_t>(strip)] = 0.0f;
        analogNoiseSeed[static_cast<size_t>(strip)] = strip == 0 ? 0x1f123bb5u : 0x93ab47cdu;
        analogRippleDelayBuffer[static_cast<size_t>(strip)].fill(0.0f);
        analogRippleDelayIndex[static_cast<size_t>(strip)] = 0;

        transformerInMagState[static_cast<size_t>(strip)] = 0.0f;
        transformerInBodyState[static_cast<size_t>(strip)] = 0.0f;
        transformerInAirState[static_cast<size_t>(strip)] = 0.0f;

        transformerOutMagState[static_cast<size_t>(strip)] = 0.0f;
        transformerOutBodyState[static_cast<size_t>(strip)] = 0.0f;
        transformerOutAirState[static_cast<size_t>(strip)] = 0.0f;

        inputMeterStateDb[static_cast<size_t>(strip)] = -100.0f;
        outputMeterStateDb[static_cast<size_t>(strip)] = -100.0f;

        inputMeterDb[static_cast<size_t>(strip)].store(-100.0f, std::memory_order_relaxed);
        outputMeterDb[static_cast<size_t>(strip)].store(-100.0f, std::memory_order_relaxed);
        detailActivityPct[static_cast<size_t>(strip)].store(0.0f, std::memory_order_relaxed);
    }
}

void Dolby740AudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Dolby740AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

#if !JucePlugin_IsSynth
    if (output != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void Dolby740AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear(channel, 0, numSamples);

    if (numSamples <= 0 || totalOutputChannels <= 0)
        return;

    auto* left = buffer.getWritePointer(0);
    auto* right = totalOutputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    const auto stereoLinked = parameters.getRawParameterValue(stereoLinkId)->load() > 0.5f;
    const auto sampleRateFloat = static_cast<float>(currentSampleRate);

    struct ChannelRuntime
    {
        float thresholdDbFs = -68.0f;
        float sourceNrDb = 0.0f;

        float lowGain = 1.0f;
        float midGain = 1.0f;
        float highGain = 1.0f;

        float lowAlpha = 0.0f;
        float highAlpha = 0.0f;

        float hpAlpha = 0.0f;
        float lpAlpha = 0.0f;

        int eqMode = 0;
        bool analogMode = false;
        bool analogInOutStage = false;
        float inModeLevelMatchGain = 1.0f;
        float sidechainModeLevelMatchGain = 1.0f;
        float outputGain = 1.0f;
    };

    auto buildRuntime = [&](int strip) -> ChannelRuntime
    {
        const auto parameterStrip = (strip == 1 && stereoLinked) ? 0 : strip;
        const auto index = static_cast<size_t>(parameterStrip);

        ChannelRuntime runtime;

        runtime.thresholdDbFs = parameters.getRawParameterValue(thresholdIds[index])->load();

        runtime.sourceNrDb = parameters.getRawParameterValue(sourceNrIds[index])->load();

        const auto lowBoostDb = parameters.getRawParameterValue(lowBoostIds[index])->load();
        const auto midBoostDb = parameters.getRawParameterValue(midBoostIds[index])->load();
        const auto highBoostDb = parameters.getRawParameterValue(highBoostIds[index])->load();

        runtime.lowGain = juce::Decibels::decibelsToGain(lowBoostDb);
        runtime.midGain = juce::Decibels::decibelsToGain(midBoostDb);
        runtime.highGain = juce::Decibels::decibelsToGain(highBoostDb);

                // Keep mode processing untouched, but level-match In vs Side Chain better
                // when EQ boosts exceed roughly +5 dB.
        const auto highBoostExcessDb = juce::jmax(0.0f, highBoostDb - 5.0f);
        const auto midBoostExcessDb = juce::jmax(0.0f, midBoostDb - 5.0f);
        const auto lowBoostExcessDb = juce::jmax(0.0f, lowBoostDb - 5.0f);
                const auto baseModeMismatchDriveDb = juce::jlimit(
            0.0f,
                        13.5f,
                        highBoostExcessDb * 0.90f
                    + midBoostExcessDb * 0.60f
                    + lowBoostExcessDb * 0.35f);

                // Add extra correction only at extreme boosts so +20 dB stays matched,
                // while +10 dB and +15 dB behavior stays effectively unchanged.
                const auto highBoostExtremeDb = juce::jmax(0.0f, highBoostDb - 15.0f);
                const auto midBoostExtremeDb = juce::jmax(0.0f, midBoostDb - 15.0f);
                const auto lowBoostExtremeDb = juce::jmax(0.0f, lowBoostDb - 15.0f);
                const auto extremeModeMismatchDriveDb = juce::jlimit(
                        0.0f,
                        5.0f,
                        highBoostExtremeDb * 0.45f
                    + midBoostExtremeDb * 0.25f
                    + lowBoostExtremeDb * 0.15f);

                const auto modeMismatchDriveDb = juce::jlimit(
                        0.0f,
                        18.5f,
                        baseModeMismatchDriveDb + extremeModeMismatchDriveDb);

                // Split the correction evenly so mode switching stays centered.
                runtime.inModeLevelMatchGain = juce::Decibels::decibelsToGain(-0.50f * modeMismatchDriveDb);
                runtime.sidechainModeLevelMatchGain = juce::Decibels::decibelsToGain(0.50f * modeMismatchDriveDb);

        const auto lowMidXoverHz = juce::jlimit(
            75.0f,
            1000.0f,
            parameters.getRawParameterValue(lowMidXoverIds[index])->load());

        const auto midHighXoverHz = juce::jmax(
            lowMidXoverHz + 100.0f,
            juce::jlimit(500.0f,
                         8000.0f,
                         parameters.getRawParameterValue(midHighXoverIds[index])->load()));

        runtime.lowAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * lowMidXoverHz / sampleRateFloat);
        runtime.highAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * midHighXoverHz / sampleRateFloat);

        const auto hpSelection = juce::jlimit(
            0,
            1,
            juce::roundToInt(parameters.getRawParameterValue(hpFilterIds[index])->load()));
        const auto lpSelection = juce::jlimit(
            0,
            1,
            juce::roundToInt(parameters.getRawParameterValue(lpFilterIds[index])->load()));

        const auto hpFreq = hpSelection == 0 ? 100.0f : 200.0f;
        const auto lpFreq = lpSelection == 0 ? 4000.0f : 8000.0f;

        runtime.hpAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hpFreq / sampleRateFloat);
        runtime.lpAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * lpFreq / sampleRateFloat);

        runtime.eqMode = juce::jlimit(
            0,
            2,
            juce::roundToInt(parameters.getRawParameterValue(eqModeIds[index])->load()));

        runtime.analogMode = parameters.getRawParameterValue(analogModeId)->load() > 0.5f;
        runtime.analogInOutStage = parameters.getRawParameterValue(analogInOutStageId)->load() > 0.5f;

        runtime.outputGain = juce::Decibels::decibelsToGain(
            parameters.getRawParameterValue(outputGainIds[index])->load());

        return runtime;
    };

    const auto runtimeA = buildRuntime(0);
    const auto runtimeB = buildRuntime(1);

    std::array<float, 2> inputPeak { 0.0f, 0.0f };
    std::array<float, 2> outputPeak { 0.0f, 0.0f };
    std::array<float, 2> sourceNrActivityPeak { 0.0f, 0.0f };

    // Control envelope smoothing avoids fast gain modulation artifacts in sidechain extraction.
    const auto controlAttackCoeff = std::exp(-1.0f / (0.004f * sampleRateFloat));
    const auto controlReleaseCoeff = std::exp(-1.0f / (0.120f * sampleRateFloat));
    const auto sidechainEntryCoeff = 1.0f - std::exp(-1.0f / (0.018f * sampleRateFloat));
    const auto analogPhaseIncA = juce::MathConstants<float>::twoPi * 4.5f / sampleRateFloat;
    const auto analogPhaseIncB = juce::MathConstants<float>::twoPi * 10.5f / sampleRateFloat;
    const auto sowterInMagCoeff = 1.0f - std::exp(-1.0f / (0.050f * sampleRateFloat));
    const auto sowterOutMagCoeff = 1.0f - std::exp(-1.0f / (0.070f * sampleRateFloat));
    const auto sowterInBodyAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 180.0f / sampleRateFloat);
    const auto sowterInAirAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 8200.0f / sampleRateFloat);
    const auto sowterOutBodyAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 230.0f / sampleRateFloat);
    const auto sowterOutAirAlpha = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 12000.0f / sampleRateFloat);

    auto nextRandomSigned = [&](size_t index) -> float
    {
        auto& state = analogNoiseSeed[index];
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const auto unit = static_cast<float>(state & 0x00ffffffu)
                        / static_cast<float>(0x01000000u);
        return unit * 2.0f - 1.0f;
    };

    auto applySowterInputStage = [&](float sample, size_t stripIndex) -> float
    {
        auto& mag = transformerInMagState[stripIndex];
        auto& body = transformerInBodyState[stripIndex];
        auto& air = transformerInAirState[stripIndex];

        // Track magnitude only for subtle state continuity; drive stays static for EQ neutrality.
        mag += sowterInMagCoeff * (std::abs(sample) - mag);
        juce::ignoreUnused(mag);

        const auto drive = 1.055f;
        const auto asym = 0.0012f;
        const auto saturated = (std::tanh(sample * drive + asym) - std::tanh(asym))
                             / juce::jmax(0.60f, std::tanh(drive));
        const auto harmonic = saturated - sample;

        body += sowterInBodyAlpha * (harmonic - body);
        air += sowterInAirAlpha * (harmonic - air);

        const auto harmonicColor = 0.80f * harmonic + 0.16f * body - 0.05f * air;
        return sample + 0.16f * harmonicColor;
    };

    auto applySowterOutputStage = [&](float sample, size_t stripIndex, float strength) -> float
    {
        auto& mag = transformerOutMagState[stripIndex];
        auto& body = transformerOutBodyState[stripIndex];
        auto& air = transformerOutAirState[stripIndex];

        // Track magnitude only for subtle state continuity; drive stays static for EQ neutrality.
        mag += sowterOutMagCoeff * (std::abs(sample) - mag);
        juce::ignoreUnused(mag);

        const auto drive = 1.06f;
        const auto asym = 0.0010f;
        const auto saturated = (std::tanh(sample * drive + asym) - std::tanh(asym))
                             / juce::jmax(0.60f, std::tanh(drive));
        const auto harmonic = saturated - sample;

        body += sowterOutBodyAlpha * (harmonic - body);
        air += sowterOutAirAlpha * (harmonic - air);

        const auto harmonicColor = 0.78f * harmonic + 0.14f * body - 0.04f * air;
        const auto blend = 0.10f * juce::jlimit(0.0f, 1.0f, strength);
        return sample + blend * harmonicColor;
    };

    auto processStripSample = [&](float dry,
                                  int strip,
                                  const ChannelRuntime& runtime,
                                  float& stripInputPeak,
                                  float& stripOutputPeak,
                                  float& stripActivityPeak) -> float
    {
        const auto stripIndex = static_cast<size_t>(strip);
        auto& rippleDelayBuffer = analogRippleDelayBuffer[stripIndex];
        auto& rippleWriteIndex = analogRippleDelayIndex[stripIndex];

        const auto highBoostDb = juce::jmax(0.0f,
            juce::Decibels::gainToDecibels(runtime.highGain));
        // As High EQ boost rises, ease off output transformer color to preserve low/mid balance.
        const auto outputTransformerStrength = juce::jlimit(
            0.35f,
            1.0f,
            1.0f - juce::jmax(0.0f, highBoostDb - 5.0f) * 0.055f);

        auto& stripPreviousEqMode = previousEqMode[stripIndex];
        auto& stripSidechainModeEntryGain = sidechainModeEntryGain[stripIndex];

        const auto enteringSidechainMode = runtime.eqMode == 1 && stripPreviousEqMode != 1;
        if (enteringSidechainMode)
            stripSidechainModeEntryGain = 0.0f;

        stripPreviousEqMode = runtime.eqMode;

        const auto inSidechainReturnMode = runtime.eqMode == 1;
        if (inSidechainReturnMode)
            stripSidechainModeEntryGain += (1.0f - stripSidechainModeEntryGain) * sidechainEntryCoeff;
        else
            stripSidechainModeEntryGain = 1.0f;

        auto analogThresholdOffsetDb = 0.0f;
        auto analogOutputOffsetDb = 0.0f;
        auto analogDetectorGain = 1.0f;
        auto analogSidechainLevelDb = 0.0f;
        auto analogEqLowOffsetDb = 0.0f;
        auto analogEqMidOffsetDb = 0.0f;
        auto analogEqHighOffsetDb = 0.0f;
        auto analogEqLowAlphaDrift = 0.0f;
        auto analogEqHighAlphaDrift = 0.0f;
        auto analogCurrentRippleDepthGain = juce::Decibels::decibelsToGain(1.0f) - 1.0f;
        auto analogNoiseAValue = 0.0f;
        auto analogNoiseBValue = 0.0f;
        auto analogNoiseCValue = 0.0f;
        auto analogPhaseAValue = 0.0f;
        auto analogPhaseBValue = 0.0f;
        if (runtime.analogMode)
        {
            auto& phaseA = analogDriftPhaseA[stripIndex];
            auto& phaseB = analogDriftPhaseB[stripIndex];
            auto& noiseA = analogNoiseA[stripIndex];
            auto& noiseB = analogNoiseB[stripIndex];
            auto& noiseC = analogNoiseC[stripIndex];

            noiseA += 0.0060f * (nextRandomSigned(stripIndex) - noiseA);
            noiseB += 0.0040f * (nextRandomSigned(stripIndex) - noiseB);
            noiseC += 0.0075f * (nextRandomSigned(stripIndex) - noiseC);

            phaseA += analogPhaseIncA * (strip == 0 ? 1.0f : 1.07f);
            phaseB += analogPhaseIncB * (strip == 0 ? 0.93f : 1.03f);

            if (phaseA >= juce::MathConstants<float>::twoPi)
                phaseA -= juce::MathConstants<float>::twoPi;
            if (phaseB >= juce::MathConstants<float>::twoPi)
                phaseB -= juce::MathConstants<float>::twoPi;

            analogNoiseAValue = noiseA;
            analogNoiseBValue = noiseB;
            analogNoiseCValue = noiseC;
            analogPhaseAValue = phaseA;
            analogPhaseBValue = phaseB;

            const auto wobble = 0.6f * std::sin(phaseA) + 0.4f * std::sin(phaseB);
            const auto mixedWobble = 0.55f * wobble + 0.30f * noiseA + 0.15f * noiseC;

            analogThresholdOffsetDb = (strip == 0 ? -0.10f : 0.10f)
                                    + 0.12f * mixedWobble
                                    + 0.06f * noiseB;
            analogOutputOffsetDb = (strip == 0 ? -0.07f : 0.07f)
                                 + 0.05f * noiseC;
            analogSidechainLevelDb = (strip == 0 ? -0.10f : 0.10f)
                                   + 0.18f * noiseB;
            analogDetectorGain = juce::Decibels::decibelsToGain(
                (strip == 0 ? -0.05f : 0.05f) + 0.15f * mixedWobble);

            analogEqLowOffsetDb = 0.07f * mixedWobble + 0.03f * noiseC;
            analogEqMidOffsetDb = -0.08f * noiseB + 0.03f * mixedWobble;
            analogEqHighOffsetDb = 0.07f * noiseC - 0.03f * noiseA;

            analogEqLowAlphaDrift = 0.012f * noiseA;
            analogEqHighAlphaDrift = 0.015f * noiseC;

            const auto analogCurrentRippleDepthDb = juce::jlimit(0.90f,
                                                                  1.10f,
                                                                  1.0f + 0.08f * noiseC);
            analogCurrentRippleDepthGain = juce::Decibels::decibelsToGain(analogCurrentRippleDepthDb) - 1.0f;
        }

        auto stripInputSample = dry;
        if (runtime.analogInOutStage)
            stripInputSample = applySowterInputStage(stripInputSample, stripIndex);

        stripInputPeak = juce::jmax(stripInputPeak, std::abs(stripInputSample));

        if (runtime.eqMode == 2)
        {
            // Hard bypass for Dolby NR/EQ path; optional analog IO stages can still be auditioned.
            hpFilterState[stripIndex] = 0.0f;
            lpFilterState[stripIndex] = 0.0f;
            nrLowBandState[stripIndex] = 0.0f;
            nrLowBandState2[stripIndex] = 0.0f;
            nrHighBandLpState[stripIndex] = 0.0f;
            nrHighBandLpState2[stripIndex] = 0.0f;
            lowBandState[stripIndex] = 0.0f;
            lowBandState2[stripIndex] = 0.0f;
            highBandLpState[stripIndex] = 0.0f;
            highBandLpState2[stripIndex] = 0.0f;
            detectorStates[stripIndex].rmsSq = 1.0e-9f;
            detectorStates[stripIndex].peak = 0.0f;
            rippleDelayBuffer[static_cast<size_t>(rippleWriteIndex)] = stripInputSample;
            rippleWriteIndex = (rippleWriteIndex + 1) % kAnalogRippleDelaySize;

            auto bypassCore = stripInputSample;
            if (runtime.analogInOutStage)
                bypassCore = applySowterOutputStage(bypassCore, stripIndex, outputTransformerStrength);

            const auto output = bypassCore * runtime.outputGain;
            stripOutputPeak = juce::jmax(stripOutputPeak, std::abs(output));
            stripActivityPeak = juce::jmax(stripActivityPeak, 0.0f);
            return output;
        }

        // Side-chain detector path with dedicated HP/LP shaping.
        auto sidechainDetector = stripInputSample * analogDetectorGain;

        hpFilterState[stripIndex] += runtime.hpAlpha * (sidechainDetector - hpFilterState[stripIndex]);
        sidechainDetector -= hpFilterState[stripIndex];

        lpFilterState[stripIndex] += runtime.lpAlpha * (sidechainDetector - lpFilterState[stripIndex]);
        sidechainDetector = lpFilterState[stripIndex];

        const auto detectorAbs = std::abs(sidechainDetector);
        auto& detector = detectorStates[stripIndex];

        const auto controlCoeff = detectorAbs > detector.peak ? controlAttackCoeff : controlReleaseCoeff;
        detector.peak = controlCoeff * detector.peak + (1.0f - controlCoeff) * detectorAbs;
        const auto controlAbs = detector.peak;

        // Threshold knob selects side-chain cutoff in the -40 to -60 dBFS range.
        // dB-domain soft knee keeps extraction smooth and active through the full range.
        const auto sidechainCutoffDb = juce::jlimit(-60.0f,
                                -40.0f,
                                runtime.thresholdDbFs + analogThresholdOffsetDb);
        const auto sidechainControlDb = juce::Decibels::gainToDecibels(
            juce::jmax(1.0e-7f, controlAbs),
            -140.0f);
        constexpr float extractionKneeDb = 10.0f;
        const auto detailZoneRaw = juce::jlimit(0.0f,
                            1.0f,
                            (sidechainCutoffDb - sidechainControlDb + extractionKneeDb)
                                / (2.0f * extractionKneeDb));
        const auto detailZone = detailZoneRaw * detailZoneRaw * (3.0f - 2.0f * detailZoneRaw);

        // Main path NR stage: compand then expand, with strongest HF weighting.
        const auto nrDrive = juce::jlimit(0.0f, 1.0f, runtime.sourceNrDb / 20.0f);
        const auto compandDepthDb = detailZone * runtime.sourceNrDb;

        nrLowBandState[stripIndex] += runtime.lowAlpha * (stripInputSample - nrLowBandState[stripIndex]);
        nrLowBandState2[stripIndex] += runtime.lowAlpha * (nrLowBandState[stripIndex] - nrLowBandState2[stripIndex]);

        const auto nrLowBand = nrLowBandState2[stripIndex];
        const auto nrUpper = stripInputSample - nrLowBand;

        nrHighBandLpState[stripIndex] += runtime.highAlpha * (nrUpper - nrHighBandLpState[stripIndex]);
        nrHighBandLpState2[stripIndex] += runtime.highAlpha * (nrHighBandLpState[stripIndex] - nrHighBandLpState2[stripIndex]);

        const auto nrMidBand = nrHighBandLpState2[stripIndex];
        const auto nrHighBand = nrUpper - nrMidBand;

        const auto lowCompandDb = compandDepthDb * (0.08f + 0.07f * nrDrive);
        const auto midCompandDb = compandDepthDb * (0.40f + 0.20f * nrDrive);
        const auto highCompandDb = compandDepthDb * (0.95f + 0.25f * nrDrive);

        const auto compandedLow = nrLowBand * juce::Decibels::decibelsToGain(lowCompandDb);
        const auto compandedMid = nrMidBand * juce::Decibels::decibelsToGain(midCompandDb);
        const auto compandedHigh = nrHighBand * juce::Decibels::decibelsToGain(highCompandDb);

        // Slightly stronger decode at higher NR settings gives a practical noise-reduction bias.
        const auto expandScale = 0.90f + 0.20f * nrDrive;

        const auto mainNrSignal = compandedLow * juce::Decibels::decibelsToGain(-lowCompandDb * expandScale)
                + compandedMid * juce::Decibels::decibelsToGain(-midCompandDb * expandScale)
                + compandedHigh * juce::Decibels::decibelsToGain(-highCompandDb * expandScale);

        // Internal sidechain band signal for Side Chain mode.
        const auto filteredSidechainBand = sidechainDetector;
        // In mode sidechain contribution is sourced from the same HP/LP-filtered detector band.
        const auto sidechainExtracted = filteredSidechainBand * detailZone;

        // Threshold-gated sidechain extraction for Side Chain mode processing.
        // At very low threshold settings, add a sample-level tail capture so content
        // below -60 dBFS can still be extracted and returned.
        const auto sidechainSampleDb = juce::Decibels::gainToDecibels(
            juce::jmax(1.0e-7f, std::abs(filteredSidechainBand)),
            -140.0f);
        const auto detailZoneSampleRaw = juce::jlimit(0.0f,
                                1.0f,
                                (sidechainCutoffDb - sidechainSampleDb + extractionKneeDb)
                                    / (2.0f * extractionKneeDb));
        const auto detailZoneSample = detailZoneSampleRaw * detailZoneSampleRaw
                                    * (3.0f - 2.0f * detailZoneSampleRaw);
        const auto lowThresholdTailBlend = juce::jlimit(0.0f,
                                                        1.0f,
                                                        (-56.0f - sidechainCutoffDb) / 4.0f);
        const auto lowThresholdReturnBlendRaw = juce::jlimit(0.0f,
                                     1.0f,
                                                             (-50.0f - sidechainCutoffDb) / 10.0f);
        const auto lowThresholdReturnBlend = lowThresholdReturnBlendRaw * lowThresholdReturnBlendRaw
                           * (3.0f - 2.0f * lowThresholdReturnBlendRaw);
        const auto deepLowThresholdBlendRaw = juce::jlimit(0.0f,
                                   1.0f,
                                   (-55.0f - sidechainCutoffDb) / 5.0f);
        const auto deepLowThresholdBlend = deepLowThresholdBlendRaw * deepLowThresholdBlendRaw
                         * (3.0f - 2.0f * deepLowThresholdBlendRaw);
        const auto sidechainReturnBlend = lowThresholdReturnBlend;
        const auto sidechainDetailZone = juce::jlimit(0.0f,
                                                      1.0f,
                                                      juce::jmax(detailZone,
                                                                 detailZoneSample * lowThresholdTailBlend));
        const auto filteredSidechainExtracted = filteredSidechainBand * sidechainDetailZone;
        const auto sidechainAssistNorm = juce::jlimit(0.0f,
                                  1.0f,
                                  (runtime.thresholdDbFs + 60.0f) / 20.0f);
        // Keep a small threshold-aware sidechain floor so Side Chain mode remains audible
        // on dense program material when extracted-only energy gets too low.
        const auto sidechainAssistTaper = (1.0f - 0.45f * lowThresholdReturnBlend)
                        * (1.0f - deepLowThresholdBlend);
        const auto sidechainAssist = 0.10f * sidechainAssistNorm * sidechainAssistTaper;
        const auto filteredSidechainEqProgramInput = filteredSidechainExtracted
                               + filteredSidechainBand * sidechainAssist;
        const auto sidechainAnalogLevelGain = runtime.analogMode
                              ? juce::Decibels::decibelsToGain(analogSidechainLevelDb)
                              : 1.0f;
        // Side Chain mode makeup keeps extracted content audible in real DAW program material.
        const auto thresholdToLow = juce::jlimit(0.0f, 1.0f, (-40.0f - sidechainCutoffDb) / 20.0f);
        const auto sidechainEqTailLiftTaper = 1.0f - 0.60f * deepLowThresholdBlend;
        const auto sidechainEqTailLiftDb = 4.0f * lowThresholdTailBlend * sidechainEqTailLiftTaper;
        const auto sidechainEqLowLevelTrimDb = 3.0f * lowThresholdReturnBlend
                             + 2.0f * deepLowThresholdBlend;
        const auto sidechainEqMakeupDb = 6.0f + 8.0f * thresholdToLow + sidechainEqTailLiftDb
                           - sidechainEqLowLevelTrimDb;
        const auto filteredSidechainEqInput = filteredSidechainEqProgramInput
                            * sidechainAnalogLevelGain
                            * juce::Decibels::decibelsToGain(sidechainEqMakeupDb);

        float eqInput = 0.0f;
        float dryBlend = 0.0f;
        // PROC activity follows incoming sidechain level with threshold-position weighting.
        const auto sidechainEnvelopeDb = juce::Decibels::gainToDecibels(
            juce::jmax(1.0e-7f, controlAbs),
            -100.0f);
        const auto sidechainEnvelopeActivity = juce::jlimit(0.0f,
                                                            1.0f,
                                                            (sidechainEnvelopeDb + 75.0f) / 55.0f);
        const auto thresholdNorm = juce::jlimit(0.0f,
                            1.0f,
                            (sidechainCutoffDb + 60.0f) / 20.0f);
        // In mode can over-add extracted detail at higher threshold settings; trim it
        // more strongly near -40 dBFS and ease toward -60 dBFS.
        const auto inModeSidechainTrimDb = -10.0f + 4.0f * (1.0f - thresholdNorm);
        const auto inModeSidechainGain = juce::Decibels::decibelsToGain(inModeSidechainTrimDb);
        const auto thresholdWeight = 0.35f + 0.65f * thresholdNorm;
        const auto thresholdScaledActivity = sidechainEnvelopeActivity * thresholdWeight;
        const float activity = juce::jlimit(0.0f,
                            1.0f,
                            thresholdScaledActivity);

        if (runtime.eqMode == 1)
        {
            // Side Chain: EQ only threshold-extracted sidechain content, then blend with NR-processed main path.
            eqInput = filteredSidechainEqInput;
            dryBlend = mainNrSignal;
        }
        else
        {
            // In: merge NR-processed main and sidechain, then EQ combined signal.
            eqInput = mainNrSignal + sidechainExtracted * inModeSidechainGain * sidechainAnalogLevelGain;
            dryBlend = 0.0f;
        }

        auto eqLowAlpha = runtime.lowAlpha;
        auto eqHighAlpha = runtime.highAlpha;
        if (runtime.analogMode)
        {
            eqLowAlpha = juce::jlimit(0.0001f,
                                      0.9999f,
                                      runtime.lowAlpha * (1.0f + analogEqLowAlphaDrift));
            eqHighAlpha = juce::jlimit(0.0001f,
                                       0.9999f,
                                       runtime.highAlpha * (1.0f + analogEqHighAlphaDrift));
        }

        // Two-stage crossover split gives cleaner band boundaries for mid/high boosts.
        lowBandState[stripIndex] += eqLowAlpha * (eqInput - lowBandState[stripIndex]);
        lowBandState2[stripIndex] += eqLowAlpha * (lowBandState[stripIndex] - lowBandState2[stripIndex]);

        const auto lowBand = lowBandState2[stripIndex];
        const auto upperAfterLow = eqInput - lowBand;

         highBandLpState[stripIndex] += eqHighAlpha * (upperAfterLow - highBandLpState[stripIndex]);
         highBandLpState2[stripIndex] += eqHighAlpha * (highBandLpState[stripIndex] - highBandLpState2[stripIndex]);

        const auto midBand = highBandLpState2[stripIndex];
        const auto highBand = upperAfterLow - midBand;

         const auto analogLowGain = runtime.analogMode
                            ? runtime.lowGain * juce::Decibels::decibelsToGain(analogEqLowOffsetDb)
                            : runtime.lowGain;
         const auto analogMidGain = runtime.analogMode
                            ? runtime.midGain * juce::Decibels::decibelsToGain(analogEqMidOffsetDb)
                            : runtime.midGain;
         const auto analogHighGain = runtime.analogMode
                             ? runtime.highGain * juce::Decibels::decibelsToGain(analogEqHighOffsetDb)
                             : runtime.highGain;

         const auto eqSignal = lowBand * analogLowGain
                  + midBand * analogMidGain
                  + highBand * analogHighGain;

         const auto eqDelta = lowBand * (analogLowGain - 1.0f)
             + midBand * (analogMidGain - 1.0f)
             + highBand * (analogHighGain - 1.0f);

         const auto mainPathEqDelta = nrLowBand * (analogLowGain - 1.0f)
                        + nrMidBand * (analogMidGain - 1.0f)
                        + nrHighBand * (analogHighGain - 1.0f);

         // Preserve prior Side Chain behavior at higher thresholds, but start
         // reducing full parallel return around -55 dBFS to curb comb filtering.
         const auto sidechainReturn = eqSignal + (eqDelta - eqSignal) * sidechainReturnBlend;
         const auto deepThresholdSidechainReturnLevel = 1.0f - 0.94f * deepLowThresholdBlend;
         const auto deepThresholdMainEqBlend = 0.20f * deepLowThresholdBlend;
         const auto deepThresholdReturnTrim = 1.0f - 0.15f * deepLowThresholdBlend;
         const auto deepThresholdSafeReturn = (sidechainReturn * deepThresholdSidechainReturnLevel
                                            + mainPathEqDelta * deepThresholdMainEqBlend)
                                            * deepThresholdReturnTrim;
         // Keep the low-threshold feel, but trim high-threshold Side Chain return more
         // so -40 dBFS settings do not over-boost when blended back into the main path.
         const auto sidechainPreBlendTrimDb = -6.0f - 4.0f * thresholdNorm;
         const auto sidechainPreBlendTrim = juce::Decibels::decibelsToGain(sidechainPreBlendTrimDb);
         const auto sidechainPreBlendReturn = deepThresholdSafeReturn * sidechainPreBlendTrim;
         const auto sidechainModeReturn = mainNrSignal + sidechainPreBlendReturn * stripSidechainModeEntryGain;

         const auto dryEqReturn = dryBlend + eqSignal;
         const auto inModeReturn = dryEqReturn * runtime.inModeLevelMatchGain;
         const auto outModeReturn = dryEqReturn;
        const auto sidechainMatchedReturn = sidechainModeReturn * runtime.sidechainModeLevelMatchGain;
         const auto outCore = (runtime.eqMode == 1)
             ? sidechainMatchedReturn
             : ((runtime.eqMode == 0) ? inModeReturn : outModeReturn);

        rippleDelayBuffer[static_cast<size_t>(rippleWriteIndex)] = outCore;

        auto analogShapedCore = outCore;
        if (runtime.analogMode)
        {
            // Dense current-style ripple: random multi-delay branch with zero-sum tap weights
            // to create moving boost/cut shapes across the full spectrum, not broad EQ tilt.
            auto delayFromMod = [](float modValue, int minDelay, int maxDelay)
            {
                const auto norm = juce::jlimit(0.0f, 1.0f, 0.5f * (modValue + 1.0f));
                return minDelay + static_cast<int>(std::round(norm * static_cast<float>(maxDelay - minDelay)));
            };

            auto delayedAt = [&](int delaySamples)
            {
                auto readIndex = rippleWriteIndex - delaySamples;
                while (readIndex < 0)
                    readIndex += kAnalogRippleDelaySize;
                return rippleDelayBuffer[static_cast<size_t>(readIndex)];
            };

                // Multi-scale delay zones spread the same random ripple feel from lows to highs,
                // so the 1-5 kHz character is carried across the full spectrum.
                const auto d1 = delayFromMod(std::sin(analogPhaseAValue * 5.8f + 2.0f * analogNoiseAValue), 1, 2);
                const auto d2 = delayFromMod(std::sin(analogPhaseBValue * 5.1f + 1.7f * analogNoiseBValue + 0.6f), 3, 5);
                const auto d3 = delayFromMod(std::sin(analogPhaseAValue * 6.6f + analogPhaseBValue * 1.4f + 0.8f * analogNoiseCValue), 6, 9);
                const auto d4 = delayFromMod(std::sin(analogPhaseBValue * 7.4f + 1.5f * analogNoiseAValue), 10, 16);
                const auto d5 = delayFromMod(std::sin(analogPhaseAValue * 8.1f + 1.8f * analogNoiseBValue), 17, 28);
                const auto d6 = delayFromMod(std::sin(analogPhaseBValue * 8.9f + 2.1f * analogNoiseCValue), 29, 46);
                const auto d7 = delayFromMod(std::sin(analogPhaseAValue * 3.5f + analogPhaseBValue * 0.9f + 1.1f * analogNoiseAValue), 47, 72);
                const auto d8 = delayFromMod(std::sin(analogPhaseBValue * 3.0f + analogPhaseAValue * 1.0f + 0.9f * analogNoiseCValue), 73, 120);
                const auto d9 = delayFromMod(std::sin(analogPhaseAValue * 2.1f + analogPhaseBValue * 0.8f + 0.7f * analogNoiseBValue), 121, 220);
                const auto d10 = delayFromMod(std::sin(analogPhaseBValue * 1.6f + analogPhaseAValue * 0.6f + 0.8f * analogNoiseAValue), 221, 420);

                const auto w1 = 0.30f + 0.04f * analogNoiseAValue;
                const auto w2 = -0.28f + 0.05f * analogNoiseBValue;
                const auto w3 = 0.24f - 0.04f * analogNoiseCValue;
                const auto w4 = -0.22f + 0.05f * analogNoiseAValue;
                const auto w5 = 0.20f + 0.04f * analogNoiseBValue;
                const auto w6 = -0.18f + 0.04f * analogNoiseCValue;
                const auto w7 = 0.16f - 0.03f * analogNoiseAValue;
                const auto w8 = -0.14f + 0.03f * analogNoiseBValue;
                const auto w9 = 0.12f - 0.03f * analogNoiseCValue;
                const auto w10 = juce::jlimit(-0.65f,
                                                        0.65f,
                                                        -(w1 + w2 + w3 + w4 + w5 + w6 + w7 + w8 + w9));

                const auto highRipple = delayedAt(d1) * w1
                                             + delayedAt(d2) * w2
                                             + delayedAt(d3) * w3;
                const auto midRipple = delayedAt(d4) * w4
                                            + delayedAt(d5) * w5
                                            + delayedAt(d6) * w6
                                            + delayedAt(d7) * w7;
                const auto lowRipple = delayedAt(d8) * w8
                                            + delayedAt(d9) * w9
                                            + delayedAt(d10) * w10;

                const auto highLift = 1.22f + 0.12f * std::sin(analogPhaseAValue * 2.3f + 0.8f * analogNoiseAValue);
                const auto lowLift = 1.24f + 0.12f * std::sin(analogPhaseBValue * 1.9f + 0.9f * analogNoiseCValue);

                const auto rippleSum = highRipple * highLift + midRipple + lowRipple * lowLift;

                const auto rippleNorm = 1.0f
                                             / (std::abs(w1) * highLift
                                                 + std::abs(w2) * highLift
                                                 + std::abs(w3) * highLift
                                                 + std::abs(w4)
                                                 + std::abs(w5)
                                                 + std::abs(w6)
                                                 + std::abs(w7)
                                                 + std::abs(w8) * lowLift
                                                 + std::abs(w9) * lowLift
                                                 + std::abs(w10) * lowLift
                                                 + 1.0e-6f);
                const auto rippleComposite = rippleSum * rippleNorm;

            analogShapedCore = outCore + rippleComposite * analogCurrentRippleDepthGain;
        }

        rippleWriteIndex = (rippleWriteIndex + 1) % kAnalogRippleDelaySize;

        const auto analogOutputGain = runtime.analogMode
                          ? juce::Decibels::decibelsToGain(analogOutputOffsetDb)
                          : 1.0f;
        auto outputCore = analogShapedCore;
        if (runtime.analogInOutStage)
            outputCore = applySowterOutputStage(outputCore, stripIndex, outputTransformerStrength);

        const auto output = outputCore * runtime.outputGain * analogOutputGain;

        stripOutputPeak = juce::jmax(stripOutputPeak, std::abs(output));
        stripActivityPeak = juce::jmax(stripActivityPeak, activity);

        return output;
    };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto dryLeft = left[sample];
        left[sample] = processStripSample(dryLeft,
                                          0,
                                          runtimeA,
                                          inputPeak[0],
                                          outputPeak[0],
                                          sourceNrActivityPeak[0]);

        if (right != nullptr)
        {
            const auto dryRight = right[sample];
            right[sample] = processStripSample(dryRight,
                                               1,
                                               runtimeB,
                                               inputPeak[1],
                                               outputPeak[1],
                                               sourceNrActivityPeak[1]);
        }
    }

    const auto stripsProcessed = right != nullptr ? 2 : 1;
    for (int strip = 0; strip < stripsProcessed; ++strip)
    {
        const auto stripIndex = static_cast<size_t>(strip);

        const auto inputDbTarget = juce::Decibels::gainToDecibels(
            juce::jmax(1.0e-6f, inputPeak[strip]),
            -100.0f);

        const auto outputDbTarget = juce::Decibels::gainToDecibels(
            juce::jmax(1.0e-6f, outputPeak[strip]),
            -100.0f);

        const auto inputSmooth = inputDbTarget > inputMeterStateDb[stripIndex] ? 0.36f : 0.10f;
        const auto outputSmooth = outputDbTarget > outputMeterStateDb[stripIndex] ? 0.36f : 0.10f;

        inputMeterStateDb[stripIndex] += (inputDbTarget - inputMeterStateDb[stripIndex]) * inputSmooth;
        outputMeterStateDb[stripIndex] += (outputDbTarget - outputMeterStateDb[stripIndex]) * outputSmooth;

        inputMeterDb[stripIndex].store(inputMeterStateDb[stripIndex], std::memory_order_relaxed);
        outputMeterDb[stripIndex].store(outputMeterStateDb[stripIndex], std::memory_order_relaxed);

        const auto activityPctTarget = juce::jlimit(
            0.0f,
            100.0f,
            sourceNrActivityPeak[strip] * 100.0f);

        const auto activityCurrent = detailActivityPct[stripIndex].load(std::memory_order_relaxed);
        detailActivityPct[stripIndex].store(activityCurrent + (activityPctTarget - activityCurrent) * 0.22f,
                                            std::memory_order_relaxed);
    }

    if (right == nullptr)
    {
        inputMeterDb[1].store(inputMeterDb[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
        outputMeterDb[1].store(outputMeterDb[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
        detailActivityPct[1].store(detailActivityPct[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* Dolby740AudioProcessor::createEditor()
{
    return new Dolby740AudioProcessorEditor(*this);
}

void Dolby740AudioProcessor::setCurrentProgram(int)
{
}

const juce::String Dolby740AudioProcessor::getProgramName(int)
{
    return "Default";
}

void Dolby740AudioProcessor::changeProgramName(int, const juce::String&)
{
}

void Dolby740AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto state = parameters.copyState(); auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void Dolby740AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Dolby740AudioProcessor();
}
