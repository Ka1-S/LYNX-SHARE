#include "ui/ReferenceMatchPanel.h"

#include <cmath>
#include <thread>

#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
juce::String signedDb (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String lufsText (const refmatch::Analysis& a) { return a.valid && a.lufs > -70.0 ? juce::String (a.lufs, 1) + " LUFS" : juce::String ("--"); }
juce::String peakText (const refmatch::Analysis& a) { return a.valid && a.truePeakDb > -100.0 ? juce::String (a.truePeakDb, 1) + " dBTP" : juce::String ("--"); }
juce::String widthText (const refmatch::Analysis& a) { return a.valid ? juce::String (juce::roundToInt (a.widthPercent())) + " %" : juce::String ("--"); }
juce::String correlationText (const refmatch::Analysis& a) { return a.valid ? juce::String (a.correlation, 2) : juce::String ("--"); }

// a curve without its mean level over 100 Hz … 10 kHz: the tone alone, so two tracks of different loudness compare
std::array<double, refmatch::bands> toneOf (const refmatch::Analysis& a)
{
    auto out = a.spectrumDb;
    double mean = 0.0;
    int n = 0;
    for (int k = 0; k < a.usedBands; ++k)
        if (refmatch::bandHz (k) >= 100.0 && refmatch::bandHz (k) <= 10000.0)
        {
            mean += out[(std::size_t) k];
            ++n;
        }
    mean = n > 0 ? mean / n : 0.0;
    for (auto& v : out)
        v -= mean;
    return out;
}
} // namespace

struct ReferenceMatchPanel::Shared
{
    std::atomic<int> running { 0 }; // the job at work (0 = none)
    std::atomic<int> done { 0 };    // the job that finished and waits to be read
    std::mutex lock;
    refmatch::Analysis result;
};

