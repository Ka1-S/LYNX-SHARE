// Round 190qp (0.1.31.1 hotfix: issue #253 "Still broken in 0.1.31", Discord "volume automations are still broken",
// "the Fruity Filter's cutoff doesn't follow its clip"): an FL project's mixer volume, send level and effect parameter
// automation, written as FL writes it, imported back and rendered; and the values the controls show (AutomationDisplay:
// the mixer's faders and send knobs, the plugin windows, the Device view) for FL's clips linked to several controls.

namespace hotfix190qp
{
// a LYNX Synth chord into insert 1 (a Fruity Filter in its first slot), insert 1 to the Master and sent to insert 2, a
// bus to the Master; `toMaster` false: insert 1 is heard only through the send
inline lynx::Song busSong (std::int64_t length, bool toMaster)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.clear();
    s.playlist.clear();
    s.automations.clear();
    s.patterns.clear();
    s.mixer = Song::defaultMixer (3);
    s.mode = PlayMode::song;
    ChannelDef c;
    c.name = "Pad";
    c.instrument = InstrumentKind::synth;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.mixer[1].name = "PLUCK 2";
    s.mixer[2].name = "MUSIC BUS";
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::fruityFilter));
    s.mixer[1].toMaster = toMaster;
    s.setRoute (1, 2, true, 1.0f);
    Pattern p;
    p.name = "Chord";
    for (const auto key : { 48, 55, 60, 64 })
    {
        Note n;
        n.start = 0;
        n.length = length;
        n.key = key;
        n.channel = 0;
        p.notes.push_back (n);
    }
    s.patterns.push_back (p);
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = 0;
    clip.track = 0;
    clip.start = 0;
    clip.length = length;
    s.playlist.push_back (clip);
    return s;
}

// `target` automated by two clips back to back (as the reporter's split clips): the first ramps `a0` -> `a1`, the second
// holds `b`
inline void twoClips (lynx::Song& s, const lynx::AutomationTarget& target, const char* name, float a0, float a1, float b, std::int64_t half)
{
    using namespace lynx;
    for (int k = 0; k < 2; ++k)
    {
        Automation a;
        a.name = name;
        a.target = target;
        a.points = k == 0 ? std::vector<AutomationPoint> { { 0, a0, 0.0f }, { half, a1, 0.0f } }
                          : std::vector<AutomationPoint> { { 0, b, 0.0f }, { half, b, 0.0f } };
        s.automations.push_back (a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) s.automations.size() - 1;
        c.track = 2 + c.automation;
        c.start = k * half;
        c.length = half;
        s.playlist.push_back (c);
    }
}

inline lynx::Song throughFl (const lynx::Song& song)
{
    lynx::flpexport::Options options;
    lynx::flpexport::Report report;
    return lynx::importFlp (flp::parseProject (lynx::flpexport::exportFlp (song, options, report)));
}

// the RMS of the Master's left channel over [from, to) song ticks
inline double rmsOver (const lynx::Song& song, double from, double to)
{
    constexpr int rate = 44100, block = 256;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    auto state = lynx::RackState::build (song, nullptr, rate);
    lynx::RackRenderer renderer;
    renderer.prepare (rate, block);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    double sum = 0.0;
    long count = 0;
    for (double tick = 0.0; tick < to; tick += block * tps)
    {
        renderer.process (state.get(), true, tick, block, l.data(), r.data());
        if (tick >= from && tick + block * tps <= to)
        {
            for (const auto x : l)
                sum += (double) x * x;
            count += block;
        }
    }
    return count > 0 ? std::sqrt (sum / (double) count) : 0.0;
}

inline std::optional<float> shownAt (const lynx::Song& song, const lynx::AutomationTarget& t, double tick)
{
    lynx::AutomationDisplay d;
    d.update (song, 1, tick, true);
    return d.valueOf (t);
}
} // namespace hotfix190qp

