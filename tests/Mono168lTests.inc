// Round 168l (YouTube review of 0.1.21, 5:33–5:58: "this monophonic is not turning on. So I really can't play legato
// stuff"): Channel settings › Misc › Mono / Porta / Slide on every generator, played by the rack's channel layer as FL's
// channel voice code does (REA_rodada168l, core/ChannelPoly.h)

#include "core/ChannelPoly.h"
#include "core/HostSlide.h"

namespace mono168l
{
// an instrument that records what the rack asks of it (a hosted plugin's place in the rack)
struct VoiceSpy final : lynx::Instrument
{
    struct Got
    {
        char what = 'n'; // n note on, f note off, s slide
        int key = 0;
        float cents = 0.0f, glideFrom = 0.0f, glideSamples = 0.0f, samples = 0.0f;
    };
    std::vector<Got> got;
    float nextCents = 0.0f;
    void prepare (double) noexcept override {}
    void setNextNoteExpression (float, float cents, bool) noexcept override { nextCents = cents; }
    void noteOn (int key, float) noexcept override
    {
        auto& g = lynx::HostSlide::pendingGlide();
        got.push_back ({ 'n', key, nextCents, g.fromCents, g.samples, 0.0f });
        g.taken = g.samples > 0.0f;
        nextCents = 0.0f;
    }
    void noteOff (int key) noexcept override { got.push_back ({ 'f', key }); }
    void slideTo (int key, float cents, float samples) noexcept override { got.push_back ({ 's', key, cents, 0.0f, 0.0f, samples }); }
    bool followsChannelPorta() const noexcept override { return true; }
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    std::string trace() const
    {
        std::string s;
        for (const auto& g : got)
            s += std::string (1, g.what) + std::to_string (g.key) + " ";
        return s;
    }
};

struct Rig
{
    lynx::Song song;
    std::shared_ptr<VoiceSpy> spy = std::make_shared<VoiceSpy>();
    std::unique_ptr<const lynx::RackState> state;
    lynx::RackRenderer renderer;
    lynx::LiveEventQueue live;
    lynx::RackState::Factories f;

    explicit Rig (std::function<void (lynx::ChannelDef&)> setup)
    {
        song.mixer = lynx::Song::defaultMixer (2);
        song.bpm = 120.0;
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::plugin;
        c.plugin.id = "spy";
        setup (c);
        song.channels.push_back (c);
        song.patterns.emplace_back();
        f.pluginInstruments = [this] (const lynx::PluginRef&, double) { return std::shared_ptr<lynx::Instrument> (spy); };
        state = lynx::RackState::build (song, nullptr, 48000.0, f);
        renderer.prepare (48000.0, 256);
    }
    void play (int key, bool on = true, int colour = 0)
    {
        lynx::NoteEvent e;
        e.type = on ? lynx::NoteEvent::Type::on : lynx::NoteEvent::Type::off;
        e.key = key;
        e.velocity = 0.8f;
        e.colour = (std::int8_t) colour;
        live.push (e);
        std::vector<float> l (256), r (256);
        renderer.process (state.get(), false, 0.0, 256, l.data(), r.data(), &live, nullptr);
    }
};
} // namespace mono168l

TEST (mono168l_misc_mono_porta_slide_on_every_generator)
{
    using namespace lynx;
    // the video's channels: an imported 3x Osc, LYNX's built-in Kick / Hat, Toxic Biohazard, a Sampler, a hosted plugin
    for (const auto kind : { InstrumentKind::threeosc, InstrumentKind::kick, InstrumentKind::hat, InstrumentKind::toxic, InstrumentKind::sampler,
                             InstrumentKind::plugin, InstrumentKind::sytrus, InstrumentKind::soundfont, InstrumentKind::patcher })
    {
        ChannelDef c;
        c.instrument = kind;
        CHECK (channelHasMono (c) && channelHasPortamento (c)); // (main: only the LYNX Synth / Sampler had them)
        setChannelMono (c, true);
        setChannelPortamento (c, true, 0.5f);
        CHECK (channelMono (c) && channelPortamento (c) && std::abs (channelPortamentoBeats (c) - 0.5f) < 1e-6f);
        setChannelMono (c, false);
        CHECK (! channelMono (c) && channelPortamento (c));
    }
    // the LYNX Synth keeps its own flags (its page and the Misc tab show the same)
    ChannelDef synth;
    setChannelMono (synth, true);
    setChannelPortamento (synth, true, 9.0f); // (clamped to 4 beats)
    CHECK (synth.synth.mono && synth.synth.porta && std::abs (synth.synth.portaBeats - 4.0f) < 1e-6f && channelMono (synth));
    // channels with no sound of their own have nothing to make mono
    for (const auto kind : { InstrumentKind::controller, InstrumentKind::midiOut, InstrumentKind::layer, InstrumentKind::vfx, InstrumentKind::audioClip })
    {
        ChannelDef c;
        c.instrument = kind;
        setChannelMono (c, true);
        CHECK (! channelHasMono (c) && ! channelMono (c));
    }
}

