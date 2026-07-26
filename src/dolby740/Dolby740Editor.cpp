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
}

Dolby740AudioProcessorEditor::Dolby740AudioProcessorEditor(Dolby740AudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      audioProcessor(processor)
{
    titleLabel.setText("Dolby 740", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 214, 173));
    addAndMakeVisible(titleLabel);

    operatingLevelLabel.setText("Nominal operating level: +4 dBu", juce::dontSendNotification);
    operatingLevelLabel.setJustificationType(juce::Justification::centredLeft);
    operatingLevelLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    operatingLevelLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(189, 177, 154));
    addAndMakeVisible(operatingLevelLabel);

    configureLabel(stereoLinkLabel, "STEREO LINK");
    stereoLinkLabel.setJustificationType(juce::Justification::centredLeft);
    stereoLinkLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 200, 158));

    stereoLinkButton.setButtonText("LINK CHANNEL B TO A");
    stereoLinkButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(228, 208, 168));
    addAndMakeVisible(stereoLinkButton);

    configureLabel(analogModeLabel, "ANALOG MODE");
    analogModeLabel.setJustificationType(juce::Justification::centredLeft);
    analogModeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 200, 158));

    analogModeButton.setButtonText("ENABLE ANALOG VARIATION");
    analogModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(228, 208, 168));
    addAndMakeVisible(analogModeButton);

    configureLabel(analogInOutStageLabel, "ANALOG IN/OUT STAGE");
    analogInOutStageLabel.setJustificationType(juce::Justification::centredLeft);
    analogInOutStageLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 200, 158));

    analogInOutStageButton.setButtonText("TRANSFORMER IN/OUT COLOR");
    analogInOutStageButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(228, 208, 168));
    addAndMakeVisible(analogInOutStageButton);

    linkStatusLabel.setJustificationType(juce::Justification::centredLeft);
    linkStatusLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    addAndMakeVisible(linkStatusLabel);

    for (int strip = 0; strip < kNumStrips; ++strip)
    {
        const auto index = static_cast<size_t>(strip);

        configureLabel(channelLabels[index], strip == 0 ? "CHANNEL A" : "CHANNEL B");
        channelLabels[index].setJustificationType(juce::Justification::centred);
        channelLabels[index].setFont(juce::FontOptions(9.0f, juce::Font::bold));

        configureRotarySlider(thresholdSliders[index], " dB");
        configureRotarySlider(lowBoostSliders[index], " dB");
        configureRotarySlider(midBoostSliders[index], " dB");
        configureRotarySlider(highBoostSliders[index], " dB");
        configureRotarySlider(lowMidXoverSliders[index], " Hz");
        configureRotarySlider(midHighXoverSliders[index], " Hz");
        configureRotarySlider(sourceNrSliders[index], " dB");
        configureRotarySlider(outputSliders[index], " dB");

        configureLabel(thresholdLabels[index], "THRESHOLD");
        configureLabel(lowBoostLabels[index], "LOW BOOST");
        configureLabel(midBoostLabels[index], "MID BOOST");
        configureLabel(highBoostLabels[index], "HIGH BOOST");
        configureLabel(lowMidXoverLabels[index], "LOW-MID XOVER");
        configureLabel(midHighXoverLabels[index], "MID-HIGH XOVER");
        configureLabel(sourceNrLabels[index], "SOURCE NR");
        configureLabel(outputLabels[index], "OUTPUT");
        configureLabel(eqModeLabels[index], "EQ MODE");
        configureLabel(hpFilterLabels[index], "HIGHPASS");
        configureLabel(lpFilterLabels[index], "LOWPASS");

        // Section names are drawn in dedicated header bars.
        thresholdLabels[index].setVisible(false);
        sourceNrLabels[index].setVisible(false);
        outputLabels[index].setVisible(false);

        configureSwitchBox(eqModeBoxes[index], { "In", "Side Chain", "Out" });
        configureSwitchBox(hpFilterBoxes[index], { "100 Hz", "200 Hz" });
        configureSwitchBox(lpFilterBoxes[index], { "4 kHz", "8 kHz" });
    }

    auto& apvts = audioProcessor.getValueTreeState();
    for (int strip = 0; strip < kNumStrips; ++strip)
    {
        const auto index = static_cast<size_t>(strip);
        thresholdAttachments[index] = std::make_unique<SliderAttachment>(apvts, thresholdIds[index], thresholdSliders[index]);
        sourceNrAttachments[index] = std::make_unique<SliderAttachment>(apvts, sourceNrIds[index], sourceNrSliders[index]);
        lowBoostAttachments[index] = std::make_unique<SliderAttachment>(apvts, lowBoostIds[index], lowBoostSliders[index]);
        midBoostAttachments[index] = std::make_unique<SliderAttachment>(apvts, midBoostIds[index], midBoostSliders[index]);
        highBoostAttachments[index] = std::make_unique<SliderAttachment>(apvts, highBoostIds[index], highBoostSliders[index]);
        lowMidXoverAttachments[index] = std::make_unique<SliderAttachment>(apvts, lowMidXoverIds[index], lowMidXoverSliders[index]);
        midHighXoverAttachments[index] = std::make_unique<SliderAttachment>(apvts, midHighXoverIds[index], midHighXoverSliders[index]);
        outputAttachments[index] = std::make_unique<SliderAttachment>(apvts, outputGainIds[index], outputSliders[index]);

        eqModeAttachments[index] = std::make_unique<ComboAttachment>(apvts, eqModeIds[index], eqModeBoxes[index]);
        hpFilterAttachments[index] = std::make_unique<ComboAttachment>(apvts, hpFilterIds[index], hpFilterBoxes[index]);
        lpFilterAttachments[index] = std::make_unique<ComboAttachment>(apvts, lpFilterIds[index], lpFilterBoxes[index]);
    }

    stereoLinkAttachment = std::make_unique<ButtonAttachment>(apvts, stereoLinkId, stereoLinkButton);
    analogModeAttachment = std::make_unique<ButtonAttachment>(apvts, analogModeId, analogModeButton);
    analogInOutStageAttachment = std::make_unique<ButtonAttachment>(apvts, analogInOutStageId, analogInOutStageButton);

    stereoLinkButton.onClick = [this]
    {
        const auto linked = stereoLinkButton.getToggleState();
        updateLinkVisualState(linked);
        setRightChannelEnabled(!linked);
        if (linked)
            mirrorLinkedParameters(true);
    };

    setSize(1540, 290);

    const auto linked = isStereoLinked();
    updateLinkVisualState(linked);
    setRightChannelEnabled(!linked);
    wasLinkedLastFrame = linked;

    startTimerHz(30);
}