// The two plots: the long-term spectra (tone only) on top, the suggested EQ below, on one frequency axis
class ReferenceMatchPanel::Graph final : public juce::Component
{
public:
    explicit Graph (ReferenceMatchPanel& p) : owner (p) {}

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        const auto axis = area.removeFromBottom (16);
        auto top = area.removeFromTop (area.getHeight() * 58 / 100);
        area.removeFromTop (6);
        drawPlot (g, top, true);
        drawPlot (g, area, false);
        // the frequency axis
        g.setFont (theme::font (theme::type::caption));
        g.setColour (theme::colour::textSecondary);
        for (const auto& [hz, text] : { std::pair<double, const char*> { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" },
                                        { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
            g.drawText (text, juce::Rectangle<int> (40, 14).withCentre ({ juce::roundToInt (xOf (hz, top)), axis.getCentreY() }), juce::Justification::centred, false);
    }

private:
    static constexpr int leftAxis = 34;
    static float xOf (double hz, juce::Rectangle<int> plot)
    {
        const auto t = std::log (hz / 18.0) / std::log (22000.0 / 18.0);
        return (float) (plot.getX() + leftAxis) + (float) t * (float) (plot.getWidth() - leftAxis - 6);
    }

    void drawPlot (juce::Graphics& g, juce::Rectangle<int> plot, bool spectrum)
    {
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (plot.toFloat(), theme::radius::md);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (plot.toFloat().reduced (0.5f), theme::radius::md, 1.0f);

        const auto songOk = owner.song.valid, refOk = owner.reference.valid;
        const auto songTone = toneOf (owner.song), refTone = toneOf (owner.reference);
        // the vertical range: the spectra's own span (in 6 dB steps), ±9 dB for the EQ
        double lo = -9.0, hi = 9.0, step = 3.0;
        if (spectrum)
        {
            lo = -30.0; hi = 18.0; step = 12.0;
            double mn = 0.0, mx = 0.0;
            for (int k = 0; k < refmatch::bands; ++k)
            {
                if (songOk) { mn = std::min (mn, songTone[(std::size_t) k]); mx = std::max (mx, songTone[(std::size_t) k]); }
                if (refOk) { mn = std::min (mn, refTone[(std::size_t) k]); mx = std::max (mx, refTone[(std::size_t) k]); }
            }
            if (songOk || refOk)
            {
                lo = std::max (-60.0, std::floor ((mn - 3.0) / 12.0) * 12.0);
                hi = std::min (36.0, std::ceil ((mx + 3.0) / 12.0) * 12.0);
            }
        }
        const auto inner = plot.reduced (0, 8);
        const auto yOf = [&] (double db) { return (float) inner.getY() + (float) ((hi - db) / (hi - lo)) * (float) inner.getHeight(); };
        g.setFont (theme::font (theme::type::caption));
        for (double db = lo; db <= hi + 0.01; db += step)
        {
            const auto y = yOf (db);
            g.setColour (std::abs (db) < 0.01 ? theme::colour::borderStrong : theme::colour::border);
            g.drawHorizontalLine (juce::roundToInt (y), (float) (plot.getX() + leftAxis), (float) plot.getRight() - 6.0f);
            g.setColour (theme::colour::textSecondary);
            g.drawText ((db > 0.0 ? "+" : "") + juce::String (juce::roundToInt (db)), juce::Rectangle<int> (plot.getX() + 2, juce::roundToInt (y) - 7, leftAxis - 6, 14),
                        juce::Justification::centredRight, false);
        }
        g.setColour (theme::colour::border);
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            g.drawVerticalLine (juce::roundToInt (xOf (hz, plot)), (float) inner.getY(), (float) inner.getBottom());

        const auto curve = [&] (const std::array<double, refmatch::bands>& db, int used) {
            juce::Path p;
            for (int k = 0; k < used; ++k)
            {
                const auto x = xOf (refmatch::bandHz (k), plot), y = yOf (std::clamp (db[(std::size_t) k], lo, hi));
                if (k == 0)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x, y);
            }
            return p;
        };
        const juce::PathStrokeType solid (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        // the legend: a short line and the name, right-aligned in the plot's top row
        auto legend = plot.reduced (10, 6).removeFromTop (14);
        const auto key = [&] (const juce::String& name, juce::Colour colour, bool dashed) {
            g.setFont (theme::font (theme::type::caption));
            const auto w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 6;
            auto cell = legend.removeFromRight (w + 22);
            legend.removeFromRight (10);
            g.setColour (theme::colour::textSecondary);
            g.drawText (name, cell.removeFromRight (w), juce::Justification::centredLeft, false);
            g.setColour (colour);
            const auto y = (float) cell.getCentreY();
            if (dashed)
            {
                const float dashes[] { 4.0f, 3.0f };
                g.drawDashedLine ({ (float) cell.getX(), y, (float) cell.getRight() - 4.0f, y }, dashes, 2, 2.0f);
            }
            else
                g.fillRect ((float) cell.getX(), y - 1.0f, (float) cell.getWidth() - 4.0f, 2.0f);
        };
        g.setFont (theme::font (theme::type::small, true));
        g.setColour (theme::colour::textPrimary);
        g.drawText (spectrum ? tr ("Long-term spectrum (tone)") : tr ("Suggested EQ"), plot.reduced (leftAxis + 6, 6).removeFromTop (14), juce::Justification::centredLeft, false);

        if (spectrum)
        {
            if (refOk)
            {
                g.setColour (theme::colour::info);
                g.strokePath (curve (refTone, owner.reference.usedBands), solid);
            }
            if (songOk)
            {
                g.setColour (theme::colour::knobArc);
                g.strokePath (curve (songTone, owner.song.usedBands), solid);
            }
            key (tr ("Reference"), theme::colour::info, false);
            key (tr ("Your song"), theme::colour::knobArc, false);
        }
        else if (owner.suggestion.valid)
        {
            const auto used = std::min (owner.song.usedBands, owner.reference.usedBands);
            juce::Path target;
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (target, curve (owner.suggestion.targetDb, used), dashes, 2);
            g.setColour (theme::colour::warning);
            g.fillPath (target);
            g.setColour (theme::colour::accent);
            g.strokePath (curve (owner.suggestion.eqDb, used), solid);
            key (tr ("EQ curve"), theme::colour::accent, false);
            key (tr ("Difference"), theme::colour::warning, true);
        }
        else
        {
            g.setFont (theme::font (theme::type::small));
            g.setColour (theme::colour::textDisabled);
            g.drawText (tr ("Measure both to see the suggestion."), inner, juce::Justification::centred, false);
        }
    }

    ReferenceMatchPanel& owner;
};

ReferenceMatchPanel::ReferenceMatchPanel (AudioEngine& e, Hooks h) : engine (e), hooks (std::move (h)), shared (std::make_shared<Shared>())
{
    setTitle (tr ("Reference match"));
    title.setText (tr ("Reference match"), juce::dontSendNotification);
    title.setFont (theme::font (theme::type::title, true));
    addAndMakeVisible (title);
    status.setFont (theme::font (theme::type::small));
    status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    status.setJustificationType (juce::Justification::centredRight);
    status.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (status);

    chooseButton.setButtonText (tr ("Choose reference..."));
    chooseButton.setTooltip (tr ("A finished track whose sound you want to get close to"));
    chooseButton.onClick = [this] { chooseReference(); };
    addAndMakeVisible (chooseButton);
    fileLabel.setFont (theme::font (theme::type::body));
    fileLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (fileLabel);
    measureButton.setButtonText (tr ("Measure my song"));
    measureButton.setTooltip (tr ("Renders the song without the match and measures it"));
    measureButton.onClick = [this] { start (2); };
    addAndMakeVisible (measureButton);

    const char* heads[4] { "", "Your song", "Reference", "After match" };
    const char* names[4] { "Loudness", "True peak", "Stereo width", "Left/right correlation" };
    for (int i = 0; i < 4; ++i)
    {
        statHead[i].setText (i == 0 ? juce::String() : tr (heads[i]), juce::dontSendNotification);
        statHead[i].setFont (theme::font (theme::type::small, true));
        statHead[i].setColour (juce::Label::textColourId, theme::colour::textSecondary);
        statHead[i].setJustificationType (i == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight);
        addAndMakeVisible (statHead[i]);
        statName[i].setText (tr (names[i]), juce::dontSendNotification);
        statName[i].setFont (theme::font (theme::type::body));
        statName[i].setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (statName[i]);
        for (auto& cell : statCell[i])
        {
            cell.setFont (theme::font (theme::type::body, true));
            cell.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (cell);
        }
    }

    graph = std::make_unique<Graph> (*this);
    addAndMakeVisible (*graph);

    amountLabel.setText (tr ("Amount"), juce::dontSendNotification);
    amountLabel.setFont (theme::font (theme::type::body, true));
    addAndMakeVisible (amountLabel);
    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
    amount.setRange (0.0, 100.0, 1.0);
    amount.setValue (100.0, juce::dontSendNotification);
    amount.setDoubleClickReturnValue (true, 100.0);
    amount.setTextValueSuffix (" %");
    amount.setTitle (tr ("Amount"));
    amount.onValueChange = [this] { recompute(); };
    addAndMakeVisible (amount);
    loudnessToggle.setButtonText (tr ("Match the loudness too"));
    loudnessToggle.setToggleState (true, juce::dontSendNotification);
    loudnessToggle.onClick = [this] { recompute(); };
    addAndMakeVisible (loudnessToggle);

    matchButton.setButtonText (tr ("Match"));
    matchButton.setTooltip (tr ("Puts the suggested EQ and the loudness gain on the Master"));
    matchButton.onClick = [this] { match(); };
    addAndMakeVisible (matchButton);
    checkButton.setButtonText (tr ("Measure the result"));
    checkButton.setTooltip (tr ("Renders the song with the match and measures it"));
    checkButton.onClick = [this] { start (3); };
    addAndMakeVisible (checkButton);
    hearWithout.setButtonText (tr ("Hear without"));
    hearWithout.setClickingTogglesState (true);
    hearWithout.onClick = [this] {
        if (updating)
            return;
        const auto off = hearWithout.getToggleState();
        engine.editSong ([off] (Song& s) { refmatch::setBypassed (s, off); });
    };
    addAndMakeVisible (hearWithout);
    removeButton.setButtonText (tr ("Remove match"));
    removeButton.onClick = [this] {
        engine.editSongStructural ([] (Song& s) { refmatch::remove (s); });
        hasAfter = false;
        trimDb = appliedTrimDb = resultOffDb = 0.0;
        recompute();
        refresh();
    };
    addAndMakeVisible (removeButton);
    masterButton.setButtonText (tr ("Open in the Mixer"));
    masterButton.onClick = [this] {
        if (hooks.showMaster)
            hooks.showMaster();
    };
    addChildComponent (masterButton);
    masterButton.setVisible (hooks.showMaster != nullptr);

    note.setFont (theme::font (theme::type::small));
    note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    note.setJustificationType (juce::Justification::topLeft);
    note.setMinimumHorizontalScale (0.85f);
    addAndMakeVisible (note);

    setSize (defaultWidth, defaultHeight);
    refresh();
}

ReferenceMatchPanel::~ReferenceMatchPanel() { stopTimer(); }

refmatch::Analysis ReferenceMatchPanel::measureSong (Song s, bool withMatch)
{
    refmatch::setBypassed (s, ! withMatch);
    exportopts::Options o;
    o.format = exportopts::Format::wav;
    o.wavBits = 32;
    o.sampleRate = 44100.0;
    const auto file = juce::File::createTempFile (".wav");
    refmatch::Analysis a;
    {
        ExportJob job (s, o, file);
        job.setExtraData (false);
        juce::String error;
        if (job.run ([] (double) { return true; }, error))
        {
            juce::WavAudioFormat wav;
            if (std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true)); reader != nullptr)
            {
                const auto n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (44100.0 * 900.0));
                juce::AudioBuffer<float> buf ((int) juce::jmax (2u, reader->numChannels), n);
                reader->read (&buf, 0, n, 0, true, true);
                a = refmatch::analyse (buf.getReadPointer (0), buf.getReadPointer (1), (std::size_t) n, 44100.0);
            }
        }
    }
    file.deleteFile();
    return a;
}

