#include "core/FlmExport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <tuple>

#include "core/Effects.h"
#include "core/FlmDwSampler.h"
#include "core/FlmFormat.h"
#include "core/FlmSlicer.h"
#include "core/GmsMobile.h"
#include "core/ModelParams.h"
#include "core/ProjectKey.h"
#include "core/ScaleKeys.h"
#include "core/TransistorBass.h"

// Round 190og. The laws below are the .flm import's (core/FlmImport.cpp, FLM_FORMAT.md) turned around, so that a song
// exported here and imported again keeps its notes, levels, instruments and effects; where FLM has no place for
// something the loss is listed instead.

namespace lynx
{
float flmFaderValue (double gain) noexcept
{
    // FLM's fader: gain = 10^(3.3·v^0.6 − 3) (FLM_FORMAT §3), so v = ((log10 g + 3) / 3.3)^(1/0.6); 0 below −60 dB
    if (! (gain > 0.001))
        return 0.0f;
    const auto x = (std::log10 (gain) + 3.0) / 3.3;
    return (float) std::clamp (std::pow (std::max (0.0, x), 1.0 / 0.6), 0.0, 1.0);
}

float flmPostGainValue (double gain) noexcept
{
    // the module wrapper's Post Gain: 10^(2g − 1), −20…+20 dB
    if (! (gain > 0.0))
        return 0.0f;
    return (float) std::clamp ((std::log10 (gain) + 1.0) / 2.0, 0.0, 1.0);
}

float flmSpeedValue (int semitones) noexcept
{
    // a pad's Speed: (int) (48v − 24), truncated towards zero; the value sits a quarter step inside its semitone
    semitones = std::clamp (semitones, -24, 24);
    return std::clamp (((float) semitones + 24.0f + (semitones >= 0 ? 0.25f : -0.25f)) / 48.0f, 0.0f, 1.0f);
}

namespace
{
constexpr double tpb = (double) ppq;                 // LYNX ticks per beat
constexpr double flmTpb = (double) flm::ticksPerBeat; // FLM's

std::uint32_t flmTicks (std::int64_t lynxTicks) noexcept
{
    return (std::uint32_t) std::max<std::int64_t> (0, std::llround ((double) lynxTicks * flmTpb / tpb));
}
double beats (std::int64_t lynxTicks) noexcept { return (double) lynxTicks / tpb; }

std::string fileName (const std::string& path)
{
    const auto slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? path : path.substr (slash + 1);
}
std::string fileStem (const std::string& path)
{
    auto n = fileName (path);
    if (const auto dot = n.find_last_of ('.'); dot != std::string::npos && dot > 0)
        n.resize (dot);
    return n;
}

std::string utcDate (double seconds)
{
    if (! (seconds > 0.0))
        return {};
    const auto t = (std::time_t) seconds;
    std::tm tm {};
#if defined(_WIN32)
    gmtime_s (&tm, &t);
#else
    gmtime_r (&t, &tm);
#endif
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%04d-%02d-%02d %02d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// the losses, grouped by text (detail = the names it applies to)
struct Losses
{
    std::vector<std::pair<FlmExportLoss, std::vector<std::string>>> list;
    void add (const std::string& text, const std::string& name, bool sound = true)
    {
        for (auto& [l, names] : list)
            if (l.text == text)
            {
                if (! name.empty() && std::find (names.begin(), names.end(), name) == names.end())
                    names.push_back (name);
                return;
            }
        list.push_back ({ FlmExportLoss { text, {}, sound }, name.empty() ? std::vector<std::string> {} : std::vector<std::string> { name } });
    }
    std::vector<FlmExportLoss> done() const
    {
        std::vector<FlmExportLoss> out;
        for (const auto& [l, names] : list)
        {
            auto o = l;
            for (std::size_t k = 0; k < names.size() && k < 6; ++k)
                o.detail += (k == 0 ? "" : ", ") + names[k];
            if (names.size() > 6)
                o.detail += ", +" + std::to_string (names.size() - 6);
            out.push_back (o);
        }
        return out;
    }
};

// FLM's module for each Pocket engine (the inverse of FlmEffects.cpp map()) and the PRMS count FLM saves (FLM_FORMAT §7)
struct PocketModule
{
    EffectKind kind;
    std::uint32_t type;
    int count;
};
constexpr PocketModule pocketModules[] = {
    { EffectKind::pocketEq, 3, 21 },           { EffectKind::pocketGraphicEq, 7, 9 },   { EffectKind::pocketFilter, 9, 5 },
    { EffectKind::pocketReverbClassic, 12, 22 }, { EffectKind::pocketReverb2, 35, 19 }, { EffectKind::pocketReverb, 32, 16 },
    { EffectKind::pocketLimiterClassic, 5, 4 }, { EffectKind::pocketDistortion, 6, 7 }, { EffectKind::pocketLimiter, 28, 5 },
    { EffectKind::pocketLeveller, 20, 3 },     { EffectKind::pocketGate, 27, 9 },       { EffectKind::pocketTranceDelay, 16, 7 },
    { EffectKind::pocketTapeDelay, 18, 5 },    { EffectKind::pocketMultiFx, 4, 7 },     { EffectKind::pocketAutoduck, 29, 8 },
    { EffectKind::pocketChorus, 11, 5 },       { EffectKind::pocketSpacer, 34, 8 },     { EffectKind::pocketStereoizer, 15, 7 },
    { EffectKind::pocketWowFlutter, 43, 11 },  { EffectKind::pocketPhaser, 14, 10 },    { EffectKind::pocketFlanger, 13, 12 },
    { EffectKind::pocketTremolo, 46, 12 },     { EffectKind::pocketWaveshaper, 41, 21 }, { EffectKind::pocketCompressor, 8, 9 },
    { EffectKind::pocketSpreader, 44, 5 },     { EffectKind::pocketComb, 40, 12 },      { EffectKind::pocketMultiband, 36, 31 },
    { EffectKind::pocketTapeStop, 42, 8 },
};
const PocketModule* pocketModule (EffectKind k) noexcept
{
    for (const auto& p : pocketModules)
        if (p.kind == k)
            return &p;
    return nullptr;
}

// FLM's key locker for a project key (FlmImport's scalekeys::flmKeyLocker turned around: the FLM scale index whose
// degrees are the key's; 0 = none) and LYNX's chord set as FLM's KEYB JSON (the shape parseFlmChordSet reads)
std::pair<std::uint32_t, std::uint32_t> flmKeyLockerOf (const projectkey::Key& key)
{
    const auto& want = projectkey::intervals (key);
    const auto root = (std::uint32_t) std::clamp (key.root, 0, 11);
    for (std::uint32_t i = 1; i < 64; ++i)
        if (const auto k = scalekeys::flmKeyLocker (root, i); k && projectkey::intervals (*k) == want)
            return { root, i };
    return { root, 0u };
}

std::string flmChordSetJson (const std::vector<KeyboardChord>& chords)
{
    std::string out = "{\n    \"chords\": [";
    for (std::size_t i = 0; i < chords.size() && i < 24; ++i)
    {
        const auto& c = chords[i];
        out += i == 0 ? "\n" : ",\n";
        out += "        {\n            \"baseNote\": " + std::to_string (((c.root % 12) + 12) % 12) + ",\n            \"extensions\": [";
        for (std::size_t k = 0; k < c.extensions.size(); ++k)
            out += (k == 0 ? "" : ", ") + std::to_string (c.extensions[k]);
        out += "],\n            \"octave\": " + std::to_string (c.octave) + ",\n            \"quality\": " + std::to_string (c.quality) + "\n        }";
    }
    out += "\n    ],\n    \"rootNote\": 0\n}";
    return out;
}

struct Ctx
{
    const Song& song;
    const FlmExportOptions& options;
    Losses losses;
    flm::Project project;
    std::map<std::string, std::string> copied; // source → relative
    std::set<std::string> usedNames;
    std::string samplesFolder;
    // (insert, slot) → the module instance its effect became in the rack being built (automation lanes)
    std::map<std::pair<int, int>, std::uint32_t> slotModule;
    int insertNow = -1; // the insert effectModules is converting
};

// a sample the copy needs: where it goes beside the .flm (one copy per file; equal names get a number)
flm::SampleRef sampleRef (Ctx& ctx, const std::string& source, const std::string& shown)
{
    flm::SampleRef s;
    s.name = shown.empty() ? fileStem (source) : shown;
    if (source.empty())
        return s;
    // FLM's own factory content stays a reference to the app's resources
    for (auto root : ctx.options.resourceRoots)
    {
        std::replace (root.begin(), root.end(), '\\', '/');
        while (! root.empty() && root.back() == '/')
            root.pop_back();
        auto src = source;
        std::replace (src.begin(), src.end(), '\\', '/');
        if (! root.empty() && src.size() > root.size() + 1 && src.compare (0, root.size(), root) == 0 && src[root.size()] == '/')
        {
            s.root = "@R@";
            s.path = src.substr (root.size() + 1);
            s.relName = s.path;
            return s;
        }
    }
    auto it = ctx.copied.find (source);
    if (it == ctx.copied.end())
    {
        auto name = fileName (source);
        if (ctx.usedNames.count (name) > 0)
        {
            const auto stem = fileStem (name), ext = name.substr (stem.size());
            for (int n = 2;; ++n)
                if (const auto candidate = stem + " (" + std::to_string (n) + ")" + ext; ctx.usedNames.count (candidate) == 0)
                {
                    name = candidate;
                    break;
                }
        }
        ctx.usedNames.insert (name);
        it = ctx.copied.emplace (source, ctx.samplesFolder + "/" + name).first;
    }
    s.root = "@L@";
    s.path = it->second;
    s.relName = it->second;
    if (! ctx.options.targetFolder.empty())
    {
        // (the folder's own separators: a Windows folder gives a Windows path)
        auto abs = ctx.options.targetFolder + "/" + it->second;
        if (ctx.options.targetFolder.find ('\\') != std::string::npos)
            std::replace (abs.begin(), abs.end(), '/', '\\');
        s.savedPath = abs;
    }
    return s;
}

// --- instruments -----------------------------------------------------------------------------------------------------
using Bytes = std::vector<std::uint8_t>;
void put (Bytes& b, const void* p, std::size_t n) { b.insert (b.end(), static_cast<const std::uint8_t*> (p), static_cast<const std::uint8_t*> (p) + n); }
void putU32 (Bytes& b, std::uint32_t v) { put (b, &v, 4); }
void putF32 (Bytes& b, float v) { put (b, &v, 4); }

// The MiniSynth state ("SSLF" … DATA "0rev" + 43 floats, FlmImport.cpp miniSynthState): the 43 desktop values
std::vector<float> miniSynthValues (const std::vector<std::uint8_t>& state)
{
    for (std::size_t at = 0; at + 12 <= state.size(); ++at)
        if (std::memcmp (state.data() + at, "DATA", 4) == 0 && std::memcmp (state.data() + at + 8, "0rev", 4) == 0)
        {
            std::uint32_t size;
            std::memcpy (&size, state.data() + at + 4, 4);
            const auto n = std::min<std::size_t> ((size - 4) / 4, (state.size() - at - 12) / 4);
            std::vector<float> v (n);
            std::memcpy (v.data(), state.data() + at + 12, n * 4);
            return v;
        }
    return {};
}

bool flmBuild (const ChannelDef& c) noexcept { return c.modelParams.size() == 1 && c.modelParams[0] == 1.0f; }

// The channel's instrument as an FLM module; false = FLM has none (the caller writes the stand-in and lists it)
bool instrumentModule (Ctx& ctx, const ChannelDef& c, flm::Module& m)
{
    const auto enable = c.muted ? 0.0f : 1.0f;
    switch (c.instrument)
    {
        case InstrumentKind::minisynth:
        {
            const auto v = miniSynthValues (c.pluginState);
            if (v.size() < 43)
                return false;
            m.type = flm::module::miniSynth;
            m.params = { enable };
            for (std::size_t k = 0; k < 43; ++k)
            {
                auto x = std::isfinite (v[k]) ? std::clamp (v[k], 0.0f, 1.0f) : 0.0f;
                if (k == 3) // MW Dest: desktop 1 = LFO amount = FLM's middle step; 0 = the filter
                    x = x >= 1.0f ? 0.5f : 0.0f;
                m.params.push_back (x);
            }
            if (! flmBuild (c))
                ctx.losses.add ("{0}: FL Studio's MiniSynth plays as FL Studio Mobile's (velocity and pan as on the phone)", c.name);
            return true;
        }
        case InstrumentKind::pocketSuperSaw:
        {
            if (c.modelParams.size() < 59)
                return false;
            m.type = flm::module::superSaw;
            m.params = { enable };
            m.params.insert (m.params.end(), c.modelParams.begin(), c.modelParams.begin() + 59);
            return true;
        }
        case InstrumentKind::gms:
        {
            if (! flmBuild (c))
                return false; // (FL's desktop GMS is another engine)
            const auto patch = gmsmobile::readState (c.pluginState);
            if (! patch)
                return false;
            m.type = flm::module::gmSynth;
            m.params.assign (patch->P.begin(), patch->P.end());
            if (! m.params.empty())
                m.params[0] = c.muted ? 0.0f : m.params[0];
            m.custom.resize (20);
            const std::int32_t a = patch->waveA, b = patch->waveB;
            std::memcpy (m.custom.data(), &a, 4);
            std::memcpy (m.custom.data() + 4, &b, 4);
            std::memcpy (m.custom.data() + 8, &patch->bendRange, 4);
            std::memcpy (m.custom.data() + 12, &patch->mwAmount, 4);
            std::memcpy (m.custom.data() + 16, &patch->mwDest, 4);
            return true;
        }
        case InstrumentKind::transistorbass:
        {
            // the 13 knobs FLM's module has, read back where transistorBassFlmState puts them
            const auto j = [&c] (int index, float fallback) {
                const auto at = transistorBassParamOffset (c.pluginState, index);
                if (at < 0 || (std::size_t) at + 4 > c.pluginState.size())
                    return fallback;
                float x;
                std::memcpy (&x, c.pluginState.data() + at, 4);
                return std::isfinite (x) ? std::clamp (x, 0.0f, 1.0f) : fallback;
            };
            if (transistorBassParamOffset (c.pluginState, 1) < 0)
                return false;
            m.type = flm::module::transistorBass;
            const auto sq = j (29, 0.5f);
            m.params = { enable, j (1, 0.5f), j (2, 0.5f), j (4, 0.5f), j (5, 0.5f), j (6, 0.5f), j (7, 0.5f), j (27, 0.5f), j (28, 0.5f),
                         sq * sq, j (31, 0.5f), j (11, 0.5f), j (12, 0.5f), j (13, 0.5f) };
            if (! flmBuild (c))
                ctx.losses.add ("{0}: FL Studio's Transistor Bass keeps the knobs FL Studio Mobile's has (no sequence, no effects)", c.name);
            return true;
        }
        case InstrumentKind::threeosc:
        {
            // FLM's 3x Osc (module 39, 149 values): the .flm import's laws (FlmImport.cpp threeOscState: FLM value →
            // the desktop's raw value and FL's channel settings) turned around. Only the state version the import writes
            // (14, 460 bytes: an .flm's or an .flp's 3x Osc in that layout); another version stands in (listed)
            const auto& st = c.pluginState;
            std::int32_t version = 0;
            if (st.size() >= 460)
                std::memcpy (&version, st.data(), 4);
            if (version != 14)
                return false;
            const auto raw = [&st] (std::size_t at) { std::int32_t x; std::memcpy (&x, st.data() + at, 4); return (double) x; };
            std::vector<float> v (149, 0.5f);
            const auto set = [&v] (std::size_t k, double x) { v[k] = (float) std::clamp (x, 0.0, 1.0); };
            v[0] = enable;
            // the octave the desktop's key sits above FLM's (FlmImport.cpp: the channel's pitch carries −12 for FLM's)
            const auto shift = (int) std::lround (c.sampler.pitch - (-12.0f));
            bool coarseCapped = false;
            for (std::size_t k = 0; k < 3; ++k)
            {
                const auto at = 4 + 4 * 7 * k;
                set (19 + k, raw (at) / 128.0 + 0.5);
                set (1 + k, raw (at + 4) / 6.0);
                const auto coarse = raw (at + 8) + shift;
                coarseCapped = coarseCapped || std::abs (coarse) > 24.0;
                set (13 + k, std::clamp (coarse, -24.0, 24.0) / 48.0 + 0.5);
                set (16 + k, raw (at + 12) / 200.0 + 0.5);
                set (7 + k, raw (at + 16) / 128.0 + 0.5);
                set (10 + k, raw (at + 20) / 100.0 + 0.5);
                v[4 + k] = st[0x58 + k] != 0 ? 1.0f : 0.0f;
            }
            if (coarseCapped)
                ctx.losses.add ("{0}: 3x Osc coarse tuning beyond FL Studio Mobile's ±24 semitones (capped)", c.name);
            set (22, raw (4 + 4 * 6) / 128.0);
            set (23, raw (4 + 4 * 13) / 128.0);
            set (26, raw (4 + 4 * 20) / 64.0);
            v[24] = st[0x5b] != 0 ? 1.0f : 0.0f;
            v[25] = st[0x5c] != 0 ? 1.0f : 0.0f;
            for (std::size_t j = 0; j < 5; ++j)
            {
                const auto r = [&] (std::size_t i) { return raw (0x60 + 0x44 * j + 4 * i); };
                const auto flags = (std::int32_t) r (0);
                v[27 + j] = (flags & 0x20) != 0 ? 1.0f : 0.0f;
                v[32 + j] = (flags & 2) != 0 ? 1.0f : 0.0f;
                set (37 + j, r (13) / 2.0);
                set (42 + j, r (9) / 65536.0);
                set (47 + j, r (10) / 65536.0);
                set (52 + j, r (11) / (j == 0 ? 128.0 : j == 4 ? 4800.0 : 256.0) + 0.5);
                set (57 + j, r (12) / 65536.0);
                if (j >= 1)
                {
                    const auto e = j - 1;
                    v[62 + e] = r (1) != 0.0 ? 1.0f : 0.0f;
                    v[66 + e] = (flags & 1) != 0 ? 1.0f : 0.0f;
                    set (70 + e, r (2) / 65536.0);
                    set (74 + e, r (3) <= 100.0 ? 0.0 : r (3) / 65536.0);
                    set (82 + e, r (4) / 65536.0);
                    set (86 + e, r (5) / 65536.0);
                    set (94 + e, r (6) / 128.0);
                    set (98 + e, r (7) / 65536.0);
                    if (e >= 1)
                        set (106 + e, r (8) / (j == 4 ? 4800.0 : 256.0) + 0.5);
                    set (78 + e, r (14) / 256.0 + 0.5);
                    set (90 + e, r (15) / 256.0 + 0.5);
                    set (102 + e, r (16) / 256.0 + 0.5);
                }
            }
            set (111, raw (0x60 + 0x15c) / 256.0);
            set (115, 0.5);
            set (112, raw (0x60 + 0x160) / 256.0);
            set (116, 0.5);
            set (110, raw (0x60 + 0x168) / 7.0);
            // the level: FLM's Volume (×2v) with the volume envelope's amount at full
            const auto gain = c.modelParams.size() >= 3 ? (double) c.modelParams[2] : 1.0;
            v[106] = 1.0f;
            set (114, gain / 2.0);
            set (113, (c.pan + 1.0) / 2.0);
            set (117, c.maxPolyphony / 99.0);
            // FL's time knobs, n = round((27^(r / 1024) − 1)·48/26) steps of 1/192 beat, turned around
            // (the import truncates 1660·v, 997·v + 450 to whole knob positions: the position is found whole, and the value
            // sits half a step inside it)
            const auto knob = [] (double beats) {
                const auto steps = [] (int r) { return std::max (1.0, std::round ((std::pow (27.0, r / 1024.0) - 1.0) * 48.0 / 26.0)); };
                const auto want = std::max (1.0, std::round (beats * 192.0));
                auto r = (int) std::floor (1024.0 * std::log (want * 26.0 / 48.0 + 1.0) / std::log (27.0));
                for (int k = std::max (0, r - 3); k <= r + 3; ++k)
                    if (steps (k) == want)
                    {
                        r = k;
                        break;
                    }
                return (double) r + 0.5;
            };
            const auto& misc = c.misc;
            v[120] = misc.mono ? 1.0f : 0.0f;
            v[119] = misc.porta ? 1.0f : 0.0f;
            set (118, knob (misc.portaBeats) / 1660.0);
            v[129] = misc.gateBeats <= 0.0f ? 1.0f : (float) std::clamp ((knob (misc.gateBeats) - 450.0) / 997.0, 0.0, 0.999);
            set (121, (120.0 - misc.key.mid) / 120.0);
            set (122, misc.key.pan / 8.0 + 0.5);
            set (123, misc.key.modX / 2.0 + 0.5);
            set (124, misc.key.modY / 2.0 + 0.5);
            set (125, misc.velocity.mid / 120.0);
            set (126, misc.velocity.pan / 8.0 + 0.5);
            set (127, misc.velocity.modX / 2.0 + 0.5);
            set (128, misc.velocity.modY / 2.0 + 0.5);
            set (130, (misc.echo.echoes - 1) / 9.0);
            set (131, misc.echo.feedback / 2.0);
            set (132, misc.echo.timeBeats / 2.0);
            set (133, (misc.echo.pan + 4.0) / 8.0);
            set (134, misc.echo.pitch / 2400.0 + 0.5);
            set (135, (misc.echo.modX + 1.0) / 2.0);
            set (136, (misc.echo.modY + 1.0) / 2.0);
            v[137] = misc.echo.pingPong ? 1.0f : 0.0f;
            v[138] = misc.echo.fat ? 1.0f : 0.0f;
            static constexpr int directions[] = { 0, 1, 2, 3, 5, 4 };
            v[139] = 0.0f;
            if (c.arp.enabled())
            {
                for (int i = 1; i < 6; ++i)
                    if (directions[i] == c.arp.direction)
                        v[139] = (float) i / 5.0f;
                set (142, c.arp.range / 6.0);
                set (140, (knob (c.arp.stepBeats) - 450.0) / 997.0);
                set (141, c.arp.gate);
                set (143, (c.arp.octaveRepeat - 1) / 5.0);
                v[144] = c.arp.slide ? 1.0f : 0.0f;
                set (145, (! c.arp.chordSustain ? 0.0 : c.arp.chord == std::vector<int> { 0 } ? 2.0 : 1.0) / 90.0);
            }
            v[146] = v[147] = v[148] = 0.0f;
            if (! c.samplePath.empty())
                ctx.losses.add ("{0}: 3x Osc custom shape (the sample stays on this computer)", c.name);
            m.type = flm::module::threeOsc;
            m.params = std::move (v);
            return true;
        }
        case InstrumentKind::sampler:
        {
            // FLM's DW Sampler (the .flm import's own engine): its 34 values, and its zones as the module's "10WD" state
            if (! flmdw::isChannel (c) || c.zones.empty())
                return false;
            m.type = flm::module::dwSampler;
            for (int k = 0; k < flmdw::paramCount; ++k)
                m.params.push_back (flmdw::value (c, k));
            m.params[flmdw::param::enable] = enable;
            Bytes cstm;
            put (cstm, "10WD", 4);
            for (std::size_t i = 0; i < c.zones.size(); ++i)
            {
                const auto& z = c.zones[i];
                const auto ref = sampleRef (ctx, z.path, {});
                std::string where = ref.root.empty() ? std::string() : ref.root + std::string (1, '\0');
                auto path = ref.path;
                std::replace (path.begin(), path.end(), '/', '\\');
                where += path;
                where.resize (1024, '\0');
                put (cstm, "ZONE", 4);
                putU32 (cstm, 1065);
                put (cstm, where.data(), 1024);
                for (const auto b : { z.rootKey, z.lowKey, z.highKey, z.lowVelocity, z.highVelocity })
                    cstm.push_back ((std::uint8_t) std::clamp (b, 0, 255));
                putF32 (cstm, z.gain);
                putF32 (cstm, 0.0f);
                putU32 (cstm, z.loopMode != 0 ? 2u : 0u);
                putU32 (cstm, (std::uint32_t) (std::int32_t) z.loopStart);
                putU32 (cstm, (std::uint32_t) (std::int32_t) z.loopEnd);
                const auto decayFlag = (std::size_t) (flmdw::head + flmdw::paramCount) + i;
                putF32 (cstm, 0.0f);
                putF32 (cstm, decayFlag < c.modelParams.size() ? c.modelParams[decayFlag] : 1.0f);
                putF32 (cstm, 1.0f);
                putF32 (cstm, 0.1f);
                if (std::abs (z.tune) > 1.0e-3f)
                    ctx.losses.add ("Zone tuning of sampler zones: {0}", c.name);
            }
            put (cstm, "MIDI", 4);
            putU32 (cstm, 2);
            cstm.push_back ((std::uint8_t) std::clamp ((int) std::lround (c.modelParams.size() > 1 ? c.modelParams[1] : 4.0f), 0, 48));
            cstm.push_back (c.modelParams.size() > 2 && c.modelParams[2] < 0.5f ? 0 : 1);
            m.custom = std::move (cstm);
            return true;
        }
        default: return false;
    }
}

// The instrument wrapper (FlmImport.cpp applyGeneratorWrapper turned around): the channel's level below 0 dB as Post
// Gain × Mix (a generator's dry buffer is silent, so the product is its level), its pan as the Wet Pan
void applyWrapper (const ChannelDef& c, flm::Module& m)
{
    const auto gain = (double) channelGain (c.volume);
    float post = 0.5f, mix = 1.0f;
    if (gain >= 0.1)
        post = flmPostGainValue (std::min (gain, 10.0));
    else
    {
        post = 0.0f; // −20 dB, the rest on Mix
        mix = (float) std::clamp (gain / 0.1, 0.0, 1.0);
    }
    m.reserved = { post, mix, std::clamp ((c.pan + 1.0f) * 0.5f, 0.0f, 1.0f) };
}

// --- effects ---------------------------------------------------------------------------------------------------------
int nextInstance (const flm::Rack& rack)
{
    std::uint32_t id = 1;
    for (const auto& m : rack.modules)
        id = std::max (id, m.instanceId + 1);
    return (int) id;
}

bool pocketSlotModule (const EffectSlot& slot, flm::Module& m)
{
    const auto* pm = pocketModule (slot.kind);
    if (pm == nullptr)
        return false;
    m.type = pm->type;
    m.params = { slot.enabled ? 1.0f : 0.0f };
    const auto& info = effectInfo (slot.kind);
    auto n = std::min<int> ((int) info.params.size(), pm->count - 1);
    if (slot.kind == EffectKind::pocketTranceDelay)
    {
        n = std::min (n, 6); // (LYNX's 7th value is FLM's saved flag, one byte of CSTM)
        m.custom = { (std::uint8_t) (slot.params[6] >= 0.5f ? 1 : 0) };
    }
    for (int k = 0; k < n; ++k)
        m.params.push_back (std::clamp (slot.params[(std::size_t) k], 0.0f, 1.0f));
    m.reserved = { 0.5f, std::clamp (slot.mix, 0.0f, 1.0f), 0.5f };
    return true;
}

// a utility that only changes the level: its gain in dB (else nullopt)
std::optional<double> utilityGainDb (const EffectSlot& s)
{
    if (s.kind != EffectKind::utility || ! s.enabled)
        return std::nullopt;
    const auto plain = makeEffectSlot (EffectKind::utility);
    for (std::size_t k = 1; k < effectInfo (EffectKind::utility).params.size(); ++k)
        if (std::abs (s.params[k] - plain.params[k]) > 1.0e-5f)
            return std::nullopt;
    return ((double) s.params[0] - 0.5) * 48.0;
}

// the slots of an insert as rack modules, in order; what has no FLM module is listed
void effectModules (Ctx& ctx, const std::vector<EffectSlot>& slots, flm::Rack& rack, const std::string& owner)
{
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        const auto& slot = slots[i];
        flm::Module m;
        if (slot.kind == EffectKind::parallel && slot.branches.size() == 2 && slot.branches[0].chain.empty() && slot.branches[1].chain.size() == 1
            && pocketSlotModule (slot.branches[1].chain.front(), m))
        {
            // FlmEffects' blend container: dry × (1 − Mix) + Post Gain × wet × Mix
            const auto mix = std::clamp (1.0 - (double) slot.branches[0].gain, 0.0, 1.0);
            const auto post = mix > 1.0e-6 ? (double) slot.branches[1].gain / mix : 1.0;
            m.reserved = { flmPostGainValue (post), (float) mix, 0.5f };
            if (! slot.enabled)
                m.params[0] = 0.0f;
        }
        else if (! pocketSlotModule (slot, m))
        {
            if (slot.kind == EffectKind::utility && ! rack.modules.empty() && utilityGainDb (slot))
            {
                // a level after a module: that module's Post Gain (±20 dB)
                auto& prev = rack.modules.back();
                const auto db = *utilityGainDb (slot) + (40.0 * (double) prev.reserved[0] - 20.0);
                if (std::abs (db) <= 20.0 + 1.0e-6)
                {
                    prev.reserved[0] = (float) ((db + 20.0) / 40.0);
                    continue;
                }
            }
            if (slot.enabled)
                ctx.losses.add ("Effects FL Studio Mobile doesn't have (left out): {0}", owner + " › " + effectInfo (slot.kind).name);
            continue;
        }
        m.instanceId = (std::uint32_t) nextInstance (rack);
        if (ctx.insertNow >= 0)
            ctx.slotModule[{ ctx.insertNow, (int) i }] = m.instanceId;
        rack.modules.push_back (std::move (m));
    }
}

// --- the mixer -------------------------------------------------------------------------------------------------------
std::vector<float> rackParamsFor (const MixerTrack* t)
{
    std::vector<float> p { 0.853125f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f };
    if (t != nullptr)
    {
        p[0] = flmFaderValue (faderGain (t->volume));
        p[1] = std::clamp ((t->pan + 1.0f) * 0.5f, 0.0f, 1.0f);
        p[2] = t->muted ? 1.0f : 0.0f;
        p[3] = t->solo ? 1.0f : 0.0f;
    }
    return p;
}

std::vector<std::uint32_t> sendWords (int targetId, double gain)
{
    const auto f = [] (float x) {
        std::uint32_t u;
        std::memcpy (&u, &x, 4);
        return u;
    };
    return { (std::uint32_t) targetId, f (1.0f), f ((float) std::min (gain, 1.0)), f (1.0f), f (0.853125f), f (flmFaderValue (gain)) };
}

// --- the notes -------------------------------------------------------------------------------------------------------
std::uint8_t velocityByte (float v) noexcept { return (std::uint8_t) std::clamp ((int) std::lround (255.0f * std::clamp (v, 0.0f, 1.0f)), 1, 255); }
std::uint8_t panByte (float pan) noexcept
{
    return std::abs (pan) < 1.0e-4f ? (std::uint8_t) 127 : (std::uint8_t) std::clamp ((int) std::lround ((pan + 1.0f) * 0.5f * 255.0f), 0, 255);
}
} // namespace

namespace flmexport
{
std::string checksum (const std::vector<std::uint8_t>& bytes);
}

FlmExportResult exportFlm (const Song& song, const FlmExportOptions& options)
{
    FlmExportResult result;
    Ctx ctx { song, options, {}, {}, {}, {}, {}, {}, -1 };
    auto& p = ctx.project;
    const auto title = ! options.title.empty() ? options.title : ! song.info.title.empty() ? song.info.title : std::string ("LYNX SHARE");
    ctx.samplesFolder = ! options.samplesFolder.empty() ? options.samplesFolder : title + " Samples";

    // the song (FLM_FORMAT §2)
    p.title = title;
    p.tempo = song.bpm;
    p.numerator = std::clamp (song.beatsPerBar, 1, 32);
    p.denominator = 4; // the grid's steps per beat (FLM's TDIV), not a note value
    if (song.beatUnit != 4)
        ctx.losses.add ("Time signature {0} (FL Studio Mobile counts beats per bar only)", std::to_string (song.beatsPerBar) + "/" + std::to_string (song.beatUnit));
    if (const auto key = projectkey::get (song))
    {
        const auto [root, scale] = flmKeyLockerOf (*key);
        p.root = root;
        p.scale = scale;
        if (scale == 0)
            ctx.losses.add ("The project's scale (FL Studio Mobile's key locker has no scale with its notes)", {}, false);
    }
    if (! song.keyboardChords.empty())
        p.chordSetJson = flmChordSetJson (song.keyboardChords);
    p.appVersion = options.appVersion;
    p.created = utcDate (song.info.created > 0.0 ? song.info.created : options.nowSeconds);
    p.saved = utcDate (options.nowSeconds);
    p.editSeconds = std::max (0.0, song.info.workSeconds);
    p.author = song.info.author;
    p.notes = song.info.comments;
    p.originalName = title;
    if (song.loopEnd > song.loopStart)
    {
        p.loopStart = beats (song.loopStart);
        p.loopEnd = beats (song.loopEnd);
        if (! song.loopOff)
            ctx.losses.add ("The loop is saved as FL Studio Mobile's play loop, which starts switched off", {}, false);
    }

    // --- who uses each insert: the channels that play into it and the Playlist tracks whose audio clips do
    const auto inserts = (int) song.mixer.size();
    const auto insertOf = [&] (int m) { return m > 0 && m < inserts ? m : 0; };
    std::vector<int> users ((std::size_t) std::max (1, inserts), 0);
    std::vector<bool> receives ((std::size_t) std::max (1, inserts), false);
    for (int t = 1; t < inserts; ++t)
        for (const auto& s : song.mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < inserts)
                receives[(std::size_t) s.target] = true;

    // which instrument channels play anything, and the audio tracks
    std::set<int> playedPatterns;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
            playedPatterns.insert (c.pattern);
    const auto noSound = [] (InstrumentKind k) {
        return k == InstrumentKind::controller || k == InstrumentKind::midiOut || k == InstrumentKind::dashboard || k == InstrumentKind::voltage
               || k == InstrumentKind::vfx || k == InstrumentKind::fruitydance || k == InstrumentKind::layer || k == InstrumentKind::audioClip;
    };
    const auto isPad = [] (const ChannelDef& c) {
        // (a sampler whose file is missing stays a pad: its notes and settings go over, the missing file is listed)
        return c.instrument == InstrumentKind::sampler && c.zones.empty() && ! flmdw::isChannel (c) && ! flmslicer::isChannel (c) && c.sampler.slices.empty();
    };

    struct Out
    {
        flm::Rack rack;
        flm::Channel channel;
        int insert = 0;              // the LYNX insert it stands for (its own) or plays into
        bool drums = false;
        std::vector<int> pads;       // drums: the LYNX channels of its kit, kit order
        int instrument = -1;         // note channel: the LYNX channel (a Layer: its children are the rack's instruments)
        std::vector<int> children;
        int audioTrack = -1;         // audio channel: the Playlist track
        bool bus = false;
        int busInsert = 0;
    };
    std::vector<Out> outs;
    std::map<int, int> drumOutOfInsert; // insert → the drum channel its pads join
    std::map<int, int> outOfChannel;    // LYNX channel → out

    // a Layer is one FLM rack with several instruments (each its key range): the .flm import's own reading of such a rack
    std::map<int, int> layerOf; // child → its Layer
    for (int i = 0; i < (int) song.channels.size(); ++i)
    {
        const auto& c = song.channels[(std::size_t) i];
        if (c.instrument == InstrumentKind::layer)
        {
            for (const auto k : c.layer.children)
                if (k >= 0 && k < (int) song.channels.size() && k != i && ! noSound (song.channels[(std::size_t) k].instrument))
                    layerOf.emplace (k, i);
        }
        else if (noSound (c.instrument) && c.instrument != InstrumentKind::audioClip)
            ctx.losses.add ("Channels that make no sound of their own (left out): {0}", c.name, false);
    }

    for (int i = 0; i < (int) song.channels.size(); ++i)
    {
        const auto& c = song.channels[(std::size_t) i];
        if (c.instrument == InstrumentKind::layer)
        {
            std::vector<int> kids;
            for (const auto& [child, layer] : layerOf)
                if (layer == i)
                    kids.push_back (child);
            if (kids.empty())
                continue;
            Out o;
            o.instrument = i;
            o.children = kids;
            o.insert = insertOf (song.channels[(std::size_t) kids.front()].mixerTrack);
            outs.push_back (std::move (o));
            outOfChannel[i] = (int) outs.size() - 1;
            for (const auto k : kids)
                outOfChannel[k] = (int) outs.size() - 1;
            ++users[(std::size_t) outs.back().insert];
            continue;
        }
        if (noSound (c.instrument) || layerOf.count (i) > 0)
            continue;
        const auto ins = insertOf (c.mixerTrack);
        if (isPad (c))
        {
            auto it = drumOutOfInsert.find (ins);
            if (it == drumOutOfInsert.end() || outs[(std::size_t) it->second].pads.size() >= 16)
            {
                Out o;
                o.drums = true;
                o.insert = ins;
                outs.push_back (std::move (o));
                it = drumOutOfInsert.insert_or_assign (ins, (int) outs.size() - 1).first;
                ++users[(std::size_t) ins];
            }
            outs[(std::size_t) it->second].pads.push_back (i);
            outOfChannel[i] = it->second;
            continue;
        }
        Out o;
        o.instrument = i;
        o.insert = ins;
        outs.push_back (std::move (o));
        outOfChannel[i] = (int) outs.size() - 1;
        ++users[(std::size_t) ins];
    }
    {
        // one audio channel per Playlist track that holds audio clips (the insert of its clips' channel)
        std::map<int, int> insertOfTrack;
        for (const auto& clip : song.playlist)
        {
            if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size())
                continue;
            const auto ins = insertOf (song.channels[(std::size_t) clip.channel].mixerTrack);
            const auto [it, added] = insertOfTrack.emplace (clip.track, ins);
            if (! added && it->second != ins)
                ctx.losses.add ("Audio clips of one track that play into different inserts: the track's first insert is used ({0})",
                                clip.track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) clip.track].name : std::to_string (clip.track + 1));
        }
        for (const auto& [track, ins] : insertOfTrack)
        {
            Out o;
            o.audioTrack = track;
            o.insert = ins;
            outs.push_back (std::move (o));
            ++users[(std::size_t) ins];
        }
    }
    // buses: an insert shared by several, or one that receives sends
    std::map<int, int> busOutOfInsert;
    for (int t = 1; t < inserts; ++t)
    {
        const auto& m = song.mixer[(std::size_t) t];
        (void) m;
        if (users[(std::size_t) t] > 1 || receives[(std::size_t) t])
        {
            Out o;
            o.bus = true;
            o.busInsert = t;
            o.insert = t;
            outs.push_back (std::move (o));
            busOutOfInsert[t] = (int) outs.size() - 1;
        }
    }

    // ids: the master 0, then the others in order
    int nextId = 1;
    for (auto& o : outs)
        o.channel.channelId = nextId++;

    // --- each FLM channel's rack, header and routes
    const auto routesOf = [&] (const Out& o, int insert, bool ownsInsert) {
        std::vector<std::vector<std::uint32_t>> sends;
        if (! ownsInsert && insert > 0)
        {
            sends.push_back (sendWords (outs[(std::size_t) busOutOfInsert.at (insert)].channel.channelId, 1.0));
            return sends;
        }
        const MixerTrack* t = insert > 0 && insert < inserts ? &song.mixer[(std::size_t) insert] : nullptr;
        if (t == nullptr || t->toMaster)
            sends.push_back (sendWords (0, t == nullptr ? 1.0 : (double) t->masterLevel));
        if (t != nullptr)
            for (const auto& s : t->sends)
            {
                if (const auto b = busOutOfInsert.find (s.target); b != busOutOfInsert.end() && b->second != (int) (&o - outs.data()))
                    sends.push_back (sendWords (outs[(std::size_t) b->second].channel.channelId, (double) s.level));
                else
                    ctx.losses.add ("Sends FL Studio Mobile can't route: {0}", t->name);
            }
        return sends;
    };
    const auto mixerLosses = [&] (int insert) {
        if (insert <= 0 || insert >= inserts)
            return;
        const auto& t = song.mixer[(std::size_t) insert];
        const auto name = t.name.empty() ? "Insert " + std::to_string (insert) : t.name;
        if (t.eq.active())
            ctx.losses.add ("Mixer track EQ (FL Studio Mobile's tracks have none): {0}", name);
        if (std::abs (t.stereo) > 1.0e-4f || t.invertPhase || t.swapChannels)
            ctx.losses.add ("Mixer track stereo separation, polarity or swap: {0}", name);
    };

    std::vector<bool> insertDone ((std::size_t) std::max (1, inserts), false);
    for (auto& o : outs)
    {
        auto& ch = o.channel;
        ch.kind = o.bus ? flm::kindBus : flm::kindNormal;
        o.rack.kind = ch.kind;
        ch.order = (double) ch.channelId;
        // the rack stands for the insert when this channel is its only user (or it is the bus of a shared one)
        const auto owns = o.bus || (o.insert > 0 && users[(std::size_t) o.insert] == 1 && busOutOfInsert.count (o.insert) == 0);
        const MixerTrack* t = owns && o.insert > 0 ? &song.mixer[(std::size_t) o.insert] : nullptr;
        o.rack.params = rackParamsFor (t);
        std::uint32_t colour = t != nullptr ? t->colour : 0;
        if (o.bus)
        {
            const auto& m = song.mixer[(std::size_t) o.busInsert];
            ch.name = m.name.empty() ? "Insert " + std::to_string (o.busInsert) : m.name;
            ch.typeName = "Bus " + std::to_string (ch.channelId);
            flm::Lane lane;
            lane.rawKind = 1; // a bus has its automation lane only
            lane.targetModule = 0;
            lane.targetParam = 0;
            lane.name = "Level";
            ch.lanes.push_back (lane);
        }
        else if (o.drums)
        {
            const auto& first = song.channels[(std::size_t) o.pads.front()];
            ch.name = o.insert > 0 && ! song.mixer[(std::size_t) o.insert].name.empty() && owns ? song.mixer[(std::size_t) o.insert].name : (o.pads.size() > 1 ? std::string ("Drums") : first.name);
            ch.typeName = "Drums " + std::to_string (ch.channelId);
            if (colour == 0)
                colour = first.colour;
            // the rack's held pitch bend (RPRM [4], 48v − 24 semitones on every pad; the import folds it into each pad's
            // pitch): a fraction of a semitone all the pads share goes back there, the whole semitones stay each pad's Speed
            float padBend = 0.0f;
            {
                const auto frac = [&song] (int ch) { const auto p = song.channels[(std::size_t) ch].sampler.pitch; return p - std::round (p); };
                const auto f0 = frac (o.pads.front());
                if (std::abs (f0) > 1.0e-3f && std::all_of (o.pads.begin(), o.pads.end(), [&] (int ch) { return std::abs (frac (ch) - f0) < 1.0e-3f; }))
                {
                    // the bend's whole semitones: the pitch most pads share (they sit at Speed 0, as in FLM's own kits).
                    // FLM's Speed and the rack's bend are not the same transposition ([EXEC] REA_rodada190og §4: a kit
                    // written as Speed + the bend's fraction plays 5.6 ms late against the song), so the split matters
                    std::map<long, int> count;
                    for (const auto ch : o.pads)
                        ++count[std::lround (song.channels[(std::size_t) ch].sampler.pitch * 1000.0f)];
                    const auto mode = std::max_element (count.begin(), count.end(), [] (const auto& a, const auto& b) { return a.second < b.second; })->first;
                    padBend = (float) mode / 1000.0f;
                    if (std::abs (padBend) > 24.0f || std::any_of (o.pads.begin(), o.pads.end(), [&] (int ch) {
                            return std::abs (song.channels[(std::size_t) ch].sampler.pitch - padBend) > 24.0f; }))
                        padBend = f0;
                }
            }
            for (std::size_t k = 0; k < o.pads.size(); ++k)
            {
                const auto& c = song.channels[(std::size_t) o.pads[k]];
                auto s = sampleRef (ctx, c.samplePath, c.name);
                if (c.samplePath.empty())
                    ctx.losses.add ("Samples not found on this computer ({0}): their notes stay, FL Studio Mobile plays nothing for them", c.name);
                s.slotId = (int) k + 1;
                namespace sp = flm::samplepad;
                s.params.assign (sp::defaults, sp::defaults + sp::count);
                const auto gain = (double) channelGain (c.volume) * std::pow (10.0, (double) c.sampler.gainDb / 20.0);
                s.params[sp::level] = gain > 0.001 ? flmFaderValue (gain) : 0.0f;
                s.params[sp::pan] = std::clamp ((c.pan + 1.0f) * 0.5f, 0.0f, 1.0f);
                s.params[sp::speed] = flmSpeedValue ((int) std::lround (c.sampler.pitch - padBend));
                s.params[sp::mute] = c.muted ? 1.0f : 0.0f;
                if (c.sampler.flmPad.size() >= 6)
                {
                    s.params[sp::egAttack] = c.sampler.flmPad[0];
                    s.params[sp::egDecay] = c.sampler.flmPad[1];
                    s.params[sp::egSustain] = c.sampler.flmPad[2];
                    s.params[sp::egRelease] = c.sampler.flmPad[3];
                    s.params[sp::length] = c.sampler.flmPad[4];
                    s.params[sp::start] = c.sampler.flmPad[5];
                }
                else if (c.sampler.attack > 0.0f || c.sampler.decay > 0.0f || c.sampler.sustain < 1.0f || c.sampler.release > 0.0f)
                    ctx.losses.add ("Sampler envelopes (FL Studio Mobile's drum pads have their own): {0}", c.name);
                if (std::abs (c.sampler.pitch - padBend - std::round (c.sampler.pitch - padBend)) > 1.0e-3f)
                    ctx.losses.add ("Sampler pitch between semitones (a pad's Speed has whole semitones): {0}", c.name);
                s.reverse = c.sampler.reverse;
                s.polyphonic = ! c.sampler.cut;
                if (c.cutGroup > 0 && c.cutGroup == c.cutBy && c.cutGroup <= 3)
                    s.cutGroup = c.cutGroup;
                else if (c.cutGroup > 0 || c.cutBy > 0)
                    ctx.losses.add ("Cut groups FL Studio Mobile's A / B / C can't hold: {0}", c.name);
                if (c.sampler.stretchBeats > 0.0f || c.sampler.regionStart > 0.0f || c.sampler.regionEnd < 1.0f || c.sampler.cutoff < 19999.0f)
                    ctx.losses.add ("Sampler settings a drum pad has no place for (stretch, region, filter): {0}", c.name);
                o.rack.kit.push_back (std::move (s));
            }
            if (padBend != 0.0f)
                o.rack.params[4] = std::clamp (0.5f + padBend / 48.0f, 0.0f, 1.0f);
            flm::Lane lane;
            lane.rawKind = 3;
            lane.name = ch.name;
            ch.lanes.push_back (lane);
        }
        else if (o.audioTrack >= 0)
        {
            const auto& tr = o.audioTrack < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) o.audioTrack] : PlaylistTrack {};
            ch.name = ! tr.name.empty() ? tr.name : "Audio " + std::to_string (ch.channelId);
            ch.typeName = "Audio " + std::to_string (ch.channelId);
            if (colour == 0)
                colour = tr.colour;
            flm::SampleRef empty;
            empty.name = "<empty>";
            empty.params.assign (flm::samplepad::defaults, flm::samplepad::defaults + flm::samplepad::count);
            o.rack.kit.push_back (empty); // (FLM's audio tracks keep one empty slot)
            flm::Lane lane;
            lane.rawKind = 2;
            lane.name = ch.name;
            ch.lanes.push_back (lane);
        }
        else
        {
            const auto& c = song.channels[(std::size_t) o.instrument];
            ch.name = c.name;
            ch.typeName = "Instrument " + std::to_string (ch.channelId);
            ch.userName = c.name;
            if (colour == 0)
                colour = c.colour;
            std::vector<int> players = o.children.empty() ? std::vector<int> { o.instrument } : o.children;
            for (const auto k : players)
            {
                const auto& pc = song.channels[(std::size_t) k];
                flm::Module m;
                bool stoodIn = false;
                if (! instrumentModule (ctx, pc, m))
                {
                    stoodIn = true;
                    m = {};
                    m.type = flm::module::miniSynth;
                    m.params = { pc.muted ? 0.0f : 1.0f }; // (FLM fills the rest with MiniSynth's defaults)
                    ctx.losses.add ("Instruments FL Studio Mobile doesn't have: their notes play FL Studio Mobile's MiniSynth ({0})", pc.name);
                }
                m.instanceId = (std::uint32_t) nextInstance (o.rack);
                m.keyLow = std::clamp (pc.keyLow, 0, 127);
                m.keyHigh = std::clamp (pc.keyHigh, 0, 127);
                applyWrapper (pc, m);
                if (m.type == flm::module::threeOsc && ! stoodIn && m.reserved.size() >= 3)
                    m.reserved[2] = 0.5f; // (the 3x Osc's own pan holds the channel's)
                o.rack.modules.push_back (std::move (m));
                // the rack's held pitch bend (the import puts bend × the module's range on the channel's pitch, as its
                // misc.pitchRange says): back on RPRM [4] for a rack with one instrument
                if (pc.instrument == InstrumentKind::threeosc && ! stoodIn)
                    ; // (the 3x Osc's octave and tuning went into its coarse values)
                else if (std::abs (pc.sampler.pitch) > 1.0e-3f && o.children.empty() && stoodIn == false && pc.misc.pitchRange > 0
                    && std::abs (pc.sampler.pitch) <= (float) pc.misc.pitchRange + 1.0e-3f)
                    o.rack.params[4] = std::clamp (0.5f + pc.sampler.pitch / (2.0f * (float) pc.misc.pitchRange), 0.0f, 1.0f);
                else if (std::abs (pc.sampler.pitch) > 1.0e-3f && pc.instrument != InstrumentKind::sampler)
                    ctx.losses.add ("Channel pitch (FL Studio Mobile's instruments have their own tuning): {0}", pc.name);
                if (pc.arp.enabled())
                    ctx.losses.add ("Channel arpeggiators (left out): {0}", pc.name);
            }
            if (! o.children.empty() && (std::abs (c.volume - 0.78f) > 1e-3f && std::abs (c.volume - 1.0f) > 1e-3f))
                ctx.losses.add ("A Layer's own level (FL Studio Mobile's rack has its instruments' levels): {0}", c.name);
            flm::Lane lane;
            lane.rawKind = 0;
            lane.name = c.name;
            ch.lanes.push_back (lane);
        }
        if (t != nullptr || o.bus)
        {
            const auto insert = o.bus ? o.busInsert : o.insert;
            if (! insertDone[(std::size_t) insert])
            {
                ctx.insertNow = insert;
                effectModules (ctx, song.mixer[(std::size_t) insert].effects, o.rack, ch.name);
                ctx.insertNow = -1;
                mixerLosses (insert);
                insertDone[(std::size_t) insert] = true;
            }
        }
        ch.hue = flm::hueOfColour (colour);
        ch.sends = routesOf (o, o.insert, owns || o.bus);
    }
    // inserts nothing plays into (left out) and the Master (FLM's master rack)
    for (int t = 1; t < inserts; ++t)
        if (! insertDone[(std::size_t) t] && users[(std::size_t) t] == 0 && busOutOfInsert.count (t) == 0 && ! song.mixer[(std::size_t) t].effects.empty())
            ctx.losses.add ("Mixer tracks nothing plays into (left out): {0}", song.mixer[(std::size_t) t].name.empty() ? "Insert " + std::to_string (t) : song.mixer[(std::size_t) t].name, false);
    flm::Rack masterRack;
    masterRack.kind = flm::kindMaster;
    masterRack.params = rackParamsFor (inserts > 0 ? &song.mixer[0] : nullptr);
    if (inserts > 0)
    {
        ctx.insertNow = 0;
        effectModules (ctx, song.mixer[0].effects, masterRack, "Master");
        ctx.insertNow = -1;
        mixerLosses (0);
    }
    flm::Channel master;
    master.kind = flm::kindMaster;
    master.name = "MASTER";
    master.typeName = "MASTER";
    master.channelId = 0;
    {
        flm::Lane lane;
        lane.rawKind = 1;
        lane.targetModule = 0;
        lane.targetParam = 0;
        lane.name = "Level";
        master.lanes.push_back (lane);
    }

    // --- the notes: every pattern clip of the Playlist on its channels' lanes (one content per pattern and channel)
    std::map<std::pair<int, int>, std::uint32_t> contentOf; // (out, pattern) → content id
    std::map<int, std::uint32_t> nextContent;
    int mutedNotes = 0, repeats = 0, chance = 0, finePitch = 0, outsidePads = 0;
    std::vector<Clip> clips;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern)
            clips.push_back (c);
    if (clips.empty() && ! song.patterns.empty())
    {
        // a song never arranged: the current pattern from the start, once
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
        c.length = song.patterns[(std::size_t) c.pattern].effectiveLength (song.meter());
        clips.push_back (c);
        ctx.losses.add ("The song had no Playlist clips: the current pattern is placed once at the start", {}, false);
    }
    std::sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start != b.start ? a.start < b.start : a.track < b.track; });
    for (const auto& clip : clips)
    {
        if (clip.pattern < 0 || clip.pattern >= (int) song.patterns.size() || clip.muted)
            continue;
        const auto& pat = song.patterns[(std::size_t) clip.pattern];
        const auto patLength = pat.effectiveLength (song.meter());
        // the notes per FLM channel
        std::map<int, std::vector<const Note*>> byOut;
        for (const auto& n : pat.notes)
        {
            if (n.channel < 0 || n.channel >= (int) song.channels.size())
                continue;
            const auto it = outOfChannel.find (n.channel);
            if (it == outOfChannel.end())
                continue;
            byOut[it->second].push_back (&n);
        }
        for (auto& [oi, notes] : byOut)
        {
            auto& o = outs[(std::size_t) oi];
            auto& lane = o.channel.lanes.front();
            auto& id = contentOf[{ oi, clip.pattern }];
            if (id == 0) // (a pattern placed again is the same content: FLM links clips by this id, each keeps its events)
                id = ++nextContent[oi];
            flm::Clip fc;
            fc.startTick = flmTicks (clip.start);
            fc.length = beats (clip.length);
            fc.loopLength = beats (patLength);
            fc.offset = beats (clip.offset);
            fc.contentId = id;
            fc.sourceLength = fc.loopLength;
            for (const auto* n : notes)
            {
                if (n->muted)
                {
                    ++mutedNotes;
                    continue;
                }
                flm::Event e;
                e.tick = flmTicks (n->start);
                e.length = beats (n->length);
                e.velocity = velocityByte (n->velocity);
                e.pan = panByte (n->pan);
                if (o.drums)
                {
                    const auto pad = std::find (o.pads.begin(), o.pads.end(), n->channel);
                    e.key = (int) (pad - o.pads.begin());
                    // the note's pitch against the pad's own: 2^(2p − 1), one octave either way
                    const auto semis = (double) (n->key - 60) + (double) n->finePitch / 100.0;
                    if (std::abs (semis) > 12.0)
                        ++outsidePads;
                    if (std::abs (semis) > 1.0e-6)
                        e.value = (std::uint16_t) std::clamp ((int) std::lround ((std::clamp (semis, -12.0, 12.0) / 12.0 + 1.0) * 0.5 * 65534.0), 0, 65534);
                }
                else
                {
                    e.key = std::clamp (n->key, 0, 127);
                    e.slide = n->slide ? 1 : 0;
                    if (std::abs (n->finePitch) > 0.5f)
                        ++finePitch;
                }
                repeats += n->repeat > 0 ? 1 : 0;
                chance += n->hasChance() ? 1 : 0;
                fc.events.push_back (e);
            }
            std::stable_sort (fc.events.begin(), fc.events.end(), [] (const flm::Event& a, const flm::Event& b) { return a.tick < b.tick; });
            lane.clips.push_back (std::move (fc));
        }
    }
    if (mutedNotes > 0)
        ctx.losses.add ("Muted notes (left out): {0}", std::to_string (mutedNotes), false);
    if (repeats > 0)
        ctx.losses.add ("Note repeats ({0} notes play once)", std::to_string (repeats));
    if (chance > 0)
        ctx.losses.add ("Note chance and velocity range ({0} notes always play)", std::to_string (chance));
    if (finePitch > 0)
        ctx.losses.add ("Fine pitch of notes on instruments ({0} notes)", std::to_string (finePitch));
    if (outsidePads > 0)
        ctx.losses.add ("Drum notes pitched more than an octave from the pad ({0} notes, held at one octave)", std::to_string (outsidePads));
    {
        bool sharedPatterns = false, named = false;
        std::map<int, std::set<int>> outsOfPattern;
        for (const auto& [key, id] : contentOf)
            outsOfPattern[key.second].insert (key.first);
        for (const auto& [pat, set] : outsOfPattern)
        {
            sharedPatterns = sharedPatterns || set.size() > 1;
            named = named || ! song.patterns[(std::size_t) pat].name.empty();
        }
        if (sharedPatterns || named)
            ctx.losses.add ("Pattern names, and patterns shared by several channels (FL Studio Mobile keeps one pattern per track)", {}, false);
    }

    // --- audio clips
    int fades = 0, envelopes = 0, stretched = 0;
    for (auto& o : outs)
    {
        if (o.audioTrack < 0)
            continue;
        std::uint32_t content = 0;
        for (const auto& clip : song.playlist)
        {
            if (clip.kind != ClipKind::audio || clip.track != o.audioTrack || clip.channel < 0 || clip.channel >= (int) song.channels.size())
                continue;
            const auto& c = song.channels[(std::size_t) clip.channel];
            flm::Clip fc;
            fc.startTick = flmTicks (clip.start);
            fc.length = beats (clip.length);
            fc.offset = beats (clip.offset);
            fc.contentId = ++content;
            const auto seconds = options.sampleSeconds && ! c.samplePath.empty() ? options.sampleSeconds (c.samplePath) : 0.0;
            const auto sourceBeats = seconds > 0.0 ? seconds * song.bpm / 60.0 * clip.stretch : 0.0;
            fc.sourceLength = sourceBeats > 0.0 ? sourceBeats : fc.offset + fc.length;
            fc.loopLength = std::max (fc.sourceLength, fc.offset + fc.length); // (a LYNX audio clip does not repeat)
            auto s = sampleRef (ctx, c.samplePath, fileStem (c.samplePath));
            namespace sp = flm::samplepad;
            s.params.assign (sp::defaults, sp::defaults + sp::count);
            const auto gain = (double) clip.gain * (double) channelGain (c.volume) / (double) channelGain (audioClipUnityVolume);
            s.params[sp::level] = flmFaderValue (gain);
            s.params[sp::pan] = std::clamp ((clip.pan + c.pan + 1.0f) * 0.5f, 0.0f, 1.0f);
            s.params[sp::speed] = flmSpeedValue ((int) std::lround (c.sampler.keepPitch ? 0.0f : c.sampler.pitch));
            s.params[sp::mute] = clip.muted ? 1.0f : 0.0f;
            s.reverse = clip.reversed != c.sampler.reverse;
            s.pitch = std::round (clip.pitch + (c.sampler.keepPitch ? c.sampler.pitch : 0.0f));
            if (std::abs (clip.stretch - 1.0) > 1.0e-6)
            {
                s.stretchMode = 1;
                s.stretch = clip.stretch;
                ++stretched;
            }
            if (c.sampler.flmPad.size() >= 6)
            {
                s.params[sp::egAttack] = c.sampler.flmPad[0];
                s.params[sp::egDecay] = c.sampler.flmPad[1];
                s.params[sp::egSustain] = c.sampler.flmPad[2];
                s.params[sp::egRelease] = c.sampler.flmPad[3];
                s.params[sp::length] = c.sampler.flmPad[4];
                s.params[sp::start] = c.sampler.flmPad[5];
            }
            fades += clip.fadeIn > 0 || clip.fadeOut > 0 ? 1 : 0;
            envelopes += clip.envelope >= 0 ? 1 : 0;
            fc.sample = std::move (s);
            o.channel.lanes.front().clips.push_back (std::move (fc));
        }
    }
    if (fades > 0)
        ctx.losses.add ("Audio clip fades ({0} clips)", std::to_string (fades));
    if (envelopes > 0)
        ctx.losses.add ("Audio clip envelopes ({0} clips)", std::to_string (envelopes));
    if (stretched > 0)
        ctx.losses.add ("Stretched audio clips play on FL Studio Mobile's own stretcher ({0} clips)", std::to_string (stretched));

    // --- what the song has that FLM cannot hold
    // --- automation clips (FLM_FORMAT §4: an automation lane's EVN2 is its curve, sampled): the rack's Level and Pan,
    // Pocket effects' parameters and the DW Sampler's / GMSynth's own values — the import's lanes turned around. A
    // straight segment is two points; a curved one is sampled every 1/32 beat (FLM draws straight lines through them).
    {
        int unsupported = 0;
        // out index (−1 = the master) and the FLM lane target for a LYNX target, with the value law
        struct LaneOf
        {
            int out = -2;
            int module = 0, param = 0;
            std::string name;
            std::function<float (float)> value;
        };
        const auto ownerOf = [&] (int insert) -> int {
            if (insert <= 0)
                return insert == 0 ? -1 : -2;
            if (const auto b = busOutOfInsert.find (insert); b != busOutOfInsert.end())
                return b->second;
            for (int k = 0; k < (int) outs.size(); ++k)
                if (! outs[(std::size_t) k].bus && outs[(std::size_t) k].insert == insert && users[(std::size_t) insert] == 1)
                    return k;
            return -2;
        };
        const auto laneOf = [&] (const AutomationTarget& t) -> LaneOf {
            LaneOf l;
            switch (t.kind)
            {
                case AutomationTargetKind::mixerVolume:
                    l.out = ownerOf (t.index);
                    l.name = "Level";
                    l.value = [] (float v) { return flmFaderValue (faderGain (v)); };
                    break;
                case AutomationTargetKind::mixerPan:
                    l.out = ownerOf (t.index);
                    l.param = 1;
                    l.name = "Pan";
                    l.value = [] (float v) { return v; };
                    break;
                case AutomationTargetKind::effectParam:
                    if (const auto it = ctx.slotModule.find ({ t.index, t.slot }); it != ctx.slotModule.end())
                    {
                        l.out = ownerOf (t.index);
                        l.module = (int) it->second;
                        l.param = t.param + 1;
                        const auto& slots = song.mixer[(std::size_t) t.index].effects;
                        l.name = std::string (effectInfo (slots[(std::size_t) t.slot].kind).params[(std::size_t) t.param].name) + " : "
                                 + effectInfo (slots[(std::size_t) t.slot].kind).name;
                        l.value = [] (float v) { return v; };
                    }
                    break;
                case AutomationTargetKind::channelParam:
                    if (t.index >= 0 && t.index < (int) song.channels.size())
                    {
                        const auto& c = song.channels[(std::size_t) t.index];
                        const auto o = outOfChannel.find (t.index);
                        const auto dw = flmdw::isChannel (c) && t.param >= flmdw::automationBase && t.param < flmdw::automationBase + flmdw::paramCount;
                        const auto gm = c.instrument == InstrumentKind::gms && flmBuild (c) && t.param >= 0 && t.param < gmsmobile::paramCount;
                        // MiniSynth / SuperSaw (FLM's engines in LYNX): engine value k is FLM's parameter k + 1, through the
                        // engine's parameter range (the import's generatorLaneTarget turned around)
                        const auto* ep = (c.instrument == InstrumentKind::minisynth || c.instrument == InstrumentKind::pocketSuperSaw)
                                             ? modelparams::find (c.instrument, t.param)
                                             : nullptr;
                        if (o != outOfChannel.end() && outs[(std::size_t) o->second].children.empty() && ep != nullptr
                            && ! (c.instrument == InstrumentKind::minisynth && t.param == 3) && t.param >= 0)
                        {
                            l.out = o->second;
                            l.module = 1;
                            l.param = t.param + 1;
                            l.name = ep->name + " : " + c.name;
                            l.value = [ep] (float v) { return std::clamp (ep->raw (v), 0.0f, 1.0f); };
                        }
                        else if (o != outOfChannel.end() && outs[(std::size_t) o->second].children.empty() && (dw || gm))
                        {
                            l.out = o->second;
                            l.module = 1;
                            l.param = dw ? t.param - flmdw::automationBase : t.param;
                            l.name = (dw ? std::string (flmdw::paramName (l.param)) : "Param " + std::to_string (l.param)) + " : " + c.name;
                            l.value = [] (float v) { return v; };
                        }
                    }
                    break;
                default: break;
            }
            return l;
        };
        std::map<std::tuple<int, int, int>, std::size_t> laneIndex;
        for (const auto& clip : song.playlist)
        {
            if (clip.kind != ClipKind::automation || clip.automation < 0 || clip.automation >= (int) song.automations.size() || clip.muted)
                continue;
            const auto& a = song.automations[(std::size_t) clip.automation];
            const auto l = laneOf (a.target);
            if (l.out < -1 || a.points.empty() || a.source.track >= 0 || a.source.channel >= 0)
            {
                ++unsupported;
                continue;
            }
            auto& ch = l.out < 0 ? master : outs[(std::size_t) l.out].channel;
            const auto key = std::make_tuple (l.out, l.module, l.param);
            auto it = laneIndex.find (key);
            if (it == laneIndex.end())
            {
                flm::Lane lane;
                lane.rawKind = 1;
                lane.targetModule = l.module;
                lane.targetParam = l.param;
                lane.name = l.module == 0 ? l.name : l.name;
                // the master's (and a bus's) first lane is FLM's empty "Level" lane: use it for the Level
                if (l.module == 0 && l.param == 0 && ! ch.lanes.empty() && ch.lanes.front().rawKind == 1 && ch.lanes.front().targetModule == 0
                    && ch.lanes.front().targetParam == 0 && ch.lanes.front().clips.empty())
                    it = laneIndex.emplace (key, 0).first;
                else
                {
                    ch.lanes.push_back (lane);
                    it = laneIndex.emplace (key, ch.lanes.size() - 1).first;
                }
            }
            flm::Clip fc;
            fc.startTick = flmTicks (clip.start);
            fc.length = beats (clip.length);
            fc.loopLength = fc.length;
            fc.contentId = (std::uint32_t) ch.lanes[it->second].clips.size() + 1;
            const auto point = [&] (double tick, float v, bool control) {
                const auto t = tick - (double) clip.offset;
                if (t < -1.0e-6 || t > (double) clip.length + 1.0e-6)
                    return;
                flm::Event e;
                e.tick = (std::uint32_t) std::max (0.0, std::round (t * flmTpb / tpb));
                e.key = control ? 1 : -1;
                e.length = 0.0;
                e.velocity = 0xB2;
                e.value = (std::uint16_t) std::clamp ((int) std::lround (std::clamp (l.value (v), 0.0f, 1.0f) * 65535.0f), 0, 65535);
                fc.events.push_back (e);
            };
            for (std::size_t k = 0; k < a.points.size(); ++k)
            {
                const auto& pt = a.points[k];
                point ((double) pt.tick, pt.value, true);
                if (k + 1 < a.points.size() && (std::abs (pt.tension) > 1.0e-4f || pt.mode != 0))
                {
                    const auto from = (double) pt.tick, to = (double) a.points[k + 1].tick;
                    for (auto x = from + tpb / 32.0; x < to - 1.0e-6; x += tpb / 32.0)
                        point (x, automationCurveAt (a.points, x), false);
                }
            }
            ch.lanes[it->second].clips.push_back (std::move (fc));
        }
        if (unsupported > 0)
            ctx.losses.add ("Automation clips of targets FL Studio Mobile can't automate ({0})", std::to_string (unsupported));
    }
    if (song.arrangements.size() > 1)
        ctx.losses.add ("Other arrangements (the copy holds the current one)", {}, false);
    if (song.clipGrid && ! song.clipGrid->slots.empty())
        ctx.losses.add ("The Clip-launch grid (its clips are left out)", {}, false);
    if (song.swing > 1.0e-4f)
        ctx.losses.add ("The song's swing", {});
    if (std::abs (song.masterPitch) > 0.5f)
        ctx.losses.add ("The master pitch", {});

    // --- the project: the master, then the channels in order (racks first, then channels, paired by index)
    p.racks.push_back (std::move (masterRack));
    p.channels.push_back (std::move (master));
    for (auto& o : outs)
    {
        p.racks.push_back (std::move (o.rack));
        p.channels.push_back (std::move (o.channel));
    }
    result.bytes = flm::write (p);
    // the LYNX chunk (FlmExport.h): which FLM content was which LYNX pattern, names and colours, and a checksum
    {
        std::string text = "LYNX SHARE flm extras 1\n";
        text += "check " + flmexport::checksum (result.bytes) + "\n";
        std::set<int> used;
        for (const auto& [key, id] : contentOf)
            used.insert (key.second);
        for (const auto pat : used)
        {
            auto name = song.patterns[(std::size_t) pat].name;
            std::replace (name.begin(), name.end(), '\n', ' ');
            char colour[16];
            std::snprintf (colour, sizeof (colour), "%08x", (unsigned) song.patterns[(std::size_t) pat].colour);
            text += "pattern " + std::to_string (pat) + " " + colour + " " + name + "\n";
        }
        for (const auto& [key, id] : contentOf)
            text += "content " + std::to_string (key.first + 1) + " " + std::to_string (id) + " " + std::to_string (key.second) + "\n";
        const char tag[4] = { 'L', 'Y', 'N', 'X' };
        result.bytes.insert (result.bytes.end(), tag, tag + 4);
        const auto size = (std::uint32_t) text.size();
        result.bytes.insert (result.bytes.end(), (const std::uint8_t*) &size, (const std::uint8_t*) &size + 4);
        result.bytes.insert (result.bytes.end(), text.begin(), text.end());
    }
    for (const auto& [source, rel] : ctx.copied)
        result.samples.push_back ({ source, rel });
    std::sort (result.samples.begin(), result.samples.end(), [] (const FlmExportSample& a, const FlmExportSample& b) { return a.relative < b.relative; });
    result.lost = ctx.losses.done();
    result.ok = ! result.bytes.empty();
    return result;
}

