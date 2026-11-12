// --- FL Studio Mobile (.flm) import, round 190de: the module wrapper on instruments, automation of instrument
// parameters and of the rack's Pitch Bend, a kit sample's stretch and pitch, an audio clip's envelope and Sample Start
// (docs/analise/FLM_FORMAT.md §6, §6.1, §7.4). Every project is written by the tests; no file of Image-Line's is
// committed.

#include "core/MidiControls.h"
#include "core/ModelParams.h"

namespace flm190de
{
using namespace flm182c;
using Bytes = flm182c::Bytes;

// a module with its own parameter values and its wrapper values (SMPR: Post Gain, Mix, Wet Pan)
inline Bytes moduleWith (std::uint32_t type, std::uint32_t id, const std::vector<float>& params, const std::vector<float>& smpr = {},
                         const Bytes& custom = {}, const std::string& presetFile = {}, int keyLow = 0, int keyHigh = 127)
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
    if (! smpr.empty())
    {
        Bytes s;
        for (const auto v : smpr)
            f32 (s, v);
        const auto c = chunk ("SMPR", s);
        put (body, c.data(), c.size());
    }
    if (! custom.empty())
    {
        const auto c = chunk ("CSTM", custom);
        put (body, c.data(), c.size());
    }
    return chunk ("RMOd", body);
}

inline std::vector<float> superSawDefaults()
{
    std::vector<float> p (60, 0.5f);
    p[0] = 1.0f;
    return p;
}

inline const lynx::Automation* automationOf (const lynx::Song& s, lynx::AutomationTargetKind kind, int index, int param = -1)
{
    for (const auto& a : s.automations)
        if (a.target.kind == kind && a.target.index == index && (param < 0 || a.target.param == param))
            return &a;
    return nullptr;
}
} // namespace flm190de

TEST (flm190de_instrument_wrapper_and_parameter_lanes)
{
    using namespace lynx;
    namespace t = flm190de;
    namespace rp = flm::rackparam;
    auto off = t::superSawDefaults();
    off[0] = 0.0f;
    // rack 1: a SuperSaw switched off; rack 2: a SuperSaw at Post Gain 0.35 (−6 dB) and Wet Pan 0.75, its Filter Cutoff
    // (19) and Enable (0) automated; rack 3: two SuperSaws (a Layer) whose Pitch Bend is automated
    const auto bytes = flm190bs::song ({
        t::rack (flm::kindMaster, {}),
        t::rack (flm::kindNormal, { t::moduleWith (23, 2, off) }),
        t::rack (flm::kindNormal, { t::moduleWith (23, 2, t::superSawDefaults(), { 0.35f, 1.0f, 0.75f }) }),
        t::rack (flm::kindNormal, { t::moduleWith (23, 2, t::superSawDefaults()), t::moduleWith (23, 3, t::superSawDefaults()) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Off", 1, { t::lane (0, "Off", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }),
        t::channel (flm::kindNormal, "Saw", 2, { t::lane (0, "Saw", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                 t::lane (1, "Filter Cutoff : SuperSaw", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 0 }, { 2, -1, 32768 }, { 4, 1, 65535 } }) }, 2, 19),
                                                 t::lane (1, "Enable : SuperSaw", { flm187d::autoClip (4, 4, 4, 2, { { 0, 1, 0 }, { 2, 1, 65535 } }) }, 2, 0) }),
        t::channel (flm::kindNormal, "Pair", 3, { t::lane (0, "Pair", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                  t::lane (1, "Pitch Bend", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 32768 }, { 2, 1, 49151 } }) }, 0, rp::pitchBend),
                                                  t::lane (1, "Aftertouch", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 0 } }) }, 0, rp::aftertouch) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto index = [&] (const std::string& n) {
        for (std::size_t i = 0; i < s->channels.size(); ++i)
            if (s->channels[i].name == n)
                return (int) i;
        return -1;
    };
    const auto offAt = index ("Off"), sawAt = index ("Saw"), pairAt = index ("Pair");
    CHECK (offAt >= 0 && sawAt >= 0 && pairAt >= 0);
    if (offAt < 0 || sawAt < 0 || pairAt < 0)
        return;
    const auto& offC = s->channels[(std::size_t) offAt];
    const auto& saw = s->channels[(std::size_t) sawAt];
    // the wrapper: Enable off = not rendered (muted); Post Gain × Mix on the level, Wet Pan on the pan
    CHECK (offC.instrument == InstrumentKind::pocketSuperSaw && offC.muted);
    CHECK (saw.instrument == InstrumentKind::pocketSuperSaw && ! saw.muted); // (its Enable is automated: the lane drives it)
    CHECK (std::abs (channelGain (saw.volume) - (float) std::pow (10.0, 2.0 * 0.35 - 1.0)) < 1e-4f && std::abs (saw.pan - 0.5f) < 1e-6f);
    // Filter Cutoff (FLM 19) = Pocket SuperSaw's value 18, FLM's own 0…1 through the engine's range
    const auto* p = modelparams::find (InstrumentKind::pocketSuperSaw, 18);
    const auto* cutoff = t::automationOf (*s, AutomationTargetKind::channelParam, sawAt, 18);
    CHECK (p != nullptr && cutoff != nullptr && cutoff->points.size() == 3);
    if (p != nullptr && cutoff != nullptr && cutoff->points.size() == 3)
        CHECK (std::abs (cutoff->points[0].value - p->normalised (0.0f)) < 1e-6f && std::abs (cutoff->points[1].value - p->normalised (32768.0f / 65535.0f)) < 1e-6f
               && cutoff->points[1].tick == 2 * ppq && std::abs (cutoff->points[2].value - p->normalised (1.0f)) < 1e-6f);
    // Enable: the channel's volume, silent below 0.5, the channel's own volume above
    const auto* enable = t::automationOf (*s, AutomationTargetKind::channelVolume, sawAt);
    CHECK (enable != nullptr && enable->points.size() == 2);
    if (enable != nullptr && enable->points.size() == 2)
        CHECK (enable->points[0].value == 0.0f && enable->points[1].value == saw.volume);
    bool enableClip = false;
    for (const auto& c : s->playlist)
        enableClip = enableClip || (c.kind == ClipKind::automation && c.start == 4 * ppq && s->automations[(std::size_t) c.automation].target.kind == AutomationTargetKind::channelVolume);
    CHECK (enableClip);
    // Pitch Bend: on both children of the Layer, LYNX's bend value as it is; the bend range is SuperSaw's own,
    // int(11·P37 + 1) = 6 semitones at 0.5
    const auto& layer = s->channels[(std::size_t) pairAt];
    CHECK (layer.instrument == InstrumentKind::layer && layer.layer.children.size() == 2);
    for (const auto child : layer.layer.children)
    {
        const auto* b = t::automationOf (*s, AutomationTargetKind::channelParam, child, midictl::pitchBendBase);
        CHECK (b != nullptr && s->channels[(std::size_t) child].misc.pitchRange == 6);
        if (b != nullptr && b->points.size() == 2)
            CHECK (std::abs (b->points[0].value - 32768.0f / 65535.0f) < 1e-6f && std::abs (b->points[1].value - 49151.0f / 65535.0f) < 1e-6f);
    }
    CHECK (report.converted.count ("FL Studio Mobile: instrument parameter automation") == 1
           && report.converted["FL Studio Mobile: instrument parameter automation"] == 2);
    CHECK (report.converted.count ("FL Studio Mobile: Pitch Bend automation") == 1 && report.converted.count ("FL Studio Mobile: switched-off instrument (muted)") == 1);
    CHECK (report.partialEffects.count ("FL Studio Mobile: instrument Enable automation (the channel's volume on or off)") == 1);
    // (round 190dw: the aftertouch lane reaches no FLM module, so there is nothing to import)
    CHECK (report.converted.count ("FL Studio Mobile: Aftertouch automation (no FL Studio Mobile instrument responds to it)") == 1
           && report.unsupported.count ("FL Studio Mobile: Pitch Bend/Aftertouch automation (not imported)") == 0);
    CHECK (report.unsupported.count ("FL Studio Mobile: automation of module parameters (not imported yet)") == 0);
    // rows: the cutoff and enable lanes on the Saw, one row per bent child
    int rows = 0;
    for (const auto& r : s->playlistTracks)
        rows += r.name.rfind ("Pair Pitch Bend", 0) == 0;
    CHECK (rows == 2);
}

