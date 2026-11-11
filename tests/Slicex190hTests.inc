// Round 190h, slice 1: Slicex's state, regions, key map and laws (core/Slicex.h, docs/analise/REA_rodada190h.md)

#include "core/Slicex.h"

namespace slicex190h
{
void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back ((std::uint8_t) (v >> (8 * i)));
}
void tag (std::vector<std::uint8_t>& out, const char* t) { out.insert (out.end(), t, t + 4); }
void chunk (std::vector<std::uint8_t>& out, const char* t, const std::vector<std::uint8_t>& body)
{
    tag (out, t);
    put32 (out, (std::uint32_t) body.size());
    out.insert (out.end(), body.begin(), body.end());
    if (body.size() & 1)
        out.push_back (0);
}

struct Cue
{
    std::uint32_t id, start, length; // length 0 = a marker
    const char* name;
    int key;                         // −1 = none
};

// a 16-bit mono WAV of `frames` frames with FL's cue / adtl / tlst chunks
std::vector<std::uint8_t> wavWith (std::uint32_t frames, const std::vector<Cue>& cues)
{
    std::vector<std::uint8_t> fmt, data (frames * 2, 0), cue, adtl, tlst;
    for (std::uint32_t v : { 0x00010001u, 44100u, 88200u, 0x00100002u })
        put32 (fmt, v);
    put32 (cue, (std::uint32_t) cues.size());
    tag (adtl, "adtl");
    tag (tlst, "tlst");
    std::uint32_t keyed = 0;
    for (const auto& c : cues)
        keyed += c.key >= 0 ? 1u : 0u;
    put32 (tlst, keyed);
    for (const auto& c : cues)
    {
        put32 (cue, c.id);
        put32 (cue, c.start);
        tag (cue, "data");
        put32 (cue, 0);
        put32 (cue, 0);
        put32 (cue, c.start);
        if (c.name != nullptr)
        {
            std::vector<std::uint8_t> body;
            put32 (body, c.id);
            for (const char* p = c.name; *p != 0; ++p)
                body.push_back ((std::uint8_t) *p);
            body.push_back (0);
            chunk (adtl, "labl", body);
        }
        if (c.length > 0)
        {
            std::vector<std::uint8_t> body;
            put32 (body, c.id);
            put32 (body, c.length);
            tag (body, "rgn ");
            for (int i = 0; i < 8; ++i)
                body.push_back (0);
            chunk (adtl, "ltxt", body);
        }
        if (c.key >= 0)
        {
            tag (tlst, "cue ");
            put32 (tlst, c.id - 1);
            put32 (tlst, 1);
            tlst.push_back (0xff);
            tlst.push_back ((std::uint8_t) c.key);
            tlst.push_back (0x90);
            tlst.push_back (0);
            put32 (tlst, 0);
            put32 (tlst, 0);
        }
    }
    std::vector<std::uint8_t> body;
    tag (body, "WAVE");
    chunk (body, "fmt ", fmt);
    chunk (body, "data", data);
    chunk (body, "cue ", cue);
    chunk (body, "LIST", adtl);
    if (keyed > 0)
        chunk (body, "LIST", tlst);
    std::vector<std::uint8_t> out;
    tag (out, "RIFF");
    put32 (out, (std::uint32_t) body.size());
    out.insert (out.end(), body.begin(), body.end());
    return out;
}
} // namespace slicex190h

