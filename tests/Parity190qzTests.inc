// Round 190qz (0.1.33 track, plug-in parity round 2): FL's controllers send a value to their links only when it changes,
// so before the first one a link's target keeps its own value — an Envelope Controller before a note has reached it
// (ChannelDef::engineRev 6; Asher Postman's "Basic Sidechain" → Love Philter: the intro and the Outro Synth solo 25 dB
// under FL), a Peak Controller whose slot has not processed a block (EffectSlot::engineRev 3; RawFL's "3xOsc #5" with the
// slot bypassed). REA_rodada190qz.
#include "core/FlpExport.h"
#include <cmath>

namespace parity190qz
{
// channel 0 a LYNX Synth note on insert 1 for 0.5 s; channel 1 an Envelope Controller at `rev` with no notes; when
// `link`, the controller's output 0 drives insert 1's volume
std::vector<float> render (int rev, bool link, bool continuous = false)
{
    using namespace lynx;
    constexpr double rate = 44100.0;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    synth.mixerTrack = 1;
    ChannelDef ctl;
    ctl.instrument = InstrumentKind::controller;
    ctl.engineRev = rev;
    auto d = defaultEnvelopeController();
    d.continuous = continuous;
    d[0].base = 0.2f;
    d[0].envOn = false;
    ctl.controllerState = writeEnvelopeController (d);
    s.channels = { synth, ctl };
    Pattern p;
    Note n;
    n.start = 0, n.length = 2 * ppq, n.key = 60, n.velocity = 0.78125f, n.channel = 0;
    p.notes.push_back (n);
    s.patterns = { p };
    Clip clip;
    clip.pattern = 0;
    clip.length = 4 * ppq;
    s.playlist = { clip };
    if (link)
    {
        Automation a;
        a.name = "link";
        a.target.kind = AutomationTargetKind::mixerVolume;
        a.target.index = 1;
        a.source.channel = 1;
        a.source.output = 0;
        s.automations.push_back (a);
    }
    auto state = RackState::build (s, nullptr, rate, [] (const std::string&) { return std::shared_ptr<const SampleData>(); });
    RackRenderer rr;
    constexpr int block = 256;
    rr.prepare (rate, block);
    const auto total = (int) (0.5 * rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int at = 0; at < total; at += block)
    {
        const auto len = std::min (block, total - at);
        rr.process (state.get(), true, (double) at * tps, len, l.data() + at, r.data() + at, nullptr);
    }
    return l;
}

double rms (const std::vector<float>& x)
{
    double sum = 0.0;
    for (const auto v : x)
        sum += (double) v * (double) v;
    return std::sqrt (sum / (double) std::max<std::size_t> (1, x.size()));
}
} // namespace parity190qz

TEST (parity190qz_envelope_controller_sends_after_its_first_note)
{
    for (const int rev : { 4, 6 })
    {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::controller;
        c.engineRev = rev;
        c.controllerState = lynx::writeEnvelopeController (lynx::defaultEnvelopeController());
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::controller);
        inst->prepare (48000.0);
        inst->configure (c, 120.0);
        // (before the first block too: the rack reads the outputs before the instrument renders)
        CHECK (inst->controlSent (0) == (rev < 6));
        std::vector<float> l (480), r (480);
        inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
        CHECK (inst->controlSent (0) == (rev < 6) && inst->controlSent (7) == (rev < 6));
        inst->noteOn (60, 0.8f);
        inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
        CHECK (inst->controlSent (0) && inst->controlSent (7)); // (every output's key range holds 60)
        inst->noteOff (60);
        for (int k = 0; k < 400; ++k) // the voice ends: the output holds its last value, still sent
            inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
        CHECK (inst->controlSent (0));
        CHECK (inst->controlSent (8) && inst->controlSent (-1)); // (the last key / velocity outputs: the editor's)
    }
    // continuous output sends from the start
    auto d = lynx::defaultEnvelopeController();
    d.continuous = true;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::controller;
    c.controllerState = lynx::writeEnvelopeController (d);
    CHECK (c.engineRev >= 6);
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::controller);
    inst->prepare (48000.0);
    inst->configure (c, 120.0);
    CHECK (inst->controlSent (0));
}

