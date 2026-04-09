#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto inputId = "input";
constexpr auto driveId = "drive";
constexpr auto outputId = "output";
constexpr auto trackId = "track";
constexpr auto ipsId = "ips";
constexpr auto calId = "cal";
constexpr auto bleedId = "bleed";
constexpr auto bleedThresholdId = "bleedThreshold";
constexpr auto wowFlutterOnId = "wowFlutterOn";
constexpr auto hissOnId = "hissOn";
constexpr auto biasId = "bias";
constexpr auto headBumpFreqId = "headBumpFreq";
constexpr auto headBumpAmtId = "headBumpAmt";
constexpr auto printThroughId = "printThrough";
constexpr auto azimuthId = "azimuth";

float dbToGain(float db)
{
    return juce::Decibels::decibelsToGain(db);
}
}

JuiceAudioProcessor::JuiceAudioProcessor()
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
    juce::Random seeded(0x4d4d3132);
    for (auto& value : trackVarianceTable)
    {
        value = juce::jlimit(0.9f, 1.1f, 1.0f + seeded.nextFloat() * 0.2f - 0.1f);
    }
}

JuiceAudioProcessor::APVTS::ParameterLayout JuiceAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(inputId, 1),
        "Input",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        "dB"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(driveId, 1),
        "Drive",
        juce::NormalisableRange<float>(0.2f, 2.5f, 0.01f),
        1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(outputId, 1),
        "Output",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        "dB"));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(trackId, 1),
        "Tape Track",
        1,
        24,
        1));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ipsId, 1),
        "IPS",
        juce::StringArray { "15 ips", "30 ips" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(calId, 1),
        "Cal Mode",
        juce::StringArray { "+3", "+6", "+9" },
        1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(bleedId, 1),
        "Bleed",
        juce::NormalisableRange<float>(0.0f, 0.35f, 0.001f),
        0.08f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(bleedThresholdId, 1),
        "Bleed Threshold",
        juce::NormalisableRange<float>(-24.0f, 6.0f, 0.1f),
        -6.0f,
        "dBFS"));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(wowFlutterOnId, 1),
        "Wow Flutter",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(hissOnId, 1),
        "Tape Hiss",
        false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(biasId, 1),
        "Bias",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(headBumpFreqId, 1),
        "Head Bump Freq",
        juce::NormalisableRange<float>(30.0f, 150.0f, 1.0f),
        80.0f,
        "Hz"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(headBumpAmtId, 1),
        "Head Bump Amt",
        juce::NormalisableRange<float>(0.0f, 2.5f, 0.01f),
        1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(printThroughId, 1),
        "Print Through",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(azimuthId, 1),
        "Azimuth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f));

    return { params.begin(), params.end() };
}

void JuiceAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    channelStates.assign(static_cast<size_t>(juce::jmax(1, getTotalNumOutputChannels())), {});

    wowDelayBufferSize = juce::jmax(64, static_cast<int>(currentSampleRate * 0.05));
    wowDelayBuffers.assign(static_cast<size_t>(juce::jmax(1, getTotalNumOutputChannels())), std::vector<float>(static_cast<size_t>(wowDelayBufferSize), 0.0f));

    printBufferSize = juce::jmax(64, static_cast<int>(currentSampleRate * 1.1));
    printBuffers.assign(static_cast<size_t>(juce::jmax(1, getTotalNumOutputChannels())), std::vector<float>(static_cast<size_t>(printBufferSize), 0.0f));

    juce::Random seeded(0x5757464c);
    for (auto& state : channelStates)
    {
        state.wowPhase = seeded.nextFloat() * juce::MathConstants<float>::twoPi;
        state.flutterPhase = seeded.nextFloat() * juce::MathConstants<float>::twoPi;
        state.noiseSeed ^= static_cast<uint32_t>(seeded.nextInt());
        state.wowWritePosition = 0;
        state.printWritePos = 0;
        state.azApX = 0.0f;
        state.azApY = 0.0f;
        state.azToneLp = 0.0f;
    }
}

void JuiceAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool JuiceAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    {
        return false;
    }

#if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    {
        return false;
    }
#endif

    return true;
#endif
}
#endif

void JuiceAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const auto totalOutChannels = getTotalNumOutputChannels();
    const auto totalInChannels = getTotalNumInputChannels();
    const auto numSamples = buffer.getNumSamples();

    if (static_cast<int>(channelStates.size()) < totalOutChannels)
    {
        channelStates.resize(static_cast<size_t>(totalOutChannels));
    }

    if (static_cast<int>(printBuffers.size()) < totalOutChannels)
    {
        printBuffers.resize(static_cast<size_t>(totalOutChannels),
            std::vector<float>(static_cast<size_t>(printBufferSize), 0.0f));
    }

    const auto inputDb = parameters.getRawParameterValue(inputId)->load();
    const auto drive = parameters.getRawParameterValue(driveId)->load();
    const auto outputDb = parameters.getRawParameterValue(outputId)->load();
    const auto selectedTrack = juce::jlimit(1, 24, static_cast<int>(std::lround(parameters.getRawParameterValue(trackId)->load())));
    const auto ipsChoice = static_cast<int>(std::lround(parameters.getRawParameterValue(ipsId)->load()));
    const auto calChoice = static_cast<int>(std::lround(parameters.getRawParameterValue(calId)->load()));
    const auto ips = ipsChoice == 0 ? 15.0f : 30.0f;
    const auto bleedAmount = parameters.getRawParameterValue(bleedId)->load();
    const auto wowFlutterOn = parameters.getRawParameterValue(wowFlutterOnId)->load() > 0.5f;
    const auto hissOn = parameters.getRawParameterValue(hissOnId)->load() > 0.5f;
    const auto biasParam = parameters.getRawParameterValue(biasId)->load();
    const auto headBumpFreqHz = parameters.getRawParameterValue(headBumpFreqId)->load();
    const auto headBumpAmt = parameters.getRawParameterValue(headBumpAmtId)->load();
    const auto printThroughParam = parameters.getRawParameterValue(printThroughId)->load();
    const auto azimuthParam = parameters.getRawParameterValue(azimuthId)->load();
    const auto bleedThresholdDb = parameters.getRawParameterValue(bleedThresholdId)->load();
    constexpr float bleedKneeWidthDb = 1.5f;
    const auto kneeLowDb = bleedThresholdDb - bleedKneeWidthDb;
    const auto kneeHighDb = bleedThresholdDb + bleedKneeWidthDb;
    const auto kneeRangeDb = juce::jmax(0.0001f, kneeHighDb - kneeLowDb);
    const auto inputGain = dbToGain(inputDb) * drive;
    const auto outputGain = dbToGain(outputDb);
    const auto bleedHighPassHz = ips <= 15.0f ? 140.0f : 210.0f;
    const auto bleedLowPassHz = ips <= 15.0f ? 4300.0f : 6500.0f;
    const auto bleedColourGain = ips <= 15.0f ? 0.55f : 0.45f;

    const auto bleedHpAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * bleedHighPassHz / static_cast<float>(currentSampleRate));
    const auto bleedLpAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * bleedLowPassHz / static_cast<float>(currentSampleRate));
    const auto bleedEnvAttackHz = 190.0f;
    const auto bleedEnvReleaseHz = 12.0f;
    const auto bleedEnvAttackAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * bleedEnvAttackHz / static_cast<float>(currentSampleRate));
    const auto bleedEnvReleaseAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * bleedEnvReleaseHz / static_cast<float>(currentSampleRate));

    const auto wowRateHz = ips <= 15.0f ? 0.22f : 0.34f;
    const auto flutterRateHz = ips <= 15.0f ? 4.2f : 5.4f;
    const auto wowDepthMs = ips <= 15.0f ? 0.028f : 0.018f;
    const auto flutterDepthMs = ips <= 15.0f ? 0.006f : 0.004f;
    const auto wowPhaseStep = juce::MathConstants<float>::twoPi * wowRateHz / static_cast<float>(currentSampleRate);
    const auto flutterPhaseStep = juce::MathConstants<float>::twoPi * flutterRateHz / static_cast<float>(currentSampleRate);
    const auto wowDepthSamples = wowDepthMs * static_cast<float>(currentSampleRate) * 0.001f;
    const auto flutterDepthSamples = flutterDepthMs * static_cast<float>(currentSampleRate) * 0.001f;
    const auto wowBaseDelaySamples = 6.0f;

    const auto hissLpHz = ips <= 15.0f ? 2800.0f : 3500.0f;
    const auto hissLpAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * hissLpHz / static_cast<float>(currentSampleRate));
    const auto hissGain = dbToGain(ips <= 15.0f ? -75.0f : -78.0f);

    // Cal mode shifts operating level character like console calibration.
    // +3: hottest, saturates earliest.  +6: neutral reference.  +9: most headroom.
    //
    // calSaturation   – drives the tanh harder/softer inside processTapeSample.
    // calInputTrim    – pre-scales signal into the tape stage so the saturation kneepoint
    //                   matches the intended operating level; keeps perceived loudness even.
    // calOutputTrim   – compensates the gain change introduced by calInputTrim so the
    //                   output level is consistent regardless of Cal mode.
    // calHfBoost      – slight HF lift at +9 / cut at +3 matching the real-world practice
    //                   of adjusting bias and EQ when recalibrating tape machines.
    float calSaturation, calInputTrim, calOutputTrim, calHfBoost;
    switch (calChoice)
    {
        case 0: // +3 – hot calibration, less headroom
            calSaturation  = 1.22f;
            calInputTrim   = dbToGain (3.0f);
            calOutputTrim  = dbToGain (-3.0f);
            calHfBoost     = 0.92f;   // slight HF roll-off, heavier compression feel
            break;
        case 2: // +9 – conservative calibration, most headroom
            calSaturation  = 0.82f;
            calInputTrim   = dbToGain (-3.0f);
            calOutputTrim  = dbToGain (3.0f);
            calHfBoost     = 1.08f;   // slight HF lift, airier, less saturation
            break;
        default: // +6 – neutral reference (default)
            calSaturation  = 1.0f;
            calInputTrim   = 1.0f;
            calOutputTrim  = 1.0f;
            calHfBoost     = 1.0f;
            break;
    }
    const auto effInputGain  = inputGain * calInputTrim;
    const auto effOutputGain = outputGain * calOutputTrim;

    // Print-through: ghost echo at tape layer separation distance
    const auto layerDelaySamples = ips <= 15.0f
        ? static_cast<int>(currentSampleRate * 0.9)
        : static_cast<int>(currentSampleRate * 0.45);
    const auto printPostLevel = printThroughParam * dbToGain(-32.0f);
    const auto printPreLevel  = -printPostLevel * 0.55f;  // opposite polarity, slightly quieter
    const auto safeLayerDelay = juce::jmin(layerDelaySamples, printBufferSize - 1);
    const auto safeLayer2Delay = juce::jmin(layerDelaySamples * 2, printBufferSize - 1);

    std::vector<std::vector<float>> preBleedChannels(static_cast<size_t>(totalOutChannels), std::vector<float>(static_cast<size_t>(numSamples), 0.0f));
    std::vector<std::vector<float>> bleedBandChannels(static_cast<size_t>(totalOutChannels), std::vector<float>(static_cast<size_t>(numSamples), 0.0f));
    std::vector<std::vector<float>> sourceDriveChannels(static_cast<size_t>(totalOutChannels), std::vector<float>(static_cast<size_t>(numSamples), 0.0f));

    for (auto channel = 0; channel < totalOutChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        auto& state = channelStates[static_cast<size_t>(channel)];
        const auto trackIndex = (selectedTrack - 1 + channel) % 24;
        const auto trackVariance = trackVarianceTable[static_cast<size_t>(trackIndex)];

        auto& staged = preBleedChannels[static_cast<size_t>(channel)];
        auto& bleedBand = bleedBandChannels[static_cast<size_t>(channel)];
        auto& sourceDrive = sourceDriveChannels[static_cast<size_t>(channel)];
        auto& wowBuffer = wowDelayBuffers[static_cast<size_t>(channel)];
        auto& pBuf = printBuffers[static_cast<size_t>(channel)];

        // Azimuth model:
        // 1) all-pass phase shift (head misalignment)
        // 2) slight per-track stereo skew (each track aligns a bit differently)
        // 3) HF blur/loss that increases with azimuth amount
        // Taper the knob so 0-50% stays subtle, while the top end becomes
        // progressively stronger for special-effect ranges.
        const auto azTaper = 0.2f * azimuthParam + 0.8f * std::pow(azimuthParam, 2.2f);
        const auto azBaseCoeff = 0.62f * azTaper;
        const auto azTrackSkew = (trackVariance - 1.0f) * 1.6f * azTaper;
        const auto azStereoSkew = ((trackIndex & 1) == 0 ? -1.0f : 1.0f) * 0.14f * azTaper;
        const auto azCoeff = juce::jlimit(-0.96f, 0.96f,
            azBaseCoeff + azTrackSkew + azStereoSkew);
        const auto azToneCutHz = juce::jmap(azTaper, 18000.0f, ips <= 15.0f ? 8200.0f : 9800.0f);
        const auto azToneAlpha = std::exp(-juce::MathConstants<float>::twoPi * azToneCutHz / static_cast<float>(currentSampleRate));
        const auto azToneBlend = juce::jmap(azTaper, 0.0f, ips <= 15.0f ? 0.30f : 0.22f);

        for (auto sample = 0; sample < numSamples; ++sample)
        {
            auto pushed = channelData[sample] * effInputGain;

            if (wowFlutterOn)
            {
                wowBuffer[static_cast<size_t>(state.wowWritePosition)] = pushed;

                const auto wowOffset = std::sin(state.wowPhase) * wowDepthSamples;
                const auto flutterOffset = std::sin(state.flutterPhase) * flutterDepthSamples;
                const auto delayInSamples = wowBaseDelaySamples + wowOffset + flutterOffset;
                const auto readPos = static_cast<float>(state.wowWritePosition) - delayInSamples + static_cast<float>(wowDelayBufferSize);
                auto readIndexA = static_cast<int>(std::floor(readPos)) % wowDelayBufferSize;
                const auto readIndexB = (readIndexA + 1) % wowDelayBufferSize;
                const auto frac = readPos - std::floor(readPos);
                const auto delayedA = wowBuffer[static_cast<size_t>(readIndexA)];
                const auto delayedB = wowBuffer[static_cast<size_t>(readIndexB)];
                pushed = juce::jmap(frac, delayedA, delayedB);

                state.wowPhase += wowPhaseStep;
                state.flutterPhase += flutterPhaseStep;
                if (state.wowPhase > juce::MathConstants<float>::twoPi)
                    state.wowPhase -= juce::MathConstants<float>::twoPi;
                if (state.flutterPhase > juce::MathConstants<float>::twoPi)
                    state.flutterPhase -= juce::MathConstants<float>::twoPi;
            }

            const auto pushedAbs = std::abs(pushed);
            const auto envAlpha = pushedAbs > state.bleedEnv ? bleedEnvAttackAlpha : bleedEnvReleaseAlpha;
            state.bleedEnv = envAlpha * state.bleedEnv + (1.0f - envAlpha) * pushedAbs;
            sourceDrive[static_cast<size_t>(sample)] = state.bleedEnv;
            const auto saturated = processTapeSample(pushed, state, trackVariance, ips,
                calSaturation, calHfBoost, biasParam, headBumpFreqHz, headBumpAmt);
            auto shapedSample = saturated;

            if (hissOn)
            {
                state.noiseSeed = state.noiseSeed * 1664525u + 1013904223u;
                const auto white = static_cast<float>((state.noiseSeed & 0x7fffffff) / 1073741824.0 - 1.0);
                state.hissLp = hissLpAlpha * state.hissLp + (1.0f - hissLpAlpha) * white;
                const auto hiss = (white - state.hissLp) * hissGain;
                shapedSample += hiss;
            }

            // Azimuth: first-order all-pass per track (Schroeder form)
            if (azimuthParam > 0.001f)
            {
                const float azOut = azCoeff * (shapedSample - state.azApY) + state.azApX;
                state.azApX = shapedSample;
                state.azApY = azOut;

                // Real tape azimuth error also dulls/smears upper frequencies,
                // especially at 15 ips. Blend in an azimuth-controlled LP path.
                state.azToneLp = azToneAlpha * state.azToneLp + (1.0f - azToneAlpha) * azOut;
                shapedSample = juce::jmap(azToneBlend, azOut, state.azToneLp);
            }

            // Print-through: write pre-saturation signal to delay line, read layer echoes
            pBuf[static_cast<size_t>(state.printWritePos)] = pushed;
            if (printThroughParam > 0.001f)
            {
                const auto readPost = (state.printWritePos - safeLayerDelay + printBufferSize) % printBufferSize;
                const auto readPre  = (state.printWritePos - safeLayer2Delay + printBufferSize) % printBufferSize;
                shapedSample += pBuf[static_cast<size_t>(readPost)] * printPostLevel;
                shapedSample += pBuf[static_cast<size_t>(readPre)] * printPreLevel;
            }
            ++state.printWritePos;
            if (state.printWritePos >= printBufferSize)
                state.printWritePos = 0;

            staged[static_cast<size_t>(sample)] = shapedSample;

            // Crosstalk path is band-limited to mimic head/tape coupling between adjacent tracks.
            state.bleedHp = bleedHpAlpha * state.bleedHp + (1.0f - bleedHpAlpha) * pushed;
            const auto highPassed = pushed - state.bleedHp;
            state.bleedLp = bleedLpAlpha * state.bleedLp + (1.0f - bleedLpAlpha) * highPassed;
            bleedBand[static_cast<size_t>(sample)] = state.bleedLp * bleedColourGain;

            if (wowFlutterOn)
            {
                ++state.wowWritePosition;
                if (state.wowWritePosition >= wowDelayBufferSize)
                    state.wowWritePosition = 0;
            }
        }
    }

    for (auto channel = 0; channel < totalOutChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        const auto& current = preBleedChannels[static_cast<size_t>(channel)];
        const bool hasLeft = channel > 0;
        const bool hasRight = channel < (totalOutChannels - 1);
        const auto& left = hasLeft ? bleedBandChannels[static_cast<size_t>(channel - 1)] : current;
        const auto& right = hasRight ? bleedBandChannels[static_cast<size_t>(channel + 1)] : current;
        const auto& leftDrive = hasLeft ? sourceDriveChannels[static_cast<size_t>(channel - 1)] : sourceDriveChannels[static_cast<size_t>(channel)];
        const auto& rightDrive = hasRight ? sourceDriveChannels[static_cast<size_t>(channel + 1)] : sourceDriveChannels[static_cast<size_t>(channel)];

        for (auto sample = 0; sample < numSamples; ++sample)
        {
            auto drySample = current[static_cast<size_t>(sample)];
            float bleedContribution = 0.0f;
            const auto leftEnvDb = juce::Decibels::gainToDecibels(leftDrive[static_cast<size_t>(sample)], -160.0f);
            const auto rightEnvDb = juce::Decibels::gainToDecibels(rightDrive[static_cast<size_t>(sample)], -160.0f);

            auto kneeBlend = [&](float levelDb)
            {
                auto t = juce::jlimit(0.0f, 1.0f, (levelDb - kneeLowDb) / kneeRangeDb);
                return t * t * (3.0f - 2.0f * t);
            };

            const auto leftBleedOpen = kneeBlend(leftEnvDb);
            const auto rightBleedOpen = kneeBlend(rightEnvDb);

            if (hasLeft)
            {
                bleedContribution += left[static_cast<size_t>(sample)] * bleedAmount * leftBleedOpen;
            }

            if (hasRight)
            {
                bleedContribution += right[static_cast<size_t>(sample)] * bleedAmount * rightBleedOpen;
            }

            channelData[sample] = (drySample + bleedContribution) * effOutputGain;
        }
    }

    for (auto channel = totalOutChannels; channel < totalInChannels; ++channel)
    {
        buffer.clear(channel, 0, numSamples);
    }
}