// The rack's Pitch Bend plays: a kit pad of one second at +12 semitones (v = 0.75) ends after half a second
TEST (flm190de_pitch_bend_lane_plays)
{
    using namespace lynx;
    namespace t = flm190de;
    namespace rp = flm::rackparam;
    const auto make = [&] (bool bend) {
        return flm190bs::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, {}, { flm190bs::sampleOf ({ "Hit", {} }) }),
                                 t::channel (flm::kindMaster, "MASTER", 0, {}),
                                 bend ? t::channel (flm::kindNormal, "Kit", 1, { t::lane (3, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }),
                                                                                 t::lane (1, "Pitch Bend", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 49151 } }) }, 0, rp::pitchBend) })
                                      : t::channel (flm::kindNormal, "Kit", 1, { t::lane (3, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }) }) });
    };
    const auto plain = importFlm (make (false), flm190bs::anyFile (nullptr));
    const auto bent = importFlm (make (true), flm190bs::anyFile (nullptr));
    CHECK (plain.has_value() && bent.has_value());
    if (! plain || ! bent)
        return;
    const auto a = flm190bs::rendered (*plain, 1.2);
    const auto b = flm190bs::rendered (*bent, 1.2);
    const auto lastSound = [] (const std::vector<float>& x) {
        int last = -1;
        for (int i = 0; i < (int) x.size(); ++i)
            if (std::abs (x[(std::size_t) i]) > 1e-4f)
                last = i;
        return last;
    };
    CHECK (std::abs (lastSound (a) - 44100) < 400);       // the whole second
    CHECK (std::abs (lastSound (b) - 22050) < 400);       // twice as fast: half of it
}

TEST (flm190de_kit_sample_stretch_and_pitch)
{
    using namespace lynx;
    namespace sp = flm::samplepad;
    const std::vector<float> d (sp::defaults, sp::defaults + sp::count);
    auto fast = d;
    fast[sp::speed] = 0.75f; // +12 semitones (a resampling)
    const auto bytes = flm190bs::song ({
        flm190bs::rack (flm::kindMaster, {}),
        flm190bs::rack (flm::kindNormal, {}, { flm190bs::sampleOf ({ "Long", d, false, 0.0, 0, true, 1, 1.5, 0.0 }),       // 1.5 × as long
                                               flm190bs::sampleOf ({ "LongFast", fast, false, 0.0, 0, true, 1, 1.5, 3.0 }), // and Speed, pitch +3
                                               flm190bs::sampleOf ({ "Mode0", d, false, 0.0, 0, true, 0, 2.0, 0.0 }),       // mode 0: no stretch
                                               flm190bs::sampleOf ({ "Shift", d, false, 0.0, 0, true, 0, 1.0, -2.4 }) }),   // pitch only, whole semitones
        flm190bs::channel (flm::kindMaster, "MASTER", 0, {}),
        flm190bs::channel (flm::kindNormal, "Kit", 1, { flm190bs::lane (3, "Kit", { flm190bs::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 } }) }) }) }, 120.0);
    FlpImportReport report;
    auto o = flm190bs::anyFile (&report);
    o.common.sampleSeconds = [] (const std::string&) { return 1.0; };
    const auto s = importFlm (bytes, o);
    CHECK (s.has_value());
    if (! s)
        return;
    const auto* lng = flm182c::channelNamed (*s, "Long");
    const auto* lf = flm182c::channelNamed (*s, "LongFast");
    const auto* m0 = flm182c::channelNamed (*s, "Mode0");
    const auto* sh = flm182c::channelNamed (*s, "Shift");
    CHECK (lng != nullptr && lf != nullptr && m0 != nullptr && sh != nullptr);
    if (lng == nullptr || lf == nullptr || m0 == nullptr || sh == nullptr)
        return;
    // 1 s × 1.5 at 120 BPM = 3 beats, pitch kept
    CHECK (lng->sampler.keepPitch && std::abs (lng->sampler.stretchBeats - 3.0f) < 1e-5f && lng->sampler.pitch == 0.0f);
    // Speed +12 = the same shift and half the length; the stretch's +3 on top
    CHECK (lf->sampler.keepPitch && std::abs (lf->sampler.stretchBeats - 1.5f) < 1e-5f && lf->sampler.pitch == 15.0f);
    CHECK (! m0->sampler.keepPitch && m0->sampler.stretchBeats == 0.0f);
    CHECK (sh->sampler.keepPitch && std::abs (sh->sampler.stretchBeats - 2.0f) < 1e-5f && sh->sampler.pitch == -2.0f);
    CHECK (report.converted.count ("FL Studio Mobile: drum pad time-stretch or pitch shift") == 1
           && report.converted["FL Studio Mobile: drum pad time-stretch or pitch shift"] == 3);
}

