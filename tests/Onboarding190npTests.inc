// --- Round 190np: first-run onboarding (core/Onboarding.h). What a fresh profile and an upgraded one do at start-up,
// each of the welcome card's five choices and Skip, the "Next: 3 things" items ticking from the song, the empty-state
// hints, at most one contextual tip per session and the kept state.

#include "core/Onboarding.h"

namespace onboard190np
{
using namespace lynx;

inline Song empty()
{
    Song s;
    s.mixer = Song::defaultMixer();
    return s;
}

inline Clip audioClip (int track)
{
    Clip c;
    c.kind = ClipKind::audio;
    c.track = track;
    c.length = ppq * 4;
    return c;
}
} // namespace onboard190np

TEST (onboard190np_fresh_profile_shows_the_welcome_card_an_upgrade_never_does)
{
    using namespace lynx::onboarding;
    // a fresh profile: nothing in the settings folder
    CHECK (startAction (false, false, false) == StartAction::welcomeCard);
    // an upgrade: setup-done.txt from the old wizard -> nothing at all, whatever else is there
    CHECK (startAction (true, true, false) == StartAction::none);
    CHECK (startAction (true, false, false) == StartAction::none);
    CHECK (startAction (true, true, true) == StartAction::none); // (answered once: never again)
    // an existing user who never finished the old wizard keeps the old wizard (no change for existing users)
    CHECK (startAction (false, true, false) == StartAction::setupWizard);
    // the card was shown and not answered (a restart for another language, the app closed): it comes back
    CHECK (startAction (false, true, true) == StartAction::welcomeCard);
}

TEST (onboard190np_each_choice_has_its_layout_starter_and_next3)
{
    using namespace lynx::onboarding;
    using lynx::layoutstyle::Style;
    CHECK (choiceCount == 5);
    CHECK (layoutFor (Choice::mobile) == Style::simple);
    CHECK (layoutFor (Choice::fl) == Style::pattern);
    CHECK (layoutFor (Choice::live) == Style::clipLaunch);
    CHECK (layoutFor (Choice::timeline) == Style::simple);
    CHECK (layoutFor (Choice::newcomer) == Style::simple);
    CHECK (starterFor (Choice::mobile) == Starter::starterBeat);
    CHECK (starterFor (Choice::fl) == Starter::none);
    CHECK (starterFor (Choice::live) == Starter::none);
    CHECK (starterFor (Choice::timeline) == Starter::newTrackCard);
    CHECK (starterFor (Choice::newcomer) == Starter::tour);
    CHECK (showsMixer (Choice::timeline));
    for (const auto c : { Choice::mobile, Choice::fl, Choice::live, Choice::newcomer })
        CHECK (! showsMixer (c));
    for (int i = 0; i < choiceCount; ++i)
    {
        const auto c = choiceAt (i);
        CHECK (choiceFromId (choiceId (c)) == c);
        CHECK (std::string (choiceTitle (c)).size() > 3);
        CHECK (std::string (choiceText (c)).size() > 20);
        CHECK (next3 (c).size() == (c == Choice::newcomer ? 0u : 3u)); // (the newcomer has the tour)
        for (const auto item : next3 (c))
            CHECK (std::string (itemText (item)).size() > 5);
    }
    CHECK (next3 (Choice::mobile) == std::vector<Item> ({ Item::play, Item::addLoop, Item::recordVoice }));
    CHECK (choiceAt (5) == Choice::none);
    CHECK (choiceAt (-1) == Choice::none);
    CHECK (choiceFromId ("nonsense") == Choice::none);
}

TEST (onboard190np_skip_keeps_the_pattern_layout_and_changes_nothing)
{
    using namespace lynx::onboarding;
    // Skip, the × button and Esc answer the card with no choice: the Pattern layout, nothing started
    CHECK (layoutFor (Choice::none) == lynx::layoutstyle::Style::pattern);
    CHECK (starterFor (Choice::none) == Starter::none);
    CHECK (! showsMixer (Choice::none));
    CHECK (next3 (Choice::none).empty());
    State s;
    s.pending = true;
    answer (s, Choice::none);
    CHECK (s.choice == "skip");
    CHECK (! s.pending);
    CHECK (chosen (s) == Choice::none);
    // answered: the card never opens by itself again (setup-done.txt is written with it)
    CHECK (startAction (true, true, s.pending) == StartAction::none);
    // a choice is remembered for Help › Getting started
    answer (s, Choice::live);
    CHECK (s.choice == "live");
    CHECK (chosen (s) == Choice::live);
}

