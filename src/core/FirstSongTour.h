#pragma once

// The Simple workflow's first-song tour (round 190dv; BandLab's "Welcome to Studio" + coach-mark lesson, studied in
// bandlab182d/REPORT.md §1 and simple184a/DESIGN.md §2.3.3, clean-room: LYNX's own steps, words and look).
//
// One real song is built step by step: a beat, the song's key, an instrument, chords from the Chord pad, a voice or a
// loop that follows the key, a one-knob effect, then Finish (a Master look) and save. Each step names the controls it
// points at (component IDs, found in the window by ui/FirstSongTour.cpp: never screen coordinates) and is done when
// the SONG shows it (satisfied() reads the model: no timers, no clicks counted). Doing a step advances the tour; Next
// moves on without doing it, Back goes back, Skip ends the tour for good, and closing the card pauses it (the state is
// kept in the settings folder, so it resumes after a restart and Help reopens it).
// No JUCE: tests/FirstSongTour190dvTests.inc drives everything below.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/GoalKnobs.h"
#include "core/MasterLooks.h"
#include "core/ProjectKey.h"
#include "core/ScaleKeys.h"
#include "core/SimpleTracks.h"
#include "core/Song.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::tour
{
enum Step : int { beat = 0, key, instrument, chords, loop, effect, finish };
constexpr int stepCount = 7; // the end card ("You're all set") is step == stepCount

inline const char* stepId (int step) noexcept
{
    static const char* ids[] { "beat", "key", "instrument", "chords", "loop", "effect", "finish" };
    return step >= 0 && step < stepCount ? ids[step] : "end";
}

// English, for tr(): the card's title and its one sentence
inline const char* title (int step) noexcept
{
    switch (step)
    {
        case beat: return TR_NOOP ("Pick a beat");
        case key: return TR_NOOP ("Choose the song's key");
        case instrument: return TR_NOOP ("Add keys or a bass");
        case chords: return TR_NOOP ("Play chords");
        case loop: return TR_NOOP ("Add a voice or a loop");
        case effect: return TR_NOOP ("Shape a sound with one knob");
        case finish: return TR_NOOP ("Finish the song");
        default: return TR_NOOP ("You're all set");
    }
}

inline const char* text (int step) noexcept
{
    switch (step)
    {
        case beat: return TR_NOOP ("Click Add track, then Drums, and pick a starter beat. Press Space to hear it.");
        case key: return TR_NOOP ("Click Key on the toolbar and pick one: chords and loops follow it.");
        case instrument: return TR_NOOP ("Click Add track, then Instrument: a sound you can play at once.");
        case chords: return TR_NOOP ("In the Keyboard tab choose Chord pad and record a few chords, or click notes into the Editor tab.");
        case loop: return TR_NOOP ("Record your voice on a Voice track, or drag a loop onto the timeline: it is moved to the song's key.");
        case effect: return TR_NOOP ("Open a track's Effects tab, add Room or Echo and turn its knob.");
        case finish: return TR_NOOP ("Click Finish and pick a look for the whole song.");
        default: return TR_NOOP ("Your first song is made. Save it as a file, open a demo song to see how one is built, or keep going.");
    }
}

// The controls a step points at, best first (ui/FirstSongTour.cpp rings the first one that is on screen)
inline std::vector<std::string> targets (int step)
{
    switch (step)
    {
        case beat: return { "simple.starters", "simple.tile.drums", "simple.addTrack" };
        case key: return { "toolbar.key", "keyboard.key" };
        case instrument: return { "simple.tile.instrument", "simple.addTrack" };
        case chords: return { "keyboard.chordPad", "simple.dock.keyboard", "simple.dock.editor" };
        case loop: return { "simple.tile.voice", "simple.addTrack" };
        case effect: return { "simple.dock.addEffect", "simple.dock.effects" };
        case finish: return { "simple.finish.looks", "simple.finish" };
        default: return {};
    }
}

// Whether the card offers "Do it for me" (ui/FirstSongTour.cpp does it with the same code the user's action runs).
// A voice or a loop can't be made up: that step offers the loops instead.
inline bool canDoForUser (int step) noexcept { return step >= 0 && step < stepCount && step != loop; }

// --- what the song shows ------------------------------------------------------------------------------------------------
// A Simple Drums track whose pattern has notes (a starter beat, or steps set by hand)
inline bool hasBeat (const Song& song)
{
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size() && ! song.patterns[(std::size_t) c.pattern].notes.empty()
            && simpletracks::kindOf (song, c.track) == simpletracks::Kind::drums)
            return true;
    return false;
}