// An audio clip's EG and Sample Start play as on a pad: −0.125 s of silence first (Sample Start 0.25)
TEST (flm190de_audio_clip_envelope_and_sample_start)
{
    using namespace lynx;
    namespace sp = flm::samplepad;
    const std::vector<float> d (sp::defaults, sp::defaults + sp::count);
    auto late = d, soft = d;
    late[sp::start] = 0.25f;
    soft[sp::egAttack] = 0.3f;
    const auto make = [&] (const std::vector<float>& prms) {
        return flm190bs::song ({ flm190bs::rack (flm::kindMaster, {}), flm190bs::rack (flm::kindNormal, {}), flm190bs::channel (flm::kindMaster, "MASTER", 0, {}),
                                 flm190bs::channel (flm::kindNormal, "Audio", 1, { flm190bs::lane (2, "Audio", {
                                     flm190bs::clip (0, 2, 256, 0, 1, {}, flm190bs::sampleOf ({ "Voice", prms }), 2) }) }) });
    };
    FlpImportReport report;
    const auto plain = importFlm (make (d), flm190bs::anyFile (nullptr));
    const auto delayed = importFlm (make (late), flm190bs::anyFile (&report));
    const auto shaped = importFlm (make (soft), flm190bs::anyFile (nullptr));
    CHECK (plain && delayed && shaped);
    if (! plain || ! delayed || ! shaped)
        return;
    const auto padOf = [] (const Song& s) -> std::vector<float> {
        for (const auto& c : s.channels)
            if (c.instrument == InstrumentKind::audioClip)
                return c.sampler.flmPad;
        return {};
    };
    CHECK (padOf (*plain).empty() && padOf (*delayed).size() == 7 && padOf (*shaped).size() == 7);
    CHECK (report.converted.count ("FL Studio Mobile: audio clip envelope, Length or Sample Start") == 1);
    const auto a = flm190bs::rendered (*plain, 0.5);
    const auto b = flm190bs::rendered (*delayed, 0.5);
    const auto c = flm190bs::rendered (*shaped, 0.5);
    const auto firstSound = [] (const std::vector<float>& x) {
        for (int i = 0; i < (int) x.size(); ++i)
            if (std::abs (x[(std::size_t) i]) > 1e-4f)
                return i;
        return -1;
    };
    CHECK (firstSound (a) >= 0 && firstSound (a) < 64);
    CHECK (std::abs (firstSound (b) - (firstSound (a) + 5512)) < 64); // 0.125 s later
    // the attack: well below the plain level after 10 ms, near it after 0.4 s (attack 0.3 ≈ 0.11 s to the top)
    CHECK (std::abs (c[441]) < 0.5f * std::abs (a[441]) && std::abs (c[17640] - a[17640]) < 0.05f * std::abs (a[17640]));
}

// --- FL Studio Mobile's DW Sampler on FLM's own engine (core/FlmDwSampler.h, FLM_FORMAT §7.5)
#include "core/FlmDwSampler.h"

