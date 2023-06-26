/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

ResponseCurveComponent::ResponseCurveComponent(SoundWizardAudioProcessor& processor) :audioProcessor(processor), leftChannelQueue(&audioProcessor.leftChanelQueue)
{
	const auto& params = audioProcessor.getParameters();

	for (auto param : params)
		param->addListener(this);

	leftChannelFFTDataGenerator.changeOrder(FFTOrder::order2048);
	monoBuffer.setSize(1, leftChannelFFTDataGenerator.getFFTSize());

	updateChain();

	startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
	const auto& params = audioProcessor.getParameters();

	for (auto param : params)
		param->removeListener(this);
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
	parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback()
{
	juce::AudioBuffer<float> tempIncomingBuffer;

	while (leftChannelQueue->getNumCompleteBuffersAvailable() > 0)
		if (leftChannelQueue->getAudioBuffer(tempIncomingBuffer))
		{
			auto size = tempIncomingBuffer.getNumSamples();
			//shifting over the data
			juce::FloatVectorOperations::copy(
				monoBuffer.getWritePointer(0, 0),
				monoBuffer.getReadPointer(0, size),
				monoBuffer.getNumSamples() - size);
			//copying this to the end
			juce::FloatVectorOperations::copy(
				monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
				tempIncomingBuffer.getReadPointer(0, 0),
				size);

			leftChannelFFTDataGenerator.produceFFTDataForRendering(monoBuffer, -48.f);
		}

	const auto fftBounds = getLocalBounds().toFloat();
	const auto fftSize = leftChannelFFTDataGenerator.getFFTSize();

	const auto binWidth = audioProcessor.getSampleRate() / (double)fftSize;

	while (leftChannelFFTDataGenerator.getNumAvailableFFTDataBlocks() > 0)
	{
		std::vector<float> fftData;
		if (leftChannelFFTDataGenerator.getFFTData(fftData))
		{
			pathProducer.generatePath(fftData, fftBounds, fftSize, binWidth, -48.f);
		}
	}
	while (pathProducer.getNumPathsAvailable())
	{
		pathProducer.getPath(leftPanelFFTPath);
	}

	if (parametersChanged.compareAndSetBool(false, true))
	{
		//update the monochain
		updateChain();
		//signal a repaint
	}

	repaint();
}

void ResponseCurveComponent::updateChain()
{
	//Peak chain
	auto chainSettings = getChainSettings(audioProcessor.apvts);
	auto peakCoefficient = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
	updateCoefficients(monoChain.get<ChainPossition::Peak>().coefficients, peakCoefficient);

	//Cut chain
	auto lowCutCoefficients = makeLowCutFilter(chainSettings, audioProcessor.getSampleRate());
	auto highCutCoefficients = makeHighCutFilter(chainSettings, audioProcessor.getSampleRate());

	updateCutFilter(monoChain.get<ChainPossition::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
	updateCutFilter(monoChain.get<ChainPossition::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
	using namespace juce;
	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);

	g.drawImage(background, getLocalBounds().toFloat());

	auto responseArea = getLocalBounds();

	auto w = responseArea.getWidth();

	auto& lowcut = monoChain.get<ChainPossition::LowCut>();
	auto& peak = monoChain.get<ChainPossition::Peak>();
	auto& highcut = monoChain.get<ChainPossition::HighCut>();

	auto sampleRate = audioProcessor.getSampleRate();

	std::vector<double> mags;

	mags.resize(w);

	for (int i = 0; i < w; i++)
	{
		double mag = 1.f;
		auto freq = mapToLog10((double)i / (double)w, 20.0, 20000.0);

		if (!monoChain.isBypassed<ChainPossition::Peak>())
			mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);

		if (!lowcut.isBypassed<0>())
			mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowcut.isBypassed<1>())
			mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowcut.isBypassed<2>())
			mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowcut.isBypassed<3>())
			mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

		if (!highcut.isBypassed<0>())
			mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highcut.isBypassed<1>())
			mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highcut.isBypassed<2>())
			mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highcut.isBypassed<3>())
			mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

		mags[i] = Decibels::gainToDecibels(mag);
	}

	Path responseCurve;

	const double outputMin = responseArea.getBottom();
	const double outputMax = responseArea.getY();

	auto map = [outputMin, outputMax](double input)
	{
		return jmap(input, -24.0, 24.0, outputMin, outputMax);
	};

	responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

	for (size_t i = 0; i < mags.size(); i++)
	{
		responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
	}

	leftPanelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY()));

	g.setColour(Colours::aliceblue);
	g.strokePath(leftPanelFFTPath, PathStrokeType(2.f));

	g.setColour(Colours::antiquewhite);
	g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);

	g.setColour(Colours::white);
	g.strokePath(responseCurve, PathStrokeType(2.f));
}

void ResponseCurveComponent::resized()
{
	using namespace juce;
	background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);

	Graphics g(background);

	Array<float> freqs
	{
		50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 20000
	};

	g.setColour(Colours::darkgrey);

	for (auto f : freqs)
	{
		auto normX = mapFromLog10(f, 20.f, 20000.f);

		g.drawVerticalLine(getWidth() * normX, 0.f, getHeight());
	}

	Array<float> gain
	{
		-24, -12, 0, 12, 24
	};

	for (auto gDb : gain)
	{
		auto y = jmap(gDb, -24.f, 24.f, float(getHeight()), 0.f);

		g.drawHorizontalLine(y, 0.f, getWidth());
	}
}

std::vector<juce::Component*> SoundWizardAudioProcessorEditor::getComps()
{
	return { &peakFreqSlider, &peakGainSlider, &peakQualitySlider, &lowCutFreqSlider, &highCutFreqSlider, &lowCutSlopeSlider, &highCutSlopeSlider, &responseCurveComponent };
}

//==============================================================================
SoundWizardAudioProcessorEditor::SoundWizardAudioProcessorEditor(SoundWizardAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p),
	peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
	peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
	peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
	lowCutFreqSliderAttachment(audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
	highCutFreqSliderAttachment(audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
	lowCutSlopeSliderAttachment(audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
	highCutSlopeSliderAttachment(audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider),
	responseCurveComponent(audioProcessor)
{
	// Make sure that before the constructor has finished, you've set the
	// editor's size to whatever you need it to be.
	for (auto* comp : getComps())
	{
		addAndMakeVisible(comp);
	}

	setSize(600, 400);
}

SoundWizardAudioProcessorEditor::~SoundWizardAudioProcessorEditor()
{
}

//==============================================================================
void SoundWizardAudioProcessorEditor::paint(juce::Graphics& g)
{
	using namespace juce;
	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);
}

void SoundWizardAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..

	auto bounds = getLocalBounds();
	float hRatio = 25.f / 100.f;
	auto responseArea = bounds.removeFromTop(bounds.getHeight() * hRatio);

	responseCurveComponent.setBounds(responseArea);

	auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
	auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

	lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
	lowCutSlopeSlider.setBounds(lowCutArea);
	highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
	highCutSlopeSlider.setBounds(highCutArea);

	peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
	peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
	peakQualitySlider.setBounds(bounds);
}