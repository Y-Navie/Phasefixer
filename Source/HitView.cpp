#include "HitView.h"
#include "LockstepLookAndFeel.h"

HitView::HitView (LockstepProcessor& p) : proc (p) { startTimerHz (30); }

void HitView::timerCallback()
{
    if (proc.fetchLatestHit (hit, serial))
    {
        history.push_back (hit.match);
        if (history.size() > 48) history.erase (history.begin());
        fade = 1.0f;
    }
    else
        fade = std::max (0.35f, fade - 0.004f);   // dims slowly when no new hits arrive
    repaint();
}

void HitView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 10.0f);

    auto area = bounds.reduced (12.0f, 10.0f);
    auto histArea = area.removeFromBottom (54.0f);
    area.removeFromBottom (18.0f);   // time labels
    auto plot = area;

    // legend
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (colours::kick);
    g.drawText ("KICK", plot.getX() + 4, plot.getY(), 50, 16, juce::Justification::centredLeft);
    g.setColour (colours::bass);
    g.drawText ("BASS", plot.getX() + 50, plot.getY(), 50, 16, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("green = push together   red = cancelling", plot.getX() + 100, plot.getY(), 260, 16,
                juce::Justification::centredLeft);

    // time grid (-5 .. 100 ms)
    const float t0 = (float) -lockstep::preRollMs, t1 = (float) lockstep::windowMs;
    auto tx = [&] (float ms) { return plot.getX() + (ms - t0) / (t1 - t0) * plot.getWidth(); };
    for (float ms = 0.0f; ms <= t1; ms += 10.0f)
    {
        g.setColour (colours::outline.withAlpha (ms < 1.0f ? 1.0f : 0.5f));
        g.drawVerticalLine ((int) tx (ms), plot.getY(), plot.getBottom());
        g.setColour (colours::textDim);
        g.drawText (juce::String ((int) ms) + (ms < 1.0f ? " ms" : ""), juce::Rectangle<float> (tx (ms) - 20, plot.getBottom() + 2, 40, 14),
                    juce::Justification::centred);
    }
    g.setColour (colours::outline);
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

    if (hit.kick.empty())
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (proc.sidechainActive() ? "Waiting for a kick + bass hit..."
                                           : "Route your kick into the sidechain input and press play",
                    plot, juce::Justification::centred);
    }
    else
    {
        const int n = (int) hit.kick.size();
        float pk = 1e-9f, pb = 1e-9f;
        for (int i = 0; i < n; ++i)
        {
            pk = std::max (pk, std::abs (hit.kick[(size_t) i]));
            pb = std::max (pb, std::abs (hit.bass[(size_t) i]));
        }
        const float half = plot.getHeight() * 0.42f;
        const float msPerSample = (float) (1000.0 / hit.fs);
        const int cols = (int) plot.getWidth();

        juce::Path pkPath, pbPath;
        for (int c = 0; c <= cols; ++c)
        {
            const float ms = t0 + (float) c / (float) cols * (t1 - t0);
            const float fi = (ms + (float) lockstep::preRollMs) / msPerSample;
            const int i = juce::jlimit (0, n - 2, (int) fi);
            const float fr = juce::jlimit (0.0f, 1.0f, fi - (float) i);
            const float k = (hit.kick[(size_t) i] + fr * (hit.kick[(size_t) i + 1] - hit.kick[(size_t) i])) / pk;
            const float b = (hit.bass[(size_t) i] + fr * (hit.bass[(size_t) i + 1] - hit.bass[(size_t) i])) / pb;
            const float x = plot.getX() + (float) c;
            const float yk = plot.getCentreY() - k * half, yb = plot.getCentreY() - b * half;

            if (ms >= 0.0f)
            {
                const float strength = std::min (std::abs (k), std::abs (b));
                g.setColour ((k * b >= 0 ? colours::good : colours::bad).withAlpha (0.45f * strength * fade));
                g.drawVerticalLine ((int) x, std::min (yk, yb) - 0.0f, std::max (std::max (yk, yb), std::min (yk, yb) + 1.0f));
                g.setColour ((k * b >= 0 ? colours::good : colours::bad).withAlpha (0.12f * strength * fade));
                g.drawVerticalLine ((int) x, std::min ({ yk, yb, plot.getCentreY() }), std::max ({ yk, yb, plot.getCentreY() }));
            }

            if (c == 0) { pkPath.startNewSubPath (x, yk); pbPath.startNewSubPath (x, yb); }
            else        { pkPath.lineTo (x, yk);          pbPath.lineTo (x, yb); }
        }
        g.setColour (colours::kick.withAlpha (fade));
        g.strokePath (pkPath, juce::PathStrokeType (2.0f));
        g.setColour (colours::bass.withAlpha (fade));
        g.strokePath (pbPath, juce::PathStrokeType (2.0f));
    }

    // history
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("LAST HITS", histArea.removeFromTop (14.0f), juce::Justification::centredLeft);
    const int maxBars = 48;
    const float bw = histArea.getWidth() / (float) maxBars;
    for (int i = 0; i < (int) history.size(); ++i)
    {
        const float m = history[(size_t) i];
        const float h = histArea.getHeight() * juce::jlimit (0.06f, 1.0f, m / 100.0f);
        const float x = histArea.getRight() - (float) (history.size() - (size_t) i) * bw;
        g.setColour (colours::forMatch (m).withAlpha (0.85f));
        g.fillRoundedRectangle (x + 1.0f, histArea.getBottom() - h, bw - 2.0f, h, 2.0f);
    }
}

