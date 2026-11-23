// Round 190qh (0.1.32 track, research190qa R5 "Mixer QoL"): D2-MX9 FL's rule for a switched-off effect slot in the delay
// compensation (REA 190qc: its latency leaves the PDC at once; a muted track keeps its latency) with a click-free move of
// the plan while the audio runs; D2-MX1 un-solo / un-mute all and restore; D2-MX8 input monitoring on / off; D2-MX12 a
// slot LED on several selected inserts.

namespace mixerqol190qh
{
using namespace lynx;

// the largest step between two samples in [from, to)
float maxStep (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float m = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < to && i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

std::vector<float> sine (int frames, double hz, double rate, float amplitude)
{
    std::vector<float> x ((std::size_t) frames);
    for (int i = 0; i < frames; ++i)
        x[(std::size_t) i] = amplitude * (float) std::sin (2.0 * 3.14159265358979323846 * hz * i / rate);
    return x;
}

// runs `in` through `line` (in place) in blocks of `block` from `at`, appending to `out`
void run (DelayLine& line, const std::vector<float>& in, std::size_t at, std::size_t count, std::vector<float>& out, int block = 256)
{
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    for (std::size_t done = 0; done < count;)
    {
        const auto n = std::min<std::size_t> ((std::size_t) block, count - done);
        for (std::size_t i = 0; i < n; ++i)
            l[i] = r[i] = in[at + done + i];
        line.process (l.data(), r.data(), (int) n);
        out.insert (out.end(), l.begin(), l.begin() + (std::ptrdiff_t) n);
        done += n;
    }
}

// pass-through "line" for an edge with no delay
void runDirect (const std::vector<float>& in, std::size_t at, std::size_t count, std::vector<float>& out)
{
    out.insert (out.end(), in.begin() + (std::ptrdiff_t) at, in.begin() + (std::ptrdiff_t) (at + count));
}

// the same samples as `in` delayed by `delay` over [from, to) of `out` (`out` starts at input sample 0)
bool delayedBy (const std::vector<float>& out, const std::vector<float>& in, int delay, std::size_t from, std::size_t to)
{
    for (auto i = from; i < to && i < out.size(); ++i)
        if (out[i] != (i >= (std::size_t) delay ? in[i - (std::size_t) delay] : 0.0f))
            return false;
    return true;
}

// a sampler channel playing `sample` into insert `insert`
ChannelDef sampler (const std::string& sample, int insert)
{
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.samplePath = sample;
    c.mixerTrack = insert;
    return c;
}

EffectSlot latent (int samples)
{
    EffectSlot fx;
    fx.kind = EffectKind::plugin;
    fx.plugin.id = "latent" + std::to_string (samples);
    return fx;
}

// "impulse" (64 samples, 1 at 0) and "sine" (a 100 Hz sine at 0.4, 4 s); "latent<N>" = pdc190hq's N-sample delay
RackState::Factories factories()
{
    RackState::Factories f;
    f.movePdcLines = true; // (as the live engine's builds)
    f.samples = [] (const std::string& name) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        if (name == "sine")
            d->left = sine (4 * 48000, 100.0, 48000.0, 0.4f);
        else
        {
            d->left.assign (64, 0.0f);
            d->left[0] = 1.0f;
        }
        return std::shared_ptr<const SampleData> (d);
    };
    f.pluginEffects = [] (const PluginRef& ref, double) -> std::shared_ptr<Effect> {
        return std::make_shared<pdc190hq::Latent> (std::stoi (ref.id.substr (6)));
    };
    return f;
}

// two channels of `sample` into inserts 1 and 2 (both to the Master), insert 1 with a `latency`-sample slot
Song twoTracks (const std::string& sample, int latency)
{
    Song s;
    s.mixer = Song::defaultMixer (2);
    s.channels.push_back (sampler (sample, 1));
    s.channels.push_back (sampler (sample, 2));
    s.mixer[1].effects.push_back (latent (latency));
    s.patterns.emplace_back();
    uids::normalize (s); // (as every published song: the PDC lines know their edges by it)
    return s;
}

struct Session
{
    RackRenderer renderer;
    std::unique_ptr<const RackState> state;
    std::vector<float> out;
    explicit Session (const Song& s) : state (RackState::build (s, nullptr, 48000.0, factories())) { renderer.prepare (48000.0, 256); }