refmatch::Analysis ReferenceMatchPanel::measureFile (AudioEngine& engine, const juce::File& file)
{
    const auto sample = engine.getSampleLibrary().load (file.getFullPathName());
    if (sample == nullptr || sample->frames() == 0)
        return {};
    return refmatch::analyse (sample->leftData(), sample->rightData(), sample->frames(), sample->sampleRate);
}

void ReferenceMatchPanel::chooseReference()
{
    chooser = ui::makeFileChooser (tr ("Choose a reference track"), referenceFile.existsAsFile() ? referenceFile.getParentDirectory() : juce::File(),
                                   engine.getSampleLibrary().wildcard());
    ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        referenceFile = file;
        referenceName = file.getFileName();
        reference = {};
        start (1);
    });
}

void ReferenceMatchPanel::start (int job)
{
    if (shared->running.load() != 0)
        return;
    shared->running.store (job);
    auto state = shared;
    if (job == 1)
    {
        auto* e = &engine;
        std::thread ([state, e, file = referenceFile] {
            auto a = measureFile (*e, file);
            {
                const std::lock_guard<std::mutex> guard (state->lock);
                state->result = a;
            }
            state->done.store (1);
        }).detach();
    }
    else
    {
        std::thread ([state, job, s = engine.getSong()] {
            auto a = measureSong (s, job == 3);
            {
                const std::lock_guard<std::mutex> guard (state->lock);
                state->result = a;
            }
            state->done.store (job);
        }).detach();
    }
    updateTexts();
    startTimer (100);
}

