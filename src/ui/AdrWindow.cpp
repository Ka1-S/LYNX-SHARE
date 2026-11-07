#include "ui/AdrWindow.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Adr.h"
#include "core/DialogueAlign.h"
#include "core/Instruments.h"
#include "core/SessionIds.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/TextCache.h"
#include "ui/Theme.h"
#include "ui/VideoTracks.h"

namespace lynx
{
namespace
{
using namespace theme;

double songSeconds (AudioEngine& e)
{
    return e.getSong().secondsForTicks ((std::int64_t) std::llround (std::max (0.0, e.songPlayheadTicks())));
}

std::int64_t ticksAt (const Song& s, double seconds)
{
    return (std::int64_t) std::llround (videotrack::ticksForSeconds (s, std::max (0.0, seconds)));
}

juce::String tc (const Song& s, double seconds)
{
    return juce::String (vtime::format (videotrack::timecodeOf (s.video, seconds), s.video.rate));
}

std::optional<double> parseTc (const Song& s, const juce::String& text)
{
    if (const auto t = vtime::parseSeconds (text.toStdString(), s.video.rate))
        return videotrack::songOf (s.video, *t);
    return std::nullopt;
}

int clipIndexOf (const Song& s, std::uint64_t uid)
{
    if (uid == 0)
        return -1;
    for (std::size_t i = 0; i < s.playlist.size(); ++i)
        if (s.playlist[i].uid == uid)
            return (int) i;
    return -1;
}

// A take's audio between song times [from, to) as mono at the file's rate (the clip's channel sample)
std::vector<float> clipAudio (AudioEngine& engine, const Song& s, const Clip& c, double from, double to, double& rate)
{
    std::vector<float> out;
    if (c.channel < 0 || c.channel >= (int) s.channels.size())
        return out;
    const auto data = engine.getSampleLibrary().load (juce::String::fromUTF8 (s.channels[(std::size_t) c.channel].samplePath.c_str()));
    if (data == nullptr || data->left.empty())
        return out;
    rate = data->sampleRate;
    // the clip's start in the file: its offset (ticks at the song tempo, as the Playlist's audio clips)
    const auto clipStart = s.secondsForTicks (c.start);
    const auto offset = (double) c.offset * 60.0 / (std::max (1.0, s.bpm) * ppq);
    const auto a = (long long) std::llround ((from - clipStart + offset) * rate), b = (long long) std::llround ((to - clipStart + offset) * rate);
    out.resize ((std::size_t) std::max (0LL, b - a));
    for (long long k = a; k < b; ++k)
    {
        float v = 0.0f;
        if (k >= 0 && k < (long long) data->left.size())
            v = data->right.empty() ? data->left[(std::size_t) k] : 0.5f * (data->left[(std::size_t) k] + data->right[(std::size_t) k]);
        out[(std::size_t) (k - a)] = v;
    }
    return out;
}

std::vector<float> resample (const std::vector<float>& x, double from, double to)
{
    if (std::abs (from - to) < 1e-6 || x.empty())
        return x;
    const auto n = (std::size_t) std::floor ((double) x.size() * to / from);
    std::vector<float> y (n);
    // (a two-tap average first when shrinking a lot: alignment only needs the features up to 8 kHz)
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto pos = (double) i * from / to;
        const auto k = (std::size_t) pos;
        const auto f = (float) (pos - (double) k);
        const auto a = x[std::min (k, x.size() - 1)], b = x[std::min (k + 1, x.size() - 1)];
        y[i] = a + (b - a) * f;
    }
    return y;
}

bool writeWav (const juce::File& file, const std::vector<float>& mono, double rate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), rate, 1, 24, {}, 0));
    if (w == nullptr)
        return false;
    stream.release();
    const float* ch[] = { mono.data() };
    return w->writeFromFloatArrays (ch, 1, (int) mono.size());
}

// ---------------------------------------------------------------------------------------------------------------
// The comp strip: one lane per take over the cue; drag on a lane = that take plays there
class CompStrip final : public juce::Component
{
public:
    std::function<const AdrCue*()> cue;
    std::function<void (double from, double to, int take)> onRange;
    std::function<double()> now;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::bg0);
        const auto* c = cue ? cue() : nullptr;
        if (c == nullptr || c->takes.empty() || c->out <= c->in)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (tr ("Comp: record takes, then drag on a take's lane to choose where it plays"), getLocalBounds().reduced (6), juce::Justification::centred, true);
            return;
        }
        const auto spans = adr::compSpans (*c);
        const auto lh = laneHeight (*c);
        for (std::size_t i = 0; i < c->takes.size(); ++i)
        {
            const auto& t = c->takes[i];
            const auto y = (int) i * lh;
            g.setColour (i % 2 == 0 ? colour::bg1 : colour::bg2);
            g.fillRect (labelWidth, y, getWidth() - labelWidth, lh);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            textcache::drawText (g, "T" + juce::String (t.number) + (t.circled ? " (o)" : ""), juce::Rectangle<int> (4, y, labelWidth - 6, lh), juce::Justification::centredLeft, true);
            for (const auto& s : spans)
                if (s.take == t.number)
                {
                    const auto x0 = xOf (*c, s.from), x1 = xOf (*c, s.to);
                    g.setColour (colour::audioClip.withAlpha (0.85f));
                    g.fillRect (x0, y + 2, std::max (1, x1 - x0), lh - 4);
                }
        }
        if (dragging)
        {
            g.setColour (colour::selectionFill);
            const auto a = std::min (dragFrom, dragTo), b = std::max (dragFrom, dragTo);
            g.fillRect (xOf (*c, a), dragLane * lh, std::max (1, xOf (*c, b) - xOf (*c, a)), lh);
        }
        if (now)
        {
            const auto x = xOf (*c, now());
            if (x >= labelWidth && x < getWidth())
            {
                g.setColour (colour::playhead);
                g.fillRect (x, 0, 1, getHeight());
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* c = cue ? cue() : nullptr;
        if (c == nullptr || c->takes.empty() || e.x < labelWidth)
            return;
        dragLane = juce::jlimit (0, (int) c->takes.size() - 1, e.y / laneHeight (*c));
        dragFrom = dragTo = secondsAt (*c, e.x);
        dragging = true;
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto* c = cue ? cue() : nullptr;
        if (! dragging || c == nullptr)
            return;
        dragTo = secondsAt (*c, e.x);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        const auto* c = cue ? cue() : nullptr;
        if (dragging && c != nullptr && std::abs (dragTo - dragFrom) > 0.01 && onRange && dragLane < (int) c->takes.size())
            onRange (std::min (dragFrom, dragTo), std::max (dragFrom, dragTo), c->takes[(std::size_t) dragLane].number);
        dragging = false;
        repaint();
    }

private:
    static constexpr int labelWidth = 54;
    int laneHeight (const AdrCue& c) const { return std::max (12, getHeight() / std::max (1, (int) c.takes.size())); }
    int xOf (const AdrCue& c, double t) const
    {
        return labelWidth + (int) std::lround ((t - c.in) / (c.out - c.in) * (getWidth() - labelWidth));
    }
    double secondsAt (const AdrCue& c, int x) const
    {
        return juce::jlimit (c.in, c.out, c.in + (double) (x - labelWidth) / std::max (1, getWidth() - labelWidth) * (c.out - c.in));
    }
    bool dragging = false;
    int dragLane = 0;
    double dragFrom = 0.0, dragTo = 0.0;
};

