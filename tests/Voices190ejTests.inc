// Round 190ej (issue #158 follow-up): two overlapping notes of one key on one channel. FL 2026 gives every note its own
// voice and ends that voice at the note's end; LYNX's instruments ended every voice of the key at the first note's end.
// FL's /R renders (/mnt/e/lynx-work/voices190ej/, HANDOFF.md): FIFO (A 0-4 s, B 2-6 s) and LIFO (C 8-14 s, D 10-12 s)
// on one channel, key 60. Per voice (B sounds to 6 s, C to 14 s): the Sampler, 3x Osc, Sytrus, BeepMap, DX10, SimSynth,
// Ogun, DirectWave, Harmor, Harmless. One note per key, the newest holds it (B sounds to 6 s, the key goes quiet at D's
// end): Kepler, Kepler Exo, MiniSynth, PoiZone, Sawer, Toxic Biohazard, Morphine, GMS, BooBass, Sakura.

#include "core/ChannelMiscFx.h"
#include "core/RackOps.h"

namespace voices190ej
{
constexpr double rate = 48000.0;

struct Step
{
    double seconds;
    bool on;
    std::uint32_t id;
};

// `steps` (in time order) played on `in` at key 60, rendered for `seconds`; the left channel
inline std::vector<float> run (lynx::Instrument& in, const std::vector<Step>& steps, double seconds, int key = 60)
{
    std::vector<float> out, l (64), r (64);
    std::size_t next = 0;
    const auto total = (int) (seconds * rate);
    for (int done = 0; done < total; done += 64)
    {
        while (next < steps.size() && steps[next].seconds * rate <= (double) done)
        {
            const auto& s = steps[next++];
            if (s.on)
            {
                in.setNextNoteId (s.id);
                in.noteOn (key, 0.8f);
                in.setNextNoteId (0);
            }
            else
            {
                in.setNextNoteOffId (s.id);
                in.noteOff (key);
                in.setNextNoteOffId (0);
            }
        }
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        in.render (l.data(), r.data(), 64, 1.0f, 1.0f);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

inline double rms (const std::vector<float>& x, double from, double to)
{
    const auto a = (std::size_t) (from * rate), b = std::min (x.size(), (std::size_t) (to * rate));
    double sum = 0.0;
    for (auto i = a; i < b; ++i)
        sum += (double) x[i] * x[i];
    return b > a ? std::sqrt (sum / (double) (b - a)) : 0.0;
}

// FIFO: A (id 1) 0-0.6 s, B (id 2) 0.3-2.4 s; B alone after A's end. LIFO: C (id 3) 0-2.4 s, D (id 4) 0.3-0.6 s. The
// levels are taken 1-1.4 s after the first end, past the release tails (Sytrus' Default voices cancel each other while
// two sound, as in FL's render)
const std::vector<Step> fifo { { 0.0, true, 1 }, { 0.3, true, 2 }, { 0.6, false, 1 }, { 2.4, false, 2 } };
const std::vector<Step> fifoB { { 0.3, true, 2 }, { 2.4, false, 2 } };
const std::vector<Step> lifo { { 0.0, true, 3 }, { 0.3, true, 4 }, { 0.6, false, 4 }, { 2.4, false, 3 } };
const std::vector<Step> lifoC { { 0.0, true, 3 }, { 2.4, false, 3 } };

struct Result
{
    double fifo, fifoRef, lifo, lifoRef;
};

// the levels after the first note's end, each beside the same note played alone
template <typename Make>
Result measure (Make make, bool withIds = true)
{
    const auto strip = [withIds] (std::vector<Step> s) {
        if (! withIds)
            for (auto& x : s)
                x.id = 0;
        return s;
    };
    Result r {};
    {
        auto in = make();
        r.fifo = rms (run (*in, strip (fifo), 2.1), 1.6, 2.0);
    }
    {
        auto in = make();
        r.fifoRef = rms (run (*in, strip (fifoB), 2.1), 1.6, 2.0);
    }
    {
        auto in = make();
        r.lifo = rms (run (*in, strip (lifo), 2.1), 1.6, 2.0);
    }
    {
        auto in = make();
        r.lifoRef = rms (run (*in, strip (lifoC), 2.1), 1.6, 2.0);
    }
    return r;
}

inline std::shared_ptr<const lynx::SampleData> sine (double seconds = 3.0)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < d->left.size(); ++i)
        d->left[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * (double) i / rate);
    return d;
}

inline std::unique_ptr<lynx::Instrument> sampler (double seconds = 4.0)
{
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.sampler.release = 0.02f; // (without a release the sampler plays its sample out: the note's end changes nothing)
    auto in = lynx::makeInstrument (lynx::InstrumentKind::sampler, sine (seconds));
    in->prepare (rate);
    in->configure (c, 120.0);
    return in;
}

// records the note id each call carries
struct IdSpy final : lynx::Instrument
{
    struct Event
    {
        bool on;
        int key;
        std::uint32_t id;
    };
    std::vector<Event> log;
    void prepare (double) noexcept override {}
    void noteOn (int key, float) noexcept override { log.push_back ({ true, key, startingNoteId() }); }
    void noteOff (int key) noexcept override { log.push_back ({ false, key, endingNoteId() }); }
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    bool idle() const noexcept override { return false; }
};
} // namespace voices190ej

