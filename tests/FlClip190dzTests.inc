// Round 190dz: FL's audio clip menu layout (core/FlClipMenu.h) and the channel's Misc › Levels adjustment (pan offset,
// volume multiplier, Mod X / Mod Y offsets; FL's channel parameters 0x10..0x14, .flp 0xE5), which Automate on an audio
// clip now moves. FL facts: /mnt/e/lynx-work/flclip190dz/HANDOFF.md.
#include "core/FlClipMenu.h"
#include "core/LastTweaked.h"
#include "core/SessionIds.h"

namespace flclip190dz
{
inline std::vector<std::string> texts (const std::vector<lynx::flclipmenu::Entry>& entries)
{
    using K = lynx::flclipmenu::Entry::Kind;
    std::vector<std::string> out;
    for (const auto& e : entries)
        out.push_back (e.kind == K::separator ? std::string ("-")
                       : e.kind == K::columnBreak ? std::string ("||")
                       : e.kind == K::header ? "[" + e.text + "]"
                       : e.kind == K::submenu ? e.text + " >"
                       : e.kind == K::check ? "(v) " + e.text
                                            : e.text);
    return out;
}

// one Synth channel on insert 1 of a plain mixer (no Master effects), a 4-beat note at C5 from the start
inline lynx::Song synthSong()
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.clear();
    s.playlist.clear();
    s.automations.clear();
    s.patterns.clear();
    s.mixer = Song::defaultMixer (2);
    s.mode = PlayMode::song; // (the playlist plays: the automation clips too)
    ChannelDef c;
    c.name = "Lead";
    c.instrument = InstrumentKind::synth;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    Pattern p;
    p.name = "P1";
    Note n;
    n.start = 0;
    n.length = 4 * ppq;
    n.key = 72;
    p.notes.push_back (n);
    s.patterns.push_back (p);
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = 0;
    clip.track = 0;
    clip.start = 0;
    clip.length = 4 * ppq;
    s.playlist.push_back (clip);
    return s;
}

struct Render
{
    std::vector<float> l, r;
    double rms (const std::vector<float>& x) const
    {
        double sum = 0.0;
        for (std::size_t i = x.size() / 4; i < x.size(); ++i) // (past the attack)
            sum += (double) x[i] * x[i];
        return std::sqrt (sum / (double) (x.size() - x.size() / 4));
    }
};

inline Render render (const lynx::Song& song, int blocks = 120)
{
    constexpr int rate = 44100, block = 256;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    auto state = lynx::RackState::build (song, nullptr, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, block);
    Render out;
    out.l.assign ((std::size_t) (blocks * block), 0.0f);
    out.r.assign ((std::size_t) (blocks * block), 0.0f);
    double tick = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        renderer.process (state.get(), true, tick, block, out.l.data() + b * block, out.r.data() + b * block);
        tick += block * tps;
    }
    return out;
}
} // namespace flclip190dz