TEST (mono168l_fl_glide_time)
{
    using namespace lynx::channelpoly;
    // FL's Slide knob: n = round((27^(v/1024) − 1)·48/26) 1/192 beats; FL's porta ticks n·24/48 at 96 PPQ; the glide
    // lasts ticks + 1 ticks (FLEngine 0x1312200, 0xd26460)
    CHECK (portaTicks (24.0f / 192.0f) == 12 && std::abs (glideBeats (24.0f / 192.0f) - 13.0 / 96.0) < 1e-12); // 820, FL's default
    CHECK (portaTicks (384.0f / 192.0f) == 192 && std::abs (glideBeats (2.0f) - 193.0 / 96.0) < 1e-12);       // 1660, the maximum
    CHECK (portaTicks (1.0f / 192.0f) == 0 && std::abs (glideBeats (1.0f / 192.0f) - 1.0 / 96.0) < 1e-12);    // ~0: no porta glide
    CHECK (portaTicks (7.0f / 192.0f) == 3);                                                                  // 500, old files
}

TEST (mono168l_channel_layer_mono_and_porta_like_fl)
{
    using namespace lynx::channelpoly;
    Layer layer;
    Mode mono;
    mono.mono = true;
    // first note: a voice
    auto on = layer.noteOn (mono, 60, 0.0f, false, 0);
    CHECK (on.start && ! on.slide && ! on.glide && on.release < 0);
    // played over it: no voice, the sounding one glides there (FL's virtual voice, 0xd236d0 → 0xfeaf70)
    on = layer.noteOn (mono, 67, 0.0f, false, 0);
    CHECK (! on.start && on.slide);
    on = layer.noteOn (mono, 72, 0.0f, false, 0);
    CHECK (! on.start && on.slide);
    // the newest released: back to the newest still held (67); then 67 released: back to 60
    auto off = layer.noteOff (mono, 72, 0);
    CHECK (off.slide && off.key == 67 && off.release < 0);
    off = layer.noteOff (mono, 67, 0);
    CHECK (off.slide && off.key == 60 && off.release < 0);
    // the last held note ends the voice, whatever its key (the voice keeps its first key)
    layer.noteOn (mono, 64, 0.0f, false, 0);
    off = layer.noteOff (mono, 60, 0);
    CHECK (! off.slide || off.key == 64);
    off = layer.noteOff (mono, 64, 0);
    CHECK (off.release == 60 && ! off.slide);
    // not overlapping: a new voice
    on = layer.noteOn (mono, 62, 0.0f, false, 0);
    CHECK (on.start && ! on.slide);
    // another colour is another group (FL: one mono voice per note colour / MIDI channel)
    on = layer.noteOn (mono, 50, 0.0f, false, 3);
    CHECK (on.start && ! on.slide);
    CHECK (layer.noteOff (mono, 50, 3).release == 50 && layer.noteOff (mono, 62, 0).release == 62);
    // an instrument that cannot slide: the new note takes the voice over
    Mode drums = mono;
    drums.slides = false;
    layer.noteOn (drums, 36, 0.0f, false, 0);
    on = layer.noteOn (drums, 38, 0.0f, false, 0);
    CHECK (on.start && on.release == 36);
    CHECK (layer.noteOff (drums, 36, 0).release < 0 && layer.noteOff (drums, 38, 0).release == 38);
    // with the arpeggiator on FL keeps no mono list (0xd28910)
    Mode arp = mono;
    arp.arpeggiator = true;
    layer.noteOn (arp, 60, 0.0f, false, 0);
    CHECK (layer.noteOn (arp, 64, 0.0f, false, 0).start);
    layer.reset();

    // Porta: from the group's last note (even ended), linear in cents; a note's porta flag inverts the channel's
    Mode porta;
    porta.porta = true;
    CHECK (! layer.noteOn (porta, 60, 0.0f, false, 0).glide); // no note before
    layer.noteOff (porta, 60, 0);
    on = layer.noteOn (porta, 67, 25.0f, false, 0);
    CHECK (on.start && on.glide && std::abs (on.glideFrom - (6000.0f - 6725.0f)) < 1e-3f);
    CHECK (! layer.noteOn (porta, 72, 0.0f, true, 0).glide); // porta note on a Porta channel: no glide
    Mode plain;
    on = layer.noteOn (plain, 60, 0.0f, true, 0); // porta note on a channel without Porta: glide
    CHECK (on.glide && std::abs (on.glideFrom - 1200.0f) < 1e-3f);
    // no glide with a Slide under one tick, the echo delay's feedback, the arpeggiator, or an engine that can't
    {
        auto tiny = porta, echo = porta, arpeggio = porta, engine = porta;
        tiny.portaBeats = 1.0f / 192.0f;
        echo.echo = true;
        arpeggio.arpeggiator = true;
        engine.glidesIn = false;
        int key = 50;
        for (const auto& m : { tiny, echo, arpeggio, engine })
            CHECK (! layer.noteOn (m, ++key, 0.0f, false, 0).glide);
    }
    // Mono + Porta, not overlapping: the new voice glides in from the last note
    layer.reset();
    Mode both = mono;
    both.porta = true;
    layer.noteOn (both, 60, 0.0f, false, 0);
    layer.noteOff (both, 60, 0);
    on = layer.noteOn (both, 64, 0.0f, false, 0);
    CHECK (on.start && on.glide && std::abs (on.glideFrom + 400.0f) < 1e-3f);
}