Dolby740AudioProcessorEditor::~Dolby740AudioProcessorEditor() = default;

void Dolby740AudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    juce::ColourGradient bg(juce::Colour::fromRGB(17, 14, 10),
                            area.getTopLeft(),
                            juce::Colour::fromRGB(8, 7, 6),
                            area.getBottomRight(),
                            false);
    bg.addColour(0.45, juce::Colour::fromRGB(28, 22, 16));
    g.setGradientFill(bg);
    g.fillRect(area);

    auto frame = getLocalBounds().reduced(8).toFloat();
    g.setColour(juce::Colour::fromRGBA(24, 20, 16, 236));
    g.fillRoundedRectangle(frame, 14.0f);
    g.setColour(juce::Colour::fromRGBA(225, 194, 141, 76));
    g.drawRoundedRectangle(frame, 14.0f, 1.2f);

    if (!brandSectionBounds.isEmpty())
    {
        auto brand = brandSectionBounds.toFloat();
        juce::ColourGradient brandGrad(juce::Colour::fromRGB(45, 36, 26),
                                       brand.getTopLeft(),
                                       juce::Colour::fromRGB(24, 20, 15),
                                       brand.getBottomRight(),
                                       false);
        g.setGradientFill(brandGrad);
        g.fillRoundedRectangle(brand, 10.0f);
        g.setColour(juce::Colour::fromRGBA(233, 199, 141, 88));
        g.drawRoundedRectangle(brand, 10.0f, 1.0f);
    }

    auto drawSection = [&g](juce::Rectangle<int> section)
    {
        if (section.isEmpty())
            return;

        auto box = section.toFloat();
        g.setColour(juce::Colour::fromRGBA(19, 15, 11, 165));
        g.fillRoundedRectangle(box, 8.0f);
        g.setColour(juce::Colour::fromRGBA(196, 167, 122, 120));
        g.drawRoundedRectangle(box, 8.0f, 0.9f);
    };

    auto drawStripHeaderBar = [&g](juce::Rectangle<int> threshold,
                                   juce::Rectangle<int> eq,
                                   juce::Rectangle<int> source,
                                   juce::Rectangle<int> output)
    {
        if (threshold.isEmpty() || eq.isEmpty() || source.isEmpty() || output.isEmpty())
            return;

        auto merged = threshold.getUnion(eq).getUnion(source).getUnion(output);
        auto titleArea = juce::Rectangle<int>(merged.getX() + 2,
                                              threshold.getY() - 17,
                                              juce::jmax(1, merged.getWidth() - 4),
                                              14);

        g.setColour(juce::Colour::fromRGB(84, 67, 42));
        g.fillRoundedRectangle(titleArea.toFloat(), 2.0f);
        g.setColour(juce::Colour::fromRGBA(204, 176, 132, 130));
        g.drawRoundedRectangle(titleArea.toFloat(), 2.0f, 0.8f);

        g.setColour(juce::Colour::fromRGBA(204, 176, 132, 120));
        g.drawLine(static_cast<float>(eq.getX()),
                   static_cast<float>(titleArea.getY() + 1),
                   static_cast<float>(eq.getX()),
                   static_cast<float>(titleArea.getBottom() - 1),
                   0.8f);
        g.drawLine(static_cast<float>(source.getX()),
                   static_cast<float>(titleArea.getY() + 1),
                   static_cast<float>(source.getX()),
                   static_cast<float>(titleArea.getBottom() - 1),
                   0.8f);
        g.drawLine(static_cast<float>(output.getX()),
                   static_cast<float>(titleArea.getY() + 1),
                   static_cast<float>(output.getX()),
                   static_cast<float>(titleArea.getBottom() - 1),
                   0.8f);

        g.setColour(juce::Colour::fromRGB(205, 182, 141));
        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));

        g.drawFittedText("THRESHOLD",
                         juce::Rectangle<int>(threshold.getX(), titleArea.getY(), threshold.getWidth(), titleArea.getHeight()),
                         juce::Justification::centred,
                         1);
        g.drawFittedText("EQUALIZATION",
                         juce::Rectangle<int>(eq.getX(), titleArea.getY(), eq.getWidth(), titleArea.getHeight()),
                         juce::Justification::centred,
                         1);
        g.drawFittedText("SOURCE NR",
                         juce::Rectangle<int>(source.getX(), titleArea.getY(), source.getWidth(), titleArea.getHeight()),
                         juce::Justification::centred,
                         1);
        g.drawFittedText("OUTPUT",
                         juce::Rectangle<int>(output.getX(), titleArea.getY(), output.getWidth(), titleArea.getHeight()),
                         juce::Justification::centred,
                         1);
    };

    for (int strip = 0; strip < kNumStrips; ++strip)
    {
        const auto index = static_cast<size_t>(strip);
        auto panel = stripPanelBounds[index].toFloat();
        if (panel.isEmpty())
            continue;

        juce::ColourGradient panelGrad(strip == 0 ? juce::Colour::fromRGB(42, 33, 23) : juce::Colour::fromRGB(37, 30, 22),
                                       panel.getTopLeft(),
                                       strip == 0 ? juce::Colour::fromRGB(22, 17, 12) : juce::Colour::fromRGB(20, 16, 12),
                                       panel.getBottomRight(),
                                       false);
        g.setGradientFill(panelGrad);
        g.fillRoundedRectangle(panel, 10.0f);
        g.setColour(juce::Colour::fromRGBA(218, 185, 130, 70));
        g.drawRoundedRectangle(panel, 10.0f, 1.0f);

        drawSection(thresholdSectionBounds[index]);
        drawSection(eqSectionBounds[index]);
        drawSection(sourceSectionBounds[index]);
        drawSection(outputSectionBounds[index]);

        drawStripHeaderBar(thresholdSectionBounds[index],
                   eqSectionBounds[index],
                   sourceSectionBounds[index],
                   outputSectionBounds[index]);

        const auto litCount = juce::jlimit(0,
                           kThresholdLedCount,
                                           static_cast<int>(std::floor(sourceActivityPctDisplay[index] / 20.0f)));

        for (int led = 0; led < kThresholdLedCount; ++led)
            drawLed(g,
                    thresholdActivityLedBounds[index][static_cast<size_t>(led)],
                    led < litCount,
                    juce::Colour::fromRGB(240, 187, 88));

        const auto inputClip = inputMeterDbDisplay[index] >= -0.4f;
        const auto nrEngaged = sourceNrSliders[index].getValue() > 0.05;
        const auto sourceActive = nrEngaged && sourceActivityPctDisplay[index] > 4.0f;
        const auto outputClip = outputMeterDbDisplay[index] >= -0.4f;

        drawLed(g, inputClipLedBounds[index], inputClip, juce::Colour::fromRGB(224, 72, 58));
        drawLed(g, sourceActiveLedBounds[index], sourceActive, juce::Colour::fromRGB(108, 227, 151));
        drawLed(g, outputClipLedBounds[index], outputClip, juce::Colour::fromRGB(224, 72, 58));

        g.setColour(juce::Colour::fromRGB(208, 185, 146));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));

        auto ledRowBounds = thresholdActivityLedBounds[index][0];
        ledRowBounds = ledRowBounds.getUnion(thresholdActivityLedBounds[index][kThresholdLedCount - 1]);

        auto procLabelBounds = ledRowBounds
            .withY(ledRowBounds.getBottom() + 1)
            .withHeight(10)
            .expanded(8, 0);
        g.drawText("PROC", procLabelBounds, juce::Justification::centred, false);

        const int separatorY = inputClipLedBounds[index].getY() - 4;
        g.setColour(juce::Colour::fromRGBA(192, 164, 118, 120));
        g.drawLine(static_cast<float>(thresholdSectionBounds[index].getX() + 8),
                   static_cast<float>(separatorY),
                   static_cast<float>(thresholdSectionBounds[index].getRight() - 8),
                   static_cast<float>(separatorY),
                   1.0f);

        g.setColour(juce::Colour::fromRGB(208, 185, 146));
        g.drawText("IN CLIP",
                   inputClipLedBounds[index].translated(-52, -1).withWidth(46).withHeight(10),
                   juce::Justification::centredRight,
                   false);

        g.drawText("NR ACTIVE",
                   sourceActiveLedBounds[index].translated(-66, -1).withWidth(60).withHeight(10),
                   juce::Justification::centredRight,
                   false);

        g.drawText("OUT CLIP",
               outputClipLedBounds[index].translated(-58, -1).withWidth(52).withHeight(10),
                   juce::Justification::centredRight,
                   false);
    }
}