// ---------------------------------------------------------------------------

void MatchMeter::timerCallback()
{
    const float target = proc.getMatch();
    shown += 0.25f * (target - shown);
    gr = std::max (proc.getDuckGrDb(), gr * 0.85f);
    if (result.isNotEmpty() && ++resultAge > 30 * 8) result.clear();
    repaint();
}

void MatchMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, 10.0f);
    b.reduce (12.0f, 10.0f);

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("PHASE MATCH", b.removeFromTop (16.0f), juce::Justification::centred);

    auto grArea = b.removeFromBottom (34.0f);
    auto resArea = b.removeFromBottom (34.0f);

    const float size = std::min (b.getWidth(), b.getHeight());
    auto gauge = b.withSizeKeepingCentre (size, size).reduced (6.0f);
    const auto c = gauge.getCentre();
    const float r = gauge.getWidth() * 0.5f, thick = r * 0.14f;
    const float start = -2.35f, end = 2.35f;
    const float pos = juce::jlimit (0.0f, 1.0f, (shown - 30.0f) / 70.0f);   // 30 % .. 100 %

    juce::Path track, val;
    track.addCentredArc (c.x, c.y, r - thick, r - thick, 0.0f, start, end, true);
    val.addCentredArc (c.x, c.y, r - thick, r - thick, 0.0f, start, start + pos * (end - start), true);
    g.setColour (colours::outline);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto col = colours::forMatch (shown);
    g.setColour (col);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (juce::FontOptions (r * 0.55f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (shown)) + "%", gauge.withSizeKeepingCentre (r * 1.6f, r * 0.7f),
                juce::Justification::centred);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    const juce::String verdict = shown >= 85 ? "locked in" : shown >= 70 ? "good" : shown >= 55 ? "could be tighter" : "cancelling";
    g.drawText (verdict, gauge.withSizeKeepingCentre (r * 1.6f, 16.0f).translated (0, r * 0.45f), juce::Justification::centred);

    if (result.isNotEmpty())
    {
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (result, resArea.toNearestInt(), juce::Justification::centred, 2);
    }

    // duck meter
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("DUCK", grArea.removeFromTop (14.0f), juce::Justification::centredLeft);
    auto bar = grArea.reduced (0, 4);
    g.setColour (colours::outline);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (colours::duck);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, gr / 24.0f)), 3.0f);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("-" + juce::String (gr, 1) + " dB", bar.reduced (4, 0), juce::Justification::centredRight);
}
