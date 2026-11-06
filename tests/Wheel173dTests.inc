// Round 173d (wheel173d): a MIDI controller's pitch wheel, live or recorded, on FL's native generators.
// FL: the wheel moves the channel's pitch (the voice's Final.Pitch) and every synth follows it as it follows the PITCH
// knob (REA_rodada167a §1, REA_rodada164e); a generator's own "Bend range" leaves it unchanged (FL render, Sawer /
// PoiZone / GMS at their max range: bit-identical output); the native generators take no MIDI controller (their
// MIDIIn is abstract or gated, REA_rodada164e), so the mod wheel (CC 1) leaves them as they are.
// LYNX before: Instrument::pitchBend did nothing on these engines, so the wheel was lost.

namespace wheel173d
{
enum class Path { none, live, automation, liveCc1 };

// one held C4 on `channel` (Pattern mode, 120 BPM, 48 kHz, its pitch range `range`); the wheel to `wheel` (−1..1) at
// `at` seconds, live (a controller) or as a recorded pitch bend event in the pattern; `noteAt` = when the note starts
std::vector<float> render (const lynx::ChannelDef& channel, Path path, float wheel, int range, double at, double noteAt, double seconds)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (2);
    s.bpm = 120.0;
    auto c = channel;
    c.mixerTrack = 1;
    c.misc.pitchRange = range;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    const auto tickOf = [] (double sec) { return (std::int64_t) std::llround (sec * 2.0 * ppq); };
    Note held;
    held.key = 60;
    held.start = tickOf (noteAt);
    held.length = ppq * 16;
    held.velocity = 0.8f;
    s.patterns[0].notes = { held };
    s.patterns[0].lengthTicks = (std::int64_t) (ppq * 32);
    if (path == Path::automation)
    {
        PatternEvents lane;
        lane.target = { AutomationTargetKind::channelParam, 0, 0, midictl::pitchBendBase };
        lane.events = { { 0, 0.5f }, { tickOf (at), (wheel + 1.0f) * 0.5f } };
        s.patterns[0].events.push_back (lane);
    }
    s.mode = PlayMode::pattern;
    auto samples = [] (const std::string& path) -> std::shared_ptr<const SampleData> {
        auto p = path;
        std::replace (p.begin(), p.end(), '\\', '/');
        return tools::readWav (existingPathIgnoringCase (p));
    };
    const auto state = RackState::build (s, nullptr, 48000.0, samples);
    RackRenderer r;
    r.prepare (48000.0, 512);
    LiveEventQueue live;
    const auto n = (int) (seconds * 48000.0);
    const auto move = (int) (at * 48000.0);
    const auto tps = 120.0 / 60.0 * ppq / 48000.0;
    bool moved = false;
    std::vector<float> out;
    for (int done = 0; done < n; done += 512)
    {
        if (! moved && done >= move && (path == Path::live || path == Path::liveCc1))
        {
            moved = true;
            NoteEvent e;
            e.type = NoteEvent::Type::control;
            e.channel = 0;
            e.key = path == Path::live ? NoteEvent::controlPitchBend : 1;
            e.velocity = path == Path::live ? wheel : 1.0f;
            live.push (e);
        }
        std::vector<float> l (512), rr (512);
        r.process (state.get(), true, done * tps, 512, l.data(), rr.data(), &live, nullptr);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

// the synths FL measured following the channel's pitch at once (left167), less Toxic Biohazard (its own CC map)
const std::set<std::string>& synths()
{
    static const std::set<std::string> s { "3x Osc", "Sytrus", "Harmless", "MiniSynth", "Sawer", "SimSynth", "PoiZone", "GMS",
                                           "Harmor", "Fruity DX10", "FL Keys", "BooBass", "Morphine", "Kepler" };
    return s;
}
} // namespace wheel173d

// a +2 semitone wheel at a pitch range of 2 moves a held note by +200 cents, live and from a recorded pitch bend; a
// note started with the wheel already up sounds +200 too and stays there (no second jump at the next chunk); the wheel
// back to the centre brings it back
TEST (wheel173d_pitch_wheel_moves_fl_native_generators_by_the_channel_range)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto print = std::getenv ("LYNX_PITCH_AUDIT") != nullptr;
    const auto fl = natives142::flAccess();
    int checked = 0;
    for (const auto& e : natives::instrumentEngines())
    {
        if (wheel173d::synths().count (e.name) == 0)
            continue;
        const auto c = natives142::writtenWith (e.kind, fl);
        if (! c)
            continue;
        using wheel173d::Path;
        const auto still = wheel173d::render (*c, Path::none, 0.0f, 2, 0.75, 0.0, 2.0);
        const auto base = issue14::pitchAt (still, 0.5);
        const auto live = wheel173d::render (*c, Path::live, 1.0f, 2, 0.75, 0.0, 2.0);
        const auto rec = wheel173d::render (*c, Path::automation, 1.0f, 2, 0.75, 0.0, 2.0);
        const auto down = wheel173d::render (*c, Path::live, -0.5f, 2, 0.75, 0.0, 2.0); // −1 semitone
        const auto wide = wheel173d::render (*c, Path::live, 1.0f, 12, 0.75, 0.0, 2.0); // range 12: +1 octave
        // (the same move on the PITCH knob, the path FL measured: the wheel must land where the knob does)
        const auto knob = left167::renderLiveTweak (*c, 12.0f, 0.0f, 0.75, 2.0);
        const auto later = wheel173d::render (*c, Path::live, 1.0f, 2, 0.1, 0.5, 2.0); // the note starts bent
        const auto stillLater = wheel173d::render (*c, Path::none, 0.0f, 2, 0.1, 0.5, 2.0);
        const auto vLive = issue14::cents (base, issue14::pitchAt (live, 1.25));
        const auto vRec = issue14::cents (base, issue14::pitchAt (rec, 1.25));
        const auto vDown = issue14::cents (base, issue14::pitchAt (down, 1.25));
        const auto vWide = issue14::cents (base, issue14::pitchAt (wide, 1.25));
        const auto vKnob = issue14::cents (issue14::pitchAt (knob, 0.5), issue14::pitchAt (knob, 1.25));
        const auto ref = issue14::pitchAt (stillLater, 1.0);
        const auto vEarly = issue14::cents (ref, issue14::pitchAt (later, 1.0));
        const auto vLate = issue14::cents (issue14::pitchAt (stillLater, 1.75), issue14::pitchAt (later, 1.75));
        const auto before = issue14::cents (base, issue14::pitchAt (live, 0.5));
        if (print)
        {
            // (an octave played as a note, for an engine whose octave the estimate reads short)
            Note n60, n72;
            n60.key = 60, n72.key = 72, n60.length = n72.length = ppq * 4, n60.velocity = n72.velocity = 0.8f;
            const auto octave = issue14::cents (issue14::pitchAt (issue14::renderPattern (*c, { n60 }, 0.0f, 1.0), 0.5),
                                                issue14::pitchAt (issue14::renderPattern (*c, { n72 }, 0.0f, 1.0), 0.5));
            std::printf ("  %-16s key 72 vs 60: %+6.0f\n", e.name, octave);
        }
        if (print)
            std::printf ("  %-16s live %+6.0f rec %+6.0f down %+6.0f wide %+6.0f (knob %+6.0f) bentNote %+6.0f/%+6.0f before %+5.0f\n",
                         e.name, vLive, vRec, vDown, vWide, vKnob, vEarly, vLate, before);
        const auto near = [] (double v, double want) { return std::abs (v - want) < 30.0; };
        if (! (near (vLive, 200) && near (vRec, 200) && near (vDown, -100) && near (vWide, vKnob) && near (vEarly, 200)
               && near (vLate, 200) && near (before, 0)))
            std::printf ("  wheel: %s live %+.0f rec %+.0f down %+.0f wide %+.0f (knob %+.0f) bent note %+.0f/%+.0f before %+.0f\n",
                         e.name, vLive, vRec, vDown, vWide, vKnob, vEarly, vLate, before);
        CHECK (near (vLive, 200));
        CHECK (near (vRec, 200));
        CHECK (near (vDown, -100));
        CHECK (near (vWide, vKnob)); // (+1200 on all but Morphine: at a +12 channel pitch it reads about +800 by the wheel and by the
                                     // PITCH knob alike, while key 72 against 60 reads +1198, so that is Morphine's own response to a large
                                     // channel-pitch shift, not this path; the wheel only has to equal the knob)
        CHECK (near (vEarly, 200));
        CHECK (near (vLate, 200));
        CHECK (near (before, 0));
        ++checked;
    }
    CHECK (checked >= 10);
}

