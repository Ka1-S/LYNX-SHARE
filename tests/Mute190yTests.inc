// Round 190y, issue #144: a silenced mixer insert (its mute, a solo elsewhere) keeps showing its level, as FL's does,
// and what LYNX plays stays the same: nothing of it reaches the Master or the inserts it sends to.

namespace mute190y
{
struct Result
{
    std::vector<float> l, r;
    float insert1 = 0.0f, insert2 = 0.0f, insert3 = 0.0f, master = 0.0f;
};

// channel 0 plays a DC of 0.25 on insert 1 (which also sends to insert 2), channel 1 a DC of 0.1 on insert 3
Result render (const std::function<void (lynx::Song&)>& tweak, bool metering)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (4);
    s.mixer[1].sends.push_back ({ 2, 1.0f });
    for (const auto& [insert, path] : { std::pair { 1, "dc25" }, std::pair { 3, "dc10" } })
    {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = path;
        c.volume = 1.0f;
        c.mixerTrack = insert;
        s.channels.push_back (c);
    }
    s.patterns.emplace_back();
    tweak (s);
    auto provider = [] (const std::string& path) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (48000, path == "dc25" ? 0.25f : 0.1f);
        return std::shared_ptr<const SampleData> (d);
    };
    auto st = RackState::build (s, nullptr, 48000.0, provider);
    RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    LiveEventQueue live;
    for (int ch = 0; ch < 2; ++ch)
    {
        NoteEvent on;
        on.channel = ch;
        on.key = 60;
        on.velocity = 1.0f;
        live.push (on);
    }
    MeterBank meters;
    Result out;
    std::vector<float> l (256), r (256);
    for (int block = 0; block < 16; ++block)
    {
        renderer.process (st.get(), false, 0.0, 256, l.data(), r.data(), &live, metering ? &meters : nullptr);
        out.l.insert (out.l.end(), l.begin(), l.end());
        out.r.insert (out.r.end(), r.begin(), r.end());
    }
    out.insert1 = meters.takePeak (1, 0);
    out.insert2 = meters.takePeak (2, 0);
    out.insert3 = meters.takePeak (3, 0);
    out.master = meters.takePeak (0, 0);
    return out;
}
} // namespace mute190y

TEST (mute190y_muted_insert_keeps_its_meter_not_its_sound)
{
    using namespace lynx;
    const auto playing = mute190y::render ([] (Song&) {}, true);
    CHECK (playing.insert1 > 0.1f && playing.insert2 > 0.1f && playing.insert3 > 0.05f);

    const auto mute = [] (Song& s) { s.mixer[1].muted = true; };
    const auto muted = mute190y::render (mute, true);
    const auto unmetered = mute190y::render (mute, false);
    // the sound is what it was without meters (the old path): insert 3 only, nothing from insert 1 or its send
    CHECK (muted.l == unmetered.l && muted.r == unmetered.r);
    const auto alone = mute190y::render ([] (Song& s) { s.mixer[1].toMaster = false; s.mixer[1].sends.clear(); }, false);
    CHECK (muted.l == alone.l && muted.r == alone.r);
    // its meter shows the level it would have unmuted (fader without the mute); the send target and Master hear nothing of it
    CHECK (std::abs (muted.insert1 - playing.insert1) < 1.0e-4f);
    CHECK (muted.insert2 == 0.0f);
    const auto aloneMetered = mute190y::render ([] (Song& s) { s.mixer[1].toMaster = false; s.mixer[1].sends.clear(); }, true);
    CHECK (std::abs (muted.master - aloneMetered.master) < 1.0e-6f);

    // the fader still moves the muted insert's meter (FL: the meter is after the fader)
    const auto lower = mute190y::render ([] (Song& s) { s.mixer[1].muted = true; s.mixer[1].volume *= 0.5f; }, true);
    CHECK (lower.insert1 > 0.01f && lower.insert1 < muted.insert1 * 0.75f);

    // silenced by a solo elsewhere: the same
    const auto solo = mute190y::render ([] (Song& s) { s.mixer[3].solo = true; }, true);
    CHECK (std::abs (solo.insert1 - playing.insert1) < 1.0e-4f && solo.insert2 == 0.0f);

    // a Fruity Send in the muted insert sends nothing to its target
    const auto send = [] (Song& s) {
        s.mixer[1].muted = true;
        s.mixer[1].sends.clear();
        s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 256, 2));
    };
    const auto sendMuted = mute190y::render (send, true);
    const auto sendUnmetered = mute190y::render (send, false);
    CHECK (sendMuted.l == sendUnmetered.l && sendMuted.r == sendUnmetered.r);
    CHECK (sendMuted.insert2 == 0.0f && sendMuted.insert1 > 0.1f);

    // a muted Master: silent output, its meter keeps its level (FL)
    const auto masterMuted = mute190y::render ([] (Song& s) { s.mixer[0].muted = true; }, true);
    CHECK (std::all_of (masterMuted.l.end() - 2048, masterMuted.l.end(), [] (float x) { return x == 0.0f; }));
    CHECK (std::abs (masterMuted.master - playing.master) < 1.0e-4f && playing.master > 0.1f);

    // a controller in it (it would move other controls) keeps the old way: not run, no meter
    const auto controller = mute190y::render ([] (Song& s) {
        s.mixer[1].muted = true;
        s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::peakController));
    }, true);
    CHECK (controller.insert1 == 0.0f);
}