    void notes (const Song& s, LiveEventQueue& live)
    {
        for (int c = 0; c < (int) s.channels.size(); ++c)
        {
            NoteEvent on;
            on.channel = c;
            on.key = 60;
            on.velocity = 1.0f;
            live.push (on);
        }
    }
    // `blocks` blocks of 256, the notes at the first one when `play`
    void render (int blocks, LiveEventQueue* live = nullptr)
    {
        std::vector<float> l (256), r (256);
        for (int b = 0; b < blocks; ++b)
        {
            renderer.process (state.get(), false, 0.0, 256, l.data(), r.data(), b == 0 ? live : nullptr);
            out.insert (out.end(), l.begin(), l.end());
        }
    }
    // the song as it is after a live edit: built from the snapshot that plays (as AudioEngine::publishSong)
    std::unique_ptr<const RackState> keep;
    void edit (const Song& s)
    {
        auto next = RackState::build (s, state.get(), 48000.0, factories());
        keep = std::move (state); // (the renderer may still hold the old one until its next block)
        state = std::move (next);
    }
};
} // namespace mixerqol190qh

TEST (mixerqol190qh_fade_length_and_bound)
{
    using namespace lynx;
    // N = rate / (100 π): the crossfade's added step 2 / N stays under a full-scale 100 Hz sine's steepest step
    CHECK (switchFadeSamples (44100.0) == 141 && switchFadeSamples (48000.0) == 153 && switchFadeSamples (96000.0) == 306);
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        CHECK (2.0 / switchFadeSamples (rate) <= 2.0 * 3.14159265358979323846 * 100.0 / rate + 1.0e-12);
    CHECK (switchFadeSamples (0.0) > 0);
}