void Dolby740AudioProcessorEditor::resized()
{
    brandSectionBounds = {};

    auto area = getLocalBounds().reduced(16);

    const int brandGap = 8;
    const int stripGap = 8;
    const int brandWidth = juce::jlimit(175, 230, area.getWidth() / 7);

    brandSectionBounds = area.removeFromLeft(brandWidth);
    area.removeFromLeft(brandGap);

    auto brand = brandSectionBounds.reduced(10, 10);
    titleLabel.setBounds(brand.removeFromTop(40));
    operatingLevelLabel.setBounds(brand.removeFromTop(16));
    brand.removeFromTop(8);
    stereoLinkLabel.setBounds(brand.removeFromTop(12));
    stereoLinkButton.setBounds(brand.removeFromTop(20));
    brand.removeFromTop(3);
    analogModeLabel.setBounds(brand.removeFromTop(12));
    analogModeButton.setBounds(brand.removeFromTop(20));
    brand.removeFromTop(3);
    analogInOutStageLabel.setBounds(brand.removeFromTop(12));
    analogInOutStageButton.setBounds(brand.removeFromTop(20));
    brand.removeFromTop(4);
    linkStatusLabel.setBounds(brand.removeFromTop(18));

    const int stripWidth = (area.getWidth() - stripGap) / 2;
    auto stripA = area.removeFromLeft(stripWidth);
    area.removeFromLeft(stripGap);
    auto stripB = area;

    layoutChannelStrip(0, stripA, false);
    layoutChannelStrip(1, stripB, false);
}