// Round 190y, FL 2026 renders (E:/lynx-work/mute190y/rt, insert 1 = a kick, Master peak): insert 1 muted with a mixer send
// to insert 8 -> 0.0000 (FL cuts the send); unmuted with only the send -> 0.6578; muted with a Fruity Send (dry 100 %) to
// insert 8 instead -> 0.8222, the same as insert 1 alone (FL keeps running a muted insert's effects: the Fruity Send is
// heard). Song::muteRunsEffects (.flp imports, new projects) follows that; older LYNX projects keep their old sound.
TEST (mute190y_muted_insert_runs_its_effects_like_fl)
{
    using namespace lynx;
    const auto fsend = [] (bool fl, bool muted) {
        return [fl, muted] (Song& s) {
            s.muteRunsEffects = fl;
            s.mixer[1].muted = muted;
            s.mixer[1].sends.clear();
            s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 256, 2));
        };
    };
    const auto peak = [] (const mute190y::Result& r) {
        float p = 0.0f;
        for (std::size_t i = 0; i < r.l.size(); ++i)
            p = std::max ({ p, std::abs (r.l[i]), std::abs (r.r[i]) });
        return p;
    };
    // insert 3's own channel off, so the Master hears insert 1's paths only
    const auto quiet = [] (const std::function<void (Song&)>& f) { return [f] (Song& s) { f (s); s.channels[1].muted = true; }; };

    // FL: a muted insert's mixer send is cut (both ways)
    for (const auto fl : { false, true })
    {
        const auto r = mute190y::render (quiet ([fl] (Song& s) { s.muteRunsEffects = fl; s.mixer[1].muted = true; }), false);
        CHECK (peak (r) == 0.0f);
    }
    // FL: its Fruity Send is heard, as loud as from the unmuted insert routed nowhere else
    const auto unmuted = mute190y::render (quiet ([fsend] (Song& s) { fsend (true, false) (s); s.mixer[1].toMaster = false; }), false);
    const auto flMuted = mute190y::render (quiet (fsend (true, true)), false);
    CHECK (peak (unmuted) > 0.1f);
    CHECK (std::abs (peak (flMuted) - peak (unmuted)) < 1.0e-4f);
    // the same live, with the meters on; and its meter moves
    const auto flMetered = mute190y::render (quiet (fsend (true, true)), true);
    CHECK (flMetered.l == flMuted.l && flMetered.insert1 > 0.1f && flMetered.insert2 > 0.1f);
    // a project from before keeps its sound: nothing from the muted insert
    const auto old = mute190y::render (quiet (fsend (false, true)), false);
    CHECK (peak (old) == 0.0f);

    // saved with the project; a project without the key (older) loads with it off; .flp imports and new projects: on
    Song s;
    CHECK (! s.muteRunsEffects);
}
