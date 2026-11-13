// --- FL Studio Mobile (.flm) import, round 182c (docs/analise/FLM_FORMAT.md). The projects here are written by the
// tests, field by field as the format page describes them; no file of Image-Line's or of a user is committed. Real
// projects (FL's FLM demos, a user's share-zip) are imported only by the opt-in test at the end (LYNX_FLM_DIR).

#include "core/FlmDwSampler.h"
#include "core/FlmFormat.h"
#include "core/FlmImport.h"
#include "core/MiniSynth.h"

namespace flm182c
{
namespace flm = lynx::flm;
using Bytes = std::vector<std::uint8_t>;

inline void put (Bytes& b, const void* p, std::size_t n) { b.insert (b.end(), (const std::uint8_t*) p, (const std::uint8_t*) p + n); }
inline void u8 (Bytes& b, std::uint8_t v) { b.push_back (v); }
inline void u16 (Bytes& b, std::uint16_t v) { put (b, &v, 2); }
inline void u32 (Bytes& b, std::uint32_t v) { put (b, &v, 4); }
inline void f32 (Bytes& b, float v) { put (b, &v, 4); }
inline void f64 (Bytes& b, double v) { put (b, &v, 8); }
inline void tag (Bytes& b, const char* t) { put (b, t, 4); }
inline void fixed (Bytes& b, const std::string& s, std::size_t size)
{
    Bytes f (size, 0);
    std::memcpy (f.data(), s.data(), std::min (s.size(), size - 1));
    put (b, f.data(), size);
}
inline void lp32 (Bytes& b, const std::string& s)
{
    u32 (b, (std::uint32_t) s.size());
    put (b, s.data(), s.size());
}
inline Bytes chunk (const char* t, const Bytes& body)
{
    Bytes b;
    tag (b, t);
    u32 (b, (std::uint32_t) body.size());
    put (b, body.data(), body.size());
    return b;
}
inline Bytes cat (std::initializer_list<Bytes> parts)
{
    Bytes b;
    for (const auto& p : parts)
        put (b, p.data(), p.size());
    return b;
}

inline Bytes head (const std::string& title, double tempo)
{
    Bytes b;
    u32 (b, 0);
    u32 (b, 0);
    fixed (b, title, 256);
    f64 (b, tempo);
    f64 (b, 0);
    f64 (b, 0);
    f64 (b, 0); // (skipped)
    for (int k = 0; k < 4; ++k)
        f64 (b, 0.5);
    b.resize (b.size() + 16, 0x11); // GUID
    u32 (b, 5);                     // root (F; round 190ds: +344 is the root, every file on disk agrees)
    u32 (b, 2);                     // scale (FLM's list: 2 = Major Bebop)
    u8 (b, 1);
    u8 (b, 1);
    u32 (b, 9); // channel-id counter
    lp32 (b, "5.0.4");
    return chunk ("HEAD", b);
}

inline Bytes sample (const std::string& name, const std::string& root, const std::string& path, double pitch = 0.0, double stretch = 1.0)
{
    Bytes body;
    tag (body, "20LS");
    Bytes main;
    u32 (main, 0xFFFFFFFFu);
    f64 (main, -1.0);
    fixed (main, name, 512);
    fixed (main, "My Samples", 512);
    Bytes str1;
    u8 (str1, 0);
    f64 (str1, stretch);
    f64 (str1, pitch);
    Bytes pth;
    const auto text = root + std::string (1, '\0') + path;
    u16 (pth, (std::uint16_t) text.size());
    put (pth, text.data(), text.size());
    Bytes prst;
    lp32 (prst, "/storage/emulated/0/Android/data/com.imageline.FLM/files/" + path);
    lp32 (prst, path);
    return cat ({ body, chunk ("MAIN", main), chunk ("STR1", str1), chunk ("PTH1", pth), chunk ("PRST", prst) });
}

inline Bytes module (std::uint32_t type, std::uint32_t id, const std::string& presetFile = {}, const std::string& presetName = "Default/Default",
                     int keyLow = 0, int keyHigh = 127, int params = 4, bool ramp = false)
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
    lp32 (prst, presetName);
    Bytes prms;
    for (int k = 0; k < params; ++k)
        f32 (prms, ramp ? (float) k / 100.0f : 0.5f); // ramp: every parameter its own value (order checks)
    return chunk ("RMOd", cat ({ b, chunk ("HEAD", h), chunk ("DESC", desc), chunk ("PRST", prst), chunk ("ADD1", add1), chunk ("PRMS", prms) }));
}

