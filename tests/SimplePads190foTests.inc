// --- Round 190fo: the Simple layout's Sampler track — 16 pads (core/SimplePads.h) — and a folder of stems and MIDI files
// brought in at once (core/SimpleTracks.h folderPlan). The render tests play pads on the real render path (RackState /
// RackRenderer) and check the modes, the tone, the choke groups and that a pad is the same sound as an ordinary Sampler
// channel with the same settings (nothing new in the DSP: the plugin quality gate's "no level change"); LYNX_190FO_REPORT=1
// prints the numbers.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>

#include "core/RackRenderer.h"
#include "core/SimplePads.h"
#include "core/SimpleTracks.h"
#include "core/EffectPresets.h"

namespace simple190fo
{
using namespace lynx;

inline bool report() { return std::getenv ("LYNX_190FO_REPORT") != nullptr; }

// a Sampler track with its 16 pads on a default song
inline Song padsSong (simpletracks::Result& r)
{
    Song s = Song::makeDefault();
    s.playlist.clear();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::pads;
    q.kit = simplepads::emptyKit();
    Pattern p;
    p.lengthTicks = 4 * s.barTicks();
    q.pattern = p;
    r = simpletracks::addTrack (s, q);
    return s;
}

// test sounds: "tone" = 0.2 s of a 1 kHz sine with a 3 kHz partial, "hat" = 0.5 s of steady noise, at 48 kHz
inline std::shared_ptr<const SampleData> sound (const std::string& name)
{
    auto d = std::make_shared<SampleData>();
    d->sampleRate = 48000.0;
    if (name == "hat")
    {
        d->left.resize (24000);
        std::uint32_t seed = 1;
        for (auto& v : d->left)
        {
            seed = seed * 1664525u + 1013904223u;
            v = 0.3f * ((float) (seed >> 8) / 8388608.0f - 1.0f);
        }
    }
    else
    {
        d->left.resize (9600);
        for (std::size_t i = 0; i < d->left.size(); ++i)
        {
            const auto t = (double) i / 48000.0;
            d->left[i] = (float) (0.4 * std::sin (2.0 * 3.141592653589793 * 1000.0 * t) + 0.2 * std::sin (2.0 * 3.141592653589793 * 3000.0 * t));
        }
    }
    return d;
}

struct Render
{
    std::vector<float> out;
    double nsPerSample = 0.0;
};

// plays `song` from its start for `seconds` (Song mode)
inline Render play (Song s, double seconds)
{
    s.mode = PlayMode::song;
    constexpr double rate = 48000.0;
    RackState::Factories f;
    f.samples = [] (const std::string& path) { return sound (path); };
    auto state = RackState::build (s, nullptr, rate, f);
    RackRenderer renderer;
    constexpr int block = 256;
    renderer.prepare (rate, block);
    const auto tps = ticksPerSample (s.bpm, rate);
    Render r;
    std::vector<float> l (block), rr (block);
    double tick = 0.0;
    std::int64_t ns = 0;
    const auto blocks = (int) (seconds * rate / block);
    for (int b = 0; b < blocks; ++b, tick += block * tps)
    {
        const auto t0 = std::chrono::steady_clock::now();
        renderer.process (state.get(), true, tick, block, l.data(), rr.data());
        ns += std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
        r.out.insert (r.out.end(), l.begin(), l.end());
    }
    r.nsPerSample = (double) ns / (double) (blocks * block);
    return r;
}

inline double rms (const std::vector<float>& x, double from, double to)
{
    const auto a = (std::size_t) (from * 48000.0), b = std::min (x.size(), (std::size_t) (to * 48000.0));
    double sum = 0.0;
    for (auto i = a; i < b; ++i)
        sum += (double) x[i] * x[i];
    return b > a ? std::sqrt (sum / (double) (b - a)) : 0.0;
}
inline double db (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

// energy of the 3 kHz partial against the 1 kHz one, over [from, from + 4096) (Goertzel)
inline double partialRatioDb (const std::vector<float>& x, double from)
{
    const auto goertzel = [&] (double hz) {
        const auto a = (std::size_t) (from * 48000.0);
        const auto k = 2.0 * std::cos (2.0 * 3.141592653589793 * hz / 48000.0);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = a; i < a + 4096 && i < x.size(); ++i)
        {
            const auto s0 = x[i] + k * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return std::sqrt (s1 * s1 + s2 * s2 - k * s1 * s2);
    };
    return db (goertzel (3000.0)) - db (goertzel (1000.0));
}

// one note on pad `pad`'s channel
inline void hitPad (Song& s, const simpletracks::Result& r, int pad, std::int64_t start, std::int64_t length)
{
    Note n;
    n.channel = simplepads::padChannel (s, r.track, pad);
    n.key = simplepads::padKey;
    n.start = start;
    n.length = length;
    n.velocity = 0.78f;
    s.patterns[(std::size_t) r.pattern].notes.push_back (n);
}
} // namespace simple190fo

TEST (simple190fo_pads_track_shape)
{
    using namespace lynx;
    namespace st = simpletracks;
    namespace sp = simplepads;
    st::Result r;
    auto s = simple190fo::padsSong (r);
    CHECK (r.ok() && r.channels.size() == (std::size_t) sp::padCount && r.pattern >= 0 && r.insert > 0);
    if (! r.ok())
        return;
    CHECK (st::kindOf (s, r.track) == st::Kind::pads);
    CHECK (s.playlistTracks[(std::size_t) r.track].mode == PlaylistTrackMode::audio); // (FL's audio track, as Drums)
    CHECK (s.playlistTracks[(std::size_t) r.track].name == "Sampler");
    const auto chans = sp::padChannels (s, r.track);
    for (int p = 0; p < sp::padCount; ++p)
    {
        const auto c = chans[(std::size_t) p];
        CHECK (c >= 0 && sp::padChannel (s, r.track, p) == c);
        if (c < 0)
            continue;
        const auto& ch = s.channels[(std::size_t) c];
        CHECK (ch.instrument == InstrumentKind::sampler && ch.mixerTrack == r.insert && ch.group == r.group);
        CHECK (ch.samplePath.empty() && ! sp::hasSample (s, r.track, p));
        CHECK (ch.name == "Pad " + std::to_string (p + 1));
    }
    CHECK (sp::padChannel (s, r.track, 16) == -1 && sp::padChannel (s, r.track, -1) == -1);
    // a Drums track stays a Drums track; a pad channel elsewhere is not a pad of this track
    st::Request d;
    d.kind = st::Kind::drums;
    ChannelDef kick;
    kick.instrument = InstrumentKind::sampler;
    d.kit = { kick };
    const auto rd = st::addTrack (s, d);
    CHECK (rd.ok() && st::kindOf (s, rd.track) == st::Kind::drums && sp::padChannel (s, rd.track, 0) == -1);
    // rename and recolour reach the pads' group
    st::rename (s, r.track, "Chops");
    CHECK (s.channelGroups[(std::size_t) r.group] == "Chops");
    st::recolour (s, r.track, 0xFF112233u);
    CHECK (s.channels[(std::size_t) chans[3]].colour == 0xFF112233u);
    // a second Sampler track: its own insert, so its choke groups are its own
    st::Result r2;
    {
        st::Request q;
        q.kind = st::Kind::pads;
        q.kit = sp::emptyKit();
        r2 = st::addTrack (s, q);
    }
    CHECK (r2.ok() && r2.insert != r.insert);
    auto& a = s.channels[(std::size_t) sp::padChannel (s, r.track, 0)];
    auto& b = s.channels[(std::size_t) sp::padChannel (s, r2.track, 0)];
    a.simplePad.group = b.simplePad.group = 1;
    sp::apply (a, 0);
    sp::apply (b, 0);
    CHECK (a.cutGroup > 0 && a.cutGroup == a.cutBy && b.cutGroup > 0 && a.cutGroup != b.cutGroup);
}

TEST (simple190fo_pad_keys_tone_and_modes)
{
    using namespace lynx;
    namespace sp = simplepads;
    // the keys: four rows, bottom to top, and back
    const std::string rows = "VBNMFGHJRTYU4567";
    for (int p = 0; p < sp::padCount; ++p)
    {
        CHECK (sp::typingKey (p) == rows[(std::size_t) p]);
        CHECK (sp::padForKey (rows[(std::size_t) p]) == p);
        if (std::isalpha ((unsigned char) rows[(std::size_t) p]))
            CHECK (sp::padForKey (std::tolower ((unsigned char) rows[(std::size_t) p])) == p);
    }
    CHECK (sp::padForKey ('Z') == -1 && sp::padForKey ('8') == -1 && sp::typingKey (16) == 0);
    // tone: off at 0, low-pass 22.05 kHz → 100 Hz, high-pass 20 Hz → 8 kHz, even per octave
    CHECK (! sp::toneFilter (0.0f).on && sp::toneText (0.0f) == "Off");
    CHECK (sp::toneFilter (-1.0f).filterType == 0 && std::abs (sp::toneFilter (-1.0f).cutoff - 100.0f) < 0.5f);
    CHECK (std::abs (sp::toneFilter (-0.5f).cutoff - std::sqrt (22050.0f * 100.0f)) < 1.0f);
    CHECK (sp::toneFilter (1.0f).filterType == 2 && std::abs (sp::toneFilter (1.0f).cutoff - 8000.0f) < 1.0f);
    CHECK (sp::toneFilter (0.5f).cutoff > 300.0f && sp::toneFilter (0.5f).cutoff < 500.0f);
    CHECK (sp::toneText (-1.0f) == "Low-pass 100 Hz" && sp::toneText (1.0f) == "High-pass 8.0 kHz");
    // modes on the channel: gate = a release of 5 ms at least, one-shot = the Sampler's one-shot, loop = one zone
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.samplePath = "tone";
    c.mixerTrack = 3;
    c.simplePad.index = 0;
    sp::apply (c, 9600);
    CHECK (sp::modeOf (c) == sp::Mode::gate && c.sampler.release >= 0.005f && c.zones.empty());
    c.simplePad.release = 0.3f;
    sp::apply (c, 9600);
    CHECK (std::abs (c.sampler.release - 0.3f) < 1.0e-6f);
    c.simplePad.mode = 1;
    sp::apply (c, 9600);
    CHECK (c.sampler.release == 0.0f && c.zones.empty());
    c.simplePad.mode = 2;
    c.sampler.regionStart = 0.25f;
    c.sampler.regionEnd = 0.75f;
    sp::apply (c, 9600);
    CHECK (c.zones.size() == 1 && c.zones[0].loopMode == 2 && c.zones[0].loopStart == 2400 && c.zones[0].loopEnd == 7199
           && c.zones[0].rootKey == sp::padKey && c.zones[0].path == "tone");
    sp::apply (c, 0); // (length unknown: plays as a gate pad)
    CHECK (c.zones.empty());
    c.simplePad.mode = 0;
    // ranges are kept
    c.sampler.pitch = 30.0f;
    c.sampler.attack = 5.0f;
    c.simplePad.release = 9.0f;
    c.simplePad.group = 7;
    sp::apply (c, 9600);
    CHECK (c.sampler.pitch == sp::maxPitch && c.sampler.attack == sp::maxAttack && c.simplePad.release == sp::maxRelease && c.simplePad.group == 4);
    CHECK (std::abs (sp::normalizeGainDb (0.5f) - 6.0206f) < 0.01f && sp::normalizeGainDb (0.0f) == 0.0f && sp::normalizeGainDb (0.001f) == 24.0f);
}

TEST (simple190fo_pad_edits)
{
    using namespace lynx;
    namespace sp = simplepads;
    simpletracks::Result r;
    auto s = simple190fo::padsSong (r);
    if (! r.ok())
        return;
    CHECK (sp::loadSample (s, r.track, 2, "/kits/snare.wav", "snare", 9600));
    const auto c2 = sp::padChannel (s, r.track, 2);
    CHECK (s.channels[(std::size_t) c2].samplePath == "/kits/snare.wav" && s.channels[(std::size_t) c2].name == "snare" && sp::hasSample (s, r.track, 2));
    auto& ch = s.channels[(std::size_t) c2];
    ch.simplePad.mode = 1;
    ch.volume = 0.5f;
    ch.sampler.pitch = 3.0f;
    // a new sample keeps the pad's mode (length unknown) and volume, resets pitch and region
    CHECK (sp::loadSample (s, r.track, 2, "/kits/clap.wav", "clap", 9600));
    CHECK (s.channels[(std::size_t) c2].simplePad.mode == 1 && s.channels[(std::size_t) c2].volume == 0.5f && s.channels[(std::size_t) c2].sampler.pitch == 0.0f);
    // with its length known: a short hit plays One-shot, a longer sound Gate
    CHECK (sp::loadSample (s, r.track, 2, "/kits/pad.wav", "pad", 96000, 2.0) && s.channels[(std::size_t) c2].simplePad.mode == 0);
    CHECK (sp::loadSample (s, r.track, 2, "/kits/clap.wav", "clap", 9600, 0.2) && s.channels[(std::size_t) c2].simplePad.mode == 1);
    // copy, swap (the notes stay on their channels: the music doesn't change), clear
    CHECK (sp::copyPad (s, r.track, 2, 9, 9600));
    const auto c9 = sp::padChannel (s, r.track, 9);
    CHECK (c9 != c2 && s.channels[(std::size_t) c9].samplePath == "/kits/clap.wav" && s.channels[(std::size_t) c9].simplePad.index == 9
           && s.channels[(std::size_t) c9].volume == 0.5f);
    CHECK (sp::swapPads (s, r.track, 2, 5));
    CHECK (sp::padChannel (s, r.track, 5) == c2 && s.channels[(std::size_t) c2].simplePad.index == 5);
    CHECK (sp::clearPad (s, r.track, 5));
    CHECK (s.channels[(std::size_t) c2].samplePath.empty() && s.channels[(std::size_t) c2].simplePad.index == 5 && s.channels[(std::size_t) c2].mixerTrack == r.insert
           && s.channels[(std::size_t) c2].group == r.group && s.channels[(std::size_t) c2].name == "Pad 6");
    CHECK (! sp::copyPad (s, r.track, 3, 3, 0) && ! sp::swapPads (s, r.track, 3, 99) && ! sp::loadSample (s, 99, 0, "x", "x", 0));
    CHECK (sp::padChannels (s, r.track).size() == (std::size_t) sp::padCount);
}

TEST (simple190fo_pads_play_on_the_render_path)
{
    using namespace lynx;
    namespace sp = simplepads;
    using simple190fo::rms;
    using simple190fo::db;
    const auto beat = (std::int64_t) ppq; // 0.5 s at 120 BPM
    simpletracks::Result r;
    auto base = simple190fo::padsSong (r);
    if (! r.ok())
        return;
    sp::loadSample (base, r.track, 0, "tone", "tone", 9600);
    sp::loadSample (base, r.track, 1, "hat", "hat", 24000);
    sp::loadSample (base, r.track, 2, "hat", "hat open", 24000);
    const auto ch = [&] (Song& s, int pad) -> ChannelDef& { return s.channels[(std::size_t) sp::padChannel (s, r.track, pad)]; };

    // gate: a 0.05 s note on a 0.2 s tone stops early; one-shot plays the whole 0.2 s
    auto gate = base;
    simple190fo::hitPad (gate, r, 0, 0, beat / 10);
    const auto g = simple190fo::play (gate, 0.6);
    auto oneShot = gate;
    ch (oneShot, 0).simplePad.mode = 1;
    sp::apply (ch (oneShot, 0), 9600);
    const auto o = simple190fo::play (oneShot, 0.6);
    const auto gateTail = db (rms (g.out, 0.10, 0.18)), oneTail = db (rms (o.out, 0.10, 0.18)), gateHead = db (rms (g.out, 0.005, 0.04));
    CHECK (gateHead > -40.0 && gateTail < gateHead - 40.0); // (stopped with the note)
    CHECK (oneTail > gateHead - 3.0);                      // (still sounding at full level)
    CHECK (db (rms (o.out, 0.25, 0.5)) < -80.0);            // (and over after its 0.2 s)

    // loop: a held note keeps the 0.2 s tone sounding for a whole second
    auto loop = base;
    simple190fo::hitPad (loop, r, 0, 0, beat * 2);
    ch (loop, 0).simplePad.mode = 2;
    sp::apply (ch (loop, 0), 9600);
    const auto l = simple190fo::play (loop, 1.4);
    const auto loopMid = db (rms (l.out, 0.6, 0.9)), loopAfter = db (rms (l.out, 1.1, 1.3));
    CHECK (loopMid > gateHead - 3.0 && loopAfter < -60.0);

    // a pad is an ordinary Sampler channel: the same settings give the same samples (no new DSP)
    auto plain = gate;
    {
        auto& c = ch (plain, 0);
        c.simplePad = {}; // not a pad any more (the track's kind no longer matters to the renderer)
    }
    const auto p = simple190fo::play (plain, 0.6);
    double diff = 0.0;
    for (std::size_t i = 0; i < std::min (p.out.size(), g.out.size()); ++i)
        diff = std::max (diff, (double) std::abs (p.out[i] - g.out[i]));
    CHECK (diff < 1.0e-6);

    // tone: the low-pass halfway takes the 3 kHz partial down, the high-pass takes the 1 kHz one down
    auto dark = oneShot;
    ch (dark, 0).simplePad.tone = -0.6f; // (≈ 1.0 kHz low-pass)
    sp::apply (ch (dark, 0), 9600);
    auto thin = oneShot;
    ch (thin, 0).simplePad.tone = 0.8f; // (≈ 2.4 kHz high-pass)
    sp::apply (ch (thin, 0), 9600);
    const auto open = simple190fo::partialRatioDb (o.out, 0.05);
    const auto darker = simple190fo::partialRatioDb (simple190fo::play (dark, 0.3).out, 0.05);
    const auto thinner = simple190fo::partialRatioDb (simple190fo::play (thin, 0.3).out, 0.05);
    CHECK (darker < open - 6.0 && thinner > open + 6.0);

    // choke: pad 1 (closed hat) then pad 2 (open hat) in group 1: the first is cut when the second starts
    auto choke = base;
    for (auto pad : { 1, 2 })
    {
        ch (choke, pad).simplePad.mode = 1; // (one-shot: the hats ring on after their notes)
        sp::apply (ch (choke, pad), 24000);
    }
    simple190fo::hitPad (choke, r, 1, 0, beat / 4);
    auto free = choke;
    simple190fo::hitPad (choke, r, 2, beat / 2, beat / 4); // (at 0.25 s; pad 2 alone after it)
    for (auto pad : { 1, 2 })
    {
        ch (choke, pad).simplePad.group = 1;
        sp::apply (ch (choke, pad), 24000);
    }
    // pad 1 alone, and pad 1 then pad 2 in one group: from 0.3 s the choked render must equal pad 2's alone
    auto onlyTwo = base;
    ch (onlyTwo, 2).simplePad.mode = 1;
    sp::apply (ch (onlyTwo, 2), 24000);
    simple190fo::hitPad (onlyTwo, r, 2, beat / 2, beat / 4);
    const auto c = simple190fo::play (choke, 0.7), two = simple190fo::play (onlyTwo, 0.7), one = simple190fo::play (free, 0.7);
    double chokeDiff = 0.0, freeLevel = 0.0;
    for (auto i = (std::size_t) (0.30 * 48000.0); i < (std::size_t) (0.45 * 48000.0) && i < c.out.size(); ++i)
    {
        chokeDiff = std::max (chokeDiff, (double) std::abs (c.out[i] - two.out[i]));
        freeLevel = std::max (freeLevel, (double) std::abs (one.out[i]));
    }
    CHECK (freeLevel > 0.05);          // (without the group, pad 1 still sounds there)
    CHECK (chokeDiff < freeLevel * 0.05); // (with it, only pad 2 does)

    if (simple190fo::report())
        std::printf ("  190fo pads: gate head %.1f dB tail %.1f dB, one-shot tail %.1f dB, loop mid %.1f dB after %.1f dB, plain-vs-pad max diff %.2e,"
                     " 3k/1k open %.1f dark %.1f thin %.1f dB, choke residue %.4f (free %.4f), CPU %.3f / %.3f us per sample (one pad / plain)\n",
                     gateHead, gateTail, oneTail, loopMid, loopAfter, diff, open, darker, thinner, chokeDiff, freeLevel, g.nsPerSample / 1000.0,
                     p.nsPerSample / 1000.0);
}

TEST (simple190fo_folder_of_stems_and_midi)
{
    using namespace lynx;
    namespace st = simpletracks;
    CHECK (st::stemTrackName ("03_Lead Vox") == "Lead Vox");
    CHECK (st::stemTrackName ("01 Drums") == "Drums");
    CHECK (st::stemTrackName ("12-Bass") == "Bass");
    CHECK (st::stemTrackName ("2024") == "2024" && st::stemTrackName ("Kick 808") == "Kick 808" && st::stemTrackName ("1999_") == "1999_");
    // the other app's "download stems": NN_name, in number order (10 after 9)
    auto bl = st::folderPlan ({ "10_Pad.wav", "02_Bass.m4a", "01_Drums.wav", "09_Keys.wav", "notes.txt", ".DS_Store" });
    CHECK (bl.size() == 4);
    if (bl.size() == 4)
        CHECK (bl[0].name == "Drums" && bl[1].name == "Bass" && bl[2].name == "Keys" && bl[3].name == "Pad" && ! bl[0].midi && ! bl[0].muted);
    // LYNX's own export for other apps: Stems/ and MIDI/, the "(all tracks)" file and the mixdown left out, a MIDI file
    // whose stem is there comes in muted, one with no stem doesn't
    auto ly = st::folderPlan ({ "MIDI/02 Keys.mid", "Stems/01 Drums.wav", "Stems/02 Keys.wav", "MIDI/My song (all tracks).mid", "My song - mixdown.wav",
                                "README.txt", "MIDI/03 Lead.mid" });
    CHECK (ly.size() == 4);
    if (ly.size() == 4)
    {
        CHECK (ly[0].path == "Stems/01 Drums.wav" && ly[1].path == "Stems/02 Keys.wav"); // (audio first)
        CHECK (ly[2].midi && ly[2].name == "Keys" && ly[2].muted);
        CHECK (ly[3].midi && ly[3].name == "Lead" && ! ly[3].muted);
    }
    CHECK (st::folderPlan ({}).empty() && st::folderPlan ({ "a.txt", "b.pdf" }).empty());
}

TEST (simple190fo_shared_reverb_and_reset_levels)
{
    using namespace lynx;
    namespace st = simpletracks;
    Song s = Song::makeDefault();
    s.playlist.clear();
    st::Request voice;
    voice.kind = st::Kind::voice;
    const auto a = st::addTrack (s, voice);
    st::Request keys;
    keys.kind = st::Kind::instrument;
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    keys.channel = synth;
    const auto b = st::addTrack (s, keys);
    CHECK (a.ok() && b.ok() && st::sharedReverb (s) == -1 && st::reverbSend (s, a.insert) == 0.0f);
    // the first send makes the shared "Reverb" insert (Atrium fully wet), the second reuses it
    CHECK (st::setReverbSend (s, a.insert, 0.4f));
    const auto rv = st::sharedReverb (s);
    CHECK (rv > 0 && rv != a.insert && rv != b.insert && s.mixer[(std::size_t) rv].name == "Reverb");
    if (rv <= 0)
        return;
    const auto mix = presets::paramByName (EffectKind::atrium, "Mix");
    CHECK (mix >= 0 && s.mixer[(std::size_t) rv].effects.size() == 1 && s.mixer[(std::size_t) rv].effects[0].params[(std::size_t) mix] == 1.0f);
    CHECK (std::abs (st::reverbSend (s, a.insert) - 0.4f) < 1.0e-6f);
    CHECK (st::setReverbSend (s, b.insert, 0.2f) && st::sharedReverb (s) == rv && std::abs (st::reverbSend (s, b.insert) - 0.2f) < 1.0e-6f);
    CHECK (st::setReverbSend (s, a.insert, 0.7f) && s.mixer[(std::size_t) a.insert].sends.size() == 1); // (one send, moved)
    CHECK (st::setReverbSend (s, a.insert, 0.0f) && st::reverbSend (s, a.insert) == 0.0f && s.mixer[(std::size_t) a.insert].sends.empty());
    CHECK (! st::setReverbSend (s, rv, 0.5f)); // (not into itself)
    // a new track never takes the Reverb insert as its own
    const auto c = st::addTrack (s, voice);
    CHECK (c.ok() && c.insert != rv);
    // reset: levels and pans back, the sends stay
    s.mixer[(std::size_t) a.insert].volume = 0.3f;
    s.mixer[(std::size_t) b.insert].pan = -0.5f;
    CHECK (st::resetLevels (s) == 2);
    CHECK (s.mixer[(std::size_t) a.insert].volume == unityFader && s.mixer[(std::size_t) b.insert].pan == 0.0f && st::reverbSend (s, b.insert) > 0.0f);
    CHECK (st::resetLevels (s) == 0);
}
