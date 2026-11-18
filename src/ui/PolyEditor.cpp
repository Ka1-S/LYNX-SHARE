#include "ui/PolyEditor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <set>

#include "core/AudioToMidi.h"
#include "core/PolyEdit.h"
#include "engine/Text.h"
#include "ui/BasicPitchSession.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
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
constexpr int headerHeight = 40, keyboardWidth = 64, rulerHeight = 16, footerHeight = 22; // (round 190lo: the status line moved to a footer)
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
// waiting one. Round 190lo: a second slot for previews (a dragged note heard while it moves): a preview replaces only the
// waiting preview, never stops a running job, and runs after any waiting job (so the analysis object is only ever used by
// this one thread).
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
        hasPending = true;
        cancel = true;
        wake.signal();
    }
    void postPreview (Job job)
    {
        const std::lock_guard<std::mutex> lock (jobLock);
        preview = std::move (job);
        hasPending = true;
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
                if (pending)
                {
                    job = std::move (pending);
                    pending = nullptr;
                    cancel = false;
                }
                else if (preview)
                {
                    job = std::move (preview);
                    preview = nullptr;
                }
                hasPending = pending != nullptr || preview != nullptr;
            }
            if (! job)
            {
                wake.wait (200);
                continue;
            }
            running = true;
            job (cancel);
            running = false;
        }
    }
    std::mutex jobLock;
    Job pending, preview;
    std::atomic<bool> cancel { false }, running { false }, hasPending { false };
    juce::WaitableEvent wake;
};