inline Bytes rack (std::uint32_t kind, std::initializer_list<Bytes> modules, const std::vector<Bytes>& kit = {})
{
    Bytes b;
    u32 (b, kind);
    tag (b, "10KR");
    Bytes rhed;
    u32 (rhed, 0xFFFFFFFFu);
    u32 (rhed, 3);
    u32 (rhed, 0);
    u32 (rhed, 0xFFFFFFFFu);
    Bytes rprm;
    for (const float v : { 0.9f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f })
        f32 (rprm, v);
    auto body = cat ({ b, chunk ("RHED", rhed), chunk ("RPRM", rprm) });
    if (! kit.empty())
    {
        Bytes rsmp;
        tag (rsmp, "10MS");
        u32 (rsmp, 0);
        Bytes pads;
        for (int k = 0; k < 22; ++k)
            pads.push_back ((std::uint8_t) k);
        put (rsmp, chunk ("PADS", pads).data(), chunk ("PADS", pads).size());
        for (const auto& s : kit)
        {
            const auto c = chunk ("SMPl", s);
            put (rsmp, c.data(), c.size());
        }
        const auto c = chunk ("RSMP", rsmp);
        put (body, c.data(), c.size());
    }
    for (const auto& m : modules)
        put (body, m.data(), m.size());
    return chunk ("RACK", body);
}

struct Ev
{
    double beat, length;
    int key;
    int velocity = 200;
    int pan = 127, pitch = 0x7FFF, repeat = 0, slide = 0; // bytes 15, 16–17, 18, 19
};

inline Bytes clip (double startBeat, double length, double loop, double offset, std::uint32_t id, const std::vector<Ev>& events,
                   const Bytes& sampleBody = {}, double sourceLength = 0.0)
{
    Bytes b;
    u32 (b, (std::uint32_t) std::llround (startBeat * 128));
    tag (b, "20LC");
    Bytes hd;
    f64 (hd, length);
    f64 (hd, loop);
    f64 (hd, offset);
    u32 (hd, id);
    f64 (hd, sourceLength);
    u8 (hd, 0);
    Bytes zoom (32, 0);
    Bytes evn;
    u16 (evn, 20);
    for (const auto& e : events)
    {
        u32 (evn, (std::uint32_t) std::llround (e.beat * 128));
        f64 (evn, e.length);
        u16 (evn, (std::uint16_t) (std::int16_t) e.key);
        u8 (evn, (std::uint8_t) e.velocity);
        u8 (evn, (std::uint8_t) e.pan);
        u16 (evn, (std::uint16_t) e.pitch);
        u8 (evn, (std::uint8_t) e.repeat);
        u8 (evn, (std::uint8_t) e.slide);
    }
    auto body = cat ({ b, chunk ("CLHd", hd), chunk ("ZOOM", zoom) });
    if (! sampleBody.empty())
    {
        const auto c = chunk ("CLSm", sampleBody);
        put (body, c.data(), c.size());
    }
    const auto c = chunk ("EVN2", evn);
    put (body, c.data(), c.size());
    return chunk ("CLIP", body);
}

inline Bytes lane (std::uint32_t kind, const std::string& name, std::initializer_list<Bytes> clips, int module = -1, int param = -1,
                   double swing = 0.0)
{
    Bytes d;
    u32 (d, kind);
    u32 (d, (std::uint32_t) module);
    u32 (d, (std::uint32_t) param);
    f64 (d, swing);
    u32 (d, 0);
    u32 (d, 0);
    fixed (d, name, 1024);
    lp32 (d, name);
    auto body = chunk ("DESc", d);
    for (const auto& c : clips)
        put (body, c.data(), c.size());
    return chunk ("TRKH", body);
}

struct Send
{
    std::uint32_t target;
    float level;
};

