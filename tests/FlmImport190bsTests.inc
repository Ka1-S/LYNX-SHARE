// --- FL Studio Mobile (.flm) import, round 190bs: a sample slot's own settings (drum pads and audio clips), audio clip
// loops and envelopes, cut groups, the note effects (Transpose, Scale, Randomizer), SoundFont Player and racks with
// several instruments (docs/analise/FLM_FORMAT.md §6.1, §7.2, §7.3). Every project is written by the tests; no file of
// Image-Line's is committed.

#include "core/FlmNoteFx.h"
#include "core/FlmPad.h"
#include "core/SoundFont.h"

namespace flm190bs
{
using namespace flm182c;
using Bytes = flm182c::Bytes; // (tests.cpp has a Bytes of its own in its unnamed namespace: name it here)

struct Pad
{
    std::string name;
    std::vector<float> prms; // the slot's parameters (fewer than eleven = the app's defaults for the rest)
    bool reverse = false;
    double shift = 0.0;
    int cutGroup = 0;
    bool polyphonic = true;
    int stretchMode = 0;
    double stretch = 1.0, pitch = 0.0;
    int output = -1;
};

inline Bytes sampleOf (const Pad& p, const std::string& root = "@L@")
{
    Bytes body;
    tag (body, "20LS");
    Bytes main;
    u32 (main, 0xFFFFFFFFu);
    f64 (main, -1.0);
    fixed (main, p.name, 512);
    fixed (main, "My Samples", 512);
    u8 (main, p.reverse ? 1 : 0);
    f64 (main, p.shift);
    f32 (main, 0.5f);
    u8 (main, (std::uint8_t) p.cutGroup);
    u8 (main, p.polyphonic ? 1 : 0);
    Bytes link;
    u32 (link, 1);
    u32 (link, 0xFFFFFFFFu);
    u16 (link, 0xFFFF);
    u32 (link, (std::uint32_t) p.output);
    Bytes str1;
    u8 (str1, (std::uint8_t) p.stretchMode);
    f64 (str1, p.stretch);
    f64 (str1, p.pitch);
    Bytes pth;
    const auto path = "My Samples/" + p.name + ".wav";
    const auto text = root + std::string (1, '\0') + path;
    u16 (pth, (std::uint16_t) text.size());
    put (pth, text.data(), text.size());
    Bytes prms;
    for (const auto v : p.prms)
        f32 (prms, v);
    return cat ({ body, chunk ("MAIN", main), chunk ("LINk", link), chunk ("STR1", str1), chunk ("PTH1", pth), chunk ("PRMS", prms) });
}

// a module with its own parameter values, key range and engine state
inline Bytes moduleOf (std::uint32_t type, std::uint32_t id, const std::vector<float>& params, int keyLow = 0, int keyHigh = 127, const Bytes& custom = {},
                       const std::string& presetFile = {})
{
    Bytes b;
    u32 (b, type);
    u32 (b, id);
    Bytes h;
    u32 (h, type);
    u32 (h, 0);
    u8 (h, 1);
    Bytes add1 { 0, (std::uint8_t) keyLow, (std::uint8_t) keyHigh };
    Bytes desc;
    fixed (desc, "Default", 256);
    fixed (desc, "", 256);
    Bytes prst;
    lp32 (prst, presetFile);
    lp32 (prst, "Default/Default");
    Bytes prms;
    for (const auto v : params)
        f32 (prms, v);
    auto body = cat ({ b, chunk ("HEAD", h), chunk ("DESC", desc), chunk ("PRST", prst), chunk ("ADD1", add1), chunk ("PRMS", prms) });
    if (! custom.empty())
    {
        const auto c = chunk ("CSTM", custom);
        put (body, c.data(), c.size());
    }
    return chunk ("RMOd", body);
}

inline Bytes song (std::initializer_list<Bytes> racksAndChannels, double tempo = 120.0)
{
    Bytes f;
    tag (f, "10LF");
    Bytes m;
    fixed (m, "2025-07-06 11-39-10", 256);
    f64 (m, 0);
    fixed (m, "", 256);
    fixed (m, "Pads", 256);
    u8 (m, 0);
    fixed (m, "", 2048);
    fixed (m, "2026-11-11 12:00:00", 256);
    const Bytes tdiv { 4, 4 };
    auto out = cat ({ f, head ("Pads", tempo), chunk ("META", m), chunk ("TDIV", tdiv) });
    for (const auto& part : racksAndChannels)
        put (out, part.data(), part.size());
    // every rack at 0 dB (the builder's 0.9 is +1.96 dB)
    for (std::size_t i = 0; i + 12 <= out.size(); ++i)
        if (std::memcmp (out.data() + i, "RPRM", 4) == 0)
        {
            const float zero = 0.8531241f;
            std::memcpy (out.data() + i + 8, &zero, 4);
        }
    return out;
}

inline lynx::FlmImportOptions anyFile (lynx::FlpImportReport* report)
{
    lynx::FlmImportOptions o;
    o.common.projectFolder = "E:/flm/Song";
    o.resourceRoot = "C:/FLM";
    o.common.fileExists = [] (const std::string& p) { return p.find ("E:/flm/Song/") == 0 || p.find ("C:/FLM/") == 0; };
    o.common.report = report;
    return o;
}

// the imported song played from its start: a mono test sample for every file (`frames` of `value` at 44.1 kHz)
inline std::vector<float> rendered (const lynx::Song& song, double seconds, float value = 0.5f, int frames = 44100, std::vector<float>* right = nullptr)
{
    constexpr double rate = 44100.0;
    auto state = lynx::RackState::build (song, nullptr, rate, [value, frames] (const std::string&) -> std::shared_ptr<const lynx::SampleData> {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign ((std::size_t) frames, value);
        d->sampleRate = 44100.0;
        return d;
    });
    lynx::RackRenderer renderer;
    renderer.prepare (rate, 2048);
    const auto total = (std::size_t) (seconds * rate);
    std::vector<float> l (total, 0.0f), r (total, 0.0f);
    const auto perSample = lynx::ticksPerSample (song.bpm, rate);
    double tick = 0.0;
    for (std::size_t pos = 0; pos < total; pos += 256)
    {
        const auto n = (int) std::min<std::size_t> (256, total - pos);
        renderer.process (state.get(), true, tick, n, l.data() + pos, r.data() + pos);
        tick += n * perSample;
    }
    if (right != nullptr)
        *right = r;
    return l;
}
} // namespace flm190bs