TEST (mono168l_rack_plays_mono_and_porta_in_front_of_a_plugin)
{
    using namespace lynx;
    {
        // Mono: one note on, the rest are slides of the sounding voice; its off when the last key goes up
        mono168l::Rig rig ([] (ChannelDef& c) { setChannelMono (c, true); });
        rig.play (60);
        rig.play (64);
        rig.play (67);
        rig.play (67, false);
        rig.play (64, false);
        rig.play (60, false);
        CHECK (rig.spy->trace() == "n60 s64 s67 s64 s60 f60 ");
        // FL's glide time at 120 BPM (Slide 0.125 beat → 13 ticks of 96 PPQ): 13/96 beat = 3250 samples at 48 kHz
        CHECK (std::abs (rig.spy->got[1].samples - 3250.0f) < 1.0f);
        // the Max polyphony limiter does not run on a Mono channel
        rig.song.channels[0].maxPolyphony = 1;
        rig.state = RackState::build (rig.song, rig.state.get(), 48000.0, rig.f);
        rig.spy->got.clear();
        rig.play (60);
        rig.play (62);
        rig.play (62, false);
        rig.play (60, false);
        CHECK (rig.spy->trace() == "n60 s62 s60 f60 ");
    }
    {
        // Porta: the new note starts at the last note's pitch (its cents hold the offset) and glides in
        mono168l::Rig rig ([] (ChannelDef& c) { setChannelPortamento (c, true, 0.5f); });
        rig.play (60);
        rig.play (60, false);
        rig.play (67);
        CHECK (rig.spy->trace() == "n60 f60 n67 ");
        const auto& g = rig.spy->got[2];
        CHECK (std::abs (g.cents + 700.0f) < 1e-3f && std::abs (g.glideFrom + 700.0f) < 1e-3f);
        // 0.5 beat → n 96 → 48 ticks → 49/96 beat at 120 BPM = 12250 samples
        CHECK (std::abs (g.glideSamples - 12250.0f) < 1.0f);
        CHECK (rig.spy->got[0].glideSamples == 0.0f); // (the first note: nothing to glide from)
        CHECK (HostSlide::pendingGlide().samples == 0.0f); // (cleared after the note on)
    }
}