inline Bytes channel (std::uint32_t kind, const std::string& name, double order, std::initializer_list<Bytes> lanes, bool oldHeader = false,
                      std::initializer_list<Send> sends = {})
{
    Bytes b;
    u32 (b, kind);
    tag (b, oldHeader ? "10HC" : "20HC");
    Bytes hd;
    fixed (hd, name, 1024);
    u32 (hd, 0);
    f64 (hd, order);
    if (! oldHeader)
    {
        f32 (hd, 0);
        f64 (hd, 1);
        f64 (hd, 0);
        u32 (hd, 0);
        u32 (hd, 0);
        hd.resize (hd.size() + 16, 0x22);
        u32 (hd, (std::uint32_t) order);
        lp32 (hd, "Instrument " + std::to_string ((int) order));
        lp32 (hd, name);
        lp32 (hd, "");
    }
    auto body = cat ({ b, chunk ("CHHD", hd) });
    for (const auto& snd : sends)
    {
        Bytes w;
        u32 (w, snd.target);
        f32 (w, 1.0f);
        f32 (w, std::min (1.0f, snd.level)); // (the linear gain FLM writes is capped at 1)
        f32 (w, 1.0f);
        f32 (w, snd.level);
        f32 (w, snd.level);
        const auto c = chunk ("SEND", w);
        put (body, c.data(), c.size());
    }
    for (const auto& l : lanes)
        put (body, l.data(), l.size());
    return chunk ("CHNL", body);
}

// A project: master (automation lane), a sampler with a looping pattern, a drum kit, an audio track, a MiniSynth
// channel with an old channel header (10HC), and a Reverb 2 on the audio track.
inline Bytes project()
{
    Bytes f;
    tag (f, "10LF");
    const auto meta = [] {
        Bytes m;
        fixed (m, "2025-07-06 11-39-10", 256);
        f64 (m, 0);
        fixed (m, "", 256);
        fixed (m, "New Song (1)", 256);
        u8 (m, 0);
        fixed (m, "", 2048);
        fixed (m, "2026-11-08 02:09:36", 256);
        return chunk ("META", m);
    }();
    const Bytes tdiv { 3, 4 };
    return cat ({ f, head ("Test Song", 130.0), meta, chunk ("TDIV", tdiv),
                  rack (flm::kindMaster, { module (28, 1) }),
                  rack (flm::kindNormal, { module (1, 2, "/storage/emulated/0/Android/data/com.imageline.FLM/files/My Samples/808.wav", "My Samples/808.wav", 24, 96, 34),
                                           module (7, 3) }),
                  rack (flm::kindNormal, {}, { sample ("Kick", "@R@", "Drum Samples\\Kick\\Kick 1.wav"), sample ("Snare", "@R@", "Drum Samples\\Snare\\Gone.wav") }),
                  rack (flm::kindNormal, { module (35, 2) }),
                  rack (flm::kindNormal, { module (0, 2, {}, "Default/Default", 0, 127, 44, true) }),
                  rack (flm::kindBus, { module (12, 2) }),
                  channel (flm::kindMaster, "MASTER", 0, { lane (1, "Level", { clip (8, 0, 16, 0, 1, { { 0, 0, -5 } }) }, 0, 0) }),
                  // 2 clips of content 1 (8 beats, 4-beat loop → 2 passes each) and one of content 2 starting 1 beat in
                  channel (flm::kindNormal, "808", 1, { lane (0, "808", { clip (4, 8, 4, 0, 1, { { 0, 1, 36 }, { 2, 0.5, 48, 255 } }),
                                                                        clip (20, 8, 4, 0, 1, { { 0, 1, 36 }, { 2, 0.5, 48, 255 } }),
                                                                        clip (32, 3, 4, 1, 2, { { 1, 1, 40 }, { 3, 1, 41 } }) }) }),
                  channel (flm::kindNormal, "Drums", 3, { lane (3, "Drums", { clip (0, 4, 4, 0, 1, { { 0, 0.1249, 0, 255 }, { 1, 0.1249, 1 }, { 2, 0.1249, 5 } }) }) }),
                  channel (flm::kindNormal, "Vocal", 2, { lane (2, "Vocal", { clip (2, 10, 256, 0.125, 1, {}, sample ("Vocal", "@L@", "My Samples/Vocal.wav", 2.0), 10.125) }) },
                           false, { { 0, 0.6f }, { 5, 0.8531241f } }),
                  channel (flm::kindNormal, "Lead", 4, { lane (0, "Lead", { clip (0, 0, 4, 0, 1, { { 0, 4, 60 } }) }) }, true),
                  channel (flm::kindBus, "Reverb Bus", 5, {}) });
}