void Dolby740AudioProcessorEditor::timerCallback()
{
    const auto linked = isStereoLinked();
    if (linked)
    {
        mirrorLinkedParameters(!wasLinkedLastFrame);
        setRightChannelEnabled(false);
    }
    else
    {
        setRightChannelEnabled(true);
    }

    updateLinkVisualState(linked);
    wasLinkedLastFrame = linked;

    for (int strip = 0; strip < kNumStrips; ++strip)
    {
        const auto index = static_cast<size_t>(strip);

        const auto inDb = audioProcessor.getInputMeterDb(strip);
        const auto outDb = audioProcessor.getOutputMeterDb(strip);
        const auto activity = audioProcessor.getDetailActivityPct(strip);

        inputMeterDbDisplay[index] += (inDb - inputMeterDbDisplay[index]) * (inDb > inputMeterDbDisplay[index] ? 0.35f : 0.10f);
        outputMeterDbDisplay[index] += (outDb - outputMeterDbDisplay[index]) * (outDb > outputMeterDbDisplay[index] ? 0.35f : 0.10f);
        sourceActivityPctDisplay[index] += (activity - sourceActivityPctDisplay[index]) * 0.22f;
    }

    repaint();
}

void Dolby740AudioProcessorEditor::configureRotarySlider(juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.18f,
                               juce::MathConstants<float>::pi * 2.82f,
                               true);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 14);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(208, 166, 102));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(72, 57, 40));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(236, 222, 187));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(224, 211, 185));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGBA(11, 9, 8, 220));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGBA(198, 164, 114, 90));
    addAndMakeVisible(slider);
}