namespace flm190de
{
// a ZONE record: the saved path (root marker, NUL, path, then `stale` text after a NUL), the tail
inline Bytes zoneOf (const std::string& path, int root, int lo, int hi, int vlo, int vhi, float gain = 1.0f, int loopMode = 0, int ls = 0, int le = 0,
                     float decay = 1.0f, const std::string& stale = {})
{
    Bytes text (1024, 0);
    std::string p = "@R@";
    p.push_back ('\0');
    p += path;
    std::memcpy (text.data(), p.data(), p.size());
    if (! stale.empty())
        std::memcpy (text.data() + p.size() + 1, stale.data(), stale.size());
    Bytes b = text;
    b.push_back ((std::uint8_t) root), b.push_back ((std::uint8_t) lo), b.push_back ((std::uint8_t) hi), b.push_back ((std::uint8_t) vlo), b.push_back ((std::uint8_t) vhi);
    f32 (b, gain);
    f32 (b, 0.5f);
    u32 (b, (std::uint32_t) loopMode);
    u32 (b, (std::uint32_t) ls);
    u32 (b, (std::uint32_t) le);
    f32 (b, 0.0f);
    f32 (b, decay);
    f32 (b, 1.0f);
    f32 (b, 0.25f);
    return chunk ("ZONE", b);
}
inline Bytes dwCustom (std::initializer_list<Bytes> parts)
{
    Bytes b;
    tag (b, "10WD");
    for (const auto& p : parts)
        put (b, p.data(), p.size());
    return b;
}
inline Bytes midiChunk (int range, int smooth) { return chunk ("MIDI", Bytes { (std::uint8_t) range, (std::uint8_t) smooth }); }
inline std::vector<float> dwDefaults() { return std::vector<float> (lynx::flmdw::defaults, lynx::flmdw::defaults + lynx::flmdw::paramCount); }

// two zones: one with stale text after its path, one with FLM's old integer 63 as its gain; MIDI (range 2, no smoothing)
inline Bytes twoZones()
{
    Bytes old = zoneOf ("DirectWave Samples/Bass/b.wav", 60, 0, 127, 64, 127);
    const std::uint32_t sixtyThree = 63;
    std::memcpy (old.data() + 8 + 1024 + 5, &sixtyThree, 4);
    return dwCustom ({ zoneOf ("DirectWave Samples/Bass/a.wav", 48, 0, 127, 0, 63, 0.5f, 2, 100, 900, 0.0f, "ss a.wav"), old, midiChunk (2, 0) });
}

// a PATh: the preset file and the two values that replace Attack / Release
inline Bytes pathCustom()
{
    Bytes path;
    const std::string where = std::string ("@R@") + '\0' + "DirectWave Samples\\Guitar\\Picked Bass.dwp";
    u32 (path, (std::uint32_t) where.size());
    put (path, where.data(), where.size());
    f32 (path, 0.2f);
    f32 (path, 0.3f);
    return dwCustom ({ chunk ("PATh", path) });
}

// a DirectWave program as FLM reads it (entries id, u64 size, body; a zone = 500 / 502 / 503 / 509), twice the same zone
inline Bytes program (std::uint32_t version)
{
    const auto entry = [] (std::uint32_t id, const Bytes& body) {
        Bytes b;
        u32 (b, id);
        u32 (b, (std::uint32_t) body.size());
        u32 (b, 0);
        put (b, body.data(), body.size());
        return b;
    };
    Bytes k500 { 57, 0, 59, 0, 127, 0, 0, 0, 0 };
    f32 (k500, 0.675f);
    f32 (k500, 0.5f);
    u32 (k500, 0), u32 (k500, 512);
    const std::string file = "%ILSharedData%\\DirectWave\\Nylon Guitar\\Nylon Guitar a3.wav";
    const Bytes k502 (file.begin(), file.end());
    Bytes k503;
    u32 (k503, 130802), u32 (k503, 0), u32 (k503, 1), u32 (k503, 16);
    f32 (k503, 44100.0f);
    u32 (k503, 2), u32 (k503, 129189), u32 (k503, 130802), u32 (k503, 0);
    Bytes k509;
    for (const float v : { 0.0f, 0.5f, 1.0f, 0.25f })
        f32 (k509, v);
    const auto zone = cat ({ entry (500, k500), entry (502, k502), entry (503, k503), entry (509, k509) });
    Bytes dwp;
    tag (dwp, "DwPr");
    u32 (dwp, version);
    const auto prog = entry (1, cat ({ entry (3, zone), entry (3, zone), entry (2, {}) }));
    put (dwp, prog.data(), prog.size());
    return dwp;
}
} // namespace flm190de

TEST (flm190de_dw_sampler_state_and_laws)
{
    using namespace lynx;
    namespace t = flm190de;
    const auto cstm = t::twoZones();
    const auto c = flmdw::parseCustom ({ cstm.data(), cstm.size() });
    CHECK (c.has_value() && c->zones.size() == 2 && c->pbRange == 2 && ! c->smoothAttack && ! c->hasPath);
    if (c && c->zones.size() == 2)
    {
        const auto& a = c->zones[0];
        CHECK (a.root == "@R@" && a.path == "DirectWave Samples/Bass/a.wav" && a.rootKey == 48 && a.velHigh == 63 && a.gain == 0.5f && a.loopMode == 2
               && a.loopStart == 100 && a.loopEnd == 900 && a.decay == 0.0f && a.release == 0.25f);
        CHECK (c->zones[1].gain < 0.0001f && c->zones[1].velLow == 64); // (63 as an integer: FLM plays it at gain 1)
        const auto z = flmdw::sampleZone (a, "C:/x.wav");
        CHECK (z.path == "C:/x.wav" && z.loopMode == 1 && z.loopEnd == 900 && z.lowVelocity == 0 && z.highVelocity == 63);
    }
    CHECK (! flmdw::parseCustom ({ cstm.data() + 4, cstm.size() - 4 }).has_value());
    const auto pc = t::pathCustom();
    const auto p = flmdw::parseCustom ({ pc.data(), pc.size() });
    CHECK (p && p->hasPath && p->pathRoot == "@R@" && p->path == "DirectWave Samples/Guitar/Picked Bass.dwp" && p->pathFloats && p->pathAttack == 0.2f
           && p->pathRelease == 0.3f && p->pbRange == 4 && p->smoothAttack);
    const auto dwp = t::program (37);
    const auto zones = flmdw::parseProgram ({ dwp.data(), dwp.size() });
    CHECK (zones.size() == 2);
    if (! zones.empty())
        CHECK (zones[0].rootKey == 57 && zones[0].keyHigh == 59 && zones[0].gain == 0.675f && zones[0].loopMode == 2 && zones[0].loopStart == 129189
               && zones[0].loopEnd == 130802 && zones[0].path == "%ILSharedData%/DirectWave/Nylon Guitar/Nylon Guitar a3.wav" && zones[0].release == 0.25f);
    const auto old = t::program (35); // (not read)
    CHECK (flmdw::parseProgram ({ old.data(), old.size() }).empty());

    // the laws (FLM_FORMAT §7.5's test vectors)
    CHECK (std::abs (flmdw::envelopeSeconds (0.25f) - 0.0400625) < 1e-9 && std::abs (flmdw::envelopeSeconds (0.0f) - 0.001) < 1e-12
           && std::abs (flmdw::envelopeSeconds (1.0f) - 10.001) < 1e-9);
    CHECK (std::abs (flmdw::onePoleRate (0.25f, 50.0) - 5.85475e-4) < 1e-8 && std::abs (flmdw::onePoleRate (1.0f, 50.0) - 4.0e-6) < 1e-10
           && flmdw::onePoleRate (0.0005f, 50.0) == 1.0);
    CHECK (flmdw::pitchSemitones (0.5f) == 0 && flmdw::pitchSemitones (0.759039f) == 12 && flmdw::pitchSemitones (0.09699f) == -19);
    float third;
    std::uint32_t bits = 0x3EAAAAABu;
    std::memcpy (&third, &bits, 4);
    CHECK (flmdw::filterKind (third) == 1 && flmdw::filterKind (0.333f) == 0 && flmdw::filterKind (1.0f) == 3);
    CHECK (std::abs (flmdw::cutoffHz (0.0f, 0.0, 0.0) - 16.0) < 1e-6 && std::abs (flmdw::cutoffHz (0.375f, 0.0, 0.0) - 238.86) < 0.01
           && std::abs (flmdw::cutoffHz (1.0f, 0.0, 0.0) - 21618.8) < 0.1 && flmdw::cutoffHz (1.0f, 1.0, 0.0) == 22000.0);
    CHECK (std::abs (flmdw::lfoHz (0.0f) - 0.033296) < 1e-5 && std::abs (flmdw::lfoHz (0.47f) - 0.22224) < 1e-4 && std::abs (flmdw::lfoHz (1.0f) - 30.0) < 1e-9);
}