inline lynx::FlmImportOptions options (lynx::FlpImportReport* report)
{
    lynx::FlmImportOptions o;
    o.common.projectFolder = "E:/flm/Song";
    o.resourceRoot = "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed";
    o.common.fileExists = [] (const std::string& p) {
        return p == "E:/flm/Song/My Samples/808.wav" || p == "E:/flm/Song/My Samples/Vocal.wav"
               || p == "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed/Drum Samples/Kick/Kick 1.wav"
               || p == "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed/oscdata.dat";
    };
    o.common.report = report;
    return o;
}

inline const lynx::ChannelDef* channelNamed (const lynx::Song& s, const std::string& n)
{
    for (const auto& c : s.channels)
        if (c.name == n)
            return &c;
    return nullptr;
}
} // namespace flm182c

TEST (flm182c_reads_the_structure)
{
    using namespace lynx;
    const auto bytes = flm182c::project();
    CHECK (flm::isFlmData (bytes.data(), bytes.size()));
    CHECK (! flm::isFlmData (bytes.data(), 3));
    std::string error;
    const auto p = flm::parse ({ bytes.data(), bytes.size() }, &error);
    CHECK (p.has_value());
    if (! p)
    {
        std::printf ("  parse error: %s\n", error.c_str());
        return;
    }
    CHECK (p->title == "Test Song" && p->tempo == 130.0 && p->numerator == 3 && p->denominator == 4);
    CHECK (p->root == 5 && p->scale == 2 && p->appVersion == "5.0.4");
    CHECK (p->created == "2025-07-06 11-39-10" && p->saved == "2026-11-08 02:09:36" && p->originalName == "New Song (1)");
    CHECK (p->racks.size() == 6 && p->channels.size() == 6);
    CHECK (p->racks[1].params.size() == 6 && std::abs (p->racks[1].params[0] - 0.9f) < 1e-6f);
    CHECK (p->racks[1].modules.size() == 2 && p->racks[1].modules[0].type == 1 && p->racks[1].modules[0].keyLow == 24
           && p->racks[1].modules[0].keyHigh == 96 && p->racks[1].modules[0].params.size() == 34);
    CHECK (p->racks[1].modules[0].presetName == "My Samples/808.wav");
    CHECK (p->racks[2].kit.size() == 2 && p->racks[2].kit[0].root == "@R@" && p->racks[2].kit[0].path == "Drum Samples/Kick/Kick 1.wav");
    CHECK (p->racks[2].pads.size() == 22);
    const auto& ch = p->channels[1];
    CHECK (ch.name == "808" && ch.typeName == "Instrument 1" && ch.userName == "808" && ch.order == 1.0 && ch.channelId == 1);
    CHECK (ch.lanes.size() == 1 && ch.lanes[0].kind() == flm::LaneKind::notes && ch.lanes[0].clips.size() == 3);
    const auto& c = ch.lanes[0].clips[0];
    CHECK (c.startTick == 512 && c.length == 8 && c.loopLength == 4 && c.contentId == 1 && c.events.size() == 2);
    CHECK (c.events[1].key == 48 && c.events[1].velocity == 255 && c.events[1].length == 0.5 && c.events[1].tick == 256);
    CHECK (p->channels[0].lanes[0].kind() == flm::LaneKind::automation && p->channels[0].lanes[0].clips[0].events[0].key == -5);
    const auto& audio = p->channels[3].lanes[0].clips[0];
    CHECK (audio.sample && audio.sample->root == "@L@" && audio.sample->path == "My Samples/Vocal.wav" && audio.sample->pitch == 2.0
           && audio.offset == 0.125 && audio.sourceLength == 10.125);
    CHECK (p->channels[4].name == "Lead" && p->channels[4].typeName.empty()); // (the old 10HC header)
    // the module registry
    CHECK (std::string (flm::moduleName (0)) == "MiniSynth" && std::string (flm::moduleName (39)) == "3x Osc"
           && std::string (flm::moduleName (52)) == "SoundFont Player" && flm::moduleName (17) == nullptr);
    CHECK (flm::isGenerator (23) && ! flm::isGenerator (35) && flm::moduleCategory (51) == flm::ModuleCategory::noteEffect);
}

