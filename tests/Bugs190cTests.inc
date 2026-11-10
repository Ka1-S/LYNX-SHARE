// Round 190c: issue #134 (changing the output device with a project open crashed LYNX). The engine's side is in
// --devicechangetest / --selftest "device_change" (engine/DeviceChangeTest.cpp); these are the core's parts.

namespace bugs190c
{
struct Probe
{
    int* alive;
    explicit Probe (int* a) : alive (a) { ++*alive; }
    ~Probe() { --*alive; }
};

// a hosted plugin stand-in that remembers the rate it was made for (hosted plugins are prepared when they are made)
struct RatePlugin final : lynx::Instrument
{
    explicit RatePlugin (double r) : madeFor (r) {}
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    double madeFor;
};
struct RatePluginFx final : lynx::Effect
{
    explicit RatePluginFx (double r) : madeFor (r) {}
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const lynx::EffectParams&, double) noexcept override {}
    double madeFor;
};
} // namespace bugs190c

TEST (issue134_an_unread_snapshot_goes_to_the_owner_like_a_returned_one)
{
    // With no audio callback running (the device closed, or between two devices) nothing takes the published rack, so
    // the next publish replaces it. It was deleted there and then — inside the engine's timer callback when the timer
    // published, hosted plugins and all (issue #36's crash, by another door). It is handed to `dispose` instead.
    int alive = 0;
    std::vector<const bugs190c::Probe*> handed;
    {
        lynx::SnapshotExchange<bugs190c::Probe> x;
        x.dispose = [&handed] (const bugs190c::Probe* p) { handed.push_back (p); };
        x.publish (std::make_unique<bugs190c::Probe> (&alive));
        x.publish (std::make_unique<bugs190c::Probe> (&alive)); // the first was never read
        CHECK (handed.size() == 1 && alive == 2);                // handed over, not freed inside publish
        x.publish (std::make_unique<bugs190c::Probe> (&alive));
        CHECK (handed.size() == 2 && alive == 3);
        const auto* active = x.acquire();
        CHECK (active != nullptr && handed.size() == 2); // the newest one is what the audio thread gets
        for (const auto* p : handed)
            delete p;
        CHECK (alive == 1);
    }
    CHECK (alive == 0);

    // without an owner it is freed at once, as before
    {
        lynx::SnapshotExchange<bugs190c::Probe> x;
        x.publish (std::make_unique<bugs190c::Probe> (&alive));
        x.publish (std::make_unique<bugs190c::Probe> (&alive));
        CHECK (alive == 1);
    }
    CHECK (alive == 0);
}

TEST (issue134_the_master_limiter_stays_inside_its_delay_line_above_192_khz)
{
    // The default project's Master limiter keeps 1.5 ms of look-ahead in a delay line sized for 192 kHz. A device
    // above that (352.8 / 384 kHz USB converters) made the look-ahead longer than the line: it wrote past its end.
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::limiter);
    slot.params = { 11.7f / 12.0f, 0.0f, 0.5f }; // (Song::makeDefault's)
    for (const auto rate : { 44100.0, 192000.0, 352800.0, 384000.0, 768000.0 })
    {
        auto fx = lynx::makeEffect (slot);
        fx->prepare (rate);
        const auto latency = fx->latencySamples();
        CHECK (latency >= 1 && latency <= (int) (0.0015 * 192000.0) + 4); // what the line holds
        if (rate <= 192000.0)
            CHECK (latency == (int) std::lround (0.0015 * rate)); // unchanged where it fitted

        // an impulse comes out exactly `latency` samples later, at its level (under the ceiling), and nothing else
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        l[100] = r[100] = 0.5f;
        for (int at = 0; at < 4096; at += 512)
            fx->process (l.data() + at, r.data() + at, 512, slot.params, 120.0);
        auto others = 0.0f;
        for (int i = 0; i < 4096; ++i)
            if (i != 100 + latency)
                others = std::max (others, std::abs (l[(std::size_t) i]));
        CHECK (std::abs (l[(std::size_t) (100 + latency)] - 0.5f) < 1.0e-3f && others < 1.0e-6f);
    }
}

