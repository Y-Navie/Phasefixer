#include "PluginEditor.h"

LockstepEditor::LockstepEditor (LockstepProcessor& p)
    : AudioProcessorEditor (p), proc (p), hitView (p), meter (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (hitView);
    addAndMakeVisible (meter);

    setupToggle (invertBtn, invertAtt, ids::invert,  colours::bass);
    setupToggle (phaseBtn,  phaseAtt,  ids::phaseOn, colours::bass);
    setupToggle (duckBtn,   duckAtt,   ids::duckOn,  colours::duck);
    invertBtn.setTooltip ("Flip the polarity of the bass");
    phaseBtn.setTooltip ("Allpass filter: rotates the bass phase around the chosen frequency without changing its tone");
    duckBtn.setTooltip ("Duck the bass every time the kick hits");

    autoBtn.setColour (juce::TextButton::buttonColourId, colours::good.withAlpha (0.18f));
    autoBtn.setColour (juce::TextButton::textColourOffId, colours::good);
    autoBtn.setTooltip ("Searches polarity, delay and phase filter for the best match over the last few kick hits");
    autoBtn.onClick = [this]
    {
        const auto r = proc.runAutoAlign();
        if (! r.found)
        {
            meter.setResultText ("Need a few kick + bass hits first. Press play.");
            return;
        }
        juce::String s = (r.invert ? "Inverted, " : "") + juce::String (r.delayMs, 2) + " ms";
        if (r.phaseOn) s << ", phase " << juce::String (r.phaseHz, 0) << " Hz";
        s << "\n" << juce::roundToInt (r.matchBefore) << "% -> " << juce::roundToInt (r.matchAfter) << "%";
        meter.setResultText (s);
    };
    addAndMakeVisible (autoBtn);

    setupKnob (delay,       ids::delay,       "DELAY",     colours::bass);
    setupKnob (phaseFreq,   ids::phaseFreq,   "FREQ",      colours::bass);
    setupKnob (phaseQ,      ids::phaseQ,      "Q",         colours::bass);
    setupKnob (duckDepth,   ids::duckDepth,   "DEPTH",     colours::duck);
    setupKnob (duckAttack,  ids::duckAttack,  "ATTACK",    colours::duck);
    setupKnob (duckRelease, ids::duckRelease, "RELEASE",   colours::duck);
    setupKnob (duckXover,   ids::duckXover,   "CROSSOVER", colours::duck);
    setupKnob (output,      ids::output,      "OUTPUT",    colours::text);

    setupCombo (bandBox,     bandAtt,     ids::duckBand);
    setupCombo (analysisBox, analysisAtt, ids::analysisHz);
    setupCombo (monitorBox,  monitorAtt,  ids::monitor);

    for (auto* l : { &analysisLbl, &monitorLbl })
    {
        l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, colours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*l);
    }
    analysisLbl.setText ("ANALYSE", juce::dontSendNotification);
    monitorLbl.setText ("LISTEN", juce::dontSendNotification);

    setResizable (true, true);
    setResizeLimits (860, 560, 1600, 1100);
    setSize (980, 620);
    startTimerHz (10);
}

LockstepEditor::~LockstepEditor() { stopTimer(); setLookAndFeel (nullptr); }

void LockstepEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name, juce::Colour col)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, col);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    auto* prm = proc.apvts.getParameter (id);
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (k.label);

    k.att = std::make_unique<SA> (proc.apvts, id, k.slider);
}

void LockstepEditor::setupToggle (juce::TextButton& b, std::unique_ptr<BA>& att, const juce::String& id, juce::Colour on)
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonOnColourId, on);
    addAndMakeVisible (b);
    att = std::make_unique<BA> (proc.apvts, id, b);
}

void LockstepEditor::setupCombo (juce::ComboBox& box, std::unique_ptr<CA>& att, const juce::String& id)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        box.addItemList (choice->choices, 1);
    addAndMakeVisible (box);
    att = std::make_unique<CA> (proc.apvts, id, box);
}

void LockstepEditor::timerCallback()
{
    // grey out controls that currently do nothing
    const bool ph = phaseBtn.getToggleState(), dk = duckBtn.getToggleState();
    phaseFreq.slider.setEnabled (ph);
    phaseQ.slider.setEnabled (ph);
    for (auto* k : { &duckDepth, &duckAttack, &duckRelease })
        k->slider.setEnabled (dk);
    duckXover.slider.setEnabled (dk && bandBox.getSelectedItemIndex() == 1);
    bandBox.setEnabled (dk);
}

void LockstepEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);

    auto header = getLocalBounds().removeFromTop (52).reduced (18, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (23.0f, juce::Font::bold));
    g.drawText ("LOCKSTEP", header, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("kick / bass phase alignment", header.withTrimmedLeft (128), juce::Justification::centredLeft);

    auto drawPanel = [&g] (juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (accent);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, r.reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft);
    };
    drawPanel (alignPanel, "ALIGN", colours::bass);
    drawPanel (duckPanel,  "RECHAIN", colours::duck);
    drawPanel (outPanel,   "OUT", colours::textDim);
}

void LockstepEditor::layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
{
    const int w = area.getWidth() / (int) knobs.size();
    for (auto* k : knobs)
    {
        auto cell = area.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell.withSizeKeepingCentre (std::min (cell.getWidth(), cell.getHeight() + 10), cell.getHeight()));
    }
}

void LockstepEditor::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (52).reduced (16, 12);
    monitorBox.setBounds (header.removeFromRight (120));
    monitorLbl.setBounds (header.removeFromRight (56));
    header.removeFromRight (14);
    analysisBox.setBounds (header.removeFromRight (100));
    analysisLbl.setBounds (header.removeFromRight (64));

    r.reduce (12, 0);
    auto bottom = r.removeFromBottom (std::max (190, getHeight() / 3)).withTrimmedBottom (12);
    r.removeFromBottom (10);

    meter.setBounds (r.removeFromRight (std::max (210, getWidth() / 5)));
    r.removeFromRight (10);
    hitView.setBounds (r);

    // bottom panels
    const int total = bottom.getWidth();
    alignPanel = bottom.removeFromLeft ((int) (total * 0.47f));
    bottom.removeFromLeft (10);
    outPanel = bottom.removeFromRight (110);
    bottom.removeFromRight (10);
    duckPanel = bottom;

    {
        auto a = alignPanel.reduced (12, 8);
        a.removeFromTop (18);
        auto buttons = a.removeFromTop (30);
        const int bw = buttons.getWidth() / 3;
        invertBtn.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
        phaseBtn.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
        autoBtn.setBounds (buttons.reduced (3, 0));
        a.removeFromTop (6);
        layoutKnobs (a, { &delay, &phaseFreq, &phaseQ });
    }
    {
        auto d = duckPanel.reduced (12, 8);
        d.removeFromTop (18);
        auto buttons = d.removeFromTop (30);
        const int bw = buttons.getWidth() / 2;
        duckBtn.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
        bandBox.setBounds (buttons.reduced (3, 0));
        d.removeFromTop (6);
        layoutKnobs (d, { &duckDepth, &duckAttack, &duckRelease, &duckXover });
    }
    {
        auto o = outPanel.reduced (8, 8);
        o.removeFromTop (54);
        layoutKnobs (o, { &output });
    }
}