// ScheduledNotes numbers every scheduled note; its "off" carries the number, also when the later note ends first
TEST (voices190ej_each_scheduled_note_gets_its_own_id)
{
    using namespace lynx;
    ScheduledNotes ledger;
    const double tps = 0.01; // 100 samples a tick
    EventBuffer ev;
    auto add = [&] (NoteEvent::Type type, int offset, float length) {
        NoteEvent e;
        e.type = type;
        e.channel = 0;
        e.key = 60;
        e.sampleOffset = offset;
        e.lengthTicks = length;
        ev.add (e);
    };
    // block 1: C at 0 (8 ticks), D at 10 (2 ticks): two ids
    add (NoteEvent::Type::on, 0, 8.0f);
    add (NoteEvent::Type::on, 10, 2.0f);
    ledger.process (ev, 100, tps);
    CHECK (ev.size() == 2);
    const auto c = ev[0].noteId, d = ev[1].noteId;
    CHECK (c != 0 && d != 0 && c != d);
    // block 3 (ticks 2-3): D's off (at tick 2.1) carries D's id, though C started first
    ev.clear();
    ledger.process (ev, 100, tps);
    ev.clear();
    add (NoteEvent::Type::off, 10, 0.0f);
    ledger.process (ev, 100, tps);
    CHECK (ev.size() == 1 && ev[0].noteId == d);
    // ... and C's off, given by the ledger at its end (none came from the scheduler), C's
    long long seen = 0;
    for (int b = 0; b < 8 && seen == 0; ++b)
    {
        ev.clear();
        ledger.process (ev, 100, tps);
        for (const auto& e : ev)
            if (e.type == NoteEvent::Type::off)
            {
                CHECK (e.noteId == c);
                ++seen;
            }
    }
    CHECK (seen == 1);
    // the numbers go on after a stop: an "off" sent late never matches a new note's voice
    ledger.clear();
    ev.clear();
    add (NoteEvent::Type::on, 0, 1.0f);
    ledger.process (ev, 100, tps);
    CHECK (ev.size() == 1 && ev[0].noteId != c && ev[0].noteId != d && ev[0].noteId != 0);
}