TEST (flm190de_dw_sampler_import_and_sound)
{
    using namespace lynx;
    namespace t = flm190de;
    auto plain = t::dwDefaults();
    plain[flmdw::param::release] = 0.0f;
    auto up = plain;
    up[flmdw::param::pitch] = 0.759039f; // +12
    auto soft = plain;
    soft[flmdw::param::attack] = 0.5f;   // 0.626 s linear, heard squared
    // one zone of a 1-second sample, root 60, every key and velocity; a module with no zone; a rack with a Pitch lane
    using Bytes = flm182c::Bytes;
    const auto oneZone = t::dwCustom ({ t::zoneOf ("DirectWave Samples/Pad/p.wav", 60, 0, 127, 0, 127), t::midiChunk (2, 0) });
    const auto empty = t::dwCustom ({});
    const auto make = [&] (const std::vector<float>& prms, const Bytes& custom, bool lane) {
        return flm190bs::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, { t::moduleWith (1, 2, prms, {}, custom) }),
                                 t::channel (flm::kindMaster, "MASTER", 0, {}),
                                 lane ? t::channel (flm::kindNormal, "Pad", 1, { t::lane (0, "Pad", { t::clip (0, 4, 4, 0, 1, { { 0, 3.5, 60, 255 } }) }),
                                                                                 t::lane (1, "Pitch : DirectWave", { flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 40000 } }) }, 2, 3) })
                                      : t::channel (flm::kindNormal, "Pad", 1, { t::lane (0, "Pad", { t::clip (0, 4, 4, 0, 1, { { 0, 3.5, 60, 255 } }) }) }) });
    };
    FlpImportReport report;
    const auto a = importFlm (make (plain, oneZone, true), flm190bs::anyFile (&report));
    const auto b = importFlm (make (up, oneZone, false), flm190bs::anyFile (nullptr));
    const auto s = importFlm (make (soft, oneZone, false), flm190bs::anyFile (nullptr));
    const auto e = importFlm (make (plain, empty, false), flm190bs::anyFile (nullptr));
    CHECK (a && b && s && e);
    if (! a || ! b || ! s || ! e)
        return;
    const auto* pad = flm182c::channelNamed (*a, "Pad");
    CHECK (pad != nullptr && flmdw::isChannel (*pad) && pad->zones.size() == 1 && pad->zones[0].path == "C:/FLM/DirectWave Samples/Pad/p.wav"
           && pad->modelParams[1] == 2.0f && pad->modelParams[2] == 0.0f && pad->misc.pitchRange == 2);
    CHECK (report.converted.count ("FL Studio Mobile: DW Sampler (FLM's engine)") == 1
           && report.unsupported.count ("FL Studio Mobile: DW Sampler (multisample)") == 0);
    // the Pitch lane: the module's own value 3 on the channel
    const auto* pitch = t::automationOf (*a, AutomationTargetKind::channelParam, (int) (pad - a->channels.data()), flmdw::automationBase + 3);
    CHECK (pitch != nullptr && pitch->points.size() == 1 && std::abs (pitch->points[0].value - 40000.0f / 65535.0f) < 1e-6f);
    // a module with no zone is silent (FLM: no sample), not LYNX's generic synth
    const auto* none = flm182c::channelNamed (*e, "Pad");
    CHECK (none != nullptr && none->instrument == InstrumentKind::sampler && flmdw::isChannel (*none) && none->zones.empty());

    // sound: the sample (1 s of 0.5) at full velocity, release 0: level 0.5; +12 plays it in half the time; a slow
    // attack is the square of a linear ramp
    const auto x = flm190bs::rendered (*b, 1.2);
    const auto y = flm190bs::rendered (*s, 1.2);
    const auto z = flm190bs::rendered (*e, 1.2);
    // (a's Pitch lane moves the key: render it without the lane for the plain level)
    const auto plainSong = importFlm (make (plain, oneZone, false), flm190bs::anyFile (nullptr));
    const auto w = flm190bs::rendered (*plainSong, 1.2);
    const auto lastSound = [] (const std::vector<float>& v) {
        int last = -1;
        for (int i = 0; i < (int) v.size(); ++i)
            if (std::abs (v[(std::size_t) i]) > 1e-4f)
                last = i;
        return last;
    };
    const auto peak = [] (const std::vector<float>& v) { float m = 0.0f; for (const auto s : v) m = std::max (m, std::abs (s)); return m; };
    CHECK (std::abs (w[22050] - 0.5f) < 1e-3f && std::abs (lastSound (w) - 44100) < 600);
    CHECK (std::abs (lastSound (x) - 22050) < 600 && std::abs (x[11025] - 0.5f) < 1e-3f);
    const auto ta = flmdw::envelopeSeconds (0.5f);
    const auto at = [&] (double sec) { const auto r = std::min (1.0, sec / ta); return 0.5 * r * r; };
    CHECK (std::abs (y[4410] - at (0.1)) < 2e-3 && std::abs (y[22050] - at (0.5)) < 2e-3 && std::abs (y[33075] - 0.5f) < 2e-3f);
    CHECK (peak (z) == 0.0f);
}

// --- Pitcher / Pitcher 2 → LYNX Tune (FLM_FORMAT §7.6) and the Slicer (§7.7)
#include "core/FlmSlicer.h"
#include "core/LynxTune.h"

