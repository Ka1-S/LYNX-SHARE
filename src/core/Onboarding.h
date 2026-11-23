#pragma once

// First-run onboarding (round 190np; design: /mnt/e/lynx-work/onboard190nj/ONBOARDING-DESIGN.md, mockups M1–M5).
// LYNX's own: no other DAW asks at first run, so the card, its words and its icons are LYNX's (clean-room).
//
//  - The welcome card asks one question, "Where are you coming from?", with five choices. One click applies the
//    layout and the starter of the choice; Skip, × or Esc keep the Pattern layout (the default, unchanged). It shows
//    on a fresh profile only: a profile that already has LYNX settings (an upgrade) keeps what it had (the old Setup
//    wizard when it was never finished, else nothing).
//  - "Next: 3 things to try": three items per choice that tick themselves from the song (and a few view facts the
//    window passes in `Context`), never from timers or counted clicks.
//  - Empty-state hints for an empty Playlist (Pattern), an empty clip grid (Clip-launch) and an empty Simple song
//    whose New-track card was closed.
//  - At most one contextual tip per session, only on a confusion signal: Play in Song mode with an empty Playlist, and
//    Record (audio) with no input to record from. "Got it" retires that tip, "No more tips" all of them.
//
// No JUCE: tests/Onboarding190npTests.inc drives everything below.

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "core/LayoutStyle.h"
#include "core/SimpleTracks.h"
#include "core/Song.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text
#endif

