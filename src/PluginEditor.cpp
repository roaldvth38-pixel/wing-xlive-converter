#include "PluginEditor.h"

JuiceAudioProcessorEditor::JuiceAudioProcessorEditor(JuiceAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      audioProcessor(processor)
{
    titleLabel.setText("MM1200 Tape", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    configureRotarySlider(inputSlider, " dB");
    configureRotarySlider(driveSlider, "x");
    configureRotarySlider(outputSlider, " dB");
    configureRotarySlider(bleedSlider, "");
    configureRotarySlider(bleedThresholdSlider, " dBFS");

    configureRotarySlider(biasSlider, "");
    configureRotarySlider(headBumpFreqSlider, " Hz");
    configureRotarySlider(headBumpAmtSlider, "x");
    configureRotarySlider(printThroughSlider, "");
    configureRotarySlider(azimuthSlider, "");

    addAndMakeVisible(inputSlider);
    addAndMakeVisible(driveSlider);
    addAndMakeVisible(outputSlider);
    addAndMakeVisible(bleedSlider);
    addAndMakeVisible(bleedThresholdSlider);
    addAndMakeVisible(biasSlider);
    addAndMakeVisible(headBumpFreqSlider);
    addAndMakeVisible(headBumpAmtSlider);
    addAndMakeVisible(printThroughSlider);
    addAndMakeVisible(azimuthSlider);

    configureControlLabel(inputNameLabel, "Input");
    configureControlLabel(driveNameLabel, "Drive");
    configureControlLabel(outputNameLabel, "Output");
    configureControlLabel(bleedNameLabel, "Bleed");
    configureControlLabel(bleedThresholdNameLabel, "Bleed Thr");
    configureControlLabel(biasNameLabel,          "Bias");
    configureControlLabel(headBumpFreqNameLabel,   "Bump Hz");
    configureControlLabel(headBumpAmtNameLabel,    "Bump Amt");
    configureControlLabel(printThroughNameLabel,   "Print Thru");
    configureControlLabel(azimuthNameLabel,        "Azimuth");

    addAndMakeVisible(inputNameLabel);
    addAndMakeVisible(driveNameLabel);
    addAndMakeVisible(outputNameLabel);
    addAndMakeVisible(bleedNameLabel);
    addAndMakeVisible(bleedThresholdNameLabel);
    addAndMakeVisible(biasNameLabel);
    addAndMakeVisible(headBumpFreqNameLabel);
    addAndMakeVisible(headBumpAmtNameLabel);
    addAndMakeVisible(printThroughNameLabel);
    addAndMakeVisible(azimuthNameLabel);

    trackLabel.setJustificationType(juce::Justification::centredLeft);
    trackLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(trackLabel);

    ipsLabel.setText("IPS", juce::dontSendNotification);
    ipsLabel.setJustificationType(juce::Justification::centredLeft);
    ipsLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(ipsLabel);

    calLabel.setText("Cal", juce::dontSendNotification);
    calLabel.setJustificationType(juce::Justification::centredLeft);
    calLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    addAndMakeVisible(calLabel);

    for (int track = 1; track <= 24; ++track)
    {
        trackSelector.addItem("Track " + juce::String(track), track);
    }

    ipsSelector.addItem("15 ips", 1);
    ipsSelector.addItem("30 ips", 2);
    calSelector.addItem("+3", 1);
    calSelector.addItem("+6", 2);
    calSelector.addItem("+9", 3);

    trackSelector.addListener(this);
    addAndMakeVisible(trackSelector);
    addAndMakeVisible(ipsSelector);
    addAndMakeVisible(calSelector);

    wowFlutterButton.setButtonText("Wow/Flutter");
    wowFlutterButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(245, 226, 196));
    addAndMakeVisible(wowFlutterButton);

    hissButton.setButtonText("Tape Hiss");
    hissButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(245, 226, 196));
    addAndMakeVisible(hissButton);

    auto& apvts = audioProcessor.getValueTreeState();
    inputAttachment = std::make_unique<SliderAttachment>(apvts, "input", inputSlider);
    driveAttachment = std::make_unique<SliderAttachment>(apvts, "drive", driveSlider);
    outputAttachment = std::make_unique<SliderAttachment>(apvts, "output", outputSlider);
    bleedAttachment = std::make_unique<SliderAttachment>(apvts, "bleed", bleedSlider);
    bleedThresholdAttachment = std::make_unique<SliderAttachment>(apvts, "bleedThreshold", bleedThresholdSlider);
    biasAttachment          = std::make_unique<SliderAttachment>(apvts, "bias",          biasSlider);
    headBumpFreqAttachment  = std::make_unique<SliderAttachment>(apvts, "headBumpFreq",  headBumpFreqSlider);
    headBumpAmtAttachment   = std::make_unique<SliderAttachment>(apvts, "headBumpAmt",   headBumpAmtSlider);
    printThroughAttachment  = std::make_unique<SliderAttachment>(apvts, "printThrough",  printThroughSlider);
    azimuthAttachment       = std::make_unique<SliderAttachment>(apvts, "azimuth",       azimuthSlider);
    trackAttachment = std::make_unique<ComboAttachment>(apvts, "track", trackSelector);
    ipsAttachment = std::make_unique<ComboAttachment>(apvts, "ips", ipsSelector);
    calAttachment = std::make_unique<ComboAttachment>(apvts, "cal", calSelector);
    wowFlutterAttachment = std::make_unique<ButtonAttachment>(apvts, "wowFlutterOn", wowFlutterButton);
    hissAttachment = std::make_unique<ButtonAttachment>(apvts, "hissOn", hissButton);

    updateTrackLabel();

    presetButton.setButtonText("Presets \xe2\x96\xbe");
    presetButton.setColour(juce::TextButton::buttonColourId,  juce::Colour::fromRGB(70, 52, 32));
    presetButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(245, 226, 196));
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(presetButton);

    setSize(620, 620);
}