TEST (flm190de_pitchers_become_lynx_tune)
{
    using namespace lynx;
    namespace t = flm190de;
    namespace lt = lynxtune;
    // Pitcher: CSTM mask = C D E G A (a pentatonic), Speed 0.5, Pitch +5 (0.71), Mix 0.8, BYP on E
    std::vector<float> p1 (23, 0.0f);
    p1[0] = 1.0f, p1[1] = 0.5f, p1[2] = 0.71f, p1[3] = 0.5f, p1[7] = 0.5f, p1[9] = 0.8f, p1[14] = 1.0f;
    flm182c::Bytes mask { 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 };
    flm182c::f32 (mask, 0.0f), flm182c::f32 (mask, 0.5f);
    // Pitcher 2: its defaults (C minor) with Speed 0.25 and Formant Mode 0 (= formant correction on)
    std::vector<float> p2 (27, 0.0f);
    p2[0] = 1.0f;
    for (const int k : { 1, 3, 4, 6, 8, 9, 11 })
        p2[(std::size_t) k] = 1.0f;
    p2[13] = 0.25f, p2[17] = 0.5f, p2[18] = 1.0f, p2[19] = 0.5f, p2[22] = 1.0f, p2[23] = 0.5f;
    const auto bytes = flm190bs::song ({ t::rack (flm::kindMaster, {}),
                                         t::rack (flm::kindNormal, { t::moduleWith (25, 2, p1, {}, mask), t::moduleWith (45, 3, p2) }),
                                         t::channel (flm::kindMaster, "MASTER", 0, {}), t::channel (flm::kindNormal, "Voice", 1, {}) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto& fx = s->mixer[1].effects;
    CHECK (fx.size() == 2 && fx[0].kind == EffectKind::lynxTune && fx[1].kind == EffectKind::lynxTune);
    if (fx.size() != 2)
        return;
    const auto& a = fx[0].params;
    CHECK (a[lt::scale] == 1.0f && a[lt::noteC] == 1.0f && a[lt::noteC + 1] == 0.0f && a[lt::noteC + 4] == 1.0f && a[lt::noteC + 7] == 1.0f
           && a[lt::noteC + 11] == 0.0f);
    // Speed 0.5: τ = (3·0.125 + 0.001) / 8.82 s = 42.63 ms → retune √(42.63 / 400)
    CHECK (std::abs (a[lt::retune] - 0.32646f) < 1e-4f);
    CHECK (std::abs (a[lt::transpose] - (5.0f + 12.0f) / 24.0f) < 1e-6f && std::abs (a[lt::mix] - 0.8f) < 1e-6f);
    CHECK (a[lt::bypassC + 4] == 1.0f && a[lt::bypassC] == 0.0f && a[lt::correction] == 1.0f && std::abs (a[lt::reference] - 0.5f) < 1e-6f);
    const auto& b = fx[1].params;
    CHECK (b[lt::noteC] == 1.0f && b[lt::noteC + 2] == 1.0f && b[lt::noteC + 3] == 1.0f && b[lt::noteC + 4] == 0.0f && b[lt::noteC + 10] == 1.0f
           && b[lt::noteC + 11] == 0.0f);
    CHECK (b[lt::formantPreserve] == 1.0f && std::abs (b[lt::toleranceCents] - 0.55f) < 1e-6f && b[lt::mix] == 1.0f);
    // Speed 0.25: fc = (200^0.25 − 0.9)·2.29896 = 6.576 Hz → 5.30 / (2π fc) = 128.3 ms
    CHECK (std::abs (b[lt::retune] - 0.5663f) < 2e-3f);
    CHECK (report.converted.count ("FL Studio Mobile: Pitcher (LYNX Tune)") == 1 && report.converted.count ("FL Studio Mobile: Pitcher 2 (LYNX Tune)") == 1
           && report.droppedEffects.empty());
}

TEST (flm190de_slicer_detects_and_plays)
{
    using namespace lynx;
    namespace t = flm190de;
    // four hits of noise (60 ms each, decaying) at 0, 0.25, 0.5, 0.75 s in silence
    SampleData d;
    d.sampleRate = 44100.0;
    d.left.assign (44100, 0.0f);
    std::uint32_t seed = 1;
    for (const int at : { 0, 11025, 22050, 33075 })
        for (int i = 0; i < 2646 * 2; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            d.left[(std::size_t) (at + i)] = ((float) (seed >> 8) / 16777216.0f - 0.5f) * std::exp (-(float) i / 900.0f);
        }
    const auto starts = flmslicer::slices (d, 0.25f);
    CHECK (starts.size() == 4);
    if (starts.size() == 4)
        CHECK (starts[0] == 0 && std::abs (starts[1] - 11025) < 64 && std::abs (starts[2] - 22050) < 64 && std::abs (starts[3] - 33075) < 64);
    d.markerStarts = { 0, 22050 };
    CHECK (flmslicer::slices (d, 0.0f) == std::vector<std::int64_t> ({ 0, 22050 }));

    // the module: SENS 0.25, PATH, key range 48…51; key 49 plays the second slice
    flm182c::Bytes cstm;
    flm182c::Bytes sens;
    flm182c::f32 (sens, 0.25f);
    sens.push_back (1);
    const auto senc = flm182c::chunk ("SENS", sens);
    flm182c::put (cstm, senc.data(), senc.size());
    flm182c::Bytes path;
    const std::string where = std::string ("@R@") + '\0' + "Loop Samples\\Sliced Drums\\Hits.wav";
    flm182c::u32 (path, (std::uint32_t) where.size());
    flm182c::put (path, where.data(), where.size());
    const auto pathc = flm182c::chunk ("PATH", path);
    flm182c::put (cstm, pathc.data(), pathc.size());
    const auto bytes = flm190bs::song ({ t::rack (flm::kindMaster, {}),
                                         t::rack (flm::kindNormal, { t::moduleWith (33, 2, { 1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, {}, cstm, {}, 48, 51) }),
                                         t::channel (flm::kindMaster, "MASTER", 0, {}),
                                         t::channel (flm::kindNormal, "Slices", 1, { t::lane (0, "Slices", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 49, 255 } }) }) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto* c = flm182c::channelNamed (*s, "Slices");
    CHECK (c != nullptr && flmslicer::isChannel (*c) && c->samplePath == "C:/FLM/Loop Samples/Sliced Drums/Hits.wav" && c->modelParams[1] == 0.25f
           && c->modelParams[2] == 48.0f && c->keyLow == 48 && c->keyHigh == 51);
    CHECK (report.converted.count ("FL Studio Mobile: Slicer (FLM's engine)") == 1);
    // play it on the hits: the second slice (one-shot, SUST 0) sounds for about a quarter second, then stops
    auto inst = flmslicer::makeInstrument (std::make_shared<SampleData> (d));
    inst->prepare (44100.0);
    inst->configure (*c, 120.0);
    inst->settleNew();
    std::vector<float> l (44100, 0.0f), r (44100, 0.0f);
    inst->noteOn (49, 1.0f);
    inst->render (l.data(), r.data(), 44100, 1.0f, 1.0f);
    int last = -1;
    float first = 0.0f;
    for (int i = 0; i < 44100; ++i)
        if (std::abs (l[(std::size_t) i]) > 1e-4f)
        {
            last = i;
            if (first == 0.0f)
                first = (float) i;
        }
    // (at the slice's end the voice fades at 0.01 per sample while it reads on into the next hit: ~690 samples)
    CHECK (first < 64.0f && last > 11025 + 400 && last < 11025 + 800);
    CHECK (std::abs (l[100] - d.left[(std::size_t) (starts.size() == 4 ? starts[1] : 0) + 100]) < 0.05f);
}

// --- Arpeggiator and Note Echo, baked over the song (FLM_FORMAT §7.8)
TEST (flm190de_arpeggiator_and_note_echo)
{
    using namespace lynx;
    namespace nf = flmnotefx;
    // the defaults with Arp 1 in a mode: Rate 1/8, Gate 50 %, Range 1, Octave 0, Start 0, Repeats inf, Arp 2 off
    const auto arpParams = [] (int mode) {
        std::vector<float> p (33, 0.0f);
        p[0] = 1.0f, p[1] = 2.0f / 7.0f, p[2] = 4.0f / 19.0f, p[3] = 1.0f, p[7] = p[8] = 0.5f;
        for (const int base : { 9, 20 })
            p[(std::size_t) base] = 0.4444444f, p[(std::size_t) base + 3] = 0.5f, p[(std::size_t) base + 4] = 0.5f, p[(std::size_t) base + 6] = 0.5f;
        p[31] = p[32] = 6.0f / 14.0f;
        p[10] = (float) mode / 23.0f;
        p[19] = 1.0f;
        return p;
    };
    const std::vector<nf::TimedNote> chord { { 0.0, 4.0, 60, 0.8f, 0.5f }, { 0.0, 4.0, 64, 0.8f, 0.5f }, { 0.0, 4.0, 67, 0.8f, 0.5f } };
    bool random = true;
    const auto up = nf::arpeggiate (arpParams (2), chord, 120.0, 4, random);
    CHECK (! random && up.size() == 8);
    static constexpr int upKeys[] = { 60, 64, 67 };
    if (up.size() == 8)
        for (int i = 0; i < 8; ++i)
            CHECK (std::abs (up[(std::size_t) i].start - 0.5 * i) < 1e-9 && std::abs (up[(std::size_t) i].end - up[(std::size_t) i].start - 0.25) < 1e-6
                   && up[(std::size_t) i].key == upKeys[i % 3] && std::abs (up[(std::size_t) i].velocity - 0.8f) < 1e-6f);
    // the spec's "Binary dance" Arp 1: Bass Up, rhythm R2 rotated by 2 → 0.5 60, 1.0 64, 2.0 60, 2.5 67, 3.5 60
    auto dance = arpParams (9);
    dance[14] = 2.0f / 21.0f, dance[15] = 1.0f;
    const auto d = nf::arpeggiate (dance, chord, 120.0, 4, random);
    CHECK (d.size() == 5);
    if (d.size() == 5)
    {
        const double at[] = { 0.5, 1.0, 2.0, 2.5, 3.5 };
        const int key[] = { 60, 64, 60, 67, 60 };
        for (int i = 0; i < 5; ++i)
            CHECK (std::abs (d[(std::size_t) i].start - at[i]) < 1e-9 && d[(std::size_t) i].key == key[i]);
    }
    // a chord that starts between grid points plays at the next one; the arp notes end at the release
    const std::vector<nf::TimedNote> late { { 0.3, 1.2, 60, 1.0f, 0.5f } };
    const auto l = nf::arpeggiate (arpParams (2), late, 120.0, 4, random);
    CHECK (l.size() == 2 && std::abs (l[0].start - 0.5) < 1e-9 && std::abs (l[1].start - 1.0) < 1e-9 && std::abs (l[1].end - 1.2) < 1e-9);

    // Note Echo: the spec's third vector (4 echoes of a 1/4 beat, ×0.55 velocity, +7 per echo, drift 1.15, length
    // ×0.75, pan drift +0.25 with ping-pong)
    std::vector<float> e (16, 0.5f);
    e[0] = 1.0f, e[1] = 0.125f, e[2] = 0.24253564f, e[3] = 0.16568f, e[4] = 1.0f, e[7] = 0.25f, e[8] = 19.0f / 24.0f, e[9] = 0.75f, e[10] = 0.25f,
    e[11] = 0.0f, e[13] = 0.75f, e[14] = 1.0f, e[15] = 0.0f;
    const auto echoes = nf::echo (e, { { 0.0, 0.125, 60, 0.8f, 0.5f } }, 120.0, random);
    CHECK (! random && echoes.size() == 5);
    if (echoes.size() == 5)
    {
        const double start[] = { 0.25, 0.58058, 0.96079, 1.39804 }, end[] = { 0.34375, 0.65092, 1.01354, 1.43762 };
        const int key[] = { 67, 74, 81, 88 };
        const float vel[] = { 0.44f, 0.242f, 0.1331f, 0.0732f }, pan[] = { 0.5f, 0.375f, 0.75f, 0.125f };
        CHECK (echoes[0].key == 60 && echoes[0].end == 0.125);
        for (int i = 0; i < 4; ++i)
        {
            const auto& x = echoes[(std::size_t) i + 1];
            CHECK (std::abs (x.start - start[i]) < 2e-4 && std::abs (x.end - end[i]) < 2e-4 && x.key == key[i] && std::abs (x.velocity - vel[i]) < 2e-4f
                   && std::abs (x.pan - pan[i]) < 1e-6f);
        }
    }
    // a dry note longer than the delay ends at its first echo
    std::vector<float> plain (16, 0.5f);
    plain[0] = 1.0f, plain[1] = 0.25f, plain[2] = plain[3] = 0.24253564f, plain[4] = 1.0f, plain[11] = plain[14] = plain[15] = 0.0f;
    const auto held = nf::echo (plain, { { 0.0, 1.0, 60, 0.8f, 0.5f } }, 120.0, random);
    CHECK (held.size() == 9 && std::abs (held[0].end - 0.5) < 1e-9 && std::abs (held[8].start - 4.0) < 1e-3);

    // on import: a rack with an Arpeggiator plays its notes as one pattern over the song
    namespace t = flm190de;
    const auto bytes = flm190bs::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, { t::moduleWith (23, 2, t::superSawDefaults()), t::moduleWith (51, 3, arpParams (2)) }),
                                         t::channel (flm::kindMaster, "MASTER", 0, {}),
                                         t::channel (flm::kindNormal, "Arp", 1, { t::lane (0, "Arp", { t::clip (0, 4, 4, 0, 1, { { 0, 4, 60 }, { 0, 4, 64 }, { 0, 4, 67 } }),
                                                                                                    t::clip (8, 4, 4, 0, 1, {}) }) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const Pattern* baked = nullptr;
    for (const auto& p : s->patterns)
        if (p.name == "Arp (note effects)")
            baked = &p;
    CHECK (baked != nullptr && baked->notes.size() == 16); // (both clips hold the chord: beats 0…4 and 8…12)
    if (baked != nullptr && baked->notes.size() == 16)
        CHECK (baked->notes[0].start == 0 && baked->notes[0].key == 60 && baked->notes[1].start == ppq / 2 && baked->notes[1].key == 64
               && baked->notes[8].start == 8 * ppq && baked->notes[8].key == 60);
    int clips = 0;
    for (const auto& c : s->playlist)
        clips += c.kind == ClipKind::pattern;
    CHECK (clips == 1 && report.converted.count ("FL Studio Mobile: Arpeggiator (note effect, baked over the song)") == 1
           && report.droppedEffects.empty());
}

// Coordinator's decision (round 190de): a DW Sampler whose every sample is missing on this PC (FLM's content packs) plays
// a LYNX stand-in of its category, named in the report; one with some samples missing plays the others
TEST (flm190de_dw_sampler_missing_samples)
{
    using namespace lynx;
    namespace t = flm190de;
    const auto piano = t::dwCustom ({ t::zoneOf ("DirectWave Samples EXP/Keyboard/Grand Piano/GP C3.wav", 48, 0, 59, 0, 127),
                                      t::zoneOf ("DirectWave Samples EXP/Keyboard/Grand Piano/GP C5.wav", 72, 60, 127, 0, 127), t::midiChunk (2, 0) });
    const auto pad = t::dwCustom ({ t::zoneOf ("DirectWave Samples/Pad/here.wav", 60, 0, 59, 0, 127),
                                    t::zoneOf ("DirectWave Samples/Pad/gone.wav", 72, 60, 127, 0, 127), t::midiChunk (2, 0) });
    auto prms = t::dwDefaults();
    prms[flmdw::param::pitch] = 0.6042f; // +5 semitones
    const auto bytes = flm190bs::song ({ t::rack (flm::kindMaster, {}), t::rack (flm::kindNormal, { t::moduleWith (1, 2, prms, {}, piano) }),
                                         t::rack (flm::kindNormal, { t::moduleWith (1, 2, t::dwDefaults(), {}, pad) }),
                                         t::channel (flm::kindMaster, "MASTER", 0, {}),
                                         t::channel (flm::kindNormal, "Keys", 1, { t::lane (0, "Keys", { t::clip (0, 4, 4, 0, 1, { { 0, 2, 60, 255 } }) }) }),
                                         t::channel (flm::kindNormal, "Pad", 2, { t::lane (0, "Pad", { t::clip (0, 4, 4, 0, 1, { { 0, 2, 50, 255 } }) }) }) });
    FlpImportReport report;
    auto o = flm190bs::anyFile (&report);
    o.common.fileExists = [] (const std::string& p) { return p.find ("C:/FLM/") == 0 && p.find ("EXP") == std::string::npos && p.find ("gone") == std::string::npos; };
    const auto s = importFlm (bytes, o);
    CHECK (s.has_value());
    if (! s)
        return;
    const auto* keys = flm182c::channelNamed (*s, "Keys");
    const auto* padc = flm182c::channelNamed (*s, "Pad");
    CHECK (keys != nullptr && padc != nullptr);
    if (keys == nullptr || padc == nullptr)
        return;
    // every zone missing: a LYNX sound of the keys category (a piano when there is one), the module's Pitch kept
    CHECK (! flmdw::isChannel (*keys) && keys->instrument != InstrumentKind::sampler && keys->mixerTrack == 1 && keys->sampler.pitch == 5.0f);
    bool line = false;
    for (const auto& [what, n] : report.partialEffects)
        line = line || (what.find ("DW Sampler \"Grand Piano\": every sample missing, plays LYNX's") != std::string::npos);
    CHECK (line && report.missingSamples >= 2);
    // the keys play (not silent)
    const auto sound = flm190bs::rendered (*s, 1.0);
    float peak = 0.0f;
    for (const auto x : sound)
        peak = std::max (peak, std::abs (x));
    CHECK (peak > 1.0e-3f);
    // some zones missing: FLM's engine on the zones there are, the missing one listed
    CHECK (flmdw::isChannel (*padc) && padc->zones.size() == 2 && ! padc->zones[0].path.empty() && padc->zones[1].path.empty());
    CHECK (report.partialEffects.count ("FL Studio Mobile: DW Sampler with some samples missing (the others play; the missing ones are listed)") == 1);
}