TEST (onboard190np_next3_items_tick_from_the_song_and_stay_ticked)
{
    using namespace lynx::onboarding;
    using namespace lynx;
    auto song = onboard190np::empty();
    Context ctx;
    State s;
    // mobile: play, a loop, a voice take
    CHECK (! tick (s, Choice::mobile, song, ctx));
    CHECK (s.ticked == 0);
    ctx.played = true;
    CHECK (! tick (s, Choice::mobile, song, ctx));
    CHECK (ticked (s, Item::play));
    ctx.played = false; // (sticky: once seen, it stays ticked)
    tick (s, Choice::mobile, song, ctx);
    CHECK (ticked (s, Item::play));
    // a loop dropped on its own track: the loop item, not the voice item
    {
        auto withLoop = onboard190np::empty();
        simpletracks::Request l;
        l.kind = simpletracks::Kind::audio;
        l.audioFile = "/nowhere/Loop 120 Am.wav";
        l.audioLength = ppq * 8;
        CHECK (simpletracks::addTrack (withLoop, l).ok());
        CHECK (satisfied (Item::addLoop, withLoop, {}));
        CHECK (! satisfied (Item::recordVoice, withLoop, {}));
    }
    // a voice track with a take on it: the loop item (any audio clip) and the voice item
    simpletracks::Request q;
    q.kind = simpletracks::Kind::voice;
    q.name = "Voice";
    q.input = 0;
    const auto r = simpletracks::addTrack (song, q);
    CHECK (r.ok());
    CHECK (! hasRecordedTrack (song));
    song.playlist.push_back (onboard190np::audioClip (r.track));
    CHECK (hasRecordedTrack (song));
    CHECK (tick (s, Choice::mobile, song, ctx)); // all three done
    CHECK (ticked (s, Item::addLoop) && ticked (s, Item::recordVoice));

    // fl: steps from the pattern's notes, .flp opened, plugins found
    State f;
    auto fl = onboard190np::empty();
    CHECK (! tick (f, Choice::fl, fl, {}));
    Pattern p;
    p.toggleStep (0, 0);
    fl.patterns.push_back (p);
    tick (f, Choice::fl, fl, {});
    CHECK (ticked (f, Item::steps));
    Context c2;
    c2.openedFlp = true;
    c2.plugins = 12;
    CHECK (tick (f, Choice::fl, fl, c2));

    // live: a clip in a slot, a launch, an .als
    State l;
    auto live = onboard190np::empty();
    CHECK (! satisfied (Item::slotClip, live, {}));
    live.clipGrid.emplace();
    CHECK (! satisfied (Item::slotClip, live, {}));
    live.clipGrid->clips.push_back (onboard190np::audioClip (0));
    CHECK (satisfied (Item::slotClip, live, {}));
    Context c3;
    c3.sceneLaunched = true;
    c3.openedAls = true;
    CHECK (tick (l, Choice::live, live, c3));

    // timeline: an instrument track, a recorded take, the Mixer shown
    State t;
    auto tl = onboard190np::empty();
    simpletracks::Request inst;
    inst.kind = simpletracks::Kind::instrument;
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    inst.channel = synth;
    inst.emptyBars = 4;
    CHECK (simpletracks::addTrack (tl, inst).ok());
    tick (t, Choice::timeline, tl, {});
    CHECK (ticked (t, Item::instrumentTrack));
    CHECK (! ticked (t, Item::recordAudio));
    Context c4;
    c4.mixerShown = true;
    tick (t, Choice::timeline, tl, c4);
    CHECK (ticked (t, Item::openMixer));
    // the newcomer has no Next-3 card (the tour is the guide)
    State n;
    CHECK (! tick (n, Choice::newcomer, tl, c4));
}