TEST (flclip190dz_menu_model_is_fl_order_and_sections)
{
    using namespace lynx;
    // FL 2026's menu, both columns ([SHOT] flgap189o FL-clipmenu-none-selected, [FORM] EEAUMenu)
    const std::vector<std::string> fl {
        "[Audio clip]", "Preview", "-", "(v) Muted", "-", "Rename and color...", "Change color...", "Random color", "Change icon...", "-",
        "Select source channel >", "Channel settings", "Make unique", "Select all similar clips", "Delete", "-", "Properties...",
        "[Region]", "Select region >", "Chop >", "||",
        "[Sample]", "Make unique as sample", "Edit sample", "Pitch-correct sample", "Time-warp sample", "Extract stems from sample", "Deverb",
        "Detect tempo", "Fit to tempo", "[Automation]", "Automate >", "Crossfade with >", "[Fades]", "Reset fades", "Crossfade selected",
        "Fade in curve >", "Fade out curve >"
    };
    CHECK (flclip190dz::texts (flclipmenu::fl()) == fl);
    // exactly one column break: "Audio clip" + "Region" left, "Sample" + "Automation" + "Fades" right
    int breaks = 0;
    for (const auto& e : flclipmenu::fl())
        breaks += e.kind == flclipmenu::Entry::Kind::columnBreak ? 1 : 0;
    CHECK (breaks == 1);
    // FL's underlined letters ('&' in its captions)
    for (const auto& e : flclipmenu::fl())
    {
        if (e.text == "Properties...")
            CHECK (e.mnemonic == 'o');
        if (e.text == "Make unique as sample")
            CHECK (e.mnemonic == 'u');
        if (e.text == "Crossfade with")
            CHECK (e.mnemonic == 'f');
    }

    // what LYNX shows: FL's list without what it cannot do (Deverb: FL 2026 only offers a download there), Crossfade
    // selected only when FL shows it (round 190gw: Pitch-correct / Time-warp = LYNX's Newtone / Newtime, Crossfade with
    // works and is greyed only while no other channel's clip overlaps)
    const std::vector<std::string> usual {
        "[Audio clip]", "Preview", "-", "(v) Muted", "-", "Rename and color...", "Change color...", "Random color", "Change icon...", "-",
        "Select source channel >", "Channel settings", "Make unique", "Select all similar clips", "Delete", "-", "Properties...",
        "[Region]", "Select region >", "Chop >", "||",
        "[Sample]", "Make unique as sample", "Edit sample", "Pitch-correct sample", "Time-warp sample", "Extract stems from sample",
        "Detect tempo", "Fit to tempo", "[Automation]", "Automate >", "Crossfade with >", "[Fades]", "Reset fades", "Fade in curve >", "Fade out curve >"
    };
    CHECK (flclip190dz::texts (flclipmenu::shown (true, false)) == usual);
    auto overlapped = usual;
    overlapped.insert (std::find (overlapped.begin(), overlapped.end(), "Fade in curve >"), "Crossfade selected"); // (round 190ey: the curves shown)
    CHECK (flclip190dz::texts (flclipmenu::shown (false, true)) == overlapped);
    for (const auto& e : flclipmenu::shown (true, true))
        CHECK (e.have != flclipmenu::Entry::Have::missing);

    // LYNX's own items after FL's, in this order, none of them an FL label
    const auto extras = flclipmenu::lynxExtras();
    CHECK (extras.size() == 17 && extras.front() == "Color palette" && extras.back() == "Paste here");
    for (const auto& x : extras)
        CHECK (std::none_of (fl.begin(), fl.end(), [&x] (const std::string& f) { return f == x || f == x + " >"; }));

    // FL's Chop › Time based group, in FL's order
    const auto chops = flclipmenu::timeBasedChops();
    CHECK (chops.size() == 7 && std::string (chops.front().text) == "Chop in bars" && std::string (chops.back().text) == "Chop in 32nd beats");
    CHECK (chops[0].beatDivisor == 0 && chops[1].beatDivisor == 1 && chops[6].beatDivisor == 32);
}

TEST (flclip190dz_select_region_windows)
{
    using namespace lynx::flclipmenu;
    // regions from the cue markers: each runs to the next (or the end); duplicates and markers past the end drop out
    const auto regions = sampleRegions ({ 30000, 0, 10000, 10000, 90000 }, 40000);
    CHECK ((regions == std::vector<Region> { { 0, 10000 }, { 10000, 30000 }, { 30000, 40000 } }));
    CHECK (sampleRegions ({}, 40000).empty());
    // as the clip's offset / length: 10 frames per tick, the channel plays from frame 5000 of the file
    const auto w = regionWindow ({ 10000, 30000 }, 5000, 35000, 10.0);
    CHECK (w.has_value() && w->offset == 500 && w->length == 2000);
    CHECK (! regionWindow ({ 0, 4000 }, 5000, 35000, 10.0).has_value()); // before the played part
    CHECK ((fullWindow (35000, 10.0) == ClipWindow { 0, 3500 }));
    lynx::Clip clip;
    clip.offset = 500;
    clip.length = 2001;
    CHECK (currentRegion ({ { 0, 500 }, { 500, 2000 } }, clip) == 1);
    clip.length = 3500;
    CHECK (currentRegion ({ { 0, 500 }, { 500, 2000 } }, clip) == -1); // "Full"
}

