// Round details9_190mv (0.1.30, details pass 9): FL's "Undo knob tweaks" option and the round's other rows.

// FL's General › Undo history › "Undo knob tweaks" (DF-134): on by default as in FL, kept in ui-options.txt
TEST (details190mv_undo_knob_option)
{
    using namespace lynx::flsettings;
    reset();
    CHECK (options().undoKnobTweaks.load());
    CHECK (parse ("flUndoKnobTweaks", "0"));
    CHECK (! options().undoKnobTweaks.load());
    CHECK (serialise().find ("\nflUndoKnobTweaks=0") != std::string::npos);
    CHECK (parse ("flUndoKnobTweaks", "x")); // (unreadable: FL's default)
    CHECK (options().undoKnobTweaks.load());
    reset();
}

// FL's Alt+click on a channel's mute switch (DE-12, capture 190mv A4): the solo within the channel's filter group; other
// groups and the ungrouped channels keep theirs; again = the whole group plays
TEST (details190mv_group_solo)
{
    using namespace lynx;
    Song s;
    s.channels.resize (6);
    s.channels[0].group = 0; // "Audio"
    s.channels[1].group = 0;
    s.channels[2].group = 0;
    s.channels[3].group = -1; // unsorted
    s.channels[4].group = 1;  // "Automation"
    s.channels[5].group = 0;
    s.channels[5].soloLock = true;
    noteedit::soloChannelInGroup (s, 1);
    CHECK (s.channels[0].muted && ! s.channels[1].muted && s.channels[2].muted);
    CHECK (! s.channels[3].muted && ! s.channels[4].muted && ! s.channels[5].muted);
    noteedit::soloChannelInGroup (s, 1);
    CHECK (! s.channels[0].muted && ! s.channels[1].muted && ! s.channels[2].muted);
    // the ungrouped channels are one group (FL's "Unsorted")
    s.channels.push_back (s.channels[3]);
    noteedit::soloChannelInGroup (s, 3);
    CHECK (! s.channels[3].muted && s.channels[6].muted && ! s.channels[0].muted && ! s.channels[4].muted);
}

// FL's Current (C) mixer track (DF-82, capture 190mv A1): its effects process the selected track's output in line (a
// volume of -inf on C silences the Master while the sounding insert is selected); another selection, or none, plays as
// before; exports (no source set) never hear it. The track stays the last one when inserts are added after it.
TEST (details190mv_current_track)
{
    using namespace lynx;
    auto s = mixpre167q::synthSong ({ mixpre167q::clipAt (0, 4 * ppq) });
    const auto c = mixerops::addCurrentTrack (s);
    CHECK (c == (int) s.mixer.size() - 1 && mixerops::currentTrack (s) == c && ! s.mixer[(std::size_t) c].toMaster);
    CHECK (mixerops::addCurrentTrack (s) == c); // (only one)
    EffectSlot balance;
    balance.kind = EffectKind::fruityBalance;
    flbasic::balanceToParams ({ 0, 0 }, balance.params.data()); // FL's test: Fruity Balance, volume at its minimum
    s.mixer[(std::size_t) c].effects.push_back (balance);

    const auto renderWith = [&s] (int source) {
        auto state = RackState::build (s, nullptr, 48000.0);
        RackRenderer r;
        r.prepare (48000.0, 4096);
        r.setCurrentSource (source);
        const auto tps = ticksPerSample (120.0, 48000.0);
        double position = 0.0, sum = 0.0;
        for (int done = 0; done < 24000; done += 512)
        {
            std::vector<float> l (512), rr (512);
            r.process (state.get(), true, position, 512, l.data(), rr.data(), nullptr);
            for (auto v : l)
                sum += (double) v * v;
            position += 512 * tps;
        }
        return sum;
    };
    const auto plain = renderWith (-1);
    CHECK (plain > 1.0);
    CHECK (renderWith (1) < plain * 1.0e-6); // the sounding insert selected: C's effects silence it
    CHECK (std::abs (renderWith (2) - plain) < plain * 1.0e-4); // another insert selected: as before
    CHECK (std::abs (renderWith (c) - plain) < plain * 1.0e-4); // (C itself as a source: nothing)
    CHECK (renderWith (0) < plain * 1.0e-6); // the Master selected: what leaves the Master goes through C too

    // an insert added after it: the Current track moves back to the end, references follow
    s.channels[0].mixerTrack = 1;
    s.mixer.emplace_back().name = "Insert " + std::to_string (s.mixer.size());
    s.channels[0].mixerTrack = (int) s.mixer.size() - 1; // (routed to the new one in the same edit)
    CHECK (mixerops::keepCurrentLast (s));
    CHECK (mixerops::currentTrack (s) == (int) s.mixer.size() - 1);
    CHECK (s.channels[0].mixerTrack == c && s.mixer[(std::size_t) c].name == "Insert " + std::to_string (c));
    CHECK (s.mixer.back().effects.size() == 1);
    CHECK (! mixerops::keepCurrentLast (s));
}

// FL's Master pitch typed in Hz (DF-22, capture 190mv A6): A = 440 Hz is no shift; an octave is ±1200 cents
TEST (details190mv_master_pitch_hz)
{
    using namespace lynx::masterpitch;
    CHECK (std::abs (hzOfCents (0.0) - 440.0) < 1.0e-9);
    CHECK (std::abs (hzOfCents (1200.0) - 880.0) < 1.0e-9 && std::abs (hzOfCents (-1200.0) - 220.0) < 1.0e-9);
    CHECK (std::abs (centsOfHz (432.0) - (-31.7667)) < 1.0e-3);
    CHECK (std::abs (centsOfHz (hzOfCents (-37.0)) + 37.0) < 1.0e-9);
    CHECK (centsOfHz (5000.0) == 1200.0 && centsOfHz (0.0) == 0.0);
}

// Live's phase nudge (DL-51, capture 190mv B2): the two buttons right after the tempo box, in the left section
TEST (details190mv_live_phase_nudge_items)
{
    using namespace lynx::toolbarlayout::livebar;
    CHECK ((int) Item::tempo + 1 == (int) Item::nudgeDown && (int) Item::nudgeDown + 1 == (int) Item::nudgeUp);
    CHECK (sectionOf (Item::nudgeDown) == Section::left && sectionOf (Item::nudgeUp) == Section::left);
    CHECK (defaultWidth (Item::nudgeDown) == 16 && defaultWidth (Item::nudgeUp) == 16);
}