// The channel's echo delay: each repeat is a voice of its own (its own id), and the note's end carries the note's id
TEST (voices190ej_echo_repeats_are_voices_of_their_own)
{
    using namespace lynx;
    channelmisc::NoteEcho echo;
    EchoSettings s;
    s.feedback = 0.5f;
    s.echoes = 3;
    s.timeBeats = 0.25f;
    CHECK (s.enabled());
    std::vector<NoteEvent> in (2);
    in[0].type = NoteEvent::Type::on;
    in[0].key = 60;
    in[0].noteId = 7;
    in[1] = in[0];
    in[1].type = NoteEvent::Type::off;
    in[1].sampleOffset = 100;
    std::vector<NoteEvent> out (channelmisc::NoteEcho::maxEvents);
    std::vector<float> gains (channelmisc::NoteEcho::maxEvents);
    int count = 0;
    const double tps = 1.0; // a sample a tick: the whole echo series falls in one long block
    echo.process (0, in.data(), 2, 0.0, tps, 4000, s, out.data(), gains.data(), count);
    std::vector<std::uint32_t> onIds, offIds;
    for (int k = 0; k < count; ++k)
        (out[(std::size_t) k].type == NoteEvent::Type::on ? onIds : offIds).push_back (out[(std::size_t) k].noteId);
    CHECK (onIds.size() == 4u && offIds.size() == 4u);
    std::sort (onIds.begin(), onIds.end());
    std::sort (offIds.begin(), offIds.end());
    CHECK (onIds == offIds);                                         // every voice's end carries its own id
    CHECK (std::adjacent_find (onIds.begin(), onIds.end()) == onIds.end()); // and no two voices share one
    CHECK (std::count (onIds.begin(), onIds.end(), 7u) == 1);
}

// The rack hands each note's id to the instrument, on its "on" and on its "off"; a Mono channel's shared voice ends
// with an "off" without an id (whichever note ends last ends it)
TEST (voices190ej_the_rack_hands_each_note_its_id)
{
    for (const auto mono : { false, true })
    {
        i32d::Rig rig;
        auto spy = std::make_shared<voices190ej::IdSpy>();
        auto& p = rig.song.patterns[0];
        // FIFO 0-4 / 2-6 beats, LIFO 8-14 / 10-12 beats, key 60
        p.notes = { i32d::Rig::note (0, 4 * lynx::ppq, 60), i32d::Rig::note (2 * lynx::ppq, 4 * lynx::ppq, 60),
                    i32d::Rig::note (8 * lynx::ppq, 6 * lynx::ppq, 60), i32d::Rig::note (10 * lynx::ppq, 2 * lynx::ppq, 60) };
        p.lengthTicks = 16 * lynx::ppq;
        lynx::setChannelMono (rig.song.channels[1], mono);
        rig.state = lynx::RackState::build (rig.song, nullptr, rig.rate);
        const_cast<lynx::RackState&> (*rig.state).instruments[1] = spy;
        rig.play (15.0);
        std::vector<voices190ej::IdSpy::Event> ons, offs;
        for (const auto& e : spy->log)
            (e.on ? ons : offs).push_back (e);
        if (mono)
        {
            // (Mono: the overlapping notes glide the one voice; its "off" has no id)
            for (const auto& e : offs)
                CHECK (e.id == 0u);
            continue;
        }
        CHECK (ons.size() == 4u && offs.size() == 4u);
        if (ons.size() != 4u || offs.size() != 4u)
            continue;
        for (const auto& e : ons)
            CHECK (e.id != 0u);
        // the ends in time order: A (FIFO's first), B, D (the inner note ends first), C
        CHECK (offs[0].id == ons[0].id);
        CHECK (offs[1].id == ons[1].id);
        CHECK (offs[2].id == ons[3].id);
        CHECK (offs[3].id == ons[2].id);
    }
}

// LYNX's Sampler, FL's channel sampler (issue #158's cases e / f / h / m / v / w / y): the first end leaves the other
// note's voice sounding; without ids (a live key), the key's voices all end, as before
TEST (voices190ej_sampler_ends_only_the_note_s_own_voice)
{
    using namespace voices190ej;
    const auto r = measure ([] { return sampler(); });
    CHECK (r.fifoRef > 0.1);
    CHECK (r.fifo > 0.7 * r.fifoRef && r.fifo < 1.3 * r.fifoRef);
    CHECK (r.lifo > 0.7 * r.lifoRef && r.lifo < 1.3 * r.lifoRef);
    const auto old = measure ([] { return sampler(); }, false);
    CHECK (old.fifo < 0.01 * old.fifoRef);
    CHECK (old.lifo < 0.01 * old.lifoRef);
}

