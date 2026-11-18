#include "ui/PolyEditor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>

#include "core/AudioToMidi.h"
#include "core/PolyEdit.h"
#include "engine/Text.h"
#include "ui/BasicPitchSession.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

#include "../../third_party/pitchnet-poly/PolyEngine.h"

namespace lynx
{
namespace
{
// Newtone's note view (ui/NewtoneEditor.cpp, flnt: FL's Newtone measured on a 100 % capture, round 190gu/190kj): the same
// keyboard, rows, grid, ruler and blob colours, so the polyphonic editor reads as Newtone with chords
namespace pal
{
const juce::Colour header { 0xff1a2227 }, rulerBg { 0xff1e2930 }, rulerLine { 0xff2d3940 }, rulerInk { 0xffb4c0c8 }, rowIn { 0xff3d4e59 },
    rowOut { 0xff2d3d47 }, rowLine { 0xff2b3b45 }, stepLine { 0xff2a3b46 }, beatLine { 0xff283b46 }, barLine { 0xff102632 },
    keyWhite { 0xffcdd1d9 }, keyBlack { 0xff2d3038 }, keyLine { 0xff8b8e96 }, keyC { 0xffadb1b8 }, blob { 0xffced7dd }, orange { 0xffff7900 },
    orangeLine { 0xffff9b30 }, pitchCore { 0xff14120f }, buttonEdge { 0xff12171a }, playhead { 0xffffc04a };
constexpr int headerHeight = 40, keyboardWidth = 64, rulerHeight = 16;
} // namespace pal

const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
// LYNX's piano roll naming (key 60 = C5), as Newtone
juce::String noteName (double midi)
{
    const auto k = (int) std::lround (midi);
    return juce::String (noteNames[((k % 12) + 12) % 12]) + juce::String (k / 12);
}

// Basic Pitch's note posteriorgram (LYNX's ONNX Runtime session, ui/BasicPitchSession.h) as PitchNet's evidence: frame k
// of the stitched posteriorgram is at k x 256 / 22050 s (core a2m::runModelBatched keeps an exact grid)
bool evidenceOf (const std::vector<float>& mono, double rate, polyphonic::NoteEvidence& ev, const std::function<bool()>& stop,
                 const std::function<void (double)>& progress, juce::String& note)
{
    basicpitch::Session session;
    juce::String error;
    if (! session.open (error))
    {
        note = error;
        return false;
    }
    a2m::Posteriorgram pg;
    if (! basicpitch::posteriorgram (session, mono, rate, false, stop, progress, pg, error))
    {
        note = error;
        return false;
    }
    ev.frameTimes.resize ((std::size_t) pg.frames);
    for (int k = 0; k < pg.frames; ++k)
        ev.frameTimes[(std::size_t) k] = (float) ((double) k * a2m::hop / a2m::modelRate);
    ev.note = std::move (pg.note);
    return true;
}

std::vector<float> monoOf (const SampleData& s)
{
    std::vector<float> m (s.left.size());
    for (std::size_t i = 0; i < m.size(); ++i)
        m[i] = s.right.empty() ? s.left[i] : 0.5f * (s.left[i] + s.right[i]);
    return m;
}

// One job at a time on a low-priority thread; a new job asks the running one to stop (its cancel flag) and replaces any
// waiting one
class Worker final : private juce::Thread
{
public:
    using Job = std::function<void (const std::atomic<bool>& cancel)>;
    Worker() : juce::Thread ("LYNX polyphonic notes") { startThread (juce::Thread::Priority::low); }
    ~Worker() override
    {
        cancel = true;
        signalThreadShouldExit();
        wake.signal();
        stopThread (-1);
    }
    void post (Job job)
    {
        const std::lock_guard<std::mutex> lock (jobLock);
        pending = std::move (job);
        cancel = true;
        wake.signal();
    }
    bool busy() const noexcept { return running.load() || hasPending.load(); }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            Job job;
            {
                const std::lock_guard<std::mutex> lock (jobLock);
                job = std::move (pending);
                pending = nullptr;
                hasPending = false;
                if (job)
                    cancel = false;
            }
            if (! job)
            {
                wake.wait (200);
                const std::lock_guard<std::mutex> lock (jobLock);
                hasPending = pending != nullptr;
                continue;
            }
            running = true;
            job (cancel);
            running = false;
        }
    }
    std::mutex jobLock;
    Job pending;
    std::atomic<bool> cancel { false }, running { false }, hasPending { false };
    juce::WaitableEvent wake;
};