TEST (flm190bs_sample_slot_fields_and_laws)
{
    using namespace lynx;
    namespace t = flm190bs;
    t::Pad pad { "Hat", { 0.7143f, 0.25f, 0.75f, 0.0f, 1.0f, 0.5f, 0.3f, 0.4f, 0.6f, 0.2f, 0.9f }, true, 0.25, 2, false, 1, 1.5, 3.0, 7 };
    const auto bytes = t::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, {}, { t::sampleOf (pad) }),
                                  t::channel (flm::kindMaster, "MASTER", 0, {}),
                                  t::channel (flm::kindNormal, "Kit", 1, { t::lane (0x103, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }) }) });
    const auto project = flm::parse ({ bytes.data(), bytes.size() });
    CHECK (project.has_value() && project->racks.size() == 2 && project->racks[1].kit.size() == 1);
    if (! project || project->racks.size() != 2 || project->racks[1].kit.size() != 1)
        return;
    const auto& s = project->racks[1].kit[0];
    CHECK (s.name == "Hat" && s.reverse && s.timeShift == 0.25 && s.cutGroup == 2 && ! s.polyphonic);
    CHECK (s.slotId == 1 && s.outputChannel == 7 && s.stretchMode == 1 && s.stretch == 1.5 && s.pitch == 3.0);
    CHECK (s.params.size() == 11 && s.param (flm::samplepad::speed, 0.0f) == 0.75f && s.param (20, 0.25f) == 0.25f);
    const auto& lane = project->channels[1].lanes[0];
    CHECK (lane.kind() == flm::LaneKind::drums && lane.holdsNotes());
    // a sample saved by an older app (no tail, no LINk, no PRMS) keeps the defaults
    const auto old = flm::parse ({ flm182c::project().data(), flm182c::project().size() });
    CHECK (old && old->racks[2].kit[0].polyphonic && old->racks[2].kit[0].cutGroup == 0 && old->racks[2].kit[0].params.empty()
           && ! old->channels[2].lanes[0].holdsNotes());

    // the laws (core/FlmPad.h)
    CHECK (std::abs (flmpad::levelGain (flm::samplepad::defaults[flm::samplepad::level]) - 1.0f) < 1e-4f); // the default is 0 dB
    CHECK (std::abs (flmpad::levelGain (1.0f) - 1.9953f) < 1e-3f && std::abs (flmpad::levelGain (0.0f) - 0.001f) < 1e-6f);
    CHECK (std::abs (flmpad::levelGain (0.7143f) - 0.4977f) < 2e-3f); // −6 dB: FLM's "Basic" kits
    CHECK (flmpad::speedSemitones (0.5f) == 0 && flmpad::speedSemitones (0.75f) == 12 && flmpad::speedSemitones (0.0f) == -24
           && flmpad::speedSemitones (1.0f) == 24 && flmpad::speedSemitones (0.53f) == 1 && flmpad::speedSemitones (0.47f) == -1
           && flmpad::speedSemitones (0.51f) == 0); // whole semitones, towards 0
    CHECK (flmpad::startSeconds (0.5f) == 0.0 && std::abs (flmpad::startSeconds (1.0f) - 0.5) < 1e-9 && std::abs (flmpad::startSeconds (0.25f) + 0.125) < 1e-9);
    CHECK (flmpad::attackRate (0.0f) == 1.0f && std::abs (flmpad::attackRate (1.0f) - 4.0e-7f) < 1e-9f && std::abs (flmpad::decayRate (1.0f) - 4.0e-6f) < 1e-8f);
    CHECK (std::abs (flmpad::rateAt (0.001f, 44100.0) - 0.001f) < 1e-9f && std::abs (flmpad::rateAt (0.001f, 88200.0) - 0.00050013f) < 1e-7f);
}

