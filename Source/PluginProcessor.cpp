#include "PluginProcessor.h"
#include "PluginEditor.h"

MoggDualLadderAudioProcessor::MoggDualLadderAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inGainParam  = apvts.getRawParameterValue("inGain");
    outGainParam = apvts.getRawParameterValue("outGain");
    lpFreqParam  = apvts.getRawParameterValue("lpFreq");
    hpFreqParam  = apvts.getRawParameterValue("hpFreq");
    lpResParam   = apvts.getRawParameterValue("lpRes");
    hpResParam   = apvts.getRawParameterValue("hpRes");
    driveParam   = apvts.getRawParameterValue("drive");
    linkParam    = apvts.getRawParameterValue("link");
    slopeParam   = apvts.getRawParameterValue("slope");

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
    auto lpFreqRange = juce::NormalisableRange<float>(20.0f, 30000.0f, 0.1f, 0.25f);
    auto levelRange  = juce::NormalisableRange<float>(-10.0f, 10.0f, 0.1f);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("inGain", 1), "Input Level", levelRange, 0.0f, "dB"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outGain", 1), "Output Level", levelRange, 0.0f, "dB"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lpFreq", 1), "Lowpass Cutoff", lpFreqRange, 30000.0f, "Hz"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hpFreq", 1), "Highpass Cutoff", hpFreqRange, 10.0f, "Hz"));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("lpRes", 1), "Lowpass Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hpRes", 1), "Highpass Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("drive", 1), "Input Drive",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f, "dB"));

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
    if (isUpdatingLink.load()) {
        return;
    }

    const float currentLp = lpFreqParam->load();
    const float currentHp = hpFreqParam->load();
    const bool isLinked   = linkParam->load() > 0.5f;

    if (parameterID == "link") {
        if (newValue > 0.5f) {
            float ratio = std::clamp(currentHp / std::max(currentLp, 20.0f), 0.0001f, 1000.0f);
            linkedFreqRatio.store(ratio);
        }
    } else if (parameterID == "lpFreq" && isLinked) {
        if (auto* hpParam = apvts.getParameter("hpFreq")) {
            isUpdatingLink.store(true);
            float targetHp = std::clamp(newValue * linkedFreqRatio.load(), 10.0f, 20000.0f);
            hpParam->setValueNotifyingHost(hpParam->convertTo0to1(targetHp));
            isUpdatingLink.store(false);
        }
    } else if (parameterID == "hpFreq" && isLinked) {
        float ratio = std::clamp(newValue / std::max(currentLp, 20.0f), 0.0001f, 1000.0f);
        linkedFreqRatio.store(ratio);
    }
}

void MoggDualLadderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    oversampling.reset();
    oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));

    const double oversampledRate = sampleRate * oversampling.getOversamplingFactor();

    filterL.setSampleRate(oversampledRate);
    filterR.setSampleRate(oversampledRate);
    filterL.reset();
    filterR.reset();

    setLatencySamples(static_cast<int>(oversampling.getLatencyInSamples()));
}

void MoggDualLadderAudioProcessor::releaseResources() {
    oversampling.reset();
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

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
        buffer.clear(i, 0, buffer.getNumSamples());
    }

    const float inGain   = inGainParam->load();
    const float outGain  = outGainParam->load();
    const float lpFreq   = lpFreqParam->load();
    const float hpFreq   = hpFreqParam->load();
    const float lpRes    = lpResParam->load();
    const float hpRes    = hpResParam->load();
    const float drive    = driveParam->load();
    const int slopeIndex = std::clamp(static_cast<int>(std::lround(slopeParam->load())), 0, 3);
    const auto slope     = static_cast<MoggDualLadderFilter::Slope>(slopeIndex);

    filterL.setParameters(lpFreq, hpFreq, lpRes, hpRes, drive, inGain, outGain, slope);
    filterR.setParameters(lpFreq, hpFreq, lpRes, hpRes, drive, inGain, outGain, slope);

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
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MoggDualLadderAudioProcessor();
}