TEST (flclip190dz_level_adjustment_math)
{
    using namespace lynx;
    // FL's law (REA_rodada3 §2, FUN_00fe9d20): gain = f(volume)·f(multiplier), f(x) = (11^x − 1) / 10, f(1) = 1
    CHECK (levelAdjustGain (1.0f) == 1.0f);
    CHECK (levelAdjustGain (0.0f) == 0.0f);
    CHECK (std::abs (levelAdjustGain (0.5f) - (std::sqrt (11.0f) - 1.0f) / 10.0f) < 1e-6f);
    CHECK (std::abs (levelAdjustGain (2.0f) - 12.0f) < 1e-4f);        // 200 %: +21.6 dB
    CHECK (std::abs (levelAdjustGain (5.0f) - 12.0f) < 1e-4f);        // (clamped to FL's travel)
    CHECK (std::abs (levelAdjustGain (0.5f) - channelGain (0.5f)) < 1e-7f); // the channel volume's own curve below 100 %
    // REA_rodada92b: FL's render with 7286 / 12800 was −10.4 dB (the master limiter); the law gives −10.7 dB
    CHECK (std::abs (20.0f * std::log10 (levelAdjustGain (7286.0f / 12800.0f)) + 10.7f) < 0.05f);
    // pan + offset (FUN_00fe9bf0), kept in −1..1
    CHECK (adjustedPan (0.25f, -0.5f) == -0.25f && adjustedPan (0.75f, 0.5f) == 1.0f && adjustedPan (-0.75f, -0.5f) == -1.0f);
    // Mod X on the cutoff knob's travel (20 Hz · 1000^x): no offset = the value bit for bit; +100 % = the top
    CHECK (adjustedCutoff (1234.5f, 0.0f) == 1234.5f && adjustedResonance (0.3f, 0.0f) == 0.3f);
    CHECK (std::abs (adjustedCutoff (20.0f * std::pow (1000.0f, 0.25f), 0.25f) - 20.0f * std::pow (1000.0f, 0.5f)) < 0.05f);
    CHECK (std::abs (adjustedCutoff (632.0f, 1.0f) - 20000.0f) < 1.0f && std::abs (adjustedResonance (0.3f, -0.5f)) < 1e-7f);
    // the defaults are neutral (every project from before this round)
    CHECK (ChannelMisc {}.levels.neutral() && ChannelMisc::LevelAdjust {}.volume == 1.0f);
}

TEST (flclip190dz_level_adjustment_automation_target)
{
    using namespace lynx;
    auto song = flclip190dz::synthSong();
    auto& levels = song.channels[0].misc.levels;
    const AutomationTarget pan { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::pan };
    const AutomationTarget volume { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::volume };
    const AutomationTarget modX { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::modX };
    // automation's 0..1: pan / Mod offsets −1..1, the volume multiplier 0..200 % (FL's whole knob travel, 0.5 = 100 %)
    CHECK (song.targetValue (volume) == 0.5f && song.targetValue (pan) == 0.5f && song.targetValue (modX) == 0.5f);
    CHECK (song.setTargetValue (volume, 0.25f) && levels.volume == 0.5f);
    CHECK (song.setTargetValue (pan, 0.0f) && levels.pan == -1.0f);
    CHECK (song.setTargetValue (modX, 1.0f) && levels.modX == 1.0f);
    CHECK (song.setTargetValue ({ AutomationTargetKind::channelAdjust, 0, 0, leveladjust::modY }, 0.75f) && levels.modY == 0.5f);
    CHECK (! song.setTargetValue ({ AutomationTargetKind::channelAdjust, 0, 0, 2 }, 0.5f)); // (FL's 0x12: not a Levels knob)
    CHECK (! song.setTargetValue ({ AutomationTargetKind::channelAdjust, 3, 0, leveladjust::pan }, 0.5f));
    CHECK (song.targetValue ({ AutomationTargetKind::channelAdjust, 0, 0, 2 }) < 0.0f);
    // the channel's targets: removing a channel before it moves the index; removing it drops the target
    CHECK (refs::targetsChannel (volume));
    song.channels.insert (song.channels.begin(), ChannelDef {});
    song.automations.push_back ({});
    song.automations.back().target = { AutomationTargetKind::channelAdjust, 1, 0, leveladjust::volume };
    song.removeChannel (0);
    CHECK (song.automations.back().target.index == 0 && song.automations.back().target.kind == AutomationTargetKind::channelAdjust);
    song.removeChannel (0);
    CHECK (song.automations.back().target.kind == AutomationTargetKind::none);
    // the last-tweaked read-out
    CHECK (lasttweak::valueText (song, volume, 0.5f) == "100%  0.0dB  1.00" && lasttweak::valueText (song, pan, 0.25f) == "L50"); // (round 190ey: FL's text)
}

