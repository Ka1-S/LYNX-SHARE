// Round 190rt, issue #311 (Eliovani72: "many controls of the channel's other tab change nothing in the sound", "I can't
// make a pluck with 3x Osc"): every control of the channel settings tabs of 3x Osc and the Sampler, rendered at a
// setting away from its default, must change the sound (where FL's does). The table is printed with LYNX_AUDIT311=1.

#include "core/FlVoiceBlock.h"

namespace issue311
{
constexpr double rate = 44100.0;

inline std::shared_ptr<const lynx::SampleData> tone()
{
    static const auto d = [] {
        auto s = std::make_shared<lynx::SampleData>();
        s->sampleRate = rate;
        s->left.resize ((std::size_t) (2.0 * rate));
        s->right.resize (s->left.size());
        for (std::size_t i = 0; i < s->left.size(); ++i)
        {
            // a bright, decaying tone with harmonics (a filter has something to take away), slightly stereo
            const auto t = (double) i / rate;
            double v = 0.0;
            for (int h = 1; h <= 12; ++h)
                v += std::sin (2.0 * 3.141592653589793 * 220.0 * h * t) / h;
            s->left[i] = (float) (0.3 * v * std::exp (-0.6 * t));
            s->right[i] = (float) (0.3 * v * std::exp (-0.7 * t));
        }
        return std::shared_ptr<const lynx::SampleData> (s);
    }();
    return d;
}

struct Rendered
{
    std::vector<float> left, right;
};

inline Rendered render (const lynx::Song& s, double seconds = 3.0)
{
    using namespace lynx;
    const int block = 256;
    auto state = RackState::build (s, nullptr, rate, [] (const std::string&) { return tone(); });
    RackRenderer r;
    r.prepare (rate, block);
    const auto blocks = (int) (seconds * rate / block);
    Rendered out;
    out.left.resize ((std::size_t) blocks * (std::size_t) block);
    out.right.resize (out.left.size());
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int b = 0; b < blocks; ++b)
        r.process (state.get(), true, (double) b * block * tps, block, out.left.data() + (std::size_t) b * (std::size_t) block,
                   out.right.data() + (std::size_t) b * (std::size_t) block);
    return out;
}

// the difference relative to the reference's RMS (0 = the same sound)
inline double change (const Rendered& a, const Rendered& b)
{
    double ref = 0.0, d = 0.0;
    for (std::size_t i = 0; i < a.left.size() && i < b.left.size(); ++i)
    {
        ref += (double) a.left[i] * a.left[i] + (double) a.right[i] * a.right[i];
        const auto l = (double) a.left[i] - b.left[i], r = (double) a.right[i] - b.right[i];
        d += l * l + r * r;
    }
    return ref > 0.0 ? std::sqrt (d / ref) : (d > 0.0 ? 1.0 : 0.0);
}

// one channel; notes: C5 v0.3, E5 v0.78 overlapping it, G5 v1.0, a C6 alone — over a bar and a half
inline lynx::Song song (const lynx::ChannelDef& c)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.channels = { c };
    s.patterns.resize (1);
    auto note = [] (std::int64_t start, std::int64_t length, int key, float velocity) {
        Note n;
        n.start = start;
        n.length = length;
        n.channel = 0;
        n.key = key;
        n.velocity = velocity;
        return n;
    };
    const auto step = (std::int64_t) ticksPerStep;
    s.patterns[0].notes = { note (0, 6 * step, 60, 0.3f), note (2 * step, 2 * step, 64, 0.78f), note (8 * step, 2 * step, 67, 1.0f),
                            note (13 * step, step, 72, 0.78f) };
    s.patterns[0].lengthTicks = 16 * step;
    Clip clip;
    clip.pattern = 0;
    clip.track = 0;
    clip.start = 0;
    clip.length = 16 * step;
    s.playlist = { clip };
    return s;
}

inline lynx::ChannelDef threeOsc()
{
    lynx::ChannelDef c;
    c.name = "3x Osc";
    c.instrument = lynx::InstrumentKind::threeosc;
    c.pluginState = lynx::flpexport::defaultThreeOscState();
    return c;
}

inline lynx::ChannelDef sampler()
{
    lynx::ChannelDef c;
    c.name = "Sampler";
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "tone.wav";
    return c;
}

struct Control
{
    const char* name;
    std::function<void (lynx::Song&, lynx::ChannelDef&)> set;
};