TEST (slicex190h_fresh_state_round_trips_with_fl_defaults)
{
    using namespace lynx;
    const auto s = slicex::fresh();
    const auto bytes = slicex::write (s);
    // version, 8 masters, 47 option bytes, 3 editor ints; per deck: flag, empty path, 76 bytes, 4 two-point curves,
    // one region; 8 groups of a 20-byte header and 6 articulators
    CHECK (bytes.size() > 0x5f);
    CHECK (bytes[0] == 3 && bytes[2] == 6);
    const auto back = slicex::parse (bytes);
    CHECK (back.has_value());
    CHECK (back && *back == s);
    CHECK (back && slicex::write (*back) == bytes);
    CHECK (s.masters[slicex::mVolume] == 12800 && s.masters[slicex::mModX] == 256);
    CHECK (s.optionInt (slicex::oCrossfade) == 3 && s.optionInt (slicex::oLayering) == 0);
    CHECK (s.optionByte (slicex::oModSmooth) == 1 && s.optionByte (slicex::oVelocityToVolume) == 1 && s.optionByte (slicex::oLoopHalf) == 0);
    CHECK (s.decks[0].empty() && s.decks[1].empty());
    CHECK (s.decks[0].regions.size() == 1 && s.decks[0].regions[0] == slicex::defaultRegion());
    // the default articulators: envelopes and LFOs off, the volume envelope decays to its sustain point, the maps rest
    // at 1 (volume, speed) or 0.5
    const auto& g = s.groups[7];
    CHECK (g.filterOrder() == 0 && g.filterCut() == 128);
    CHECK (! g.art[slicex::volume].envelope.on() && ! g.art[slicex::volume].lfo.on());
    CHECK (g.art[slicex::volume].envelope.points.size() == 2 && g.art[slicex::volume].envelope.sustain == 1);
    CHECK (g.art[slicex::volume].maps[slicex::mapVelocity].points[1].y == 1.0);
    CHECK (g.art[slicex::pan].maps[slicex::mapRandom].points[0].y == 0.5);
    CHECK (g.art[slicex::sampleStart].lfoKnobs[0] == 40000 && g.art[slicex::speed].lfoKnobs[0] == 31102);
}

TEST (slicex190h_edited_state_round_trips)
{
    using namespace lynx;
    auto s = slicex::fresh();
    s.masters[slicex::mPitch] = -700;
    s.setOptionInt (slicex::oLayering, 6);
    s.setOptionByte (slicex::oLoopHalf, 1);
    slicex::setDeckFile (s, 0, "%FLStudioFactoryData%/Data/Patches/Packs/Loops/beat.wav", 3);
    CHECK (s.decks[0].regions.size() == 3 && s.decks[0].referenceOnly == 1);
    s.decks[0].regions[1][slicex::rPitch] = 300;
    s.decks[0].regions[2][slicex::rFilterGroup] = -1;
    // deck B: an embedded WAV and a path long enough for the long string form
    s.decks[1].referenceOnly = 0;
    s.decks[1].path = std::string (300, 'x');
    s.decks[1].longPath = true;
    s.decks[1].wav = slicex190h::wavWith (1000, { { 1, 0, 0, "Kick", -1 }, { 2, 500, 0, nullptr, -1 } });
    s.decks[1].regions.assign (2, slicex::defaultRegion());
    auto& vol = s.groups[2].art[slicex::volume];
    vol.envelope.enabled = -1;
    vol.envelope.points[1].dt = 0.624;
    vol.envKnobs = { 100, 90, 30, 200 };
    s.groups[2].header = { 128, 34, 0, 3, 1 };
    const auto bytes = slicex::write (s);
    const auto back = slicex::parse (bytes);
    CHECK (back.has_value());
    CHECK (back && *back == s);
    CHECK (back && back->decks[1].longPath);
    CHECK (back && slicex::write (*back) == bytes);
    // the plugin database's copy carries the plain version number: the same layout
    auto plain = s;
    plain.version = 6;
    const auto plainBack = slicex::parse (slicex::write (plain));
    CHECK (plainBack && plainBack->groups == s.groups && plainBack->decks == s.decks);
    // not a Slicex state: other versions, truncated streams
    auto other = bytes;
    other[0] = 0x25;
    other[1] = other[2] = other[3] = 0;
    CHECK (! slicex::parse (other));
    CHECK (! slicex::parse (std::vector<std::uint8_t> (bytes.begin(), bytes.begin() + 200)));
    CHECK (! slicex::parse ({}));
}

