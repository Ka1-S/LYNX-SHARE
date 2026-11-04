// Round 169h: Ogun / Autogun parameters driven live by automation clips and the editor's knobs (FL's ProcessParam,
// REA_rodada169h §1), Autogun in the editor pictures (§3)

#include "core/ModelParamsSynths.h"
#include "core/Ogun.h"

namespace inst169h
{
// FL's Ogun preset "300 Ft Glass Bowl" as a new channel gets it (natives::makeChannel), or nullopt without FL
std::optional<lynx::ChannelDef> ogunChannel()
{
    if (realFlpDir.empty())
        return std::nullopt;
    auto c = lynx::natives::makeChannel (lynx::InstrumentKind::ogun, natives142::flAccess());
    if (! c || c->instrument != lynx::InstrumentKind::ogun || ! lynx::ogunStateUsable (c->pluginState))
        return std::nullopt;
    return c;
}

// renders `blocks` blocks of 512 of a held C5, calling `between (block)` before each block
template <typename Between>
std::vector<float> run (lynx::Instrument& inst, int blocks, Between&& between)
{
    std::vector<float> out;
    std::vector<float> l (512), r (512);
    inst.noteOn (60, 0.9f);
    for (int b = 0; b < blocks; ++b)
    {
        between (b);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        inst.render (l.data(), r.data(), 512, 1.0f, 1.0f);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

double energy (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (auto i = from; i < to && i < v.size(); ++i)
        e += (double) v[i] * v[i];
    return e;
}

float normalisedOf (const lynx::EngineParam& p, int raw) { return (float) (raw - p.low) / (p.high - p.low); }
} // namespace inst169h

TEST (inst169h_ogun_automation_is_live)
{
    using namespace inst169h;
    const auto channel = ogunChannel();
    if (! channel)
        return;
    const auto& table = lynx::ogunEngineParams();
    auto fresh = [&] {
        auto inst = lynx::makeOgunInstrument();
        inst->prepare (44100.0);
        inst->configure (*channel, 140.0);
        return inst;
    };
    const auto reference = [&] {
        auto inst = fresh();
        return run (*inst, 40, [] (int) {});
    }();
    CHECK (energy (reference, 10240, 20480) > 1e-3);

    // the parameter's own value again: nothing changes (bit for bit)
    const auto vol = *lynx::findEngineParam (table, 0);
    const auto p0 = *lynx::ogunParam (channel->pluginState, 0);
    {
        auto inst = fresh();
        const auto same = run (*inst, 40, [&] (int b) { if (b == 5) inst->setParameter (0, normalisedOf (vol, p0)); });
        CHECK (same == reference);
    }
    // Master volume to 0 while the note plays: FL's law (3^(x) − 1)/2 is 0 at x = 0 — the voice falls silent after the
    // gain ramp (only the effects' tail goes on), the samples before the change are the reference's; back to its value:
    // the same voice goes on (no new note)
    {
        auto inst = fresh();
        const auto silent = run (*inst, 40, [&] (int b) {
            if (b == 10)
                inst->setParameter (0, 0.0f);
        });
        auto inst2 = fresh();
        const auto back = run (*inst2, 40, [&] (int b) {
            if (b == 10)
                inst2->setParameter (0, 0.0f);
            if (b == 20)
                inst2->setParameter (0, normalisedOf (vol, p0));
        });
        const auto ref = energy (reference, 30 * 512, 40 * 512);
        std::printf ("  Ogun Master volume 0 at block 10: blocks 30-40 %.3g (reference %.3g), restored at 20: %.3g\n",
                     energy (silent, 30 * 512, 40 * 512), ref, energy (back, 30 * 512, 40 * 512));
        CHECK (std::equal (silent.begin(), silent.begin() + 10 * 512, reference.begin()));
        CHECK (energy (silent, 30 * 512, 40 * 512) < 0.02 * ref);
        CHECK (energy (back, 30 * 512, 40 * 512) > 0.25 * ref);
    }
    // the coarse pitch (read by every control block) changes the sound at once, the voice keeps playing; every
    // automatable index is safe at its ends while a note plays (effects re-designed live, lines re-allocated /
    // cleared like FL)
    {
        const auto crs = *lynx::findEngineParam (table, 1);
        const auto p1 = *lynx::ogunParam (channel->pluginState, 1);
        auto inst = fresh();
        const auto out = run (*inst, 40, [&] (int b) {
            if (b == 10)
                inst->setParameter (1, normalisedOf (crs, std::min (p1 + 7, (int) crs.high)));
        });
        CHECK (std::equal (out.begin(), out.begin() + 10 * 512, reference.begin()));
        CHECK (! std::equal (out.begin() + 12 * 512, out.begin() + 20 * 512, reference.begin() + 12 * 512));
        CHECK (energy (out, 12 * 512, 20 * 512) > 1e-6);
    }
    for (const auto& p : table)
    {
        auto inst = fresh();
        const auto out = run (*inst, 12, [&] (int b) {
            if (b == 2)
                inst->setParameter (p.index, 0.0f);
            if (b == 5)
                inst->setParameter (p.index, 1.0f);
            if (b == 8)
                inst->setParameter (p.index, 0.37f);
        });
        CHECK (std::all_of (out.begin(), out.end(), [] (float x) { return std::isfinite (x) && std::fabs (x) < 64.0f; }));
    }
    // Richness and Quality are not FL automation targets (ProcessParam takes 0…48 only): no effect
    {
        auto inst = fresh();
        const auto out = run (*inst, 40, [&] (int b) {
            if (b == 3)
                inst->setParameter (52, 0.0f), inst->setParameter (54, 1.0f);
        });
        CHECK (out == reference);
    }
}

TEST (inst169h_ogun_knob_edits_keep_the_note_and_automation_survives_a_rebuild)
{
    using namespace inst169h;
    const auto channel = ogunChannel();
    if (! channel)
        return;
    auto inst = lynx::makeOgunInstrument();
    inst->prepare (44100.0);
    inst->configure (*channel, 140.0);
    // a knob of the editor (the state changes in P[0…48] only): the held note goes on, now silent with Volume 0
    auto edited = *channel;
    CHECK (lynx::ogunSetParam (edited.pluginState, 0, 0));
    const auto out = run (*inst, 30, [&] (int b) {
        if (b == 10)
            inst->configure (edited, 140.0);
        if (b == 20)
            inst->configure (*channel, 140.0); // (back: the same voice, still held)
    });
    std::printf ("  Ogun knob Volume 0 at block 10: blocks 16-20 %.3g, back at 20: blocks 24-30 %.3g\n", energy (out, 16 * 512, 20 * 512),
                 energy (out, 24 * 512, 30 * 512));
    CHECK (energy (out, 16 * 512, 20 * 512) < 0.05 * energy (out, 24 * 512, 30 * 512)); // (the effects' tail only)
    CHECK (energy (out, 24 * 512, 30 * 512) > 1e-4);

    // an automated value outlives a rebuild of the engine (a new tempo)
    auto inst2 = lynx::makeOgunInstrument();
    inst2->prepare (44100.0);
    inst2->configure (*channel, 140.0);
    inst2->setParameter (0, 0.0f);
    inst2->configure (*channel, 100.0);
    const auto after = run (*inst2, 12, [] (int) {});
    CHECK (energy (after, 0, after.size()) < 1e-9); // (a new engine: no tail yet)
}

TEST (inst169h_ogun_automation_clip_drives_the_engine)
{
    using namespace inst169h;
    const auto channel = ogunChannel();
    if (! channel)
        return;
    auto render = [&] (bool automated) {
        lynx::Song s;
        s.mixer = lynx::Song::defaultMixer (2);
        s.channels.push_back (*channel);
        s.channels[0].mixerTrack = 0;
        s.patterns.emplace_back();
        s.mode = lynx::PlayMode::song;
        if (automated)
        {
            lynx::Automation a; // Master volume: held at 0
            a.target = { lynx::AutomationTargetKind::channelParam, 0, 0, 0 };
            a.points = { { 0, 0.0f, 0.0f }, { lynx::ppq * 16, 0.0f, 0.0f } };
            s.automations.push_back (a);
            lynx::Clip clip;
            clip.kind = lynx::ClipKind::automation;
            clip.automation = 0;
            clip.length = lynx::ppq * 16;
            s.playlist.push_back (clip);
        }
        auto state = lynx::RackState::build (s, nullptr, 48000.0);
        lynx::RackRenderer r;
        r.prepare (48000.0, 256);
        lynx::LiveEventQueue live;
        lynx::NoteEvent on;
        on.channel = 0;
        on.key = 60;
        on.velocity = 0.9f;
        live.push (on);
        std::vector<float> l (256), rr (256), all;
        for (int b = 0; b < 120; ++b)
        {
            r.process (state.get(), true, (double) b * 256.0 * 140.0 / 60.0 * lynx::ppq / 48000.0, 256, l.data(), rr.data(), b == 0 ? &live : nullptr);
            all.insert (all.end(), l.begin(), l.end());
        }
        return energy (all, 20 * 256, all.size());
    };
    const auto plain = render (false), automated = render (true);
    std::printf ("  Ogun with a Master volume clip at 0: %.3g (plain %.3g)\n", automated, plain);
    CHECK (plain > 1e-4 && automated < plain * 1e-6);
}

TEST (inst169h_autogun_shot_channel)
{
    if (realFlpDir.empty())
        return;
    const auto c = lynx::natives::autogunShotChannel (natives142::flAccess());
    CHECK (c.has_value());
    if (! c)
        return;
    CHECK (c->instrument == lynx::InstrumentKind::autogun && c->name == "Autogun" && lynx::autogunStateUsable (c->pluginState));
    CHECK (lynx::modelparams::tableOf (c->instrument) != nullptr);
    // Magic dust (P63) is live from automation: the stage switches off at 0 (FL's Gen_Render test), the voice goes on
    auto inst = lynx::makeAutogunInstrument();
    inst->prepare (44100.0);
    inst->configure (*c, 140.0);
    const auto out = inst169h::run (*inst, 20, [&] (int b) {
        if (b == 10)
            inst->setParameter (63, 0.0f);
    });
    CHECK (std::all_of (out.begin(), out.end(), [] (float x) { return std::isfinite (x); }));
    CHECK (inst169h::energy (out, 12 * 512, 20 * 512) > 0.0);
}