namespace flmexport
{
std::string checksum (const std::vector<std::uint8_t>& bytes)
{
    std::uint64_t h = 1469598103934665603ull; // FNV-1a, 64 bits
    for (const auto b : bytes)
        h = (h ^ b) * 1099511628211ull;
    char out[24];
    std::snprintf (out, sizeof (out), "%016llx", (unsigned long long) h);
    return out;
}

std::optional<Extras> readExtras (const std::vector<std::uint8_t>& bytes)
{
    // the top-level chunks after "10LF": the last one must be "LYNX"
    std::size_t at = 4, lynxAt = 0, lynxSize = 0;
    while (at + 8 <= bytes.size())
    {
        std::uint32_t size;
        std::memcpy (&size, bytes.data() + at + 4, 4);
        if ((std::size_t) size > bytes.size() - at - 8)
            return std::nullopt;
        if (std::memcmp (bytes.data() + at, "LYNX", 4) == 0)
        {
            lynxAt = at;
            lynxSize = size;
        }
        at += 8 + (std::size_t) size;
    }
    if (lynxAt == 0 || lynxAt + 8 + lynxSize != bytes.size())
        return std::nullopt;
    const std::string text ((const char*) bytes.data() + lynxAt + 8, lynxSize);
    if (text.rfind ("LYNX SHARE flm extras 1\n", 0) != 0)
        return std::nullopt;
    const std::vector<std::uint8_t> before (bytes.begin(), bytes.begin() + (std::ptrdiff_t) lynxAt);
    Extras e;
    bool checked = false;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        auto end = text.find ('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        const auto line = text.substr (pos, end - pos);
        pos = end + 1;
        if (line.rfind ("check ", 0) == 0)
            checked = line.substr (6) == checksum (before);
        else if (line.rfind ("pattern ", 0) == 0)
        {
            int index = -1;
            unsigned colour = 0;
            int consumed = 0;
            if (std::sscanf (line.c_str() + 8, "%d %8x %n", &index, &colour, &consumed) >= 2 && index >= 0 && index < 100000)
            {
                if ((int) e.patterns.size() <= index)
                    e.patterns.resize ((std::size_t) index + 1);
                e.patterns[(std::size_t) index] = { line.substr (8 + (std::size_t) consumed), colour };
            }
        }
        else if (line.rfind ("content ", 0) == 0)
        {
            int channel = -1, pattern = -1;
            unsigned id = 0;
            if (std::sscanf (line.c_str() + 8, "%d %u %d", &channel, &id, &pattern) == 3 && pattern >= 0 && pattern < 100000)
                e.contents[{ channel, id }] = pattern;
        }
    }
    if (! checked)
        return std::nullopt;
    return e;
}

void regroupPatterns (Song& song, const Extras& extras, const std::map<std::pair<int, std::uint32_t>, int>& contentPatterns)
{
    // imported pattern → the LYNX pattern it was part of
    std::map<int, int> groupOf;
    for (const auto& [key, imported] : contentPatterns)
        if (const auto it = extras.contents.find (key); it != extras.contents.end() && imported >= 0 && imported < (int) song.patterns.size())
            groupOf[imported] = it->second;
    if (groupOf.empty())
        return;
    // the first imported pattern of each group takes the others' notes
    std::map<int, int> target; // group → imported pattern that holds it
    std::vector<bool> gone (song.patterns.size(), false);
    for (const auto& [imported, group] : groupOf)
    {
        const auto [it, first] = target.emplace (group, imported);
        auto& into = song.patterns[(std::size_t) it->second];
        if (first)
        {
            if (group < (int) extras.patterns.size())
            {
                into.name = extras.patterns[(std::size_t) group].name;
                into.colour = extras.patterns[(std::size_t) group].colour;
            }
            continue;
        }
        auto& from = song.patterns[(std::size_t) imported];
        into.notes.insert (into.notes.end(), from.notes.begin(), from.notes.end());
        into.lengthTicks = std::max (into.lengthTicks, from.lengthTicks);
        gone[(std::size_t) imported] = true;
    }
    for (const auto& [group, held] : target)
        song.patterns[(std::size_t) held].sortNotes();
    // the clips: on the merged pattern, one per place (the first lane's)
    std::set<std::tuple<int, std::int64_t, std::int64_t, std::int64_t>> placed;
    std::vector<Clip> clips;
    for (auto c : song.playlist)
    {
        if (c.kind == ClipKind::pattern)
        {
            if (const auto g = groupOf.find (c.pattern); g != groupOf.end())
                c.pattern = target[g->second];
            if (! placed.insert ({ c.pattern, c.start, c.length, c.offset }).second)
                continue;
        }
        clips.push_back (c);
    }
    song.playlist = std::move (clips);
    // drop the emptied patterns, every pattern index after them moves down
    std::vector<int> newIndex (song.patterns.size(), -1);
    std::vector<Pattern> kept;
    for (std::size_t k = 0; k < song.patterns.size(); ++k)
        if (! gone[k])
        {
            newIndex[k] = (int) kept.size();
            kept.push_back (std::move (song.patterns[k]));
        }
    song.patterns = std::move (kept);
    for (auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) newIndex.size())
            c.pattern = std::max (0, newIndex[(std::size_t) c.pattern]);
    song.currentPattern = 0;
}
} // namespace flmexport
} // namespace lynx