// Every generator engine plays a Mono channel legato and a Porta channel's glide (needs FL's data: the FL engines'
// factory states). LYNX_MONO_AUDIT=1 prints the table.
TEST (mono168l_every_engine_plays_mono_legato_and_porta)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto print = std::getenv ("LYNX_MONO_AUDIT") != nullptr;
    // (the same engines whose pitch the autocorrelation can't read as issue14's audit; Plucked! reads its pitch only
    // when a voice starts, so FL can't glide it either)
    const std::set<std::string> unreadable { "FL Slayer", "Fruity Granulizer", "Wave Traveller", "Drumaxx", "Fruit Kick", "BassDrum", "Drumpad", "Sakura", "Ogun" };
    const std::set<std::string> noSlide { "Plucked!" };
    // their sound follows the voice's own timeline (BeepMap scans its image left to right, a Transistor Bass preset runs
    // its own sequencer): a pitch read at another moment of the voice can't be compared with the reference
    const std::set<std::string> ownTimeline { "BeepMap", "Transistor Bass" };
    const auto fl = natives142::flAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        const auto made = natives::makeChannel (e.kind, fl);
        if (! made)
            continue;
        const auto& c = *made;
        // its voices take the porta glide at note on (HostSlide::anchor) when it says it follows the channel porta
        {
            Song s;
            s.mixer = Song::defaultMixer (2);
            s.channels.push_back (c);
            s.patterns.emplace_back();
            auto samples = [] (const std::string& path) -> std::shared_ptr<const SampleData> {
                auto p = path;
                std::replace (p.begin(), p.end(), '\\', '/');
                return tools::readWav (existingPathIgnoringCase (p));
            };
            auto state = RackState::build (s, nullptr, 48000.0, samples);
            auto& in = *state->instruments[0];
            in.prepare (48000.0);
            HostSlide::pendingGlide() = { -700.0f, 1000.0f, false };
            in.setNextNoteExpression (0.0f, -700.0f, false);
            in.noteOn (60, 0.8f);
            const auto taken = HostSlide::pendingGlide().taken;
            HostSlide::pendingGlide() = {};
            // an engine that refuses the note (e.g. a Granulizer whose sample is not on this machine) starts no voice
            // and so takes no glide (round 170i: it no longer writes the glide into voice −1)
            bool refused = false;
            if (! taken && in.followsChannelPorta())
            {
                std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
                in.render (l.data(), r.data(), 2048, 1.0f, 1.0f);
                refused = std::all_of (l.begin(), l.end(), [] (float x) { return x == 0.0f; })
                          && std::all_of (r.begin(), r.end(), [] (float x) { return x == 0.0f; });
            }
            if (refused)
                std::printf ("  porta: %s refuses the note here (no voice, no glide)\n", e.name);
            else if (taken != in.followsChannelPorta())
                std::printf ("  porta: %s %s the glide at note on\n", e.name, taken ? "takes" : "does not take");
            CHECK (refused || taken == in.followsChannelPorta());
        }
        if (unreadable.count (std::string (e.name)) != 0 || ownTimeline.count (std::string (e.name)) != 0)
            continue;
        Note low;
        low.key = 60;
        low.velocity = 0.8f;
        Note high = low;
        high.key = 67;
        // Mono: C5 for 2 beats, G5 from beat 1 to 3 (120 BPM): the voice glides to G5 at 0.5 s and stays there
        auto mono = c;
        setChannelMono (mono, true);
        low.length = ppq * 2;
        high.length = ppq * 2;
        // the references, as issue14's audit: C5 and G5 held 8 beats, read at 1 s (the engine's own timbre at each pitch)
        auto held = low;
        held.length = ppq * 8;
        const auto base = issue14::pitchAt (issue14::renderPattern (c, { held }, 0.0f, 1.6), 1.0);
        held.key = 67;
        const auto ref = issue14::pitchAt (issue14::renderPattern (c, { held }, 0.0f, 1.6), 1.0);
        high.start = ppq;
        const auto legato = issue14::renderPattern (mono, { low, high }, 0.0f, 1.6);
        const auto before = issue14::pitchAt (legato, 0.3), after = issue14::pitchAt (legato, 1.1);
        // Porta: C5 for half a beat, G5 a beat later with a Slide of 1 beat (FL: 97/96 beat ≈ 0.505 s at 120 BPM)
        auto porta = c;
        setChannelPortamento (porta, true, 1.0f);
        low.length = ppq / 2;
        high.length = ppq * 3;
        const auto glide = issue14::renderPattern (porta, { low, high }, 0.0f, 1.6);
        const auto mid = issue14::pitchAt (glide, 0.6), end = issue14::pitchAt (glide, 1.2);
        if (print)
            std::printf ("  %-28s ref %7.1f Hz  mono %+6.0f → %+6.0f  porta mid %+6.0f end %+6.0f cents\n", e.name, ref,
                         issue14::cents (ref, before), issue14::cents (ref, after), issue14::cents (ref, mid), issue14::cents (ref, end));
        if (noSlide.count (std::string (e.name)) != 0 || ref <= 0.0)
            continue;
        // within 40 cents of G5 + `cents`, or (an engine whose key doesn't move its pitch the plain way) of C5 + 700 + `cents`
        const auto near = [ref, base] (double hz, double cents) {
            return std::abs (issue14::cents (ref, hz) - cents) < 40.0 || std::abs (issue14::cents (base, hz) - 700.0 - cents) < 40.0;
        };
        const auto monoOk = near (before, -700.0) && near (after, 0.0);
        // (mid-glide: the window at 0.6…0.685 s is a quarter of the way into the glide that started at 0.5 s)
        const auto midCents = issue14::cents (ref, mid);
        const auto midBase = issue14::cents (base, mid) - 700.0;
        const auto portaOk = ((midCents > -620.0 && midCents < -250.0) || (midBase > -620.0 && midBase < -250.0)) && near (end, 0.0);
        if (! monoOk || ! portaOk)
            std::printf ("  mono/porta: %s (mono %+.0f → %+.0f, porta mid %+.0f end %+.0f cents)\n", e.name, issue14::cents (ref, before),
                         issue14::cents (ref, after), midCents, issue14::cents (ref, end));
        CHECK (monoOk);
        CHECK (portaOk);
    }
}