namespace lynx::onboarding
{
// --- the five choices ---------------------------------------------------------------------------------------------------
enum class Choice : int { none = -1, mobile = 0, fl, live, timeline, newcomer };
constexpr int choiceCount = 5;

inline Choice choiceAt (int index) noexcept { return index >= 0 && index < choiceCount ? (Choice) index : Choice::none; }

inline const char* choiceId (Choice c) noexcept
{
    switch (c)
    {
        case Choice::mobile: return "mobile";
        case Choice::fl: return "fl";
        case Choice::live: return "live";
        case Choice::timeline: return "timeline";
        case Choice::newcomer: return "new";
        default: return "";
    }
}

inline Choice choiceFromId (const std::string& id) noexcept
{
    for (int i = 0; i < choiceCount; ++i)
        if (id == choiceId ((Choice) i))
            return (Choice) i;
    return Choice::none;
}

// The card's words (English, for tr()). Product names are plain text: no logos (they are trademarks of their owners).
inline const char* choiceTitle (Choice c) noexcept
{
    switch (c)
    {
        case Choice::mobile: return TR_NOOP ("BandLab, FL Studio Mobile or GarageBand");
        case Choice::fl: return TR_NOOP ("FL Studio");
        case Choice::live: return TR_NOOP ("Ableton Live or Bitwig");
        case Choice::timeline: return TR_NOOP ("Logic, Cubase, Reaper, Pro Tools or Studio One");
        case Choice::newcomer: return TR_NOOP ("I'm new to making music");
        default: return "";
    }
}

inline const char* choiceText (Choice c) noexcept
{
    switch (c)
    {
        case Choice::mobile: return TR_NOOP ("A mobile or simple app. Simple layout: tracks, loops, a drum machine and quick recording.");
        case Choice::fl: return TR_NOOP ("Pattern layout: Channel Rack, Piano roll, Playlist and Mixer, with FL's shortcuts. Your .flp projects open.");
        case Choice::live: return TR_NOOP ("Clip-launch layout: a clip grid with scenes beside the arrangement. Your .als sets open.");
        case Choice::timeline: return TR_NOOP ("Simple layout with the Mixer at hand: one track per instrument on a timeline.");
        case Choice::newcomer: return TR_NOOP ("Simple layout and a short first-song tour (about five minutes).");
        default: return "";
    }
}

// The layout a choice opens; no choice (Skip, ×, Esc) keeps the Pattern layout, LYNX's default
inline layoutstyle::Style layoutFor (Choice c) noexcept
{
    switch (c)
    {
        case Choice::mobile:
        case Choice::timeline:
        case Choice::newcomer: return layoutstyle::Style::simple;
        case Choice::live: return layoutstyle::Style::clipLaunch;
        default: return layoutstyle::Style::pattern;
    }
}

// What the choice starts with
enum class Starter { none, starterBeat, newTrackCard, tour };

inline Starter starterFor (Choice c) noexcept
{
    switch (c)
    {
        case Choice::mobile: return Starter::starterBeat;  // a Drums track with Simple's own starter beat
        case Choice::timeline: return Starter::newTrackCard; // empty, on the New-track card
        case Choice::newcomer: return Starter::tour;        // the first-song tour (round 190dv)
        default: return Starter::none;                      // FL Studio and Live: the project as it is
    }
}

inline bool showsMixer (Choice c) noexcept { return c == Choice::timeline; } // (owner decision O2: Simple with the Mixer)

// --- what happens at start-up -------------------------------------------------------------------------------------------
enum class StartAction { none, welcomeCard, setupWizard };

// `setupDone`: setup-done.txt exists (the old wizard or this card was answered once). `hadProfile`: the settings folder
// already held LYNX's UI settings before this start (an upgrade, or a user who never finished the old wizard).
// `welcomePending`: the card was shown and not answered yet (a restart for another language, the app closed).
inline StartAction startAction (bool setupDone, bool hadProfile, bool welcomePending) noexcept
{
    if (setupDone)
        return StartAction::none;
    if (welcomePending)
        return StartAction::welcomeCard;
    return hadProfile ? StartAction::setupWizard : StartAction::welcomeCard; // (an existing user keeps the old wizard)
}

// --- "Next: 3 things to try" --------------------------------------------------------------------------------------------
enum class Item : int { play = 0, addLoop, recordVoice, steps, openFlp, plugins, slotClip, launchScene, openAls, instrumentTrack, recordAudio, openMixer };
constexpr int itemCount = 12;

inline std::vector<Item> next3 (Choice c)
{
    switch (c)
    {
        case Choice::mobile: return { Item::play, Item::addLoop, Item::recordVoice };
        case Choice::fl: return { Item::steps, Item::openFlp, Item::plugins };
        case Choice::live: return { Item::slotClip, Item::launchScene, Item::openAls };
        case Choice::timeline: return { Item::instrumentTrack, Item::recordAudio, Item::openMixer };
        default: return {}; // (the newcomer has the tour)
    }
}

// The group a window without a remembered choice uses (Help › Getting started): its layout's
inline Choice choiceForLayout (layoutstyle::Style s) noexcept
{
    return s == layoutstyle::Style::simple ? Choice::mobile : s == layoutstyle::Style::clipLaunch ? Choice::live : Choice::fl;
}

inline const char* itemId (Item i) noexcept
{
    static const char* ids[] { "play", "addLoop", "recordVoice", "steps", "openFlp", "plugins", "slotClip", "launchScene", "openAls",
                               "instrumentTrack", "recordAudio", "openMixer" };
    return (int) i >= 0 && (int) i < itemCount ? ids[(int) i] : "";
}

inline const char* itemText (Item i) noexcept
{
    switch (i)
    {
        case Item::play: return TR_NOOP ("Press Space to hear the starter beat");
        case Item::addLoop: return TR_NOOP ("Add a loop: drag one onto the timeline");
        case Item::recordVoice: return TR_NOOP ("Record your voice on a new Voice track");
        case Item::steps: return TR_NOOP ("Click steps in the Channel Rack to make a beat");
        case Item::openFlp: return TR_NOOP ("Open one of your .flp projects");
        case Item::plugins: return TR_NOOP ("Find your plugins (VST, CLAP)");
        case Item::slotClip: return TR_NOOP ("Drag a loop into a slot of the clip grid");
        case Item::launchScene: return TR_NOOP ("Launch a clip or a scene");
        case Item::openAls: return TR_NOOP ("Open one of your .als sets");
        case Item::instrumentTrack: return TR_NOOP ("Add an instrument track and play it with the computer keys");
        case Item::recordAudio: return TR_NOOP ("Record audio on a Voice or Guitar track");
        case Item::openMixer: return TR_NOOP ("Open the Mixer (F9)");
        default: return "";
    }
}

// The item's button, which does the first click for the user (nullptr: none)
inline const char* itemAction (Item i) noexcept
{
    switch (i)
    {
        case Item::play: return TR_NOOP ("Play");
        case Item::addLoop: return TR_NOOP ("Open Loops");
        case Item::recordVoice: return TR_NOOP ("+ Voice track");
        case Item::steps: return TR_NOOP ("Channel Rack");
        case Item::openFlp:
        case Item::openAls: return TR_NOOP ("Open...");
        case Item::plugins: return TR_NOOP ("Plugin folders...");
        case Item::slotClip: return TR_NOOP ("Browse loops");
        case Item::launchScene: return nullptr;
        case Item::instrumentTrack: return TR_NOOP ("+ Instrument");
        case Item::recordAudio: return TR_NOOP ("+ Voice track");
        case Item::openMixer: return TR_NOOP ("Mixer");
        default: return nullptr;
    }
}

inline const char* next3Subtitle (Choice c) noexcept
{
    switch (c)
    {
        case Choice::mobile: return TR_NOOP ("Your starter beat is ready in the Simple layout.");
        case Choice::fl: return TR_NOOP ("The Pattern layout works like FL Studio.");
        case Choice::live: return TR_NOOP ("The Clip-launch layout: clips and scenes beside the arrangement.");
        case Choice::timeline: return TR_NOOP ("One track per instrument, with the Mixer at hand.");
        default: return "";
    }
}

// What the window knows beyond the song (sticky: once seen, an item stays ticked)
struct Context
{
    bool played = false;        // the transport ran since the card came up
    bool mixerShown = false;    // the Mixer was shown
    bool sceneLaunched = false; // a clip or a scene of the grid played
    bool openedFlp = false;     // an .flp project was opened
    bool openedAls = false;     // an .als set was opened
    int plugins = 0;            // plugins known to the plugin list
};

// Any audio clip in the current arrangement (a dropped loop or a recorded take)
inline bool hasAudioClip (const Song& song)
{
    return std::any_of (song.playlist.begin(), song.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::audio; });
}