// FL's native generators take no MIDI controller (REA_rodada164e): the mod wheel leaves their output as it was
// (their own "Mod wheel" controls are on-screen parameters, not CC 1)
TEST (wheel173d_mod_wheel_leaves_fl_native_generators_as_fl_does)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        if (wheel173d::synths().count (e.name) == 0)
            continue;
        const auto c = natives142::writtenWith (e.kind, fl);
        if (! c)
            continue;
        using wheel173d::Path;
        const auto still = wheel173d::render (*c, Path::none, 0.0f, 2, 0.5, 0.0, 1.0);
        const auto cc = wheel173d::render (*c, Path::liveCc1, 0.0f, 2, 0.5, 0.0, 1.0);
        if (still != cc)
            std::printf ("  mod wheel: %s moved\n", e.name);
        CHECK (still == cc);
    }
}

// the default wheel: what the rack adds to the channel pitch (hosted plugins and the SoundFont player override it and
// bend by the raw MIDI message only)
TEST (wheel173d_native_default_keeps_the_wheel_for_the_channel_pitch)
{
    using namespace lynx;
    struct Native final : Instrument
    {
        void prepare (double) noexcept override {}
        void noteOn (int, float) noexcept override {}
        void noteOff (int) noexcept override {}
        void allNotesOff() noexcept override {}
        void render (float*, float*, int, float, float) noexcept override {}
    } native;
    native.pitchBend (2.0f);
    CHECK (native.wheelCents() == 200.0f);
    native.pitchBend (-0.5f);
    CHECK (native.wheelCents() == -50.0f);
    native.pitchBend (NAN);
    CHECK (native.wheelCents() == 0.0f);
}