TEST (flm190bs_drum_pads_take_their_settings)
{
    using namespace lynx;
    namespace t = flm190bs;
    namespace sp = flm::samplepad;
    const std::vector<float> d (sp::defaults, sp::defaults + sp::count);
    auto quiet = d, loud = d, right = d, fast = d, muted = d, shaped = d;
    quiet[sp::level] = 0.7143f;
    loud[sp::level] = 1.0f;
    right[sp::pan] = 0.75f;
    fast[sp::speed] = 0.75f;
    muted[sp::mute] = 1.0f;
    shaped[sp::egAttack] = 0.2f, shaped[sp::egDecay] = 0.3f, shaped[sp::egSustain] = 0.5f, shaped[sp::egRelease] = 0.4f, shaped[sp::start] = 0.6f;
    const auto bytes = t::song ({ t::rack (flm::kindMaster, {}),
                                  t::rack (flm::kindNormal, {}, { t::sampleOf ({ "Plain", {} }), t::sampleOf ({ "Quiet", quiet }), t::sampleOf ({ "Loud", loud }),
                                                                  t::sampleOf ({ "Right", right }), t::sampleOf ({ "Fast", fast }), t::sampleOf ({ "Muted", muted }),
                                                                  t::sampleOf ({ "Shaped", shaped }), t::sampleOf ({ "Back", d, true, 0.5 }),
                                                                  t::sampleOf ({ "Closed", d, false, 0.0, 1 }), t::sampleOf ({ "Open", d, false, 0.0, 1, false }),
                                                                  t::sampleOf ({ "Away", d, false, 0.0, 0, true, 1, 2.0, 0.0, 3 }) }),
                                  t::channel (flm::kindMaster, "MASTER", 0, {}),
                                  t::channel (flm::kindNormal, "Kit", 1, { t::lane (3, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 }, { 1, 0.25, 7 }, { 3.75, 0.25, 7 } }) }) }) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto named = [&] (const char* n) { return flm182c::channelNamed (*song, n); };
    for (const auto* n : { "Plain", "Quiet", "Loud", "Right", "Fast", "Muted", "Shaped", "Back", "Closed", "Open", "Away" })
        CHECK (named (n) != nullptr);
    if (named ("Away") == nullptr || named ("Plain") == nullptr)
        return;
    // a pad at its defaults is the plain one-shot it was
    CHECK (*named ("Plain") == [&] { auto c = *named ("Plain"); c.volume = 1.0f; c.pan = 0.0f; c.muted = false; c.sampler = SamplerSettings {}; return c; }());
    CHECK (std::abs (channelGain (named ("Quiet")->volume) - flmpad::levelGain (0.7143f)) < 1e-4f && named ("Quiet")->sampler.gainDb == 0.0f);
    CHECK (named ("Loud")->volume == 1.0f && std::abs (named ("Loud")->sampler.gainDb - 6.0f) < 0.01f); // above 0 dB: the sampler's gain
    CHECK (std::abs (named ("Right")->pan - 0.5f) < 1e-6f);
    CHECK (named ("Fast")->sampler.pitch == 12.0f);
    CHECK (named ("Muted")->muted && ! named ("Plain")->muted);
    const auto& pad = named ("Shaped")->sampler.flmPad;
    CHECK (pad.size() == (std::size_t) flmpad::count && pad[flmpad::attack] == 0.2f && pad[flmpad::decay] == 0.3f && pad[flmpad::sustain] == 0.5f
           && pad[flmpad::release] == 0.4f && pad[flmpad::length] == 0.0f && pad[flmpad::start] == 0.6f && pad[flmpad::hold] == 0.0f);
    CHECK (named ("Plain")->sampler.flmPad.empty() && named ("Quiet")->sampler.flmPad.empty());
    CHECK (named ("Back")->sampler.reverse && ! named ("Plain")->sampler.reverse);
    // the cut group of two: each cuts the other, neither itself; the monophonic pad cuts itself
    const auto* closed = named ("Closed");
    const auto* open = named ("Open");
    CHECK (closed->cutGroup > 0 && closed->cutGroup == open->cutBy && open->cutGroup > 0 && open->cutGroup == closed->cutBy && closed->cutGroup != closed->cutBy);
    CHECK (open->sampler.cut && ! closed->sampler.cut && named ("Plain")->cutGroup == 0);
    // the time shift moves the pad's notes (never before the clip's start, and out of the clip = not played)
    CHECK (song->patterns.size() == 1);
    if (song->patterns.size() == 1)
    {
        const auto& notes = song->patterns[0].notes;
        CHECK (notes.size() == 2 && notes[0].start == 0 && notes[1].start == ppq * 3 / 2 && song->channels[(std::size_t) notes[1].channel].name == "Back");
    }
    CHECK (report.converted.count ("FL Studio Mobile: drum pad settings (level, pan, speed, mute, envelope)") == 1
           && report.converted["FL Studio Mobile: drum pad settings (level, pan, speed, mute, envelope)"] == 7);
    CHECK (report.converted.count ("FL Studio Mobile: drum pad cut group") == 1 && report.converted.count ("FL Studio Mobile: drum pad time shift") == 1);
    // (round 190dw: output links are applied; "Away" links to channel 3, which this song lacks)
    CHECK (report.partialEffects.count ("FL Studio Mobile: drum pad linked to a channel the song lacks (plays in its own)") == 1
           && report.partialEffects.count ("FL Studio Mobile: drum pad time-stretch or pitch shift (played as recorded)") == 1);

    // Solo on one pad mutes the others; a lane that holds its notes hands every pad FLM's envelope
    auto solo = d;
    solo[sp::solo] = 1.0f;
    const auto held = t::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, {}, { t::sampleOf ({ "A", solo }), t::sampleOf ({ "B", d }) }),
                                 t::channel (flm::kindMaster, "MASTER", 0, {}),
                                 t::channel (flm::kindNormal, "Kit", 1, { t::lane (0x103, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 0 } }) }) }) });
    const auto second = importFlm (held, t::anyFile (nullptr));
    CHECK (second.has_value());
    if (second)
    {
        const auto* a = flm182c::channelNamed (*second, "A");
        const auto* b = flm182c::channelNamed (*second, "B");
        CHECK (a != nullptr && b != nullptr && ! a->muted && b->muted);
        CHECK (a != nullptr && a->sampler.flmPad.size() == (std::size_t) flmpad::count && a->sampler.flmPad[flmpad::hold] == 1.0f);
    }
}