// The first Simple Instrument / Sampler track (-1: none)
inline int firstInstrumentTrack (const Song& song)
{
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (const auto k = simpletracks::kindOf (song, t); k == simpletracks::Kind::instrument || k == simpletracks::Kind::sampler)
            return t;
    return -1;
}

// An Instrument / Sampler track with notes in its pattern
inline bool hasNotes (const Song& song)
{
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (const auto p = simpletracks::patternOf (song, t); p >= 0 && ! song.patterns[(std::size_t) p].notes.empty())
            return true;
    return false;
}

// An audio clip (a recorded take or a dropped loop) on a Simple audio track
inline bool hasAudio (const Song& song)
{
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::audio && simpletracks::kindOf (song, c.track) == simpletracks::Kind::audio)
            return true;
    return false;
}

// A one-knob effect on any mixer track
inline bool hasOneKnob (const Song& song)
{
    for (const auto& m : song.mixer)
        for (const auto& slot : m.effects)
            if (slot.goal != goals::none && goals::active (slot))
                return true;
    return false;
}

inline bool satisfied (const Song& song, int step)
{
    switch (step)
    {
        case beat: return hasBeat (song);
        case key: return projectkey::get (song).has_value();
        case instrument: return firstInstrumentTrack (song) >= 0;
        case chords: return hasNotes (song);
        case loop: return hasAudio (song);
        case effect: return hasOneKnob (song);
        case finish: return looks::present (song);
        default: return false;
    }
}

// --- "Do it for me", the parts that are plain song edits ----------------------------------------------------------------
// The key a tour song gets when the user lets the tour choose: A minor (the Key menu's second entry, as the demo song)
inline projectkey::Key defaultKey() { return { 9, projectkey::commonFirst().size() > 1 ? projectkey::commonFirst()[1] : 0 }; }

// Four chords of the project key (or `defaultKey` without one), one per bar on Instrument / Sampler track `track`'s
// pattern (made if needed): i – VI – III – VII in a minor key, I – V – vi – IV in a major one, triads from C3 up.
// Returns the notes written (0: not an Instrument / Sampler track).
inline int writeChords (Song& song, int track, int bars = 4)
{
    const auto pattern = simpletracks::ensurePattern (song, track, bars);
    if (pattern < 0)
        return 0;
    const auto channel = song.playlistTracks[(std::size_t) track].channel;
    const auto k = projectkey::get (song).value_or (defaultKey());
    const bool minor = projectkey::isMinor (k);
    const int minorDegrees[] { 0, 5, 2, 6 }, majorDegrees[] { 0, 4, 5, 3 };
    const auto bar = song.barTicks();
    auto& p = song.patterns[(std::size_t) pattern];
    int written = 0;
    for (int b = 0; b < bars; ++b)
        for (const auto n : scalekeys::diatonicChord (k, (minor ? minorDegrees : majorDegrees)[b % 4], 48, scalekeys::Complexity::triad, scalekeys::Spread::close))
        {
            Note note;
            note.channel = channel;
            note.key = n;
            note.start = b * bar;
            note.length = bar - ppq / 4;
            note.velocity = 0.72f;
            p.notes.push_back (note);
            ++written;
        }
    p.sortNotes();
    return written;
}

// A one-knob effect (Room) on the insert of `track` (a Simple track); false when the track has no insert or no room
inline bool addOneKnob (Song& song, int track, int goal = goals::room)
{
    const auto insert = simpletracks::insertOf (song, track);
    if (insert < 0 || insert >= (int) song.mixer.size() || song.mixer[(std::size_t) insert].effects.size() >= 10)
        return false;
    song.mixer[(std::size_t) insert].effects.push_back (goals::make (goal));
    return true;
}

// --- the progress -----------------------------------------------------------------------------------------------------
struct State
{
    bool welcomeSeen = false; // the Welcome screen was shown once (it opens by itself only the first time)
    bool started = false;     // a tour was started and is not finished or skipped (it can be continued)
    bool done = false;        // finished or skipped (Help still reopens it)
    int step = 0;             // where it is (stepCount = the end card)
    std::uint32_t passed = 0; // steps left with Next before they were done (not asked again on resume)
    std::uint32_t completed = 0; // steps the song showed done
    bool operator== (const State&) const = default;
};

