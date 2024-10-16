/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SoundWizardAudioProcessor::SoundWizardAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
	: AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
		.withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
		.withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
	)
#endif
{
}

SoundWizardAudioProcessor::~SoundWizardAudioProcessor()
{
}

//==============================================================================
const juce::String SoundWizardAudioProcessor::getName() const
{
	return JucePlugin_Name;
}

bool SoundWizardAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
	return true;
#else
	return false;
#endif
}

bool SoundWizardAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
	return true;
#else
	return false;
#endif
}

bool SoundWizardAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
	return true;
#else
	return false;
#endif
}

double SoundWizardAudioProcessor::getTailLengthSeconds() const
{
	return 0.0;
}

int SoundWizardAudioProcessor::getNumPrograms()
{
	return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
	// so this should be at least 1, even if you're not really implementing programs.
}

int SoundWizardAudioProcessor::getCurrentProgram()
{
	return 0;
}

void SoundWizardAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String SoundWizardAudioProcessor::getProgramName(int index)
{
	return {};
}

void SoundWizardAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void SoundWizardAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	// Use this method as the place to do any pre-playback
	// initialisation that you need..

//Prepare two chanels output
	juce::dsp::ProcessSpec spec{};

	spec.maximumBlockSize = samplesPerBlock;

	spec.numChannels = 1;

	spec.sampleRate = sampleRate;

	leftChain.prepare(spec);
	rightChain.prepare(spec);

	updateFilters();

	leftChanelQueue.prepare(samplesPerBlock);
	rightChanelQueue.prepare(samplesPerBlock);
}

void SoundWizardAudioProcessor::releaseResources()
{
	// When playback stops, you can use this as an opportunity to free up any
	// spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SoundWizardAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
	juce::ignoreUnused(layouts);
	return true;
#else
	// This is the place where you check if the layout is supported.
	// In this template code we only support mono or stereo.
	// Some plugin hosts, such as certain GarageBand versions, will only
	// load plugins that support stereo bus layouts.
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
		&& layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;

	// This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
	if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
		return false;
#endif

	return true;
#endif
}
#endif

void SoundWizardAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;
	auto totalNumInputChannels = getTotalNumInputChannels();
	auto totalNumOutputChannels = getTotalNumOutputChannels();

	// In case we have more outputs than inputs, this code clears any output
	// channels that didn't contain input data, (because these aren't
	// guaranteed to be empty - they may contain garbage).
	// This is here to avoid people getting screaming feedback
	// when they first compile a plugin, but obviously you don't need to keep
	// this code if your algorithm always overwrites all the output channels.
	for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
		buffer.clear(i, 0, buffer.getNumSamples());

	//Update coeffitients
	updateFilters();

	//We need to extract left and right chanel from buffer

	juce::dsp::AudioBlock<float> block(buffer);

	auto leftBlock = block.getSingleChannelBlock(0);
	auto rightBlock = block.getSingleChannelBlock(1);

	juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
	juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

	leftChain.process(leftContext);
	rightChain.process(rightContext);

	leftChanelQueue.update(buffer);
	rightChanelQueue.update(buffer);
}