// The pad envelope in the sampler against the law written out here: level += (target − level)·rate per sample, the
// attack towards 5 and clipped at 1, then the decay towards EG S; the release towards 0 only on a lane that holds notes.
TEST (flm190bs_pad_envelope_in_the_sampler)
{
    using namespace lynx;
    const auto sample = [] {
        auto d = std::make_shared<SampleData>();
        d->left.assign (44100 * 3, 1.0f);
        d->sampleRate = 44100.0;
        return d;
    }();
    const auto play = [&] (const std::vector<float>& pad, int holdSamples, int total, double rate = 44100.0) {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.volume = 1.0f;
        c.sampler.flmPad = pad;
        auto inst = makeInstrument (InstrumentKind::sampler, sample);
        inst->prepare (rate);
        inst->configure (c, 120.0);
        std::vector<float> l ((std::size_t) total, 0.0f), r ((std::size_t) total, 0.0f);
        inst->noteOn (60, 1.0f);
        inst->render (l.data(), r.data(), holdSamples, 1.0f, 1.0f);
        inst->noteOff (60);
        if (total > holdSamples)
            inst->render (l.data() + holdSamples, r.data() + holdSamples, total - holdSamples, 1.0f, 1.0f);
        return l;
    };
    const auto law = [] (const std::vector<float>& pad, int releaseAt, int total) {
        std::vector<float> y ((std::size_t) total, 0.0f);
        float level = 0.0f, target = 5.0f, rate = flmpad::attackRate (pad[flmpad::attack]);
        for (int i = 0; i < total; ++i)
        {
            if (i == releaseAt)
                target = 0.0f, rate = flmpad::decayRate (pad[flmpad::release]);
            level += (target - level) * rate;
            if (level > 1.0f)
                level = 1.0f, target = pad[flmpad::sustain], rate = flmpad::decayRate (pad[flmpad::decay]);
            y[(std::size_t) i] = level;
        }
        return y;
    };
    const auto worst = [] (const std::vector<float>& a, const std::vector<float>& b, float scale) {
        float w = 0.0f;
        for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            w = std::max (w, std::abs (a[i] - scale * b[i]));
        return w;
    };
    const auto unit = play ({}, 100, 200)[150]; // the sampler's level for a full-velocity note of a sample at 1
    CHECK (unit > 0.1f);

    // attack, decay to the sustain; a one-shot lane ignores the note's end
    const std::vector<float> shaped { 0.1f, 0.15f, 0.4f, 0.1f, 0.0f, 0.5f, 0.0f };
    const auto a = play (shaped, 2000, 30000);
    const auto expected = law (shaped, -1, 30000);
    CHECK (worst (a, expected, unit) < 2e-3f * unit);
    CHECK (a[100] < 0.5f * unit && a[29999] < 0.6f * unit && a[29999] > 0.39f * unit); // it rose, then fell towards 0.4
    // a lane that holds its notes: the release starts at the note's end, and the voice ends below −80 dB
    auto heldPad = shaped;
    heldPad[flmpad::hold] = 1.0f;
    const auto h = play (heldPad, 2000, 30000);
    CHECK (worst (h, law (heldPad, 2001, 30000), unit) < 2e-3f * unit); // (the sample after the note's end is the first of the release)
    CHECK (h[1999] > 0.2f * unit && h[29999] == 0.0f);
    // Length: the release without a note end, Length × the sample's frames after the start
    auto lengthPad = shaped;
    lengthPad[flmpad::length] = 0.05f; // 6615 frames of the 3 s sample
    const auto byLength = play (lengthPad, 100, 30000);
    CHECK (worst (byLength, law (lengthPad, 6615, 30000), unit) < 2e-3f * unit);
    // the same curve in time at another output rate
    const auto twice = play (shaped, 4000, 60000, 88200.0);
    float off = 0.0f;
    for (int i = 0; i < 30000; i += 100)
        off = std::max (off, std::abs (twice[(std::size_t) (2 * i + 1)] - a[(std::size_t) i]));
    CHECK (off < 5e-3f * unit);

    // Sample Start: later = into the sample; earlier = silence first
    auto ramp = std::make_shared<SampleData>();
    ramp->left.resize (44100);
    for (std::size_t i = 0; i < ramp->left.size(); ++i)
        ramp->left[i] = (float) i / 44100.0f;
    ramp->sampleRate = 44100.0;
    const auto start = [&] (float v) {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.volume = 1.0f;
        c.sampler.flmPad = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, v, 0.0f };
        auto inst = makeInstrument (InstrumentKind::sampler, ramp);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        std::vector<float> l (8000, 0.0f), r (8000, 0.0f);
        inst->noteOn (60, 1.0f);
        inst->render (l.data(), r.data(), 8000, 1.0f, 1.0f);
        return l;
    };
    const auto plain = start (0.5f), later = start (0.75f), earlier = start (0.25f);
    CHECK (plain[4410] > 0.0f && std::abs (plain[4410] / unit - 0.1f) < 2e-3f);
    CHECK (std::abs (later[0] / unit - 0.125f) < 2e-3f);                        // 0.5·(0.5)² = 0.125 s in
    CHECK (earlier[5000] == 0.0f && std::abs (earlier[5512 + 441] / unit - 0.01f) < 2e-3f); // 0.125 s of silence first

    // editing the ADSR by hand goes back to LYNX's own envelope
    SamplerSettings edited;
    edited.flmPad = shaped;
    samplerparam::set (edited, samplerparam::attack, 0.1f);
    CHECK (edited.flmPad.empty());
}

TEST (flm190bs_drum_pads_render_at_their_level)
{
    using namespace lynx;
    namespace t = flm190bs;
    namespace sp = flm::samplepad;
    const std::vector<float> d (sp::defaults, sp::defaults + sp::count);
    auto quiet = d, left = d;
    quiet[sp::level] = 0.7143f;
    left[sp::pan] = 0.25f;
    const auto bytes = t::song ({ t::rack (flm::kindMaster, {}),
                                  t::rack (flm::kindNormal, {}, { t::sampleOf ({ "Plain", {} }), t::sampleOf ({ "Quiet", quiet }), t::sampleOf ({ "Left", left }) }),
                                  t::channel (flm::kindMaster, "MASTER", 0, {}),
                                  t::channel (flm::kindNormal, "Kit", 1, { t::lane (3, "Kit", { t::clip (0, 8, 8, 0, 1, { { 0, 0.25, 0, 255 }, { 2, 0.25, 1, 255 }, { 4, 0.25, 2, 255 } }) }) }) });
    const auto song = importFlm (bytes, t::anyFile (nullptr));
    CHECK (song.has_value());
    if (! song)
        return;
    std::vector<float> r;
    const auto l = t::rendered (*song, 3.5, 0.5f, 22050, &r); // beats 0, 2 and 4 at 120 BPM: 0 s, 1 s, 2 s; each hit 0.5 s long
    const auto plain = l[4410], quietLevel = l[44100 + 4410], leftL = l[88200 + 4410], leftR = r[88200 + 4410];
    CHECK (plain > 0.05f && std::abs (r[4410] - plain) < 1e-4f);
    CHECK (std::abs (quietLevel / plain - flmpad::levelGain (0.7143f)) < 2e-3f); // −6.06 dB
    CHECK (std::abs (leftL / plain - 1.0f) < 2e-3f && std::abs (leftR / plain - 0.5f) < 2e-3f); // balance: the far side falls linearly
}