JuiceAudioProcessorEditor::~JuiceAudioProcessorEditor()
{
    trackSelector.removeListener(this);
}

void JuiceAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient gradient(
        juce::Colour::fromRGB(50, 38, 24), bounds.getTopLeft(),
        juce::Colour::fromRGB(18, 14, 10), bounds.getBottomRight(), false);
    gradient.addColour(0.45, juce::Colour::fromRGB(84, 62, 40));
    g.setGradientFill(gradient);
    g.fillRect(bounds);

    g.setColour(juce::Colour::fromRGBA(224, 196, 148, 34));
    g.drawRoundedRectangle(getLocalBounds().reduced(10).toFloat(), 12.0f, 1.5f);
}

void JuiceAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    titleLabel.setBounds(area.removeFromTop(34));

    auto presetRow = area.removeFromTop(28);
    presetRow.removeFromTop(4);
    presetButton.setBounds(presetRow.removeFromLeft(130).withTrimmedBottom(2));

    auto controlArea = area.removeFromTop(408);
    const int knobSize = 105;
    const int gap = 12;

    auto placeRow = [&](std::initializer_list<std::pair<juce::Slider*, juce::Label*>> row)
    {
        auto knobRow  = controlArea.removeFromTop(170);
        auto labelRow = controlArea.removeFromTop(24);
        for (auto [slider, label] : row)
        {
            slider->setBounds(knobRow.removeFromLeft(knobSize));
            label->setBounds(labelRow.removeFromLeft(knobSize));
            knobRow.removeFromLeft(gap);
            labelRow.removeFromLeft(gap);
        }
    };

    placeRow({{ &inputSlider, &inputNameLabel },
              { &driveSlider, &driveNameLabel },
              { &outputSlider, &outputNameLabel },
              { &bleedSlider, &bleedNameLabel },
              { &bleedThresholdSlider, &bleedThresholdNameLabel }});

    placeRow({{ &biasSlider,         &biasNameLabel },
              { &headBumpFreqSlider, &headBumpFreqNameLabel },
              { &headBumpAmtSlider,  &headBumpAmtNameLabel },
              { &printThroughSlider, &printThroughNameLabel },
              { &azimuthSlider,      &azimuthNameLabel }});

    auto bottom = area.removeFromTop(64);
    const int boxWidth = 170;
    const int boxGap = 12;

    auto trackBox = bottom.removeFromLeft(boxWidth);
    trackLabel.setBounds(trackBox.removeFromLeft(52));
    trackSelector.setBounds(trackBox);

    bottom.removeFromLeft(boxGap);
    auto calBox = bottom.removeFromLeft(boxWidth);
    calLabel.setBounds(calBox.removeFromLeft(32));
    calSelector.setBounds(calBox);

    bottom.removeFromLeft(boxGap);
    auto ipsBox = bottom.removeFromLeft(boxWidth);
    ipsLabel.setBounds(ipsBox.removeFromLeft(34));
    ipsSelector.setBounds(ipsBox);

    auto switches = area.removeFromTop(34);
    wowFlutterButton.setBounds(switches.removeFromLeft(170));
    switches.removeFromLeft(20);
    hissButton.setBounds(switches.removeFromLeft(140));
}

void JuiceAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &trackSelector)
    {
        updateTrackLabel();
    }
}

void JuiceAudioProcessorEditor::configureRotarySlider(juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(232, 182, 108));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(252, 220, 174));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(248, 240, 228));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGBA(20, 16, 12, 180));
}

void JuiceAudioProcessorEditor::configureControlLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(13.0f, juce::Font::plain));
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(245, 226, 196));
}

void JuiceAudioProcessorEditor::updateTrackLabel()
{
    trackLabel.setText("Track " + juce::String(trackSelector.getSelectedId()), juce::dontSendNotification);
}

void JuiceAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;

    menu.addSectionHeader("Built-in Presets");
    menu.addItem(1, "15 ips Color");
    menu.addItem(2, "30 ips Clean");
    menu.addSeparator();
    menu.addItem(10, "Save Preset...");
    menu.addItem(11, "Load Preset...");

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&presetButton),
        [this](int result)
        {
            if (result >= 1 && result <= 2)
                audioProcessor.setCurrentProgram(result - 1);
            else if (result == 10)
                savePreset();
            else if (result == 11)
                loadPreset();
        });
}

void JuiceAudioProcessorEditor::savePreset()
{
    currentFileChooser = std::make_shared<juce::FileChooser>(
        "Save MM1200 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("MM1200 Presets"),
        "*.mm1200");

    currentFileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file != juce::File{})
            {
                if (auto xml = audioProcessor.getValueTreeState().copyState().createXml())
                    file.replaceWithText(xml->toString());
            }
        });
}

void JuiceAudioProcessorEditor::loadPreset()
{
    currentFileChooser = std::make_shared<juce::FileChooser>(
        "Load MM1200 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("MM1200 Presets"),
        "*.mm1200");

    currentFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file != juce::File{} && file.existsAsFile())
            {
                if (auto xml = juce::XmlDocument::parse(file))
                {
                    if (xml->hasTagName(audioProcessor.getValueTreeState().state.getType()))
                        audioProcessor.getValueTreeState().replaceState(
                            juce::ValueTree::fromXml(*xml));
                }
            }
        });
}