TEST (parity190qz_peak_controller_sends_after_its_first_block)
{
    for (const int rev : { 2, 3 })
    {
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::peakController);
        CHECK (slot.engineRev >= 3);
        slot.engineRev = rev;
        auto fx = lynx::makeEffect (slot);
        CHECK (fx != nullptr);
        if (fx == nullptr)
            continue;
        fx->prepare (44100.0);
        fx->reset();
        CHECK (fx->controlSent (0) == (rev < 3) && fx->controlSent (1) == (rev < 3) && fx->controlSent (2) == (rev < 3));
        std::vector<float> l (256, 0.25f), r (256, 0.25f);
        fx->process (l.data(), r.data(), 256, slot.params, 120.0);
        CHECK (fx->controlSent (0) && fx->controlSent (1) && fx->controlSent (2));
        fx->reset(); // (a transport reset: nothing sent until the next block, as at the start)
        CHECK (fx->controlSent (0) == (rev < 3));
    }
}

TEST (parity190qz_link_target_keeps_its_value_until_the_controller_sends)
{
    // revision 6: the controller never plays a note, so insert 1's fader stays where it is — the render is the one with
    // no link at all, bit for bit
    const auto none = parity190qz::render (6, false);
    const auto rev6 = parity190qz::render (6, true);
    CHECK (parity190qz::rms (none) > 1.0e-3);
    CHECK (rev6 == none);
    // revision 4 (a project saved before): the output's base (0.2) drives the fader from the start, as before
    const auto rev4 = parity190qz::render (4, true);
    CHECK (rev4 != none && parity190qz::rms (rev4) < 0.8 * parity190qz::rms (none));
    // continuous output sends its base from the start in either revision
    const auto cont = parity190qz::render (6, true, true);
    CHECK (cont == parity190qz::render (4, true, true) && cont != none);
}

namespace parity190qz
{
// one channel playing C5 for 2 beats with the note's Mod X / Y; 0.6 s at 44.1 kHz
std::vector<float> renderNote (const lynx::ChannelDef& channel, float modX, float modY)
{
    using namespace lynx;
    constexpr double rate = 44100.0;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.channels = { channel };
    Pattern p;
    Note n;
    n.start = 0, n.length = 2 * ppq, n.key = 72, n.velocity = 0.78125f, n.modX = modX, n.modY = modY;
    p.notes.push_back (n);
    s.patterns = { p };
    Clip clip;
    clip.pattern = 0;
    clip.length = 4 * ppq;
    s.playlist = { clip };
    auto state = RackState::build (s, nullptr, rate, [] (const std::string&) { return std::shared_ptr<const SampleData>(); });
    RackRenderer rr;
    constexpr int block = 256;
    rr.prepare (rate, block);
    const auto total = (int) (0.6 * rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int at = 0; at < total; at += block)
    {
        const auto len = std::min (block, total - at);
        rr.process (state.get(), true, (double) at * tps, len, l.data() + at, r.data() + at, nullptr);
    }
    return l;
}
} // namespace parity190qz

TEST (parity190qz_notes_mod_x_y_reach_three_osc_and_harmless_from_revision_6)
{
    using namespace lynx;
    // FL hands a note's Mod X / Y to the voice as Final.FCut / FRes ((b − 128) / 128), added to the voice filter's X / Y
    // (RawFL's "3xOsc #5": 16 notes at Mod X 40, its top oscillator 16 dB down in FL's render). Revision 6 on; a project
    // saved before (revision 4) plays every note at the centre, bit for bit as before.
    ChannelDef osc;
    osc.instrument = InstrumentKind::threeosc;
    osc.pluginState = flpexport::defaultThreeOscState();
    ChannelDef harmless;
    harmless.instrument = InstrumentKind::harmless;
    for (auto* ch : { &osc, &harmless })
    {
        ch->engineRev = 4;
        const auto centre4 = parity190qz::renderNote (*ch, 0.5f, 0.5f), low4 = parity190qz::renderNote (*ch, 0.15625f, 0.5f);
        ch->engineRev = 6;
        const auto centre6 = parity190qz::renderNote (*ch, 0.5f, 0.5f), low6 = parity190qz::renderNote (*ch, 0.15625f, 0.5f),
                   res6 = parity190qz::renderNote (*ch, 0.5f, 0.9f);
        CHECK (parity190qz::rms (centre4) > 1.0e-4);
        CHECK (low4 == centre4);      // revision 4: the note's Mod X is not heard
        CHECK (centre6 == centre4);   // a note at the centre: the revision changes nothing
        CHECK (low6 != centre6);      // revision 6: Mod X moves the voice filter
        CHECK (res6 != centre6);      // and Mod Y its resonance
        std::printf ("  %s note Mod X 40 / 256: %.2f -> %.2f dB\n", ch == &osc ? "3x Osc" : "Harmless",
                     20.0 * std::log10 (parity190qz::rms (centre6)), 20.0 * std::log10 (parity190qz::rms (low6)));
    }
}
