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