// An audio clip on a Simple audio track that records an input (a Voice, Guitar or Bass track: a loop's track has none)
inline bool hasRecordedTrack (const Song& song)
{
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::audio && simpletracks::kindOf (song, c.track) == simpletracks::Kind::audio)
            if (const auto insert = simpletracks::insertOf (song, c.track); insert >= 0 && insert < (int) song.mixer.size()
                                                                            && song.mixer[(std::size_t) insert].inputChannel >= 0)
                return true;
    return false;
}

inline bool anyPatternHasNotes (const Song& song)
{
    return std::any_of (song.patterns.begin(), song.patterns.end(), [] (const Pattern& p) { return ! p.notes.empty(); });
}

inline bool hasInstrumentTrack (const Song& song)
{
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (const auto k = simpletracks::kindOf (song, t); k == simpletracks::Kind::instrument || k == simpletracks::Kind::sampler)
            return true;
    return false;
}

inline bool gridHasClip (const Song& song) { return song.clipGrid.has_value() && ! song.clipGrid->clips.empty(); }

inline bool satisfied (Item i, const Song& song, const Context& ctx)
{
    switch (i)
    {
        case Item::play: return ctx.played;
        case Item::addLoop: return hasAudioClip (song) || gridHasClip (song);
        case Item::recordVoice:
        case Item::recordAudio: return hasRecordedTrack (song);
        case Item::steps: return anyPatternHasNotes (song);
        case Item::openFlp: return ctx.openedFlp;
        case Item::plugins: return ctx.plugins > 0;
        case Item::slotClip: return gridHasClip (song);
        case Item::launchScene: return ctx.sceneLaunched;
        case Item::openAls: return ctx.openedAls;
        case Item::instrumentTrack: return hasInstrumentTrack (song);
        case Item::openMixer: return ctx.mixerShown;
        default: return false;
    }
}

