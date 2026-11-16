#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr int numRecentHits = 6;
const float analysisFreqs[] = { 80.0f, 120.0f, 200.0f };
}

LockstepProcessor::LockstepProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Bass",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Kick (sidechain)", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout LockstepProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    auto ms = [] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " ms"; };
    auto hz = [] (float v, int) { return String (v, 0) + " Hz"; };

    l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::invert, 1 }, "Invert Polarity", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::delay, 1 }, "Delay",
            NormalisableRange<float> (0.0f, (float) lockstep::maxDelayMs, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::phaseOn, 1 }, "Phase Filter", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::phaseFreq, 1 }, "Phase Freq",
            NormalisableRange<float> (20.0f, 500.0f, 0.1f, 0.4f), 100.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::phaseQ, 1 }, "Phase Q",
            NormalisableRange<float> (0.3f, 4.0f, 0.01f, 0.5f), 0.7f));

    l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::duckOn, 1 }, "Duck", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::duckDepth, 1 }, "Duck Depth",
            NormalisableRange<float> (0.0f, 36.0f, 0.1f), 12.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::duckAttack, 1 }, "Duck Attack",
            NormalisableRange<float> (0.1f, 50.0f, 0.01f, 0.4f), 1.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::duckRelease, 1 }, "Duck Release",
            NormalisableRange<float> (10.0f, 600.0f, 0.1f, 0.4f), 120.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::duckBand, 1 }, "Duck Band",
            StringArray { "Full band", "Lows only" }, 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::duckXover, 1 }, "Duck Crossover",
            NormalisableRange<float> (40.0f, 500.0f, 0.1f, 0.5f), 150.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (hz)));

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::analysisHz, 1 }, "Analysis Band",
            StringArray { "< 80 Hz", "< 120 Hz", "< 200 Hz" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::monitor, 1 }, "Monitor",
            StringArray { "Bass", "Bass + Kick", "Kick" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output",
            NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    return l;
}

bool LockstepProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (out != layouts.getMainInputChannelSet())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void LockstepProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int nCh = std::max (1, getMainBusNumOutputChannels());

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) std::max (1, samplesPerBlock), (juce::uint32) nCh };
    maxD = (int) std::ceil (lockstep::maxDelayMs * 0.001 * sr);
    delayLine.setMaximumDelayInSamples (maxD + 8);
    delayLine.prepare (spec);
    delayLine.reset();

    duckLow.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    duckLow.prepare (spec);

    juce::dsp::ProcessSpec mono { sr, (juce::uint32) std::max (1, samplesPerBlock), 1 };
    for (auto* f : { &lpKick, &lpRaw, &lpProc })
    {
        f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        f->prepare (mono);
    }
    analysisChoice = -1;
    updateAnalysisFilters();

    for (auto& a : allpass) a.reset();
    apFreq = apQ = -1.0f;

    delaySm.reset (sr, 0.05);
    delaySm.setCurrentAndTargetValue (apvts.getRawParameterValue (ids::delay)->load() * 0.001f * (float) sr);
    outSm.reset (sr, 0.05);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ids::output)->load()));

    W   = (int) std::lround (lockstep::windowMs * 0.001 * sr);
    pre = (int) std::lround (lockstep::preRollMs * 0.001 * sr);
    const int ringSize = juce::nextPowerOfTwo (pre + maxD + W + samplesPerBlock + 1024);
    ringKick.assign ((size_t) ringSize, 0.0f);
    ringRaw.assign ((size_t) ringSize, 0.0f);
    ringProc.assign ((size_t) ringSize, 0.0f);
    ringMask = ringSize - 1;
    ringPos = 0;

    auto sizeHit = [this] (lockstep::Hit& h)
    {
        h.fs = sr; h.pre = pre;
        h.kick.assign ((size_t) (pre + W), 0.0f);
        h.bass.assign ((size_t) (pre + W), 0.0f);
        h.rawBass.assign ((size_t) (pre + maxD + W), 0.0f);
    };
    {
        const juce::SpinLock::ScopedLockType lock (hitLock);
        sizeHit (latest);
        sizeHit (scratch);
        latest.kick.clear();   // marks "no hit yet"
        recent.clear();
        recentPos = 0;
    }

    duckEnv = 0; duckGain = 1.0f; kickPeak = 1e-4f;
    trigEnv = 0; trigPeak = 1e-4f; armed = true;
    pendingOnset = -1; pendingCount = 0; holdoff = 0;
}

void LockstepProcessor::updateAnalysisFilters()
{
    const int c = juce::jlimit (0, 2, (int) apvts.getRawParameterValue (ids::analysisHz)->load());
    if (c == analysisChoice) return;
    analysisChoice = c;
    for (auto* f : { &lpKick, &lpRaw, &lpProc })
    {
        f->setCutoffFrequency (analysisFreqs[c]);
        f->reset();
    }
}

void LockstepProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, true, 0);
    const int nCh = std::min (main.getNumChannels(), 2);
    const int n   = buffer.getNumSamples();
    if (nCh == 0 || n == 0) return;

    const float* sc[2] = { nullptr, nullptr };
    int nSc = 0;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto side = getBusBuffer (buffer, true, 1);
        nSc = std::min (side.getNumChannels(), 2);
        for (int c = 0; c < nSc; ++c) sc[c] = side.getReadPointer (c);
    }

    // ---- parameters ----
    updateAnalysisFilters();
    const bool  invert   = apvts.getRawParameterValue (ids::invert)->load() > 0.5f;
    const bool  phaseOn  = apvts.getRawParameterValue (ids::phaseOn)->load() > 0.5f;
    const float pf       = apvts.getRawParameterValue (ids::phaseFreq)->load();
    const float pq       = apvts.getRawParameterValue (ids::phaseQ)->load();
    if (std::abs (pf - apFreq) > 1.0e-4f || std::abs (pq - apQ) > 1.0e-5f)
    {
        apFreq = pf; apQ = pq;
        for (auto& a : allpass) a.set (sr, std::min ((double) pf, sr * 0.45), pq);
    }
    delaySm.setTargetValue (apvts.getRawParameterValue (ids::delay)->load() * 0.001f * (float) sr);
    outSm.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ids::output)->load()));

    const bool  duckOn  = apvts.getRawParameterValue (ids::duckOn)->load() > 0.5f;
    const float depth   = apvts.getRawParameterValue (ids::duckDepth)->load();
    const float atk     = std::exp (-1.0f / (0.001f * apvts.getRawParameterValue (ids::duckAttack)->load() * (float) sr));
    const float rel     = std::exp (-1.0f / (0.001f * apvts.getRawParameterValue (ids::duckRelease)->load() * (float) sr));
    const bool  lowsOnly= apvts.getRawParameterValue (ids::duckBand)->load() > 0.5f;
    duckLow.setCutoffFrequency (apvts.getRawParameterValue (ids::duckXover)->load());
    const int   monitor = (int) apvts.getRawParameterValue (ids::monitor)->load();

    const float peakDecay = std::exp (-1.0f / (1.5f * (float) sr));
    const float trigRel   = std::exp (-1.0f / (0.03f * (float) sr));
    const float gainSmooth= std::exp (-1.0f / (0.002f * (float) sr));
    const int   holdoffLen= (int) (0.06 * sr);

    float maxGr = 0.0f;
    bool scEnergy = false;

    for (int i = 0; i < n; ++i)
    {
        // ---- kick (sidechain) ----
        float kick = 0.0f;
        for (int c = 0; c < nSc; ++c) kick += sc[c][i];
        if (nSc > 1) kick *= 0.5f;
        const float ak = std::abs (kick);
        if (ak > 1.0e-5f) scEnergy = true;

        trigEnv  = std::max (ak, trigEnv * trigRel);
        trigPeak = std::max (trigEnv, trigPeak * peakDecay);
        kickPeak = trigPeak;

        // ---- bass: delay -> phase filter -> polarity ----
        const float d = delaySm.getNextValue();
        float procMono = 0.0f, rawMono = 0.0f;
        float* ch[2] = { main.getWritePointer (0), nCh > 1 ? main.getWritePointer (1) : nullptr };

        for (int c = 0; c < nCh; ++c)
        {
            const float x = ch[c][i];
            rawMono += x;
            delayLine.pushSample (c, x);
            float y = delayLine.popSample (c, d);
            if (phaseOn) y = allpass[c].process (y);
            if (invert)  y = -y;
            ch[c][i] = y;
            procMono += y;
        }
        rawMono  /= (float) nCh;
        procMono /= (float) nCh;

        // ---- analysis rings (low-passed, mono) ----
        ringKick[(size_t) ringPos] = lpKick.processSample (0, kick);
        ringRaw [(size_t) ringPos] = lpRaw.processSample (0, rawMono);
        ringProc[(size_t) ringPos] = lpProc.processSample (0, procMono);
        const int thisIndex = ringPos;
        ringPos = (ringPos + 1) & ringMask;

        // ---- hit detection ----
        if (holdoff > 0) --holdoff;
        if (pendingCount > 0 && --pendingCount == 0)
            captureHit (pendingOnset);

        if (armed && pendingCount == 0 && trigEnv > 0.5f * trigPeak && trigEnv > 0.003f)
        {
            armed = false;
            holdoff = holdoffLen;
            pendingOnset = thisIndex;
            pendingCount = W;
        }
        else if (! armed && holdoff == 0 && trigEnv < 0.2f * trigPeak)
            armed = true;

        // ---- ducking (ReChain-style) ----
        float target = 1.0f;
        if (duckOn && nSc > 0)
        {
            duckEnv = ak > duckEnv ? ak + atk * (duckEnv - ak) : ak + rel * (duckEnv - ak);
            const float norm = juce::jlimit (0.0f, 1.0f, duckEnv / std::max (kickPeak, 1.0e-4f));
            target = juce::Decibels::decibelsToGain (-depth * norm);
        }
        duckGain = target + gainSmooth * (duckGain - target);
        maxGr = std::max (maxGr, -juce::Decibels::gainToDecibels (duckGain, -60.0f));

        const float g = outSm.getNextValue();
        for (int c = 0; c < nCh; ++c)
        {
            float y = ch[c][i];
            if (lowsOnly)
            {
                const float low = duckLow.processSample (c, y);
                y -= (1.0f - duckGain) * low;   // transparent when not ducking
            }
            else
                y *= duckGain;

            const float k = nSc > 1 ? sc[std::min (c, nSc - 1)][i] : kick;
            if      (monitor == 1) y += k;
            else if (monitor == 2) y = k;
            ch[c][i] = y * g;
        }
    }

    for (int c = nCh; c < buffer.getNumChannels(); ++c)
        if (c < main.getNumChannels()) main.clear (c, 0, n);

    duckGrMeter.store (maxGr);
    if (scEnergy) scSilentSamples = 0; else scSilentSamples += n;
    scActive.store (scSilentSamples < (int) sr);
}