//==============================================================================
bool SoundWizardAudioProcessor::hasEditor() const
{
	return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SoundWizardAudioProcessor::createEditor()
{
	return new SoundWizardAudioProcessorEditor(*this);
	//return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SoundWizardAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
	// You should use this method to store your parameters in the memory block.
	// You could do that either as raw data, or use the XML or ValueTree classes
	// as intermediaries to make it easy to save and load complex data.
	juce::MemoryOutputStream mos(destData, true);
	apvts.state.writeToStream(mos);
}

void SoundWizardAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
	auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
	if (tree.isValid())
	{
		apvts.replaceState(tree);
		updateFilters();
	}
	// You should use this method to restore your parameters from this memory block,
	// whose contents will have been created by the getStateInformation() call.
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
	ChainSettings settings;

	settings.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
	settings.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
	settings.peakFreq = apvts.getRawParameterValue("Peak Freq")->load();
	settings.peakGainDecibels = apvts.getRawParameterValue("Peak Gain")->load();
	settings.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();

	settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
	settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());

	return settings;
}
//Equalization layout
juce::AudioProcessorValueTreeState::ParameterLayout SoundWizardAudioProcessor::createParameterLayout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout layout;

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"LowCut Freq",
		"LowCut Freq",
		juce::NormalisableRange<float>(20.f, 20000.f, 1.f, .25f),
		20.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"HighCut Freq",
		"HighCut Freq",
		juce::NormalisableRange<float>(20.f, 20000.f, 1.f, .25f),
		20000.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak Freq",
		"Peak Freq",
		juce::NormalisableRange<float>(20.f, 20000.f, 1.f, .25f),
		750.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak Gain",
		"Peak Gain",
		juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
		0.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak Quality",
		"Peak Quality",
		juce::NormalisableRange<float>(.1f, 10.f, .05f, 1.f),
		1.f));

	//Choises for Slopes
	juce::StringArray stringArray;
	for (auto i = 0; i < 4; i++)
	{
		juce::String str;
		str << (12 + i * 12);
		str << " db/Oct";
		stringArray.add(str);
	}

	layout.add(std::make_unique<juce::AudioParameterChoice>("LowCut Slope", "LowCut Slope", stringArray, 0));
	layout.add(std::make_unique<juce::AudioParameterChoice>("HighCut Slope", "HighCut Slope", stringArray, 0));

	return layout;
}



inline void updateCoefficients(Coefficients& old, const Coefficients& replacements)
{
	*old = *replacements;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
	return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
		sampleRate,
		chainSettings.peakFreq,
		chainSettings.peakQuality,
		juce::Decibels::decibelsToGain(chainSettings.peakGainDecibels));
}


void SoundWizardAudioProcessor::updatePeakFilter(const ChainSettings& chainSettings)
{
	auto peekCoefficient = makePeakFilter(chainSettings, getSampleRate());

	updateCoefficients(leftChain.get<ChainPossition::Peak>().coefficients, peekCoefficient);
	updateCoefficients(rightChain.get<ChainPossition::Peak>().coefficients, peekCoefficient);
}

void SoundWizardAudioProcessor::updateLowCutFilters(const ChainSettings& chainSettings)
{
	auto lowCutCoefficients = makeLowCutFilter(chainSettings, getSampleRate());

	auto& leftLowCut = leftChain.get<ChainPossition::LowCut>();
	auto& rightLowCut = rightChain.get<ChainPossition::LowCut>();
	updateCutFilter(leftLowCut, lowCutCoefficients, chainSettings.lowCutSlope);
	updateCutFilter(rightLowCut, lowCutCoefficients, chainSettings.lowCutSlope);
}

void SoundWizardAudioProcessor::updateHighCutFilters(const ChainSettings& chainSettings)
{
	auto highCutCoefficients = makeHighCutFilter(chainSettings,getSampleRate());

	auto& leftHighCut = leftChain.get<ChainPossition::HighCut>();
	auto& rightHighCut = rightChain.get<ChainPossition::HighCut>();
	updateCutFilter(leftHighCut, highCutCoefficients, chainSettings.highCutSlope);
	updateCutFilter(rightHighCut, highCutCoefficients, chainSettings.highCutSlope);
}

void SoundWizardAudioProcessor::updateFilters()
{
	auto chainSettings = getChainSettings(apvts);

	updatePeakFilter(chainSettings);

	updateLowCutFilters(chainSettings);
	updateHighCutFilters(chainSettings);
}

template<typename ChainType, typename CoefficientType>
void updateCutFilter(ChainType& chain,
		const CoefficientType& coefficients,
		const Slope& slope)
{
	chain.setBypassed<0>(true);
	chain.setBypassed<1>(true);
	chain.setBypassed<2>(true);
	chain.setBypassed<3>(true);

	switch( slope )
    {
        case S_48:
        {
            updateSlope<3>(chain, coefficients);
        }
        case S_36:
        {
            updateSlope<2>(chain, coefficients);
        }
        case S_24:
        {
            updateSlope<1>(chain, coefficients);
        }
        case S_12:
        {
            updateSlope<0>(chain, coefficients);
        }
    }
}

template<int Index, typename ChainType, typename CoefficientType>
inline void updateSlope(ChainType & chainType, const CoefficientType & coefficients)
{
	updateCoefficients(chainType.template get<Index>().coefficients, coefficients[Index]);
    chainType.template setBypassed<Index>(false);
}



//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new SoundWizardAudioProcessor();
}