class PolyEditor final : public juce::Component,
                         private juce::Timer
{
public:
    // Round 190lo: buffer mode (Edison's Tools › Analysis entry): the editor works on `buffer` instead of a channel's sample,
    // keeps its own undo (Ctrl+Z / Ctrl+Y here), and Apply hands the render to `apply`
    using Apply = std::function<void (std::shared_ptr<const SampleData>)>;

    PolyEditor (AudioEngine& e, int ch, bool analyseNow, std::shared_ptr<const SampleData> buffer = nullptr, Apply applyTo = {})
        : engine (e), channel (ch), apply (std::move (applyTo))
    {
        setWantsKeyboardFocus (true);
        for (auto* b : { &playButton, &originalButton, &resetButton, &cancelButton, &assignButton, &applyButton, &optionsButton })
            addAndMakeVisible (b);
        optionsButton.onClick = [this] { showOptions(); };
        for (auto* b : { &mainTool, &modulationTool, &driftTool, &drawTool })
        {
            addAndMakeVisible (b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (190);
            b->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        }
        mainTool.setToggleState (true, juce::dontSendNotification);
        mainTool.setTooltip (tr ("Notes: drag a note for its pitch and time, its end for its length; drag over empty space to select several"));
        modulationTool.setTooltip (tr ("Pitch modulation: drag a note up / down to scale its vibrato (0 - 200 %); double-click = 0 % / 100 %"));
        driftTool.setTooltip (tr ("Pitch drift: drag a note up / down to scale its slow pitch wander (0 - 200 %); double-click = 0 % / 100 %"));
        drawTool.setTooltip (tr ("Draw pitch: draw a note's pitch line; Alt+drag erases, double-click clears the drawn line"));
        mainTool.onClick = [this] { setTool (Tool::main); };
        modulationTool.onClick = [this] { setTool (Tool::modulation); };
        driftTool.onClick = [this] { setTool (Tool::drift); };
        drawTool.onClick = [this] { setTool (Tool::draw); };
        assignButton.setClickingTogglesState (true);
        assignButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        assignButton.setTooltip (tr ("Note assignment: the candidates the note finder rejected are shown faint; double-click one to make it a note, or a note to make it a candidate"));
        assignButton.onClick = [this] {
            assignMode = assignButton.getToggleState();
            selected.clear();
            updateStatus();
            repaint();
        };
        originalButton.setClickingTogglesState (true);
        originalButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        playButton.onClick = [this] { togglePlay(); };
        originalButton.onClick = [this] {
            if (previewing)
                startPreview();
        };
        resetButton.onClick = [this] { resetSelection(); };
        cancelButton.onClick = [this] { cancelAnalysis(); };
        applyButton.onClick = [this] { applyToBuffer(); };
        applyButton.setVisible (bufferMode());
        applyButton.setTooltip (tr ("Write the edited notes into Edison's audio (one undo step there) and close"));
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
        setSize (1100, 580);

        if (bufferMode())
        {
            sample = buffer;
            path = "edison:" + std::to_string ((std::uintptr_t) buffer.get());
            if (sample == nullptr || sample->left.empty())
                state = State::noSample;
            else
                startAnalysis (analyseNow);
        }
        else
        {
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
                        // a project opened with "Prepare edited clips" left this object with its masks built
                        analysis = polyedit::takePrepared (path, model);
                        if (analysis == nullptr)
                            analysis = polyedit::toAnalysis (model);
                        state = State::ready;
                        notesChanged();
                    }
            }
            if (sample == nullptr || sample->left.empty())
                state = State::noSample;
            else if (state != State::ready)
                startAnalysis (analyseNow);
        }
        fitView();
        updateStatus();
        startTimerHz (20);
    }

    ~PolyEditor() override
    {
        stopTimer();
        if (previewing || dragHeard)
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
                if (k.isNotEmpty())
                    selected.insert (k.getIntValue());
        }
        else if (s.startsWith ("pitch:") && valid (note))
            model.edits[note].semitones = (float) value;
        else if (s.startsWith ("move:") && valid (note))
            model.edits[note].moveSeconds = value;
        else if (s.startsWith ("level:") && valid (note))
            model.edits[note].gainDb = (float) value;
        else if (s.startsWith ("stretch:") && valid (note))
            model.edits[note].stretch = (float) value;
        else if (s.startsWith ("modulation:") && valid (note))
            model.edits[note].modulation = (float) value;
        else if (s.startsWith ("drift:") && valid (note))
            model.edits[note].drift = (float) value;
        else if (s.startsWith ("curve:") && valid (note))
        {
            // a drawn line: `value` semitones over the note's second half (a bend up)
            const auto& n = model.notes[(std::size_t) note];
            auto& c = model.edits[note].curve;
            c.assign (n.f0.size(), 0.0f);
            for (std::size_t i = c.size() / 2; i < c.size(); ++i)
                c[i] = (float) (value * (double) (i - c.size() / 2) / std::max<double> (1.0, (double) (c.size() - c.size() / 2)));
        }
        else if (s.startsWith ("mute:") && valid (note))
            model.edits[note].mute = ! model.edits[note].mute;
        else if (s == "clear")
            model.edits.clear();
        else if (s.startsWith ("assign:"))
        {
            assignMode = parts.size() > 1 && parts[1] == "on";
            assignButton.setToggleState (assignMode, juce::dontSendNotification);
            updateStatus();
        }
        else if (s.startsWith ("toggle:") && valid (note))
        {
            toggleAssignment (note, false);
            shotState ("render"); // (the route waits for nothing: the new state is committed now, as the worker would)
            return "assignment " + juce::String (note) + (model.notes[(std::size_t) note].active ? " is a note" : " is a candidate");
        }
        else if (s.startsWith ("tool:"))
        {
            const auto t = parts[1];
            setTool (t == "modulation" ? Tool::modulation : t == "drift" ? Tool::drift : t == "draw" ? Tool::draw : Tool::main);
        }
        else if (s.startsWith ("band:") && parts.size() >= 5)
        {
            // a rubber band from (seconds, midi) to (seconds, midi)
            const auto a = juce::Point<float> (xOfSeconds (parts[1].getDoubleValue()), yOfPitch (parts[2].getDoubleValue()));
            const auto b = juce::Point<float> (xOfSeconds (parts[3].getDoubleValue()), yOfPitch (parts[4].getDoubleValue()));
            band = juce::Rectangle<float> (a, b);
            selectInBand (false);
            juce::String list;
            for (auto k : selected)
                list << (list.isEmpty() ? "" : ",") << k;
            return "band selects " + list;
        }
        else if (s == "band:show")
            showBand = true;
        else if (s == "band:hide")
            band = {}, showBand = false;
        else if (s.startsWith ("preview:") && valid (note))
        {
            // what a dragged note plays: its render on its own, timed
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            std::int64_t from = 0;
            const auto part = polyedit::renderNote (*sample, previewModel(), note, *analysis, &from);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            return "preview " + juce::String (ms, 1) + " ms, " + juce::String (part != nullptr ? (double) part->left.size() / sample->sampleRate : 0.0, 2) + " s of audio";
        }
        else if (s.startsWith ("zoom:"))
            pixelsPerSecond = std::max (10.0, s.fromFirstOccurrenceOf (":", false, false).getDoubleValue());
        else if (s.startsWith ("view:") && parts.size() >= 3)
        {
            scrollSeconds = std::max (0.0, parts[1].getDoubleValue());
            topPitch = parts[2].getDoubleValue();
            if (parts.size() >= 4)
                rowHeight = std::clamp (parts[3].getDoubleValue(), 4.0, 40.0);
        }
        else if (s == "render")
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (auto it = model.edits.begin(); it != model.edits.end();)
                it = it->second.identity() ? model.edits.erase (it) : std::next (it);
            const auto blob = polyedit::write (model);
            std::shared_ptr<const SampleData> out = polyedit::render (*sample, model, analysis.get());
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            if (out != nullptr && ! bufferMode())
                polyedit::remember (path, blob, out);
            rendered = out;
            publish (blob);
            // (a render the worker was making for an earlier state is no longer wanted)
            rendering = false;
            ++renderGeneration;
            updateStatus();
            return "render " + juce::String (ms, 1) + " ms, masks " + juce::String ((double) polyphonic::maskBytes (*analysis) / 1.0e6, 2) + " MB";
        }
        else if (s == "follow")
            followSong(); // (what the timer does after an undo / redo of the song)
        else if (s == "undo" || s == "redo")
        {
            bufferStep (s == "undo" ? -1 : 1);
            return juce::String (s) + " " + juce::String (bufferAt) + "/" + juce::String ((int) bufferHistory.size());
        }
        else if (s == "apply")
        {
            applyToBuffer (true);
            return "applied";
        }
        updateStatus();
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
                     << juce::String (it->second.moveSeconds, 3) << " s" << (it->second.mute ? " muted" : "")
                     << (it->second.stretch != 1.0f ? " stretch " + juce::String (it->second.stretch, 2) : juce::String())
                     << (it->second.modulation != 1.0f ? " modulation " + juce::String (juce::roundToInt (it->second.modulation * 100.0f)) + "%" : juce::String())
                     << (it->second.drift != 1.0f ? " drift " + juce::String (juce::roundToInt (it->second.drift * 100.0f)) + "%" : juce::String())
                     << (! it->second.curve.empty() ? " curve" : "");
            if (! n.active)
                text << " candidate";
            text << "\n";
        }
        return text;
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (pal::headerHeight).reduced (8, 7);
        playButton.setBounds (bar.removeFromLeft (56));
        bar.removeFromLeft (4);
        originalButton.setBounds (bar.removeFromLeft (70));
        bar.removeFromLeft (12);
        for (auto [b, w] : { std::pair { &mainTool, 58 }, std::pair { &modulationTool, 86 }, std::pair { &driftTool, 52 }, std::pair { &drawTool, 82 } })
        {
            b->setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (2);
        }
        bar.removeFromLeft (8);
        assignButton.setBounds (bar.removeFromLeft (118));
        bar.removeFromLeft (12);
        levelLabel.setBounds (bar.removeFromLeft (36));
        level.setBounds (bar.removeFromLeft (170));
        bar.removeFromLeft (6);
        resetButton.setBounds (bar.removeFromLeft (56));
        if (bufferMode())
        {
            bar.removeFromLeft (6);
            applyButton.setBounds (bar.removeFromLeft (64));
        }
        optionsButton.setBounds (bar.removeFromRight (64));
        auto foot = getLocalBounds().removeFromBottom (pal::footerHeight).reduced (8, 2);
        cancelButton.setBounds (foot.removeFromRight (72));
        status.setBounds (foot);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (pal::header);
        g.fillRect (getLocalBounds().removeFromTop (pal::headerHeight));
        g.fillRect (getLocalBounds().removeFromBottom (pal::footerHeight));
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
    void mouseMove (const juce::MouseEvent& e) override
    {
        auto cursor = juce::MouseCursor::NormalCursor;
        if (state == State::ready && notesArea().contains (e.getPosition()))
        {
            if (tool == Tool::main && endHandleAt (e.position) >= 0)
                cursor = juce::MouseCursor::LeftRightResizeCursor;
            else if ((tool == Tool::modulation || tool == Tool::drift) && noteAt (e.position) >= 0)
                cursor = juce::MouseCursor::UpDownResizeCursor;
            else if (tool == Tool::draw)
                cursor = juce::MouseCursor::CrosshairCursor;
        }
        setMouseCursor (cursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        drag = Drag::none;
        if (state != State::ready || ! notesArea().contains (e.getPosition()))
            return;
        const auto toggles = e.mods.isCtrlDown() || e.mods.isCommandDown();
        if (tool == Tool::draw)
        {
            const auto hit = noteAt (e.position, false);
            if (hit < 0 || ! model.notes[(std::size_t) hit].active)
                return;
            drawNote = hit;
            selected = { hit };
            dragStart = { { hit, editOf (hit) } };
            lastDraw = {};
            drag = Drag::draw;
            drawAt (e.position, e.mods.isAltDown());
            repaint();
            return;
        }
        const auto handle = tool == Tool::main ? endHandleAt (e.position) : -1;
        const auto hit = handle >= 0 ? handle : noteAt (e.position);
        if (hit < 0)
        {
            // a rubber band (Shift / Ctrl: adds to the selection)
            if (! e.mods.isShiftDown() && ! toggles)
                selected.clear();
            bandBase = selected;
            band = juce::Rectangle<float> (e.position, e.position);
            drag = Drag::band;
            syncLevel();
            repaint();
            return;
        }
        if (! model.notes[(std::size_t) hit].active)
        {
            // a candidate (note assignment): it is selected; a double-click makes it a note
            selected = { hit };
            syncLevel();
            repaint();
            return;
        }
        if (e.mods.isShiftDown() || toggles)
        {
            if (! selected.erase (hit))
                selected.insert (hit);
        }
        else if (selected.count (hit) == 0)
            selected = { hit };
        if (selected.count (hit) == 0)
        {
            repaint();
            return;
        }
        dragStart.clear();
        for (auto k : selected)
            if (valid (k) && model.notes[(std::size_t) k].active)
                dragStart[k] = editOf (k);
        dragNote = hit;
        drag = handle >= 0 ? Drag::stretch : tool == Tool::modulation ? Drag::modulation : tool == Tool::drift ? Drag::drift : Drag::move;
        dragMoved = false;
        heardKey = {};
        warmPreview (hit);
        syncLevel();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::none || state != State::ready)
            return;
        if (drag == Drag::band)
        {
            band = juce::Rectangle<float> (e.mouseDownPosition, e.position);
            showBand = true;
            selectInBand (true);
            repaint();
            return;
        }
        if (drag == Drag::draw)
        {
            drawAt (e.position, e.mods.isAltDown());
            dragMoved = true;
            repaint();
            return;
        }
        if (drag == Drag::stretch)
        {
            // the dragged note's end follows the mouse; every selected note takes the same ratio
            const auto& n = model.notes[(std::size_t) dragNote];
            const auto st = dragStart[dragNote];
            const auto fs = model.frameSeconds();
            const auto startSec = n.startFrame * fs + st.moveSeconds;
            const auto endSec = std::max (startSec + fs, secondsOfX (e.position.x));
            const auto ratio = (float) std::clamp (stretchFor (n, (endSec - startSec) / fs), 0.25, 4.0);
            const auto factor = ratio / std::max (0.25f, st.stretch);
            dragMoved = dragMoved || std::abs (e.getDistanceFromDragStartX()) >= 2;
            for (const auto& [k, start] : dragStart)
            {
                auto edit = start;
                edit.stretch = k == dragNote ? ratio : std::clamp (start.stretch * factor, 0.25f, 4.0f);
                model.edits[k] = edit;
            }
            hearDragged();
            repaint();
            return;
        }
        if (drag == Drag::modulation || drag == Drag::drift)
        {
            // Melodyne's Pitch Modulation / Pitch Drift tools: up = more, 100 px = 100 %, 0 .. 200 %
            const auto change = (float) -e.getDistanceFromDragStartY() / 100.0f;
            dragMoved = dragMoved || e.getDistanceFromDragStartY() != 0;
            for (const auto& [k, start] : dragStart)
            {
                auto edit = start;
                auto& v = drag == Drag::modulation ? edit.modulation : edit.drift;
                v = std::clamp (std::round ((v + change) * 100.0f) / 100.0f, 0.0f, 2.0f);
                model.edits[k] = edit;
            }
            hearDragged();
            repaint();
            return;
        }
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
        hearDragged();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const auto was = std::exchange (drag, Drag::none);
        if (was == Drag::band)
        {
            band = {};
            showBand = false;
            syncLevel();
            repaint();
            return;
        }
        if (dragHeard)
        {
            // (Melodyne: the note sounds while it is dragged and stops when it is let go)
            engine.previewSample (nullptr);
            dragHeard = false;
        }
        ++previewGeneration;
        if (was != Drag::none && dragMoved)
            commit();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = noteAt (e.position);
        if (hit < 0)
            return;
        if (assignMode && tool == Tool::main)
        {
            toggleAssignment (hit);
            return;
        }
        if (! model.notes[(std::size_t) hit].active)
            return;
        if (tool == Tool::modulation || tool == Tool::drift)
        {
            // Melodyne: a double-click sets the selected notes' modulation (drift) to 0 %, the next one back to 100 %
            if (selected.count (hit) == 0)
                selected = { hit };
            const auto isModulation = tool == Tool::modulation;
            const auto anyOn = std::any_of (selected.begin(), selected.end(), [&] (int k) {
                const auto ed = editOf (k);
                return (isModulation ? ed.modulation : ed.drift) != 0.0f;
            });
            for (auto k : selected)
                (isModulation ? model.edits[k].modulation : model.edits[k].drift) = anyOn ? 0.0f : 1.0f;
            commit();
            repaint();
            return;
        }
        if (tool == Tool::draw)
        {
            if (model.edits.count (hit))
                model.edits[hit].curve.clear();
            commit();
            repaint();
            return;
        }
        selected = { hit };
        resetSelection();
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
                if (valid (k) && model.notes[(std::size_t) k].active)
                    model.edits[k].mute = anyOn;
            commit();
            return true;
        }
        if (bufferMode() && key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'Y'))
        {
            bufferStep (key.getKeyCode() == 'Z' && ! key.getModifiers().isShiftDown() ? -1 : 1);
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
        {
            selected.clear();
            for (int k = 0; k < (int) model.notes.size(); ++k)
                if (model.notes[(std::size_t) k].active)
                    selected.insert (k);
            syncLevel();
            repaint();
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
    enum class Tool
    {
        main,
        modulation,
        drift,
        draw
    };
    enum class Drag
    {
        none,
        move,
        stretch,
        modulation,
        drift,
        draw,
        band
    };

    bool valid (int k) const noexcept { return k >= 0 && k < (int) model.notes.size(); }
    bool bufferMode() const noexcept { return (bool) apply; }

    // the editor's Options menu: the LYNX extra that prepares edited clips (also in Settings › LYNX extras)
    void showOptions()
    {
        juce::PopupMenu menu;
        menu.addItem (tr ("Prepare edited clips when a project opens (LYNX)"), true, PolyEditorWindows::prepareOnOpen, [] {
            PolyEditorWindows::prepareOnOpen = ! PolyEditorWindows::prepareOnOpen;
            if (mixeroptions::changed)
                mixeroptions::changed(); // (the main window writes ui-options.txt)
        });
        menu.addSeparator();
        menu.addItem (tr ("Select all notes") + "\tCtrl+A", state == State::ready, false, [this] {
            selected.clear();
            for (int k = 0; k < (int) model.notes.size(); ++k)
                if (model.notes[(std::size_t) k].active)
                    selected.insert (k);
            syncLevel();
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton));
    }

    void setTool (Tool t)
    {
        tool = t;
        for (auto [b, which] : { std::pair { &mainTool, Tool::main }, std::pair { &modulationTool, Tool::modulation }, std::pair { &driftTool, Tool::drift },
                                 std::pair { &drawTool, Tool::draw } })
            b->setToggleState (which == t, juce::dontSendNotification);
        updateStatus();
        repaint();
    }

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
        notesChanged();
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
        {
            // (back where it was: a render still running for another state is no longer wanted)
            if (rendering)
                ++renderGeneration, rendering = false;
            updateStatus();
            repaint();
            return;
        }
        const auto generation = ++renderGeneration;
        rendering = true;
        updateStatus();
        worker->post ([this, blob, m = model, src = sample, a = analysis, generation, buffer = bufferMode()] (const std::atomic<bool>& cancel) {
            std::shared_ptr<const SampleData> out = polyedit::render (*src, m, a.get(), &cancel);
            if (cancel.load())
                return;
            juce::Component::SafePointer<PolyEditor> self (this);
            juce::MessageManager::callAsync ([self, blob, out, generation, buffer] {
                if (self == nullptr || generation != self->renderGeneration)
                    return;
                self->rendering = false;
                if (out != nullptr && ! buffer)
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

    // the channel's polyEdit (the channel must still play this sample); in buffer mode the editor's own history
    void publish (const std::vector<std::uint8_t>& blob, bool undoable = true)
    {
        committed = blob;
        if (bufferMode())
        {
            if (! undoable || bufferHistory.empty())
            {
                bufferHistory = { { blob, rendered } };
                bufferAt = 0;
                return;
            }
            bufferHistory.resize ((std::size_t) bufferAt + 1);
            bufferHistory.push_back ({ blob, rendered });
            // (a bounded history: the oldest states go; each keeps its render, a few MB per minute of audio)
            while (bufferHistory.size() > 32)
                bufferHistory.erase (bufferHistory.begin() + 1);
            bufferAt = (int) bufferHistory.size() - 1;
            return;
        }
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

    // buffer mode: Ctrl+Z / Ctrl+Y in the editor
    void bufferStep (int step)
    {
        if (! bufferMode() || rendering || bufferHistory.empty())
            return;
        const auto at = std::clamp (bufferAt + step, 0, (int) bufferHistory.size() - 1);
        if (at == bufferAt)
            return;
        bufferAt = at;
        const auto& [blob, audio] = bufferHistory[(std::size_t) at];
        if (auto other = polyedit::parse (blob))
            adoptState (*other);
        committed = blob;
        rendered = audio;
        if (previewing)
            startPreview();
        updateStatus();
        repaint();
    }

    void applyToBuffer (bool now = false)
    {
        if (! bufferMode() || state != State::ready)
            return;
        auto out = rendered;
        if (model.edited() && (out == nullptr || rendering || now))
            out = polyedit::render (*sample, model, analysis.get());
        if (out == nullptr || ! model.edited())
            out = sample;
        auto fn = apply;
        if (now)
        {
            fn (out);
            return;
        }
        // (the window closes after: Edison's audio has changed under it)
        juce::MessageManager::callAsync ([fn, out] { fn (out); });
    }

    // the edits (and the note assignment) of another saved state of the same notes: undo / redo
    void adoptState (const polyedit::Model& other)
    {
        const auto assignmentChanged = polyedit::activeNotes (other) != polyedit::activeNotes (model);
        for (std::size_t i = 0; i < model.notes.size() && i < other.notes.size(); ++i)
            model.notes[i].active = other.notes[i].active;
        model.edits = other.edits;
        if (assignmentChanged)
        {
            analysis = polyedit::toAnalysis (model);
            notesChanged();
        }
        for (auto it = selected.begin(); it != selected.end();)
            it = valid (*it) ? std::next (it) : selected.erase (it);
        syncLevel();
    }

    // Melodyne's note assignment: a candidate becomes a note, or a note a candidate (its edit goes); one undo step. The
    // masks depend on which notes are active, so the analysis object is made again.
    void toggleAssignment (int k, bool commitNow = true)
    {
        if (! valid (k))
            return;
        auto& n = model.notes[(std::size_t) k];
        n.active = ! n.active;
        if (! n.active)
            model.edits.erase (k);
        analysis = polyedit::toAnalysis (model);
        notesChanged();
        selected = { k };
        if (commitNow)
            commit();
        updateStatus();
        repaint();
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
            if (valid (k) && model.notes[(std::size_t) k].active)
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

    // What a dragged note is heard from: the edit state without "take background" (the note's claim on what no note owns
    // and its 2 s tail cost 250 - 700 ms a render; without them ~60 - 90 ms for a 2 s note). The committed render keeps it.
    polyedit::Model previewModel() const
    {
        auto m = model;
        m.takeBackground = false;
        return m;
    }

    // Round 190lo: a pressed note gets its masks ready on the worker (the first render of a span builds them), so that
    // dragging it is heard at once
    void warmPreview (int k)
    {
        if (! valid (k) || sample == nullptr || analysis == nullptr)
            return;
        worker->postPreview ([m = previewModel(), k, src = sample, a = analysis] (const std::atomic<bool>&) {
            std::int64_t from = 0;
            (void) polyedit::renderNote (*src, m, k, *a, &from); // (the result is thrown away; the masks stay)
        });
    }

    // Round 190lo: the dragged note heard as it is now (PitchNet's renderNotes: the note alone, its edit applied); the
    // newest state wins, a render that comes back after the mouse was let go is dropped. Nothing of this runs while no note
    // is dragged.
    void hearDragged()
    {
        if (! valid (dragNote) || sample == nullptr || analysis == nullptr)
            return;
        const auto e = editOf (dragNote);
        // (only when what is heard changes: a whole step of pitch, a move, a new length or scale)
        const auto key = juce::String (e.semitones, 2) + "|" + juce::String (std::round (e.moveSeconds * 50.0) / 50.0, 3) + "|" + juce::String (e.stretch, 2) + "|"
                         + juce::String (e.modulation, 2) + "|" + juce::String (e.drift, 2);
        if (key == heardKey)
            return;
        heardKey = key;
        const auto generation = ++previewGeneration;
        if (previewing)
        {
            previewing = false;
            updateStatus();
        }
        worker->postPreview ([this, m = previewModel(), k = dragNote, src = sample, a = analysis, generation] (const std::atomic<bool>&) {
            std::int64_t from = 0;
            auto part = polyedit::renderNote (*src, m, k, *a, &from);
            if (part == nullptr)
                return;
            juce::Component::SafePointer<PolyEditor> self (this);
            juce::MessageManager::callAsync ([self, part = std::shared_ptr<const SampleData> (std::move (part)), generation] {
                if (self == nullptr || generation != self->previewGeneration || self->drag == Drag::none)
                    return;
                self->engine.previewSample (part, false, 0.8f);
                self->dragHeard = true;
            });
        });
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
        followSong();
    }

    // the song changed under the editor (undo / redo): its edit state follows
    void followSong()
    {
        if (bufferMode())
            return;
        const auto& song = engine.getSong();
        if (state == State::ready && ! rendering && drag == Drag::none && channel >= 0 && channel < (int) song.channels.size())
        {
            const auto& c = song.channels[(std::size_t) channel];
            if (c.samplePath == path && c.polyEdit != committed && ! c.polyEdit.empty())
                if (auto other = polyedit::parse (c.polyEdit); other && polyedit::sameNotes (*other, model))
                {
                    adoptState (*other);
                    committed = c.polyEdit;
                    rendered = polyedit::renderedFor (path, sample, committed);
                    if (rendered == sample)
                        rendered = nullptr;
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
            const auto notes = (int) polyedit::activeNotes (model).size();
            const auto candidates = (int) model.notes.size() - notes;
            text = notes == 0 ? tr ("No notes found") : tr ("{0} notes", { juce::String (notes) });
            if (assignMode)
                text << "  -  " << tr ("{0} candidates", { juce::String (candidates) });
            if (rendering)
                text << "  -  " << tr ("Rendering...");
            else if (assignMode)
                text << "  -  " << tr ("Double-click a faint candidate to make it a note, or a note to make it a candidate.");
            else if (tool == Tool::modulation)
                text << "  -  " << tr ("Drag a note up / down: its vibrato from 0 to 200 %.") << modulationText();
            else if (tool == Tool::drift)
                text << "  -  " << tr ("Drag a note up / down: its pitch drift from 0 to 200 %.") << modulationText();
            else if (tool == Tool::draw)
                text << "  -  " << tr ("Draw over a note to set its pitch line. Alt = erase, double-click = clear.");
            else if (notes > 0)
                text << "  -  " << tr ("Drag a note: up / down = pitch (Alt: cents), left / right = time, its end = length. Del = mute, R = reset.");
        }
        status.setText (text, juce::dontSendNotification);
        cancelButton.setVisible (state == State::analysing);
        playButton.setButtonText (previewing ? tr ("Stop") : tr ("Play"));
        playButton.setEnabled (sample != nullptr);
        resetButton.setEnabled (state == State::ready);
        originalButton.setEnabled (state == State::ready);
        applyButton.setEnabled (state == State::ready && ! rendering);
        level.setEnabled (state == State::ready && ! selected.empty());
        if (state == State::analysing)
            repaint();
    }

    // the selected note's scales in the status line ("Modulation 80 %")
    juce::String modulationText() const
    {
        if (selected.size() != 1 || ! valid (*selected.begin()))
            return {};
        const auto e = editOf (*selected.begin());
        const auto v = tool == Tool::modulation ? e.modulation : e.drift;
        return "  " + (tool == Tool::modulation ? tr ("Pitch modulation") : tr ("Pitch drift")) + " " + juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }

    // --- geometry ---------------------------------------------------------------------------------------------------
    juce::Rectangle<int> viewArea() const { return getLocalBounds().withTrimmedTop (pal::headerHeight).withTrimmedBottom (pal::footerHeight); }
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
        if (! polyedit::activeNotes (model).empty())
        {
            lo = 200.0, hi = 0.0;
            for (const auto& n : model.notes)
                if (n.active)
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

    // per-note values the paint and the hit tests use: the pitch range of the track (cheap bounds before building a path)
    void notesChanged()
    {
        pitchRange.assign (model.notes.size(), { 0.0f, 0.0f });
        loudest = 1e-12;
        for (std::size_t k = 0; k < model.notes.size(); ++k)
        {
            const auto& n = model.notes[k];
            if (n.active)
                loudest = std::max (loudest, n.energy);
            float lo = 1e9f, hi = -1e9f;
            for (auto f : n.f0)
                if (f > 0.0f)
                {
                    const auto p = 69.0f + 12.0f * std::log2 (f / 440.0f);
                    lo = std::min (lo, p), hi = std::max (hi, p);
                }
            pitchRange[k] = lo <= hi ? std::pair { lo, hi } : std::pair { 0.0f, 0.0f };
        }
        // the candidates note assignment offers (Melodyne shows its possible notes as plain blobs at one pitch): long
        // enough to be a note (>= 0.15 s), and not the double of a note that is already there (same pitch, mostly the same time)
        candidateShown.assign (model.notes.size(), 0);
        const auto fs = std::max (1e-6, model.frameSeconds());
        auto& median = medians;
        median.assign (model.notes.size(), 0.0f);
        std::vector<std::size_t> actives;
        for (std::size_t k = 0; k < model.notes.size(); ++k)
        {
            median[k] = model.notes[k].medianMidi();
            if (model.notes[k].active)
                actives.push_back (k);
        }
        for (std::size_t k = 0; k < model.notes.size(); ++k)
        {
            const auto& c = model.notes[k];
            if (c.active || (c.endFrame - c.startFrame + 1) * fs < 0.15)
                continue;
            const auto doubled = std::any_of (actives.begin(), actives.end(), [&] (std::size_t a) {
                const auto& n = model.notes[a];
                const auto overlap = std::min (n.endFrame, c.endFrame) - std::max (n.startFrame, c.startFrame);
                return std::abs (median[a] - median[k]) < 0.5f && overlap > (c.endFrame - c.startFrame) / 2;
            });
            candidateShown[k] = doubled ? 0 : 1;
        }
    }

    // the output frame of the note's frame i under its stretch (PitchNet's StretchMap: the attack keeps its speed; a
    // shortened note is cut); < 0 = cut
    double outFrame (const polyedit::Note& n, const polyedit::Edit& e, int i) const
    {
        const auto ratio = (double) std::clamp (e.stretch, 0.25f, 4.0f);
        if (ratio == 1.0)
            return n.startFrame + i;
        const auto start = (double) n.startFrame;
        if (ratio < 1.0)
        {
            const auto end = start + std::max (0, n.endFrame - n.startFrame) * ratio;
            return start + i <= end ? start + i : -1.0;
        }
        const auto anchor = start + std::max (0.0, std::round (0.08 / std::max (1e-6, model.frameSeconds())));
        const auto f = start + i;
        return f <= anchor ? f : anchor + (f - anchor) * ratio;
    }

    // the stretch ratio that puts a note's last frame `frames` after its first
    double stretchFor (const polyedit::Note& n, double frames) const
    {
        const auto len = (double) std::max (1, n.endFrame - n.startFrame);
        if (frames <= len)
            return frames / len;
        const auto anchor = std::max (0.0, std::round (0.08 / std::max (1e-6, model.frameSeconds())));
        return anchor >= len ? 1.0 : (frames - anchor) / (len - anchor);
    }

    // where a note's blob is, cheaply (no path): time span and pitch span, with its edit
    juce::Rectangle<float> boundsOf (int k) const
    {
        const auto& n = model.notes[(std::size_t) k];
        const auto e = n.active ? editOf (k) : polyedit::Edit {};
        const auto fs = model.frameSeconds();
        const auto last = outFrame (n, e, std::max (0, n.endFrame - n.startFrame));
        const auto endFrame = last >= 0.0 ? last : n.startFrame + std::max (0, n.endFrame - n.startFrame) * (double) e.stretch;
        const auto x0 = xOfSeconds (n.startFrame * fs + e.moveSeconds), x1 = xOfSeconds ((endFrame + 1.0) * fs + e.moveSeconds);
        auto lo = k < (int) pitchRange.size() ? pitchRange[(std::size_t) k].first : 0.0f, hi = k < (int) pitchRange.size() ? pitchRange[(std::size_t) k].second : 0.0f;
        lo += e.semitones, hi += e.semitones;
        if (e.pitchMoves())
            lo -= 3.0f, hi += 3.0f; // (scaled vibrato / a drawn line: wider)
        return juce::Rectangle<float> (x0, yOfPitch (hi) - (float) rowHeight, std::max (1.0f, x1 - x0), (float) (hi - lo) * (float) rowHeight + 2.0f * (float) rowHeight);
    }

    // the blob's outline: along the pitch track, thicker for louder notes, tapering at both ends
    juce::Path blobOf (int k, bool atOldPlace) const
    {
        const auto& n = model.notes[(std::size_t) k];
        const auto e = atOldPlace || ! n.active ? polyedit::Edit {} : editOf (k);
        juce::Path p;
        if (n.f0.empty())
            return p;
        const auto fs = model.frameSeconds();
        const auto gain = (float) std::pow (10.0, e.gainDb / 20.0);
        const auto energy = n.active ? n.energy : std::min (n.energy, loudest * 0.3);
        const auto half = (float) std::clamp (rowHeight * 0.48 * std::sqrt (std::min (1.0, energy / std::max (1e-12, loudest)) * gain), 1.5, rowHeight * 0.9);
        const auto frames = (int) n.f0.size();
        const auto curve = e.pitchMoves() ? polyedit::pitchCurveOf (n, e, fs) : std::vector<float>();
        const auto flatPitch = n.active ? 0.0f : (k < (int) medians.size() ? medians[(std::size_t) k] : n.medianMidi());
        std::vector<juce::Point<float>> lower;
        const auto spacing = fs * pixelsPerSecond * std::max (0.25, (double) e.stretch);
        const auto step = std::max (1, (int) std::floor (2.0 / std::max (1e-6, spacing))); // (about 2 px apart)
        int lastFrame = -1;
        for (int i = 0; i < frames; i += step)
        {
            const auto f = n.f0[(std::size_t) i];
            if (f <= 0.0f)
                continue;
            const auto at = outFrame (n, e, i);
            if (at < 0.0)
                break;
            lastFrame = i;
            // (a candidate is drawn flat at its pitch: its track often jumps between partials)
            const auto pitch = n.active ? 69.0 + 12.0 * std::log2 (f / 440.0) + e.semitones + (i < (int) curve.size() ? curve[(std::size_t) i] : 0.0f) : (double) flatPitch;
            const auto x = xOfSeconds (at * fs + e.moveSeconds);
            const auto shown = (int) std::lround (outFrame (n, e, frames - 1) >= 0.0 ? frames : std::max (1.0, (n.endFrame - n.startFrame) * (double) e.stretch));
            const auto taper = (float) std::min ({ 1.0, (i + 1) / 4.0, (shown - i) / 6.0 });
            const auto h = std::max (1.0f, half * std::max (0.15f, taper));
            const auto y = yOfPitch (pitch);
            if (lower.empty())
                p.startNewSubPath (x, y - h);
            else
                p.lineTo (x, y - h);
            lower.push_back ({ x, y + h });
        }
        (void) lastFrame;
        for (auto it = lower.rbegin(); it != lower.rend(); ++it)
            p.lineTo (*it);
        p.closeSubPath();
        return p;
    }

    // the pitch line drawn inside an edited blob (Melodyne draws a note's pitch curve through it)
    juce::Path pitchLineOf (int k) const
    {
        const auto& n = model.notes[(std::size_t) k];
        const auto e = editOf (k);
        juce::Path p;
        const auto fs = model.frameSeconds();
        const auto curve = polyedit::pitchCurveOf (n, e, fs);
        const auto step = std::max (1, (int) std::floor (2.0 / std::max (1e-6, fs * pixelsPerSecond * std::max (0.25, (double) e.stretch))));
        bool started = false;
        for (int i = 0; i < (int) n.f0.size(); i += step)
        {
            const auto f = n.f0[(std::size_t) i];
            const auto at = outFrame (n, e, i);
            if (at < 0.0)
                break;
            if (f <= 0.0f)
                continue;
            const auto pitch = 69.0 + 12.0 * std::log2 (f / 440.0) + e.semitones + (i < (int) curve.size() ? curve[(std::size_t) i] : 0.0f);
            const juce::Point<float> pt (xOfSeconds (at * fs + e.moveSeconds), yOfPitch (pitch));
            if (! started)
                p.startNewSubPath (pt), started = true;
            else
                p.lineTo (pt);
        }
        return p;
    }

    bool shown (int k) const
    {
        return model.notes[(std::size_t) k].active || (assignMode && k < (int) candidateShown.size() && candidateShown[(std::size_t) k] != 0);
    }

    int noteAt (juce::Point<float> at, bool candidates = true) const
    {
        // the topmost (last drawn) blob under the mouse; a little slack around thin blobs. Notes before candidates.
        for (const auto wantActive : { true, false })
        {
            if (! wantActive && ! (candidates && assignMode))
                break;
            for (int k = (int) model.notes.size() - 1; k >= 0; --k)
            {
                if (model.notes[(std::size_t) k].active != wantActive || ! shown (k) || ! boundsOf (k).expanded (2.0f, 4.0f).contains (at))
                    continue;
                const auto b = blobOf (k, false);
                if (b.contains (at) || b.getBounds().expanded (0.0f, 3.0f).contains (at))
                    return k;
            }
        }
        return -1;
    }

    // the note whose end (its stretch handle: the last 6 px of the blob) is under the mouse
    int endHandleAt (juce::Point<float> at) const
    {
        for (int k = (int) model.notes.size() - 1; k >= 0; --k)
        {
            if (! model.notes[(std::size_t) k].active)
                continue;
            const auto r = boundsOf (k);
            if (at.x < r.getRight() - 6.0f || at.x > r.getRight() + 2.0f || r.getWidth() < 14.0f)
                continue;
            const auto b = blobOf (k, false).getBounds();
            if (at.y >= b.getY() - 3.0f && at.y <= b.getBottom() + 3.0f && at.x >= b.getRight() - 7.0f && at.x <= b.getRight() + 3.0f)
                return k;
        }
        return -1;
    }

    // the rubber band: the shown notes it touches join the selection (Melodyne: a box over the blobs)
    void selectInBand (bool keepBase)
    {
        selected = keepBase ? bandBase : std::set<int> {};
        const auto r = band.withWidth (std::max (1.0f, band.getWidth())).withHeight (std::max (1.0f, band.getHeight()));
        for (int k = 0; k < (int) model.notes.size(); ++k)
            if (model.notes[(std::size_t) k].active && boundsOf (k).intersects (r) && blobOf (k, false).getBounds().intersects (r))
                selected.insert (k);
    }

    // the draw tool: the note's drawn line follows the mouse (frames between two mouse points are filled in)
    void drawAt (juce::Point<float> at, bool erase)
    {
        if (! valid (drawNote))
            return;
        const auto& n = model.notes[(std::size_t) drawNote];
        auto& e = model.edits[drawNote];
        const auto fs = model.frameSeconds();
        if (e.curve.size() != n.f0.size())
            e.curve.resize (n.f0.size(), 0.0f);
        // the note's frame under x (no stretch: the line is drawn over the note's own frames)
        const auto frameAt = [&] (float x) { return (int) std::lround ((secondsOfX (x) - e.moveSeconds) / fs) - n.startFrame; };
        auto base = e;
        base.curve.clear();
        const auto rest = polyedit::pitchCurveOf (n, base, fs);
        const auto setFrame = [&] (int i, double target) {
            if (i < 0 || i >= (int) n.f0.size() || n.f0[(std::size_t) i] <= 0.0f)
                return;
            if (erase)
            {
                e.curve[(std::size_t) i] = 0.0f;
                return;
            }
            const auto own = 69.0 + 12.0 * std::log2 (n.f0[(std::size_t) i] / 440.0) + e.semitones + (i < (int) rest.size() ? rest[(std::size_t) i] : 0.0f);
            e.curve[(std::size_t) i] = (float) std::clamp (target - own, -24.0, 24.0);
        };
        const auto i1 = frameAt (at.x);
        const auto p1 = pitchOfY (at.y);
        if (lastDraw.has_value())
        {
            const auto [i0, p0] = *lastDraw;
            const auto lo = std::min (i0, i1), hi = std::max (i0, i1);
            for (int i = lo; i <= hi; ++i)
                setFrame (i, hi == lo ? p1 : p0 + (p1 - p0) * (double) (i - i0) / (double) (i1 - i0));
        }
        else
            setFrame (i1, p1);
        lastDraw = std::pair { i1, p1 };
        if (std::none_of (e.curve.begin(), e.curve.end(), [] (float v) { return v != 0.0f; }))
            e.curve.clear();
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
        if (pitchRange.size() != model.notes.size())
            notesChanged();
        const auto visible = area.toFloat();
        const auto onScreen = [&] (int k) {
            const auto r = boundsOf (k);
            return r.getRight() >= visible.getX() && r.getX() <= visible.getRight() && r.getBottom() >= visible.getY() && r.getY() <= visible.getBottom();
        };
        // note assignment: the rejected candidates, faint and outlined (Melodyne's possible notes), under the notes
        if (assignMode)
            for (int k = 0; k < (int) model.notes.size(); ++k)
            {
                if (model.notes[(std::size_t) k].active || ! shown (k) || ! onScreen (k))
                    continue;
                const auto blob = blobOf (k, false);
                g.setColour (pal::blob.withAlpha (selected.count (k) ? 0.35f : 0.05f)); // (Melodyne: possible notes are outlines)
                g.fillPath (blob);
                g.setColour (selected.count (k) ? juce::Colours::white : pal::blob.withAlpha (0.55f));
                g.strokePath (blob, juce::PathStrokeType (selected.count (k) ? 1.4f : 0.8f));
            }
        // where edited notes were: a dashed outline
        for (int k = 0; k < (int) model.notes.size(); ++k)
        {
            if (! model.notes[(std::size_t) k].active)
                continue;
            const auto e = editOf (k);
            if (e.identity() || (e.semitones == 0.0f && e.moveSeconds == 0.0 && ! e.mute && e.stretch == 1.0f && ! e.pitchMoves()) || ! onScreen (k))
                continue;
            const auto old = blobOf (k, true);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, old, dashes, 2);
            g.setColour (pal::orangeLine.withAlpha (0.7f));
            g.fillPath (dashed);
        }
        // (the selected notes last: on top of the others, as Melodyne draws them)
        std::vector<int> order;
        order.reserve (model.notes.size());
        for (int k = 0; k < (int) model.notes.size(); ++k)
            if (model.notes[(std::size_t) k].active && selected.count (k) == 0)
                order.push_back (k);
        for (auto k : selected)
            if (valid (k) && model.notes[(std::size_t) k].active)
                order.push_back (k);
        for (const auto k : order)
        {
            if (! onScreen (k))
                continue;
            const auto blob = blobOf (k, false);
            const auto e = editOf (k);
            const auto isSelected = selected.count (k) > 0;
            auto colour = e.identity() ? pal::blob : pal::orange;
            if (e.mute)
                colour = colour.withAlpha (0.28f);
            g.setColour (colour);
            g.fillPath (blob);
            g.setColour (isSelected ? juce::Colours::white : pal::pitchCore.withAlpha (0.55f));
            g.strokePath (blob, juce::PathStrokeType (isSelected ? 1.6f : 0.8f));
            // a note with a changed pitch movement shows its pitch line (Melodyne's pitch curve through the blob)
            if (e.pitchMoves() || (isSelected && tool != Tool::main))
            {
                g.setColour (pal::pitchCore.withAlpha (0.8f));
                g.strokePath (pitchLineOf (k), juce::PathStrokeType (1.2f));
            }
            // the stretch handle of a selected note (Melodyne's time handle at its end)
            if (isSelected && tool == Tool::main)
            {
                const auto b = blob.getBounds();
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRect (juce::Rectangle<float> (b.getRight() - 3.0f, b.getCentreY() - 5.0f, 3.0f, 10.0f));
            }
            // the note's name at its start, as Newtone writes it
            const auto& n = model.notes[(std::size_t) k];
            const auto b = blob.getBounds();
            if (b.getWidth() > 28.0f && rowHeight >= 9.0)
            {
                g.setColour (pal::pitchCore.withAlpha (e.mute ? 0.3f : 0.85f));
                g.setFont (juce::FontOptions ((float) std::min (11.0, rowHeight * 0.8)));
                g.drawText (noteName ((k < (int) medians.size() ? medians[(std::size_t) k] : n.medianMidi()) + e.semitones), b.withWidth (40.0f).translated (4.0f, 0.0f),
                            juce::Justification::centredLeft, false);
            }
        }
        if (showBand && ! band.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRect (band);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawRect (band, 1.0f);
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
    Apply apply;
    std::string path;
    juce::String channelName;
    std::shared_ptr<const SampleData> sample, rendered;
    polyedit::Model model;
    std::shared_ptr<polyphonic::Analysis> analysis;
    std::vector<std::uint8_t> committed;
    State state = State::analysing;
    Tool tool = Tool::main;
    Drag drag = Drag::none;
    std::atomic<float> progress { 0.0f };
    bool rendering = false, previewing = false, dragMoved = false, syncingLevel = false, assignMode = false, dragHeard = false, showBand = false;
    int renderGeneration = 0, previewGeneration = 0, dragNote = -1, drawNote = -1;
    juce::String heardKey;
    std::optional<std::pair<int, double>> lastDraw;
    double playAt = -1.0, loudest = 1e-12;
    double pixelsPerSecond = 100.0, scrollSeconds = 0.0, topPitch = 84.0, rowHeight = 14.0;
    std::set<int> selected, bandBase;
    juce::Rectangle<float> band;
    std::map<int, polyedit::Edit> dragStart;
    std::vector<std::pair<float, float>> pitchRange;
    std::vector<char> candidateShown;
    std::vector<float> medians; // (each note's pitch: notesChanged())
    // buffer mode: the editor's own history (blob, its render)
    std::vector<std::pair<std::vector<std::uint8_t>, std::shared_ptr<const SampleData>>> bufferHistory;
    int bufferAt = 0;
    std::unique_ptr<Worker> worker = std::make_unique<Worker>();
    juce::TextButton playButton { tr ("Play") }, originalButton { tr ("Original") }, resetButton { tr ("Reset") }, cancelButton { tr ("Cancel") },
        mainTool { tr ("Notes") }, modulationTool { tr ("Modulation") }, driftTool { tr ("Drift") }, drawTool { tr ("Draw pitch") },
        assignButton { tr ("Note assignment") }, applyButton { tr ("Apply") }, optionsButton { tr ("Options") };
    juce::Label levelLabel, status;
    juce::Slider level;
};

class PolyEditorWindow final : public juce::DocumentWindow
{
public:
    PolyEditorWindow (AudioEngine& engine, int channel, const juce::String& title, std::shared_ptr<const SampleData> buffer = nullptr,
                      PolyEditor::Apply apply = {})
        : DocumentWindow (title, theme::colour::bg2, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new PolyEditor (engine, channel, false, std::move (buffer), std::move (apply)), true);
        setResizable (true, false);
        setResizeLimits (720, 360, 4000, 2400);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        dockOrKeepAbove (*this);
    }
    void closeButtonPressed() override;
};

// channel windows by channel; buffer windows (Edison) by negative numbers
std::map<int, std::unique_ptr<PolyEditorWindow>>& windows()
{
    static std::map<int, std::unique_ptr<PolyEditorWindow>> open;
    return open;
}

void closeWindow (PolyEditorWindow* which)
{
    for (auto& [key, window] : windows())
        if (window.get() == which)
        {
            juce::MessageManager::callAsync ([k = key] { windows().erase (k); });
            return;
        }
}

void PolyEditorWindow::closeButtonPressed()
{
    closeWindow (this);
}

// "Prepare edited clips when a project opens": one low-priority thread, one clip after the other
std::unique_ptr<Worker>& preparer()
{
    static std::unique_ptr<Worker> worker;
    return worker;
}
} // namespace

namespace PolyEditorWindows
{
bool prepareOnOpen = false;

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

void openBuffer (AudioEngine& engine, std::shared_ptr<const SampleData> buffer, const juce::String& title,
                 std::function<void (std::shared_ptr<const SampleData>)> apply)
{
    if (buffer == nullptr || buffer->left.empty() || ! apply)
        return;
    auto& open = windows();
    int key = -1;
    while (open.count (key))
        --key;
    auto* slot = &open[key];
    // Apply hands the render over, then the window closes (the buffer it was made from has changed)
    auto done = [apply = std::move (apply), key] (std::shared_ptr<const SampleData> out) {
        apply (std::move (out));
        if (auto it = windows().find (key); it != windows().end())
            closeWindow (it->second.get());
    };
    *slot = std::make_unique<PolyEditorWindow> (engine, -1, tr ("Edit notes (polyphonic)") + (title.isNotEmpty() ? " - " + title : juce::String()), std::move (buffer),
                                                std::move (done));
}

int prepareEditedClips (AudioEngine& engine)
{
    if (! prepareOnOpen)
        return 0;
    struct Job
    {
        std::string path;
        std::shared_ptr<const SampleData> sample;
        polyedit::Model model;
    };
    std::vector<Job> jobs;
    for (const auto& c : engine.getSong().channels)
    {
        if (c.instrument != InstrumentKind::audioClip || c.polyEdit.empty())
            continue;
        auto m = polyedit::parse (c.polyEdit);
        if (! m || ! m->edited() || std::any_of (jobs.begin(), jobs.end(), [&c] (const Job& j) { return j.path == c.samplePath; }))
            continue;
        auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (c.samplePath.c_str()));
        if (sample != nullptr && ! sample->left.empty())
            jobs.push_back ({ c.samplePath, std::move (sample), std::move (*m) });
    }
    if (jobs.empty())
        return 0;
    auto& worker = preparer();
    if (worker == nullptr)
        worker = std::make_unique<Worker>();
    const auto count = (int) jobs.size();
    worker->post ([jobs = std::move (jobs)] (const std::atomic<bool>& cancel) {
        for (const auto& j : jobs)
        {
            if (cancel.load())
                return;
            std::shared_ptr<polyphonic::Analysis> a = polyedit::toAnalysis (j.model);
            if (polyedit::prepareMasks (*j.sample, j.model, *a, &cancel))
                polyedit::keepPrepared (j.path, j.model, std::move (a));
        }
    });
    return count;
}

bool preparing()
{
    return preparer() != nullptr && preparer()->busy();
}

void closeAll()
{
    windows().clear();
    preparer().reset();
    polyedit::clearPrepared();
}
} // namespace PolyEditorWindows

std::unique_ptr<juce::Component> makePolyEditor (AudioEngine& engine, int channel, bool analyseNow)
{
    return std::make_unique<PolyEditor> (engine, channel, analyseNow);
}

std::unique_ptr<juce::Component> makePolyBufferEditor (AudioEngine& engine, std::shared_ptr<const SampleData> buffer, bool analyseNow,
                                                       std::function<void (std::shared_ptr<const SampleData>)> apply)
{
    return std::make_unique<PolyEditor> (engine, -1, analyseNow, std::move (buffer), std::move (apply));
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