class PolyEditor final : public juce::Component,
                         private juce::Timer
{
public:
    PolyEditor (AudioEngine& e, int ch, bool analyseNow) : engine (e), channel (ch)
    {
        setWantsKeyboardFocus (true);
        for (auto* b : { &playButton, &originalButton, &resetButton, &cancelButton })
            addAndMakeVisible (b);
        originalButton.setClickingTogglesState (true);
        originalButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        playButton.onClick = [this] { togglePlay(); };
        originalButton.onClick = [this] {
            if (previewing)
                startPreview();
        };
        resetButton.onClick = [this] { resetSelection(); };
        cancelButton.onClick = [this] { cancelAnalysis(); };
        levelLabel.setText (tr ("Level"), juce::dontSendNotification);
        levelLabel.setFont (theme::font (theme::type::small));
        levelLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (levelLabel);
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        level.setRange (-24.0, 12.0, 0.1);
        level.setTextValueSuffix (" dB");
        level.setDoubleClickReturnValue (true, 0.0);
        level.setColour (juce::Slider::trackColourId, theme::colour::accentMuted);
        level.onDragEnd = [this] { commit(); };
        level.onValueChange = [this] { setSelectionLevel ((float) level.getValue(), ! level.isMouseButtonDown()); };
        addAndMakeVisible (level);
        status.setFont (theme::font (theme::type::small));
        status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (status);
        setSize (1000, 560);

        const auto& song = engine.getSong();
        if (channel >= 0 && channel < (int) song.channels.size())
        {
            const auto& c = song.channels[(std::size_t) channel];
            path = c.samplePath;
            channelName = juce::String::fromUTF8 (c.name.c_str());
            sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (path.c_str()));
            if (sample != nullptr && ! c.polyEdit.empty())
                if (auto saved = polyedit::parse (c.polyEdit); saved && saved->frames == (std::int64_t) sample->left.size()
                                                                && std::abs (saved->sampleRate - sample->sampleRate) < 0.5)
                {
                    model = std::move (*saved);
                    committed = c.polyEdit;
                    analysis = polyedit::toAnalysis (model);
                    state = State::ready;
                }
        }
        if (sample == nullptr || sample->left.empty())
            state = State::noSample;
        else if (state != State::ready)
            startAnalysis (analyseNow);
        fitView();
        updateStatus();
        startTimerHz (20);
    }

    ~PolyEditor() override
    {
        stopTimer();
        if (previewing)
            engine.previewSample (nullptr);
        worker.reset(); // (the jobs use `analysis`: they end first)
    }

    // --- shot routes ----------------------------------------------------------------------------------------------
    juce::String shotState (const juce::String& s)
    {
        const auto parts = juce::StringArray::fromTokens (s, ":", "");
        const auto note = parts.size() > 1 ? parts[1].getIntValue() : -1;
        const auto value = parts.size() > 2 ? parts[2].getDoubleValue() : 0.0;
        if (s.startsWith ("select:"))
        {
            selected.clear();
            for (const auto& k : juce::StringArray::fromTokens (s.fromFirstOccurrenceOf (":", false, false), ",", ""))
                selected.insert (k.getIntValue());
        }
        else if (s.startsWith ("pitch:") && valid (note))
            model.edits[note].semitones = (float) value;
        else if (s.startsWith ("move:") && valid (note))
            model.edits[note].moveSeconds = value;
        else if (s.startsWith ("level:") && valid (note))
            model.edits[note].gainDb = (float) value;
        else if (s.startsWith ("mute:") && valid (note))
            model.edits[note].mute = ! model.edits[note].mute;
        else if (s.startsWith ("zoom:"))
            pixelsPerSecond = std::max (10.0, s.fromFirstOccurrenceOf (":", false, false).getDoubleValue());
        else if (s == "render")
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto blob = polyedit::write (model);
            std::shared_ptr<const SampleData> out = polyedit::render (*sample, model, analysis.get());
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            if (out != nullptr)
                polyedit::remember (path, blob, out);
            rendered = out;
            publish (blob);
            return "render " + juce::String (ms, 1) + " ms, masks " + juce::String ((double) polyphonic::maskBytes (*analysis) / 1.0e6, 2) + " MB";
        }
        repaint();
        return {};
    }

    juce::String notesText() const
    {
        juce::String text;
        for (int k = 0; k < (int) model.notes.size(); ++k)
        {
            const auto& n = model.notes[(std::size_t) k];
            text << k << " " << noteName (n.medianMidi()) << " " << juce::String (n.medianMidi(), 2) << " " << juce::String (n.startFrame * model.frameSeconds(), 3) << "-"
                 << juce::String ((n.endFrame + 1) * model.frameSeconds(), 3);
            if (auto it = model.edits.find (k); it != model.edits.end() && ! it->second.identity())
                text << " edit " << juce::String (it->second.semitones, 2) << " st " << juce::String (it->second.gainDb, 1) << " dB "
                     << juce::String (it->second.moveSeconds, 3) << " s" << (it->second.mute ? " muted" : "");
            text << "\n";
        }
        return text;
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (pal::headerHeight).reduced (8, 7);
        playButton.setBounds (bar.removeFromLeft (64));
        bar.removeFromLeft (6);
        originalButton.setBounds (bar.removeFromLeft (78));
        bar.removeFromLeft (14);
        levelLabel.setBounds (bar.removeFromLeft (40));
        level.setBounds (bar.removeFromLeft (220));
        bar.removeFromLeft (8);
        resetButton.setBounds (bar.removeFromLeft (64));
        bar.removeFromLeft (12);
        cancelButton.setBounds (bar.removeFromRight (72));
        status.setBounds (bar);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (pal::header);
        g.fillRect (getLocalBounds().removeFromTop (pal::headerHeight));
        const auto view = viewArea();
        paintKeyboard (g, view.withWidth (pal::keyboardWidth).withTrimmedTop (pal::rulerHeight));
        paintRuler (g, notesArea().withHeight (pal::rulerHeight).translated (0, -pal::rulerHeight));
        paintNotes (g, notesArea());
        if (state != State::ready)
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::body));
            g.drawText (state == State::noSample ? tr ("The sample of this clip could not be read.") : status.getText(), notesArea(), juce::Justification::centred, true);
        }
    }

    // --- mouse / keys ---------------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (state != State::ready || ! notesArea().contains (e.getPosition()))
            return;
        const auto hit = noteAt (e.position);
        if (hit < 0)
        {
            if (! e.mods.isShiftDown())
                selected.clear();
            repaint();
            return;
        }
        if (e.mods.isShiftDown())
        {
            if (! selected.erase (hit))
                selected.insert (hit);
        }
        else if (selected.count (hit) == 0)
            selected = { hit };
        dragStart.clear();
        for (auto k : selected)
            dragStart[k] = model.edits.count (k) ? model.edits[k] : polyedit::Edit {};
        dragging = true;
        dragMoved = false;
        syncLevel();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || state != State::ready)
            return;
        const auto dy = (double) -e.getDistanceFromDragStartY() / rowHeight;
        const auto dx = (double) e.getDistanceFromDragStartX() / pixelsPerSecond;
        const auto semis = e.mods.isAltDown() ? std::round (dy * 100.0) / 100.0 : std::round (dy);
        const auto timeMove = std::abs (e.getDistanceFromDragStartX()) >= 4 ? dx : 0.0;
        dragMoved = dragMoved || semis != 0.0 || timeMove != 0.0;
        for (const auto& [k, start] : dragStart)
        {
            auto edit = start;
            edit.semitones = (float) (start.semitones + semis);
            edit.moveSeconds = start.moveSeconds + timeMove;
            model.edits[k] = edit;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && dragMoved)
            commit();
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = noteAt (e.position);
        if (hit >= 0)
        {
            selected = { hit };
            resetSelection();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            const auto at = secondsOfX (e.position.x);
            pixelsPerSecond = std::clamp (pixelsPerSecond * (w.deltaY > 0 ? 1.25 : 0.8), 10.0, 2000.0);
            scrollSeconds = std::max (0.0, at - (e.position.x - (float) notesArea().getX()) / pixelsPerSecond);
        }
        else if (e.mods.isShiftDown())
            scrollSeconds = std::max (0.0, scrollSeconds - w.deltaY * 120.0 / pixelsPerSecond);
        else
            topPitch = std::clamp (topPitch + w.deltaY * 6.0, 24.0, 120.0);
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            const auto anyOn = std::any_of (selected.begin(), selected.end(), [this] (int k) { return ! (model.edits.count (k) && model.edits[k].mute); });
            for (auto k : selected)
                model.edits[k].mute = anyOn;
            commit();
            return true;
        }
        if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        {
            resetSelection();
            return true;
        }
        if (key == juce::KeyPress::spaceKey)
        {
            togglePlay();
            return true;
        }
        return false;
    }