TEST (mixerqol190qh_delay_line_moves_without_a_click)
{
    using namespace lynx;
    using namespace mixerqol190qh;
    const auto fade = switchFadeSamples (48000.0);
    const auto in = sine (48000, 100.0, 48000.0, 1.0f); // full scale: the worst step a move can add
    const auto natural = maxStep (in, 1, in.size());
    const auto bound = natural + 2.0f / (float) fade + 1.0e-5f;
    const std::size_t at = 8192; // the edit's sample

    // the old way, for scale: a new line of another length starts from silence (a cut of the whole signal)
    {
        auto old = std::make_shared<DelayLine> (300);
        std::vector<float> out;
        run (*old, in, 0, at, out);
        DelayLine fresh (100);
        run (fresh, in, at, 4096, out);
        CHECK (maxStep (out, 1, out.size()) > 0.25f);
    }

    for (const auto [from, to] : { std::pair { 300, 100 }, { 100, 600 }, { 300, 0 }, { 0, 300 }, { 37, 38 }, { 2000, 1 } })
    {
        std::shared_ptr<DelayLine> old = from > 0 ? std::make_shared<DelayLine> (from) : nullptr;
        std::vector<float> out;
        if (old != nullptr)
            run (*old, in, 0, at, out);
        else
            runDirect (in, 0, at, out);
        DelayLine moved (to, old, from, fade);
        CHECK (! moved.settled());
        run (moved, in, at, 16384, out);
        const auto step = maxStep (out, (std::size_t) std::max (from, to) + 2, out.size());
        CHECK (step <= bound);
        std::printf ("  move %d -> %d: largest step %.5f, bound %.5f (full-scale 100 Hz sine %.5f)\n", from, to, step, bound, natural);
        // once moved (a longer delay waits until the line holds it): exactly the input, `to` samples late
        const auto done = at + (std::size_t) std::max (0, to - from) + (std::size_t) fade;
        CHECK (delayedBy (out, in, to, done, out.size()));
        CHECK (moved.settled());
        CHECK (moved.passesThrough() == (to == 0));
        // the line before can go once the audio took over
        moved.dropPrevious();
    }

    // mixInto (a route into a mix) with a level, and a silent source while it moves: the same rule
    {
        auto old = std::make_shared<DelayLine> (300);
        std::vector<float> out (at + 8192, 0.0f);
        std::vector<float> dummy (256);
        for (std::size_t b = 0; b < at; b += 256)
        {
            std::fill (dummy.begin(), dummy.end(), 0.0f);
            old->mixInto (in.data() + b, in.data() + b, 0.5f, out.data() + b, dummy.data(), 256);
        }
        DelayLine moved (120, old, 300, fade);
        for (std::size_t b = at; b < at + 8192; b += 256)
        {
            std::fill (dummy.begin(), dummy.end(), 0.0f);
            moved.mixInto (in.data() + b, in.data() + b, 0.5f, out.data() + b, dummy.data(), 256);
        }
        CHECK (maxStep (out, 302, out.size()) <= 0.5f * natural + 1.0f / (float) fade + 1.0e-5f);
        for (std::size_t i = at + (std::size_t) fade; i < out.size(); ++i)
            if (out[i] != in[i - 120] * 0.5f)
            {
                CHECK (false);
                break;
            }
        // a source gone silent: the line drains, then does nothing
        DelayLine drain (0, std::make_shared<DelayLine> (50), 50, fade);
        std::vector<float> l (256, 0.0f), r (256, 0.0f);
        drain.mixInto (nullptr, nullptr, 1.0f, l.data(), r.data(), 256);
        CHECK (drain.settled() && drain.silent());
    }

    // a line made the first way is unchanged: in → out `delay` later, bit for bit
    {
        DelayLine plain (333);
        std::vector<float> out;
        run (plain, in, 0, 8192, out, 100);
        CHECK (delayedBy (out, in, 333, 0, out.size()) && plain.settled() && ! plain.passesThrough());
    }
}

TEST (mixerqol190qh_off_slot_leaves_the_pdc_fl_exact)
{
    using namespace lynx;
    using namespace mixerqol190qh;
    // FL (REA 190qc, FUN_012b6960 / FUN_010ff3b0): an off slot's latency is 0 in the plan, so the other paths stop waiting
    auto s = twoTracks ("impulse", 200);
    auto on = RackState::build (s, nullptr, 48000.0, factories());
    CHECK (on->pdc.total == 200 && on->pdc.sendDelay[2][0] == 200 && on->pdc.outLat[1] == 200);
    s.mixer[1].effects[0].enabled = false;
    auto off = RackState::build (s, nullptr, 48000.0, factories());
    CHECK (off->pdc.total == 0 && off->pdc.sendDelay[2][0] == 0 && off->pdc.outLat[1] == 0 && ! off->pdc.any());
    CHECK (insertEffectsLatency (off->pdc, off->song, 1, 48000.0) == 0);
    // a muted track keeps its latency (FL: 7.82 ms with the track's strip LED off)
    s.mixer[1].effects[0].enabled = true;
    s.mixer[1].muted = true;
    auto muted = RackState::build (s, nullptr, 48000.0, factories());
    CHECK (muted->pdc.total == 200 && muted->pdc.sendDelay[2][0] == 200);
    // FL's slot solo (Ctrl+click the LED) leaves only that slot's latency
    s.mixer[1].muted = false;
    s.mixer[1].effects.push_back (latent (50));
    mixerops::soloSlot (s, 1, 1);
    auto soloed = RackState::build (s, nullptr, 48000.0, factories());
    CHECK (soloed->pdc.total == 50);
}

