/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//Аналізатор спектру
template<typename T>
struct Queue
{
    /*Підготовка буферу*/
    void prepare(int numChannels, int numSamples)
    {
        static_assert( std::is_same_v<T, juce::AudioBuffer<float>>,
                      "prepare(numChannels, numSamples) should only be used when the Queue is holding juce::AudioBuffer<float>");
        for( auto& buffer : buffers)
        {
            buffer.setSize(numChannels,
                           numSamples,
                           false,   //очистити буфер?
                           true,    //виділити додаткове місце?
                           true);   //уникати перерозподілу?
            buffer.clear();
        }
    }
    
    void prepare(size_t numElements)
    {
        static_assert( std::is_same_v<T, std::vector<float>>,
                      "prepare(numElements) should only be used when the Queue is holding std::vector<float>");
        for( auto& buffer : buffers )
        {
            buffer.clear();
            buffer.resize(numElements, 0);
        }
    }
    
    bool push(const T& t)
    {
        auto write = queue.write(1);
        if( write.blockSize1 > 0 )
        {
            buffers[write.startIndex1] = t;
            return true;
        }
        
        return false;
    }
    
    bool pull(T& t)
    {
        auto read = queue.read(1);
        if( read.blockSize1 > 0 )
        {
            t = buffers[read.startIndex1];
            return true;
        }
        
        return false;
    }
    
    int getNumAvailableForReading() const
    {
        return queue.getNumReady();
    }
private:
    static constexpr int Capacity = 30;
    std::array<T, Capacity> buffers;
    juce::AbstractFifo queue {Capacity};
};

enum Channel
{
	Right, //0
	Left //1
};

//Аналізатор спектру
template<typename BlockType>
struct SingleChannelSampleQueue
{
    SingleChannelSampleQueue(Channel ch) : channelToUse(ch)
    {
        prepared.set(false);
    }
    
    void update(const BlockType& buffer)
    {
        jassert(prepared.get());
        jassert(buffer.getNumChannels() > channelToUse );
        auto* channelPtr = buffer.getReadPointer(channelToUse);
        
        for( int i = 0; i < buffer.getNumSamples(); ++i )
        {
            pushNextSampleIntoQueue(channelPtr[i]);
        }
    }

    void prepare(int bufferSize)
    {
        prepared.set(false);
        size.set(bufferSize);
        
        bufferToFill.setSize(1,             //канал
                             bufferSize,    //номер семплу
                             false,         //зберегти існуючий вміст
                             true,          //очистити додаткову пам'ять
                             true);         //уникати перерозподілу
        audioBufferQueue.prepare(1, bufferSize);
        queueIndex = 0;
        prepared.set(true);
    }
    //==============================================================================
    int getNumCompleteBuffersAvailable() const { return audioBufferQueue.getNumAvailableForReading(); }
    bool isPrepared() const { return prepared.get(); }
    int getSize() const { return size.get(); }
    //==============================================================================
    bool getAudioBuffer(BlockType& buf) { return audioBufferQueue.pull(buf); }
private:
    Channel channelToUse;
    int queueIndex = 0;
    Queue<BlockType> audioBufferQueue;
    BlockType bufferToFill;
    juce::Atomic<bool> prepared = false;
    juce::Atomic<int> size = 0;
    
    void pushNextSampleIntoQueue(float sample)
    {
        if (queueIndex == bufferToFill.getNumSamples())
        {
            auto ok = audioBufferQueue.push(bufferToFill);

            juce::ignoreUnused(ok);
            
            queueIndex = 0;
        }
        
        bufferToFill.setSample(0, queueIndex, sample);
        ++queueIndex;
    }
};

enum Slope
{
	S_12,
	S_24,
	S_36,
	S_48
};

enum ChainPossition
{
	LowCut,
	Peak,
	HighCut
};

struct ChainSettings
{
	float peakFreq{ 0 }, peakGainDecibels{ 0 }, peakQuality{ 1.f }, lowCutFreq{ 0 }, highCutFreq{ 0 };

	Slope lowCutSlope{ Slope::S_12 }, highCutSlope{ Slope::S_12 };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

using Filter = juce::dsp::IIR::Filter<float>;

using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;

using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter>;

using Coefficients = Filter::CoefficientsPtr;
void updateCoefficients(Coefficients& old, const Coefficients& replacements);

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate);

template<int Index, typename ChainType, typename CoefficientType>
void updateSlope(ChainType& chainType, const CoefficientType& coefficients);

template<typename ChainType, typename CoefficientType>
void updateCutFilter(ChainType& chain,
	const CoefficientType& coefficients,
	const Slope& slope);

inline auto makeLowCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
	return juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(
		chainSettings.lowCutFreq,
		sampleRate,
		(chainSettings.lowCutSlope + 1) * 2);
}

inline auto makeHighCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
	return juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(
		chainSettings.highCutFreq,
		sampleRate,
		(chainSettings.highCutSlope + 1) * 2);
}
//==============================================================================
/**
*/
class SoundWizardAudioProcessor : public juce::AudioProcessor
#if JucePlugin_Enable_ARA
	, public juce::AudioProcessorARAExtension
#endif
{
public:
	//==============================================================================
	SoundWizardAudioProcessor();
	~SoundWizardAudioProcessor() override;

	//==============================================================================
	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
	bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

	void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

	//==============================================================================
	juce::AudioProcessorEditor* createEditor() override;
	bool hasEditor() const override;

	//==============================================================================
	const juce::String getName() const override;

	bool acceptsMidi() const override;
	bool producesMidi() const override;
	bool isMidiEffect() const override;
	double getTailLengthSeconds() const override;

	//==============================================================================
	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram(int index) override;
	const juce::String getProgramName(int index) override;
	void changeProgramName(int index, const juce::String& newName) override;

	//==============================================================================
	void getStateInformation(juce::MemoryBlock& destData) override;
	void setStateInformation(const void* data, int sizeInBytes) override;

	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
	juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameters", createParameterLayout()};

    using BlockType = juce::AudioBuffer<float>;
    SingleChannelSampleQueue<BlockType> leftChanelQueue {Channel::Left};
    SingleChannelSampleQueue<BlockType> rightChanelQueue {Channel::Right};
private:

	//Create a stereo using 2 mono channels
	MonoChain leftChain, rightChain;

	void updatePeakFilter(const ChainSettings& chainSettings);

	void updateLowCutFilters(const ChainSettings& chainSettings);
	void updateHighCutFilters(const ChainSettings& chainSettings);

	void updateFilters();
	//==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundWizardAudioProcessor)
};
