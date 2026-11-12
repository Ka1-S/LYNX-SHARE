// --- FL Studio Mobile (.flm) import, round 187d: slices F2 (mixer: the rack's Mute/Solo/Pitch Bend, AUX routes) and
// F6 (the rack's automation lanes: Level, Pan, send levels; the other lanes listed). The projects are written by the
// test with flm182c's helpers (FlmImport182cTests.inc); real projects only through the opt-in test (LYNX_FLM_DIR).

namespace flm187d
{
// the flm182c helpers by name (a using-directive would make Bytes ambiguous with tests.cpp's own)
namespace flm = lynx::flm;
using flm182c::Bytes;
using flm182c::put;
using flm182c::u8;
using flm182c::u16;
using flm182c::u32;
using flm182c::f32;
using flm182c::f64;
using flm182c::tag;
using flm182c::fixed;
using flm182c::lp32;
using flm182c::chunk;
using flm182c::cat;
using flm182c::head;
using flm182c::sample;
using flm182c::module;
using flm182c::rack;
using flm182c::clip;
using flm182c::lane;
using flm182c::channel;

// a rack with its own RPRM values (flm182c::rack writes FLM's defaults)
inline Bytes rackWith (std::uint32_t kind, std::initializer_list<float> rprm, std::initializer_list<Bytes> modules)
{
    Bytes b;
    u32 (b, kind);
    tag (b, "10KR");
    Bytes rhed;
    u32 (rhed, 0xFFFFFFFFu);
    u32 (rhed, 3);
    u32 (rhed, 0);
    u32 (rhed, 0xFFFFFFFFu);
    Bytes r;
    for (const float v : rprm)
        f32 (r, v);
    auto body = cat ({ b, chunk ("RHED", rhed), chunk ("RPRM", r) });
    for (const auto& m : modules)
        put (body, m.data(), m.size());
    return chunk ("RACK", body);
}

// an automation clip whose events carry values (flm182c::clip writes 0x7FFF): points (beat, key, value)
struct Pt
{
    double beat;
    int key;
    std::uint16_t value;
};

inline Bytes autoClip (double startBeat, double length, double loop, std::uint32_t id, const std::vector<Pt>& points)
{
    Bytes b;
    u32 (b, (std::uint32_t) std::llround (startBeat * 128));
    tag (b, "20LC");
    Bytes hd;
    f64 (hd, length);
    f64 (hd, loop);
    f64 (hd, 0.0);
    u32 (hd, id);
    f64 (hd, 0.0);
    u8 (hd, 0);
    Bytes evn;
    u16 (evn, 20);
    for (const auto& p : points)
    {
        u32 (evn, (std::uint32_t) std::llround (p.beat * 128));
        f64 (evn, 0.0);
        u16 (evn, (std::uint16_t) (std::int16_t) p.key);
        u8 (evn, 0xB2);
        u8 (evn, 0x7F);
        u16 (evn, p.value);
        u8 (evn, 0);
        u8 (evn, 0);
    }
    return chunk ("CLIP", cat ({ b, chunk ("CLHd", hd), chunk ("EVN2", evn) }));
}

namespace rp = lynx::flm::rackparam;

// master; "Keys" (a sampler: muted, soloed, bend held at +9 semitones) with Pan, Mute and module lanes; "Vocal" routed to the Master and the
// "Reverb Bus" (id 3) with a send-level lane to the bus, one to the master, one to a channel it has no route to, and a
// MIDI CC lane
inline Bytes project()
{
    Bytes f;
    tag (f, "10LF");
    const Bytes tdiv { 4, 4 };
    return cat ({ f, head ("Mixer Song", 120.0), chunk ("TDIV", tdiv),
                  rackWith (flm::kindMaster, { 0.8531f, 0.5f, 1.0f, 0.0f, 0.5f, 0.0f }, {}),
                  rackWith (flm::kindNormal, { 0.8531f, 0.25f, 1.0f, 1.0f, 0.6875f, 0.2f },
                           { module (1, 2, "/storage/emulated/0/Android/data/com.imageline.FLM/files/My Samples/808.wav", "My Samples/808.wav", 0, 127, 34) }),
                  rackWith (flm::kindNormal, { 1.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f }, {}),
                  rackWith (flm::kindBus, { 0.8531f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f }, {}),
                  channel (flm::kindMaster, "MASTER", 0, {}),
                  channel (flm::kindNormal, "Keys", 1,
                           { lane (0, "Keys", { clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                             // a drawn ramp: the control points (positive key) and the samples FLM keeps between them
                             lane (1, "Pan", { autoClip (4, 4, 4, 1, { { 0, 10, 0 }, { 1, -10, 16384 }, { 2, 10, 32768 }, { 2, 10, 65535 }, { 4, -10, 65535 } }) }, 0, rp::pan),
                             lane (1, "Mute", { autoClip (0, 4, 4, 1, { { 0, 1, 0 } }) }, 0, rp::mute),
                             lane (1, "Pitch : DirectWave", { autoClip (0, 4, 4, 1, { { 0, 1, 0 } }) }, 2, 3) }),
                  channel (flm::kindNormal, "Vocal", 2,
                           { lane (1, "Send Level (to Reverb Bus)", { autoClip (8, 0, 2, 1, { { 0, 1, 0 }, { 1, -1, 65535 } }) }, 0, rp::sendLevelBase + 3),
                             lane (1, "Send Level (to MASTER)", { autoClip (0, 1, 1, 1, { { 0, 1, 32768 } }) }, 0, rp::sendLevelBase + 0),
                             lane (1, "Send Level (to Keys)", { autoClip (0, 1, 1, 1, { { 0, 1, 32768 } }) }, 0, rp::sendLevelBase + 1),
                             lane (1, "MIDI CC #1", { autoClip (0, 1, 1, 1, { { 0, 1, 32768 } }) }, 0, rp::firstMidiCc + 1) },
                           false, { { 0, 0.8531f }, { 3, 0.6f } }),
                  channel (flm::kindBus, "Reverb Bus", 3, {}) });
}
} // namespace flm187d

TEST (flm187d_mixer_and_rack_automation)
{
    using namespace lynx;
    FlpImportReport report;
    std::string error;
    const auto song = importFlm (flm187d::project(), flm182c::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
    {
        std::printf ("  import error: %s\n", error.c_str());
        return;
    }
    const auto& s = *song;
    CHECK (s.mixer.size() == 4);
    if (s.mixer.size() != 4)
        return;
    // F2: Keys is muted and soloed, panned left (0.25 → −0.5); the master's Mute is the Master's
    CHECK (s.mixer[1].name == "Keys" && s.mixer[1].muted && s.mixer[1].solo && std::abs (s.mixer[1].pan + 0.5f) < 1e-6f);
    CHECK (s.mixer[0].muted && ! s.mixer[0].solo && ! s.mixer[2].muted && ! s.mixer[2].solo);
    // the held pitch bend 0.6875 on the channel's pitch: the rack hands 4v − 2 = 0.75 to the DW Sampler, which bends by
    // half of it × its PB Range (4 without a MIDI chunk) = +1.5 semitones (round 190de; until then +9, the drum slots'
    // 2^(4v − 2)); aftertouch reported
    const auto* keys = flm182c::channelNamed (s, "Keys");
    CHECK (keys != nullptr && keys->instrument == InstrumentKind::sampler && std::abs (keys->sampler.pitch - 1.5f) < 1e-4f);
    CHECK (report.converted.count ("FL Studio Mobile: channel pitch bend (on the channel's pitch)") == 1
           && report.partialEffects.count ("FL Studio Mobile: channel aftertouch (not imported)") == 0); // (round 190dw: no FLM module reads it)
    // Vocal → Master and the bus at FLM's send levels
    CHECK (s.mixer[2].toMaster && s.mixer[2].sends.size() == 1 && s.mixer[2].sends[0].target == 3);

    // F6: Pan, the bus send and the master send are clips; Mute, the module lane, the send to Keys and the MIDI CC are listed
    int pan = -1, toBus = -1, toMaster = -1;
    for (std::size_t i = 0; i < s.automations.size(); ++i)
    {
        const auto& t = s.automations[i].target;
        if (t.kind == AutomationTargetKind::mixerPan && t.index == 1)
            pan = (int) i;
        if (t.kind == AutomationTargetKind::mixerSend && t.index == 2 && t.slot == 3)
            toBus = (int) i;
        if (t.kind == AutomationTargetKind::mixerSend && t.index == 2 && t.slot == 0)
            toMaster = (int) i;
    }
    // (round 190de: the DW Sampler's Pitch lane is imported too, on its own value: a fourth)
    CHECK (s.automations.size() == 4 && pan >= 0 && toBus >= 0 && toMaster >= 0);
    if (pan >= 0)
    {
        // every saved point kept, the jump at beat 2 as two points on one tick
        const auto& p = s.automations[(std::size_t) pan].points;
        CHECK (p.size() == 5);
        if (p.size() == 5)
            CHECK (p[0].tick == 0 && std::abs (p[1].value - 16384.0f / 65535.0f) < 1e-6f && p[2].tick == 2 * ppq && p[3].tick == 2 * ppq
                   && std::abs (p[2].value - 32768.0f / 65535.0f) < 1e-6f && std::abs (p[3].value - 1.0f) < 1e-6f && p[4].tick == 4 * ppq);
    }
    if (toBus >= 0)
    {
        // a send level on FLM's fader law: 65535 = +5.6 dB, capped at LYNX's top; the clip is one 2-beat loop
        const auto& p = s.automations[(std::size_t) toBus].points;
        CHECK (p.size() == 2 && p[0].value == 0.0f && std::abs (p[1].value - faderPosition (maxFaderGain)) < 1e-4f && p[1].tick == ppq);
        const auto clip = std::find_if (s.playlist.begin(), s.playlist.end(), [toBus] (const Clip& c) { return c.kind == ClipKind::automation && c.automation == toBus; });
        CHECK (clip != s.playlist.end() && clip->start == 8 * ppq && clip->length == 2 * ppq);
    }
    CHECK (report.converted.count ("FL Studio Mobile: Pan automation") == 1 && report.converted.count ("FL Studio Mobile: send level automation") == 1
           && report.converted.at ("FL Studio Mobile: send level automation") == 2);
    CHECK (report.unsupported.count ("FL Studio Mobile: Mute/Solo automation (LYNX automates neither)") == 1
           && report.unsupported.count ("FL Studio Mobile: automation of module parameters (not imported yet)") == 0
           && report.converted.count ("FL Studio Mobile: instrument parameter automation") == 1
           && report.unsupported.count ("FL Studio Mobile: send level automation without that route") == 1
           // (round 190dw: CC #1 is the mod wheel, which no instrument of that empty rack reads: nothing to play)
           && report.converted.count ("FL Studio Mobile: mod-wheel automation on instruments that ignore the wheel (nothing to play)") == 1);
    // the point shapes are FLM's saved samples, so no "straight lines" approximation is reported any more
    CHECK (report.partialEffects.count ("FL Studio Mobile: automation point shapes (straight lines)") == 0);
    // the rows: one per imported lane, in FLM's order, under the channel's name
    int rows = 0;
    for (const auto& t : s.playlistTracks)
        rows += t.name == "Keys Pan" || t.name == "Vocal Send Level (to Reverb Bus)" || t.name == "Vocal Send Level (to MASTER)";
    CHECK (rows == 3);
}

// Opt-in (LYNX_FLM_DIR, LYNX_FLM_REPORT=1): the import report of every real project below the folder, so a round can
// read what each one skips
TEST (flm187d_real_project_reports)
{
    using namespace lynx;
    const char* dir = std::getenv ("LYNX_FLM_DIR");
    if (dir == nullptr || std::getenv ("LYNX_FLM_REPORT") == nullptr)
        return;
    const char* resources = std::getenv ("LYNX_FLM_RESOURCES");
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".flm" && ext != ".songtmpl")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        FlpImportReport report;
        FlmImportOptions o;
        o.common.projectFolder = entry.path().parent_path().string();
        o.common.fileExists = [] (const std::string& p) { return std::filesystem::exists (p); };
        o.common.report = &report;
        o.resourceRoot = resources != nullptr ? resources : "";
        const auto song = importFlm (bytes, o);
        CHECK (song.has_value());
        if (! song)
            continue;
        int muted = 0, routes = 0;
        for (const auto& m : song->mixer)
        {
            muted += m.muted;
            routes += (int) m.sends.size();
        }
        std::printf ("  %s: %zu automation clips, %d muted inserts, %d insert sends\n", entry.path().filename().string().c_str(), song->automations.size(),
                     muted, routes);
        const auto list = [] (const char* what, const std::map<std::string, int>& m) {
            for (const auto& [k, n] : m)
                std::printf ("    %s %dx %s\n", what, n, k.c_str());
        };
        list ("converted", report.converted);
        list ("partial", report.partialEffects);
        list ("unsupported", report.unsupported);
        list ("dropped", report.droppedEffects);
    }
}
