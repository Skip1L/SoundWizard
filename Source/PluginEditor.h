/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

struct RotarySlyder : juce::Slider
{ 
	RotarySlyder() : juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag, juce::Slider::TextEntryBoxPosition::TextBoxBelow)
	{
	}
};

struct ResponseCurveComponent : juce::Component,
	juce::AudioProcessorParameter::Listener,
	juce::Timer
{
	ResponseCurveComponent(SoundWizardAudioProcessor&);
	~ResponseCurveComponent();

	void parameterValueChanged(int parameterIndex, float newValue) override;

	void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override { };

	void updateChain();

	void timerCallback() override;

	void paint(juce::Graphics& graphic) override;

private:
	SoundWizardAudioProcessor& audioProcessor;
	juce::Atomic<bool> parametersChanged {false};
	MonoChain monoChain;
};
//==============================================================================
/**
*/
class SoundWizardAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
	SoundWizardAudioProcessorEditor(SoundWizardAudioProcessor&);
	~SoundWizardAudioProcessorEditor() override;

	//==============================================================================
	void paint(juce::Graphics&) override;
	void resized() override;

private:
	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	SoundWizardAudioProcessor& audioProcessor;

	RotarySlyder peakFreqSlider, peakGainSlider, peakQualitySlider, lowCutFreqSlider, highCutFreqSlider, lowCutSlopeSlider, highCutSlopeSlider;

	//Connect custom slider to back
	using apvts = juce::AudioProcessorValueTreeState;
	using attachment = apvts::SliderAttachment;

	attachment peakFreqSliderAttachment, peakGainSliderAttachment, peakQualitySliderAttachment, lowCutFreqSliderAttachment, highCutFreqSliderAttachment, lowCutSlopeSliderAttachment, highCutSlopeSliderAttachment;

	std::vector<juce::Component*> getComps();

	ResponseCurveComponent responseCurveComponent ;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundWizardAudioProcessorEditor)
};