float JuiceAudioProcessor::processTapeSample(float inputSample, TapeChannelState& state,
    float trackVariance, float ips, float calSaturation, float calHfBoost,
    float biasParam, float headBumpFreqHz, float headBumpAmt) const
{
    const auto twoPi = juce::MathConstants<float>::twoPi;
    const auto fs = static_cast<float>(currentSampleRate);

    // -----------------------------------------------------------------------
    // Two-speed tape response model (Ampex 456 / MM1200 reference curves)
    //
    // 15 ips NAB:  resonant head-bump peak ~+1.5 dB @ 80-120 Hz,
    //              flat mids 200 Hz - 8 kHz, -3 dB @ 12 kHz, -6 dB @ 20 kHz
    // 30 ips IEC:  small LF shelf, flat mids, gentle HF rolloff
    //              (existing voicing was already correct for this speed)
    // -----------------------------------------------------------------------

    // Narrow LP for head-bump / LF flux
    const auto bumpAlpha = std::exp(-twoPi * headBumpFreqHz / fs);
    state.flux = bumpAlpha * state.flux + (1.0f - bumpAlpha) * inputSample;

    float preEmphasis;

    if (ips <= 15.0f)
    {
        // ---- 15 ips: resonant bandpass head bump ----
        // headBump = LP_broad - LP_narrow  →  bandpass peaking at sqrt(broad × narrow)
        // broad = headBumpFreqHz × 1.5625 → peak ≈ headBumpFreqHz × 1.25
        // At default 80 Hz: broad = 125 Hz, peak ≈ sqrt(80 × 125) = 100 Hz  ✓
        const auto bumpBroadHz    = headBumpFreqHz * 1.5625f;
        const auto bumpBroadAlpha = std::exp(-twoPi * bumpBroadHz / fs);
        state.hfMemory = bumpBroadAlpha * state.hfMemory + (1.0f - bumpBroadAlpha) * inputSample;
        const auto headBump = state.hfMemory - state.flux;   // positive bandpass

        // Very light HF shelf (the 12 kHz output LP provides the main cut)
        const auto shelfAlpha = std::exp(-twoPi * 5000.0f / fs);
        state.hfShelfLp = shelfAlpha * state.hfShelfLp + (1.0f - shelfAlpha) * inputSample;
        const auto hfShelfContent = inputSample - state.hfShelfLp;
        const auto hfScale = calHfBoost * (1.0f - biasParam * 0.60f);

        // Final calibration: slightly lower bump so default peak is closer to
        // classic 456 alignment (+1.5 to +2 dB, not +3 dB).
        const auto bumpGain = 0.48f * headBumpAmt;
        preEmphasis = inputSample
                    + headBump      * bumpGain
                    + hfShelfContent * 0.04f * hfScale;
    }
    else
    {
        // ---- 30 ips: existing voicing (already correct in analyzer) ----
        const auto memoryAlpha = std::exp(-twoPi * 12800.0f / fs);
        state.hfMemory = memoryAlpha * state.hfMemory + (1.0f - memoryAlpha) * (inputSample - state.flux);
        const auto shelfAlpha = std::exp(-twoPi * 7200.0f / fs);
        state.hfShelfLp = shelfAlpha * state.hfShelfLp + (1.0f - shelfAlpha) * inputSample;
        const auto hfShelfContent = inputSample - state.hfShelfLp;
        const auto hfScale = calHfBoost * (1.0f - biasParam * 0.60f);
        preEmphasis = inputSample
                    + state.hfMemory * 0.33f * hfScale
                    + hfShelfContent * 0.17f * hfScale;
    }

    const auto asymmetry = (0.018f - biasParam * 0.007f) * trackVariance;
    const auto biasDC    = -biasParam * 0.020f;
    // 30 ips keeps small direct flux; 15 ips flux is already in headBump term
    const auto fluxGain      = ips <= 15.0f ? 0.0f : 0.20f;
    const auto magnetization = preEmphasis + asymmetry + state.flux * fluxGain * headBumpAmt + biasDC;

    const auto driveScaled = calSaturation * (1.0f + (-biasParam) * 0.18f);

    auto saturated = std::tanh(1.8f * trackVariance * driveScaled * magnetization);
    saturated = 0.75f * saturated + 0.25f * std::tanh(3.1f * saturated);

    // Output LP: IPS-dependent tape bandwidth limit.
    // 15 ips: a little steeper than 12 kHz to better match observed 456 tops.
    // 30 ips: -3 dB @ 22 kHz  →  barely audible at 20 kHz
    const auto hfRolloffHz    = ips <= 15.0f ? 11000.0f : 22000.0f;
    const auto hfRolloffAlpha = std::exp(-twoPi * hfRolloffHz / fs);
    state.tapeHfLp = hfRolloffAlpha * state.tapeHfLp + (1.0f - hfRolloffAlpha) * saturated;

    // 15 ips calibration trim: brings the mid-band reference closer to 0 dB.
    const auto ipsOutputTrim = ips <= 15.0f ? dbToGain(1.15f) : 1.0f;
    return state.tapeHfLp * ipsOutputTrim;
}

juce::AudioProcessorEditor* JuiceAudioProcessor::createEditor()
{
    return new JuiceAudioProcessorEditor(*this);
}

void JuiceAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = parameters.copyState(); auto xml = state.createXml())
    {
        copyXmlToBinary(*xml, destData);
    }
}

void JuiceAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
    {
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuiceAudioProcessor();
}