void Dolby740AudioProcessorEditor::configureLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(205, 182, 146));
    addAndMakeVisible(label);
}

void Dolby740AudioProcessorEditor::configureSwitchBox(juce::ComboBox& box, const juce::StringArray& items)
{
    for (int i = 0; i < items.size(); ++i)
        box.addItem(items[i], i + 1);

    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(15, 13, 11, 220));
    box.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(229, 211, 179));
    box.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(196, 167, 122, 96));
    box.setColour(juce::ComboBox::arrowColourId, juce::Colour::fromRGB(228, 201, 152));
    addAndMakeVisible(box);
}

void Dolby740AudioProcessorEditor::layoutChannelStrip(int strip,
                                                       juce::Rectangle<int> bounds,
                                                       bool includeBrandSection)
{
    juce::ignoreUnused(includeBrandSection);

    const auto index = static_cast<size_t>(strip);
    stripPanelBounds[index] = bounds;

    auto area = bounds.reduced(8);
    area.removeFromTop(20);

    const int sectionGap = 6;

    const int contentWidth = juce::jmax(1, area.getWidth() - (sectionGap * 3));

    int thresholdWidth = 0;
    int eqWidth = 0;
    int sourceWidth = 0;
    int outputWidth = 0;

    auto deriveWidthsFrom = [&](int width)
    {
        thresholdWidth = juce::roundToInt(static_cast<float>(width) * 0.16f);
        eqWidth = juce::roundToInt(static_cast<float>(width) * 0.44f);
        sourceWidth = juce::roundToInt(static_cast<float>(width) * 0.22f);
        outputWidth = juce::jmax(1, width - thresholdWidth - eqWidth - sourceWidth);
    };

    deriveWidthsFrom(contentWidth);

    if (strip == 0)
        channelASectionWidths = { thresholdWidth, eqWidth, sourceWidth, outputWidth };

    auto thresholdBounds = area.removeFromLeft(thresholdWidth);
    area.removeFromLeft(sectionGap);
    auto eqBounds = area.removeFromLeft(eqWidth);
    area.removeFromLeft(sectionGap);
    auto sourceBounds = area.removeFromLeft(sourceWidth);
    area.removeFromLeft(sectionGap);
    auto outputBounds = area.removeFromLeft(outputWidth);

    thresholdSectionBounds[index] = thresholdBounds;
    eqSectionBounds[index] = eqBounds;
    sourceSectionBounds[index] = sourceBounds;
    outputSectionBounds[index] = outputBounds;

    {
        auto section = thresholdBounds.reduced(6);
        thresholdLabels[index].setBounds({});

        const int ledWidth = 14;
        const int ledHeight = 8;
        const int ledGap = 4;

        const int ledRowWidth = (kThresholdLedCount * ledWidth) + ((kThresholdLedCount - 1) * ledGap);
        const int ledX = section.getCentreX() - (ledRowWidth / 2);
        const int ledY = section.getY() + 2;

        for (int led = 0; led < kThresholdLedCount; ++led)
        {
            thresholdActivityLedBounds[index][static_cast<size_t>(led)] =
                juce::Rectangle<int>(ledX + led * (ledWidth + ledGap), ledY, ledWidth, ledHeight);
        }

        const int clipY = ledY + ledHeight + 16;
        inputClipLedBounds[index] = juce::Rectangle<int>(
            section.getCentreX() + 16,
            clipY,
            ledWidth,
            ledHeight);

        const int knobTop = clipY + ledHeight + 8;
        const int knobSize = 76;
        auto knobBounds = juce::Rectangle<int>(
            section.getCentreX() - (knobSize / 2),
            knobTop,
            knobSize,
            knobSize);

        thresholdSliders[index].setBounds(knobBounds);

        const int channelLabelTop = juce::jmin(section.getBottom() - 12, knobBounds.getBottom() + 3);
        channelLabels[index].setBounds(section.withY(channelLabelTop).withHeight(12));
    }

    {
        auto section = eqBounds.reduced(6);

        const int eqUsableHeight = section.getHeight();
        const int modeBlockHeight = juce::jmax(26, juce::roundToInt(eqUsableHeight * 0.18f));
        const int knobBlockHeight = juce::jmax(40, eqUsableHeight - modeBlockHeight - 4);

        auto knobBlock = section.removeFromTop(knobBlockHeight);
        auto topRow = knobBlock.removeFromTop(juce::jmax(28, juce::roundToInt(knobBlockHeight * 0.58f)));
        constexpr int topGap = 8;
        const int topKnobWidth = (topRow.getWidth() - 2 * topGap) / 3;

        std::array<juce::Rectangle<int>, 3> topBlocks;
        for (int i = 0; i < 3; ++i)
        {
            auto block = topRow.removeFromLeft(topKnobWidth);
            topBlocks[static_cast<size_t>(i)] = block;

            if (i == 0)
            {
                lowBoostLabels[index].setBounds(block.removeFromTop(12));
                lowBoostSliders[index].setBounds(block);
            }
            else if (i == 1)
            {
                midBoostLabels[index].setBounds(block.removeFromTop(12));
                midBoostSliders[index].setBounds(block);
            }
            else
            {
                highBoostLabels[index].setBounds(block.removeFromTop(12));
                highBoostSliders[index].setBounds(block);
            }

            if (i < 2)
                topRow.removeFromLeft(topGap);
        }

        auto xoverRow = knobBlock;
        const int xoverWidth = juce::jmin(88, topKnobWidth);

        const int lowMidCenter = (topBlocks[0].getCentreX() + topBlocks[1].getCentreX()) / 2;
        const int midHighCenter = (topBlocks[1].getCentreX() + topBlocks[2].getCentreX()) / 2;

        auto lowMidBlock = juce::Rectangle<int>(lowMidCenter - xoverWidth / 2,
                                                xoverRow.getY(),
                                                xoverWidth,
                                                xoverRow.getHeight());
        auto midHighBlock = juce::Rectangle<int>(midHighCenter - xoverWidth / 2,
                                                 xoverRow.getY(),
                                                 xoverWidth,
                                                 xoverRow.getHeight());

        lowMidXoverLabels[index].setBounds(lowMidBlock.removeFromTop(12));
        lowMidXoverSliders[index].setBounds(lowMidBlock);

        midHighXoverLabels[index].setBounds(midHighBlock.removeFromTop(12));
        midHighXoverSliders[index].setBounds(midHighBlock);

        section.removeFromTop(4);
        eqModeLabels[index].setBounds(section.removeFromTop(10));
        eqModeBoxes[index].setBounds(section.removeFromTop(20));
    }

    {
        auto section = sourceBounds.reduced(6);
        sourceNrLabels[index].setBounds({});

        auto top = section.removeFromTop(96);

        const int sourceKnobSize = 76;
        auto sourceKnob = juce::Rectangle<int>(
            top.getCentreX() - (sourceKnobSize / 2),
            top.getY() + 16,
            sourceKnobSize,
            sourceKnobSize);
        sourceNrSliders[index].setBounds(sourceKnob);

        sourceActiveLedBounds[index] = juce::Rectangle<int>(
            sourceKnob.getCentreX() + 20,
            sourceKnob.getY() - 10,
            16,
            8);

        section.removeFromTop(2);

        hpFilterLabels[index].setBounds(section.removeFromTop(10));
        hpFilterBoxes[index].setBounds(section.removeFromTop(20));

        section.removeFromTop(3);

        lpFilterLabels[index].setBounds(section.removeFromTop(10));
        lpFilterBoxes[index].setBounds(section.removeFromTop(20));
    }

    {
        auto section = outputBounds.reduced(6);
        outputLabels[index].setBounds({});

        auto knobArea = section.removeFromTop(84);
        const int outputKnobWidth = 74;
        const int outputClipY = sourceActiveLedBounds[index].getY();
        const int outputKnobTop = juce::jmax(knobArea.getY(), outputClipY + 14);
        auto outputKnob = knobArea.withWidth(outputKnobWidth)
                              .withX(knobArea.getCentreX() - outputKnobWidth / 2)
                              .withY(outputKnobTop);
        outputSliders[index].setBounds(outputKnob);

        outputClipLedBounds[index] = juce::Rectangle<int>(
            outputKnob.getCentreX() + 20,
            outputClipY,
            16,
            8);
    }
}