TEST (issue134_every_effect_and_basic_instrument_runs_at_any_device_rate)
{
    // What a device change hands the units: 8 kHz (a Bluetooth hands-free headset) to 768 kHz. Each effect is
    // prepared for the rate (as RackRenderer::prepare does when the device starts) and processes noise; the same unit
    // is then taken to the next rate, as a running project's units are. Nothing may be read or written outside a
    // buffer (the sanitizer builds of this test, and the engine's --devicechangetest, are what see that: the Limiter and
    // FL's Pitch Shifter did above 192 kHz), and from 44.1 kHz up the output must be finite. Below that four effects
    // still give non-finite samples (Vintage Chorus, Fruity Multiband Compressor, Aether, Transmitter: muted by the
    // renderer's guard; left open by round 190c), so those rates only have to run.
    const double rates[] = { 48000.0, 8000.0, 384000.0, 16000.0, 192000.0, 11025.0, 768000.0, 22050.0, 352800.0, 44100.0 };
    std::vector<float> l (512), r (512);
    std::uint32_t seed = 134;
    auto noise = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return ((float) (seed >> 8) / 8388608.0f - 1.0f) * 0.5f;
    };
    for (int k = 0; k <= (int) lynx::lastEffectKind; ++k)
    {
        const auto kind = (lynx::EffectKind) k;
        if (kind == lynx::EffectKind::plugin || kind == lynx::EffectKind::patcher)
            continue;
        const auto slot = lynx::makeEffectSlot (kind);
        auto fx = lynx::makeEffect (slot);
        if (fx == nullptr)
            continue;
        auto finite = true;
        for (const auto rate : rates)
        {
            fx->prepare (rate);
            for (int block = 0; block < 6; ++block)
            {
                for (std::size_t i = 0; i < l.size(); ++i)
                {
                    l[i] = noise();
                    r[i] = noise();
                }
                fx->process (l.data(), r.data(), (int) l.size(), slot.params, 140.0);
                if (rate >= 44100.0)
                    for (std::size_t i = 0; i < l.size(); ++i)
                        finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            }
        }
        if (! finite)
            std::printf ("  %s: non-finite output\n", lynx::effectInfo (kind).name);
        CHECK (finite);
    }
    for (const auto kind : { lynx::InstrumentKind::kick, lynx::InstrumentKind::snare, lynx::InstrumentKind::hat, lynx::InstrumentKind::clap,
                             lynx::InstrumentKind::synth, lynx::InstrumentKind::threeosc, lynx::InstrumentKind::plucked })
    {
        auto instrument = lynx::makeInstrument (kind);
        lynx::ChannelDef def;
        def.instrument = kind;
        auto finite = true;
        for (const auto rate : rates)
        {
            instrument->prepare (rate);
            instrument->configure (def, 140.0);
            instrument->noteOn (60, 0.8f);
            for (int block = 0; block < 6; ++block)
            {
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                instrument->render (l.data(), r.data(), (int) l.size(), 1.0f, 1.0f);
                for (std::size_t i = 0; i < l.size(); ++i)
                    finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            }
            instrument->noteOff (60);
        }
        CHECK (finite);
    }
}