TEST (flm182c_rejects_damaged_files)
{
    using namespace lynx;
    std::string error;
    const flm182c::Bytes flp { 'F', 'L', 'h', 'd', 6, 0, 0, 0, 0, 0, 0, 0 };
    CHECK (! flm::parse ({ flp.data(), flp.size() }, &error) && ! error.empty());
    auto bytes = flm182c::project();
    auto cut = bytes;
    cut.resize (cut.size() - 40); // the last channel runs past the end of the file
    CHECK (! flm::parse ({ cut.data(), cut.size() }, &error));
    CHECK (! importFlm (cut, {}, &error));
    // a nested chunk that runs short: the channel keeps what came before it (the app's reader does the same)
    const auto at = std::search (bytes.begin(), bytes.end(), std::begin ("EVN2"), std::begin ("EVN2") + 4);
    CHECK (at != bytes.end());
    if (at != bytes.end())
    {
        const std::uint32_t huge = 0x7FFFFFF0u;
        std::memcpy (&*(at + 4), &huge, 4);
        const auto p = flm::parse ({ bytes.data(), bytes.size() }, &error);
        CHECK (p && p->channels.size() == 6 && p->channels[0].lanes.size() == 1 && p->channels[0].lanes[0].clips[0].events.empty());
    }
    // random damage never crashes the reader
    std::mt19937 rng (182);
    for (int round = 0; round < 300; ++round)
    {
        auto noisy = flm182c::project();
        for (int k = 0; k < 12; ++k)
            noisy[std::uniform_int_distribution<std::size_t> (4, noisy.size() - 1) (rng)] = (std::uint8_t) rng();
        if (const auto p = flm::parse ({ noisy.data(), noisy.size() }))
            (void) importFlmProject (*p, flm182c::options (nullptr));
    }
}