bool Dolby740AudioProcessorEditor::isStereoLinked() const
{
    if (const auto* value = audioProcessor.getValueTreeState().getRawParameterValue(stereoLinkId))
        return value->load() > 0.5f;

    return false;
}

void Dolby740AudioProcessorEditor::setRightChannelEnabled(bool enabled)
{
    thresholdSliders[1].setEnabled(enabled);
    lowBoostSliders[1].setEnabled(enabled);
    midBoostSliders[1].setEnabled(enabled);
    highBoostSliders[1].setEnabled(enabled);
    lowMidXoverSliders[1].setEnabled(enabled);
    midHighXoverSliders[1].setEnabled(enabled);
    sourceNrSliders[1].setEnabled(enabled);
    outputSliders[1].setEnabled(enabled);

    eqModeBoxes[1].setEnabled(enabled);
    hpFilterBoxes[1].setEnabled(enabled);
    lpFilterBoxes[1].setEnabled(enabled);

    const auto alpha = enabled ? 1.0f : 0.55f;

    channelLabels[1].setAlpha(alpha);
    thresholdLabels[1].setAlpha(alpha);
    lowBoostLabels[1].setAlpha(alpha);
    midBoostLabels[1].setAlpha(alpha);
    highBoostLabels[1].setAlpha(alpha);
    lowMidXoverLabels[1].setAlpha(alpha);
    midHighXoverLabels[1].setAlpha(alpha);
    sourceNrLabels[1].setAlpha(alpha);
    outputLabels[1].setAlpha(alpha);
    eqModeLabels[1].setAlpha(alpha);
    hpFilterLabels[1].setAlpha(alpha);
    lpFilterLabels[1].setAlpha(alpha);

    eqModeBoxes[1].setAlpha(alpha);
    hpFilterBoxes[1].setAlpha(alpha);
    lpFilterBoxes[1].setAlpha(alpha);
}