TEST (mixerqol190qh_live_switch_lines_up_and_does_not_click)
{
    using namespace lynx;
    using namespace mixerqol190qh;
    // the impulses: with the slot on both arrive 200 samples late; the slot switched off while the audio runs, the next
    // hits arrive together at once (FL), with the slot back on, 200 late together again
    {
        auto s = twoTracks ("impulse", 200);
        Session play (s);
        LiveEventQueue live;
        play.notes (s, live);
        play.render (8, &live);
        auto hits = [] (const std::vector<float>& out, std::size_t from) {
            std::vector<std::size_t> at;
            for (auto i = from; i < out.size(); ++i)
                if (std::abs (out[i]) > 0.05f)
                    at.push_back (i - from);
            return at;
        };
        const auto first = hits (play.out, 0);
        CHECK (first.size() == 1 && ! first.empty() && first.front() == 200);
        s.mixer[1].effects[0].enabled = false;
        play.edit (s);
        play.render (8); // (the move: switchFade samples)
        CHECK (play.state->pdc.total == 0);
        const auto mark = play.out.size();
        LiveEventQueue again;
        play.notes (s, again);
        play.render (8, &again);
        const auto second = hits (play.out, mark);
        CHECK (second.size() == 1 && ! second.empty() && second.front() == 0);
        s.mixer[1].effects[0].enabled = true;
        play.edit (s);
        play.render (8);
        const auto mark2 = play.out.size();
        LiveEventQueue third;
        play.notes (s, third);
        play.render (8, &third);
        const auto back = hits (play.out, mark2);
        CHECK (back.size() == 1 && ! back.empty() && back.front() == 200);
    }

    // a held sine on both tracks: switching the slot off and on while it plays adds no step beyond the crossfade's bound
    // (Pdc.h: |b − a| / N per crossfaded path; two paths here) — before 190qh the other track's route line restarted
    // from silence and the slot stepped from its output to its input
    {
        auto s = twoTracks ("sine", 400);
        Session play (s);
        LiveEventQueue live;
        play.notes (s, live);
        play.render (40, &live);
        const auto steadyFrom = (std::size_t) 2000, steadyTo = play.out.size();
        const auto natural = maxStep (play.out, steadyFrom, steadyTo);
        float peak = 0.0f;
        for (auto i = steadyFrom; i < steadyTo; ++i)
            peak = std::max (peak, std::abs (play.out[i]));
        CHECK (peak > 0.1f);
        const auto fade = (float) switchFadeSamples (48000.0);
        const auto bound = natural + 2.0f * peak / fade * 1.05f + 1.0e-4f;
        s.mixer[1].effects[0].enabled = false;
        const auto offAt = play.out.size();
        play.edit (s);
        play.render (40);
        s.mixer[1].effects[0].enabled = true;
        play.edit (s);
        play.render (40);
        const auto step = maxStep (play.out, offAt - 1, play.out.size());
        CHECK (step <= bound);
        CHECK (bound < 0.5f * peak); // (a cut or a jump of the 400-sample offset would be about the peak)
        std::printf ("  live switch off / on: largest step %.5f, bound %.5f (steady sine %.5f, peak %.3f)\n", step, bound, natural, peak);
    }
}