TEST (flm182c_imports_a_project)
{
    using namespace lynx;
    FlpImportReport report;
    std::string error;
    const auto song = importFlm (flm182c::project(), flm182c::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
    {
        std::printf ("  import error: %s\n", error.c_str());
        return;
    }
    const auto& s = *song;
    CHECK (std::abs (s.bpm - 130.0) < 1e-9 && s.beatsPerBar == 3 && s.info.title == "Test Song" && s.mode == PlayMode::song);

    // inserts: Master + 808, Vocal, Drums, Lead, Reverb Bus (FLM's display order)
    CHECK (s.mixer.size() == 6);
    if (s.mixer.size() == 6)
    {
        CHECK (s.mixer[1].name == "808" && s.mixer[2].name == "Vocal" && s.mixer[3].name == "Drums" && s.mixer[4].name == "Lead"
               && s.mixer[5].name == "Reverb Bus");
        // F2: the rack's level (FLM's law: 66·v^0.6 − 60 dB) and pan; the Vocal's routes: Master at 0.6, the bus at 0 dB
        const auto gain09 = (float) std::pow (10.0, 3.3 * std::pow (0.9, 0.6) - 3.0);
        CHECK (std::abs (faderGain (s.mixer[1].volume) - gain09) < 1e-3f && std::abs (s.mixer[1].pan) < 1e-6f);
        CHECK (s.mixer[2].toMaster && std::abs (s.mixer[2].masterLevel - (float) std::pow (10.0, 3.3 * std::pow (0.6, 0.6) - 3.0)) < 1e-4f);
        CHECK (s.mixer[2].sends.size() == 1 && s.mixer[2].sends[0].target == 5 && std::abs (s.mixer[2].sends[0].level - 1.0f) < 1e-4f);
        CHECK (s.mixer[1].toMaster && s.mixer[1].sends.empty());
    }

    // the DW Sampler with one sample → a sampler with the module's key range
    const auto* sampler = flm182c::channelNamed (s, "808");
    CHECK (sampler != nullptr && sampler->instrument == InstrumentKind::sampler && sampler->samplePath == "E:/flm/Song/My Samples/808.wav"
           && sampler->keyLow == 24 && sampler->keyHigh == 96 && sampler->mixerTrack == 1 && sampler->volume == 1.0f);
    // the drum kit: one sampler per sample (the second one missing, reported under FL's resources folder)
    const auto* kick = flm182c::channelNamed (s, "Kick");
    const auto* snare = flm182c::channelNamed (s, "Snare");
    CHECK (kick != nullptr && kick->instrument == InstrumentKind::sampler
           && kick->samplePath == "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed/Drum Samples/Kick/Kick 1.wav" && kick->mixerTrack == 3);
    CHECK (snare != nullptr && snare->instrument == InstrumentKind::sampler && snare->samplePath.empty());
    CHECK (report.missingSamples == 1 && report.missingFiles.size() == 1
           && report.missingFiles[0].path == "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed/Drum Samples/Snare/Gone.wav");
    // MiniSynth (F4): LYNX's MiniSynth engine, FLM's parameters 1…43 as the desktop plugin's 0…42, oscdata.dat from FLM's resources
    const auto* lead = flm182c::channelNamed (s, "Lead");
    CHECK (lead != nullptr && lead->instrument == InstrumentKind::minisynth && lead->mixerTrack == 4
           && lead->samplePath == "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed/oscdata.dat");
    if (lead != nullptr)
    {
        const auto params = miniSynthParams (lead->pluginState);
        CHECK (params.size() == 43);
        for (std::size_t k = 0; k < params.size(); ++k) // desktop parameter k = FLM's k + 1 (A/B inside FL, round 190n)
            CHECK (std::abs (params[k] - (k == 3 ? 0.0f : (float) (k + 1) / 100.0f)) < 1e-6f); // (MW Dest 0.04: FLM's first step)
        CHECK (miniSynthIsFlm (*lead)); // FLM's build of the engine
        // FLM's build: balance pan (centre 1, 1) against the desktop's equal-power law, the velocity as it is
        auto noisy = lead->pluginState;
        const auto at = miniSynthParamOffset (noisy, 7); // OSC Noise (plays without the wave tables)
        CHECK (at > 0);
        if (at > 0)
        {
            const float one = 1.0f;
            std::memcpy (noisy.data() + at, &one, 4);
            // Init.Vol 1 is MIDI velocity 100 on the desktop; FLM's module takes 100 / 127 itself
            const auto rms = [&noisy] (bool flm) {
                const auto y = minisynth::renderReference (noisy, { { 0, 60, flm ? 100.0 / 127.0 : 1.0, 1.0, 0.0, 0.0, 20000 } }, 44100.0, 16384, "", 512, 120.0, 0, -1.0, true, flm);
                double e = 0.0;
                for (const auto v : y)
                    e += (double) v * v;
                return std::sqrt (e / (double) std::max<std::size_t> (1, y.size()));
            };
            const auto desktop = rms (false), mobile = rms (true);
            CHECK (desktop > 1e-4 && mobile > desktop * 1.3 && mobile < desktop * 1.55); // +3 dB at centre
        }
    }
    CHECK (report.converted.count ("FL Studio Mobile: MiniSynth") == 1 && report.unsupported.count ("FL Studio Mobile: MiniSynth") == 0);
    // effects: Graphic EQ, Reverb 2 and the legacy Reverb are mapped since round 188f (F5), the Limiter since 188h
    CHECK (report.converted.count ("FL Studio Mobile: Reverb 2") == 1 && report.converted.count ("FL Studio Mobile: Graphic EQ") == 1
           && report.converted.count ("FL Studio Mobile: Reverb (legacy)") == 1 && report.converted.count ("FL Studio Mobile: Limiter") == 1
           && report.droppedEffects.empty());
    // F6: the master's Level lane → a Master volume automation clip (8 beats in, one 16-beat pass, FLM's fader law)
    CHECK (s.automations.size() == 1 && report.unsupported.count ("FL Studio Mobile: automation of module parameters (not imported yet)") == 0);
    if (s.automations.size() == 1)
    {
        const auto& a = s.automations[0];
        CHECK (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == 0 && a.points.size() == 1);
        if (! a.points.empty())
            CHECK (std::abs (a.points[0].value - faderPosition ((float) std::pow (10.0, 3.3 * std::pow (32767.0 / 65535.0, 0.6) - 3.0))) < 1e-4f);
        const auto clip = std::find_if (s.playlist.begin(), s.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::automation; });
        CHECK (clip != s.playlist.end() && clip->start == 8 * ppq && clip->length == 16 * ppq && clip->automation == 0);
    }
    CHECK (report.converted.count ("FL Studio Mobile: DW Sampler (FLM's engine)") == 1); // (round 190de: FLM's own voice)

    // patterns: 808 content 1 and 2, the drum pattern, the lead's
    CHECK (s.patterns.size() == 4);
    const auto patternIndex = [&] (const std::string& n) {
        for (std::size_t i = 0; i < s.patterns.size(); ++i)
            if (s.patterns[i].name == n)
                return (int) i;
        return -1;
    };
    const auto p808 = patternIndex ("808"), p808b = patternIndex ("808 (2)"), drums = patternIndex ("Drums");
    CHECK (p808 >= 0 && p808b >= 0 && drums >= 0);
    if (p808 < 0 || p808b < 0 || drums < 0)
        return;
    const auto& a = s.patterns[(std::size_t) p808];
    CHECK (a.lengthTicks == 4 * ppq && a.notes.size() == 2);
    CHECK (a.notes[1].start == 2 * ppq && a.notes[1].length == ppq / 2 && a.notes[1].key == 48 && std::abs (a.notes[1].velocity - 1.0f) < 1e-6f);
    CHECK (std::abs (a.notes[0].velocity - 200.0f / 255.0f) < 1e-6f);
    // drum notes on the kit channels (key 5 has no sample: dropped)
    const auto& d = s.patterns[(std::size_t) drums];
    CHECK (d.notes.size() == 2 && d.notes[0].key == 60 && d.notes[1].start == ppq);
    CHECK (report.partialEffects.count ("FL Studio Mobile: drum notes without a kit sample (dropped)") == 1);
    if (d.notes.size() == 2 && kick != nullptr)
        CHECK (s.channels[(std::size_t) d.notes[0].channel].name == "Kick" && s.channels[(std::size_t) d.notes[1].channel].name == "Snare");

    // clips: each 8-beat clip of a 4-beat loop is two passes; the third starts 1 beat into its content
    std::vector<const Clip*> of808, of808b, audio;
    for (const auto& c : s.playlist)
    {
        if (c.kind == ClipKind::pattern && c.pattern == p808)
            of808.push_back (&c);
        if (c.kind == ClipKind::pattern && c.pattern == p808b)
            of808b.push_back (&c);
        if (c.kind == ClipKind::audio)
            audio.push_back (&c);
    }
    CHECK (of808.size() == 4);
    if (of808.size() == 4)
        CHECK (of808[0]->start == 4 * ppq && of808[1]->start == 8 * ppq && of808[2]->start == 20 * ppq && of808[0]->length == 4 * ppq
               && of808[0]->offset == 0);
    CHECK (of808b.size() == 1 && of808b[0]->start == 32 * ppq && of808b[0]->offset == ppq && of808b[0]->length == 3 * ppq);
    // the audio clip: 0.125 beats into the vocal, 2 semitones up, on its own audio-clip channel
    CHECK (audio.size() == 1);
    if (audio.size() == 1)
    {
        const auto& c = *audio[0];
        CHECK (c.start == 2 * ppq && c.length == 10 * ppq && c.offset == ppq / 8 && std::abs (c.pitch - 2.0f) < 1e-6f);
        CHECK (s.channels[(std::size_t) c.channel].instrument == InstrumentKind::audioClip
               && s.channels[(std::size_t) c.channel].samplePath == "E:/flm/Song/My Samples/Vocal.wav" && s.channels[(std::size_t) c.channel].mixerTrack == 2);
    }
    // Playlist tracks: one per lane with clips, in FLM's order (the master's automation lane first)
    CHECK (s.playlistTracks.size() >= 5);
    if (s.playlistTracks.size() >= 5)
        CHECK (s.playlistTracks[0].name == "MASTER Level" && s.playlistTracks[1].name == "808" && s.playlistTracks[1].mode == PlaylistTrackMode::instrument
               && s.playlistTracks[2].name == "Vocal" && s.playlistTracks[2].mode == PlaylistTrackMode::audio && s.playlistTracks[2].mixerTrack == 2);

    // a located file (the Missing files window) is used on the next import
    auto o = flm182c::options (&report);
    o.common.relocations[report.missingFiles[0].path] = "D:/found/Gone.wav";
    const auto exists = o.common.fileExists;
    o.common.fileExists = [exists] (const std::string& p) { return p == "D:/found/Gone.wav" || exists (p); };
    report = {};
    const auto again = importFlm (flm182c::project(), o);
    CHECK (again && flm182c::channelNamed (*again, "Snare") != nullptr && flm182c::channelNamed (*again, "Snare")->samplePath == "D:/found/Gone.wav"
           && report.missingSamples == 0);
}