void Dolby740AudioProcessorEditor::updateLinkVisualState(bool linked)
{
    linkStatusLabel.setText(linked ? "Linked: channel B follows channel A"
                                   : "Unlinked: channels operate independently",
                            juce::dontSendNotification);

    linkStatusLabel.setColour(juce::Label::textColourId,
                              linked ? juce::Colour::fromRGB(235, 206, 150)
                                     : juce::Colour::fromRGB(178, 168, 147));

    stereoLinkButton.setButtonText(linked ? "CHANNELS LINKED" : "LINK CHANNEL B TO A");
    stereoLinkButton.setColour(juce::ToggleButton::textColourId,
                               linked ? juce::Colour::fromRGB(238, 213, 156)
                                      : juce::Colour::fromRGB(204, 186, 154));

    juce::ignoreUnused(linked);
    channelLabels[0].setText("CHANNEL A", juce::dontSendNotification);
    channelLabels[1].setText("CHANNEL B", juce::dontSendNotification);
}

void Dolby740AudioProcessorEditor::mirrorLinkedParameters(bool forceSync)
{
    std::array<std::pair<juce::String, juce::String>, 11> pairs {
        std::make_pair(juce::String(thresholdIds[0]), juce::String(thresholdIds[1])),
        std::make_pair(juce::String(sourceNrIds[0]), juce::String(sourceNrIds[1])),
        std::make_pair(juce::String(lowBoostIds[0]), juce::String(lowBoostIds[1])),
        std::make_pair(juce::String(midBoostIds[0]), juce::String(midBoostIds[1])),
        std::make_pair(juce::String(highBoostIds[0]), juce::String(highBoostIds[1])),
        std::make_pair(juce::String(lowMidXoverIds[0]), juce::String(lowMidXoverIds[1])),
        std::make_pair(juce::String(midHighXoverIds[0]), juce::String(midHighXoverIds[1])),
        std::make_pair(juce::String(eqModeIds[0]), juce::String(eqModeIds[1])),
        std::make_pair(juce::String(hpFilterIds[0]), juce::String(hpFilterIds[1])),
        std::make_pair(juce::String(lpFilterIds[0]), juce::String(lpFilterIds[1])),
        std::make_pair(juce::String(outputGainIds[0]), juce::String(outputGainIds[1]))
    };

    for (const auto& pair : pairs)
        setParameterFromOther(pair.first, pair.second, forceSync);
}