void ReferenceMatchPanel::timerCallback()
{
    const auto job = shared->done.exchange (0);
    if (job == 0)
        return;
    stopTimer();
    refmatch::Analysis a;
    {
        const std::lock_guard<std::mutex> guard (shared->lock);
        a = shared->result;
    }
    shared->running.store (0);
    if (job != 3)
        trimDb = appliedTrimDb = resultOffDb = 0.0; // (measured anew: the level is worked out from the start)
    if (job == 1)
    {
        reference = a;
        if (! a.valid)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Reference match"), tr ("Could not read {0}", { referenceName }));
    }
    else if (job == 2)
    {
        song = a;
        hasAfter = false;
    }
    else
        resultMeasured (a);
    recompute();
}

void ReferenceMatchPanel::resultMeasured (const refmatch::Analysis& result)
{
    after = result;
    hasAfter = result.valid;
    resultOffDb = 0.0;
    if (! result.valid || ! reference.valid || ! loudnessToggle.getToggleState() || ! refmatch::present (engine.getSong()))
        return;
    const auto off = reference.lufs - result.lufs;
    if (std::abs (off) <= 0.5)
        return;
    resultOffDb = off;
    if (appliedTrimDb == trimDb) // (once per Match: measuring twice does not ask for it twice)
        trimDb = juce::jlimit (-refmatch::maxLoudnessDb, refmatch::maxLoudnessDb, trimDb + off);
}

