#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class JuiceAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::ComboBox::Listener
{
public:
    explicit JuiceAudioProcessorEditor(JuiceAudioProcessor&);
    ~JuiceAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;

    JuiceAudioProcessor& audioProcessor;
    juce::Label titleLabel;
    juce::Label trackLabel;
    juce::Label ipsLabel;
    juce::Label calLabel;
    juce::Label inputNameLabel;
    juce::Label driveNameLabel;
    juce::Label outputNameLabel;
    juce::Label bleedNameLabel;
    juce::Label bleedThresholdNameLabel;
    juce::Label biasNameLabel;
    juce::Label headBumpFreqNameLabel;
    juce::Label headBumpAmtNameLabel;
    juce::Label printThroughNameLabel;
    juce::Label azimuthNameLabel;

    juce::Slider inputSlider;
    juce::Slider driveSlider;
    juce::Slider outputSlider;
    juce::Slider bleedSlider;
    juce::Slider bleedThresholdSlider;
    juce::Slider biasSlider;
    juce::Slider headBumpFreqSlider;
    juce::Slider headBumpAmtSlider;
    juce::Slider printThroughSlider;
    juce::Slider azimuthSlider;
    juce::ComboBox trackSelector;
    juce::ComboBox ipsSelector;
    juce::ComboBox calSelector;
    juce::ToggleButton wowFlutterButton;
    juce::ToggleButton hissButton;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> driveAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> bleedAttachment;
    std::unique_ptr<SliderAttachment> bleedThresholdAttachment;
    std::unique_ptr<SliderAttachment> biasAttachment;
    std::unique_ptr<SliderAttachment> headBumpFreqAttachment;
    std::unique_ptr<SliderAttachment> headBumpAmtAttachment;
    std::unique_ptr<SliderAttachment> printThroughAttachment;
    std::unique_ptr<SliderAttachment> azimuthAttachment;
    std::unique_ptr<ComboAttachment> trackAttachment;
    std::unique_ptr<ComboAttachment> ipsAttachment;
    std::unique_ptr<ComboAttachment> calAttachment;
    std::unique_ptr<ButtonAttachment> wowFlutterAttachment;
    std::unique_ptr<ButtonAttachment> hissAttachment;

    void configureRotarySlider(juce::Slider& slider, const juce::String& suffix);
    void configureControlLabel(juce::Label& label, const juce::String& text);
    void updateTrackLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuiceAudioProcessorEditor)
};
