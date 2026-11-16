#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Analysis.h"

namespace ids
{
inline constexpr const char* invert     = "invert";
inline constexpr const char* delay      = "delay";
inline constexpr const char* phaseOn    = "phaseOn";
inline constexpr const char* phaseFreq  = "phaseFreq";
inline constexpr const char* phaseQ     = "phaseQ";
inline constexpr const char* duckOn     = "duckOn";
inline constexpr const char* duckDepth  = "duckDepth";
inline constexpr const char* duckAttack = "duckAttack";
inline constexpr const char* duckRelease= "duckRelease";
inline constexpr const char* duckBand   = "duckBand";    // Full / Lows
inline constexpr const char* duckXover  = "duckXover";
inline constexpr const char* analysisHz = "analysisHz";  // 80 / 120 / 200 Hz
inline constexpr const char* monitor    = "monitor";     // Bass / Bass + Kick / Kick
inline constexpr const char* output     = "output";
}

class LockstepProcessor : public juce::AudioProcessor
{
public:
    LockstepProcessor();
    ~LockstepProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Lockstep"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- GUI side ----
    bool fetchLatestHit (lockstep::Hit& dest, int& serial);   // false if nothing new
    std::vector<lockstep::Hit> copyRecentHits();
    float getMatch() const          { return matchSmoothed.load(); }
    float getDuckGrDb() const       { return duckGrMeter.load(); }
    bool  sidechainActive() const   { return scActive.load(); }
    lockstep::AlignResult runAutoAlign();                     // applies the result to the parameters

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateAnalysisFilters();
    void captureHit (int onsetIndex);

    double sr = 44100.0;

    // bass processing
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 4096 };
    juce::SmoothedValue<float> delaySm, outSm;
    lockstep::Allpass allpass[2];
    float apFreq = -1, apQ = -1;
    juce::dsp::LinkwitzRileyFilter<float> duckLow;    // band for "Lows" ducking

    // kick envelope / ducking
    float duckEnv = 0, kickPeak = 1e-4f, duckGain = 1.0f;

    // analysis
    juce::dsp::LinkwitzRileyFilter<float> lpKick, lpRaw, lpProc;
    int analysisChoice = -1;
    std::vector<float> ringKick, ringRaw, ringProc;
    int ringMask = 0, ringPos = 0;
    float trigEnv = 0, trigPeak = 1e-4f;
    bool armed = true;
    int pendingOnset = -1, pendingCount = 0, holdoff = 0;
    int W = 0, pre = 0, maxD = 0;

    juce::SpinLock hitLock;
    lockstep::Hit latest, scratch;
    std::vector<lockstep::Hit> recent;   // ring of last few hits (for auto-align)
    int recentPos = 0, hitSerial = 0;

    std::atomic<float> matchSmoothed { 50.0f }, duckGrMeter { 0.0f };
    std::atomic<bool> scActive { false };
    int scSilentSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LockstepProcessor)
};