// --- empty states -------------------------------------------------------------------------------------------------------
// No clip in any arrangement (the clip grid's own clips don't count: the Playlist shows arrangements)
inline bool playlistEmpty (const Song& song)
{
    if (! song.playlist.empty())
        return false;
    for (std::size_t i = 0; i < song.arrangements.size(); ++i)
        if ((int) i != song.currentArrangement && ! song.arrangements[i].clips.empty())
            return false;
    return true;
}

inline bool clipGridEmpty (const Song& song) { return ! gridHasClip (song); }

enum class EmptyHint { none, playlist, clipGrid, simple };

// The hint an empty view shows. `simpleCardShowing`: the Simple layout already shows its New-track card, Welcome screen
// or Start-from list over the timeline (those are its empty state).
inline EmptyHint emptyHintFor (layoutstyle::Style style, const Song& song, bool simpleCardShowing, bool hintsOn)
{
    if (! hintsOn)
        return EmptyHint::none;
    switch (style)
    {
        case layoutstyle::Style::pattern: return playlistEmpty (song) ? EmptyHint::playlist : EmptyHint::none;
        case layoutstyle::Style::clipLaunch: return clipGridEmpty (song) && playlistEmpty (song) ? EmptyHint::clipGrid : EmptyHint::none;
        case layoutstyle::Style::simple: return ! simpleCardShowing && simpletracks::isEmptySong (song) ? EmptyHint::simple : EmptyHint::none;
        default: return EmptyHint::none;
    }
}

inline const char* emptyTitle (EmptyHint h) noexcept
{
    switch (h)
    {
        case EmptyHint::playlist: return TR_NOOP ("Your song is built here");
        case EmptyHint::clipGrid: return TR_NOOP ("Launch clips from this grid");
        case EmptyHint::simple: return TR_NOOP ("Drag a loop here");
        default: return "";
    }
}

inline const char* emptyText (EmptyHint h) noexcept
{
    switch (h)
    {
        case EmptyHint::playlist: return TR_NOOP ("Click in the Playlist to place the pattern you're editing, or drag a loop, audio or MIDI file in from the Browser.");
        case EmptyHint::clipGrid: return TR_NOOP ("Drag a loop or a sample from the Browser into any slot. Double-click an empty slot for a new MIDI clip. The play button at the end of a row launches the whole scene.");
        case EmptyHint::simple: return TR_NOOP ("Drop a loop, an audio or a MIDI file on the timeline, or add a track: drums, an instrument or your voice.");
        default: return "";
    }
}

// --- contextual tips ----------------------------------------------------------------------------------------------------
enum class Tip : int { songModeEmpty = 0, recordNoInput };
constexpr int tipCount = 2;

inline const char* tipId (Tip t) noexcept { return t == Tip::songModeEmpty ? "songModeEmpty" : "recordNoInput"; }

inline const char* tipTitle (Tip t) noexcept
{
    return t == Tip::songModeEmpty ? TR_NOOP ("Nothing to play in Song mode yet") : TR_NOOP ("No input to record from yet");
}

inline const char* tipText (Tip t) noexcept
{
    return t == Tip::songModeEmpty
               ? TR_NOOP ("Song mode plays the Playlist, and it's empty. Switch to Pattern to hear the pattern you're editing, or place it in the Playlist.")
               : TR_NOOP ("To record your voice or an instrument, choose an input in the audio settings and arm a track. Notes from the computer keys or a MIDI keyboard record without one.");
}

inline const char* tipAction (Tip t) noexcept { return t == Tip::songModeEmpty ? TR_NOOP ("Switch to Pattern") : TR_NOOP ("Audio settings..."); }

// The confusion signals
// Play pressed in Song mode, in the Pattern layout, with nothing in the Playlist
inline bool songModeEmptySignal (bool songMode, const Song& song, layoutstyle::Style style)
{
    return songMode && style == layoutstyle::Style::pattern && playlistEmpty (song);
}