// ---------------------------------------------------------------------------------------------------------------
class AdrPanel final : public juce::Component,
                       private juce::TableListBoxModel,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit AdrPanel (AudioEngine& e) : engine (e)
    {
        auto button = [this] (juce::TextButton& b, const juce::String& text, const juce::String& hint, std::function<void()> click) {
            b.setButtonText (text);
            b.setTooltip (hint);
            b.onClick = std::move (click);
            addAndMakeVisible (b);
        };
        button (rehearseButton, tr ("Rehearse"), tr ("Play the cue with pre-roll and post-roll, the guide on the headphones, without recording"), [this] { start (Mode::rehearse); });
        button (recordButton, tr ("Record"), tr ("Record a take of the cue (pre-roll, then the line, then post-roll)"), [this] { start (Mode::record); });
        button (playButton, tr ("Play take"), tr ("Play the cue with the selected take (or the comp), the guide muted"), [this] { start (Mode::playback); });
        button (stopButton, tr ("Stop"), {}, [this] { engine.stop(); });
        recordButton.setColour (juce::TextButton::buttonColourId, colour::accentMuted);

        auto seconds = [this] (juce::ComboBox& box, const juce::String& hint, std::function<double (const AdrSettings&)> get,
                               std::function<void (AdrSettings&, double)> set) {
            for (int i = 0; i <= 10; ++i)
                box.addItem (juce::String (i) + " s", i + 1);
            box.setTooltip (hint);
            box.onChange = [this, &box, set] {
                const auto v = (double) (box.getSelectedId() - 1);
                editSettings ([set, v] (AdrSettings& s) { set (s, v); });
            };
            getters.push_back ([&box, get] (const AdrSettings& s) { box.setSelectedId ((int) std::lround (get (s)) + 1, juce::dontSendNotification); });
            addAndMakeVisible (box);
        };
        seconds (preBox, tr ("Pre-roll: seconds played before the cue's in point"), [] (const AdrSettings& s) { return s.preRoll; },
                 [] (AdrSettings& s, double v) { s.preRoll = v; });
        seconds (postBox, tr ("Post-roll: seconds played after the cue's out point"), [] (const AdrSettings& s) { return s.postRoll; },
                 [] (AdrSettings& s, double v) { s.postRoll = v; });
        for (auto* l : { &preLabel, &postLabel, &guideLabel, &phonesLabel, &inputLabel })
        {
            l->setFont (font (type::caption));
            l->setColour (juce::Label::textColourId, colour::textSecondary);
            addAndMakeVisible (*l);
        }
        preLabel.setText (tr ("Pre-roll"), juce::dontSendNotification);
        postLabel.setText (tr ("Post-roll"), juce::dontSendNotification);
        guideLabel.setText (tr ("Guide"), juce::dontSendNotification);
        phonesLabel.setText (tr ("Headphones"), juce::dontSendNotification);
        inputLabel.setText (tr ("Record through"), juce::dontSendNotification);

        auto toggle = [this] (juce::ToggleButton& b, const juce::String& text, const juce::String& hint, bool AdrSettings::*field) {
            b.setButtonText (text);
            b.setTooltip (hint);
            b.onClick = [this, &b, field] {
                const auto on = b.getToggleState();
                editSettings ([field, on] (AdrSettings& s) { s.*field = on; });
            };
            getters.push_back ([&b, field] (const AdrSettings& s) { b.setToggleState (s.*field, juce::dontSendNotification); });
            addAndMakeVisible (b);
        };
        toggle (beepsToggle, tr ("Beeps"), tr ("Three beeps one second apart before the in point (to the headphones)"), &AdrSettings::beeps);
        toggle (streamerToggle, tr ("Streamer"), tr ("A bar crosses the video windows and reaches the right edge at the in point"), &AdrSettings::streamer);
        toggle (punchToggle, tr ("Punch"), tr ("A flash on the video windows at the in point"), &AdrSettings::punch);
        toggle (cycleToggle, tr ("Cycle"), tr ("Record the cue again and again until stopped: one take per pass"), &AdrSettings::cycle);

        for (auto* box : { &guideBox, &phonesBox, &inputBox })
            addAndMakeVisible (*box);
        guideBox.setTooltip (tr ("The mixer insert of the original dialogue: heard on the headphones only while rehearsing and recording"));
        phonesBox.setTooltip (tr ("The device output pair of the actor's headphones (the guide and the beeps)"));
        inputBox.setTooltip (tr ("The mixer insert the actor is recorded through (armed for the take)"));
        guideBox.onChange = [this] { editSettings ([v = guideBox.getSelectedId() - 2] (AdrSettings& s) { s.guideInsert = v; }); };
        phonesBox.onChange = [this] { editSettings ([v = phonesBox.getSelectedId() - 2] (AdrSettings& s) { s.headphoneOutput = v; }); };
        inputBox.onChange = [this] { editSettings ([v = inputBox.getSelectedId() - 2] (AdrSettings& s) { s.recordInsert = v; }); };

        table.setModel (this);
        auto& h = table.getHeader();
        h.addColumn (tr ("Cue"), 1, 60);
        h.addColumn (tr ("In"), 2, 100);
        h.addColumn (tr ("Out"), 3, 100);
        h.addColumn (tr ("Character"), 4, 100);
        h.addColumn (tr ("Line"), 5, 260);
        h.addColumn (tr ("Status"), 6, 80);
        h.addColumn (tr ("Takes"), 7, 60);
        table.setColour (juce::ListBox::backgroundColourId, colour::bg0);
        table.setRowHeight (22);
        addAndMakeVisible (table);

        button (addButton, tr ("Add cue"), tr ("A new cue at the play position (2 s long)"), [this] { addCue(); });
        button (inButton, tr ("Set in"), tr ("The selected cue's in point at the play position"), [this] { setEdge (true); });
        button (outButton, tr ("Set out"), tr ("The selected cue's out point at the play position"), [this] { setEdge (false); });
        button (deleteButton, tr ("Delete cue"), {}, [this] { deleteCue(); });
        button (importButton, tr ("Import CSV..."), tr ("Cues from a spreadsheet (Cue, In, Out, Character, Line, Notes, Status)"), [this] { importCsv(); });
        button (exportButton, tr ("Export CSV..."), {}, [this] { exportCsv(); });

        lineEditor.setMultiLine (true, true);
        lineEditor.setReturnKeyStartsNewLine (true);
        lineEditor.setFont (juce::FontOptions (18.0f));
        lineEditor.setTextToShowWhenEmpty (tr ("The line (what the character says)"), colour::textDisabled);
        lineEditor.onFocusLost = [this] { editCue ([t = lineEditor.getText().toStdString()] (AdrCue& c) { c.line = t; }); };
        addAndMakeVisible (lineEditor);
        characterEditor.setTextToShowWhenEmpty (tr ("Character"), colour::textDisabled);
        characterEditor.onFocusLost = [this] { editCue ([t = characterEditor.getText().trim().toStdString()] (AdrCue& c) { c.character = t; }); };
        characterEditor.onReturnKey = characterEditor.onFocusLost;
        addAndMakeVisible (characterEditor);
        notesEditor.setTextToShowWhenEmpty (tr ("Notes"), colour::textDisabled);
        notesEditor.onFocusLost = [this] { editCue ([t = notesEditor.getText().toStdString()] (AdrCue& c) { c.notes = t; }); };
        notesEditor.onReturnKey = notesEditor.onFocusLost;
        addAndMakeVisible (notesEditor);

        takes.setModel (&takesModel);
        takes.setRowHeight (22);
        takes.setColour (juce::ListBox::backgroundColourId, colour::bg0);
        addAndMakeVisible (takes);
        button (useButton, tr ("Use take"), tr ("The selected take plays the cue (the others are muted)"), [this] { useTake (selectedTake()); });
        button (circleButton, tr ("Circle"), tr ("Mark the take as the pick"), [this] {
            editTake ([] (AdrTake& t) { t.circled = ! t.circled; });
        });
        button (rateButton, tr ("Rate"), tr ("Stars: one more each click (after five: none)"), [this] {
            editTake ([] (AdrTake& t) { t.rating = (t.rating + 1) % 6; });
        });
        button (renameButton, tr ("Rename"), {}, [this] { renameTake(); });
        button (deleteTakeButton, tr ("Delete take"), tr ("Removes the take and its clip from the Playlist"), [this] { deleteTake(); });
        button (alignButton, tr ("Align to the guide"), tr ("A new take: the selected take time-aligned to the guide (offline; the original stays)"), [this] { alignTake(); });
        button (compButton, tr ("Apply the comp"), tr ("Puts the comp on the Playlist: each take where the comp strip says, the takes muted"), [this] { applyComp(); });
        comp.cue = [this] { return currentCue(); };
        comp.now = [this] { return songSeconds (engine); };
        comp.onRange = [this] (double from, double to, int take) {
            editCue ([=] (AdrCue& c) {
                if (c.selected == 0)
                    c.selected = take;
                adr::setCompRange (c, from, to, take);
            });
        };
        addAndMakeVisible (comp);
        status.setFont (font (type::caption));
        status.setColour (juce::Label::textColourId, colour::textSecondary);
        addAndMakeVisible (status);

        engine.addChangeListener (this);
        refresh();
        installOverlay();
        startTimerHz (30);
        setSize (1100, 640);
    }

    ~AdrPanel() override
    {
        if (mode != Mode::idle)
            finish();
        videotracks::setOverlay ({});
        engine.removeChangeListener (this);
        table.setModel (nullptr);
        takes.setModel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::bg2);
        g.setColour (colour::bg3);
        g.fillRect (0, 0, getWidth(), 76);
        if (mode != Mode::idle)
        {
            g.setColour (mode == Mode::record ? colour::record : colour::info);
            g.fillRect (0, 74, getWidth(), 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto bar = r.removeFromTop (30);
        for (auto* b : { &rehearseButton, &recordButton, &playButton, &stopButton })
        {
            b->setBounds (bar.removeFromLeft (92));
            bar.removeFromLeft (4);
        }
        bar.removeFromLeft (12);
        preLabel.setBounds (bar.removeFromLeft (56));
        preBox.setBounds (bar.removeFromLeft (64));
        bar.removeFromLeft (6);
        postLabel.setBounds (bar.removeFromLeft (60));
        postBox.setBounds (bar.removeFromLeft (64));
        bar.removeFromLeft (12);
        for (auto* t : { &beepsToggle, &streamerToggle, &punchToggle, &cycleToggle })
            t->setBounds (bar.removeFromLeft (86));
        r.removeFromTop (6);
        auto bar2 = r.removeFromTop (28);
        guideLabel.setBounds (bar2.removeFromLeft (50));
        guideBox.setBounds (bar2.removeFromLeft (200));
        bar2.removeFromLeft (10);
        phonesLabel.setBounds (bar2.removeFromLeft (80));
        phonesBox.setBounds (bar2.removeFromLeft (200));
        bar2.removeFromLeft (10);
        inputLabel.setBounds (bar2.removeFromLeft (100));
        inputBox.setBounds (bar2.removeFromLeft (200));
        r.removeFromTop (10);
        status.setBounds (r.removeFromBottom (20));
        auto right = r.removeFromRight (juce::jmax (340, r.getWidth() * 2 / 5));
        r.removeFromRight (8);
        auto buttons = r.removeFromBottom (28);
        for (auto* b : { &addButton, &inButton, &outButton, &deleteButton, &importButton, &exportButton })
        {
            b->setBounds (buttons.removeFromLeft (juce::jmin (100, buttons.getWidth())));
            buttons.removeFromLeft (4);
        }
        r.removeFromBottom (4);
        table.setBounds (r);
        auto top = right.removeFromTop (24);
        characterEditor.setBounds (top.removeFromLeft (top.getWidth() / 3));
        top.removeFromLeft (4);
        notesEditor.setBounds (top);
        right.removeFromTop (4);
        lineEditor.setBounds (right.removeFromTop (80));
        right.removeFromTop (6);
        auto takeButtons = right.removeFromBottom (28);
        const auto bw = takeButtons.getWidth() / 4;
        for (auto* b : { &useButton, &circleButton, &rateButton, &renameButton })
            b->setBounds (takeButtons.removeFromLeft (bw).reduced (2, 0));
        right.removeFromBottom (4);
        auto takeButtons2 = right.removeFromBottom (28);
        const auto bw2 = takeButtons2.getWidth() / 3;
        for (auto* b : { &deleteTakeButton, &alignButton, &compButton })
            b->setBounds (takeButtons2.removeFromLeft (bw2).reduced (2, 0));
        right.removeFromBottom (6);
        comp.setBounds (right.removeFromBottom (juce::jlimit (60, 140, right.getHeight() / 3)));
        right.removeFromBottom (6);
        takes.setBounds (right);
    }

private:
    enum class Mode { idle, rehearse, record, playback };

    // --- the model ------------------------------------------------------------------------------------------------
    const AdrCue* currentCue() const
    {
        const auto& a = engine.getSong().adr;
        return a.current >= 0 && a.current < (int) a.cues.size() ? &a.cues[(std::size_t) a.current] : nullptr;
    }

    void editCue (std::function<void (AdrCue&)> change)
    {
        const auto* c = currentCue();
        if (c == nullptr)
            return;
        auto copy = *c;
        change (copy);
        if (copy == *c)
            return;
        engine.editSong ([copy] (Song& s) {
            if (s.adr.current >= 0 && s.adr.current < (int) s.adr.cues.size())
                s.adr.cues[(std::size_t) s.adr.current] = copy;
        });
    }

    void editSettings (std::function<void (AdrSettings&)> change)
    {
        auto copy = engine.getSong().adr.settings;
        change (copy);
        if (! (copy == engine.getSong().adr.settings))
            engine.editSong ([copy] (Song& s) { s.adr.settings = copy; });
    }

    int selectedTake() const
    {
        const auto* c = currentCue();
        const auto row = takes.getSelectedRow();
        return c != nullptr && row >= 0 && row < (int) c->takes.size() ? c->takes[(std::size_t) row].number : 0;
    }

    void editTake (std::function<void (AdrTake&)> change)
    {
        const auto n = selectedTake();
        editCue ([n, change] (AdrCue& c) {
            for (auto& t : c.takes)
                if (t.number == n)
                    change (t);
        });
    }

    // --- cue list ---------------------------------------------------------------------------------------------------
    void addCue()
    {
        const auto t = songSeconds (engine);
        engine.editSong ([t] (Song& s) {
            AdrCue c;
            c.id = adr::nextId (s.adr.cues);
            c.in = videotrack::snapToFrame (s.video, t);
            c.out = c.in + 2.0;
            s.adr.cues.push_back (c);
            adr::sortByTime (s.adr.cues);
            for (std::size_t i = 0; i < s.adr.cues.size(); ++i)
                if (s.adr.cues[i].id == c.id)
                    s.adr.current = (int) i;
        });
    }

    void setEdge (bool in)
    {
        const auto t = videotrack::snapToFrame (engine.getSong().video, songSeconds (engine));
        editCue ([in, t] (AdrCue& c) {
            if (in)
            {
                c.in = t;
                if (c.out <= c.in)
                    c.out = c.in + 2.0;
            }
            else if (t > c.in)
                c.out = t;
        });
    }

    void deleteCue()
    {
        engine.editSong ([] (Song& s) {
            if (s.adr.current >= 0 && s.adr.current < (int) s.adr.cues.size())
                s.adr.cues.erase (s.adr.cues.begin() + s.adr.current);
            s.adr.current = juce::jlimit (0, juce::jmax (0, (int) s.adr.cues.size() - 1), s.adr.current);
        });
    }

    void importCsv()
    {
        chooser = ui::makeFileChooser (tr ("Import CSV..."), juce::File(), "*.csv;*.txt;*.tsv");
        juce::Component::SafePointer<AdrPanel> self (this);
        ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc) {
            if (self == nullptr || ! fc.getResult().existsAsFile())
                return;
            const auto& s = self->engine.getSong();
            auto result = adr::fromCsv (fc.getResult().loadFileAsString().toStdString(), s.video.rate, s.video.startTimecode);
            if (! result.error.empty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Import CSV..."),
                                                        tr ("No \"In\" column was found in this file (Cue, In, Out, Character, Line, Notes, Status)."));
                return;
            }
            self->engine.editSong ([cues = result.cues] (Song& s2) {
                for (auto c : cues)
                {
                    if (std::any_of (s2.adr.cues.begin(), s2.adr.cues.end(), [&] (const AdrCue& o) { return o.id == c.id; }))
                        c.id = adr::nextId (s2.adr.cues);
                    s2.adr.cues.push_back (c);
                }
                adr::sortByTime (s2.adr.cues);
            });
            self->status.setText (tr ("{0} cues imported", { juce::String ((int) result.cues.size()) })
                                      + (result.skipped > 0 ? ", " + tr ("{0} rows skipped (no in point)", { juce::String (result.skipped) }) : juce::String()),
                                  juce::dontSendNotification);
        });
    }

    void exportCsv()
    {
        chooser = ui::makeFileChooser (tr ("Export CSV..."), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ADR cues.csv"), "*.csv");
        juce::Component::SafePointer<AdrPanel> self (this);
        ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [self] (const juce::FileChooser& fc) {
            if (self == nullptr || fc.getResult() == juce::File())
                return;
            const auto& s = self->engine.getSong();
            auto file = fc.getResult();
            if (! file.hasFileExtension ("csv"))
                file = file.withFileExtension ("csv");
            file.replaceWithText (juce::String::fromUTF8 (adr::toCsv (s.adr.cues, s.video.rate, s.video.startTimecode).c_str()), false, false, "\r\n");
            self->status.setText (tr ("Saved {0}", { file.getFileName() }), juce::dontSendNotification);
        });
    }

    // --- TableListBoxModel (cues) / ListBoxModel (takes) ------------------------------------------------------------
    int getNumRows() override { return (int) engine.getSong().adr.cues.size(); }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        const auto& cues = engine.getSong().adr.cues;
        g.fillAll (selected ? colour::accentMuted : row % 2 == 0 ? colour::bg0 : colour::bg1);
        if (row >= 0 && row < (int) cues.size() && row == adr::cueAt (cues, songSeconds (engine)))
        {
            g.setColour (colour::playhead);
            g.fillRect (0, 0, 3, g.getClipBounds().getHeight());
        }
    }

    void paintCell (juce::Graphics& g, int row, int column, int w, int h, bool) override
    {
        const auto& s = engine.getSong();
        if (row < 0 || row >= (int) s.adr.cues.size())
            return;
        const auto& c = s.adr.cues[(std::size_t) row];
        juce::String text;
        switch (column)
        {
            case 1: text = juce::String::fromUTF8 (c.id.c_str()); break;
            case 2: text = tc (s, c.in); break;
            case 3: text = tc (s, c.out); break;
            case 4: text = juce::String::fromUTF8 (c.character.c_str()); break;
            case 5: text = juce::String::fromUTF8 (c.line.c_str()).replaceCharacters ("\r\n", "  "); break;
            case 6: text = tr (adr::statusName (c.status)); break;
            case 7: text = c.takes.empty() ? juce::String() : juce::String ((int) c.takes.size()); break;
            default: break;
        }
        g.setColour (c.status == AdrCue::Status::omit ? colour::textDisabled : colour::textPrimary);
        g.setFont (font (type::body)); // (the UI font's digits are tabular: the timecodes line up)
        g.drawText (text, 4, 0, w - 6, h, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int row) override
    {
        if (row >= 0 && row != engine.getSong().adr.current)
            engine.editSong ([row] (Song& s) { s.adr.current = row; }, false);
    }

    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override
    {
        const auto& s = engine.getSong();
        if (row < 0 || row >= (int) s.adr.cues.size())
            return;
        const auto& c = s.adr.cues[(std::size_t) row];
        if (column == 6)
        {
            juce::PopupMenu m;
            for (const auto st : { AdrCue::Status::todo, AdrCue::Status::recorded, AdrCue::Status::approved, AdrCue::Status::omit })
                m.addItem (tr (adr::statusName (st)), true, c.status == st, [this, st] { editCue ([st] (AdrCue& cue) { cue.status = st; }); });
            m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            return;
        }
        if (column == 2 || column == 3)
        {
            // a double-click on a time goes there (and types it with Alt)
            engine.seek ((double) ticksAt (s, column == 2 ? c.in : c.out));
            return;
        }
        const juce::String current = column == 1 ? juce::String::fromUTF8 (c.id.c_str()) : column == 4 ? juce::String::fromUTF8 (c.character.c_str())
                                                                                                         : juce::String::fromUTF8 (c.line.c_str());
        auto* w = new juce::AlertWindow (column == 1 ? tr ("Cue") : column == 4 ? tr ("Character") : tr ("Line"), {}, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("v", current);
        w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, column] (int r) {
            if (r != 1)
                return;
            const auto v = w->getTextEditorContents ("v").toStdString();
            editCue ([v, column] (AdrCue& cue) {
                (column == 1 ? cue.id : column == 4 ? cue.character : cue.line) = v;
            });
        }), true);
    }

    void cellClicked (int row, int column, const juce::MouseEvent& e) override
    {
        if (e.mods.isAltDown() && (column == 2 || column == 3))
        {
            const auto& s = engine.getSong();
            if (row < 0 || row >= (int) s.adr.cues.size())
                return;
            auto* w = new juce::AlertWindow (column == 2 ? tr ("In") : tr ("Out"), tr ("Timecode (hours:minutes:seconds:frames)"), juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("v", tc (s, column == 2 ? s.adr.cues[(std::size_t) row].in : s.adr.cues[(std::size_t) row].out));
            w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, column] (int r) {
                if (r != 1)
                    return;
                if (const auto t = parseTc (engine.getSong(), w->getTextEditorContents ("v")))
                    editCue ([t, column] (AdrCue& c) {
                        if (column == 2)
                            c.in = std::max (0.0, *t);
                        else
                            c.out = *t;
                        if (c.out <= c.in)
                            c.out = c.in + 2.0;
                    });
            }), true);
        }
    }

    // the takes list's model
    struct TakesModel final : juce::ListBoxModel
    {
        AdrPanel& owner;
        explicit TakesModel (AdrPanel& o) : owner (o) {}
        int getNumRows() override
        {
            const auto* c = owner.currentCue();
            return c != nullptr ? (int) c->takes.size() : 0;
        }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            const auto* c = owner.currentCue();
            if (c == nullptr || row < 0 || row >= (int) c->takes.size())
                return;
            const auto& t = c->takes[(std::size_t) row];
            g.fillAll (selected ? colour::accentMuted : row % 2 == 0 ? colour::bg0 : colour::bg1);
            const auto plays = t.number == c->selected;
            g.setColour (plays ? colour::ledOn : colour::ledOff);
            g.fillEllipse (6.0f, (float) h * 0.5f - 3.5f, 7.0f, 7.0f);
            g.setColour (colour::textPrimary);
            g.setFont (font (type::body));
            juce::String text = juce::String::fromUTF8 (t.name.c_str());
            if (t.circled)
                text = "(o) " + text;
            g.drawText (text, 20, 0, w - 110, h, juce::Justification::centredLeft, true);
            g.setColour (colour::warning);
            g.drawText (juce::String::repeatedString (juce::String::charToString ((juce::juce_wchar) 0x2605), t.rating), w - 90, 0, 86, h,
                        juce::Justification::centredRight, false);
        }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            const auto* c = owner.currentCue();
            if (c != nullptr && row >= 0 && row < (int) c->takes.size())
                owner.useTake (c->takes[(std::size_t) row].number);
        }
    };
    TakesModel takesModel { *this };

    // --- takes --------------------------------------------------------------------------------------------------------
    void useTake (int number)
    {
        if (number <= 0)
            return;
        const auto* c = currentCue();
        if (c == nullptr)
            return;
        std::vector<std::pair<std::uint64_t, bool>> mutes;
        for (const auto& t : c->takes)
            mutes.emplace_back (t.clip, t.number != number);
        engine.editSong ([mutes, number] (Song& s) {
            auto& cue = s.adr.cues[(std::size_t) s.adr.current];
            cue.selected = number;
            cue.comp.clear();
            for (const auto& [uid, muted] : mutes)
                if (const auto i = clipIndexOf (s, uid); i >= 0)
                    s.playlist[(std::size_t) i].muted = muted;
            for (const auto uid : cue.compClips)
                if (const auto i = clipIndexOf (s, uid); i >= 0)
                    s.playlist[(std::size_t) i].muted = true;
        });
    }

    void renameTake()
    {
        const auto n = selectedTake();
        const auto* c = currentCue();
        if (n <= 0 || c == nullptr)
            return;
        const auto it = std::find_if (c->takes.begin(), c->takes.end(), [n] (const AdrTake& t) { return t.number == n; });
        auto* w = new juce::AlertWindow (tr ("Rename"), {}, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("v", juce::String::fromUTF8 (it->name.c_str()));
        w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, n, clip = it->clip] (int r) {
            if (r != 1)
                return;
            const auto name = w->getTextEditorContents ("v").trim().toStdString();
            if (name.empty())
                return;
            engine.editSong ([n, name, clip] (Song& s) {
                for (auto& t : s.adr.cues[(std::size_t) s.adr.current].takes)
                    if (t.number == n)
                        t.name = name;
                if (const auto i = clipIndexOf (s, clip); i >= 0 && s.playlist[(std::size_t) i].channel >= 0)
                    s.channels[(std::size_t) s.playlist[(std::size_t) i].channel].name = name;
            });
        }), true);
    }

    void deleteTake()
    {
        const auto n = selectedTake();
        if (n <= 0)
            return;
        engine.editSong ([n] (Song& s) {
            auto& cue = s.adr.cues[(std::size_t) s.adr.current];
            const auto it = std::find_if (cue.takes.begin(), cue.takes.end(), [n] (const AdrTake& t) { return t.number == n; });
            if (it == cue.takes.end())
                return;
            if (const auto i = clipIndexOf (s, it->clip); i >= 0)
                s.playlist.erase (s.playlist.begin() + i);
            cue.takes.erase (it);
            cue.comp.erase (std::remove_if (cue.comp.begin(), cue.comp.end(), [n] (const AdrCompPart& p) { return p.take == n; }), cue.comp.end());
            if (cue.selected == n)
                cue.selected = cue.takes.empty() ? 0 : cue.takes.back().number;
        });
    }

    // The comp onto the Playlist: copies of the takes' clips cut to their spans, on the first take's track; the takes
    // muted. The previous comp's clips are replaced.
    void applyComp()
    {
        const auto* c = currentCue();
        if (c == nullptr || c->takes.empty())
            return;
        const auto spans = adr::compSpans (*c);
        engine.editSong ([spans] (Song& s) {
            auto& cue = s.adr.cues[(std::size_t) s.adr.current];
            for (const auto uid : cue.compClips)
                if (const auto i = clipIndexOf (s, uid); i >= 0)
                    s.playlist.erase (s.playlist.begin() + i);
            cue.compClips.clear();
            int track = -1;
            std::vector<Clip> made;
            const auto fade = (std::int64_t) std::llround (0.008 * std::max (1.0, s.bpm) / 60.0 * ppq); // 8 ms crossfades
            for (const auto& t : cue.takes)
            {
                const auto i = clipIndexOf (s, t.clip);
                if (i < 0)
                    continue;
                auto& take = s.playlist[(std::size_t) i];
                take.muted = true;
                if (track < 0)
                    track = take.track;
                for (const auto& sp : spans)
                    if (sp.take == t.number)
                    {
                        const auto a = std::max (take.start, ticksAt (s, sp.from)), b = std::min (take.end(), ticksAt (s, sp.to));
                        if (b <= a)
                            continue;
                        auto part = take;
                        part.muted = false;
                        part.start = a;
                        part.length = b - a;
                        part.offset = take.offset + (a - take.start);
                        part.fadeIn = part.fadeOut = std::min (fade, part.length / 4);
                        part.uid = uids::next (s);
                        made.push_back (part);
                    }
            }
            for (auto& p : made)
            {
                p.track = track; // (the comp plays on the first take's track, over its muted take)
                cue.compClips.push_back (p.uid);
                s.playlist.push_back (p);
            }
        });
        status.setText (tr ("The comp is on the Playlist ({0} parts)", { juce::String ((int) spans.size()) }), juce::dontSendNotification);
    }

    void alignTake()
    {
        const auto n = selectedTake();
        const auto* c = currentCue();
        const auto& s = engine.getSong();
        if (n <= 0 || c == nullptr)
            return;
        const auto guideInsert = s.adr.settings.guideInsert;
        // the guide: the audio clips playing through the guide insert over the cue
        const Clip* guideClip = nullptr;
        for (const auto& clip : s.playlist)
            if (clip.kind == ClipKind::audio && clip.channel >= 0 && clip.channel < (int) s.channels.size()
                && s.channels[(std::size_t) clip.channel].mixerTrack == guideInsert && s.secondsForTicks (clip.start) < c->out
                && s.secondsForTicks (clip.end()) > c->in)
                guideClip = &clip;
        const auto it = std::find_if (c->takes.begin(), c->takes.end(), [n] (const AdrTake& t) { return t.number == n; });
        const auto ti = clipIndexOf (s, it->clip);
        if (guideClip == nullptr || ti < 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Align to the guide"),
                                                    guideInsert < 1 ? tr ("Choose the guide's mixer insert first (the original dialogue).")
                                                                    : tr ("No guide audio clip plays through the guide insert during this cue."));
            return;
        }
        // a little before and after the cue (the take may start or end early)
        const auto from = std::max (0.0, c->in - 0.5), to = c->out + 0.5;
        double gr = 48000.0, tr2 = 48000.0;
        auto guide = clipAudio (engine, s, *guideClip, from, to, gr);
        auto take = clipAudio (engine, s, s.playlist[(std::size_t) ti], from, to, tr2);
        if (guide.empty() || take.empty())
            return;
        const auto cue = *c;
        const auto takeName = adr::takeName (cue, adr::nextTakeNumber (cue)) + "-aligned";
        auto folder = engine.getRecordingFolder();
        if (! folder.isDirectory())
            folder = platform::cacheFolder ("adr");
        folder.createDirectory();
        const auto file = folder.getNonexistentChildFile (juce::String::fromUTF8 (takeName.c_str()), ".wav", false);
        status.setText (tr ("Aligning..."), juce::dontSendNotification);
        juce::Component::SafePointer<AdrPanel> self (this);
        const auto takeTrack = s.playlist[(std::size_t) ti].track, takeChannel = s.playlist[(std::size_t) ti].channel;
        std::thread ([self, guide, take, gr, tr2, from, to, file, takeName, takeTrack, takeChannel] {
            constexpr double analysisRate = 16000.0;
            const auto map = align::align (resample (guide, gr, analysisRate), resample (take, tr2, analysisRate), analysisRate);
            const auto out = align::warp (take, tr2, map, (std::size_t) std::llround ((to - from) * tr2));
            const auto ok = ! map.empty() && writeWav (file, out, tr2);
            juce::MessageManager::callAsync ([self, ok, file, from, to, takeName, takeTrack, takeChannel] {
                if (self == nullptr)
                    return;
                if (! ok)
                {
                    self->status.setText (tr ("The alignment failed"), juce::dontSendNotification);
                    return;
                }
                self->addTakeClip (file, from, to, takeName, takeTrack, takeChannel, true);
                self->status.setText (tr ("Aligned: {0}", { file.getFileName() }), juce::dontSendNotification);
            });
        }).detach();
    }

    // A new take of the current cue from a WAV placed at [from, to) song seconds
    void addTakeClip (const juce::File& file, double from, double to, const std::string& name, int track, int likeChannel, bool aligned)
    {
        if (engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
            return;
        const auto path = file.getFullPathName().toStdString();
        engine.editSong ([=] (Song& s) {
            auto& cue = s.adr.cues[(std::size_t) s.adr.current];
            ChannelDef ch;
            ch.name = name;
            ch.instrument = InstrumentKind::audioClip;
            ch.samplePath = path;
            ch.mixerTrack = likeChannel >= 0 && likeChannel < (int) s.channels.size() ? s.channels[(std::size_t) likeChannel].mixerTrack : s.claimFreeInsert();
            s.channels.push_back (ch);
            Clip clip;
            clip.kind = ClipKind::audio;
            clip.channel = (int) s.channels.size() - 1;
            clip.track = track;
            clip.start = ticksAt (s, from);
            clip.length = std::max<std::int64_t> (1, ticksAt (s, to) - clip.start);
            clip.uid = uids::next (s);
            for (const auto& t : cue.takes)
                if (const auto i = clipIndexOf (s, t.clip); i >= 0)
                    s.playlist[(std::size_t) i].muted = true;
            s.playlist.push_back (clip);
            AdrTake t;
            t.number = adr::nextTakeNumber (cue);
            t.name = name;
            t.clip = clip.uid;
            t.aligned = aligned;
            cue.takes.push_back (t);
            cue.selected = t.number;
            cue.comp.clear();
        });
    }

    // --- the record cycle -------------------------------------------------------------------------------------------
    void start (Mode m)
    {
        if (mode != Mode::idle)
        {
            engine.stop();
            return;
        }
        const auto* c = currentCue();
        if (c == nullptr)
        {
            status.setText (tr ("Add a cue first (Add cue, or Import CSV...)"), juce::dontSendNotification);
            return;
        }
        const auto& s = engine.getSong();
        const auto& st = s.adr.settings;
        cue = *c;
        savedOptions = engine.getRecordOptions();
        savedLoop = { s.loopStart, s.loopEnd };
        savedMode = s.mode;
        before.clear();
        for (const auto& clip : s.playlist)
            before.insert (clip.uid);
        beforeCount = s.playlist.size();
        if (m == Mode::record)
        {
            if (st.recordInsert > 0 && ! engine.isInsertArmed (st.recordInsert))
            {
                engine.setInsertArmed (st.recordInsert, true);
                armedByUs = st.recordInsert;
            }
            if (engine.getArmedInserts().empty())
            {
                status.setText (tr ("Arm a mixer insert for the actor's microphone (or choose \"Record through\")"), juce::dontSendNotification);
                return;
            }
        }
        setMonitoring (m);
        const auto in = ticksAt (s, cue.in), out = ticksAt (s, cue.out);
        mode = m;
        if (m == Mode::record)
        {
            auto o = savedOptions;
            o.recordAudio = true;
            o.recordNotes = false;
            o.recordAutomation = false;
            o.askWhatToRecord = false;
            o.countInBars = 0;
            o.waitForInput = false;
            o.preRollSeconds = st.preRoll;
            o.postRollSeconds = st.cycle ? -1.0 : st.postRoll;
            o.punchLoopRegion = ! st.cycle;
            o.loopRecord = st.cycle;
            o.preRollBars = 0;
            o.postRollBars = 0;
            engine.setRecordOptions (o);
            engine.editSong ([in, out] (Song& s2) {
                s2.loopStart = in;
                s2.loopEnd = out;
                s2.mode = PlayMode::song;
            }, false);
            engine.setRecording (true);
            engine.seek ((double) in);
            engine.startPlayback();
        }
        else
        {
            engine.editSong ([] (Song& s2) { s2.mode = PlayMode::song; }, false);
            engine.seek ((double) ticksAt (s, cue.in - st.preRoll));
            engine.play();
        }
        stopAt = cue.out + st.postRoll;
        wasPlaying = false;
        repaint();
    }

    // The guide to the headphones only (rehearse / record) or muted (playback); the beeps on a temporary track
    void setMonitoring (Mode m)
    {
        const auto& st = engine.getSong().adr.settings;
        restoreGuide.reset();
        restoreBeepInsert.reset();
        beepClips.clear();
        beepChannel = -1;
        auto beepFile = juce::File();
        if (m != Mode::playback && st.beeps)
        {
            beepFile = platform::cacheFolder ("adr").getChildFile ("beep-" + juce::String ((int) st.beepFrequency) + ".wav");
            if (! beepFile.existsAsFile())
            {
                beepFile.getParentDirectory().createDirectory();
                writeWav (beepFile, adr::renderBeep (48000.0, st.beepFrequency), 48000.0);
            }
            if (engine.getSampleLibrary().load (beepFile.getFullPathName()) == nullptr)
                beepFile = juce::File();
        }
        const auto beepPath = beepFile.getFullPathName().toStdString();
        const auto c = cue;
        engine.editSong ([this, m, st, beepPath, c] (Song& s) {
            if (st.guideInsert > 0 && st.guideInsert < (int) s.mixer.size())
            {
                auto& g = s.mixer[(std::size_t) st.guideInsert];
                restoreGuide = g;
                if (m == Mode::playback)
                    g.muted = true;
                else if (st.headphoneOutput >= 0)
                {
                    g.toMaster = false;
                    g.sends.clear();
                    g.outputChannel = st.headphoneOutput;
                    g.outputMode = OutputMode::stereo;
                }
            }
            if (! beepPath.empty())
            {
                ChannelDef ch;
                ch.name = "ADR beeps";
                ch.instrument = InstrumentKind::audioClip;
                ch.samplePath = beepPath;
                ch.mixerTrack = s.claimFreeInsert();
                if (ch.mixerTrack > 0 && ch.mixerTrack < (int) s.mixer.size())
                    restoreBeepInsert = s.mixer[(std::size_t) ch.mixerTrack];
                if (ch.mixerTrack > 0 && st.headphoneOutput >= 0)
                {
                    auto& mt = s.mixer[(std::size_t) ch.mixerTrack];
                    mt.toMaster = false;
                    mt.outputChannel = st.headphoneOutput;
                }
                s.channels.push_back (ch);
                beepChannel = (int) s.channels.size() - 1;
                beepInsert = ch.mixerTrack;
                int track = 0;
                for (const auto& clip : s.playlist)
                    track = std::max (track, clip.track + 1);
                const auto len = std::max<std::int64_t> (1, ticksAt (s, adr::beepLength));
                for (const auto t : adr::beepTimes (c.in))
                    if (t >= 0.0)
                    {
                        Clip clip;
                        clip.kind = ClipKind::audio;
                        clip.channel = beepChannel;
                        clip.track = track;
                        clip.start = ticksAt (s, t);
                        clip.length = len;
                        clip.uid = uids::next (s);
                        beepClips.push_back (clip.uid);
                        s.playlist.push_back (clip);
                    }
            }
        }, false);
    }

    void unsetMonitoring()
    {
        const auto st = engine.getSong().adr.settings;
        engine.editSong ([this, st] (Song& s) {
            if (restoreGuide && st.guideInsert > 0 && st.guideInsert < (int) s.mixer.size())
                s.mixer[(std::size_t) st.guideInsert] = *restoreGuide;
            for (const auto uid : beepClips)
                if (const auto i = clipIndexOf (s, uid); i >= 0)
                    s.playlist.erase (s.playlist.begin() + i);
            if (beepChannel >= 0 && beepChannel < (int) s.channels.size() && s.channels[(std::size_t) beepChannel].name == "ADR beeps")
            {
                s.removeChannel (beepChannel);
                if (restoreBeepInsert && beepInsert > 0 && beepInsert < (int) s.mixer.size())
                    s.mixer[(std::size_t) beepInsert] = *restoreBeepInsert;
            }
        }, false);
        restoreGuide.reset();
        restoreBeepInsert.reset();
        beepClips.clear();
        beepChannel = -1;
    }

    void finish()
    {
        const auto m = mode;
        mode = Mode::idle;
        if (m == Mode::record)
            engine.setRecording (false);
        engine.setRecordOptions (savedOptions);
        unsetMonitoring();
        if (armedByUs > 0)
        {
            engine.setInsertArmed (armedByUs, false);
            armedByUs = -1;
        }
        engine.editSong ([l = savedLoop, md = savedMode] (Song& s) {
            s.loopStart = l.first;
            s.loopEnd = l.second;
            s.mode = md;
        }, false);
        if (m == Mode::record)
            collectTakes();
        repaint();
    }

    // The clips the take added: each one a take of the cue (named, the others muted)
    void collectTakes()
    {
        const auto& s = engine.getSong();
        std::vector<int> fresh;
        for (std::size_t i = 0; i < s.playlist.size(); ++i)
            if (s.playlist[i].kind == ClipKind::audio && (s.playlist[i].uid == 0 ? i >= beforeCount : before.count (s.playlist[i].uid) == 0))
                fresh.push_back ((int) i);
        if (fresh.empty())
        {
            status.setText (tr ("No take was recorded (is the insert's input set?)"), juce::dontSendNotification);
            return;
        }
        const auto id = cue.id;
        engine.editSong ([fresh, id] (Song& s2) {
            auto it = std::find_if (s2.adr.cues.begin(), s2.adr.cues.end(), [&] (const AdrCue& c) { return c.id == id; });
            if (it == s2.adr.cues.end())
                return;
            auto& c = *it;
            for (const auto& t : c.takes)
                if (const auto i = clipIndexOf (s2, t.clip); i >= 0)
                    s2.playlist[(std::size_t) i].muted = true;
            for (std::size_t k = 0; k < fresh.size(); ++k)
            {
                auto& clip = s2.playlist[(std::size_t) fresh[k]];
                if (clip.uid == 0)
                    clip.uid = uids::next (s2);
                AdrTake t;
                t.number = adr::nextTakeNumber (c);
                t.name = adr::takeName (c, t.number);
                t.clip = clip.uid;
                if (clip.channel >= 0 && clip.channel < (int) s2.channels.size())
                    s2.channels[(std::size_t) clip.channel].name = t.name;
                clip.muted = k + 1 < fresh.size(); // (cycle: the last pass plays)
                c.takes.push_back (t);
                c.selected = t.number;
            }
            c.comp.clear();
            if (c.status == AdrCue::Status::todo)
                c.status = AdrCue::Status::recorded;
        });
        status.setText (tr ("{0} new takes", { juce::String ((int) fresh.size()) }), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const auto playing = engine.getTransport().playing.load();
        if (mode != Mode::idle)
        {
            if (playing)
                wasPlaying = true;
            if (mode != Mode::record && playing && songSeconds (engine) >= stopAt)
                engine.stop();
            if (wasPlaying && ! playing)
                finish();
        }
        comp.repaint();
        const auto now = adr::cueAt (engine.getSong().adr.cues, songSeconds (engine));
        if (now != shownCueAtPlayhead)
        {
            shownCueAtPlayhead = now;
            table.repaint();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void refresh()
    {
        const auto& s = engine.getSong();
        for (auto& g : getters)
            g (s.adr.settings);
        // the insert and output choices
        auto fillInserts = [&] (juce::ComboBox& box, int current) {
            box.clear (juce::dontSendNotification);
            box.addItem (tr ("(none)"), 1);
            for (int i = 1; i < (int) s.mixer.size(); ++i)
            {
                const auto& n = s.mixer[(std::size_t) i].name;
                box.addItem (tr ("Insert {0}", { juce::String (i) }) + (n.empty() ? juce::String() : " - " + juce::String::fromUTF8 (n.c_str())), i + 2);
            }
            box.setSelectedId (current >= 1 ? current + 2 : 1, juce::dontSendNotification);
        };
        fillInserts (guideBox, s.adr.settings.guideInsert);
        fillInserts (inputBox, s.adr.settings.recordInsert);
        phonesBox.clear (juce::dontSendNotification);
        phonesBox.addItem (tr ("(none)"), 1);
        const auto names = engine.getOutputChannelNames();
        for (int i = 0; i + 1 < (int) names.size(); i += 2)
            phonesBox.addItem (juce::String::fromUTF8 (names[(std::size_t) i].c_str()) + " + " + juce::String::fromUTF8 (names[(std::size_t) i + 1].c_str()), i + 2);
        phonesBox.setSelectedId (s.adr.settings.headphoneOutput >= 0 ? s.adr.settings.headphoneOutput + 2 : 1, juce::dontSendNotification);

        table.updateContent();
        if (s.adr.current >= 0 && s.adr.current < (int) s.adr.cues.size() && table.getSelectedRow() != s.adr.current)
            table.selectRow (s.adr.current, true, true);
        table.repaint();
        const auto* c = currentCue();
        if (! lineEditor.hasKeyboardFocus (true))
            lineEditor.setText (c != nullptr ? juce::String::fromUTF8 (c->line.c_str()) : juce::String(), false);
        if (! characterEditor.hasKeyboardFocus (true))
            characterEditor.setText (c != nullptr ? juce::String::fromUTF8 (c->character.c_str()) : juce::String(), false);
        if (! notesEditor.hasKeyboardFocus (true))
            notesEditor.setText (c != nullptr ? juce::String::fromUTF8 (c->notes.c_str()) : juce::String(), false);
        takes.updateContent();
        takes.repaint();
        comp.repaint();
        for (auto* comp2 : std::initializer_list<juce::Component*> { &lineEditor, &characterEditor, &notesEditor, &inButton, &outButton, &deleteButton,
                                                                     &rehearseButton, &recordButton, &playButton })
            comp2->setEnabled (c != nullptr);
    }

    // The streamer, the punch and the line over the video windows
    void installOverlay()
    {
        juce::Component::SafePointer<AdrPanel> self (this);
        videotracks::setOverlay ([self] (juce::Graphics& g, juce::Rectangle<int> picture, double t) {
            if (self == nullptr)
                return;
            const auto& s = self->engine.getSong();
            const auto& a = s.adr;
            const auto frame = 1.0 / s.video.rate.fps();
            // the cue being worked on (or, idle, the next one at the play position)
            const AdrCue* c = self->mode != Mode::idle ? &self->cue : (a.current >= 0 && a.current < (int) a.cues.size() ? &a.cues[(std::size_t) a.current] : nullptr);
            if (c == nullptr)
                return;
            const auto sig = adr::signalsAt (*c, a.settings, t, frame);
            if (sig.streamer)
            {
                // a vertical band a twentieth of the picture wide, left to right, reaching the right edge at the in point
                const auto w = std::max (6, picture.getWidth() / 40);
                const auto x = picture.getX() + (int) std::lround (*sig.streamer * (picture.getWidth() - w));
                g.setColour (juce::Colour (a.settings.streamerColour).withAlpha (0.85f));
                g.fillRect (x, picture.getY(), w, picture.getHeight());
            }
            if (sig.punch)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.fillEllipse (picture.toFloat().withSizeKeepingCentre ((float) picture.getHeight() * 0.3f, (float) picture.getHeight() * 0.3f));
            }
            if (sig.inLine || (t >= c->in - a.settings.preRoll && t < c->in))
            {
                // the line along the bottom, as a cue card (dim before the in point)
                auto band = picture.withTrimmedTop (picture.getHeight() * 3 / 4).reduced (8);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRect (band);
                g.setColour (sig.inLine ? juce::Colours::white : juce::Colours::white.withAlpha (0.55f));
                g.setFont (juce::FontOptions (juce::jlimit (12.0f, 30.0f, (float) picture.getHeight() * 0.05f)));
                auto text = juce::String::fromUTF8 (c->line.c_str());
                if (! c->character.empty())
                    text = juce::String::fromUTF8 (c->character.c_str()).toUpperCase() + ": " + text;
                g.drawFittedText (text, band.reduced (8, 2), juce::Justification::centred, 3);
            }
            if (self->mode == Mode::record && t >= c->in && t < c->out)
            {
                g.setColour (colour::record);
                g.fillEllipse ((float) picture.getRight() - 24.0f, (float) picture.getY() + 10.0f, 12.0f, 12.0f);
            }
        });
    }

    AudioEngine& engine;
    Mode mode = Mode::idle;
    AdrCue cue;
    AudioEngine::RecordOptions savedOptions;
    std::pair<std::int64_t, std::int64_t> savedLoop;
    PlayMode savedMode = PlayMode::song;
    std::set<std::uint64_t> before;
    std::size_t beforeCount = 0;
    std::optional<MixerTrack> restoreGuide, restoreBeepInsert;
    std::vector<std::uint64_t> beepClips;
    int beepChannel = -1, beepInsert = -1, armedByUs = -1, shownCueAtPlayhead = -2;
    double stopAt = 0.0;
    bool wasPlaying = false;
    std::vector<std::function<void (const AdrSettings&)>> getters;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton rehearseButton, recordButton, playButton, stopButton;
    juce::ComboBox preBox, postBox, guideBox, phonesBox, inputBox;
    juce::Label preLabel, postLabel, guideLabel, phonesLabel, inputLabel, status;
    juce::ToggleButton beepsToggle, streamerToggle, punchToggle, cycleToggle;
    juce::TableListBox table;
    juce::TextButton addButton, inButton, outButton, deleteButton, importButton, exportButton;
    juce::TextEditor lineEditor, characterEditor, notesEditor;
    juce::ListBox takes;
    juce::TextButton useButton, circleButton, rateButton, renameButton, deleteTakeButton, alignButton, compButton;
    CompStrip comp;
};

class AdrWindow final : public juce::DocumentWindow
{
public:
    explicit AdrWindow (AudioEngine& e) : juce::DocumentWindow (tr ("ADR (dialogue replacement)"), colour::bg2, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setContentOwned (new AdrPanel (e), true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([] { window().reset(); });
    }
    static std::unique_ptr<AdrWindow>& window()
    {
        static std::unique_ptr<AdrWindow> w;
        return w;
    }
};
} // namespace

namespace adrwindow
{
void show (AudioEngine& engine)
{
    auto& w = AdrWindow::window();
    if (w == nullptr)
        w = std::make_unique<AdrWindow> (engine);
    w->toFront (true);
}

std::unique_ptr<juce::Component> makePanel (AudioEngine& engine)
{
    return std::make_unique<AdrPanel> (engine);
}
} // namespace adrwindow
} // namespace lynx
