#include "PluginProcessor.h"
#include "PluginEditor.h"

MoggDualLadderAudioProcessor::MoggDualLadderAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inGainParam     = apvts.getRawParameterValue("inGain");
    outGainParam    = apvts.getRawParameterValue("outGain");
    lpFreqParam     = apvts.getRawParameterValue("lpFreq");
    hpFreqParam     = apvts.getRawParameterValue("hpFreq");
    lpResParam      = apvts.getRawParameterValue("lpRes");
    hpResParam      = apvts.getRawParameterValue("hpRes");
    hpSatParam      = apvts.getRawParameterValue("hpSat");
    hpSatModeParam  = apvts.getRawParameterValue("hpSatMode");
    lpSatParam      = apvts.getRawParameterValue("lpSat");
    lpSatModeParam  = apvts.getRawParameterValue("lpSatMode");
    dirtParam       = apvts.getRawParameterValue("dirt");
    airBoostParam   = apvts.getRawParameterValue("airBoost");
    linkParam       = apvts.getRawParameterValue("link");
    slopeParam      = apvts.getRawParameterValue("slope");

    apvts.addParameterListener("lpFreq", this);
    apvts.addParameterListener("hpFreq", this);
    apvts.addParameterListener("link", this);

    filterL.setComponentTolerance(1.000, 1.000);
    filterR.setComponentTolerance(1.003, 0.996);
}

MoggDualLadderAudioProcessor::~MoggDualLadderAudioProcessor() {
    apvts.removeParameterListener("lpFreq", this);
    apvts.removeParameterListener("hpFreq", this);
    apvts.removeParameterListener("link", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout MoggDualLadderAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto hpFreqRange = juce::NormalisableRange<float>(10.0f, 20000.0f, 0.1f, 0.25f);
    auto lpFreqRange = juce::NormalisableRange<float>(20.0f, 22000.0f, 0.1f, 0.25f);
    auto levelRange  = juce::NormalisableRange<float>(-10.0f, 10.0f, 0.1f);
    auto resRange    = juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f);
    auto satRange    = juce::NormalisableRange<float>(1.0f, 11.0f, 0.1f);
    auto dirtRange   = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);

    const juce::StringArray satModes { "Ladder", "Warm", "Fold", "Grit" };

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("inGain", 1), "Input Level", levelRange, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outGain", 1), "Output Level", levelRange, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lpFreq", 1), "Lowpass Cutoff", lpFreqRange, 22000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hpFreq", 1), "Highpass Cutoff", hpFreqRange, 10.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lpRes", 1), "Lowpass Resonance", resRange, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hpRes", 1), "Highpass Resonance", resRange, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hpSat", 1), "Highpass Saturation", satRange, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("hpSatMode", 1), "Highpass Saturation Mode", satModes, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lpSat", 1), "Lowpass Saturation", satRange, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("lpSatMode", 1), "Lowpass Saturation Mode", satModes, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("dirt", 1), "Dirt", dirtRange, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("airBoost", 1), "Air Shelf Boost", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("link", 1), "Link Frequencies", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("slope", 1),
        "Filter Slope",
        juce::StringArray { "6 dB", "12 dB", "24 dB", "48 dB" },
        2));

    return { params.begin(), params.end() };
}

void MoggDualLadderAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue) {
    if (isRestoringState.load()) {
        return;
    }

    bool expected = false;
    if (!isUpdatingLink.compare_exchange_strong(expected, true)) {
        return;
    }

    struct ResetGuard {
        std::atomic<bool>& flag;
        ~ResetGuard() { flag.store(false); }
    } guard { isUpdatingLink };

    constexpr float minHpLimit = 10.0f;
    constexpr float maxHpLimit = 20000.0f;
    constexpr float minLpLimit = 20.0f;
    constexpr float maxLpLimit = 22000.0f;
    constexpr float minRatio   = minHpLimit / maxLpLimit;
    constexpr float maxRatio   = maxHpLimit / minLpLimit;

    const float currentLp = lpFreqParam->load();
    const float currentHp = hpFreqParam->load();
    const bool isLinked   = linkParam->load() > 0.5f;

    if (parameterID == "link") {
        if (newValue > 0.5f) {
            const float ratio = std::clamp(currentHp / std::max(currentLp, minLpLimit), minRatio, maxRatio);
            linkedFreqRatio.store(ratio);
        }
    } else if (parameterID == "lpFreq" && isLinked) {
        auto* lpParam = apvts.getParameter("lpFreq");
        auto* hpParam = apvts.getParameter("hpFreq");

        if (lpParam != nullptr && hpParam != nullptr) {
            const float ratio = std::clamp(linkedFreqRatio.load(), minRatio, maxRatio);

            const float coupledMinLp = std::max(minLpLimit, minHpLimit / ratio);
            const float coupledMaxLp = std::max(coupledMinLp, std::min(maxLpLimit, maxHpLimit / ratio));

            const float clampedLp = std::clamp(newValue, coupledMinLp, coupledMaxLp);
            const float targetHp  = std::clamp(clampedLp * ratio, minHpLimit, maxHpLimit);

            const float normLp    = lpParam->convertTo0to1(clampedLp);
            const float snappedLp = lpParam->convertFrom0to1(normLp);

            if (std::abs(snappedLp - newValue) > 0.001f) {
                lpParam->setValueNotifyingHost(normLp);
            }

            hpParam->setValueNotifyingHost(hpParam->convertTo0to1(targetHp));
        }
    } else if (parameterID == "hpFreq" && isLinked) {
        auto* lpParam = apvts.getParameter("lpFreq");
        auto* hpParam = apvts.getParameter("hpFreq");

        if (lpParam != nullptr && hpParam != nullptr) {
            const float ratio = std::clamp(linkedFreqRatio.load(), minRatio, maxRatio);

            const float coupledMinHp = std::max(minHpLimit, minLpLimit * ratio);
            const float coupledMaxHp = std::max(coupledMinHp, std::min(maxHpLimit, maxLpLimit * ratio));

            const float clampedHp = std::clamp(newValue, coupledMinHp, coupledMaxHp);
            const float targetLp  = std::clamp(clampedHp / ratio, minLpLimit, maxLpLimit);

            const float normHp    = hpParam->convertTo0to1(clampedHp);
            const float snappedHp = hpParam->convertFrom0to1(normHp);

            if (std::abs(snappedHp - newValue) > 0.001f) {
                hpParam->setValueNotifyingHost(normHp);
            }

            lpParam->setValueNotifyingHost(lpParam->convertTo0to1(targetLp));
        }
    }
}

void MoggDualLadderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;

    oversampling.reset();
    oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));

    const double oversampledRate = sampleRate * static_cast<double>(oversampling.getOversamplingFactor());

    filterL.setSampleRate(oversampledRate);
    filterR.setSampleRate(oversampledRate);
    filterL.reset();
    filterR.reset();

    airShelfGainDb.reset(sampleRate, 0.020);
    airShelfGainDb.setCurrentAndTargetValue(0.0f);
    airShelfL.reset();
    airShelfR.reset();

    inPeakL.store(0.0f, std::memory_order_relaxed);
    inPeakR.store(0.0f, std::memory_order_relaxed);
    outPeakL.store(0.0f, std::memory_order_relaxed);
    outPeakR.store(0.0f, std::memory_order_relaxed);

    setLatencySamples(static_cast<int>(oversampling.getLatencyInSamples()));
}

void MoggDualLadderAudioProcessor::releaseResources() {
    oversampling.reset();
    airShelfL.reset();
    airShelfR.reset();
}

bool MoggDualLadderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return (mainIn == mainOut) && (mainIn == juce::AudioChannelSet::mono() || mainIn == juce::AudioChannelSet::stereo());
}

void MoggDualLadderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numBlockSamples        = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
        buffer.clear(i, 0, numBlockSamples);
    }

    const float inGain      = inGainParam->load();
    const float outGain     = outGainParam->load();
    const float lpFreq      = lpFreqParam->load();
    const float hpFreq      = hpFreqParam->load();
    const float lpRes       = lpResParam->load();
    const float hpRes       = hpResParam->load();
    const float hpSat       = hpSatParam->load();
    const int   hpModeIdx   = std::clamp(static_cast<int>(std::lround(hpSatModeParam->load())), 0, 3);
    const auto  hpSatMode   = static_cast<MoggDualLadderFilter::SatMode>(hpModeIdx);
    const float lpSat       = lpSatParam->load();
    const int   lpModeIdx   = std::clamp(static_cast<int>(std::lround(lpSatModeParam->load())), 0, 3);
    const auto  lpSatMode   = static_cast<MoggDualLadderFilter::SatMode>(lpModeIdx);
    const float dirt        = dirtParam->load();
    const int   slopeIndex  = std::clamp(static_cast<int>(std::lround(slopeParam->load())), 0, 3);
    const auto  slope       = static_cast<MoggDualLadderFilter::Slope>(slopeIndex);

    // Input-Metering (inkl. IN LEVEL Gain-Stufe)
    const float inGainLinear = juce::Decibels::decibelsToGain(inGain);
    if (totalNumInputChannels >= 1 && numBlockSamples > 0) {
        const float peakL = buffer.getMagnitude(0, 0, numBlockSamples) * inGainLinear;
        storeMaxPeak(inPeakL, peakL);
        if (totalNumInputChannels >= 2) {
            const float peakR = buffer.getMagnitude(1, 0, numBlockSamples) * inGainLinear;
            storeMaxPeak(inPeakR, peakR);
        } else {
            storeMaxPeak(inPeakR, peakL);
        }
    }

    filterL.setParameters(lpFreq, hpFreq, lpRes, hpRes,
                          hpSat, hpSatMode,
                          lpSat, lpSatMode,
                          dirt, inGain, outGain, slope);
    filterR.setParameters(lpFreq, hpFreq, lpRes, hpRes,
                          hpSat, hpSatMode,
                          lpSat, lpSatMode,
                          dirt, inGain, outGain, slope);

    juce::dsp::AudioBlock<float> block(buffer);
    auto osBlock = oversampling.processSamplesUp(block);

    const size_t numSamples  = osBlock.getNumSamples();
    const size_t numChannels = osBlock.getNumChannels();

    if (numChannels >= 1) {
        float* left = osBlock.getChannelPointer(0);
        for (size_t i = 0; i < numSamples; ++i) {
            left[i] = filterL.processSample(left[i]);
        }
    }

    if (numChannels >= 2) {
        float* right = osBlock.getChannelPointer(1);
        for (size_t i = 0; i < numSamples; ++i) {
            right[i] = filterR.processSample(right[i]);
        }
    }

    oversampling.processSamplesDown(block);

    // Nach den Filtern: Weicher Höhen-Shelf-Boost (kompensiert den Höhenabfall je nach Slope)
    constexpr float kSlopeCompensationDb[4] = { 1.5f, 2.8f, 4.8f, 7.2f };
    const bool airEnabled = airBoostParam->load() > 0.5f;
    const float targetShelfDb = airEnabled ? kSlopeCompensationDb[slopeIndex] : 0.0f;
    airShelfGainDb.setTargetValue(targetShelfDb);

    if (airEnabled || airShelfGainDb.isSmoothing()) {
        if (numBlockSamples > 0) {
            float* outLeft  = totalNumOutputChannels >= 1 ? buffer.getWritePointer(0) : nullptr;
            float* outRight = totalNumOutputChannels >= 2 ? buffer.getWritePointer(1) : nullptr;

            for (int i = 0; i < numBlockSamples; ++i) {
                if (airShelfGainDb.isSmoothing()) {
                    const double currentDb = static_cast<double>(airShelfGainDb.getNextValue());
                    airShelfL.setCoefficients(currentSampleRate, 5800.0, 0.50, currentDb);
                    airShelfR.setCoefficients(currentSampleRate, 5800.0, 0.50, currentDb);
                }

                if (outLeft != nullptr)  outLeft[i]  = airShelfL.processSample(outLeft[i]);
                if (outRight != nullptr) outRight[i] = airShelfR.processSample(outRight[i]);
            }
        }
    } else {
        airShelfL.reset();
        airShelfR.reset();
    }

    // Output-Metering (inkl. optionalem Höhen-Shelf-Boost)
    if (totalNumOutputChannels >= 1 && numBlockSamples > 0) {
        const float peakL = buffer.getMagnitude(0, 0, numBlockSamples);
        storeMaxPeak(outPeakL, peakL);
        if (totalNumOutputChannels >= 2) {
            const float peakR = buffer.getMagnitude(1, 0, numBlockSamples);
            storeMaxPeak(outPeakR, peakR);
        } else {
            storeMaxPeak(outPeakR, peakL);
        }
    }
}

juce::AudioProcessorEditor* MoggDualLadderAudioProcessor::createEditor() {
    return new MoggDualLadderAudioProcessorEditor(*this);
}

void MoggDualLadderAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MoggDualLadderAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType())) {
        isRestoringState.store(true);
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        isRestoringState.store(false);

        if (linkParam->load() > 0.5f) {
            const float ratio = std::clamp(hpFreqParam->load() / std::max(lpFreqParam->load(), 20.0f),
                                           10.0f / 22000.0f, 1000.0f);
            linkedFreqRatio.store(ratio);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MoggDualLadderAudioProcessor();
}