void LockstepProcessor::captureHit (int onset)
{
    auto& h = scratch;
    const int start = onset - pre;
    for (int i = 0; i < pre + W; ++i)
    {
        const size_t idx = (size_t) ((start + i) & ringMask);
        h.kick[(size_t) i] = ringKick[idx];
        h.bass[(size_t) i] = ringProc[idx];
    }
    const int rstart = onset - pre - maxD;
    for (int i = 0; i < pre + maxD + W; ++i)
        h.rawBass[(size_t) i] = ringRaw[(size_t) ((rstart + i) & ringMask)];

    // skip hits where the bass is silent
    double e = 0;
    for (int i = pre; i < pre + W; ++i) e += h.bass[(size_t) i] * h.bass[(size_t) i];
    const bool bassPresent = std::sqrt (e / W) > 1.0e-4;

    const float r = lockstep::correlation (h.kick.data() + pre, h.bass.data() + pre, W);
    h.match = lockstep::correlationToMatch (r);
    if (bassPresent)
        matchSmoothed.store (0.5f * matchSmoothed.load() + 0.5f * h.match);

    const juce::SpinLock::ScopedTryLockType lock (hitLock);
    if (! lock.isLocked()) return;

    std::swap (latest.kick, h.kick);
    std::swap (latest.bass, h.bass);
    std::swap (latest.rawBass, h.rawBass);
    latest.match = h.match; latest.fs = sr; latest.pre = pre;
    if (h.kick.size() != (size_t) (pre + W))   // first hit: scratch got latest's empty vectors
    {
        h.kick.assign ((size_t) (pre + W), 0.0f);
        h.bass.assign ((size_t) (pre + W), 0.0f);
        h.rawBass.assign ((size_t) (pre + maxD + W), 0.0f);
    }
    ++hitSerial;

    if (bassPresent)
    {
        if ((int) recent.size() < numRecentHits) recent.push_back (latest);   // allocates only for the first few hits
        else recent[(size_t) recentPos] = latest;                              // same sizes -> no allocation
        recentPos = (recentPos + 1) % numRecentHits;
    }
}

bool LockstepProcessor::fetchLatestHit (lockstep::Hit& dest, int& serial)
{
    const juce::SpinLock::ScopedLockType lock (hitLock);
    if (serial == hitSerial || latest.kick.empty()) return false;
    dest = latest;
    serial = hitSerial;
    return true;
}

std::vector<lockstep::Hit> LockstepProcessor::copyRecentHits()
{
    const juce::SpinLock::ScopedLockType lock (hitLock);
    return recent;
}

lockstep::AlignResult LockstepProcessor::runAutoAlign()
{
    auto hits = copyRecentHits();
    auto res = lockstep::autoAlign (hits, apvts.getRawParameterValue (ids::phaseQ)->load());
    if (! res.found) return res;

    auto set = [this] (const char* id, float v)
    {
        auto* p = apvts.getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    };
    set (ids::invert,  res.invert ? 1.0f : 0.0f);
    set (ids::delay,   res.delayMs);
    set (ids::phaseOn, res.phaseOn ? 1.0f : 0.0f);
    if (res.phaseOn) set (ids::phaseFreq, res.phaseHz);
    return res;
}

juce::AudioProcessorEditor* LockstepProcessor::createEditor() { return new LockstepEditor (*this); }

void LockstepProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void LockstepProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LockstepProcessor(); }