TEST (slicex190h_laws_match_fl)
{
    using namespace lynx;
    // master volume (11^x − 1)/10 with 12800 = unity; FL's demo "Ghost Break" stores 13549
    CHECK (slicex::masterVolume (12800) == 1.0f);
    CHECK (std::abs (slicex::masterVolume (13549) - 1.165699f) < 1e-6f);
    CHECK (slicex::masterVolume (0) == 0.0f);
    // randomness and LFO: the same law on |x|, with the sign
    CHECK (slicex::masterBipolar (256, 256) == 1.0f && slicex::masterBipolar (128, 128) == 1.0f);
    CHECK (slicex::masterBipolar (-128, 128) == -1.0f);
    CHECK (std::abs (slicex::masterBipolar (-64, 128) + (std::sqrt (11.0f) - 1.0f) * 0.1f) < 1e-6f);
    // region volume (3^x − 1)/2: 0, unity at 8000, +12 dB at 16000
    CHECK (slicex::regionVolume (0) == 0.0f && slicex::regionVolume (8000) == 1.0f);
    CHECK (std::abs (slicex::regionVolume (16000) - 4.0f) < 1e-5f);
    CHECK (std::abs (slicex::regionVolume (4000) - (std::sqrt (3.0f) - 1.0f) * 0.5f) < 1e-6f);
    auto rec = slicex::defaultRegion();
    auto v = slicex::regionValues (rec);
    CHECK (v.pan == 0.0f && v.volume == 1.0f && v.ampAmount == 1.0f && v.cutoff == 0.5f && v.resonance == 0.5f);
    CHECK (v.speedAmount == 8.0f && v.startAmount == 2.0f && v.delayBeats == 0.0f && v.ampGroup == 0);
    rec[slicex::rPan] = -6400;
    rec[slicex::rDelay] = 6400;
    rec[slicex::rPitch] = -1200;
    rec[slicex::rStartGroup] = -1;
    v = slicex::regionValues (rec);
    CHECK (v.pan == -1.0f && v.delayBeats == 0.5f && v.pitchCents == -1200.0f && v.startGroup == -1);
    CHECK (slicex::groupOffset (128) == 0.0f && slicex::groupOffset (0) == -0.5f && slicex::groupOffset (256) == 0.5f);
}

TEST (slicex190h_wav_regions_as_fl_reads_them)
{
    using namespace lynx;
    // markers run to the next start − 1 (the last to the sample's end); a region with a length keeps its own end;
    // names and keys come from the labels and FL's trigger list
    const auto wav = slicex190h::wavWith (4000, { { 3, 3000, 0, nullptr, -1 }, { 1, 0, 0, "Kick", -1 }, { 2, 1000, 500, "Snare", 72 } });
    std::int64_t frames = 0;
    const auto r = slicex::wavRegions (wav, frames);
    CHECK (frames == 4000);
    CHECK (r.size() == 3);
    if (r.size() != 3)
        return;
    CHECK (r[0].start == 0 && r[0].end == 999 && r[0].marker && r[0].key == -1);
    CHECK (r[1].start == 1000 && r[1].end == 1499 && ! r[1].marker && r[1].key == 72);
    CHECK (r[2].start == 3000 && r[2].end == 3999 && r[2].marker);
    CHECK (slicex::regionName (r[0], 0) == "Kick" && slicex::regionName (r[1], 1) == "Snare");
    CHECK (slicex::regionName (r[2], 2) == "Marker #3");
    slicex::WavRegion unnamed;
    unnamed.marker = false;
    CHECK (slicex::regionName (unnamed, 0) == "Region #1");
    std::int64_t none = 7;
    CHECK (slicex::wavRegions ({}, none).empty() && none == 0);
    CHECK (slicex::wavRegions (std::vector<std::uint8_t> (64, 0x41), none).empty());
}