TEST (flm190bs_audio_clips_take_their_slot_settings)
{
    using namespace lynx;
    namespace t = flm190bs;
    namespace sp = flm::samplepad;
    const std::vector<float> d (sp::defaults, sp::defaults + sp::count);
    auto loud = d, several = d;
    loud[sp::level] = 0.9463f;
    several[sp::pan] = 0.25f, several[sp::speed] = 0.75f, several[sp::mute] = 1.0f;
    const auto level = [] (double beat, int value, bool user = true) { return flm182c::Ev { beat, 0.0, user ? 1 : -1, 200, 127, value }; };
    const auto bytes = t::song ({
        t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, {}),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Audio", 1, { t::lane (2, "Audio", {
            t::clip (0, 4, 256, 0, 1, {}, t::sampleOf ({ "Plain", {} }), 4),                                    // defaults: as before
            t::clip (4, 4, 256, 0, 2, {}, t::sampleOf ({ "Loud", loud }), 4),                                   // PRMS level
            t::clip (8, 4, 256, 0, 3, { level (0, 25801) }, t::sampleOf ({ "Low", {} }), 4),                    // one Level point: the clip's level
            t::clip (12, 4, 256, 0, 4, {}, t::sampleOf ({ "Set", several, true, 0.0, 0, true, 0, 2.0, 2.6 }), 4),    // pan, speed, mute, reverse, pitch; mode 0 = no stretch
            t::clip (16, 10, 4, 1, 5, {}, t::sampleOf ({ "Loop", {} }), 4),                                     // 10 beats of a 4-beat loop from beat 1
            t::clip (32, 4, 256, 0, 6, { level (0, 65534), level (2, 32767, false), level (4, 0, false) }, t::sampleOf ({ "Fade", {} }), 4),
            t::clip (40, 4, 256, 0, 7, {}, t::sampleOf ({ "Slow", d, false, 0.0, 0, true, 1, 2.0, 0.0 }), 4),    // stretched to twice its length
            t::clip (48, 4, 256, 0, 8, { flm182c::Ev { 0, 0, 3, 200, 127, 65534 }, flm182c::Ev { 2, 0, -3, 200, 127, 0 }, flm182c::Ev { 0, 0, 7, 200, 127, 100 } },
                     t::sampleOf ({ "Bend", {} }), 4) }) }) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto clipsOf = [&] (const std::string& stem) {
        std::vector<const Clip*> out;
        for (const auto& c : song->playlist)
            if (c.kind == ClipKind::audio && song->channels[(std::size_t) c.channel].samplePath == "E:/flm/Song/My Samples/" + stem + ".wav")
                out.push_back (&c);
        return out;
    };
    const auto one = [&] (const std::string& stem) -> const Clip* { const auto v = clipsOf (stem); return v.size() == 1 ? v[0] : nullptr; };
    for (const auto* n : { "Plain", "Loud", "Low", "Set", "Fade", "Slow", "Bend" })
        CHECK (one (n) != nullptr);
    if (one ("Plain") == nullptr || one ("Loud") == nullptr || one ("Low") == nullptr || one ("Set") == nullptr || one ("Fade") == nullptr || one ("Slow") == nullptr
        || one ("Bend") == nullptr)
        return;
    const auto channelOf = [&] (const Clip* c) -> const ChannelDef& { return song->channels[(std::size_t) c->channel]; };
    CHECK (one ("Plain")->gain == 1.0f && one ("Plain")->pan == 0.0f && ! one ("Plain")->muted && one ("Plain")->stretch == 1.0
           && channelOf (one ("Plain")).volume == audioClipUnityVolume && channelOf (one ("Plain")).sampler.pitch == 0.0f);
    CHECK (std::abs (one ("Loud")->gain - flmpad::levelGain (0.9463f)) < 1e-5f && one ("Loud")->gain > 1.5f); // +3.85 dB
    CHECK (std::abs (one ("Low")->gain - flmpad::levelGain (25801.0f / 65534.0f)) < 1e-5f && one ("Low")->gain < 0.1f); // −22 dB
    const auto* set = one ("Set");
    CHECK (std::abs (set->pan + 0.5f) < 1e-6f && set->muted && set->reversed && set->pitch == 3.0f && set->stretch == 1.0);
    CHECK (channelOf (set).sampler.pitch == 12.0f && ! channelOf (set).sampler.keepPitch); // Speed resamples: the channel's pitch
    // the loop: the first pass from beat 1 to the loop's end, then whole loops, the last one cut by the clip's end
    const auto loop = clipsOf ("Loop");
    CHECK (loop.size() == 3);
    if (loop.size() == 3)
        CHECK (loop[0]->start == 16 * ppq && loop[0]->offset == ppq && loop[0]->length == 3 * ppq && loop[1]->start == 19 * ppq && loop[1]->offset == 0
               && loop[1]->length == 4 * ppq && loop[2]->start == 23 * ppq && loop[2]->length == 3 * ppq);
    // a Level that moves: the clip's gain is the top, the channel (the clip's own) is automated below it
    const auto* fade = one ("Fade");
    CHECK (std::abs (fade->gain - flmpad::levelGain (1.0f)) < 1e-5f);
    const Automation* curve = nullptr;
    for (const auto& a : song->automations)
        if (a.target.kind == AutomationTargetKind::channelVolume && a.target.index == fade->channel)
            curve = &a;
    CHECK (curve != nullptr && curve->points.size() == 3);
    if (curve != nullptr && curve->points.size() == 3)
    {
        const auto unity = channelGain (audioClipUnityVolume);
        CHECK (curve->points[0].tick == 0 && std::abs (curve->points[0].value - audioClipUnityVolume) < 1e-5f); // the top = the clip's gain alone
        CHECK (curve->points[1].tick == 2 * ppq
               && std::abs (channelGain (curve->points[1].value) / unity - flmpad::levelGain (32767.0f / 65534.0f) / flmpad::levelGain (1.0f)) < 1e-4f);
        CHECK (curve->points[2].tick == 4 * ppq && channelGain (curve->points[2].value) / unity < 0.001f);
    }
    bool automationClip = false;
    for (const auto& c : song->playlist)
        automationClip = automationClip || (c.kind == ClipKind::automation && c.start == 32 * ppq && c.length == 4 * ppq
                                            && song->playlistTracks[(std::size_t) c.track].name == "Audio (clip envelope)");
    CHECK (automationClip);
    // stretch (a length ratio, with a stretch mode): the clip's stretch on a channel that keeps the pitch
    CHECK (one ("Slow")->stretch == 2.0 && channelOf (one ("Slow")).sampler.keepPitch && one ("Slow")->length == 4 * ppq);
    // Speed that moves and other parameters: listed
    CHECK (channelOf (one ("Bend")).sampler.pitch == 24.0f);
    CHECK (report.partialEffects.count ("FL Studio Mobile: audio clip Speed automation (the first value is used)") == 1
           && report.unsupported.count ("FL Studio Mobile: audio clip automation of other sample settings (not imported)") == 1
           && report.partialEffects.count ("FL Studio Mobile: audio clip time-stretch (LYNX's stretcher, approximated)") == 1
           && report.partialEffects.count ("FL Studio Mobile: audio clip time-stretch (played unstretched)") == 0);
    CHECK (report.converted.count ("FL Studio Mobile: looped audio clip") == 1 && report.converted.count ("FL Studio Mobile: audio clip level") == 1
           && report.converted.count ("FL Studio Mobile: audio clip envelope (automation)") == 1);
}