void ReferenceMatchPanel::recompute()
{
    refmatch::Options o;
    o.amount = amount.getValue() / 100.0;
    o.matchLoudness = loudnessToggle.getToggleState();
    o.trimDb = trimDb;
    suggestion = refmatch::suggest (song, reference, 44100.0, o);
    updateTexts();
    graph->repaint();
}

void ReferenceMatchPanel::match()
{
    if (! suggestion.valid)
        return;
    bool ok = true;
    const auto s = suggestion;
    engine.editSongStructural ([&ok, s] (Song& target) { ok = refmatch::apply (target, s); });
    if (! ok)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Reference match"),
                                                tr ("The Master has no room for the match: it needs two free effect slots."));
    else
        appliedTrimDb = trimDb;
    resultOffDb = 0.0;
    hasAfter = false;
    refresh();
}

bool ReferenceMatchPanel::matchForSnapshot()
{
    match();
    return refmatch::present (engine.getSong());
}

void ReferenceMatchPanel::showForSnapshot (const juce::String& name, const refmatch::Analysis& s, const refmatch::Analysis& r, const refmatch::Analysis* result)
{
    referenceName = name;
    song = s;
    reference = r;
    hasAfter = false;
    if (result != nullptr)
        resultMeasured (*result);
    recompute();
}

void ReferenceMatchPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    hearWithout.setToggleState (refmatch::bypassed (engine.getSong()), juce::dontSendNotification);
    updateTexts();
}

