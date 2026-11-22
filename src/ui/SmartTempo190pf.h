#pragma once

// Round 190pf (research190oe P12): Smart Tempo's preview (core/SmartTempo.h). The audio clip menu's LYNX extras › "Make the
// project tempo follow this clip..." opens it: the clip's bars as found (a column per bar, its tempo; the line is the
// tempo after smoothing), the first downbeat's beat, the pulse taken as found / half / double, a Smooth amount and how
// far the bar lines then end from the downbeats. "Follow" writes the tempo markers and unwarps the clip (one undo step).
// Header only: included by PlaylistComponent.cpp (and the shot route, which pictures the window without showing it).

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MarkerPlan.h"
#include "core/SmartTempo.h"
#include "engine/AudioEngine.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
// The bars' tempos: a column per bar, the smoothed tempo as a line, the lowest and highest BPM at the left
class SmartTempoGraph final : public juce::Component
{
public:
    SmartTempoGraph() { setSize (460, 150); }
    void set (std::vector<double> rawBpm, std::vector<double> smoothBpm)
    {
        raw = std::move (rawBpm);
        smooth = std::move (smoothBpm);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto r = getLocalBounds().toFloat();
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, 4.0f);
        if (raw.empty())
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::small));
            g.drawText (tr ("No beats were found in this clip."), r, juce::Justification::centred);
            return;
        }
        auto lo = *std::min_element (raw.begin(), raw.end()), hi = *std::max_element (raw.begin(), raw.end());
        lo = std::floor (lo - 2.0);
        hi = std::ceil (hi + 2.0);
        const auto plot = r.reduced (8.0f).withTrimmedLeft (40.0f).withTrimmedBottom (14.0f);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawText (juce::String (hi, 0), juce::Rectangle<float> (r.getX() + 4.0f, plot.getY() - 6.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText (juce::String (lo, 0), juce::Rectangle<float> (r.getX() + 4.0f, plot.getBottom() - 6.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
        const auto yOf = [&] (double bpm) { return plot.getBottom() - (float) ((bpm - lo) / std::max (1.0, hi - lo)) * plot.getHeight(); };
        const auto w = plot.getWidth() / (float) raw.size();
        for (std::size_t k = 0; k < raw.size(); ++k)
        {
            const auto x = plot.getX() + w * (float) k;
            g.setColour (juce::Colour (0xff5b8fc9).withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (x + 0.5f, yOf (raw[k]), std::max (1.0f, w - 1.0f), plot.getBottom() - yOf (raw[k])));
        }
        if (smooth.size() == raw.size())
        {
            juce::Path line;
            for (std::size_t k = 0; k < smooth.size(); ++k)
            {
                const auto x = plot.getX() + w * ((float) k + 0.5f);
                k == 0 ? line.startNewSubPath (x, yOf (smooth[k])) : line.lineTo (x, yOf (smooth[k]));
            }
            g.setColour (colour::accent);
            g.strokePath (line, juce::PathStrokeType (1.6f));
        }
        g.setColour (colour::textSecondary);
        const auto step = std::max<std::size_t> (1, raw.size() / 8);
        for (std::size_t k = 0; k < raw.size(); k += step)
            g.drawText (juce::String ((int) k + 1), juce::Rectangle<float> (plot.getX() + w * (float) k, plot.getBottom() + 1.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft);
    }
    std::vector<double> raw, smooth;
};

class SmartTempoWindow final : public juce::AlertWindow,
                               private juce::Slider::Listener
{
public:
    SmartTempoWindow (AudioEngine& e, int clip, std::vector<float> audio, double sampleRate, juce::Component* owner)
        : juce::AlertWindow (tr ("Make the project tempo follow this clip"),
                             tr ("The bars follow the recording: a tempo marker on each bar, and the clip plays exactly as recorded."),
                             juce::MessageBoxIconType::NoIcon, owner),
          engine (e), clipIndex (clip), mono (std::move (audio)), rate (sampleRate)
    {
        const auto& song = engine.getSong();
        const auto start = clipIndex >= 0 && clipIndex < (int) song.playlist.size() ? song.playlist[(std::size_t) clipIndex].start : 0;
        beatsPerBar = std::clamp (markers::signatureAt (song, start).first, 1, 16);
        addCustomComponent (&graph);
        juce::StringArray beats;
        for (int b = 1; b <= beatsPerBar; ++b)
            beats.add (juce::String (b));
        addComboBox ("downbeat", beats, tr ("Bar 1 starts on beat"));
        addComboBox ("pulse", { tr ("As detected"), tr ("Half"), tr ("Double") }, tr ("Tempo"));
        getComboBoxComponent ("downbeat")->setSelectedItemIndex (0, juce::dontSendNotification);
        getComboBoxComponent ("pulse")->setSelectedItemIndex (0, juce::dontSendNotification);
        getComboBoxComponent ("downbeat")->onChange = [this] { analyse(); };
        getComboBoxComponent ("pulse")->onChange = [this] { analyse(); };
        smoothLabel.setText (tr ("Smooth"), juce::dontSendNotification);
        smoothLabel.setBorderSize ({});
        smoothSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        smoothSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        smoothSlider.setRange (0.0, 100.0, 1.0);
        smoothSlider.setTextValueSuffix (" %");
        smoothSlider.setTooltip (tr ("0 %: every bar line on its downbeat. More: steadier tempos, the bar lines drift from the downbeats."));
        smoothSlider.addListener (this);
        smoothRow.addAndMakeVisible (smoothLabel);
        smoothRow.addAndMakeVisible (smoothSlider);
        smoothRow.onResize = [this] {
            smoothLabel.setBounds (0, 0, smoothRow.getWidth(), 18);
            smoothSlider.setBounds (0, 20, smoothRow.getWidth(), 24);
        };
        smoothRow.setSize (460, 46);
        smoothRow.onResize(); // (the AlertWindow may keep this size: lay the row out now)
        smoothLabel.setFont (getLookAndFeel().getAlertWindowFont());
        smoothLabel.setColour (juce::Label::textColourId, findColour (juce::AlertWindow::textColourId));
        addCustomComponent (&smoothRow);
        info.setSize (460, 40);
        info.setJustificationType (juce::Justification::topLeft);
        addCustomComponent (&info);
        addButton (tr ("Follow"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        analyse();
    }

    // the analysis and the plan again (the downbeat or the pulse changed): a few tens of ms for a song-long take
    void analyse()
    {
        smarttempo::Options o;
        o.beatsPerBar = beatsPerBar;
        o.downbeatShift = std::max (0, getComboBoxComponent ("downbeat")->getSelectedItemIndex());
        const auto pulse = getComboBoxComponent ("pulse")->getSelectedItemIndex();
        o.tempoFactor = pulse == 1 ? 0.5 : pulse == 2 ? 2.0 : 1.0;
        analysis = smarttempo::analyse (mono.data(), (long) mono.size(), rate, o);
        replan();
    }

    void replan()
    {
        const auto& song = engine.getSong();
        const auto smooth = smoothSlider.getValue() / 100.0;
        plan = smarttempo::plan (song, clipIndex, analysis.bars, (double) mono.size() / std::max (1.0, rate), smooth);
        std::vector<double> after;
        for (const auto& m : plan.marks)
            after.push_back (m.bpm);
        graph.set (plan.rawBpm, after);
        juce::String text;
        if (smarttempo::blockedBy (song))
            text = tr ("Something else moves the project's tempo (a tempo automation clip or pattern tempo events): remove it first.");
        else if (! plan.ok)
            text = tr ("No bars were found in this clip.");
        else
        {
            const auto lo = *std::min_element (plan.rawBpm.begin(), plan.rawBpm.end()), hi = *std::max_element (plan.rawBpm.begin(), plan.rawBpm.end());
            text = tr ("{0} bars, {1} to {2} BPM.", { juce::String ((int) plan.rawBpm.size()), juce::String (lo, 1), juce::String (hi, 1) });
            text << " " << (plan.maxErrorMs < 0.5 ? tr ("Every bar line is on its downbeat.")
                                                   : tr ("The bar lines end up to {0} ms from the downbeats.", { juce::String (plan.maxErrorMs, 1) }));
            if (plan.unwarp)
                text << " " << tr ("The clip's channel stops stretching to the tempo.");
        }
        info.setText (text, juce::dontSendNotification);
        if (auto* follow = getButton (0))
            follow->setEnabled (plan.ok);
    }

    bool apply()
    {
        if (! plan.ok)
            return false;
        auto done = false;
        engine.editSong ([this, &done] (Song& s) { done = smarttempo::apply (s, clipIndex, plan); });
        return done;
    }

    const smarttempo::Plan& currentPlan() const noexcept { return plan; }
    void setSmoothForShot (double percent) { smoothSlider.setValue (percent, juce::sendNotificationSync); }

private:
    void sliderValueChanged (juce::Slider*) override { replan(); }

    struct Row final : juce::Component
    {
        std::function<void()> onResize;
        void resized() override
        {
            if (onResize)
                onResize();
        }
    };

    AudioEngine& engine;
    int clipIndex = -1;
    std::vector<float> mono;
    double rate = 44100.0;
    int beatsPerBar = 4;
    smarttempo::Analysis analysis;
    smarttempo::Plan plan;
    SmartTempoGraph graph;
    Row smoothRow;
    juce::Label smoothLabel;
    juce::Slider smoothSlider;
    juce::Label info;
};

// The clip's whole file, mono (the analysis reads the file; the plan places it by the clip's offset). Empty without one.
inline std::vector<float> smartTempoAudio (AudioEngine& engine, int clipIndex, double& rate)
{
    const auto& song = engine.getSong();
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return {};
    const auto& clip = song.playlist[(std::size_t) clipIndex];
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size())
        return {};
    const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (song.channels[(std::size_t) clip.channel].samplePath.c_str()));
    if (sample == nullptr || sample->frames() == 0)
        return {};
    rate = sample->sampleRate;
    const auto frames = (std::size_t) sample->frames();
    std::vector<float> mono (sample->leftData(), sample->leftData() + frames);
    if (sample->isStereo())
        for (std::size_t i = 0; i < frames; ++i)
            mono[i] = 0.5f * (mono[i] + sample->rightData()[i]);
    return mono;
}
} // namespace lynx