TEST (flclip190dz_level_adjustment_plays_as_fl)
{
    using namespace lynx;
    const auto base = flclip190dz::synthSong();
    const auto plain = flclip190dz::render (base);
    const auto level = plain.rms (plain.l);
    CHECK (level > 1e-3);

    // neutral = the same samples (projects from before this round), also with a neutral automation of the adjustment
    auto neutral = base;
    neutral.channels[0].misc.levels = {};
    const auto same = flclip190dz::render (neutral);
    CHECK (same.l == plain.l && same.r == plain.r);
    auto automated = base;
    Automation a;
    a.target = { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::volume };
    a.points = { { 0, 0.5f, 0.0f }, { 4 * ppq, 0.5f, 0.0f } };
    automated.automations.push_back (a);
    a.target.param = leveladjust::pan;
    automated.automations.push_back (a);
    for (int k = 0; k < 2; ++k)
    {
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = k;
        c.track = 1 + k;
        c.start = 0;
        c.length = 4 * ppq;
        automated.playlist.push_back (c);
    }
    const auto still = flclip190dz::render (automated);
    CHECK (still.l == plain.l && still.r == plain.r);

    // volume multiplier 50 %: the whole channel f(0.5) quieter (−12.7 dB), on top of its own volume
    auto half = base;
    half.channels[0].misc.levels.volume = 0.5f;
    const auto quieter = flclip190dz::render (half);
    CHECK (std::abs (quieter.rms (quieter.l) / level - levelAdjustGain (0.5f)) < 1e-3);
    // the same through an automation clip of the multiplier (Automate › Volume's target)
    auto drawn = automated;
    drawn.automations[0].points = { { 0, 0.25f, 0.0f }, { 4 * ppq, 0.25f, 0.0f } };
    const auto viaClip = flclip190dz::render (drawn);
    CHECK (std::abs (viaClip.rms (viaClip.l) / level - levelAdjustGain (0.5f)) < 2e-3);
    // pan offset hard left: the right side goes silent, the channel's own pan untouched
    auto left = base;
    left.channels[0].misc.levels.pan = -1.0f;
    const auto panned = flclip190dz::render (left);
    CHECK (panned.rms (panned.r) < 1e-6 && panned.rms (panned.l) > level && left.channels[0].pan == 0.0f);
}

TEST (flclip190dz_flp_round_trip_and_automate_ids)
{
    using namespace lynx;
    auto song = flclip190dz::synthSong();
    song.channels[0].misc.levels = { -0.25f, 1.5f, 0.5f, -0.75f };
    Automation a;
    a.name = "Lead - Volume multiplier";
    a.target = { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::volume };
    a.points = { { 0, 0.5f, 0.0f }, { 4 * ppq, 0.25f, 0.0f } };
    song.automations.push_back (a);
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 0;
    c.track = 2;
    c.length = 4 * ppq;
    song.playlist.push_back (c);

    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    // FL's 0xE5 raw values: pan offset ±6400, volume 0..25600, Mod X / Y ±256
    bool found = false;
    for (const auto& ch : project.channels)
        if (ch.name == "Lead")
        {
            found = true;
            CHECK (ch.hasLevelAdjust && ch.panAdj == -1600 && ch.volAdj == 19200 && ch.modXAdj == 128 && ch.modYAdj == -192);
        }
    CHECK (found);
    // FL's automation link: channel parameter 0x11 of the channel
    CHECK (std::any_of (project.automationLinks.begin(), project.automationLinks.end(),
                        [] (const flp::AutomationLink& l) { return l.target.isChannel() && l.target.param() == 0x11; }));
    const auto back = importFlp (project, {});
    CHECK (back.channels.size() == 1 && back.channels[0].misc.levels == song.channels[0].misc.levels);
    CHECK (std::any_of (back.automations.begin(), back.automations.end(), [] (const Automation& x) {
        return x.target.kind == AutomationTargetKind::channelAdjust && x.target.index == 0 && x.target.param == leveladjust::volume
               && x.points.size() == 2 && std::abs (x.points[1].value - 0.25f) < 1e-3f;
    }));
    // the channel's own Volume / Pan come back as FL's knobs (no longer folded with the adjustment)
    CHECK (back.channels.size() == 1 && std::abs (back.channels[0].volume - song.channels[0].volume) < 1e-4f && back.channels[0].pan == song.channels[0].pan);
}