TEST (flm190bs_audio_clip_levels_render)
{
    using namespace lynx;
    namespace t = flm190bs;
    const auto level = [] (double beat, int value, bool user = true) { return flm182c::Ev { beat, 0.0, user ? 1 : -1, 200, 127, value }; };
    const auto half = (int) std::lround (65534.0 * 0.7143);
    const auto bytes = t::song ({
        t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, {}),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Audio", 1, { t::lane (2, "Audio", {
            t::clip (0, 1, 256, 0, 1, {}, t::sampleOf ({ "Plain", {} }), 4),
            t::clip (2, 1, 256, 0, 2, { level (0, half) }, t::sampleOf ({ "Low", {} }), 4),
            t::clip (4, 2, 256, 0, 3, { level (0, 55909), level (1, half, false), level (2, half, false) }, t::sampleOf ({ "Fade", {} }), 4) }) }) });
    const auto song = importFlm (bytes, t::anyFile (nullptr));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto l = t::rendered (*song, 3.2, 0.5f, 3 * 44100); // beat = 0.5 s
    const auto plain = l[11025];
    CHECK (std::abs (plain - 0.5f) < 2e-3f); // unity
    CHECK (std::abs (l[44100 + 11025] / plain - flmpad::levelGain (0.7143f)) < 3e-3f);
    // the envelope: 0 dB (55909 / 65534 = 0.8531) at the clip's start, −6 dB from its middle on
    CHECK (std::abs (l[88200 + 600] / plain - 1.0f) < 0.03f);
    CHECK (std::abs (l[88200 + 22050 + 11025] / plain - flmpad::levelGain (0.7143f)) < 0.01f);
    CHECK (l[88200 + 11025] < l[88200 + 600] && l[88200 + 11025] > l[88200 + 22050 + 2000]);
}

