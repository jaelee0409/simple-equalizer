/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void LookAndFeel::drawRotarySlider (juce::Graphics& g,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    float sliderPosProportional,
                                    float rotaryStartAngle,
                                    float rotaryEndAngle,
                                    juce::Slider& slider)
{
    using namespace juce;

    auto bounds = Rectangle<float> (x, y, width, height);
    g.setColour (Colour (0xff6b8ae5));
    g.fillEllipse (bounds);
    g.setColour (Colour (0xff52527a));
    g.drawEllipse (bounds, 1.0f);

    if (auto* rswl = dynamic_cast<RotarySliderWithLabels*> (&slider))
    {
        auto center = bounds.getCentre();

        Path p;
        Rectangle<float> r;
        r.setLeft (center.getX() - 2);
        r.setRight (center.getX() + 2);
        r.setBottom (center.getY() - rswl->getTextBoxHeight() * 2);
        r.setTop (bounds.getY());

        p.addRoundedRectangle (r, 2.0f);

        jassert (rotaryStartAngle < rotaryEndAngle);

        auto sliderRadianAngle = jmap (sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);
        p.applyTransform (AffineTransform().rotated (sliderRadianAngle, center.getX(), center.getY()));
        g.setColour (Colours::black);
        g.fillPath(p);

        g.setFont (rswl->getTextBoxHeight());
        auto text = rswl->getDisplayString();
        auto textWidth = g.getCurrentFont().getStringWidth (text);
        r.setSize (textWidth + 4, rswl->getTextHeight() + 2);
        r.setCentre (bounds.getCentre());

        g.setColour (Colours::black);
        g.fillRect (r);
        g.setColour (Colours::white);
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

void RotarySliderWithLabels::paint (juce::Graphics& g)
{
    using namespace juce;

    auto startAngle = degreesToRadians (225.0f);
    auto endAngle = degreesToRadians (135.0f) + MathConstants<float>::twoPi;

    auto range = getRange();
    auto sliderBounds = getSlidersBounds();
    //g.setColour (Colours::red);
    //g.drawRect (getLocalBounds());
    //g.setColour (Colours::yellow);
    //g.drawRect (sliderBounds);

    getLookAndFeel().drawRotarySlider (g,
                                       sliderBounds.getX(),
                                       sliderBounds.getY(),
                                       sliderBounds.getWidth(),
                                       sliderBounds.getHeight(),
                                       jmap (getValue(), range.getStart(), range.getEnd(), 0.0, 1.0),
                                       startAngle,
                                       endAngle,
                                       *this);

    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5f;
    g.setColour (Colours::white);
    g.setFont (getTextHeight());

    auto numChoices = labels.size();
    for (int i = 0; i < numChoices; ++i)
    {
        auto pos = labels[i].pos;
        jassert (0.0f <= pos);
        jassert (pos <= 1.0f);
        auto angle = jmap (pos, 0.0f, 1.0f, startAngle, endAngle);

        auto c = center.getPointOnCircumference (radius + getTextHeight() * 0.75f, angle);
        Rectangle<float> r;
        auto str = labels[i].label;
        r.setSize (g.getCurrentFont().getStringWidth (str), getTextHeight());
        r.setCentre (c);
        r.setY (r.getY() + getTextHeight());
        g.drawFittedText (str, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

juce::Rectangle<int> RotarySliderWithLabels::getSlidersBounds() const
{
    auto bounds = getLocalBounds();
    auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());

    size -= getTextHeight() * 2;
    juce::Rectangle<int> r;
    r.setSize (size, size);
    r.setCentre (bounds.getCentreX(), 0);
    r.setY (10);

    return r;
}

juce::String RotarySliderWithLabels::getDisplayString() const
{
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (param))
        return choiceParam->getCurrentChoiceName();

    juce::String str;
    bool addK = false;
    if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
    {
        float val = getValue();
        if (val > 999.0f)
        {
            val /= 1000.0f;
            addK = true;
        }
        str = juce::String (val, (addK ? 2 : 0));
    }
    else
    {
        jassertfalse;
    }

    if (suffix.isNotEmpty())
    {
        str << " ";
        if (addK)
            str << "k";

        str << suffix;
    }

    return str;
}

//==============================================================================

ResponseCurveComponent::ResponseCurveComponent (SimpleEQAudioProcessor& p)
    : audioProcessor (p)
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
    {
        param->addListener (this);
    }
    updateChain();
    startTimerHz (60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
    {
        param->removeListener (this);
    }
}

void ResponseCurveComponent::parameterValueChanged (int parameterIndex, float newValue)
{
    parametersChanged.set (true);
}

void ResponseCurveComponent::timerCallback()
{
    if (parametersChanged.compareAndSetBool (false, true))
    {
        //update the monochain
        updateChain();

        //signal a repaint for a new response curve
        repaint();
    }
}

void ResponseCurveComponent::updateChain()
{
    auto chainSettings = getChainSettings (audioProcessor.apvts);
    auto peakCoefficients = makePeakFilter (chainSettings, audioProcessor.getSampleRate());
    updateCoefficients (monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    auto lowCutCoefficients = makeLowCutFilter (chainSettings, audioProcessor.getSampleRate());
    updateCutFilter (monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
    auto highCutCoefficients = makeHighCutFilter (chainSettings, audioProcessor.getSampleRate());
    updateCutFilter (monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
}

void ResponseCurveComponent::paint (juce::Graphics& g)
{
    using namespace juce;
    auto responseArea = getLocalBounds();
    auto width = responseArea.getWidth();

    auto& lowCut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highCut = monoChain.get<ChainPositions::HighCut>();

    auto sampleRate = audioProcessor.getSampleRate();

    std::vector<double> mags;
    mags.resize (width);
    for (int i = 0; i < width; ++i)
    {
        double mag = 1.0f;
        auto freq = mapToLog10 (double (i) / double (width), 20.0, 20000.0);

        if (! monoChain.isBypassed<ChainPositions::Peak>())
            mag *= peak.coefficients->getMagnitudeForFrequency (freq, sampleRate);
        
        if (! lowCut.isBypassed<0>())
            mag *= lowCut.get<0>().coefficients->getMagnitudeForFrequency (freq, sampleRate);
        if (! lowCut.isBypassed<1>())
            mag *= lowCut.get<1>().coefficients->getMagnitudeForFrequency (freq, sampleRate);
        if (! lowCut.isBypassed<2>())
            mag *= lowCut.get<2>().coefficients->getMagnitudeForFrequency (freq, sampleRate);
        if (! lowCut.isBypassed<3>())
            mag *= lowCut.get<3>().coefficients->getMagnitudeForFrequency (freq, sampleRate);

        if (! highCut.isBypassed<0>())
            mag *= highCut.get<0>().coefficients->getMagnitudeForFrequency (freq, sampleRate);
        if (! highCut.isBypassed<1>())
            mag *= highCut.get<1>().coefficients->getMagnitudeForFrequency (freq, sampleRate);
        if (! highCut.isBypassed<2>())
            mag *= highCut.get<2>().coefficients->getMagnitudeForFrequency (freq, sampleRate);
        if (! highCut.isBypassed<3>())
            mag *= highCut.get<3>().coefficients->getMagnitudeForFrequency (freq, sampleRate);

        mags[i] = Decibels::gainToDecibels(mag);
    }

    Path responseCurve;
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax] (double input)
    {
        return jmap (input, -24.0, 24.0, outputMin, outputMax);
    };
    responseCurve.startNewSubPath (responseArea.getX(), map (mags.front()));
    for (size_t i = 1; i < mags.size(); ++i)
    {
        responseCurve.lineTo (responseArea.getX() + i, map (mags[i]));
    }

    g.setColour (Colours::black);
    g.fillRect (responseArea);

    g.setColour (Colours::white);
    g.strokePath (responseCurve, PathStrokeType (2.0f));

    g.setColour (Colours::grey);
    g.drawRect (responseArea.toFloat(), 4.0f);
}

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      peakFreqSlider (*audioProcessor.apvts.getParameter ("Peak Freq"), "Hz"),
      peakGainSlider (*audioProcessor.apvts.getParameter ("Peak Gain"), "dB"),
      peakQualitySlider (*audioProcessor.apvts.getParameter ("Peak Quality"), ""),
      lowCutFreqSlider (*audioProcessor.apvts.getParameter ("LowCut Freq"), "Hz"),
      highCutFreqSlider (*audioProcessor.apvts.getParameter ("HighCut Freq"), "Hz"),
      lowCutSlopeSlider (*audioProcessor.apvts.getParameter ("LowCut Slope"), "dB/oct"),
      highCutSlopeSlider (*audioProcessor.apvts.getParameter ("HighCut Slope"), "dB/oct"),

      peakFreqSliderAttachment (audioProcessor.apvts, "Peak Freq", peakFreqSlider),
      peakGainSliderAttachment (audioProcessor.apvts, "Peak Gain", peakGainSlider),
      peakQualitySliderAttachment (audioProcessor.apvts, "Peak Quality", peakQualitySlider),
      lowCutFreqSliderAttachment (audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
      highCutFreqSliderAttachment (audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
      lowCutSlopeSliderAttachment (audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
      highCutSlopeSliderAttachment (audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider),
      responseCurveComponent (audioProcessor)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.

    lowCutFreqSlider.labels.add ({0.0f, "20 Hz"});
    lowCutFreqSlider.labels.add ({1.0f, "20 kHz"});
    lowCutSlopeSlider.labels.add ({0.0f, "12"});
    lowCutSlopeSlider.labels.add ({1.0f, "48"});
    peakFreqSlider.labels.add ({0.0f, "20 Hz"});
    peakFreqSlider.labels.add ({1.0f, "20 kHz"});
    peakGainSlider.labels.add ({0.0f, "-24 dB"});
    peakGainSlider.labels.add ({1.0f, "+24 dB"});
    peakQualitySlider.labels.add ({0.0f, "0.1"});
    peakQualitySlider.labels.add ({1.0f, "10.0"});
    highCutFreqSlider.labels.add ({0.0f, "20 Hz"});
    highCutFreqSlider.labels.add ({1.0f, "20 kHz"});
    highCutSlopeSlider.labels.add ({0.0f, "12"});
    highCutSlopeSlider.labels.add ({1.0f, "48"});


    for (auto* comp : getComps())
    {
        addAndMakeVisible (comp);
    }

    setSize (1080, 720);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    //g.fillAll (Colours::black);
}

void SimpleEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    auto bounds = getLocalBounds();
    float hRatio = 35.0f / 100.0f;
    auto responseArea = bounds.removeFromTop (bounds.getHeight() * hRatio);
    responseCurveComponent.setBounds (responseArea);

    bounds.removeFromTop (5);
    auto lowCutArea = bounds.removeFromLeft (bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight (bounds.getWidth() * 0.5);

    lowCutFreqSlider.setBounds (lowCutArea.removeFromTop (lowCutArea.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds (lowCutArea);
    highCutFreqSlider.setBounds (highCutArea.removeFromTop (highCutArea.getHeight() * 0.5));
    highCutSlopeSlider.setBounds (highCutArea);

    peakFreqSlider.setBounds (bounds.removeFromTop (bounds.getHeight() * 0.33));
    peakGainSlider.setBounds (bounds.removeFromTop (bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds (bounds);
}

std::vector<juce::Component*> SimpleEQAudioProcessorEditor::getComps()
{
    return
    {
        &peakFreqSlider,
        &peakGainSlider,
        &peakQualitySlider,
        &lowCutFreqSlider,
        &highCutFreqSlider,
        &lowCutSlopeSlider,
        &highCutSlopeSlider,
        &responseCurveComponent
    };
}