void ReferenceMatchPanel::updateTexts()
{
    const auto busy = shared->running.load();
    const auto has = refmatch::present (engine.getSong());
    fileLabel.setText (referenceName.isEmpty() ? tr ("No reference track yet") : referenceName, juce::dontSendNotification);
    fileLabel.setColour (juce::Label::textColourId, referenceName.isEmpty() ? theme::colour::textDisabled : theme::colour::textPrimary);
    status.setText (busy == 1 ? tr ("Measuring the reference...") : busy == 2 ? tr ("Measuring your song...") : busy == 3 ? tr ("Measuring the result...")
                    : has ? tr ("On the Master: Facet EQ and Apex Limiter") : juce::String(),
                    juce::dontSendNotification);
    const refmatch::Analysis none;
    const refmatch::Analysis* cols[3] { &song, &reference, hasAfter ? &after : &none };
    for (int c = 0; c < 3; ++c)
    {
        statCell[0][c].setText (lufsText (*cols[c]), juce::dontSendNotification);
        statCell[1][c].setText (peakText (*cols[c]), juce::dontSendNotification);
        statCell[2][c].setText (widthText (*cols[c]), juce::dontSendNotification);
        statCell[3][c].setText (correlationText (*cols[c]), juce::dontSendNotification);
    }
    chooseButton.setEnabled (busy == 0);
    measureButton.setEnabled (busy == 0);
    matchButton.setEnabled (busy == 0 && suggestion.valid);
    checkButton.setEnabled (busy == 0 && has);
    hearWithout.setEnabled (has);
    removeButton.setEnabled (has);
    masterButton.setEnabled (has);
    amount.setEnabled (suggestion.valid);
    loudnessToggle.setEnabled (suggestion.valid);
    juce::String text;
    if (hasAfter && resultOffDb != 0.0)
        text = tr ("The result is {0} from the reference's loudness. Match again to correct the level.",
                   { signedDb (-resultOffDb).replace ("dB", "LU") })
               + " ";
    else if (suggestion.valid)
        text = tr ("Match will set the EQ within {0} of the curve and change the level by {1} (target {2}).",
                   { juce::String (suggestion.fitErrorDb, 1) + " dB", signedDb (suggestion.gainDb), juce::String (suggestion.targetLufs, 1) + " LUFS" })
               + " ";
    text << tr ("Match adds two Master effects, Facet EQ and Apex Limiter, with the preset name \"Reference match\". Open them to change any band or the level.");
    note.setText (text, juce::dontSendNotification);
}

void ReferenceMatchPanel::paint (juce::Graphics& g) { g.fillAll (theme::colour::bg2); }

void ReferenceMatchPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    auto head = r.removeFromTop (26);
    title.setBounds (head.removeFromLeft (220));
    status.setBounds (head);
    r.removeFromTop (8);
    auto file = r.removeFromTop (30);
    chooseButton.setBounds (file.removeFromLeft (170));
    file.removeFromLeft (10);
    measureButton.setBounds (file.removeFromRight (170));
    file.removeFromRight (10);
    fileLabel.setBounds (file);
    r.removeFromTop (10);

    // the numbers: a name column and three value columns
    const auto nameW = juce::jmin (220, r.getWidth() * 34 / 100);
    const auto colW = (r.getWidth() - nameW) / 3;
    auto row = r.removeFromTop (20);
    statHead[0].setBounds (row.removeFromLeft (nameW));
    for (int c = 1; c < 4; ++c)
        statHead[c].setBounds (row.removeFromLeft (colW));
    for (int i = 0; i < 4; ++i)
    {
        row = r.removeFromTop (22);
        statName[i].setBounds (row.removeFromLeft (nameW));
        for (auto& cell : statCell[i])
            cell.setBounds (row.removeFromLeft (colW));
    }
    r.removeFromTop (10);

    note.setBounds (r.removeFromBottom (36));
    r.removeFromBottom (6);
    auto buttons = r.removeFromBottom (30);
    const auto gap = 8;
    const auto n = masterButton.isVisible() ? 5 : 4;
    const auto bw = (buttons.getWidth() - (n - 1) * gap) / n;
    for (auto* b : { &matchButton, &checkButton, &hearWithout, &removeButton })
    {
        b->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (gap);
    }
    if (masterButton.isVisible())
        masterButton.setBounds (buttons);
    r.removeFromBottom (8);
    auto controls = r.removeFromBottom (28);
    amountLabel.setBounds (controls.removeFromLeft (70));
    loudnessToggle.setBounds (controls.removeFromRight (230));
    controls.removeFromRight (12);
    amount.setBounds (controls);
    r.removeFromBottom (10);
    graph->setBounds (r);
}
} // namespace lynx