// The same through the rack: issue #158's case e (FL e_same_pat_same_track), the earlier clip's held note ends at bar 4
// and the later clip's held note of the same key sounds on to bar 6
TEST (voices190ej_issue158_case_e_held_note_sounds_on)
{
    using namespace lynx;
    auto s = iss158::song();
    s.playlist = { iss158::clip (0, 0, 4), iss158::clip (0, 2, 4) };
    for (auto& c : s.channels)
        c.instrument = InstrumentKind::sampler;
    auto state = RackState::build (s, nullptr, voices190ej::rate);
    auto in = voices190ej::sampler (16.0); // (a sine longer than the song)
    const_cast<RackState&> (*state).instruments[0] = std::shared_ptr<Instrument> (in.release());
    for (std::size_t c = 1; c < state->instruments.size(); ++c)
        const_cast<RackState&> (*state).instruments[c] = std::make_shared<voices190ej::IdSpy>(); // (silent)
    RackRenderer renderer;
    renderer.prepare (voices190ej::rate, 512);
    renderer.setTransportOptions (true, false, 0);
    const auto tps = ticksPerSample (s.bpm, voices190ej::rate);
    std::vector<float> l (512), r (512), out;
    double tick = 0.0;
    while (tick < 6.0 * iss158::bar) // (the song is 6 bars: at its end it starts again)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        renderer.process (state.get(), true, tick, 512, l.data(), r.data());
        out.insert (out.end(), l.begin(), l.end());
        tick += 512 * tps;
    }
    // 1 bar = 2 s: one held note in 1-3 s, two in 5-7 s, the later one alone in 9-11 s (LYNX before: silence there)
    const auto one = voices190ej::rms (out, 1.0, 3.0), two = voices190ej::rms (out, 5.0, 7.0), later = voices190ej::rms (out, 9.0, 11.0);
    CHECK (one > 0.01);
    CHECK (two > 1.5 * one);
    CHECK (later > 0.8 * one && later < 1.2 * one);
}

// The LYNX Synth (LYNX's own, FL's rule for its generators) and a VoiceShaper-free FPC-like path: per voice
TEST (voices190ej_lynx_synth_ends_only_the_note_s_own_voice)
{
    using namespace voices190ej;
    const auto make = [] {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::synth;
        c.synth.release = 0.02f;
        auto in = lynx::makeInstrument (lynx::InstrumentKind::synth);
        in->prepare (rate);
        in->configure (c, 120.0);
        return in;
    };
    const auto r = measure (make);
    CHECK (r.fifoRef > 0.01);
    CHECK (r.fifo > 0.7 * r.fifoRef);
    CHECK (r.lifo > 0.7 * r.lifoRef);
}

// One note per key (the generators FL plays that way): the newest note holds the key, the earlier note's end does nothing
TEST (voices190ej_key_owners)
{
    lynx::KeyOwners owners;
    CHECK (owners.ends (60, 0) && owners.ends (60, 5)); // (nothing started: every end ends)
    owners.start (60, 1);
    owners.start (60, 2);
    CHECK (! owners.ends (60, 1)); // FIFO: A's end does nothing, B holds the key
    CHECK (owners.ends (60, 2));
    CHECK (owners.ends (60, 0));   // an end without an id (a live key, a panic) always ends
    owners.start (61, 0);          // a live key
    CHECK (owners.ends (61, 9));
}

