#pragma once

#include <JuceHeader.h>
#include <array>

#include "Dolby740Processor.h"

class Dolby740AudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit Dolby740AudioProcessorEditor(Dolby740AudioProcessor& processor);
    ~Dolby740AudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int kNumStrips = 2;
    static constexpr int kThresholdLedCount = 5;

    Dolby740AudioProcessor& audioProcessor;

    juce::Label titleLabel;
    juce::Label operatingLevelLabel;
    juce::Label stereoLinkLabel;
    juce::ToggleButton stereoLinkButton;
    juce::Label analogModeLabel;
    juce::ToggleButton analogModeButton;
    juce::Label analogInOutStageLabel;
    juce::ToggleButton analogInOutStageButton;
    juce::Label linkStatusLabel;

    std::array<juce::Label, kNumStrips> channelLabels;

    std::array<juce::Slider, kNumStrips> thresholdSliders;
    std::array<juce::Slider, kNumStrips> lowBoostSliders;
    std::array<juce::Slider, kNumStrips> midBoostSliders;
    std::array<juce::Slider, kNumStrips> highBoostSliders;
    std::array<juce::Slider, kNumStrips> lowMidXoverSliders;
    std::array<juce::Slider, kNumStrips> midHighXoverSliders;
    std::array<juce::Slider, kNumStrips> sourceNrSliders;
    std::array<juce::Slider, kNumStrips> outputSliders;

    std::array<juce::ComboBox, kNumStrips> eqModeBoxes;
    std::array<juce::ComboBox, kNumStrips> hpFilterBoxes;
    std::array<juce::ComboBox, kNumStrips> lpFilterBoxes;

    std::array<juce::Label, kNumStrips> thresholdLabels;
    std::array<juce::Label, kNumStrips> lowBoostLabels;
    std::array<juce::Label, kNumStrips> midBoostLabels;
    std::array<juce::Label, kNumStrips> highBoostLabels;
    std::array<juce::Label, kNumStrips> lowMidXoverLabels;
    std::array<juce::Label, kNumStrips> midHighXoverLabels;
    std::array<juce::Label, kNumStrips> sourceNrLabels;
    std::array<juce::Label, kNumStrips> outputLabels;
    std::array<juce::Label, kNumStrips> eqModeLabels;
    std::array<juce::Label, kNumStrips> hpFilterLabels;
    std::array<juce::Label, kNumStrips> lpFilterLabels;

    void timerCallback() override;

    void configureRotarySlider(juce::Slider& slider, const juce::String& suffix);
    void configureLabel(juce::Label& label, const juce::String& text);
    void configureSwitchBox(juce::ComboBox& box, const juce::StringArray& items);

    void layoutChannelStrip(int strip, juce::Rectangle<int> bounds, bool includeBrandSection);
    bool isStereoLinked() const;
    void setRightChannelEnabled(bool enabled);
    void updateLinkVisualState(bool linked);
    void mirrorLinkedParameters(bool forceSync);
    void setParameterFromOther(const juce::String& sourceId,
                               const juce::String& targetId,
                               bool forceSync);
    void drawLed(juce::Graphics& g,
                 juce::Rectangle<int> bounds,
                 bool on,
                 juce::Colour onColour) const;

    juce::Rectangle<int> brandSectionBounds;

    std::array<juce::Rectangle<int>, kNumStrips> stripPanelBounds;
    std::array<juce::Rectangle<int>, kNumStrips> thresholdSectionBounds;
    std::array<juce::Rectangle<int>, kNumStrips> eqSectionBounds;
    std::array<juce::Rectangle<int>, kNumStrips> sourceSectionBounds;
    std::array<juce::Rectangle<int>, kNumStrips> outputSectionBounds;

    std::array<int, 4> channelASectionWidths { 0, 0, 0, 0 };

    std::array<std::array<juce::Rectangle<int>, kThresholdLedCount>, kNumStrips> thresholdActivityLedBounds;
    std::array<juce::Rectangle<int>, kNumStrips> inputClipLedBounds;
    std::array<juce::Rectangle<int>, kNumStrips> sourceActiveLedBounds;
    std::array<juce::Rectangle<int>, kNumStrips> outputClipLedBounds;

    std::array<float, kNumStrips> inputMeterDbDisplay { -100.0f, -100.0f };
    std::array<float, kNumStrips> outputMeterDbDisplay { -100.0f, -100.0f };
    std::array<float, kNumStrips> sourceActivityPctDisplay { 0.0f, 0.0f };

    bool wasLinkedLastFrame = false;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> thresholdAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> lowBoostAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> midBoostAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> highBoostAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> lowMidXoverAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> midHighXoverAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> sourceNrAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumStrips> outputAttachments;

    std::array<std::unique_ptr<ComboAttachment>, kNumStrips> eqModeAttachments;
    std::array<std::unique_ptr<ComboAttachment>, kNumStrips> hpFilterAttachments;
    std::array<std::unique_ptr<ComboAttachment>, kNumStrips> lpFilterAttachments;

    std::unique_ptr<ButtonAttachment> stereoLinkAttachment;
    std::unique_ptr<ButtonAttachment> analogModeAttachment;
    std::unique_ptr<ButtonAttachment> analogInOutStageAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dolby740AudioProcessorEditor)
};