private:
    enum class State
    {
        analysing,
        ready,
        failed,
        noSample
    };

    bool valid (int k) const noexcept { return k >= 0 && k < (int) model.notes.size(); }

    // --- analysis and renders on the worker -------------------------------------------------------------------------
    void startAnalysis (bool now)
    {
        state = State::analysing;
        progress = 0.0f;
        auto job = [this, src = sample] (const std::atomic<bool>& cancel) {
            const auto mono = monoOf (*src);
            polyphonic::NoteEvidence ev;
            juce::String why;
            const auto stop = [&cancel] { return cancel.load(); };
            const auto withEvidence = evidenceOf (mono, src->sampleRate, ev, stop, [this] (double p) { progress = (float) (0.25 * p); }, why);
            if (cancel.load())
                return;
            polyedit::Model found;
            const auto ok = polyedit::analyse (*src, withEvidence ? &ev : nullptr, found, &cancel, [this] (float p) { progress = 0.25f + 0.75f * p; });
            juce::Component::SafePointer<PolyEditor> self (this);
            auto finish = [self, ok, withEvidence, found = std::move (found)]() mutable {
                if (self == nullptr)
                    return;
                if (! ok)
                {
                    self->state = State::failed;
                    self->updateStatus();
                    return;
                }
                self->analysed (std::move (found), withEvidence);
            };
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                finish();
            else
                juce::MessageManager::callAsync (std::move (finish));
        };
        if (now)
        {
            std::atomic<bool> never { false };
            job (never);
        }
        else
            worker->post (std::move (job));
    }

    void analysed (polyedit::Model found, bool withEvidence)
    {
        model = std::move (found);
        model.evidence = withEvidence;
        analysis = polyedit::toAnalysis (model);
        state = State::ready;
        fitView();
        // the notes go into the project at once (opening again does not analyse again); not an undo step
        publish (polyedit::write (model), false);
        updateStatus();
        repaint();
    }

    void cancelAnalysis()
    {
        if (state != State::analysing)
            return;
        worker.reset();
        worker = std::make_unique<Worker>();
        state = State::failed;
        updateStatus();
        repaint();
    }

    // the edit state goes to the worker: its render, then the song (one undo step) and the render cache
    void commit()
    {
        if (state != State::ready || sample == nullptr)
            return;
        for (auto it = model.edits.begin(); it != model.edits.end();)
            it = it->second.identity() ? model.edits.erase (it) : std::next (it);
        const auto blob = polyedit::write (model);
        if (blob == committed)
            return;
        const auto generation = ++renderGeneration;
        rendering = true;
        updateStatus();
        worker->post ([this, blob, m = model, src = sample, generation] (const std::atomic<bool>& cancel) {
            std::shared_ptr<const SampleData> out = polyedit::render (*src, m, analysis.get(), &cancel);
            if (cancel.load())
                return;
            juce::Component::SafePointer<PolyEditor> self (this);
            juce::MessageManager::callAsync ([self, blob, out, generation] {
                if (self == nullptr || generation != self->renderGeneration)
                    return;
                self->rendering = false;
                if (out != nullptr)
                    polyedit::remember (self->path, blob, out);
                self->rendered = out;
                self->publish (blob);
                if (self->previewing)
                    self->startPreview();
                self->updateStatus();
            });
        });
        repaint();
    }

    // the channel's polyEdit (the channel must still play this sample)
    void publish (const std::vector<std::uint8_t>& blob, bool undoable = true)
    {
        committed = blob;
        const auto c = channel;
        const auto p = path;
        engine.editSong (
            [c, p, blob] (Song& s) {
                if (c >= 0 && c < (int) s.channels.size() && s.channels[(std::size_t) c].samplePath == p
                    && s.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
                    s.channels[(std::size_t) c].polyEdit = blob;
            },
            undoable);
    }

    void resetSelection()
    {
        if (selected.empty())
            model.edits.clear();
        for (auto k : selected)
            model.edits.erase (k);
        syncLevel();
        commit();
        repaint();
    }

    void setSelectionLevel (float db, bool commitNow)
    {
        if (syncingLevel || selected.empty())
            return;
        for (auto k : selected)
            model.edits[k].gainDb = db;
        repaint();
        if (commitNow)
            commit();
    }

    void syncLevel()
    {
        syncingLevel = true;
        const auto k = selected.empty() ? -1 : *selected.begin();
        level.setValue (k >= 0 && model.edits.count (k) ? model.edits[k].gainDb : 0.0, juce::dontSendNotification);
        level.setEnabled (! selected.empty());
        syncingLevel = false;
    }

    // --- preview ----------------------------------------------------------------------------------------------------
    void togglePlay()
    {
        if (previewing)
        {
            engine.previewSample (nullptr);
            previewing = false;
        }
        else
            startPreview();
        updateStatus();
    }

    void startPreview()
    {
        const auto source = originalButton.getToggleState() || rendered == nullptr ? sample : rendered;
        if (source == nullptr)
            return;
        engine.previewSample (source, false, 0.8f);
        previewing = true;
    }

    void timerCallback() override
    {
        if (state == State::analysing || rendering)
            updateStatus();
        if (previewing)
        {
            const auto at = engine.getPreviewPosition();
            if (at < 0.0)
            {
                previewing = false;
                updateStatus();
            }
            playAt = at;
            repaint (notesArea());
        }
        // the song changed under the editor (undo / redo): its edit state follows
        const auto& song = engine.getSong();
        if (state == State::ready && ! rendering && ! dragging && channel >= 0 && channel < (int) song.channels.size())
        {
            const auto& c = song.channels[(std::size_t) channel];
            if (c.samplePath == path && c.polyEdit != committed && ! c.polyEdit.empty())
                if (auto other = polyedit::parse (c.polyEdit); other && other->notes == model.notes)
                {
                    model.edits = other->edits;
                    committed = c.polyEdit;
                    rendered = polyedit::renderedFor (path, sample, committed);
                    if (rendered == sample)
                        rendered = nullptr;
                    syncLevel();
                    repaint();
                }
        }
    }

    void updateStatus()
    {
        juce::String text;
        if (state == State::analysing)
            text = (progress < 0.25f ? tr ("Finding notes with Basic Pitch...") : tr ("Analysing notes...")) + " " + juce::String (juce::roundToInt (progress * 100.0f)) + " %";
        else if (state == State::failed)
            text = tr ("Analysis cancelled");
        else if (state == State::ready)
        {
            text = model.notes.empty() ? tr ("No notes found") : tr ("{0} notes", { juce::String ((int) model.notes.size()) });
            if (rendering)
                text << "  -  " << tr ("Rendering...");
            else if (! model.notes.empty())
                text << "  -  " << tr ("Drag a note: up / down = pitch (Alt: cents), left / right = time. Del = mute, R = reset.");
        }
        status.setText (text, juce::dontSendNotification);
        cancelButton.setVisible (state == State::analysing);
        playButton.setButtonText (previewing ? tr ("Stop") : tr ("Play"));
        playButton.setEnabled (sample != nullptr);
        resetButton.setEnabled (state == State::ready);
        originalButton.setEnabled (state == State::ready);
        level.setEnabled (state == State::ready && ! selected.empty());
        if (state == State::analysing)
            repaint();
    }

    // --- geometry ---------------------------------------------------------------------------------------------------
    juce::Rectangle<int> viewArea() const { return getLocalBounds().withTrimmedTop (pal::headerHeight); }
    juce::Rectangle<int> notesArea() const { return viewArea().withTrimmedLeft (pal::keyboardWidth).withTrimmedTop (pal::rulerHeight); }
    float xOfSeconds (double s) const { return (float) notesArea().getX() + (float) ((s - scrollSeconds) * pixelsPerSecond); }
    double secondsOfX (float x) const { return scrollSeconds + (x - (float) notesArea().getX()) / pixelsPerSecond; }
    float yOfPitch (double p) const { return (float) notesArea().getY() + (float) ((topPitch - p) * rowHeight); }
    double pitchOfY (float y) const { return topPitch - (y - (float) notesArea().getY()) / rowHeight; }

    void fitView()
    {
        const auto seconds = sample != nullptr && sample->sampleRate > 0.0 ? (double) sample->left.size() / sample->sampleRate : 4.0;
        const auto w = std::max (200, getWidth() - pal::keyboardWidth);
        pixelsPerSecond = std::max (10.0, (double) w / std::max (0.5, seconds));
        scrollSeconds = 0.0;
        double lo = 60.0, hi = 72.0;
        if (! model.notes.empty())
        {
            lo = 200.0, hi = 0.0;
            for (const auto& n : model.notes)
                lo = std::min (lo, (double) n.medianMidi()), hi = std::max (hi, (double) n.medianMidi());
        }
        const auto rows = std::max (1, notesArea().getHeight());
        rowHeight = std::clamp ((double) rows / std::max (12.0, hi - lo + 6.0), 6.0, 22.0);
        topPitch = std::ceil ((hi + lo) / 2.0 + rows / rowHeight / 2.0);
    }

    // the note's edit (identity when none)
    polyedit::Edit editOf (int k) const
    {
        const auto it = model.edits.find (k);
        return it != model.edits.end() ? it->second : polyedit::Edit {};
    }

    // the blob's outline: along the pitch track, thicker for louder notes, tapering at both ends
    juce::Path blobOf (int k, bool atOldPlace) const
    {
        const auto& n = model.notes[(std::size_t) k];
        const auto e = atOldPlace ? polyedit::Edit {} : editOf (k);
        juce::Path p;
        if (n.f0.empty())
            return p;
        const auto fs = model.frameSeconds();
        const auto gain = (float) std::pow (10.0, e.gainDb / 20.0);
        const auto half = (float) std::clamp (rowHeight * 0.48 * std::sqrt (std::min (1.0, n.energy / std::max (1e-12, loudest)) * gain), 1.5, rowHeight * 0.9);
        const auto frames = (int) n.f0.size();
        std::vector<juce::Point<float>> lower;
        const auto step = std::max (1, (int) std::floor (2.0 / std::max (1e-6, fs * pixelsPerSecond))); // (about 2 px apart)
        for (int i = 0; i < frames; i += step)
        {
            const auto f = n.f0[(std::size_t) i];
            if (f <= 0.0f)
                continue;
            const auto pitch = 69.0 + 12.0 * std::log2 (f / 440.0) + e.semitones;
            const auto x = xOfSeconds ((n.startFrame + i) * fs + e.moveSeconds);
            const auto taper = (float) std::min ({ 1.0, (i + 1) / 4.0, (frames - i) / 6.0 });
            const auto h = std::max (1.0f, half * taper);
            const auto y = yOfPitch (pitch);
            if (lower.empty())
                p.startNewSubPath (x, y - h);
            else
                p.lineTo (x, y - h);
            lower.push_back ({ x, y + h });
        }
        for (auto it = lower.rbegin(); it != lower.rend(); ++it)
            p.lineTo (*it);
        p.closeSubPath();
        return p;
    }

    int noteAt (juce::Point<float> at) const
    {
        // the topmost (last drawn) blob under the mouse; a little slack around thin blobs
        for (int k = (int) model.notes.size() - 1; k >= 0; --k)
        {
            const auto b = blobOf (k, false);
            if (b.contains (at) || b.getBounds().expanded (0.0f, 3.0f).contains (at))
                return k;
        }
        return -1;
    }

    void paintKeyboard (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        g.setColour (pal::keyWhite);
        g.fillRect (area);
        const auto lo = (int) std::floor (pitchOfY ((float) area.getBottom())) - 1, hi = (int) std::ceil (pitchOfY ((float) area.getY())) + 1;
        g.setFont (juce::FontOptions (11.0f));
        for (auto m = lo; m <= hi; ++m)
        {
            const auto y0 = yOfPitch (m + 0.5), y1 = yOfPitch (m - 0.5);
            const auto pc = ((m % 12) + 12) % 12;
            const auto black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            if (black)
            {
                g.setColour (pal::keyBlack);
                g.fillRect (juce::Rectangle<float> ((float) area.getX(), y0, (float) area.getWidth() * 0.6f, y1 - y0));
            }
            g.setColour (pal::keyLine);
            g.drawHorizontalLine ((int) std::round (y1), (float) area.getX(), (float) area.getRight());
            if (pc == 0)
            {
                g.setColour (pal::keyC);
                g.fillRect (juce::Rectangle<float> ((float) area.getX() + 26.0f, y0 + 1.0f, (float) area.getWidth() - 26.0f, y1 - y0 - 1.0f));
                g.setColour (juce::Colour (0xff3a3d44));
                g.drawText (noteName (m), juce::Rectangle<float> ((float) area.getX(), y0, (float) area.getWidth() - 3.0f, y1 - y0), juce::Justification::centredRight, false);
            }
        }
        g.setColour (pal::buttonEdge);
        g.drawVerticalLine (area.getRight() - 1, (float) area.getY(), (float) area.getBottom());
    }

    double beatSeconds() const { return 60.0 / std::max (1.0, engine.getSong().bpm); }

    void paintRuler (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        g.setColour (pal::rulerBg);
        g.fillRect (area);
        g.setColour (pal::rulerLine);
        g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());
        const auto bar = 4.0 * beatSeconds();
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (pal::rulerInk);
        for (auto b = std::max (0.0, std::floor (secondsOfX ((float) area.getX()) / bar)); b * bar < secondsOfX ((float) area.getRight()); b += 1.0)
        {
            if (bar * pixelsPerSecond < 18.0 && std::fmod (b, 4.0) != 0.0)
                continue;
            g.drawText (juce::String ((int) b + 1), juce::Rectangle<float> (xOfSeconds (b * bar) + 2.0f, (float) area.getY() + 1.0f, 40.0f, (float) area.getHeight() - 2.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    void paintNotes (juce::Graphics& g, juce::Rectangle<int> area)
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        const auto lo = (int) std::floor (pitchOfY ((float) area.getBottom())) - 1, hi = (int) std::ceil (pitchOfY ((float) area.getY())) + 1;
        for (auto m = lo; m <= hi; ++m)
        {
            const auto pc = ((m % 12) + 12) % 12;
            const auto black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const auto y0 = yOfPitch (m + 0.5), y1 = yOfPitch (m - 0.5);
            g.setColour (black ? pal::rowOut : pal::rowIn);
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), y0, (float) area.getWidth(), y1 - y0));
            g.setColour (pal::rowLine);
            g.drawHorizontalLine ((int) std::round (y1) - 1, (float) area.getX(), (float) area.getRight());
        }
        const auto step = beatSeconds() / 4.0;
        if (step * pixelsPerSecond >= 6.0)
            for (auto k = std::max (0.0, std::floor (secondsOfX ((float) area.getX()) / step)); k * step < secondsOfX ((float) area.getRight()); k += 1.0)
            {
                const auto kk = (long long) k;
                g.setColour (kk % 16 == 0 ? pal::barLine : kk % 4 == 0 ? pal::beatLine : pal::stepLine);
                g.drawVerticalLine ((int) std::round (xOfSeconds (k * step)), (float) area.getY(), (float) area.getBottom());
            }
        if (state != State::ready)
            return;
        loudest = 1e-12;
        for (const auto& n : model.notes)
            loudest = std::max (loudest, n.energy);
        // where edited notes were: a dashed outline
        for (int k = 0; k < (int) model.notes.size(); ++k)
        {
            const auto e = editOf (k);
            if (e.identity() || (e.semitones == 0.0f && e.moveSeconds == 0.0 && ! e.mute))
                continue;
            const auto old = blobOf (k, true);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, old, dashes, 2);
            g.setColour (pal::orangeLine.withAlpha (0.7f));
            g.fillPath (dashed);
        }
        for (int k = 0; k < (int) model.notes.size(); ++k)
        {
            const auto blob = blobOf (k, false);
            if (blob.getBounds().getRight() < (float) area.getX() || blob.getBounds().getX() > (float) area.getRight())
                continue;
            const auto e = editOf (k);
            const auto isSelected = selected.count (k) > 0;
            auto colour = e.identity() ? pal::blob : pal::orange;
            if (e.mute)
                colour = colour.withAlpha (0.28f);
            g.setColour (colour);
            g.fillPath (blob);
            g.setColour (isSelected ? juce::Colours::white : pal::pitchCore.withAlpha (0.55f));
            g.strokePath (blob, juce::PathStrokeType (isSelected ? 1.6f : 0.8f));
            // the note's name at its start, as Newtone writes it
            const auto& n = model.notes[(std::size_t) k];
            const auto b = blob.getBounds();
            if (b.getWidth() > 28.0f && rowHeight >= 9.0)
            {
                g.setColour (pal::pitchCore.withAlpha (e.mute ? 0.3f : 0.85f));
                g.setFont (juce::FontOptions ((float) std::min (11.0, rowHeight * 0.8)));
                g.drawText (noteName (n.medianMidi() + e.semitones), b.withWidth (40.0f).translated (4.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }
        if (previewing && playAt >= 0.0 && sample != nullptr)
        {
            const auto x = xOfSeconds (playAt / sample->sampleRate);
            g.setColour (pal::playhead);
            g.drawVerticalLine ((int) std::round (x), (float) area.getY(), (float) area.getBottom());
        }
    }

    AudioEngine& engine;
    int channel;
    std::string path;
    juce::String channelName;
    std::shared_ptr<const SampleData> sample, rendered;
    polyedit::Model model;
    std::unique_ptr<polyphonic::Analysis> analysis;
    std::vector<std::uint8_t> committed;
    State state = State::analysing;
    std::atomic<float> progress { 0.0f };
    bool rendering = false, previewing = false, dragging = false, dragMoved = false, syncingLevel = false;
    int renderGeneration = 0;
    double playAt = -1.0, loudest = 1e-12;
    double pixelsPerSecond = 100.0, scrollSeconds = 0.0, topPitch = 84.0, rowHeight = 14.0;
    std::set<int> selected;
    std::map<int, polyedit::Edit> dragStart;
    std::unique_ptr<Worker> worker = std::make_unique<Worker>();
    juce::TextButton playButton { tr ("Play") }, originalButton { tr ("Original") }, resetButton { tr ("Reset") }, cancelButton { tr ("Cancel") };
    juce::Label levelLabel, status;
    juce::Slider level;
};

class PolyEditorWindow final : public juce::DocumentWindow
{
public:
    PolyEditorWindow (AudioEngine& engine, int channel, const juce::String& title)
        : DocumentWindow (title, theme::colour::bg2, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new PolyEditor (engine, channel, false), true);
        setResizable (true, false);
        setResizeLimits (640, 360, 4000, 2400);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        dockOrKeepAbove (*this);
    }
    void closeButtonPressed() override;
};