TEST (slicex190h_key_map_follows_fl)
{
    using namespace lynx;
    auto regions = [] (int n) {
        std::vector<slicex::WavRegion> r ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            r[(std::size_t) i].start = i * 100;
        return r;
    };
    // automatic keys: 60 + index up to 65 regions, then an octave lower per 12 more
    CHECK (slicex::autoBaseKey (1) == 60 && slicex::autoBaseKey (65) == 60);
    CHECK (slicex::autoBaseKey (66) == 48 && slicex::autoBaseKey (77) == 48 && slicex::autoBaseKey (78) == 36);
    CHECK (slicex::autoBaseKey (200) == 0);
    auto a = regions (4), b = regions (2);
    auto map = slicex::keyMap (0, a, b, false);
    CHECK (map[59].empty() && map[64].empty());
    CHECK (map[60] == std::vector<std::int32_t> { 0 } && map[63] == std::vector<std::int32_t> { 3 });
    // deck B's automatic regions never reach deck A's table; on its own table they sit at 60 + index
    map = slicex::keyMap (1, a, b, false);
    CHECK (map[60] == std::vector<std::int32_t> { 0x10000 } && map[61] == std::vector<std::int32_t> { 0x10001 } && map[62].empty());
    // keyed regions of both decks layer on their note and win over an automatic region there
    a[2].key = 61;
    b[0].key = 61;
    b[1].key = 90;
    map = slicex::keyMap (0, a, b, false);
    CHECK (map[61] == (std::vector<std::int32_t> { 2, 0x10000 }));
    CHECK (map[60] == std::vector<std::int32_t> { 0 } && map[62].empty() && map[63] == std::vector<std::int32_t> { 3 });
    CHECK (map[90] == std::vector<std::int32_t> { 0x10001 });
    // "Layer regions of same deck only"
    map = slicex::keyMap (0, a, b, true);
    CHECK (map[61] == std::vector<std::int32_t> { 2 } && map[90].empty());
    map = slicex::keyMap (1, a, b, true);
    CHECK (map[61] == std::vector<std::int32_t> { 0x10000 } && map[60].empty());
}