void Dolby740AudioProcessorEditor::setParameterFromOther(const juce::String& sourceId,
                                                          const juce::String& targetId,
                                                          bool forceSync)
{
    auto& apvts = audioProcessor.getValueTreeState();
    auto* source = apvts.getParameter(sourceId);
    auto* target = apvts.getParameter(targetId);
    if (source == nullptr || target == nullptr)
        return;

    auto* sourceRanged = dynamic_cast<juce::RangedAudioParameter*>(source);
    auto* targetRanged = dynamic_cast<juce::RangedAudioParameter*>(target);
    if (sourceRanged == nullptr || targetRanged == nullptr)
        return;

    const auto sourceValue = source->getValue();
    const auto sourcePlain = sourceRanged->convertFrom0to1(sourceValue);
    const auto targetValue = targetRanged->convertTo0to1(sourcePlain);

    if (!forceSync && std::abs(target->getValue() - targetValue) < 1.0e-5f)
        return;

    target->beginChangeGesture();
    target->setValueNotifyingHost(targetValue);
    target->endChangeGesture();
}

void Dolby740AudioProcessorEditor::drawLed(juce::Graphics& g,
                                            juce::Rectangle<int> bounds,
                                            bool on,
                                            juce::Colour onColour) const
{
    g.setColour(on ? onColour : juce::Colour::fromRGB(41, 33, 25));
    g.fillRoundedRectangle(bounds.toFloat(), 2.4f);

    g.setColour(on ? onColour.brighter(0.45f) : juce::Colour::fromRGB(72, 58, 44));
    g.drawRoundedRectangle(bounds.toFloat(), 2.4f, 1.0f);
}