inline std::string serialize (const State& s)
{
    std::ostringstream out;
    out << "welcome=" << (s.welcomeSeen ? 1 : 0) << "\nstarted=" << (s.started ? 1 : 0) << "\ndone=" << (s.done ? 1 : 0) << "\nstep=" << s.step
        << "\npassed=" << s.passed << "\ncompleted=" << s.completed << "\n";
    return out.str();
}

inline State parse (const std::string& text)
{
    State s;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto name = line.substr (0, eq);
        long long v = 0;
        try
        {
            v = std::stoll (line.substr (eq + 1));
        }
        catch (...)
        {
            continue;
        }
        if (name == "welcome") s.welcomeSeen = v != 0;
        else if (name == "started") s.started = v != 0;
        else if (name == "done") s.done = v != 0;
        else if (name == "step") s.step = (int) std::clamp<long long> (v, 0, stepCount);
        else if (name == "passed") s.passed = (std::uint32_t) (v & 0x7f);
        else if (name == "completed") s.completed = (std::uint32_t) (v & 0x7f);
    }
    return s;
}

class Machine
{
public:
    explicit Machine (State s = {}) : st (s) {}

    const State& state() const noexcept { return st; }
    bool running() const noexcept { return on; }       // the card is up
    bool canContinue() const noexcept { return st.started && ! st.done; }
    int step() const noexcept { return st.step; }
    bool atEnd() const noexcept { return st.step >= stepCount; }
    // The step on the card is already done (shown with a tick; Next moves on). Only after Back: a step the song does
    // when it comes up is passed over at once.
    bool shownDone (const Song& song) const { return ! atEnd() && satisfied (song, st.step); }

    void welcomeShown() noexcept { st.welcomeSeen = true; }

    // A new tour: from the first step the song hasn't done
    void start (const Song& song)
    {
        st.started = true;
        st.done = false;
        st.passed = st.completed = 0;
        st.step = 0;
        on = true;
        skipDone (song);
    }

    // Continue a paused tour: the first step up to the saved one that is neither done nor passed with Next (the song
    // may be another one now), else the saved step; then past what the song already shows
    void resume (const Song& song)
    {
        if (! canContinue())
        {
            start (song);
            return;
        }
        on = true;
        const auto saved = std::clamp (st.step, 0, stepCount);
        st.step = saved;
        for (int i = 0; i < saved; ++i)
            if (! satisfied (song, i) && (st.passed & bit (i)) == 0)
            {
                st.step = i;
                break;
            }
        skipDone (song);
    }

    void pause() noexcept { on = false; } // the card closed: kept as it is, Help continues it

    void next (const Song& song)
    {
        if (! on || atEnd())
            return;
        if (! satisfied (song, st.step))
            st.passed |= bit (st.step);
        ++st.step;
        skipDone (song);
    }

    void back() noexcept
    {
        if (! on || st.step <= 0)
            return;
        --st.step;
        waitForChange = true; // (a step already done stays on the card until Next)
    }

    // Skip tour: ended for good (the Welcome screen won't come back; Help reopens it)
    void skip() noexcept
    {
        on = false;
        st.started = false;
        st.done = true;
    }

    // The end card was closed
    void finishTour() noexcept { skip(); }

    // The song changed: the step on the card is done → the next step not done yet. True when the step moved.
    bool observe (const Song& song)
    {
        if (! on || atEnd())
            return false;
        const auto now = satisfied (song, st.step);
        if (waitForChange)
        {
            if (! now)
                waitForChange = false; // (undone after Back: doing it again advances)
            return false;
        }
        if (! now)
            return false;
        const auto before = st.step;
        st.completed |= bit (st.step);
        ++st.step;
        skipDone (song);
        return st.step != before;
    }

    static constexpr std::uint32_t bit (int step) noexcept { return 1u << (std::uint32_t) step; }

private:
    void skipDone (const Song& song)
    {
        waitForChange = false;
        while (st.step < stepCount && satisfied (song, st.step))
        {
            st.completed |= bit (st.step);
            ++st.step;
        }
    }

    State st;
    bool on = false;
    bool waitForChange = false;
};
} // namespace lynx::tour