TEST (hotfix190qp_imported_fl_volume_send_and_filter_automation_play)
{
    using namespace lynx;
    using namespace hotfix190qp;
    const auto half = 4 * ppq, length = 2 * half;
    const AutomationTarget volume { AutomationTargetKind::mixerVolume, 1, 0, 0 }, send { AutomationTargetKind::mixerSend, 1, 2, 0 },
                           cutoff { AutomationTargetKind::effectParam, 1, 0, 0 };
    // each target: the second clip holds the value low, then high; the rendered level in the second clip follows it, and
    // the shown value is the clip's
    struct Case
    {
        AutomationTarget target;
        const char* name;
        bool toMaster;
        bool throughFlp = true; // (the native Fruity Filter's cutoff: in LYNX only, its .flp export drops the clip, a known gap)
    };
    for (const auto& c : { Case { volume, "PLUCK 2 - Volume", true }, Case { send, "PLUCK 2 - To MUSIC BUS", false },
                           Case { cutoff, "Filter - Cutoff frequency", true, false } })
    {
        double level[2] {};
        for (int k = 0; k < 2; ++k)
        {
            auto song = busSong (length, c.toMaster);
            twoClips (song, c.target, c.name, 0.8f, 0.2f, k == 0 ? 0.1f : 0.8f, half);
            const auto imported = c.throughFlp ? throughFl (song) : song;
            // the target the import gave the clips (an FL plugin's parameter may come back at LYNX's own index)
            AutomationTarget target;
            int driving = 0;
            for (const auto& a : imported.automations)
                if (a.name.rfind (c.name, 0) == 0)
                {
                    if (driving == 0)
                        target = a.target;
                    driving += a.target == target ? 1 : 0;
                }
            CHECK (driving == 2 && target.kind == c.target.kind && target.index == c.target.index);
            if (driving != 2 || target.kind != c.target.kind || target.index != c.target.index)
                for (const auto& a : imported.automations)
                    std::fprintf (stderr, "    %s: imported automation \"%s\" kind %d index %d slot %d param %d, links %d\n", c.name, a.name.c_str(),
                                  (int) a.target.kind, a.target.index, a.target.slot, a.target.param, (int) a.links.size());
            level[k] = rmsOver (imported, (double) half + ppq, (double) length);
            const auto shown = shownAt (imported, target, (double) half + 2.0 * ppq);
            CHECK (shown.has_value() && std::abs (*shown - (k == 0 ? 0.1f : 0.8f)) < 0.02f);
        }
        CHECK (level[1] > 0.001 && level[0] < 0.7 * level[1]); // (low: quieter, a shut filter or a closed send)
    }
}

TEST (hotfix190qp_controls_show_a_clip_linked_to_several_controls)
{
    using namespace lynx;
    using namespace hotfix190qp;
    // FL's clip linked to two controls (round 190ov's target links): the first through no formula, the second through
    // a scale and a formula. Before 0.1.31.1 the controls showed the clip's raw value on the first control only (the
    // sound followed both: RackRenderer linkGroupValueAt).
    const auto half = 4 * ppq;
    auto song = busSong (2 * half, true);
    const AutomationTarget volume { AutomationTargetKind::mixerVolume, 1, 0, 0 }, send { AutomationTargetKind::mixerSend, 1, 2, 0 };
    Automation a;
    a.name = "Sidechain";
    a.target = volume;
    a.points = { { 0, 0.5f, 0.0f }, { half, 0.5f, 0.0f } };
    a.links = { { volume, "", {} }, { send, "1-Input", { 0.0f, 0.5f } } };
    song.automations.push_back (a);
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 0;
    c.track = 3;
    c.start = 0;
    c.length = half;
    song.playlist.push_back (c);
    const auto onVolume = shownAt (song, volume, (double) ppq);
    const auto onSend = shownAt (song, send, (double) ppq);
    CHECK (onVolume.has_value() && std::abs (*onVolume - 0.5f) < 1e-4f);
    CHECK (onSend.has_value() && std::abs (*onSend - 0.25f) < 1e-4f); // (1 - 0.5 = 0.5, through the scale 0..0.5: 0.25)
    // a plain clip of the send after it: the one that plays decides, as the renderer
    Automation plain;
    plain.name = "PLUCK 2 - To MUSIC BUS";
    plain.target = send;
    plain.points = { { 0, 0.9f, 0.0f }, { half, 0.9f, 0.0f } };
    song.automations.push_back (plain);
    c.automation = 1;
    c.track = 4;
    c.start = half;
    song.playlist.push_back (c);
    const auto later = shownAt (song, send, (double) half + ppq);
    CHECK (later.has_value() && std::abs (*later - 0.9f) < 1e-4f);
    const auto before = shownAt (song, send, (double) ppq);
    CHECK (before.has_value() && std::abs (*before - 0.25f) < 1e-4f);
}

TEST (hotfix190qp_a_control_moved_by_hand_shows_its_own_value)
{
    using namespace lynx;
    using namespace hotfix190qp;
    // the engine's released targets (FL: moved during playback, until the next Play; Live's override in Clip-launch,
    // until Re-Enable Automation): the renderer plays the hand-set value, so the control shows it, not the clip
    const auto half = 4 * ppq;
    auto song = busSong (2 * half, true);
    const AutomationTarget volume { AutomationTargetKind::mixerVolume, 1, 0, 0 }, send { AutomationTargetKind::mixerSend, 1, 2, 0 };
    twoClips (song, volume, "PLUCK 2 - Volume", 0.8f, 0.2f, 0.4f, half);
    twoClips (song, send, "PLUCK 2 - To MUSIC BUS", 0.1f, 0.9f, 0.5f, half);
    AutomationDisplay d;
    const std::vector<AutomationTarget> released { volume };
    CHECK (d.update (song, 1, (double) half + ppq, true, &released));
    CHECK (! d.valueOf (volume).has_value() && std::abs (d.shown (volume, 0.66f) - 0.66f) < 1e-6f);
    CHECK (d.valueOf (send).has_value() && std::abs (*d.valueOf (send) - 0.5f) < 1e-4f);
    // released no more (Play again / Re-Enable Automation): the automation shows again
    CHECK (d.update (song, 1, (double) half + ppq, true));
    CHECK (d.valueOf (volume).has_value() && std::abs (*d.valueOf (volume) - 0.4f) < 1e-4f);
}