std::map<int, std::unique_ptr<PolyEditorWindow>>& windows()
{
    static std::map<int, std::unique_ptr<PolyEditorWindow>> open;
    return open;
}

void PolyEditorWindow::closeButtonPressed()
{
    for (auto& [key, window] : windows())
        if (window.get() == this)
        {
            juce::MessageManager::callAsync ([k = key] { windows().erase (k); });
            return;
        }
}
} // namespace

namespace PolyEditorWindows
{
void open (AudioEngine& engine, int channel)
{
    auto& open = windows();
    if (auto it = open.find (channel); it != open.end())
    {
        it->second->toFront (true);
        return;
    }
    const auto& song = engine.getSong();
    if (channel < 0 || channel >= (int) song.channels.size() || song.channels[(std::size_t) channel].instrument != InstrumentKind::audioClip)
        return;
    const auto name = juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str());
    open[channel] = std::make_unique<PolyEditorWindow> (engine, channel, tr ("Edit notes (polyphonic)") + (name.isNotEmpty() ? " - " + name : juce::String()));
}

void closeAll()
{
    windows().clear();
}
} // namespace PolyEditorWindows

std::unique_ptr<juce::Component> makePolyEditor (AudioEngine& engine, int channel, bool analyseNow)
{
    return std::make_unique<PolyEditor> (engine, channel, analyseNow);
}

juce::String setPolyEditorShotState (juce::Component& editor, const juce::String& state)
{
    if (auto* e = dynamic_cast<PolyEditor*> (&editor))
        return e->shotState (state);
    return {};
}

juce::String polyEditorNotes (juce::Component& editor)
{
    if (auto* e = dynamic_cast<PolyEditor*> (&editor))
        return e->notesText();
    return {};
}

double polyEditorPaintMs (juce::Component& editor, int paints)
{
    juce::Image image (juce::Image::ARGB, std::max (1, editor.getWidth()), std::max (1, editor.getHeight()), true);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int k = 0; k < paints; ++k)
    {
        juce::Graphics g (image);
        editor.paintEntireComponent (g, true);
    }
    return (juce::Time::getMillisecondCounterHiRes() - t0) / std::max (1, paints);
}
} // namespace lynx
