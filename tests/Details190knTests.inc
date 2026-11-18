// Round 190kn (details190if DETAILS pass): FL's mute-switch modifiers (Unmute All, Lock state, slot solo), BandLab's
// chain-preset stepper, and the CPU panel's refresh setting.
#include "core/ChainPresets.h"
#include "core/MixerOps.h"

#include <set>

namespace details190kn
{
lynx::Song mixerSong (int inserts)
{
    lynx::Song song;
    song.mixer.resize ((std::size_t) inserts + 1);
    return song;
}
} // namespace details190kn

TEST (details190kn_unmute_all_clears_mutes_and_solos_but_keeps_locked_tracks)
{
    using namespace lynx;
    auto song = details190kn::mixerSong (4);
    song.mixer[1].muted = true;
    song.mixer[2].muted = true;
    song.mixer[2].lockState = true; // FL: a locked track keeps its state
    song.mixer[3].solo = true;
    mixerops::unmuteAll (song);
    CHECK (! song.mixer[1].muted);
    CHECK (song.mixer[2].muted);
    CHECK (! song.mixer[3].solo);
    mixerops::toggleLockState (song, 2);
    CHECK (! song.mixer[2].lockState);
    mixerops::unmuteAll (song);
    CHECK (! song.mixer[2].muted);
    mixerops::toggleLockState (song, 99); // (out of range: nothing)
}

TEST (details190kn_a_locked_track_is_not_silenced_by_a_solo_elsewhere)
{
    using namespace lynx;
    auto song = details190kn::mixerSong (4);
    song.mixer[1].solo = true;
    auto audible = song.soloAudible();
    CHECK (audible[1] && ! audible[2] && ! audible[3]);
    song.mixer[2].lockState = true;
    audible = song.soloAudible();
    CHECK (audible[1] && audible[2] && ! audible[3]);
    CHECK (audible[0]); // (the Master)
    // a locked track routed into another insert keeps that path too
    song.mixer[2].toMaster = false;
    song.mixer[2].sends.push_back ({ 4, 1.0f });
    audible = song.soloAudible();
    CHECK (audible[2] && audible[4] && ! audible[3]);
    // no solo: everything as before
    song.mixer[1].solo = false;
    audible = song.soloAudible();
    CHECK (audible[1] && audible[2] && audible[3] && audible[4]);
}

TEST (details190kn_slot_solo_keeps_one_effect_then_turns_all_back_on)
{
    using namespace lynx;
    auto song = details190kn::mixerSong (1);
    for (int i = 0; i < 3; ++i)
        song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::eq));
    song.mixer[1].effects[2].enabled = false;
    mixerops::soloSlot (song, 1, 1);
    CHECK (! song.mixer[1].effects[0].enabled && song.mixer[1].effects[1].enabled && ! song.mixer[1].effects[2].enabled);
    mixerops::soloSlot (song, 1, 1); // again on the soloed slot: all on (FL's unsolo)
    CHECK (song.mixer[1].effects[0].enabled && song.mixer[1].effects[1].enabled && song.mixer[1].effects[2].enabled);
    mixerops::soloSlot (song, 1, 0);
    CHECK (song.mixer[1].effects[0].enabled && ! song.mixer[1].effects[1].enabled);
    mixerops::soloSlot (song, 1, 7);  // (no such slot)
    mixerops::soloSlot (song, 9, 0);  // (no such track)
    CHECK (song.mixer[1].effects[0].enabled);
}

TEST (details190kn_chain_preset_stepper_follows_the_menu_order_and_wraps)
{
    using namespace lynx;
    std::vector<std::string> order;
    for (const auto* category : chains::categories())
        for (const auto& p : chains::factory())
            if (std::string (p.category) == category)
                order.push_back (p.name);
    CHECK (order.size() >= 3);
    if (order.size() < 3)
        return;
    CHECK (std::string (chains::stepped ("", 1)->name) == order.front());       // none: › = the first
    CHECK (std::string (chains::stepped ("", -1)->name) == order.back());       // none: ‹ = the last
    CHECK (std::string (chains::stepped ("my own", 1)->name) == order.front()); // a user's preset
    CHECK (std::string (chains::stepped (order[0], 1)->name) == order[1]);
    CHECK (std::string (chains::stepped (order[1], -1)->name) == order[0]);
    CHECK (std::string (chains::stepped (order.back(), 1)->name) == order.front()); // wraps
    CHECK (std::string (chains::stepped (order.front(), -1)->name) == order.back());
    // every preset is reached once going round
    std::set<std::string> seen;
    std::string at;
    for (std::size_t i = 0; i < order.size(); ++i)
        seen.insert (at = chains::stepped (at, 1)->name);
    CHECK (seen.size() == order.size());
}

TEST (details190kn_live_count_in_position_runs_from_minus_bars_up_to_one)
{
    using namespace lynx;
    const auto beat = (double) ppq, bar = 4.0 * beat;
    // 2 bars of 4/4 (Live manual 19.6: "beginning at -2.1.1. ... up to 1.1.1.")
    auto p = countin::livePosition (2.0 * bar, 4, 4, ppq);
    CHECK (p.bar == -2 && p.beat == 1 && p.sixteenth == 1);
    p = countin::livePosition (2.0 * bar - 1.5 * beat, 4, 4, ppq); // a beat and an eighth in
    CHECK (p.bar == -2 && p.beat == 2 && p.sixteenth == 3);
    p = countin::livePosition (bar, 4, 4, ppq); // the second bar starts
    CHECK (p.bar == -1 && p.beat == 1 && p.sixteenth == 1);
    p = countin::livePosition (1.0, 4, 4, ppq); // the last tick
    CHECK (p.bar == -1 && p.beat == 4 && p.sixteenth == 4);
    CHECK (countin::livePosition (0.0, 4, 4, ppq).bar == 0); // not counting in
    // 6/8: six eighth-note beats per bar
    p = countin::livePosition (6.0 * beat / 2.0, 6, 8, ppq);
    CHECK (p.bar == -1 && p.beat == 1);
}