// Record pressed to record audio (audio only, or any recording in the Simple layout) with no input to record from: the
// device has no input channels, or no track / insert is armed for audio
inline bool recordNoInputSignal (bool recordAudio, bool recordNotes, bool simpleLayout, int inputChannels, bool anyArmed)
{
    if (! recordAudio || (recordNotes && ! simpleLayout))
        return false;
    return inputChannels <= 0 || ! anyArmed;
}

// --- what is kept (onboarding.txt in the settings folder) ---------------------------------------------------------------
struct State
{
    std::string choice;        // the welcome card's answer ("" = none yet; "skip" = skipped)
    bool pending = false;      // the card was shown and not answered yet
    bool next3Hidden = false;  // × on the Next-3 card (Help › Getting started shows it again)
    std::uint32_t ticked = 0;  // Next-3 items seen done (bit per Item)
    bool tipsOff = false;      // "No more tips"
    std::uint32_t tipsSeen = 0; // tips retired with "Got it" (bit per Tip)
    bool hintsOff = false;     // Help › "Show tips and hints" unticked: no empty-state hints, no tips
    bool operator== (const State&) const = default;
};

inline std::string serialize (const State& s)
{
    std::ostringstream out;
    out << "choice=" << s.choice << "\npending=" << (s.pending ? 1 : 0) << "\nnext3Hidden=" << (s.next3Hidden ? 1 : 0) << "\nticked=" << s.ticked
        << "\ntipsOff=" << (s.tipsOff ? 1 : 0) << "\ntipsSeen=" << s.tipsSeen << "\nhintsOff=" << (s.hintsOff ? 1 : 0) << "\n";
    return out.str();
}

inline State parse (const std::string& text)
{
    State s;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto name = line.substr (0, eq), value = line.substr (eq + 1);
        if (name == "choice")
        {
            if (value == "skip" || choiceFromId (value) != Choice::none)
                s.choice = value;
            continue;
        }
        long long v = 0;
        try
        {
            v = std::stoll (value);
        }
        catch (...)
        {
            continue;
        }
        if (name == "pending") s.pending = v != 0;
        else if (name == "next3Hidden") s.next3Hidden = v != 0;
        else if (name == "ticked") s.ticked = (std::uint32_t) (v & ((1 << itemCount) - 1));
        else if (name == "tipsOff") s.tipsOff = v != 0;
        else if (name == "tipsSeen") s.tipsSeen = (std::uint32_t) (v & ((1 << tipCount) - 1));
        else if (name == "hintsOff") s.hintsOff = v != 0;
    }
    return s;
}

inline Choice chosen (const State& s) noexcept { return choiceFromId (s.choice); }

// The welcome card was answered: a choice, or Skip / × / Esc (`c` = none)
inline void answer (State& s, Choice c)
{
    s.choice = c == Choice::none ? "skip" : choiceId (c);
    s.pending = false;
    s.next3Hidden = false;
}

// The Next-3 card's items: ticks what the song and the window show now (sticky) and says whether all are done
inline bool tick (State& s, Choice c, const Song& song, const Context& ctx)
{
    const auto items = next3 (c);
    bool all = ! items.empty();
    for (const auto i : items)
    {
        if (satisfied (i, song, ctx))
            s.ticked |= 1u << (std::uint32_t) i;
        all = all && (s.ticked & (1u << (std::uint32_t) i)) != 0;
    }
    return all;
}

inline bool ticked (const State& s, Item i) noexcept { return (s.ticked & (1u << (std::uint32_t) i)) != 0; }

// At most one tip per session, never one retired, none after "No more tips" or with the hints off
class TipGate
{
public:
    bool canShow (Tip t, const State& s) const noexcept
    {
        return ! shownThisSession && ! s.tipsOff && ! s.hintsOff && (s.tipsSeen & (1u << (std::uint32_t) t)) == 0;
    }
    void shown() noexcept { shownThisSession = true; }
    bool shownForCheck() const noexcept { return shownThisSession; }
    static void gotIt (State& s, Tip t) noexcept { s.tipsSeen |= 1u << (std::uint32_t) t; }
    static void noMoreTips (State& s) noexcept { s.tipsOff = true; }

private:
    bool shownThisSession = false;
};
} // namespace lynx::onboarding