// Opt-in (LYNX_FLM_DIR=folder): every .flm / .songtmpl below the folder is imported and summarised, its samples looked
// for next to it and in FL's FLM resources (LYNX_FLM_RESOURCES). FL's demos and users' projects stay on the developer's
// disk; none is committed.
TEST (flm182c_real_projects)
{
    using namespace lynx;
    const char* dir = std::getenv ("LYNX_FLM_DIR");
    if (dir == nullptr)
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
        o.common.readFile = [] (const std::string& p) {
            std::ifstream file (p, std::ios::binary);
            return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (file)), std::istreambuf_iterator<char>());
        };
        o.common.report = &report;
        o.resourceRoot = resources != nullptr ? resources : "";
        std::string error;
        const auto t0 = std::chrono::steady_clock::now();
        const auto song = importFlm (bytes, o, &error);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        CHECK (song.has_value());
        if (! song)
        {
            std::printf ("  %s: ERROR %s\n", entry.path().filename().string().c_str(), error.c_str());
            continue;
        }
        int notes = 0, audioClips = 0, samplers = 0;
        std::int64_t end = 0;
        for (const auto& p : song->patterns)
            notes += (int) p.notes.size();
        for (const auto& c : song->playlist)
        {
            audioClips += c.kind == ClipKind::audio;
            end = std::max (end, c.end());
        }
        int directWaves = 0;
        for (const auto& c : song->channels)
        {
            samplers += c.instrument == InstrumentKind::sampler && ! c.samplePath.empty();
            directWaves += c.instrument == InstrumentKind::directwave || flmdw::isChannel (c);
        }
        std::printf ("  %s: %.1f ms, bpm %.3f %d/4, %zu inserts, %zu channels (%d samplers with a sample, %d DirectWave), %zu patterns (%d notes), "
                     "%zu clips (%d audio), end %.3f beats, %d samples missing, %zu dropped effects, %zu unsupported\n",
                     entry.path().filename().string().c_str(), ms, song->bpm, song->beatsPerBar, song->mixer.size(), song->channels.size(), samplers,
                     directWaves, song->patterns.size(), notes, song->playlist.size(), audioClips, (double) end / ppq, report.missingSamples,
                     report.droppedEffects.size(), report.unsupported.size());
    }
}