// the Misc tab (FL's "Miscellaneous functions") and the header, for any generator
inline std::vector<Control> miscControls()
{
    using lynx::Song;
    using lynx::ChannelDef;
    return {
        { "Levels: panning offset", [] (Song&, ChannelDef& c) { c.misc.levels.pan = 1.0f; } },
        { "Levels: volume multiplier", [] (Song&, ChannelDef& c) { c.misc.levels.volume = 0.5f; } },
        { "Levels: Mod X offset", [] (Song&, ChannelDef& c) { c.misc.levels.modX = -0.8f; } },
        { "Levels: Mod Y offset", [] (Song&, ChannelDef& c) { c.misc.levels.modX = -0.4f; c.misc.levels.modY = 0.8f; } },
        { "Polyphony: Max 1", [] (Song&, ChannelDef& c) { c.maxPolyphony = 1; } },
        { "Polyphony: Mono", [] (Song&, ChannelDef& c) { c.misc.mono = true; } },
        { "Polyphony: Porta (with Mono)", [] (Song&, ChannelDef& c) { c.misc.mono = true; c.misc.porta = true; c.misc.portaBeats = 0.5f; } },
        { "Time: Gate", [] (Song&, ChannelDef& c) { c.misc.gateBeats = 0.1f; } },
        { "Time: Shift", [] (Song&, ChannelDef& c) { c.misc.shiftBeats = 0.25f; } },
        { "Time: Swing (song swing 0.8)", [] (Song& s, ChannelDef&) { s.swing = 0.8f; } },
        { "Tracking: Vol -> Pan", [] (Song&, ChannelDef& c) { c.misc.velocity.pan = 1.0f; } },
        { "Tracking: Vol -> Mod X", [] (Song&, ChannelDef& c) { c.misc.velocity.modX = 1.0f; } },
        { "Tracking: Vol -> Mod Y", [] (Song&, ChannelDef& c) { c.misc.velocity.modY = 1.0f; } },
        { "Tracking: Key -> Pan", [] (Song&, ChannelDef& c) { c.misc.key.pan = 1.0f; } },
        { "Tracking: Key -> Mod X", [] (Song&, ChannelDef& c) { c.misc.key.modX = -1.0f; } },
        { "Tracking: Key -> Mod Y", [] (Song&, ChannelDef& c) { c.misc.key.modY = 1.0f; } },
        { "Echo: Feed", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; } },
        { "Echo: Pan (with Feed)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.pan = 1.0f; } },
        { "Echo: Pitch (with Feed)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.pitch = 1200.0f; } },
        { "Echo: Mod X (with Feed)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.modX = -0.3f; } },
        { "Echo: Mod Y (with Feed)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.modY = 1.0f; } },
        { "Echo: Time (with Feed)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.timeBeats = 0.25f; } },
        { "Echo: Echoes 1 (with Feed)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.echoes = 1; } },
        { "Echo: Ping pong (with Feed, Pan)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.pan = 0.5f; c.misc.echo.pingPong = true; } },
        { "Echo: Fat mode (with Feed)", [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.fat = true; } },
        { "Arpeggiator: Up", [] (Song&, ChannelDef& c) { c.arp.direction = 1; } },
        { "Arpeggiator: Range 3 (Up)", [] (Song&, ChannelDef& c) { c.arp.direction = 1; c.arp.range = 3; } },
        { "Arpeggiator: Time (Up)", [] (Song&, ChannelDef& c) { c.arp.direction = 1; c.arp.stepBeats = 0.5f; } },
        { "Arpeggiator: Gate (Up)", [] (Song&, ChannelDef& c) { c.arp.direction = 1; c.arp.gate = 1.0f; } },
        { "Arpeggiator: Repeat (Up, range 2)", [] (Song&, ChannelDef& c) { c.arp.direction = 1; c.arp.range = 2; c.arp.octaveRepeat = 2; } },
        { "Arpeggiator: Chord major (Up)", [] (Song&, ChannelDef& c) { c.arp.direction = 1; c.arp.chord = { 0, 4, 7 }; } },
        { "Arpeggiator: Slide (Up)", [] (Song&, ChannelDef& c) { c.arp.direction = 1; c.arp.slide = true; } },
        { "Root note C4 + Add to key", [] (Song&, ChannelDef& c) { c.misc.rootNote = 48; c.misc.addToKey = true; } },
        { "Root note: Fine tune", [] (Song&, ChannelDef& c) { c.misc.fineTune = 50.0f; } },
        { "Main pitch (song main pitch +100 c)", [] (Song& s, ChannelDef&) { s.masterPitch = 100.0f; } },
        { "Key region (from E5)", [] (Song&, ChannelDef& c) { c.keyLow = 64; } },
        { "Cut group (cuts itself)", [] (Song&, ChannelDef& c) { c.cutGroup = 1; c.cutBy = 1; } },
        { "Header: Volume", [] (Song&, ChannelDef& c) { c.volume = 0.3f; } },
        { "Header: Pan", [] (Song&, ChannelDef& c) { c.pan = -1.0f; } },
    };
}

inline std::vector<Control> samplerControls()
{
    using lynx::Song;
    using lynx::ChannelDef;
    return {
        { "Sample: Pitch", [] (Song&, ChannelDef& c) { c.sampler.pitch = 7.0f; } },
        { "Sample: Attack", [] (Song&, ChannelDef& c) { c.sampler.attack = 0.2f; } },
        { "Sample: Decay + Sustain", [] (Song&, ChannelDef& c) { c.sampler.decay = 0.1f; c.sampler.sustain = 0.2f; } },
        { "Sample: Release", [] (Song&, ChannelDef& c) { c.sampler.release = 0.05f; } },
        { "Sample: Cutoff", [] (Song&, ChannelDef& c) { c.sampler.cutoff = 500.0f; } },
        { "Sample: Resonance (cutoff 1 kHz)", [] (Song&, ChannelDef& c) { c.sampler.cutoff = 1000.0f; c.sampler.resonance = 0.9f; } },
        { "Sample: Filter type high-pass (cutoff 1 kHz)", [] (Song&, ChannelDef& c) { c.sampler.cutoff = 1000.0f; c.sampler.filterType = 2; } },
        { "Sample: Gain", [] (Song&, ChannelDef& c) { c.sampler.gainDb = -6.0f; } },
        { "Sample: Cut itself", [] (Song&, ChannelDef& c) { c.sampler.cut = true; } },
        { "Sample: Porta", [] (Song&, ChannelDef& c) { c.sampler.porta = true; c.sampler.cut = true; } },
        { "Sample: Reverse", [] (Song&, ChannelDef& c) { c.sampler.reverse = true; } },
        { "Sample: Region start", [] (Song&, ChannelDef& c) { c.sampler.regionStart = 0.3f; } },
        { "Sample: Start offset", [] (Song&, ChannelDef& c) { c.sampler.startOffset = 0.3f; } },
        { "Sample: Time stretch (resample)", [] (Song&, ChannelDef& c) { c.sampler.stretchBeats = 2.0f; } },
        { "Sample: Declicking mode smooth", [] (Song&, ChannelDef& c) { c.sampler.release = 0.0f; c.sampler.cut = true; c.sampler.declickMode = 4; } },
        { "Precomputed: Normalize", [] (Song&, ChannelDef& c) { c.sampler.precompute.normalize = true; } },
        { "Precomputed: Reverse polarity", [] (Song&, ChannelDef& c) { c.sampler.precompute.reversePolarity = true; } },
        { "Precomputed: Swap stereo", [] (Song&, ChannelDef& c) { c.sampler.precompute.swapStereo = true; } },
        { "Precomputed: Fade in", [] (Song&, ChannelDef& c) { c.sampler.precompute.fadeIn = 128; } },
        { "Precomputed: Boost", [] (Song&, ChannelDef& c) { c.sampler.precompute.boost = 200; } },
        { "Precomputed: Cutoff", [] (Song&, ChannelDef& c) { c.sampler.precompute.cutoff = 100; } },
        { "Envelope tab: filter envelope", [] (Song&, ChannelDef& c) { c.sampler.cutoff = 2000.0f; c.sampler.filterEnv = { -3.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.1f }; } },
        { "Envelope tab: pitch envelope", [] (Song&, ChannelDef& c) { c.sampler.pitchEnv = { 12.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.1f }; } },
        { "Envelope tab: volume LFO", [] (Song&, ChannelDef& c) { c.sampler.lfo[lynx::lfoVolume].amount = 0.8f; } },
        { "Envelope tab: pan LFO", [] (Song&, ChannelDef& c) { c.sampler.lfo[lynx::lfoPan].amount = 1.0f; } },
        { "Envelope tab: cutoff LFO", [] (Song&, ChannelDef& c) { c.sampler.cutoff = 2000.0f; c.sampler.lfo[lynx::lfoCutoff].amount = 2.0f; } },
        { "Envelope tab: pitch LFO", [] (Song&, ChannelDef& c) { c.sampler.lfo[lynx::lfoPitch].amount = 1.0f; } },
    };
}

struct Row
{
    std::string name;
    double change = 0.0;
};

inline std::vector<Row> audit (const lynx::ChannelDef& base, const std::vector<Control>& controls)
{
    const auto reference = render (song (base));
    std::vector<Row> rows;
    for (const auto& control : controls)
    {
        auto c = base;
        auto s = song (c);
        control.set (s, c);
        s.channels = { c };
        rows.push_back ({ control.name, change (reference, render (s)) });
    }
    return rows;
}

inline void print (const char* instrument, const std::vector<Row>& rows)
{
    if (std::getenv ("LYNX_AUDIT311") == nullptr)
        return;
    for (const auto& r : rows)
        std::printf ("AUDIT311 %-8s %-48s change %.4g %s\n", instrument, r.name.c_str(), r.change, r.change > 1.0e-3 ? "WORKS" : "NO CHANGE");
}
} // namespace issue311

TEST (issue311_audit_channel_controls_change_the_sound)
{
    using namespace issue311;
    const auto osc = audit (threeOsc(), miscControls());
    print ("3x Osc", osc);
    auto samplerRows = audit (sampler(), miscControls());
    const auto own = audit (sampler(), samplerControls());
    samplerRows.insert (samplerRows.end(), own.begin(), own.end());
    print ("Sampler", samplerRows);
    // Every row changes the sound except where FL's does not either (FL 2026 renders, round 190rt): FL's cut groups
    // leave its plugin generators alone (REA_rodada170i3 §2); on a one-shot Sampler (no release) Gate and Max polyphony
    // only end notes, which it ignores; a Sampler's root note moves its pitch through the Misc page (the knob, not this
    // field alone)
    auto expectNoChange = [] (const char* instrument, const std::string& row) {
        if (std::string (instrument) == "3x Osc")
            return row == "Cut group (cuts itself)";
        return row == "Polyphony: Max 1" || row == "Time: Gate" || row == "Root note C4 + Add to key";
    };
    for (const auto& [instrument, rows] : { std::pair<const char*, const std::vector<Row>*> { "3x Osc", &osc }, std::pair<const char*, const std::vector<Row>*> { "Sampler", &samplerRows } })
        for (const auto& r : *rows)
        {
            const auto works = r.change > 1.0e-3;
            if (works == expectNoChange (instrument, r.name))
                std::printf ("  %s %s: change %.4g\n", instrument, r.name.c_str(), r.change);
            CHECK (works != expectNoChange (instrument, r.name));
        }
    // the channel's swing mix 0 takes the song's swing away; Main pitch off takes the song's main pitch away
    {
        auto c = sampler();
        auto swung = song (c), straight = song (c);
        swung.swing = straight.swing = 0.8f;
        straight.channels[0].swingMix = 0.0f;
        CHECK (change (render (song (c)), render (straight)) < 1.0e-6 && change (render (song (c)), render (swung)) > 1.0e-3);
        auto off = song (c);
        off.masterPitch = 100.0f;
        off.channels[0].misc.mainPitch = false;
        CHECK (change (render (song (c)), render (off)) < 1.0e-6);
    }
}

namespace issue311
{
// FL's pluck on the volume envelope: no delay / attack / hold, a short decay to no sustain, a short release (FL knob
// values: 100 = none; flp::Channel::envelopeTime)
inline lynx::flvoice::Settings pluck (lynx::flvoice::Settings s)
{
    auto& v = s.articulators[lynx::flvoice::volume];
    v.envelope = true;
    v.delay = v.attack = v.hold = 100;
    v.decay = lynx::flpexport::envelopeTimeValue (0.25);
    v.sustain = 0;
    v.release = lynx::flpexport::envelopeTimeValue (0.05);
    // and the cutoff envelope closing the filter after the attack (FL's Mod X target, the channel filter at mid)
    s.modX = 160;
    auto& x = s.articulators[lynx::flvoice::cutoff];
    x.envelope = true;
    x.delay = x.attack = x.hold = 100;
    x.decay = lynx::flpexport::envelopeTimeValue (0.2);
    x.sustain = 0;
    x.amount = 96;
    return s;
}

// one long note (C5, two beats at 120 BPM): the level in [from, to) seconds
inline double levelIn (const lynx::ChannelDef& c, double from, double to)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.channels = { c };
    s.patterns.resize (1);
    Note n;
    n.start = 0;
    n.length = 8 * ticksPerStep;
    n.channel = 0;
    n.key = 60;
    s.patterns[0].notes = { n };
    s.patterns[0].lengthTicks = 16 * ticksPerStep;
    Clip clip;
    clip.pattern = 0;
    clip.length = 16 * ticksPerStep;
    s.playlist = { clip };
    const auto r = render (s, 1.5);
    double sum = 0.0;
    int count = 0;
    for (auto i = (std::size_t) (from * rate); i < (std::size_t) (to * rate) && i < r.left.size(); ++i, ++count)
        sum += (double) r.left[i] * r.left[i] + (double) r.right[i] * r.right[i];
    return count > 0 ? std::sqrt (sum / count) : 0.0;
}
} // namespace issue311

// FL's "Envelope / instrument settings" tab for 3x Osc (round 190rt): the page edits the voice block in the plugin's
// state, the engine plays it (a pluck), and the .flp export writes it as the channel's records (what FL loads)
TEST (issue311_three_osc_envelope_tab_makes_a_pluck)
{
    using namespace issue311;
    using namespace lynx;
    auto c = threeOsc();
    CHECK (flvoiceblock::has (c));
    const auto before = flvoiceblock::read (c);
    CHECK (before.has_value() && ! before->articulators[flvoice::volume].envelope); // a new 3x Osc: FL's defaults
    // reading and writing back changes no byte
    auto same = c;
    CHECK (flvoiceblock::write (same, *before) && same.pluginState == c.pluginState);
    // the default sustains while the note is held; the pluck dies away inside it
    const auto held = levelIn (c, 0.6, 0.9);
    auto plucked = c;
    CHECK (flvoiceblock::write (plucked, pluck (*before)));
    CHECK (plucked.pluginState.size() == c.pluginState.size());
    const auto pluckHead = levelIn (plucked, 0.0, 0.05), pluckTail = levelIn (plucked, 0.6, 0.9);
    CHECK (held > 0.05);
    CHECK (pluckHead > 0.02);
    CHECK (pluckTail < held * 0.02);
    // the export writes the block as the channel's articulator records and filter (FL loads them into the plugin)
    auto s = Song::makeDefault();
    s.channels = { plucked };
    flpexport::Options options;
    flpexport::Report report;
    const auto p = flp::parseProject (flpexport::exportFlp (s, options, report));
    const flp::Channel* out = nullptr;
    for (const auto& ch : p.channels)
        if (ch.type == flp::ChannelType::instrument)
            out = &ch;
    CHECK (out != nullptr);
    if (out != nullptr)
    {
        const auto block = flvoiceblock::bytes (plucked);
        CHECK (out->envelopes[1].rawSize >= 68 && std::memcmp (out->envelopes[1].raw.data(), block.data() + 0x44, 68) == 0);
        CHECK (out->envelopes[2].rawSize >= 68 && std::memcmp (out->envelopes[2].raw.data(), block.data() + 0x88, 68) == 0);
        CHECK (out->modX == 160);
        // and LYNX reads it back the same
        FlpImportOptions o;
        o.fileExists = [] (const std::string&) { return false; };
        const auto back = importFlp (p, o);
        const lynx::ChannelDef* ch = nullptr;
        for (const auto& d : back.channels)
            if (d.instrument == InstrumentKind::threeosc)
                ch = &d;
        CHECK (ch != nullptr && flvoiceblock::bytes (*ch) == block);
    }
    // optional artefacts for FL's /R render check: the default and the pluck
    if (const char* dir = std::getenv ("LYNX_ART311"))
        for (const auto& [name, channel] : { std::pair { "osc-default", c }, std::pair { "osc-pluck", plucked } })
        {
            auto song = Song::makeDefault();
            song.bpm = 120.0;
            song.channels = { channel };
            song.patterns.resize (1);
            Note n;
            n.length = 8 * ticksPerStep;
            n.key = 60;
            song.patterns[0].notes = { n };
            song.patterns[0].lengthTicks = 16 * ticksPerStep;
            Clip clip;
            clip.length = 16 * ticksPerStep;
            song.playlist = { clip };
            flpexport::Report r;
            const auto bytes = flpexport::exportFlp (song, options, r);
            std::ofstream f (std::string (dir) + "/" + name + ".flp", std::ios::binary);
            f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
        }
}

// DX10, Granulizer and BeepMap carry the same block (FL's render-test projects): the tab reads it, writes it back byte
// for byte, and a pluck there changes the sound
TEST (issue311_other_generators_with_the_voice_block)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    int seen = 0;
    for (const auto* name : { "3OSC", "Fruity DX10", "Fruity Granulizer", "Beepmap" })
    {
        const auto file = issue282::fileOf (name);
        if (file.empty())
            continue;
        const auto song = issue282::importFile (file);
        for (const auto& c : song.channels)
        {
            if (c.instrument != InstrumentKind::threeosc && c.instrument != InstrumentKind::dx10 && c.instrument != InstrumentKind::granulizer
                && c.instrument != InstrumentKind::beepmap)
                continue;
            ++seen;
            CHECK (flvoiceblock::has (c));
            const auto settings = flvoiceblock::read (c);
            CHECK (settings.has_value());
            if (! settings)
                continue;
            auto same = c;
            CHECK (flvoiceblock::write (same, *settings) && (same.pluginState == c.pluginState || ! flvoiceblock::offsetIn (c.instrument, c.pluginState)));
            CHECK (flvoiceblock::bytes (same) == flvoiceblock::bytes (c));
            auto plucked = c;
            CHECK (flvoiceblock::write (plucked, issue311::pluck (*settings)));
            CHECK (flvoiceblock::read (plucked)->articulators[flvoice::volume].envelope);
            const auto held = issue311::levelIn (c, 0.6, 0.9), tail = issue311::levelIn (plucked, 0.6, 0.9);
            std::printf ("  %s: held %.4g, plucked tail %.4g\n", name, held, tail);
            CHECK (held < 1.0e-4 || tail < held * 0.5);
        }
    }
    CHECK (seen >= 3);
}

// The FL /R render check of the audit's doubtful rows (LYNX_ART311 = folder, LYNX_ART311_WIN = its Windows path for the
// sample): the audit's song with one control changed, as .flp, for FL to render next to its reference
TEST (issue311_fl_render_artefacts)
{
    using namespace issue311;
    using namespace lynx;
    const char* dir = std::getenv ("LYNX_ART311");
    if (dir == nullptr)
        return;
    const std::string win = std::getenv ("LYNX_ART311_WIN") != nullptr ? std::getenv ("LYNX_ART311_WIN") : dir;
    struct Case
    {
        const char* name;
        bool sampler;
        std::function<void (Song&, ChannelDef&)> set;
    };
    const std::vector<Case> cases {
        { "a-osc-ref", false, [] (Song&, ChannelDef&) {} },
        { "a-osc-levelmodx", false, [] (Song&, ChannelDef& c) { c.misc.levels.modX = -0.8f; } },
        { "a-osc-levelmody", false, [] (Song&, ChannelDef& c) { c.misc.levels.modY = 0.8f; } },
        { "a-osc-cutitself", false, [] (Song&, ChannelDef& c) { c.cutGroup = 1; c.cutBy = 1; } },
        { "a-osc-keymodx", false, [] (Song&, ChannelDef& c) { c.misc.key.modX = -1.0f; } },
        { "a-osc-echomodx", false, [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; c.misc.echo.modX = -0.3f; } },
        { "a-osc-echo", false, [] (Song&, ChannelDef& c) { c.misc.echo.feedback = 0.5f; } },
        { "a-smp-ref", true, [] (Song&, ChannelDef&) {} },
        { "a-smp-maxpoly1", true, [] (Song&, ChannelDef& c) { c.maxPolyphony = 1; } },
    };
    for (const auto& k : cases)
    {
        auto c = k.sampler ? sampler() : threeOsc();
        if (k.sampler)
            c.samplePath = win + "\\tone.wav";
        auto s = song (c);
        k.set (s, c);
        s.channels = { c };
        flpexport::Options options;
        flpexport::Report report;
        const auto bytes = flpexport::exportFlp (s, options, report);
        std::ofstream f (std::string (dir) + "/" + k.name + ".flp", std::ios::binary);
        f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    }
}