// FL's generators with the user's FL installation (the lynx_tests FL Data argument): each as FL 2026 renders it
TEST (voices190ej_fl_generators_end_voices_as_fl_does)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    struct Case
    {
        InstrumentKind kind;
        bool perVoice; // false: one note per key, the newest holds it
    };
    const Case cases[] = { { InstrumentKind::threeosc, true }, { InstrumentKind::sytrus, true }, { InstrumentKind::beepmap, true },
                           { InstrumentKind::dx10, true },     { InstrumentKind::simsynth, true }, { InstrumentKind::sakura, false },
                           { InstrumentKind::ogun, true },     { InstrumentKind::harmor, true },  { InstrumentKind::harmless, true },
                           { InstrumentKind::kepler, false },  { InstrumentKind::minisynth, false }, { InstrumentKind::poizone, false },
                           { InstrumentKind::sawer, false },   { InstrumentKind::toxic, false },  { InstrumentKind::morphine, false },
                           { InstrumentKind::gms, false },     { InstrumentKind::boobass, false } };
    for (const auto& k : cases)
    {
        // (the preset FL rendered: the Default preset where there is one, else the new channel's state)
        auto channel = natives::channelFromPresetFile (k.kind, natives::defaultPresetPath (natives::presetFolderOf (k.kind), true, fl), fl);
        if (! channel)
            channel = natives::makeChannel (k.kind, fl);
        if (! channel)
        {
            std::printf ("  voices190ej: kind %d not available here\n", (int) k.kind);
            continue;
        }
        const auto make = [&] {
            auto in = makeInstrument (k.kind);
            in->prepare (voices190ej::rate);
            in->configure (*channel, 120.0);
            in->settleNew();
            return in;
        };
        const auto r = voices190ej::measure (make);
        const auto fifoOk = r.fifoRef > 1.0e-4 && r.fifo > 0.5 * r.fifoRef;
        const auto lifoOk = k.perVoice ? r.lifo > 0.5 * r.lifoRef : r.lifo < 0.5 * r.lifoRef;
        if (! fifoOk || ! lifoOk)
            std::printf ("  voices190ej: kind %d fifo %.4g (alone %.4g), lifo %.4g (alone %.4g)\n", (int) k.kind, r.fifo,
                         r.fifoRef, r.lifo, r.lifoRef);
        CHECK (fifoOk);
        CHECK (lifoOk);
    }
}

// Evidence (not a check): LYNX_190EJ_WRITE_FLP=<folder> writes an FL project with FL's SoundFont Player (FL's own
// Default.sf2) playing round 190ej's FIFO / LIFO notes (key 60: 0-2 / 1-3 bars, 4-7 / 5-6 bars at 120 BPM). FL renders it
// (FL64.exe /R /Ewav) and LYNX imports the same file (/mnt/e/lynx-work/voices190ej/HANDOFF.md).
TEST (voices190ej_write_fl_soundfont_probe)
{
    const auto* dir = std::getenv ("LYNX_190EJ_WRITE_FLP");
    if (dir == nullptr)
        return;
    const std::string font = "C:\\Program Files\\Image-Line\\FL Studio 2026\\Plugins\\Fruity\\Generators\\Soundfont Player\\Data\\Default.sf2";
    flp::Project p;
    p.tempo = 120.0;
    flp::Channel c;
    c.index = 0;
    c.type = flp::ChannelType::instrument;
    c.name = "SF";
    c.pluginName = "Fruity Soundfont Player";
    c.pluginState = i173a::flSoundfontState6 ({ 1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -128, -1, -1 }, font);
    c.hasLevels = true;
    c.volume = 10000;
    c.pan = 6400;
    p.channels.push_back (c);
    flp::Pattern pat;
    pat.index = 1;
    for (const auto& [start, bars] : { std::pair { 0, 2 }, std::pair { 1, 2 }, std::pair { 4, 3 }, std::pair { 5, 1 } })
    {
        flp::Note n;
        n.position = start * 384;
        n.length = bars * 384;
        n.key = 60;
        n.velocity = 100;
        pat.notes.push_back (n);
    }
    p.patterns.push_back (pat);
    flp::Arrangement a;
    a.name = "Arrangement";
    flp::PlaylistItem item;
    item.position = 0;
    item.length = 384 * 8;
    item.index = 1;
    item.track = 0;
    a.items.push_back (item);
    p.arrangements.push_back (a);
    const auto bytes = flp::writeProject (p);
    std::ofstream out (std::string (dir) + "/soundfont.flp", std::ios::binary);
    out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    std::printf ("  wrote %s/soundfont.flp\n", dir);
}
