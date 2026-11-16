#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Overlays the low end of the kick (orange) and the bass (cyan) for the most
// recent kick hit. Green shading = they push together, red = they cancel.
// Bottom strip: match % for the last hits.
class HitView : public juce::Component, private juce::Timer
{
public:
    explicit HitView (LockstepProcessor&);
    ~HitView() override { stopTimer(); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    LockstepProcessor& proc;
    lockstep::Hit hit;
    int serial = -1;
    std::vector<float> history;
    float fade = 0.0f;
};

// Arc gauge with the smoothed match percentage, plus duck gain-reduction bar.
class MatchMeter : public juce::Component, private juce::Timer
{
public:
    explicit MatchMeter (LockstepProcessor& p) : proc (p) { startTimerHz (30); }
    ~MatchMeter() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void setResultText (const juce::String& s) { result = s; resultAge = 0; repaint(); }

private:
    void timerCallback() override;
    LockstepProcessor& proc;
    float shown = 50.0f, gr = 0.0f;
    juce::String result;
    int resultAge = 0;
};