TEST (flm190bs_note_effects_laws)
{
    using namespace lynx;
    namespace fx = flmnotefx;
    // Transpose: Octave round(8v − 4), Semitone round(24v − 12); off = nothing
    CHECK (fx::transposeShift (std::vector<float> { 1.0f, 0.5f, 0.5f }) == 0);
    CHECK (fx::transposeShift (std::vector<float> { 1.0f, 0.625f, 0.5f }) == 12 && fx::transposeShift (std::vector<float> { 1.0f, 0.375f, 0.75f }) == -6);
    CHECK (fx::transposeShift (std::vector<float> { 1.0f, 0.0f, 0.0f }) == -60 && fx::transposeShift (std::vector<float> { 0.0f, 1.0f, 1.0f }) == 0);
    // Scale: the switches are the scale; C major looks upwards first, A natural minor downwards first
    const auto scale = [] (int root, int index, bool snap, std::uint16_t mask) {
        std::vector<float> p { 1.0f, (float) root / 11.0f, (float) index / 33.0f, snap ? 1.0f : 0.0f };
        for (int i = 0; i < 12; ++i)
            p.push_back ((mask >> i) & 1u ? 1.0f : 0.0f);
        return p;
    };
    CHECK (fx::scaleCount == 33 && std::string (fx::scaleInfo (1).name) == "Major" && fx::scaleInfo (1).mask == 0x0AB5 && fx::scaleInfo (10).direction == -1);
    const auto major = fx::scaleSetting (scale (0, 1, true, 0x0AB5));
    CHECK (major.enabled && major.snap && major.mask == 0x0AB5 && major.direction == 1);
    CHECK (fx::scaleKey (major, 60) == 60 && fx::scaleKey (major, 61) == 62 && fx::scaleKey (major, 66) == 67 && fx::scaleKey (major, 70) == 71);
    const auto minorA = fx::scaleSetting (scale (9, 10, true, 0x0AB5)); // A natural minor = the white keys
    CHECK (minorA.direction == -1 && fx::scaleKey (minorA, 61) == 60 && fx::scaleKey (minorA, 66) == 65);
    const auto custom = fx::scaleSetting (scale (0, 33, true, 0x0081)); // C and G only
    CHECK (custom.direction == 0 && fx::scaleKey (custom, 62) == 60 && fx::scaleKey (custom, 63) == 60 && fx::scaleKey (custom, 64) == 67
           && fx::scaleKey (custom, 65) == 67 && fx::scaleKey (custom, 69) == 67 && fx::scaleKey (custom, 70) == 72); // ties go up
    CHECK (fx::scaleKey (fx::scaleSetting (scale (0, 33, true, 0)), 61) == 61); // an empty set changes nothing

    // the chain: the rack's order; Scale without Snap holds notes back
    fx::Chain chain;
    CHECK (chain.add (fx::transpose, std::vector<float> { 1.0f, 0.5f, 13.0f / 24.0f }) && chain.add (fx::scale, scale (0, 1, true, 0x0AB5)));
    CHECK (! chain.add (fx::arpeggiator, std::vector<float> { 1.0f }) && ! chain.add (fx::noteEcho, std::vector<float> { 1.0f }) && ! chain.add (7, {}));
    fx::Note n { 60, 0.5f, 0.5f };
    CHECK (chain.apply (n) && n.key == 62 && n.velocity == 0.5f && n.pan == 0.5f); // +1 → C♯ → snapped up to D
    fx::Chain filter;
    filter.add (fx::scale, scale (0, 1, false, 0x0AB5));
    fx::Note in { 64, 0.5f, 0.5f }, out { 61, 0.5f, 0.5f };
    CHECK (filter.filters() && filter.apply (in) && in.key == 64 && ! filter.apply (out));
    fx::Chain high;
    high.add (fx::transpose, std::vector<float> { 1.0f, 1.0f, 1.0f });
    fx::Note top { 100, 0.5f, 0.5f };
    CHECK (high.apply (top) && top.key == 127); // kept inside 0…127

    // Randomizer: the defaults change nothing; Gate 0 plays nothing; ranges hold; the draws repeat from import to import
    fx::Chain idle;
    idle.add (fx::randomizer, std::vector<float> { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f });
    idle.add (fx::randomizer, std::vector<float> { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f }); // switched off
    CHECK (idle.empty() && ! idle.random());
    fx::Chain closed;
    closed.add (fx::randomizer, std::vector<float> { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    fx::Note any { 60, 0.5f, 0.5f };
    CHECK (closed.random() && ! closed.apply (any));
    const auto draw = [] {
        fx::Chain c;
        c.add (fx::randomizer, std::vector<float> { 1.0f, 3.0f / 24.0f, 0.2f, 0.4f, 0.5f });
        std::vector<fx::Note> notes;
        int played = 0;
        for (int i = 0; i < 400; ++i)
        {
            fx::Note m { 60, 0.5f, 0.5f };
            if (c.apply (m))
                ++played, notes.push_back (m);
        }
        return std::pair { played, notes };
    };
    const auto [played, notes] = draw();
    CHECK (played > 150 && played < 250); // Gate 0.5
    bool inside = true, moved = false;
    for (const auto& m : notes)
    {
        inside = inside && m.key >= 57 && m.key <= 63 && m.velocity >= 0.3f - 1e-6f && m.velocity <= 0.7f + 1e-6f && m.pan >= 0.3f - 1e-6f && m.pan <= 0.7f + 1e-6f;
        moved = moved || m.key != 60;
    }
    CHECK (inside && moved);
    const auto again = draw();
    CHECK (again.first == played && again.second.size() == notes.size() && again.second.back().key == notes.back().key);
}

TEST (flm190bs_note_effects_and_key_ranges_on_import)
{
    using namespace lynx;
    namespace t = flm190bs;
    std::vector<float> scale { 1.0f, 0.0f, 1.0f / 33.0f, 0.0f }; // C major, Snap off: a filter
    for (int i = 0; i < 12; ++i)
        scale.push_back ((0x0AB5 >> i) & 1 ? 1.0f : 0.0f);
    const auto notes = std::vector<flm182c::Ev> { { 0, 1, 60 }, { 1, 1, 61 }, { 2, 1, 31 }, { 3, 1, 100 } };
    const auto bytes = t::song ({
        t::rack (flm::kindMaster, {}),
        // an effect, the instrument (keys 36…96), then the note effects: they still run before the instrument
        t::rack (flm::kindNormal, { flm182c::module (7, 5), t::moduleOf (1, 2, {}, 36, 96, {}, "/storage/emulated/0/Android/data/com.imageline.FLM/files/My Samples/808.wav"),
                                    t::moduleOf (48, 3, { 1.0f, 0.625f, 0.5f }), t::moduleOf (50, 4, scale),
                                    // (round 190de: the Arpeggiator switched off — an active one now bakes the rack's notes, see
                                    // FlmImport190deTests.inc)
                                    t::moduleOf (51, 6, [] { std::vector<float> v (31, 1.0f); v[0] = 0.0f; return v; }()),
                                    t::moduleOf (47, 7, std::vector<float> (16, 0.0f)) }),
        t::rack (flm::kindNormal, {}, { t::sampleOf ({ "Kick", {} }) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Keys", 1, { t::lane (0, "Keys", { t::clip (0, 4, 4, 0, 1, notes) }) }),
        t::channel (flm::kindNormal, "Kit", 2, { t::lane (3, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }) }) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const Pattern* keys = nullptr;
    const Pattern* kit = nullptr;
    for (const auto& p : song->patterns)
        (p.name == "Keys" ? keys : kit) = &p;
    CHECK (keys != nullptr && kit != nullptr);
    if (keys == nullptr || kit == nullptr)
        return;
    // 60 → 72 plays; 61 → 73 is outside C major (held back); 31 → 43 plays (the range is checked after the effects);
    // 100 → 112 is above the instrument's range
    CHECK (keys->notes.size() == 2 && keys->notes[0].key == 72 && keys->notes[1].key == 43 && keys->notes[1].start == 2 * ppq);
    CHECK (kit->notes.size() == 1 && kit->notes[0].key == 60); // drum lanes are not touched
    CHECK (report.converted.count ("FL Studio Mobile: Transpose (note effect)") == 1 && report.converted.count ("FL Studio Mobile: Scale (note effect)") == 1);
    CHECK (report.converted["FL Studio Mobile: notes a note effect holds back (silent in FLM too)"] == 1
           && report.converted["FL Studio Mobile: notes outside the instrument's key range (silent in FLM too)"] == 1);
    // the Arpeggiator and the Note Echo are switched off: nothing to say (round 190de: both are converted when on)
    CHECK (report.droppedEffects.count ("FL Studio Mobile: Arpeggiator (note effect, not converted)") == 0
           && report.droppedEffects.count ("FL Studio Mobile: Note Echo (note effect, not converted)") == 0
           && report.droppedEffects.count ("FL Studio Mobile: Transpose") == 0 && report.droppedEffects.count ("FL Studio Mobile: Scale") == 0);
    // (round 190fe: the effect before the instrument sits on an insert of its own that does not reach it)
    CHECK (report.partialEffects.count ("FL Studio Mobile: an instrument placed after an effect in its rack (the effect processes it too)") == 0
           && report.converted.count ("FL Studio Mobile: rack whose instruments and effects take turns (one insert per part)") == 1);
}

TEST (flm190bs_rack_with_several_instruments_is_a_layer)
{
    using namespace lynx;
    namespace t = flm190bs;
    const std::string device = "/storage/emulated/0/Android/data/com.imageline.FLM/files/My Samples/";
    const auto bytes = t::song ({
        t::rack (flm::kindMaster, {}),
        t::rack (flm::kindNormal, { t::moduleOf (1, 2, {}, 0, 59, {}, device + "Low.wav"), t::moduleOf (1, 3, {}, 60, 127, {}, device + "High.wav"),
                                    t::moduleOf (23, 4, std::vector<float> (8, 0.5f)) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Split", 1, { t::lane (0, "Split", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 48 }, { 1, 1, 72 } }) }) }) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* layer = flm182c::channelNamed (*song, "Split");
    CHECK (layer != nullptr && layer->instrument == InstrumentKind::layer && layer->layer.children.size() == 3 && layer->volume == 1.0f && layer->mixerTrack == 1);
    if (layer == nullptr || layer->layer.children.size() != 3)
        return;
    const auto& low = song->channels[(std::size_t) layer->layer.children[0]];
    const auto& high = song->channels[(std::size_t) layer->layer.children[1]];
    const auto& saw = song->channels[(std::size_t) layer->layer.children[2]];
    CHECK (low.instrument == InstrumentKind::sampler && low.samplePath == "E:/flm/Song/My Samples/Low.wav" && low.keyLow == 0 && low.keyHigh == 59 && low.mixerTrack == 1);
    CHECK (high.samplePath == "E:/flm/Song/My Samples/High.wav" && high.keyLow == 60 && high.keyHigh == 127);
    CHECK (low.name == "Split (DW Sampler)" && saw.name == "Split (SuperSaw)" && saw.keyLow == 0 && saw.keyHigh == 127);
    // the notes play the Layer, which hands each child the keys of its range
    CHECK (song->patterns.size() == 1 && song->patterns[0].notes.size() == 2);
    for (const auto& n : song->patterns[0].notes)
        CHECK (&song->channels[(std::size_t) n.channel] == layer);
    CHECK (report.converted.count ("FL Studio Mobile: rack with several instruments (Layer)") == 1);
    for (const auto& [what, count] : report.unsupported)
        CHECK (what.find ("second instrument") == std::string::npos);
    // heard: key 48 plays only the low sample, key 72 the high one and nothing else from the samplers
    auto quiet = *song;
    quiet.channels[(std::size_t) layer->layer.children[2]].muted = true; // (the synth stand-in)
    const auto l = t::rendered (quiet, 1.2, 0.5f, 44100);
    CHECK (std::abs (l[4410]) > 0.05f && std::abs (l[22050 + 4410]) > 0.05f);
}

TEST (flm190bs_soundfont_player_is_lynx_soundfont)
{
    using namespace lynx;
    namespace t = flm190bs;
    const auto state = [] (const std::string& path, int patch) {
        t::Bytes b;
        flm182c::lp32 (b, path);
        flm182c::u32 (b, (std::uint32_t) patch);
        return flm182c::chunk ("SF2D", b);
    };
    const auto bytes = t::song ({
        t::rack (flm::kindMaster, {}),
        t::rack (flm::kindNormal, { t::moduleOf (52, 2, { 1.0f, 0.5f, 0.25f, 0.5f, 0.5f, 1.0f }, 0, 127, state ("Resources/SoundFonts/Strings.sf2", 0)) }),
        t::rack (flm::kindNormal, { t::moduleOf (52, 2, { 1.0f, 0.5f, 0.25f, 0.5f, 0.5f, 0.0f }, 0, 127, state ("Resources/SoundFonts/Strings.sf2", 0)) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Custom", 1, { t::lane (0, "Custom", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }),
        t::channel (flm::kindNormal, "Original", 2, { t::lane (0, "Original", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* custom = flm182c::channelNamed (*song, "Custom");
    const auto* original = flm182c::channelNamed (*song, "Original");
    CHECK (custom != nullptr && original != nullptr);
    if (custom == nullptr || original == nullptr)
        return;
    CHECK (custom->instrument == InstrumentKind::soundfont && custom->samplePath == "C:/FLM/SoundFonts/Strings.sf2" && custom->mixerTrack == 1 && custom->volume == 1.0f);
    const auto p = sf::readParams (custom->modelParams);
    // Custom: T = (0.95·v + 0.05)³·10 s — attack 1.447 s; decay and release 2.5·T (FLM's −40 dB in T on a 100 dB knob)
    const auto seconds = [] (float knob) { return 0.001 * std::pow (20000.0, (double) knob); };
    CHECK (std::abs (seconds (p[sf::pAttack]) - 1.44703) < 2e-3 && std::abs (seconds (p[sf::pDecay]) - 2.5 * 0.237637) < 2e-3
           && std::abs (seconds (p[sf::pRelease]) - 2.5 * 1.44703) < 5e-3);
    CHECK (std::abs (p[sf::pSustain] - (float) std::pow (10.0, -5.0 + 5.0 * std::pow (64.0 / 127.0, 0.2))) < 1e-5f && p[sf::pVelocityCurve] == 1.0f);
    // Original: the font's own envelope (LYNX's knobs at "Preset")
    const auto o = sf::readParams (original->modelParams);
    CHECK (original->instrument == InstrumentKind::soundfont && o[sf::pAttack] == 0.0f && o[sf::pDecay] == 0.0f && o[sf::pSustain] == 0.0f && o[sf::pRelease] == 0.0f);
    CHECK (report.converted["FL Studio Mobile: SoundFont Player (LYNX Soundfont)"] == 2 && report.unsupported.count ("FL Studio Mobile: SoundFont Player") == 0
           && report.partialEffects.count ("FL Studio Mobile: SoundFont Player (level and envelope shape approximated)") == 1);
}