TEST (onboard190np_empty_state_hints)
{
    using namespace lynx::onboarding;
    using namespace lynx;
    using lynx::layoutstyle::Style;
    auto song = onboard190np::empty();
    CHECK (playlistEmpty (song));
    CHECK (emptyHintFor (Style::pattern, song, false, true) == EmptyHint::playlist);
    CHECK (emptyHintFor (Style::clipLaunch, song, false, true) == EmptyHint::clipGrid);
    CHECK (emptyHintFor (Style::simple, song, false, true) == EmptyHint::simple);
    // the Simple layout's own card (New track, Welcome, Start from) is its empty state: no second hint
    CHECK (emptyHintFor (Style::simple, song, true, true) == EmptyHint::none);
    // "Show tips and hints" off: none anywhere
    for (const auto style : { Style::pattern, Style::clipLaunch, Style::simple })
        CHECK (emptyHintFor (style, song, false, false) == EmptyHint::none);
    // a clip in the Playlist: gone in the Pattern layout
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 0;
    c.length = ppq * 4;
    song.playlist.push_back (c);
    CHECK (! playlistEmpty (song));
    CHECK (emptyHintFor (Style::pattern, song, false, true) == EmptyHint::none);
    CHECK (emptyHintFor (Style::clipLaunch, song, false, true) == EmptyHint::none);
    // a clip in another arrangement only also counts
    auto other = onboard190np::empty();
    other.addArrangement ("Arrangement 2", false);
    other.selectArrangement (0);
    CHECK (playlistEmpty (other));
    other.arrangements[1].clips.push_back (c);
    CHECK (! playlistEmpty (other));
    // a slot with a clip: the grid's hint goes
    auto grid = onboard190np::empty();
    grid.clipGrid.emplace();
    CHECK (emptyHintFor (Style::clipLaunch, grid, false, true) == EmptyHint::clipGrid);
    grid.clipGrid->clips.push_back (onboard190np::audioClip (0));
    CHECK (emptyHintFor (Style::clipLaunch, grid, false, true) == EmptyHint::none);
    for (const auto h : { EmptyHint::playlist, EmptyHint::clipGrid, EmptyHint::simple })
        CHECK (std::string (emptyTitle (h)).size() > 5 && std::string (emptyText (h)).size() > 20);
}

TEST (onboard190np_at_most_one_tip_per_session_only_on_a_signal)
{
    using namespace lynx::onboarding;
    using namespace lynx;
    using lynx::layoutstyle::Style;
    auto song = onboard190np::empty();
    // the signals
    CHECK (songModeEmptySignal (true, song, Style::pattern));
    CHECK (! songModeEmptySignal (false, song, Style::pattern)); // (Pattern mode plays the pattern: fine)
    CHECK (! songModeEmptySignal (true, song, Style::simple));
    Clip c;
    c.kind = ClipKind::pattern;
    c.length = ppq;
    auto full = song;
    full.playlist.push_back (c);
    CHECK (! songModeEmptySignal (true, full, Style::pattern));
    CHECK (recordNoInputSignal (true, false, false, 0, false));  // audio only, no input device
    CHECK (recordNoInputSignal (true, false, false, 2, false));  // inputs, nothing armed
    CHECK (! recordNoInputSignal (true, false, false, 2, true)); // armed with an input: fine
    CHECK (! recordNoInputSignal (true, true, false, 0, false)); // notes too (FL's default): no confusion
    CHECK (recordNoInputSignal (true, true, true, 0, true));     // the Simple layout's Record with no input device
    CHECK (! recordNoInputSignal (false, true, true, 0, false)); // notes only

    // one per session
    State s;
    TipGate gate;
    CHECK (gate.canShow (Tip::songModeEmpty, s));
    gate.shown();
    CHECK (! gate.canShow (Tip::songModeEmpty, s));
    CHECK (! gate.canShow (Tip::recordNoInput, s));
    // next session: "Got it" retired that tip only
    TipGate::gotIt (s, Tip::songModeEmpty);
    TipGate next;
    CHECK (! next.canShow (Tip::songModeEmpty, s));
    CHECK (next.canShow (Tip::recordNoInput, s));
    // "No more tips": none
    TipGate::noMoreTips (s);
    TipGate third;
    CHECK (! third.canShow (Tip::recordNoInput, s));
    // the hints switched off: no tips either
    State h;
    h.hintsOff = true;
    CHECK (! TipGate().canShow (Tip::songModeEmpty, h));
}

TEST (onboard190np_state_round_trips_and_ignores_junk)
{
    using namespace lynx::onboarding;
    State s;
    CHECK (parse (serialize (s)) == s);
    s.choice = "timeline";
    s.pending = true;
    s.next3Hidden = true;
    s.ticked = (1u << (int) Item::play) | (1u << (int) Item::openMixer);
    s.tipsOff = true;
    s.tipsSeen = 2;
    s.hintsOff = true;
    CHECK (parse (serialize (s)) == s);
    const auto junk = parse ("choice=../../etc\npending=x\nticked=999999999999\ntipsSeen=-1\r\nnothing\n=\n");
    CHECK (junk.choice.empty());
    CHECK (! junk.pending);
    CHECK (junk.ticked < (1u << itemCount));
    CHECK (junk.tipsSeen < (1u << tipCount));
    CHECK (parse ("choice=skip\n").choice == "skip");
}