// Opt-in (LYNX_FLM_RESOURCES = FL's "…/FL Studio Mobile/Installed"): a DW Sampler playing one of the DirectWave programs
// FL ships there plays the program's zones (slice F3; since round 190de on FLM's own DW Sampler voice, which reads the
// program itself, core/FlmDwSampler.h)
TEST (flm182c_directwave_program)
{
    using namespace lynx;
    const char* resources = std::getenv ("LYNX_FLM_RESOURCES");
    if (resources == nullptr)
        return;
    namespace t = flm182c;
    t::Bytes f;
    t::tag (f, "10LF");
    const auto bytes = t::cat ({ f, t::head ("DW", 120.0), t::rack (flm::kindMaster, {}),
                                 t::rack (flm::kindNormal, { t::module (1, 2, "Resources/DirectWave Samples/Keyboard/Electric.dwp", "Keyboard/Electric", 0, 127, 34) }),
                                 t::channel (flm::kindMaster, "MASTER", 0, {}),
                                 t::channel (flm::kindNormal, "Keys", 1, { t::lane (0, "Keys", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }) });
    FlpImportReport report;
    FlmImportOptions o;
    o.common.projectFolder = "/nonexistent";
    o.common.fileExists = [] (const std::string& p) { return std::filesystem::exists (p); };
    o.common.readFile = [] (const std::string& p) {
        std::ifstream in (p, std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    };
    o.common.report = &report;
    o.resourceRoot = resources;
    const auto song = importFlm (bytes, o);
    const auto* keys = song ? t::channelNamed (*song, "Keys") : nullptr;
    CHECK (keys != nullptr && keys->instrument == InstrumentKind::sampler && flmdw::isChannel (*keys) && keys->zones.size() > 1);
    CHECK (report.converted.count ("FL Studio Mobile: DW Sampler (multisample, FLM's engine)") == 1 && report.missingSamples == 0);
    if (keys != nullptr)
        std::printf ("  Electric.dwp: %zu zones, first %s\n", keys->zones.size(), keys->zones.empty() ? "-" : keys->zones[0].path.c_str());
}