TEST (slicex190h_fl_projects_states_rewrite_byte_for_byte)
{
    // every Slicex state in the installation's projects reads and writes back unchanged (needs FL's Data folder)
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    int seen = 0, same = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator (realFlpDir, std::filesystem::directory_options::skip_permission_denied, ec);
         ! ec && it != std::filesystem::recursive_directory_iterator(); it.increment (ec))
    {
        if (it->path().extension() != ".flp")
            continue;
        std::ifstream in (it->path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        if (std::search (bytes.begin(), bytes.end(), "Slicex", "Slicex" + 6) == bytes.end())
            continue;
        try
        {
            const auto project = flp::parseProject (bytes);
            for (const auto& c : project.channels)
            {
                auto name = c.pluginName;
                std::transform (name.begin(), name.end(), name.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
                if (name != "slicex" || c.pluginState.empty())
                    continue;
                ++seen;
                const auto s = slicex::parse (c.pluginState);
                same += s && slicex::write (*s) == c.pluginState ? 1 : 0;
            }
        }
        catch (const flp::Error&)
        {
        }
    }
    CHECK (seen == same);
}

// ---- slice 2: the voice engine (core/SlicexEngine.h)
#include "core/SlicexEngine.h"

namespace slicex190h
{
struct Rig
{
    std::shared_ptr<std::vector<float>> audio = std::make_shared<std::vector<float>>();
    lynx::slicex::State state = lynx::slicex::fresh();
    lynx::slicex::Engine engine;
    std::vector<float> left, right;

    // a 1 s mono sample at 44.1 kHz: four regions of 11025 frames, each a constant level (0.1 · (index + 1)) with a
    // fast sine on top in the second half of the sample
    lynx::slicex::DeckAudio deck (std::vector<int> keys = {}) const
    {
        lynx::slicex::DeckAudio a;
        a.owner = audio;
        a.left = audio->data();
        a.frames = (std::int64_t) audio->size();
        a.regions = lynx::slicex::regionsFromMarkers ({ 0, 11025, 22050, 33075 }, a.frames);
        for (std::size_t i = 0; i < keys.size() && i < a.regions.size(); ++i)
            a.regions[i].key = keys[i];
        return a;
    }

    Rig()
    {
        for (int i = 0; i < 44100; ++i)
            audio->push_back (0.1f * (float) (i / 11025 + 1) + (i >= 22050 ? 0.05f * (float) std::sin (i * 1.5) : 0.0f));
        state.decks[0].regions.assign (4, lynx::slicex::defaultRegion());
        engine.setRate (44100, false);
        engine.setTempo (120.0f);
    }

    void load (std::vector<int> keys = {}) { engine.setProgram (lynx::slicex::makeProgram (state, deck (std::move (keys)), {})); }

    int play (int key, int colour = 0, float cents = 0.0f)
    {
        lynx::slicex::Note n;
        n.key = key;
        n.colour = colour;
        n.pitchCents = (float) (100 * (key - 60)) + cents;
        return engine.trigger (n);
    }

    void run (int frames)
    {
        const auto at = left.size();
        left.resize (at + (std::size_t) frames, 0.0f);
        right.resize (at + (std::size_t) frames, 0.0f);
        for (int done = 0; done < frames; done += 256)
            engine.render (left.data() + at + done, right.data() + at + done, std::min (256, frames - done));
    }
};
} // namespace slicex190h

TEST (slicex190h_engine_plays_each_region_on_its_key_at_the_samples_own_pitch)
{
    using namespace lynx;
    slicex190h::Rig rig;
    rig.load();
    CHECK (rig.play (59) == -1 && rig.play (64) == -1 && rig.engine.idle());
    const int id = rig.play (61);
    CHECK (id >= 0 && rig.engine.voiceCount() == 1);
    rig.run (12000);
    // the region's own frames, unity gain on both sides, then nothing: the slice ends on its last frame
    bool exact = true;
    for (int i = 0; i < 11025; ++i)
        exact = exact && rig.left[(std::size_t) i] == (*rig.audio)[(std::size_t) (11025 + i)] && rig.right[(std::size_t) i] == rig.left[(std::size_t) i];
    CHECK (exact);
    bool silent = true;
    for (int i = 11025; i < 12000; ++i)
        silent = silent && std::abs (rig.left[(std::size_t) i]) < 1e-6f;
    CHECK (silent);
    CHECK (rig.engine.idle() && ! rig.engine.noteActive (id));
}

TEST (slicex190h_engine_note_off_cuts_the_slice_with_fls_20_ms_declick)
{
    using namespace lynx;
    slicex190h::Rig rig;
    rig.load();
    const int id = rig.play (60);
    rig.run (2000);
    rig.engine.release (id);
    rig.run (3000);
    CHECK (std::abs (rig.left[1999] - 0.1f) < 1e-6f);
    // the release starts on the next 48-frame sub-block and closes within 20 ms (882 frames)
    CHECK (std::abs (rig.left[2015] - 0.1f) < 1e-6f);
    CHECK (std::abs (rig.left[2016 + 882 + 48]) < 1e-5f);
    float peak = 0.0f;
    for (int i = 2016; i < 5000; ++i)
        peak = std::max (peak, std::abs (rig.left[(std::size_t) i]));
    CHECK (peak <= 0.1f + 1e-6f);
    CHECK (rig.engine.idle());
    // the "Sustaining" way: a volume envelope with a sustain point keeps the voice after the note off
    rig.left.clear();
    rig.right.clear();
    auto& env = rig.state.groups[0].art[slicex::volume].envelope;
    env.enabled = -1;
    env.points[1].dt = 6.0;
    env.points[1].y = 1.0;
    env.sustain = 0;
    rig.load();
    const int held = rig.play (60);
    rig.run (2000);
    rig.engine.release (held);
    rig.run (3000);
    CHECK (std::abs (rig.left[4000] - 0.1f) < 1e-4f);
    CHECK (! rig.engine.idle());
}

TEST (slicex190h_engine_region_settings_pitch_pan_volume_delay_reverse)
{
    using namespace lynx;
    slicex190h::Rig rig;
    auto& rec = rig.state.decks[0].regions;
    rec[0][slicex::rVolume] = 16000;   // ×4
    rec[1][slicex::rPan] = 6400;       // hard right
    rec[2][slicex::rDelay] = 6400;     // half a beat = 11025 frames at 120 bpm
    rig.state.masters[slicex::mVolume] = 0;
    rig.load();
    rig.play (60);
    rig.run (64);
    CHECK (rig.left[40] == 0.0f); // master volume 0
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    rig.state.masters[slicex::mVolume] = 12800;
    rig.load();
    rig.play (60);
    rig.run (1000);
    CHECK (std::abs (rig.left[500] - 0.4f) < 1e-5f && std::abs (rig.right[500] - 0.4f) < 1e-5f);
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    rig.play (61);
    rig.run (1000);
    CHECK (std::abs (rig.left[500]) < 1e-6f && std::abs (rig.right[500] - 0.2f * 1.4142135f) < 1e-5f);
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    rig.play (62);
    rig.run (12000);
    CHECK (rig.left[11024] == 0.0f && std::abs (rig.left[11025] - (*rig.audio)[22050]) < 1e-6f);
    // an octave up reads the region twice as fast: it ends after half its length
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    rig.play (60, 0, 1200.0f);
    rig.run (7000);
    CHECK (std::abs (rig.left[3000] - 0.4f) < 1e-3f && std::abs (rig.left[5400] - 0.4f) < 1e-3f && std::abs (rig.left[6000]) < 1e-5f);
    // colours 14 and 15 play the region backwards (its last frame first); odd colours play deck B (empty here)
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    CHECK (rig.play (63, 15) == -1);
    CHECK (rig.play (63, 14) >= 0);
    rig.run (400);
    CHECK (std::abs (rig.left[300] - (*rig.audio)[44099 - 300]) < 2e-3f); // (past the 4 ms start declick)
}

TEST (slicex190h_engine_layering_cut_groups_filter_and_envelope)
{
    using namespace lynx;
    slicex190h::Rig rig;
    // regions 0 and 1 share key 72: "cycle" layering plays them in turn, "all" plays both
    rig.state.setOptionInt (slicex::oLayering, 6);
    rig.state.setOptionInt (slicex::oCrossfade, 0);
    rig.load ({ 72, 72 });
    auto level = [&rig] {
        rig.left.clear(), rig.right.clear();
        rig.engine.killAll();
        rig.play (72);
        rig.run (600);
        return rig.left[500];
    };
    const float first = level(), second = level(), third = level();
    CHECK (std::abs (first - 0.1f) < 1e-5f && std::abs (second - 0.2f) < 1e-5f && std::abs (third - 0.1f) < 1e-5f);
    rig.state.setOptionInt (slicex::oLayering, 0);
    rig.load ({ 72, 72 });
    CHECK (std::abs (level() - 0.3f) < 1e-5f);
    rig.state.setOptionInt (slicex::oLayering, 1); // gain-compensated: each at 1/2
    rig.load ({ 72, 72 });
    CHECK (std::abs (level() - 0.15f) < 1e-5f);

    // a cut group: the next note of the group closes the sounding one
    for (auto& r : rig.state.decks[0].regions)
        r[slicex::rCutGroup] = 3;
    rig.load();
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    rig.play (60);
    rig.run (500);
    rig.play (61);
    CHECK (rig.engine.voiceCount() == 2);
    rig.run (2000);
    CHECK (rig.engine.voiceCount() == 1);
    CHECK (std::abs (rig.left[2400] - 0.2f) < 1e-4f);

    // the group's low-pass (order 1, cutoff wheel low) takes the fast sine off region 3 and keeps its level
    for (auto& r : rig.state.decks[0].regions)
        r[slicex::rCutGroup] = 0;
    rig.state.groups[0].header = { 128, 0, 128, 0, 1 };
    rig.load();
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    rig.play (62);
    rig.run (8000);
    float lo = 1.0f, hi = 0.0f;
    for (int i = 6000; i < 8000; ++i)
        lo = std::min (lo, rig.left[(std::size_t) i]), hi = std::max (hi, rig.left[(std::size_t) i]);
    CHECK (hi - lo < 0.01f && std::abs ((hi + lo) * 0.5f - 0.3f) < 0.01f);

    // a volume envelope: 1 → 0 over 0.5 units (0.25 s) with no sustain point; the voice ends with it
    rig.state.groups[0].header = { 128, 128, 128, 0, 0 };
    auto& env = rig.state.groups[0].art[slicex::volume].envelope;
    env.enabled = -1;
    env.points[1].dt = 0.5;
    env.points[1].tension = 0.0f;
    env.sustain = -1;
    rig.load();
    rig.left.clear(), rig.right.clear();
    rig.engine.killAll();
    rig.play (60);
    rig.run (11025);
    CHECK (std::abs (rig.left[10] - 0.1f) < 1e-3f);
    CHECK (std::abs (rig.left[5512] - 0.05f) < 2e-3f); // half way down (a straight line)
    CHECK (std::abs (rig.left[11000]) < 1e-3f); // (the gains ramp over each 48-frame sub-block: one sub-block behind)
}