TEST (mixerqol190qh_unsolo_unmute_restore)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.mixer = Song::defaultMixer (6);
    s.mixer[2].solo = true;
    s.mixer[4].solo = true;
    s.mixer[4].soloSources = false; // (FL's plain solo, kept by the restore)
    mixerops::SwitchMemory solos, mutes;
    CHECK (mixerops::anySolo (s) && ! mixerops::canRestore (s, solos));
    CHECK (mixerops::unsoloAllOrRestore (s, solos) == mixerops::SwitchRestore::cleared);
    CHECK (! mixerops::anySolo (s) && mixerops::canRestore (s, solos));
    CHECK (mixerops::unsoloAllOrRestore (s, solos) == mixerops::SwitchRestore::restored);
    CHECK (s.mixer[2].solo && s.mixer[4].solo && s.mixer[2].soloSources && ! s.mixer[4].soloSources && ! s.mixer[3].solo);
    CHECK (! mixerops::canRestore (s, solos)); // (used up: the next press clears again)
    // a renamed insert is not brought back blindly
    mixerops::unsoloAllOrRestore (s, solos);
    s.mixer[4].name = "Other";
    CHECK (mixerops::unsoloAllOrRestore (s, solos) == mixerops::SwitchRestore::restored && s.mixer[2].solo && ! s.mixer[4].solo);
    CHECK (mixerops::unsoloAllOrRestore (s, solos) == mixerops::SwitchRestore::cleared);
    CHECK (mixerops::unsoloAllOrRestore (s, solos) == mixerops::SwitchRestore::restored);
    s.mixer[2].solo = false;
    CHECK (mixerops::unsoloAllOrRestore (s, solos) == mixerops::SwitchRestore::nothing);

    // mutes: the Master too; a locked mute stays (FL's Unmute All) and doesn't count
    s.mixer[0].muted = true;
    s.mixer[3].muted = true;
    s.mixer[5].muted = true;
    s.mixer[5].lockState = true;
    CHECK (mixerops::anyMute (s));
    CHECK (mixerops::unmuteAllOrRestore (s, mutes) == mixerops::SwitchRestore::cleared);
    CHECK (! s.mixer[0].muted && ! s.mixer[3].muted && s.mixer[5].muted && ! mixerops::anyMute (s));
    CHECK (mixerops::unmuteAllOrRestore (s, mutes) == mixerops::SwitchRestore::restored);
    CHECK (s.mixer[0].muted && s.mixer[3].muted && s.mixer[5].muted && ! s.mixer[1].muted);
}

TEST (mixerqol190qh_input_monitoring_and_slot_led_on_a_selection)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.mixer = Song::defaultMixer (4);
    // an insert: off / when armed / auto → On; On → what it had (When armed when unknown)
    s.mixer[2].monitor = InputMonitor::automatic;
    CHECK (mixerops::toggledMonitor (s, 2, InputMonitor::whenArmed) == InputMonitor::on);
    s.mixer[2].monitor = InputMonitor::on;
    CHECK (mixerops::toggledMonitor (s, 2, InputMonitor::automatic) == InputMonitor::automatic);
    CHECK (mixerops::toggledMonitor (s, 2, InputMonitor::on) == InputMonitor::whenArmed);
    s.mixer[2].monitor = InputMonitor::off;
    CHECK (mixerops::toggledMonitor (s, 2, InputMonitor::whenArmed) == InputMonitor::on);
    // the Master: on / off only
    s.mixer[0].monitor = InputMonitor::on;
    CHECK (mixerops::toggledMonitor (s, 0, InputMonitor::whenArmed) == InputMonitor::off);
    s.mixer[0].monitor = InputMonitor::off;
    CHECK (mixerops::toggledMonitor (s, 0, InputMonitor::whenArmed) == InputMonitor::on);
    CHECK (mixerops::toggledMonitor (s, 9, InputMonitor::on) == InputMonitor::off);

    // slot 1 on inserts 1-3 (insert 2 without a slot 1, insert 3's locked)
    for (const auto t : { 1, 2, 3 })
    {
        s.mixer[(std::size_t) t].effects.push_back (makeEffectSlot (EffectKind::eq));
        if (t != 2)
            s.mixer[(std::size_t) t].effects.push_back (makeEffectSlot (EffectKind::eq));
    }
    s.mixer[3].effects[1].lockState = true;
    mixerops::setSlotEnabledOn (s, { 1, 2, 3, 99 }, 1, false);
    CHECK (! s.mixer[1].effects[1].enabled && s.mixer[3].effects[1].enabled && s.mixer[1].effects[0].enabled && s.mixer[2].effects[0].enabled);
}