TEST (issue134_a_patcher_with_a_hosted_plugin_is_built_again_for_a_new_device_rate)
{
    // Hosted plugins are prepared when they are made. On the rack they are made again when the device's rate changes;
    // inside a Patcher the old graph's running nodes were kept, so its plugins went on at the old rate.
    int instruments = 0, effects = 0;
    lynx::RackState::Factories f;
    f.pluginInstruments = [&] (const lynx::PluginRef&, double rate) -> std::shared_ptr<lynx::Instrument> {
        ++instruments;
        return std::make_shared<bugs190c::RatePlugin> (rate);
    };
    f.pluginEffects = [&] (const lynx::PluginRef&, double rate) -> std::shared_ptr<lynx::Effect> {
        ++effects;
        return std::make_shared<bugs190c::RatePluginFx> (rate);
    };

    lynx::Song song;
    song.mixer = lynx::Song::defaultMixer (2);
    song.patterns.emplace_back();
    // channel 0: a generator Patcher holding a hosted instrument; insert 1: an effect Patcher holding a hosted effect
    // and a native one; channel 1 / insert 2: Patchers of native units only
    lynx::PatchNode synth;
    synth.generator = true;
    synth.channel.instrument = lynx::InstrumentKind::plugin;
    synth.channel.plugin.id = "VST3-Synth";
    lynx::PatchNode nativeSynth;
    nativeSynth.generator = true;
    nativeSynth.channel.instrument = lynx::InstrumentKind::synth;
    lynx::ChannelDef hosting, native;
    hosting.instrument = native.instrument = lynx::InstrumentKind::patcher;
    hosting.mixerTrack = native.mixerTrack = 1;
    hosting.patcher.nodes = { synth };
    native.patcher.nodes = { nativeSynth };
    song.channels = { hosting, native };

    lynx::PatchNode hostedFx, nativeFx;
    hostedFx.effect = lynx::makeEffectSlot (lynx::EffectKind::plugin);
    hostedFx.effect.plugin.id = "VST3-Effect";
    nativeFx.effect = lynx::makeEffectSlot (lynx::EffectKind::delay);
    auto hostingFx = lynx::makeEffectSlot (lynx::EffectKind::patcher), nativeOnly = lynx::makeEffectSlot (lynx::EffectKind::patcher);
    hostingFx.patcher.nodes = { hostedFx, nativeFx };
    nativeOnly.patcher.nodes = { nativeFx };
    song.mixer[1].effects = { hostingFx };
    song.mixer[2].effects = { nativeOnly };
    song.uniqueInstances();

    auto a = lynx::RackState::build (song, nullptr, 48000.0, f);
    CHECK (instruments == 1 && effects == 1);
    auto madeFor = [] (const lynx::RackState& s) {
        const auto* gi = lynx::patcher::graphOf (s.instruments[0].get());
        const auto* ge = lynx::patcher::graphOf (s.effects[1][0].get());
        const auto* i = gi != nullptr ? dynamic_cast<const bugs190c::RatePlugin*> (gi->instrumentAt (0).get()) : nullptr;
        const auto* e = ge != nullptr ? dynamic_cast<const bugs190c::RatePluginFx*> (ge->effectAt (0).get()) : nullptr;
        return std::pair<double, double> { i != nullptr ? i->madeFor : 0.0, e != nullptr ? e->madeFor : 0.0 };
    };
    CHECK (madeFor (*a) == (std::pair<double, double> { 48000.0, 48000.0 }));
    const auto* nativeGraph = lynx::patcher::graphOf (a->instruments[1].get());
    const auto nativeNode = nativeGraph != nullptr ? nativeGraph->instrumentAt (0) : nullptr;
    const auto* nativeFxGraph = lynx::patcher::graphOf (a->effects[2][0].get());
    const auto nativeFxNode = nativeFxGraph != nullptr ? nativeFxGraph->effectAt (0) : nullptr;
    CHECK (nativeNode != nullptr && nativeFxNode != nullptr);

    // an edit at the same rate keeps every running node (nothing is made again)
    song.bpm = 133.0;
    auto b = lynx::RackState::build (song, a.get(), 48000.0, f);
    CHECK (instruments == 1 && effects == 1 && madeFor (*b) == (std::pair<double, double> { 48000.0, 48000.0 }));

    // the device changes to 44.1 kHz: the Patchers' plugins are made for it
    auto c = lynx::RackState::build (song, b.get(), 44100.0, f);
    CHECK (instruments == 2 && effects == 2);
    CHECK (madeFor (*c) == (std::pair<double, double> { 44100.0, 44100.0 }));
    // ... and a Patcher of LYNX's own units keeps its running nodes, as before (they follow the rate themselves)
    const auto* keptGraph = lynx::patcher::graphOf (c->instruments[1].get());
    const auto* keptFxGraph = lynx::patcher::graphOf (c->effects[2][0].get());
    CHECK (keptGraph != nullptr && keptGraph->instrumentAt (0) == nativeNode);
    CHECK (keptFxGraph != nullptr && keptFxGraph->effectAt (0) == nativeFxNode);
}

// Issue #138 (a short export from the very start of the Playlist came out quiet or silent). The export path is checked
// end to end by --shortexporttest / --selftest "short_export" (engine/ShortExportTest.cpp); this is the one cause found
// in the core.
TEST (issue138_an_export_waits_for_a_soundfont_that_is_still_loading)
{
    // The font loads on a thread of its own. The export's engine builds and prepares its instruments before it switches
    // to offline rendering, so the instrument's wait in prepare() did not run: an export that started before the font
    // was in (a command-line export, a font just chosen) dropped the notes at the start of the song.
    const auto path = (std::filesystem::temp_directory_path() / ("lynx-bugs190c-font-" + std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count()) + ".sf2")).string();
    {
        std::vector<lynx::sf::TestPreset> presets;
        for (int p = 0; p < 64; ++p) // (big enough to take the loader a moment)
            presets.push_back ({ "Preset " + std::to_string (p), 0, p });
        const auto bytes = lynx::sf::makeTestFont (presets, 4);
        std::ofstream out (path, std::ios::binary);
        out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    }
    auto channel = lynx::sf::makeChannel();
    channel.samplePath = path;

    lynx::setOfflineRendering (false); // as when the export's engine is made
    auto instrument = lynx::sf::makeInstrument (lynx::sf::FontLibrary::instance().get (path)); // the load starts
    instrument->prepare (44100.0);
    instrument->configure (channel, 120.0);
    lynx::setOfflineRendering (true);  // the render begins
    instrument->noteOn (48, 0.8f);     // a note on the first sample
    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    float peak = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        instrument->render (l.data(), r.data(), 512, 1.0f, 1.0f);
        for (const auto v : l)
            peak = std::max (peak, std::abs (v));
    }
    lynx::setOfflineRendering (false);
    CHECK (peak > 0.01f); // the note sounds: the render waited for the font
    instrument.reset();
    std::error_code ignored;
    std::filesystem::remove (path, ignored);
}
