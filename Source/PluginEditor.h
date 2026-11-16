#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "HitView.h"
#include "LockstepLookAndFeel.h"

class LockstepEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LockstepEditor (LockstepProcessor&);
    ~LockstepEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SA> att;
    };

    void setupKnob (Knob&, const juce::String& id, const juce::String& name, juce::Colour);
    void setupToggle (juce::TextButton&, std::unique_ptr<BA>&, const juce::String& id, juce::Colour);
    void setupCombo (juce::ComboBox&, std::unique_ptr<CA>&, const juce::String& id);
    void timerCallback() override;
    void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);

    LockstepProcessor& proc;
    LockstepLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    HitView hitView;
    MatchMeter meter;

    juce::TextButton invertBtn { "INVERT" }, phaseBtn { "PHASE FILTER" }, duckBtn { "RECHAIN DUCK" }, autoBtn { "AUTO ALIGN" };
    std::unique_ptr<BA> invertAtt, phaseAtt, duckAtt;

    Knob delay, phaseFreq, phaseQ, duckDepth, duckAttack, duckRelease, duckXover, output;

    juce::ComboBox bandBox, analysisBox, monitorBox;
    std::unique_ptr<CA> bandAtt, analysisAtt, monitorAtt;
    juce::Label analysisLbl, monitorLbl;

    juce::Rectangle<int> alignPanel, duckPanel, outPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LockstepEditor)
};
