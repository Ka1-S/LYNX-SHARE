#include "core/FlCurve.h"
#include "core/PlaylistOps.h"
#include "core/FlpExport.h"
#include "core/Fpc.h"
#include "core/SamplerPage.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
#include <set>

#include "core/EditorPolish.h"
#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/FlStateWrite.h"
#include "core/FlWrapper.h"
#include "core/FruityDance.h"
#include "core/PatternBlocks.h"
#include "core/ThreeOsc.h"
#include "core/TimeStretch.h"
#include "core/MixerOps.h"
#include "core/Transmitter.h"
#include "core/Vocodex.h"

namespace lynx::flpexport
{
namespace
{
// FL's curve mode byte of a LYNX segment (an older project's bend, LYNX's own law, goes out as a Single curve with the
// same tension: the nearest FL shape)
std::uint8_t flCurveOf (const AutomationPoint& p) noexcept
{
    return p.mode < flcurve::modeCount ? p.mode : (std::uint8_t) flcurve::singleCurve;
}

// ---- small helpers ----
int iround (double v) noexcept { return (int) std::lround (v); }

// FL's time-stretching mode. Each one is a different algorithm in FL (Thief with −2 instead of its −1 renders at xcorr
// 0.869 against FL's own, round 180d), so an imported channel goes back with its own mode, as long as that still is the
// method it plays; otherwise FL's Stretch for General. Every mode but Resample (0) keeps the length, also without a TIME.
int flStretchMode (const SamplerSettings& s) noexcept
{
    if (! s.keepPitch)
        return 0;
    if (s.flStretchMode != 0 && stretchMethodForFlMode (s.flStretchMode) == s.stretchMethod)
        return s.flStretchMode;
    const auto m = s.stretchMethod;
    return m == StretchMethod::mono || m == StretchMethod::tones ? 2 : m == StretchMethod::transients || m == StretchMethod::beats ? 7 : -1;
}

std::uint32_t rgb (std::uint32_t argb) noexcept { return argb & 0xFFFFFFu; }

struct Hash
{
    std::uint64_t h = 1469598103934665603ull; // FNV-1a 64
    void bytes (const void* p, std::size_t n) noexcept
    {
        const auto* b = (const std::uint8_t*) p;
        for (std::size_t i = 0; i < n; ++i)
            h = (h ^ b[i]) * 1099511628211ull;
    }
    void i64 (std::int64_t v) noexcept { bytes (&v, sizeof v); }
    void text (const std::string& s) noexcept { i64 ((std::int64_t) s.size()); bytes (s.data(), s.size()); }
    void blob (const std::vector<std::uint8_t>& v) noexcept { i64 ((std::int64_t) v.size()); bytes (v.data(), v.size()); }
    // the project file keeps parameters to 1e-4 (ProjectFile's effectToVar), so does the fingerprint
    void value (float v) noexcept { i64 (std::isfinite (v) ? (std::int64_t) std::llround ((double) v * 10000.0) : 0); }
};

// The FL generator for each InstrumentKind whose state LYNX keeps in FL's own format (pluginState = FL's 0xD5), and the
// sample / data path FL keeps in the channel (3x Osc's custom wave, the Granulizer's and Wave Traveller's sample).
struct FlGenerator
{
    InstrumentKind kind;
    const char* name;
    bool channelSample; // the channel's samplePath is FL's channel sample
};
constexpr FlGenerator flGenerators[] = {
    { InstrumentKind::sytrus, "Sytrus", false },           { InstrumentKind::harmor, "Harmor", false },
    { InstrumentKind::gms, "GMS", false },                 { InstrumentKind::morphine, "Morphine", false },
    { InstrumentKind::ogun, "Ogun", false },               { InstrumentKind::minisynth, "MiniSynth", false },
    { InstrumentKind::dx10, "Fruity DX10", false },        { InstrumentKind::kepler, "Kepler", false },
    { InstrumentKind::poizone, "PoiZone", false },         { InstrumentKind::fruitkick, "Fruit Kick", false },
    { InstrumentKind::bassdrum, "BassDrum", false },       { InstrumentKind::toxic, "Toxic Biohazard", false },
    { InstrumentKind::threeosc, "3x Osc", true },          { InstrumentKind::autogun, "Autogun", false },
    { InstrumentKind::transistorbass, "Transistor Bass", false }, { InstrumentKind::simsynth, "SimSynth", false },
    { InstrumentKind::wasp, "Wasp", false },               { InstrumentKind::plucked, "Plucked!", false },
    { InstrumentKind::beepmap, "BeepMap", false },         { InstrumentKind::sawer, "Sawer", false },
    { InstrumentKind::flkeys, "FL Keys", false },          { InstrumentKind::granulizer, "Fruity Granulizer", true },
    { InstrumentKind::drumaxx, "Drumaxx", false },         { InstrumentKind::slayer, "FL Slayer", false },
    { InstrumentKind::fruitydance, "Fruity Dance", false }, { InstrumentKind::directwave, "DirectWave", false },
    { InstrumentKind::midiOut, "MIDI Out", false },        { InstrumentKind::voltage, "Fruity Voltage Controller", false },
    { InstrumentKind::dashboard, "Dashboard", false },     { InstrumentKind::videoplayer, "Fruity Video Player", false },
    { InstrumentKind::keplerExo, "Kepler Exo", false },    { InstrumentKind::wavetraveller, "Wave Traveller", true },
    { InstrumentKind::fruityslicer, "Fruity Slicer", false },
    { InstrumentKind::slicex, "Slicex", false },
    { InstrumentKind::fpc, "FPC", false },
};

const FlGenerator* generatorFor (InstrumentKind kind) noexcept
{
    for (const auto& g : flGenerators)
        if (g.kind == kind)
            return &g;
    return nullptr;
}

bool originMatches (const ChannelDef& c) { return ! c.flOrigin.empty() && c.flOrigin.fingerprint == fingerprint (c); }
bool originMatches (const EffectSlot& s) { return ! s.flOrigin.empty() && s.flOrigin.fingerprint == fingerprint (s); }

bool samplerLike (InstrumentKind k) noexcept { return k == InstrumentKind::sampler || k == InstrumentKind::audioClip; }

// what FL's Sampler can't do of a LYNX sampler (it then needs a render)
bool samplerExportable (const ChannelDef& c) noexcept
{
    return samplerLike (c.instrument) && c.zones.empty() && c.sampler.slices.empty();
}

std::string channelLabel (const ChannelDef& c, int index)
{
    return "Channel " + std::to_string (index + 1) + " '" + c.name + "'";
}

std::string insertLabel (const Song& song, int t)
{
    if (t == 0)
        return "Master";
    const auto& name = song.mixer[(std::size_t) t].name;
    return "Insert " + std::to_string (t) + (name.empty() ? std::string() : " '" + name + "'");
}

// ---- Fruity Wrapper (0xD5 version 12, REA_rodada5 §2; FL 2026's record order [DATA: its saved projects]) ----
struct Records
{
    std::vector<std::uint8_t> b;
    void put32 (std::uint32_t v)
    {
        for (int k = 0; k < 4; ++k)
            b.push_back ((std::uint8_t) (v >> (8 * k)));
    }
    void record (std::uint32_t id, const std::vector<std::uint8_t>& data)
    {
        put32 (id);
        const auto n = (std::uint64_t) data.size();
        for (int k = 0; k < 8; ++k)
            b.push_back ((std::uint8_t) (n >> (8 * k)));
        b.insert (b.end(), data.begin(), data.end());
    }
    void record (std::uint32_t id, const std::string& text) { record (id, std::vector<std::uint8_t> (text.begin(), text.end())); }
};

std::vector<std::uint8_t> le32s (std::initializer_list<std::uint32_t> values)
{
    std::vector<std::uint8_t> out;
    for (const auto v : values)
        for (int k = 0; k < 4; ++k)
            out.push_back ((std::uint8_t) (v >> (8 * k)));
    return out;
}

std::vector<std::uint8_t> le32s (const std::vector<std::uint32_t>& values)
{
    std::vector<std::uint8_t> out;
    for (const auto v : values)
        for (int k = 0; k < 4; ++k)
            out.push_back ((std::uint8_t) (v >> (8 * k)));
    return out;
}

// FL 2026's Fruity Limiter as it comes (version 7, 169 bytes) [DATA: FL 2026's own templates, e.g. "Cheese's recording
// setup"]: the substitute for LYNX's Limiter (option B, and the Master, which can't be bounced)
const std::uint8_t fruityLimiterDefault[169] = {
    0x07, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x5b, 0x04, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x88, 0x13, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x42, 0x0b, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x1a, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x8a, 0x02, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5b, 0x10, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00,
    0xe8, 0x03, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x5b, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x88, 0x13, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x42, 0x0b, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0a, 0x1a, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x8a, 0x02, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5b, 0x10, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// FL 2026's Fruity Parametric EQ 2 as a new instance saves it (version 8, 354 bytes) [DATA: captured in round 189k, FL
// 2026 saving a project whose Master slot had the plugin with no state; the same bytes as its Default.fst preset except
// 0x131]: the base the Master looks' EQ is written over (core/FlStateWrite.h). A new instance has HQ with the
// alternative design on (0x124 = 3).
const std::uint8_t parametricEq2Default[354] = {
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xab, 0x2a, 0x00, 0x00, 0x1c, 0x47, 0x00, 0x00,
    0x8e, 0x63, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x72, 0x9c, 0x00, 0x00, 0xe4, 0xb8, 0x00, 0x00, 0x55, 0xd5, 0x00, 0x00,
    0xbc, 0x9c, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00,
    0x44, 0x63, 0x00, 0x00, 0xbc, 0x9c, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xab, 0x2a, 0x00, 0x00,
    0x1c, 0x47, 0x00, 0x00, 0x8e, 0x63, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x72, 0x9c, 0x00, 0x00, 0xe4, 0xb8, 0x00, 0x00,
    0x55, 0xd5, 0x00, 0x00, 0xbc, 0x9c, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00,
    0x44, 0x63, 0x00, 0x00, 0x44, 0x63, 0x00, 0x00, 0xbc, 0x9c, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x7f, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// option B's clean mappings: the LYNX effect → an FL plugin with its state
std::optional<std::pair<std::string, std::vector<std::uint8_t>>> substituteFor (const EffectSlot& slot)
{
    if (slot.kind == EffectKind::limiter)
        return std::pair { std::string ("Fruity Limiter"), std::vector<std::uint8_t> (std::begin (fruityLimiterDefault), std::end (fruityLimiterDefault)) };
    return std::nullopt;
}

// LYNX's 0..1 automation value → FL's raw parameter value for a pattern event / initial value
std::int32_t rawValue (const AutomationTarget& t, float v)
{
    v = std::clamp (v, 0.0f, 1.0f);
    switch (t.kind)
    {
        case AutomationTargetKind::channelVolume:
        case AutomationTargetKind::channelPan: return iround (v * 12800.0);
        case AutomationTargetKind::channelAdjust: // (round 190dz) FL's knob ranges: volume 0…25600, pan ±6400, Mod X / Y ±256
            return t.param == leveladjust::volume ? iround (v * 25600.0)
                 : t.param == leveladjust::pan    ? iround ((v * 2.0 - 1.0) * 6400.0)
                                                  : iround ((v * 2.0 - 1.0) * 256.0);
        case AutomationTargetKind::mixerVolume:
        case AutomationTargetKind::mixerSend: return iround (v * 16000.0);
        case AutomationTargetKind::mixerPan: return iround ((v * 2.0 - 1.0) * 6400.0);
        case AutomationTargetKind::effectMix: return iround (v * 12800.0);
        case AutomationTargetKind::tempo: return iround (10000.0 + 512000.0 * v); // BPM × 1000 (issue #86, REA_rodada178a §2)
        default: return iround (v * 65536.0);
    }
}

std::uint32_t mixerId (int track, int slot, int param) noexcept
{
    return 0x70000000u + ((std::uint32_t) track << 22) + ((std::uint32_t) slot << 16) + (std::uint32_t) param;
}

// FL's channel filter cutoff for normalised Mod X (REA_rodada4 §3), as the importer's channelFilterHz
double filterHz (int type, double x)
{
    constexpr double twoPi = 6.283185307179586;
    x = std::clamp (x, 0.0, 1.0);
    switch (type)
    {
        case 0: return x >= 1.0 ? 20000.0 : -44100.0 / twoPi * std::log (1.0 - std::min ((std::pow (601.0, x) - 1.0) / 600.0, 0.999));
        case 1:
        case 2:
        case 3:
        case 4:
        case 5: return ((std::pow (51.0, x) - 1.0) * 0.0596 + 0.02) * 44100.0 / twoPi;
        default: return 20000.0;
    }
}

// a monotonic f over [0, 1]: the x whose f(x) is nearest `target`
template <typename F>
double invert (F&& f, double target, bool increasing = true)
{
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 40; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        if ((f (mid) < target) == increasing)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}
} // namespace

// ================================================================================================================
int envelopeTimeValue (double seconds) noexcept
{
    if (! (seconds > 0.0))
        return 100; // FL: values up to 100 are no time at all
    const auto v = 65536.0 * std::log (seconds / 0.064 + 1.0) / std::log (1000.0);
    return std::clamp (iround (v), 101, 65536);
}

int slideTimeValue (float beats) noexcept
{
    // n = round((27^(v/1024) − 1)·48/26) ticks of 1/192 beat (REA_rodada6 §4)
    const auto n = std::max (1.0, (double) beats * 192.0);
    return std::clamp (iround (1024.0 * std::log (n * 26.0 / 48.0 + 1.0) / std::log (27.0)), 0, 1660);
}

std::vector<std::uint8_t> defaultThreeOscState()
{
    // a version-13 state of zeros is LYNX's Params {}; normalised: version 14 + the default voice snapshot
    std::vector<std::uint8_t> zero (4 + 21 * 4 + 8, 0);
    zero[0] = 13;
    return threeOscNormalisedState (zero, {});
}

std::vector<std::uint8_t> fruityLimiterDefaultState()
{
    return std::vector<std::uint8_t> (std::begin (fruityLimiterDefault), std::end (fruityLimiterDefault));
}

std::vector<std::uint8_t> parametricEq2DefaultState()
{
    return std::vector<std::uint8_t> (std::begin (parametricEq2Default), std::end (parametricEq2Default));
}

std::optional<std::pair<std::string, std::vector<std::uint8_t>>> nativeLookState (const EffectSlot& s)
{
    if (s.look == 0)
        return std::nullopt;
    switch (s.kind)
    {
        case EffectKind::parametricEq:
            if (peqfl::lawOf (s.params) != peqfl::Law::peq2)
                return std::nullopt;
            return std::pair { std::string ("Fruity Parametric EQ 2"), flstate::parametricEq2 (s.params, parametricEq2DefaultState()) };
        case EffectKind::fruityMultiband: return std::pair { std::string ("Fruity Multiband Compressor"), flstate::multiband (s.params) };
        case EffectKind::stereoEnhancer: return std::pair { std::string ("Fruity Stereo Enhancer"), flstate::stereoEnhancer (s.params) };
        case EffectKind::fruitySoftClipper: return std::pair { std::string ("Fruity Soft Clipper"), flstate::softClipper (s.params) };
        case EffectKind::fruityLimiter: return std::pair { std::string ("Fruity Limiter"), flstate::limiter (s.params, fruityLimiterDefaultState()) };
        default: return std::nullopt;
    }
}

std::uint64_t fingerprint (const ChannelDef& c)
{
    Hash h;
    h.i64 ((std::int64_t) c.instrument);
    h.blob (c.pluginState);
    h.blob (c.controllerState);
    h.i64 ((std::int64_t) c.modelParams.size());
    for (const auto v : c.modelParams)
        h.value (v);
    h.i64 ((std::int64_t) c.zones.size());
    h.i64 ((std::int64_t) c.sampler.slices.size());
    h.text (c.plugin.id);
    return h.h;
}

std::uint64_t fingerprint (const EffectSlot& s)
{
    Hash h;
    h.i64 ((std::int64_t) s.kind);
    const auto used = s.kind == EffectKind::plugin ? (std::size_t) 0 : std::min (effectInfo (s.kind).params.size(), s.params.size());
    for (std::size_t i = 0; i < used; ++i)
        h.value (s.params[i]);
    h.blob (s.state);
    h.i64 ((std::int64_t) s.branches.size());
    h.i64 ((std::int64_t) s.patcher.nodes.size());
    return h.h;
}

void stampOrigins (Song& song)
{
    for (auto& c : song.channels)
        if (! c.flOrigin.empty())
            c.flOrigin.fingerprint = fingerprint (c);
    for (auto& t : song.mixer)
        for (auto& s : t.effects)
            if (! s.flOrigin.empty())
                s.flOrigin.fingerprint = fingerprint (s);
}

std::string flGeneratorName (const ChannelDef& c)
{
    if (originMatches (c))
        return c.flOrigin.plugin;
    if (c.instrument == InstrumentKind::layer)
        return "Layer";
    // (an empty state: the engines LYNX plays from their defaults, which are FL's — a new FL instance)
    // (3x Osc with no state: LYNX's default is written as FL's own version-14 state [FL 2026: a 3x Osc with an empty
    // state keeps uninitialised voice settings, its notes never end; round 171e, FL render])
    const auto own = c.instrument == InstrumentKind::threeosc;
    if (const auto* g = generatorFor (c.instrument); g != nullptr && (! c.pluginState.empty() || own))
        return g->name;
    return {};
}

namespace
{
std::string instrumentName (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return "LYNX Kick";
        case InstrumentKind::snare: return "LYNX Snare";
        case InstrumentKind::hat: return "LYNX Hat";
        case InstrumentKind::clap: return "LYNX Clap";
        case InstrumentKind::synth: return "LYNX Synth";
        case InstrumentKind::sampler: return "Sampler (multi-sample / slices)";
        case InstrumentKind::controller: return "Envelope Controller";
        case InstrumentKind::patcher: return "Patcher";
        case InstrumentKind::soundfont: return "LYNX Soundfont";
        case InstrumentKind::vfx: return "VFX note processor";
        default: return "instrument";
    }
}

} // namespace

bool needsFreeze (const ChannelDef& c)
{
    if (c.instrument == InstrumentKind::plugin || c.plugin.foreign.format != ForeignPlugin::Format::none)
        return false; // the wrapper (or, unknown, planRenders decides)
    if (! flGeneratorName (c).empty())
        return false;
    return ! samplerExportable (c);
}

bool effectExportable (const EffectSlot& s, const Options& options)
{
    if (s.isSubstitute() && s.substituteFor.foreign.format != ForeignPlugin::Format::none)
        return true;
    if (s.kind == EffectKind::plugin)
        return s.plugin.foreign.format != ForeignPlugin::Format::none || (options.pluginRecord && options.pluginRecord (s.plugin, false));
    if (originMatches (s) || nativeLookState (s))
        return true;
    return options.lynxOnly == LynxOnly::substitute && substituteFor (s).has_value();
}

// ================================================================================================================
RenderPlan planRenders (const Song& song, const Options& options)
{
    RenderPlan plan;
    if (options.lynxOnly != LynxOnly::renderAudio)
        return plan;
    for (int i = 0; i < (int) song.channels.size(); ++i)
    {
        const auto& c = song.channels[(std::size_t) i];
        const auto pluginUnknown = c.instrument == InstrumentKind::plugin && c.plugin.foreign.format == ForeignPlugin::Format::none
                                   && ! (options.pluginRecord && options.pluginRecord (c.plugin, true));
        // (silent kinds — controllers, VFX, video, MIDI out — make no audio to freeze; they are left out instead)
        const auto silent = c.instrument == InstrumentKind::controller || c.instrument == InstrumentKind::vfx;
        if ((needsFreeze (c) && ! silent) || pluginUnknown)
            plan.channels.push_back (i);
    }
    // inserts with a slot FL can't have (Master excluded: bouncing it would bounce the whole song), downstream first
    std::vector<int> order;
    for (const auto t : song.mixerOrder())
        if (t > 0 && t < (int) song.mixer.size())
            order.push_back (t);
    std::set<int> silenced;
    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        const auto t = *it;
        const auto& track = song.mixer[(std::size_t) t];
        const auto lynxOnly = std::any_of (track.effects.begin(), track.effects.end(),
                                           [&] (const EffectSlot& s) { return song.slotActive (s) && ! effectExportable (s, options); });
        if (! lynxOnly)
            continue;
        // an insert heard only through inserts already bounced is in their audio
        auto audible = track.toMaster;
        for (const auto& send : track.sends)
            audible = audible || silenced.count (send.target) == 0;
        if (! audible && ! track.sends.empty())
            continue;
        plan.inserts.push_back (t);
        silenced.insert (t);
    }
    return plan;
}

Song makeInsertRenderSong (const Song& song, int track)
{
    auto out = makeBounceSong (song, track);
    if (track <= 0 || track >= (int) out.mixer.size())
        return out;
    for (int t = 1; t < (int) out.mixer.size(); ++t)
        if (t != track)
            out.mixer[(std::size_t) t].toMaster = false;
    return out;
}

Song makeFreezeSong (const Song& song, int channel)
{
    Song out = song;
    out.mode = PlayMode::song;
    for (int i = 0; i < (int) out.channels.size(); ++i)
        if (i != channel)
            out.channels[(std::size_t) i].muted = true;
    if (channel >= 0 && channel < (int) out.channels.size())
    {
        auto& c = out.channels[(std::size_t) channel];
        c.muted = false;
        c.mixerTrack = 0;
    }
    for (auto& t : out.mixer)
        t.solo = false;
    if (! out.mixer.empty())
    {
        auto& master = out.mixer[0];
        master.effects.clear();
        master.volume = unityFader;
        master.pan = 0.0f;
        master.stereo = 0.0f;
        master.eq = TrackEq {};
        master.muted = false;
        master.invertPhase = master.swapChannels = false;
    }
    // the Master's own automation would colour the render
    for (auto& a : out.automations)
        if ((a.target.kind == AutomationTargetKind::mixerVolume || a.target.kind == AutomationTargetKind::mixerPan
             || a.target.kind == AutomationTargetKind::effectParam || a.target.kind == AutomationTargetKind::effectMix)
            && a.target.index == 0)
            a.target.kind = AutomationTargetKind::none;
    return out;
}

void applyFreeze (Song& song, int channel, const std::string& wav, std::int64_t length)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return;
    auto& c = song.channels[(std::size_t) channel];
    const auto mixerTrack = c.mixerTrack;
    const auto name = c.name;
    const auto colour = c.colour;
    ChannelDef frozen;
    frozen.name = name + " (audio)";
    frozen.instrument = InstrumentKind::audioClip;
    frozen.samplePath = wav;
    frozen.mixerTrack = mixerTrack;
    // unity: the render holds the channel's level and pan. A Playlist audio clip plays at unity at FL's default channel
    // volume (10000 of 12800, Instruments.cpp audioClipUnity), in FL as in LYNX; 100 % played the render 5.2 dB louder
    // (round 182b: Patience exported from a Live set rendered +4.3 dB in FL and in LYNX)
    frozen.volume = audioClipUnityVolume;
    frozen.colour = colour;
    frozen.uid = c.uid;
    c = std::move (frozen);

    for (auto& p : song.patterns)
    {
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [channel] (const Note& n) { return n.channel == channel; }), p.notes.end());
        p.channelLoops.erase (std::remove_if (p.channelLoops.begin(), p.channelLoops.end(), [channel] (const ChannelLoop& l) { return l.channel == channel; }),
                              p.channelLoops.end());
        for (auto& lane : p.events)
            if ((lane.target.kind == AutomationTargetKind::channelVolume || lane.target.kind == AutomationTargetKind::channelPan
                 || lane.target.kind == AutomationTargetKind::channelParam || lane.target.kind == AutomationTargetKind::channelAdjust)
                && lane.target.index == channel)
                lane.target.kind = AutomationTargetKind::none;
    }
    for (auto& a : song.automations)
        if ((a.target.kind == AutomationTargetKind::channelVolume || a.target.kind == AutomationTargetKind::channelPan
             || a.target.kind == AutomationTargetKind::channelParam || a.target.kind == AutomationTargetKind::channelAdjust)
            && a.target.index == channel)
            a.target.kind = AutomationTargetKind::none;

    int lane = 0;
    song.forEachClipList ([&lane] (const std::vector<Clip>& clips) {
        for (const auto& clip : clips)
            lane = std::max (lane, clip.track + 1);
    });
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = channel;
    clip.track = lane;
    clip.length = length;
    song.playlist.push_back (clip);
    if ((int) song.playlistTracks.size() <= lane)
        song.playlistTracks.resize ((std::size_t) lane + 1);
    song.playlistTracks[(std::size_t) lane].name = name + " (audio)";
}

void applyChannelRender (Song& song, int channel, const std::string& wav, std::int64_t length, Report& report)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return;
    const auto& c = song.channels[(std::size_t) channel];
    report.rendered.push_back ("Channel " + std::to_string (channel + 1) + " '" + c.name + "': "
                               + (c.instrument == InstrumentKind::plugin ? "plugin '" + c.plugin.name + "'" : instrumentName (c.instrument))
                               + " → audio clip");
    applyFreeze (song, channel, wav, length);
}

int applyInsertRender (Song& song, int track, const std::string& wav, std::int64_t length, const Options& options, Report& report)
{
    if (track <= 0 || track >= (int) song.mixer.size())
        return -1;
    std::vector<std::string> names;
    auto& effects = song.mixer[(std::size_t) track].effects;
    for (int s = 0; s < (int) effects.size(); ++s)
    {
        auto& fx = effects[(std::size_t) s];
        if (! song.slotActive (fx) || effectExportable (fx, options))
            continue;
        names.push_back (effectInfo (fx.kind).name);
        fx.enabled = false;
        for (auto& a : song.automations)
            if ((a.target.kind == AutomationTargetKind::effectParam || a.target.kind == AutomationTargetKind::effectMix)
                && a.target.index == track && a.target.slot == s)
                a.target.kind = AutomationTargetKind::none;
        for (auto& p : song.patterns)
            for (auto& lane : p.events)
                if ((lane.target.kind == AutomationTargetKind::effectParam || lane.target.kind == AutomationTargetKind::effectMix)
                    && lane.target.index == track && lane.target.slot == s)
                    lane.target.kind = AutomationTargetKind::none;
    }
    const auto name = song.mixer[(std::size_t) track].name;
    const auto added = applyBounce (song, track, wav, length);
    if (added < 0)
        return -1;
    std::string list;
    for (const auto& n : names)
        list += (list.empty() ? "" : ", ") + n;
    report.rendered.push_back ("Insert " + std::to_string (track) + " '" + name + "' (" + list + ") → audio clip on insert "
                               + std::to_string (added));
    return added;
}

// ================================================================================================================
std::vector<std::uint8_t> flVst3State (const std::vector<std::uint8_t>& component, const std::vector<std::uint8_t>& controller)
{
    // u32 1 + records {u32 id, u64 size}: 1 (64 B, first u32 = 1), 3 component, 2 controller [DATA: FL 2026's VST3
    // states]; record 4 (FL's parameter slots) is left out: FL then gives slot k to the plugin's k-th parameter, which is
    // LYNX's parameter index (REA_rodada22 §2.3)
    if (component.empty() && controller.empty())
        return {}; // (no state: the wrapper then has none and FL starts the plugin from its defaults)
    Records r;
    r.put32 (1);
    std::vector<std::uint8_t> head (64, 0);
    head[0] = 1;
    r.record (1, head);
    r.record (3, component);
    r.record (2, controller);
    return r.b;
}

std::vector<std::uint8_t> writeWrapper (const ForeignPlugin& p, bool instrument, const PluginRef* ref)
{
    Records r;
    r.put32 (12);

    // 0x01: MIDI in / out ports, the pitch bend range, 8 zero bytes
    {
        const auto in = ref != nullptr ? ref->midiInPort : p.midiInPort;
        const auto out = ref != nullptr ? ref->midiOutPort : p.midiOutPort;
        const auto bend = ref != nullptr ? ref->bendRange : (p.bendRange >= 1 ? p.bendRange : 12);
        r.record (0x01, le32s ({ (std::uint32_t) in, (std::uint32_t) out, (std::uint32_t) bend, 0, 0 }));
    }
    // 0x02: the options [DATA: FL 2026's defaults 00 | a0 00 00 00 | 19 00 00 00 | flags 0xB4007D8C | fixed 0 | 1 | flags2 0]
    {
        std::uint32_t flags = 0xB4007D8Cu, fixed = 0, flags2 = 0;
        if (ref != nullptr)
        {
            auto set = [&flags] (std::uint32_t bit, bool on) { flags = on ? flags | bit : flags & ~bit; };
            set (0x2u, ref->fixedBuffers);
            set (0x4u, ref->notifyRender);
            set (0x1000u, ref->threaded);
            set (0x40000u, ref->separateProcess);
            set (0x20000000u, ref->allowSmartDisable);
            set (0x1u, ref->sendBendRange);
            set (0x20u, ! ref->releaseVelocity);
            set (0x800u, ref->sendLoopPosition);
            set (0x20000u, ref->modXAftertouch);
            set (0x02000000u, ! ref->resetOnFlush);
            set (0x01000000u, ref->hostKeysFirst);
            fixed = (ref->maxSizeBuffers ? 1u : 0u) | (ref->hostMaxBuffer ? 2u : 0u);
            flags2 = (ref->allNotesOffOnStop ? 0u : 0x1000u) | (ref->allowHostKeys ? 0x800u : 0u);
            set (0x8u, ref->processInactive); // "Process inactive inputs and outputs" (round 188d)
        }
        else if (p.hasWrapperOptions)
        {
            flags = p.wrapperFlags;
            fixed = p.fixedOptions;
            flags2 = p.hasWrapperFlags2 ? p.wrapperFlags2 : 0;
        }
        std::vector<std::uint8_t> o { 0x00, 0xa0, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00 };
        const auto tail = le32s ({ flags, fixed, 1, flags2 });
        o.insert (o.end(), tail.begin(), tail.end());
        r.record (0x02, o);
    }
    // 0x1E / 0x1F / 0x20: Processing › Connections, 12 bytes per input / output {i32 offset, u32 active, u32 0}
    // [DATA round 188d]; without a mapping one input and one output (FL lays them out again from the plugin)
    {
        const auto* ins = ref != nullptr ? &ref->inputs : (p.hasRoutes ? &p.inputRoutes : nullptr);
        const auto* outs = ref != nullptr ? &ref->outputs : (p.hasRoutes ? &p.outputRoutes : nullptr);
        if ((ins != nullptr && ! ins->empty()) || (outs != nullptr && ! outs->empty()))
        {
            auto list = [] (const std::vector<PluginOutputRoute>& routes) {
                std::vector<std::uint32_t> words;
                for (const auto& route : routes)
                    words.insert (words.end(), { (std::uint32_t) route.offset, route.isActive (true) ? 1u : 0u, 0u });
                return le32s (words);
            };
            // (an effect whose inputs alone are mapped: its output as below, one default entry)
            const auto outputs = outs->empty() && ! instrument ? std::vector<PluginOutputRoute> { { 0, 1 } } : *outs;
            r.record (0x1E, le32s ({ (std::uint32_t) ins->size(), (std::uint32_t) outputs.size(), 0, 0 }));
            if (! ins->empty())
                r.record (0x1F, list (*ins));
            if (! outputs.empty())
                r.record (0x20, list (outputs));
        }
        else
        {
            r.record (0x1E, le32s ({ 1, 1, 0, 0 }));
            r.record (0x1F, le32s ({ 0, instrument ? 0u : 1u, 0 }));
            r.record (0x20, le32s ({ 0, 1, 0 }));
        }
    }

    std::uint32_t type = 0;
    switch (p.format)
    {
        case ForeignPlugin::Format::vst2: type = instrument ? 4 : 0; break;
        case ForeignPlugin::Format::vst3: type = instrument ? 8 : 7; break;
        case ForeignPlugin::Format::clap: type = instrument ? 12 : 11; break;
        default: break;
    }
    r.record (0x32, le32s ({ type, 0, 0, 0 }));
    if (p.format == ForeignPlugin::Format::vst2)
        r.record (0x33, le32s ({ p.vst2Id }));
    if (p.format == ForeignPlugin::Format::vst3 || (p.format == ForeignPlugin::Format::vst2 && p.hasClassId))
        r.record (0x34, std::vector<std::uint8_t> (p.classId.begin(), p.classId.end()));
    if (p.format == ForeignPlugin::Format::clap)
        r.record (0x3A, p.clapId);
    r.record (0x36, p.name);
    r.record (0x37, p.path);
    if (! p.state.empty())
        r.record (0x35, p.state);
    return r.b;
}

// ================================================================================================================
namespace
{
struct Converter
{
    Converter (const Song& s, const Options& o, Report& r) : song (s), options (o), report (r) {}

    const Song& song;
    const Options& options;
    Report& report;
    flp::Project out;
    int filePpq = 96;
    std::map<int, int> automationChannel; // song.automations index → FL channel
    // the FL slot (0..9) of each exported effect: (track, LYNX slot) → FL slot
    std::map<std::pair<int, int>, int> slotMap;
    std::map<std::pair<int, int>, std::string> slotPlugin; // → FL plugin name ("Fruity Wrapper" for hosted plugins)

    std::uint32_t ticks (std::int64_t t) const { return (std::uint32_t) std::max<std::int64_t> (0, (t * filePpq + ppq / 2) / ppq); } // (rounded)
    std::string path (const std::string& p) const { return options.samplePath && ! p.empty() ? options.samplePath (p) : p; }

    void choosePpq()
    {
        // FL's own 96 when every note and clip fits it, else LYNX's 960; other times (pattern lengths, event ticks, markers,
        // offsets) are rounded to the file's PPQ [FL 2026 played a 960-PPQ export of an FL demo wrongly, round 171e]
        if (options.ppq > 0)
        {
            filePpq = options.ppq;
            out.header.ppq = (std::uint16_t) filePpq;
            return;
        }
        bool fine = true;
        auto use = [&fine] (std::int64_t t) { fine = fine && t % (ppq / 96) == 0; };
        for (const auto& p : song.patterns)
            for (const auto& n : p.notes)
            {
                use (n.start);
                use (n.length);
            }
        song.forEachClipList ([&] (const std::vector<Clip>& clips) {
            for (const auto& c : clips)
            {
                use (c.start);
                // (an audio clip's length goes unstretched, as playlistItem writes it)
                use (c.kind == ClipKind::audio && c.stretch > 0.0 && c.stretch != 1.0 ? (std::int64_t) std::llround ((double) c.length / c.stretch)
                                                                                        : c.length);
            }
        });
        filePpq = fine ? 96 : ppq;
        out.header.ppq = (std::uint16_t) filePpq;
    }

    void globals()
    {
        out.version = options.write.version;
        out.versionNumber = flp::parseVersionNumber (out.version);
        out.flagsA9 = 15;
        out.tempo = song.bpm;
        out.beatsPerBar = song.beatsPerBar;
        out.beatUnit = meterUnit (song.beatUnit);
        out.swing = std::clamp (iround (song.swing * 128.0), 0, 128);
        out.mainPitch = iround (song.masterPitch);
        out.currentPattern = std::clamp (song.currentPattern, 0, std::max (0, (int) song.patterns.size() - 1)) + 1;
        out.songMode = song.mode == PlayMode::song ? 1 : 0;
        out.showInfo = song.info.showOnOpen;
        out.currentArrangement = std::max (0, song.currentArrangement);
        out.playTruncatedNotes = song.playTruncatedNotes;
        out.autoPdc = song.autoPdc;
        out.compensateAutomations = song.compensateAutomations;
        out.timeSignatureMode = song.timeSignatureMode;
        out.fastDeclick = song.fastDeclick;
        out.performanceMode = song.performanceMode;
        out.panLaw = song.panTriangular ? 2 : 0;
        out.autoCrossfades = song.autoCrossfades;
        out.dataFolder = song.dataFolder;
        out.hasScaleHighlight = true;
        out.scaleHighlight = song.scaleHighlight.scale;
        out.scaleRoot = song.scaleHighlight.root;
        out.title = song.info.title;
        out.artist = song.info.author;
        out.genre = song.info.genre;
        out.url = song.info.url;
        out.comments = song.info.comments;
        // (FL's TDateTime: days since 1899-12-30; the Unix epoch is day 25569)
        if (song.info.created > 0.0)
            out.createdDays = song.info.created / 86400.0 + 25569.0;
        out.workDays = song.info.workSeconds / 86400.0;
        out.filterGroups = song.channelGroups;
        if (out.filterGroups.empty())
            out.filterGroups.push_back ("Unsorted");
        if (song.masterPitch != 0.0f)
            report.exact.push_back ("Main pitch");
        report.exact.push_back ("Tempo, time signature, swing, project info");
    }

    // ---- channels ----
    void envelopesFromAdsr (flp::Channel& c, const ChannelDef& def)
    {
        const auto& s = def.sampler;
        for (int k = 0; k < 5; ++k)
        {
            auto& e = c.envelopes[(std::size_t) k];
            e = {};
            e.present = true;
            e.flags = k == 1 ? 4 : 0;
            e.sustain = 50;
        }
        // the volume envelope: on when LYNX shapes the note (an untouched sampler plays the whole sample)
        const auto shaped = s.attack > 0.0f || s.decay > 0.0f || s.sustain < 1.0f || s.release > 0.0f;
        auto& v = c.envelopes[1];
        v.enabled = shaped;
        v.delay = 100;
        v.attack = envelopeTimeValue (s.attack);
        v.hold = 100;
        v.decay = envelopeTimeValue (s.decay);
        v.sustain = std::clamp (iround (s.sustain * 128.0), 0, 128);
        v.release = envelopeTimeValue (s.release);
        auto mod = [&] (flp::Channel::Envelope& e, const ModEnvelope& m, int amount) {
            if (m.amount == 0.0f)
                return;
            e.enabled = true;
            e.attack = envelopeTimeValue (m.attack);
            e.hold = envelopeTimeValue (m.hold);
            e.decay = envelopeTimeValue (m.decay);
            e.sustain = std::clamp (iround (m.sustain * 128.0), 0, 128);
            e.release = envelopeTimeValue (m.release);
            e.amount = amount;
        };
        mod (c.envelopes[4], s.pitchEnv, std::clamp (iround (s.pitchEnv.amount * 100.0), -2400, 2400));
        if (s.filterEnv.amount != 0.0f)
        {
            // the amount moves Mod X by amount/128: the octaves at the channel's cutoff
            const auto x = c.modX / 256.0;
            const auto from = filterHz (c.filterType, x);
            const auto target = std::clamp (from * std::pow (2.0, (double) s.filterEnv.amount), 20.0, 20000.0);
            const auto toX = invert ([&] (double y) { return filterHz (c.filterType, y); }, target);
            mod (c.envelopes[2], s.filterEnv, std::clamp (iround ((toX - x) * 128.0), -128, 128));
        }
        auto lfo = [&] (flp::Channel::Envelope& e, const ChannelLfo& l, int amount) {
            if (l.amount == 0.0f)
                return;
            e.lfoAmount = amount;
            const auto scale = l.sync ? 2.0 : 1.0;
            e.lfoSpeed = envelopeTimeValue (l.period / scale);
            e.lfoDelay = envelopeTimeValue (l.delay / scale);
            e.lfoAttack = envelopeTimeValue (l.attack / scale);
            e.lfoShape = std::clamp (l.shape, 0, 2);
            e.flags |= (l.sync ? 2 : 0) | (l.global ? 0x20 : 0);
        };
        lfo (c.envelopes[0], s.lfo[lfoPan], std::clamp (iround (s.lfo[lfoPan].amount * 64.0), -64, 64));
        lfo (c.envelopes[1], s.lfo[lfoVolume], std::clamp (iround (s.lfo[lfoVolume].amount * 128.0), -128, 128));
        lfo (c.envelopes[4], s.lfo[lfoPitch], std::clamp (iround (s.lfo[lfoPitch].amount * 100.0), -2400, 2400));
        c.envelopeCount = 5;
    }

    void filterFrom (flp::Channel& c, const SamplerSettings& s)
    {
        // LYNX's mode (0 LP, 1 BP, 2 HP, 3 BS) → FL's type (1 LP, 2 BP, 3 HP, 4 BS: the biquads)
        static constexpr int types[] = { 1, 2, 3, 4 };
        const auto open = s.cutoff >= 19999.0f && s.resonance <= 0.0f && s.filterType == 0;
        c.filterType = open ? 0 : types[std::clamp (s.filterType, 0, 3)];
        c.modX = open ? 256 : std::clamp (iround (256.0 * invert ([&] (double y) { return filterHz (c.filterType, y); }, s.cutoff)), 0, 256);
        // the biquads' Q = (51^(0.625·r) − 1)·0.4 + 0.1 and LYNX's resonance = (2 − 1/Q)/1.9
        const auto q = 1.0 / std::max (0.05, 2.0 - 1.9 * (double) s.resonance);
        const auto r = q <= 0.1 ? 0.0 : std::log ((q - 0.1) / 0.4 + 1.0) / std::log (51.0) / 0.625;
        c.modY = open ? 0 : std::clamp (iround (r * 256.0), 0, 256);
    }

    flp::Channel channel (const ChannelDef& def, int index)
    {
        flp::Channel c;
        c.index = index;
        c.name = def.name;
        c.colour = def.colour != 0 ? rgb (def.colour) : 0;
        c.icon = def.icon;
        // FL keeps a Smart disabled generator from running through silence, so it changes the phase of what it plays
        // (Thief without FL's own flags: xcorr 0.891, same spectrum; round 180d)
        c.wrapperFlags = def.flWrapperFlags;
        // a hosted plugin's window: FL's "Detached" is flag 0x4 of the same record (issue #116, REA_rodada190i.md)
        if (def.instrument == InstrumentKind::plugin || def.plugin.foreign.format != ForeignPlugin::Format::none)
            c.wrapperFlags = withFlDetached (def.flWrapperFlags >= 0 ? def.flWrapperFlags : 0x150, def.plugin.detached);
        c.enabled = ! def.muted;
        c.soloLock = def.soloLock;
        c.ghostShown = ! def.ghostHidden;
        c.filterGroup = def.group >= 0 && def.group < (int) out.filterGroups.size() ? def.group : 0;
        c.mixerTrack = std::clamp (def.mixerTrack, 0, std::max (0, (int) song.mixer.size() - 1));

        // levels: the same curves (channelGain both sides); the Sampler's Gain has no FL knob, it goes into the volume
        auto volume = def.volume;
        if (samplerLike (def.instrument) && def.sampler.gainDb != 0.0f)
        {
            const auto g = channelGain (volume) * std::pow (10.0f, def.sampler.gainDb / 20.0f);
            volume = (float) std::clamp (std::log (10.0 * g + 1.0) / std::log (11.0), 0.0, 1.0);
            if (g > 1.0f)
                report.notes.push_back (channelLabel (def, index) + ": the Sampler's Gain is above FL's channel volume range (capped at 100%)");
        }
        // a hosted plugin (round 190ad, REA_rodada190ad.md): FL's wrapper plays it √2 over LYNX's own channel law. A
        // project with FL's levels writes the knob as it is; one with LYNX's own gets the volume that sounds the same
        if (def.instrument == InstrumentKind::plugin && ! song.flPluginLevels)
        {
            const auto g = channelGain (volume) * 0.70710678f;
            volume = (float) std::clamp (std::log (10.0 * g + 1.0) / std::log (11.0), 0.0, 1.0);
        }
        // warp markers that bend the tempo inside the file (round 175a, M1): FL's channel stretch has one tempo
        if (def.sampler.keepPitch && def.sampler.stretchBeats > 0.0f
            && ! warpStretchMap (def.sampler.warp, 1.0e9, 1000.0, 1.0e9).empty()) // (any file length: do the markers bend)
            report.notes.push_back (channelLabel (def, index) + ": its warp markers are written as one tempo (FL's sample channels have none)");
        c.hasLevels = true;
        c.volume = std::clamp (iround (volume * 12800.0), 0, 12800);
        c.pan = std::clamp (iround (6400.0 + def.pan * 6400.0), 0, 12800);
        // FL's Levels adjustment (0xE5, round 190dz): the Misc tab's offsets as FL's raw values
        {
            const auto& l = def.misc.levels;
            c.hasLevelAdjust = true;
            c.panAdj = std::clamp (iround (l.pan * 6400.0), -6400, 6400);
            c.volAdj = std::clamp (iround (l.volume * 12800.0), 0, 25600);
            c.modXAdj = std::clamp (iround (l.modX * 256.0), -256, 256);
            c.modYAdj = std::clamp (iround (l.modY * 256.0), -256, 256);
        }
        const auto root = std::clamp (def.misc.rootNote, 0, 127);
        // FL's stretch PITCH knob and the header pitch add up, but do not sound the same in FL's stretcher (Thief with all
        // of it in the header: xcorr 0.934 against FL's own, round 180d), so what came from the knob goes back to the knob
        c.stretchPitch = def.sampler.keepPitch ? def.sampler.flStretchPitch : 0;
        const auto cents = iround (((double) def.sampler.pitch - (60.0 - root)) * 100.0) - c.stretchPitch;
        c.pitchCents = cents;
        c.rootNote = root;
        c.fineTune = iround (def.misc.fineTune);
        c.pitchRange = std::clamp (std::max (def.misc.pitchRange, (std::abs (cents) + 99) / 100), 0, 48);

        c.swingMix = std::clamp (iround (def.swingMix * 128.0), 0, 128);
        c.cutGroup = def.cutGroup;
        c.cutBy = def.cutBy;
        c.maxVoices = std::clamp (def.maxPolyphony, 0, 128);
        c.monoFlag = def.misc.mono;
        c.portaFlag = def.misc.porta;
        c.slideTime = slideTimeValue (def.misc.portaBeats);

        // Misc: time, echo, tracking, the root note keyboard, the arpeggiator (REA round 146b)
        const auto& m = def.misc;
        c.timeShift = m.shiftBeats > 0.0f ? channelmiscTime (m.shiftBeats) : 0;
        c.timeGate = m.gateBeats > 0.0f ? std::min (1445, channelmiscTime (m.gateBeats)) : 1447;
        c.fullPorta = m.fullPorta;
        c.echoFeedback = std::clamp (iround (m.echo.feedback * 12800.0), 0, 25600);
        c.echoPan = std::clamp (iround (6400.0 + m.echo.pan * 6400.0), 0, 12800);
        c.echoPitch = iround (m.echo.pitch);
        c.echoes = m.echo.echoes;
        c.echoTime = std::max (1, iround (m.echo.timeBeats * 192.0));
        c.echoModX = std::clamp (iround (128.0 + m.echo.modX * 128.0), 0, 256);
        c.echoModY = std::clamp (iround (128.0 + m.echo.modY * 128.0), 0, 256);
        c.delayFlags = (m.echo.pingPong ? 2 : 0) | (m.echo.fat ? 4 : 0);
        for (int k = 0; k < 2; ++k)
        {
            const auto& from = k == 0 ? m.velocity : m.key;
            auto& t = c.tracking[(std::size_t) k];
            t.present = true;
            t.mid = from.mid;
            t.pan = iround (from.pan * 64.0);
            t.modX = iround (from.modX * 256.0);
            t.modY = iround (from.modY * 256.0);
        }
        c.trackingCount = 2;
        c.mainPitch = m.mainPitch;
        c.addToKey = m.addToKey;
        // FL's directions 1 up, 2 down, 3 bounce, 4 sticky, 5 random; LYNX: 4 random, 5 sticky
        static constexpr int toFl[] = { 0, 1, 2, 3, 5, 4 };
        const auto& a = def.arp;
        c.arpDirection = toFl[std::clamp (a.direction, 0, 5)];
        c.arpRange = std::clamp (a.range, 1, 4);
        c.arpTime = channelmiscTime (a.stepBeats);
        c.arpGate = std::clamp (iround (a.gate * 48.0), 1, 128);
        c.arpSlide = a.slide;
        c.arpRepeat = a.octaveRepeat;
        c.arpChord = ! a.chordSustain ? -2 : a.chord == std::vector<int> { 0 } ? 0 : -1;
        if (a.enabled() && ! a.chord.empty() && a.chord != std::vector<int> { 0 })
            report.notes.push_back (channelLabel (def, index) + ": the arpeggiator's chord is written as Auto");
        if (def.hasKeyRegion())
        {
            c.keyRegionLow = def.keyLow;
            c.keyRegionHigh = def.keyHigh;
        }

        // the channel filter and articulators
        if (def.sampler.flChannel.size() >= 88)
        {
            // FL's own records as the import stored them (REA_rodada76)
            for (int k = 0; k < 5; ++k)
            {
                auto& e = c.envelopes[(std::size_t) k];
                e.present = true;
                e.rawSize = 68;
                std::memcpy (e.raw.data(), def.sampler.flChannel.data() + 17 * k, 68);
            }
            c.envelopeCount = 5;
            c.modX = def.sampler.flChannel[85];
            c.modY = def.sampler.flChannel[86];
            c.filterType = def.sampler.flChannel[87];
        }
        else
        {
            filterFrom (c, def.sampler);
            envelopesFromAdsr (c, def);
            if (def.sampler.flVolumeEnvelope.size() >= 17)
            {
                auto& e = c.envelopes[1];
                e.rawSize = 68;
                std::memcpy (e.raw.data(), def.sampler.flVolumeEnvelope.data(), 68);
            }
        }

        // the generator
        if (def.instrument == InstrumentKind::layer)
        {
            c.type = flp::ChannelType::layer;
            c.layerChildren = def.layer.children;
            c.layerFlags = (def.layer.random ? 1 : 0) | (def.layer.crossfade ? 2 : 0) | (def.layer.sequential ? 4 : 0);
            c.layerFade = std::clamp (iround (def.layer.fade * 256.0), 0, 256);
            report.exact.push_back (channelLabel (def, index) + ": Layer");
        }
        else if (samplerLike (def.instrument) && ! originMatches (def))
        {
            c.type = def.instrument == InstrumentKind::audioClip ? flp::ChannelType::audioClip : flp::ChannelType::sampler;
            sampler (c, def);
            report.exact.push_back (channelLabel (def, index) + (def.instrument == InstrumentKind::audioClip ? ": audio clip" : ": Sampler"));
        }
        else if (def.instrument == InstrumentKind::plugin || def.plugin.foreign.format != ForeignPlugin::Format::none)
        {
            // (also a stand-in whose FL plugin is not on this PC: its wrapper goes back as FL wrote it)
            c.type = flp::ChannelType::instrument;
            if (const auto record = hosted (def.plugin, true))
            {
                c.pluginName = "Fruity Wrapper";
                c.pluginState = writeWrapper (*record, true, def.plugin.foreign.format == ForeignPlugin::Format::none ? &def.plugin : nullptr);
                report.exact.push_back (channelLabel (def, index) + ": " + record->name + " (Fruity Wrapper)");
            }
            else
                report.dropped.push_back (channelLabel (def, index) + ": plugin '" + def.plugin.name + "' (not found)");
        }
        else if (const auto name = flGeneratorName (def); ! name.empty())
        {
            c.type = flp::ChannelType::instrument;
            c.pluginName = name;
            c.pluginState = originMatches (def) && ! def.flOrigin.state.empty() ? def.flOrigin.state : def.pluginState;
            // a dancer edited in LYNX keeps LYNX's text state: FL gets its own bytes (the sprite sheet's file included)
            if (def.instrument == InstrumentKind::fruitydance)
                if (const auto dancer = dance::fromState (c.pluginState))
                    c.pluginState = dance::toFl (*dancer, options.flFactoryData);
            if (c.pluginState.empty() && def.instrument == InstrumentKind::threeosc)
                c.pluginState = defaultThreeOscState();
            if (def.instrument == InstrumentKind::fpc) // the kit as FPC's state (core/Fpc.h: FL's own bytes when nothing was edited)
                if (const auto kit = fpc::read (c.pluginState))
                    c.pluginState = fpc::write (*kit);
            const auto* g = generatorFor (def.instrument);
            if ((g != nullptr && g->channelSample) || samplerLike (def.instrument))
                c.samplePath = path (def.samplePath);
            if (samplerLike (def.instrument))
                sampler (c, def);
            report.exact.push_back (channelLabel (def, index) + ": " + name);
        }
        else
        {
            // left for the report (option A renders these before the conversion)
            c.type = flp::ChannelType::sampler;
            report.dropped.push_back (channelLabel (def, index) + ": " + instrumentName (def.instrument) + " (no FL equivalent)");
        }
        return c;
    }

    static int channelmiscTime (double beats)
    {
        // channelmisc::flTimeKnobValue (ChannelMiscFx.h)
        if (beats <= 0.0)
            return 0;
        return (int) std::lround (1024.0 * std::log (beats * 192.0 * 26.0 / 48.0 + 1.0) / std::log (27.0));
    }


    void sampler (flp::Channel& c, const ChannelDef& def)
    {
        const auto& s = def.sampler;
        c.samplePath = path (def.samplePath);
        const auto& p = s.precompute;
        c.fxFlags = (p.fadeStereo ? 1 : 0) | (s.reverse ? 2 : 0) | (p.clip ? 4 : 0) | (p.swapStereo ? 0x100 : 0);
        c.fadeIn = p.fadeIn;
        c.fadeOut = p.fadeOut;
        c.removeDc = p.removeDc;
        c.normalize = p.normalize;
        c.reversePolarity = p.reversePolarity;
        c.crossfade = p.crossfade;
        c.trim = p.trim;
        c.pingPong = p.pingPong;
        c.boost = p.boost;
        c.eq = p.eq;
        c.ringMix = p.ringMix;
        c.ringFreq = p.ringFreq;
        c.stereoDelay = p.stereoDelay;
        c.cutoff = p.cutoff;
        c.resonance = p.resonance;
        c.reverb = p.reverb;
        c.reverbType = p.reverbType;
        c.pogo = p.pogo;
        c.declickMode = s.declickMode;
        c.sampleFlags = samplerpage::contentFlags (s); // (round 190dh: FL's Content options and Use loop points)
        if (s.hasLoopOverride())
            c.loopStart = s.loopStart, c.loopEnd = s.loopEnd;
        // the region: FL's end is a fraction of what follows the start (REA_rodada167h §2)
        const auto start = std::clamp ((double) s.regionStart, 0.0, 1.0);
        const auto end = std::clamp ((double) s.regionEnd, start, 1.0);
        c.sampleStart = start;
        c.sampleEnd = start < 1.0 ? (end - start) / (1.0 - start) : 1.0;
        c.startOffset = std::clamp ((double) s.startOffset, 0.0, 1.0);
        // the time-stretch: the whole sample lasts TIME/192 beats × MUL's factor (round 190dh: MUL kept, TIME without it)
        if (s.stretchBeats > 0.0f)
        {
            c.stretchTime = samplerpage::stretchTime (s) * 192.0f;
            c.stretchMul = std::clamp (s.stretchMul, -20000, 20000);
        }
        c.stretchMode = flStretchMode (s);
        if (s.cut && c.cutGroup == 0 && c.cutBy == 0)
            c.cutGroup = c.cutBy = freeCutGroup++; // "cut itself"
    }

    int freeCutGroup = 1;

    std::optional<ForeignPlugin> hosted (const PluginRef& ref, bool instrument) const
    {
        if (ref.foreign.format != ForeignPlugin::Format::none)
            return ref.foreign; // still the FL original, byte for byte
        if (options.pluginRecord)
            return options.pluginRecord (ref, instrument);
        return std::nullopt;
    }

    void channels()
    {
        for (const auto& c : song.channels)
            freeCutGroup = std::max ({ freeCutGroup, c.cutGroup + 1, c.cutBy + 1 });
        for (int i = 0; i < (int) song.channels.size(); ++i)
            out.channels.push_back (channel (song.channels[(std::size_t) i], i));
    }

    // ---- automation targets ----
    std::optional<flp::ControlId> controlFor (const AutomationTarget& t, std::function<float (float)>* valueMap = nullptr) const
    {
        flp::ControlId id;
        switch (t.kind)
        {
            case AutomationTargetKind::tempo:
                id.raw = flp::ControlId::tempo;
                // (LYNX's tempo target is FL's whole range, 10…522 BPM: the clip's range below is the whole range too)
                return id;
            case AutomationTargetKind::channelVolume:
            case AutomationTargetKind::channelPan:
                if (t.index < 0 || t.index >= (int) song.channels.size())
                    return std::nullopt;
                id.raw = ((std::uint32_t) t.index << 16) | (t.kind == AutomationTargetKind::channelPan ? 1u : 0u);
                return id;
            case AutomationTargetKind::channelAdjust: // FL's channel parameters 0x10…0x14 (round 190dz)
                if (t.index < 0 || t.index >= (int) song.channels.size() || ! leveladjust::valid (t.param))
                    return std::nullopt;
                id.raw = ((std::uint32_t) t.index << 16) | (std::uint32_t) (0x10 + t.param);
                return id;
            case AutomationTargetKind::channelParam:
                if (t.index < 0 || t.index >= (int) song.channels.size() || song.channels[(std::size_t) t.index].instrument != InstrumentKind::plugin
                    || out.channels[(std::size_t) t.index].pluginName != "Fruity Wrapper")
                    return std::nullopt;
                id.raw = ((std::uint32_t) t.index << 16) | (std::uint32_t) (flp::ControlId::pluginParamBase + t.param);
                return id;
            case AutomationTargetKind::mixerVolume:
                id.raw = mixerId (t.index, 0, flp::ControlId::mixerVolume);
                return id;
            case AutomationTargetKind::mixerPan:
                id.raw = mixerId (t.index, 0, flp::ControlId::mixerPan);
                return id;
            case AutomationTargetKind::mixerSend:
                id.raw = mixerId (t.index, 0, flp::ControlId::mixerSendBase + t.slot);
                return id;
            case AutomationTargetKind::effectMix:
            case AutomationTargetKind::effectParam:
            {
                const auto found = slotMap.find ({ t.index, t.slot });
                if (found == slotMap.end())
                    return std::nullopt;
                if (t.kind == AutomationTargetKind::effectMix)
                {
                    id.raw = mixerId (t.index, found->second, 0x1F01);
                    return id;
                }
                const auto& plugin = slotPlugin.at ({ t.index, t.slot });
                auto param = t.param;
                if (plugin != "Fruity Wrapper")
                {
                    // FL's parameter whose map lands on this LYNX parameter (FlEffects' effectParamFromFl), and the
                    // inverse of its value conversion
                    param = -1;
                    for (int k = 0; k < 512 && param < 0; ++k)
                        if (const auto map = effectParamFromFl (plugin, k); map && map->param == t.param)
                        {
                            param = k;
                            if (valueMap != nullptr && map->convert)
                            {
                                const auto convert = map->convert;
                                const auto increasing = convert (1.0f) >= convert (0.0f);
                                *valueMap = [convert, increasing] (float v) {
                                    return (float) invert ([&] (double y) { return (double) convert ((float) y); }, v, increasing);
                                };
                            }
                        }
                    if (param < 0)
                        return std::nullopt;
                }
                id.raw = mixerId (t.index, found->second, flp::ControlId::pluginParamBase + param);
                return id;
            }
            default: return std::nullopt;
        }
    }

    // A hosted effect's side input (slot.sidechain, picked with LYNX's "SC" picker or set by input 2's index) as FL's
    // input 2 index (round 189j): its place in insert t's inputs list as written here — [t, then every insert routed
    // to t, the level-0 sidechain routes included, ascending] (REA_rodada22 §1, core/MixerOps.h insertInputs). An
    // index already there that names another insert is replaced: the slot's source is what LYNX plays. True when
    // `inputs` changed.
    bool withSideIndex (std::vector<PluginOutputRoute>& inputs, int t, int source) const
    {
        if (t <= 0 || source <= 0 || source == t || source >= (int) song.mixer.size())
            return false;
        auto k = 1;
        for (int u = 1; u < source; ++u)
            if (u != t && (song.routes (u, t) || feedsSideInput (u, t)))
                ++k;
        if (inputs.size() > 1 && inputs[1].offset == k && inputs[1].isActive (true))
            return false;
        if (inputs.size() < 2)
            inputs.resize (2);
        inputs[1] = { k, inputs[1].active == 0 ? (std::int8_t) -1 : inputs[1].active };
        return true;
    }
    bool feedsSideInput (int source, int t) const
    {
        const auto& fx = song.mixer[(std::size_t) t].effects;
        return std::any_of (fx.begin(), fx.end(), [source] (const EffectSlot& e) { return e.sidechain == source || e.sidechain2 == source; });
    }

    // the inserts whose effects take insert `source` as a side-chain input (LYNX keeps the source, FL a level-0 route)
    std::vector<int> sidechainTargets (int source) const
    {
        std::vector<int> out;
        for (int u = 1; u < (int) song.mixer.size(); ++u)
            if (u != source)
                for (const auto& fx : song.mixer[(std::size_t) u].effects)
                    if ((fx.sidechain == source || fx.sidechain2 == source) && std::find (out.begin(), out.end(), u) == out.end())
                        out.push_back (u);
        return out;
    }

    // ---- patterns ----
    bool playsSample (int channel) const
    {
        for (const auto& c : out.channels)
            if (c.index == channel)
                return c.type == flp::ChannelType::sampler || c.type == flp::ChannelType::audioClip;
        return false;
    }

    void patterns()
    {
        for (int i = 0; i < (int) song.patterns.size(); ++i)
        {
            const auto& src = song.patterns[(std::size_t) i];
            flp::Pattern p;
            p.index = i + 1;
            p.name = src.name;
            if (src.colour != 0)
                p.colour = rgb (src.colour);
            p.fixedLength = src.lengthTicks > 0 ? ticks (src.lengthTicks) : 0;
            p.loopOn = src.loopOn;
            p.loopMode = (int) src.loopMode;
            for (const auto& l : src.channelLoops)
                p.channelLoops.push_back ({ l.channel, l.setting });
            for (const auto& m : src.markers)
                p.markers.push_back (marker (m));
            for (const auto& n : src.notes)
            {
                if (n.channel < 0 || n.channel >= (int) song.channels.size())
                    continue;
                flp::Note f;
                f.position = ticks (n.start);
                // a step (one step long, on the step grid): FL's step note, which the Channel Rack shows as a step.
                // Only on a channel that plays a sample: FL holds a step note on a generator (3x Osc, a plugin)
                // until something cuts it, so the voices pile up (FL render, round 171e)
                const auto step = n.length == ticksPerStep && n.start % ticksPerStep == 0 && playsSample (n.channel);
                f.length = step ? 0 : std::max<std::uint32_t> (1, ticks (n.length));
                f.channel = (std::uint16_t) n.channel;
                f.key = (std::uint16_t) std::clamp (n.key, 0, 131);
                f.velocity = (std::uint8_t) std::clamp (iround (n.velocity * 128.0), 0, 128);
                f.pan = (std::uint8_t) std::clamp (iround (64.0 + n.pan * 64.0), 0, 128);
                f.finePitch = (std::uint8_t) std::clamp (iround (120.0 + n.finePitch), 0, 240);
                f.release = (std::uint8_t) std::clamp (iround (n.release * 128.0), 0, 128);
                f.modX = (std::uint8_t) std::clamp (iround (n.modX * 256.0), 0, 255);
                f.modY = (std::uint8_t) std::clamp (iround (n.modY * 256.0), 0, 255);
                f.flags = (std::uint16_t) (0x4000 | (n.slide ? 8 : 0));
                f.extra = (std::uint8_t) ((n.colour & 0x0F) | (n.porta ? 0x10 : 0) | (n.muted ? 0x20 : 0));
                f.group = (std::uint16_t) std::max (0, n.group);
                p.notes.push_back (f);
            }
            std::stable_sort (p.notes.begin(), p.notes.end(), [] (const flp::Note& a, const flp::Note& b) { return a.position < b.position; });
            for (const auto& lane : src.events)
            {
                std::function<float (float)> map;
                const auto id = controlFor (lane.target, &map);
                if (! id)
                {
                    if (lane.target.kind != AutomationTargetKind::none)
                        report.dropped.push_back ("Pattern '" + src.name + "': an event lane (its target has no FL equivalent)");
                    continue;
                }
                for (const auto& e : lane.events)
                    p.controlEvents.push_back ({ ticks (e.tick), *id, rawValue (lane.target, map ? map (e.value) : e.value) });
            }
            out.patterns.push_back (std::move (p));
        }
        report.exact.push_back (std::to_string (song.patterns.size()) + " patterns (notes with every property, names, colours)");
    }

    flp::TimeMarker marker (const Marker& m) const
    {
        flp::TimeMarker t;
        t.position = ticks (m.tick);
        t.name = m.name;
        if (m.isSignature())
        {
            t.type = 8;
            t.numerator = m.numerator;
            t.denominator = m.denominator;
        }
        else
            t.type = m.action;
        return t;
    }

    // ---- automation clips: FL channels after the song's ----
    void automations()
    {
        for (int a = 0; a < (int) song.automations.size(); ++a)
        {
            const auto& src = song.automations[(std::size_t) a];
            if (src.isLive())
            {
                report.dropped.push_back ("Automation '" + src.name + "': a live controller link");
                continue;
            }
            std::function<float (float)> map;
            const auto id = controlFor (src.target, &map);
            if (! id && src.target.kind != AutomationTargetKind::none)
            {
                report.dropped.push_back ("Automation '" + src.name + "': its target has no FL equivalent");
                continue;
            }
            // (no target at all: FL's own unlinked automation clip, kept as one)
            flp::Channel c;
            c.index = (int) out.channels.size();
            c.type = flp::ChannelType::automationClip;
            c.name = src.name;
            // an automation clip's channel levels are its range: pan = minimum, volume = maximum (of 12800, in the
            // target's whole range) [DATA: FL's tempo clips keep 1250…4250 = 60…180 BPM; FL 2026 played a clip written
            // with a channel's default levels over 6400…10000: round 171e, FL render]. Always the whole range here.
            c.hasLevels = true;
            c.pan = 0;
            c.volume = 12800;
            for (std::size_t i = 0; i < src.points.size(); ++i)
            {
                const auto& pt = src.points[i];
                flp::AutomationPoint fp;
                fp.position = (double) pt.tick / ppq;
                fp.value = std::clamp ((double) (map ? map (pt.value) : pt.value), 0.0, 1.0);
                // FL's curve sits on the segment ending at the point (FlpImport: previous.tension = −p.tension)
                fp.tension = i > 0 ? 0.0f - src.points[i - 1].tension : 0.0f; // (0 − t: a straight segment writes +0)
                fp.curve = i > 0 ? flCurveOf (src.points[i - 1]) : (std::uint8_t) 0;
                c.automation.push_back (fp);
            }
            if (c.automation.empty())
                c.automation.push_back ({ 0.0, (double) (map ? map (song.targetValue (src.target)) : song.targetValue (src.target)), 0.0f });
            // FL's clip LFO (round 177b): the fields back as they were read; the level carries the clip's range (FL scales
            // the LFO by its channel range, written whole here)
            if (const auto& lfo = src.lfo; ! lfo.isDefault())
            {
                auto& f = c.automationLfo;
                f.present = true;
                f.enabled = lfo.on ? -1 : 0;
                f.level = std::abs (lfo.range - 1.0f) < 1.0e-6f ? lfo.level : (int) std::lround (lfo.level * lfo.range);
                f.mode = lfo.add ? 1 : 0;
                f.flags = lfo.flags;
                f.speed = lfo.speed;
                f.tension = lfo.tension;
                f.skew = lfo.skew;
                f.pulseWidth = lfo.pulseWidth;
                f.phase = lfo.phase;
                if (lfo.fl.present)
                {
                    f.mainFlags = lfo.fl.mainFlags;
                    f.mainEnabled = lfo.fl.mainEnabled;
                    f.mainLoop = lfo.fl.mainLoop;
                    f.adsr = lfo.fl.adsr;
                    f.lfoLoop = lfo.fl.lfoLoop;
                }
                f.points.clear();
                for (std::size_t i = 0; i < lfo.amp.size(); ++i)
                    f.points.push_back ({ (double) lfo.amp[i].tick / ppq, (double) std::clamp (lfo.amp[i].value, 0.0f, 1.0f),
                                          i > 0 ? 0.0f - lfo.amp[i - 1].tension : 0.0f, i > 0 ? flCurveOf (lfo.amp[i - 1]) : (std::uint8_t) 0 });
                if (lfo.amp == lfo.fl.rawAmp && lfo.fl.rawPoints.size() == lfo.amp.size() * 24)
                    f.rawPoints = lfo.fl.rawPoints; // unchanged since the import: its records byte for byte
            }
            automationChannel[a] = c.index;
            if (id)
                out.automationLinks.push_back ({ c.index, *id });
            out.channels.push_back (std::move (c));
        }
        if (! automationChannel.empty())
            report.exact.push_back (std::to_string (automationChannel.size()) + " automation clips (points, curves, links)");
        for (const auto& v : song.initialValues)
            if (const auto id = controlFor (v.target))
                out.initControls.push_back ({ 0, *id, rawValue (v.target, v.value) });
        if (! song.midiLinks.empty())
            report.notes.push_back ("MIDI controller links stay in LYNX (FL keeps them per machine)");
    }

    // ---- the Playlist ----
    void arrangements()
    {
        for (int i = 0; i < (int) song.arrangements.size(); ++i)
        {
            const auto current = i == song.currentArrangement;
            const auto& arrangement = song.arrangements[(std::size_t) i];
            const auto& clips = current ? song.playlist : arrangement.clips;
            flp::Arrangement a;
            a.index = i;
            a.name = arrangement.name;
            for (const auto& m : current ? song.markers() : arrangement.markers)
                a.markers.push_back (marker (m));
            for (const auto& clip : clips)
                if (auto item = playlistItem (clip))
                    a.items.push_back (*item);
            std::stable_sort (a.items.begin(), a.items.end(), [] (const flp::PlaylistItem& x, const flp::PlaylistItem& y) { return x.position < y.position; });
            for (int t = 0; t < (int) song.playlistTracks.size() && t < 500; ++t)
                a.tracks.push_back (trackInfo (song.playlistTracks[(std::size_t) t], t));
            out.arrangements.push_back (std::move (a));
        }
        report.exact.push_back ("Playlist: " + std::to_string (song.arrangements.size()) + " arrangement(s), tracks, markers");
    }

    std::optional<flp::PlaylistItem> playlistItem (const Clip& clip) const
    {
        flp::PlaylistItem item;
        item.track = std::clamp (clip.track, 0, 499);
        item.position = ticks (clip.start);
        item.length = std::max<std::uint32_t> (1, ticks (clip.length));
        // FL's clip mute: bit 0x2000 of the item flags (issue #110, REA round 182e)
        item.flags = (std::uint16_t) (flp::PlaylistItem::defaultFlags | (clip.muted ? flp::PlaylistItem::mutedFlag : 0));
        switch (clip.kind)
        {
            case ClipKind::pattern:
                if (clip.pattern < 0 || clip.pattern >= (int) song.patterns.size())
                    return std::nullopt;
                item.kind = flp::PlaylistItem::Kind::pattern;
                item.index = clip.pattern + 1;
                item.gain = 0.0f;
                if (clip.offset > 0)
                {
                    item.patternStartTick = (std::int32_t) ticks (clip.offset);
                    item.patternEndTick = (std::int32_t) ticks (clip.offset + clip.length);
                }
                return item;
            case ClipKind::audio:
            {
                if (clip.channel < 0 || clip.channel >= (int) song.channels.size())
                    return std::nullopt;
                item.kind = flp::PlaylistItem::Kind::channelClip;
                item.index = clip.channel;
                const auto stretch = clip.stretch > 0.0 ? clip.stretch : 1.0;
                item.stretch = stretch;
                item.length = std::max<std::uint32_t> (1, ticks ((std::int64_t) std::llround ((double) clip.length / stretch)));
                // start / end in milliseconds (of the sample: a stretched channel's factor, REA_rodada110 §1)
                const auto msPerTick = 60000.0 / song.bpm / ppq;
                auto factor = 1.0;
                const auto& ch = song.channels[(std::size_t) clip.channel];
                if (ch.sampler.stretchBeats > 0.0f && options.sampleSeconds)
                    if (const auto seconds = options.sampleSeconds (ch.samplePath); seconds > 0.0)
                        factor = seconds * 1000.0 / (ch.sampler.stretchBeats * 60000.0 / song.bpm);
                const auto offset = ((double) clip.offset + clip.offsetFraction) / stretch;
                item.startOffset = (float) (offset * msPerTick * factor);
                item.endOffset = (float) ((offset + (double) clip.length / stretch) * msPerTick * factor);
                item.fadeInMs = (float) ((double) clip.fadeIn * msPerTick);
                item.fadeOutMs = (float) ((double) clip.fadeOut * msPerTick);
                item.fadeInTension = clip.fadeIn > 0 ? clip.fadeInTension : 0.0f;
                item.fadeOutTension = clip.fadeOut > 0 ? clip.fadeOutTension : 0.0f;
                {
                    // the shapes follow the clip, the rest of FL's bits stay as they came
                    const auto shape = [&clip] (int side, std::uint32_t raw) -> std::uint32_t
                    { return (clip.linearFades & (1 << side)) != 0 ? 1u : raw == 1u ? 0u : raw; };
                    item.fadeFlags = (clip.flFadeFlags & ~((3u << 8) | (3u << 12)))
                                     | (shape (0, (clip.flFadeFlags >> 8) & 3) << 8) | (shape (1, (clip.flFadeFlags >> 12) & 3) << 12);
                }
                item.gain = clip.gain;
                item.pitch = clip.pitch;
                item.reversed = clip.reversed;
                return item;
            }
            case ClipKind::automation:
            {
                const auto found = automationChannel.find (clip.automation);
                if (found == automationChannel.end())
                    return std::nullopt;
                item.kind = flp::PlaylistItem::Kind::channelClip;
                item.index = found->second;
                item.gain = 1.0f;
                // the trim in beats. −1 = none, and FL then takes the clip's whole content for its end, whatever the
                // item's length says: a 20-beat clip of a 546-beat automation at beat 284 made Thief 208 bars long
                // instead of 144 (round 180d). So only a clip that shows all its content goes without a trim, as in
                // FL's own files.
                if (clip.offset > 0 || (clip.automation >= 0 && clip.automation < (int) song.automations.size()
                                        && clip.length != song.automations[(std::size_t) clip.automation].length()))
                {
                    item.startOffset = (float) ((double) clip.offset / ppq);
                    item.endOffset = (float) ((double) (clip.offset + clip.length) / ppq);
                }
                else
                    item.startOffset = item.endOffset = -1.0f;
                return item;
            }
        }
        return std::nullopt;
    }

    static flp::PlaylistTrackInfo trackInfo (const PlaylistTrack& t, int index)
    {
        flp::PlaylistTrackInfo info;
        info.index = index;
        info.name = t.name;
        info.enabled = ! t.muted;
        info.colour = t.colour != 0 ? rgb (t.colour) : 0x485156;
        info.grouped = t.grouped;
        info.expanded = ! t.collapsed;
        info.lockedToContent = t.lockedToContent;
        info.lockedHeight = t.lockedHeight;
        info.heightScale = t.height > 0 ? polish::flSizeForHeight (t.height) : 1.0f; // (round 190ft: FL's size factor)
        info.mode = t.mode == PlaylistTrackMode::audio ? 1 : t.mode == PlaylistTrackMode::instrument ? 3 : 0;
        info.mixerTrack = t.mixerTrack;
        info.channel = t.channel;
        info.icon = t.icon;
        info.motion = (int) t.performance.motion;
        info.press = (int) t.performance.press;
        info.triggerSync = (int) t.performance.triggerSync;
        info.positionSync = (int) t.performance.positionSync;
        info.queue = t.performance.queue;
        info.tolerant = t.performance.tolerant;
        return info;
    }

    // ---- the mixer ----
    void mixer()
    {
        for (int t = 0; t < (int) song.mixer.size(); ++t)
        {
            const auto& track = song.mixer[(std::size_t) t];
            flp::MixerInsert m;
            m.index = t;
            m.name = t == 0 ? std::string() : track.name;
            m.colour = track.colour != 0 ? rgb (track.colour) : 0;
            m.colourSet = track.colour != 0 ? 1 : 0;
            m.icon = track.icon;
            m.flags = 0x04u | (track.muted ? 0u : 0x08u) | (track.invertPhase ? 0x01u : 0u) | (track.swapChannels ? 0x02u : 0u)
                      | (t > 0 && track.solo ? 0x1000u : 0u) | (t > 0 && track.separator ? 0x400u : 0u)
                      | (track.compensateMasterLatency ? 0u : 0x4000u)
                      | ((std::uint32_t) (t == 0 ? 0 : track.dock == MixerDock::left ? 0 : track.dock == MixerDock::right ? 2 : 1) << 6);
            m.latencyMs = track.latencyMs;
            m.latencyBypass = (int) track.latencyBypass;
            m.recordSource = (int) track.recordSource;
            m.monitor = (int) track.monitor;
            m.input = track.flInput;
            m.output = t == 0 ? 0 : track.outputChannel == 0 ? 0 : -1;
            if (t > 0)
            {
                m.hasRoutes = true;
                if (track.toMaster)
                    m.routes.push_back (0);
                for (const auto& s : track.sends)
                    if (s.target > 0 && s.target < (int) song.mixer.size() && s.target != t)
                        m.routes.push_back (s.target);
                // a side-chain input is FL's route at level 0 (sidechain only): without it FL's compressors and
                // limiters have nothing to follow (thief's kick ducking was lost: +3.7 dB in FL, round 171e2)
                for (const auto target : sidechainTargets (t))
                    if (std::find (m.routes.begin(), m.routes.end(), target) == m.routes.end())
                        m.routes.push_back (target);
            }

            // the levels (FL ranges: volume 0..16000 with 12800 = 0 dB, pan ±6400, separation ±64, route levels on the fader curve)
            auto control = [&] (int slot, int param, std::int32_t value) { out.controls.push_back ({ { mixerId (t, slot, param) }, value }); };
            // FL has no VCA (round 177a): the VCAs' gain is baked into the member's fader
            const auto vca = t > 0 ? mixerops::vcaGain (song, t) : 1.0f;
            const auto volume = vca == 1.0f ? track.volume : std::clamp (faderPosition (faderGain (track.volume) * vca), 0.0f, 1.0f);
            control (0, flp::ControlId::mixerVolume, std::clamp (iround (volume * 16000.0), 0, 16000));
            control (0, flp::ControlId::mixerPan, std::clamp (iround (track.pan * 6400.0), -6400, 6400));
            control (0, flp::ControlId::mixerStereo, std::clamp (iround (track.stereo * 64.0), -64, 64));
            for (int b = 0; b < 3; ++b)
            {
                const auto& eq = track.eq;
                // (a bypassed EQ, LYNX's own: FL has no bypass, so it goes flat — what is heard stays the same)
                control (0, flp::ControlId::mixerEqBase + b, eq.bypassed ? 0 : std::clamp (iround (eq.gainDb[(std::size_t) b] * 100.0), -1800, 1800));
                const auto hz = std::clamp ((double) eq.hz[(std::size_t) b], 10.0, 16000.0);
                control (0, flp::ControlId::mixerEqBase + 8 + b, std::clamp (iround (65536.0 * std::log ((hz - 10.0) / 159.9 + 1.0) / std::log (101.0)), 0, 65536));
                control (0, flp::ControlId::mixerEqBase + 16 + b, std::clamp (iround (eq.width[(std::size_t) b] * 65536.0), 0, 65536));
            }
            if (t > 0)
            {
                control (0, flp::ControlId::mixerSendBase, std::clamp (iround (faderPosition (track.masterLevel) * 16000.0), 0, 16000));
                for (const auto& s : track.sends)
                    if (s.target > 0 && s.target < (int) song.mixer.size())
                        control (0, flp::ControlId::mixerSendBase + s.target, std::clamp (iround (faderPosition (s.level) * 16000.0), 0, 16000));
                for (const auto target : sidechainTargets (t))
                    if (std::none_of (track.sends.begin(), track.sends.end(), [&] (const MixerSend& s) { return s.target == target; }))
                        control (0, flp::ControlId::mixerSendBase + target, 0);
            }

            // the effect slots. Round 190bu: FL has no dry/wet for a whole chain, so the insert's Blend goes into each
            // exported slot's own Mix (the same for one effect, close for several) and the report says so
            const auto chainBlend = std::isfinite (track.chainBlend) ? std::clamp (track.chainBlend, 0.0f, 1.0f) : 1.0f;
            bool blendNoted = false;
            int next = 0;
            for (int s = 0; s < (int) track.effects.size(); ++s)
            {
                const auto& fx = track.effects[(std::size_t) s];
                const auto row = std::max (next, fx.row >= 0 ? fx.row : next);
                if (row >= maxEffectSlots)
                    break;
                flp::MixerInsert::Slot slot;
                slot.index = row;
                slot.wrapperFlags = fx.smartDisable ? 0x160 : 0x140;
                const auto label = insertLabel (song, t) + " slot " + std::to_string (row + 1) + ": ";
                std::string what;
                if (fx.isSubstitute() && fx.substituteFor.foreign.format != ForeignPlugin::Format::none)
                {
                    slot.plugin = "Fruity Wrapper";
                    slot.state = writeWrapper (fx.substituteFor.foreign, false);
                    what = fx.substituteFor.foreign.name + " (the original plugin, LYNX's substitute left out)";
                }
                else if (fx.kind == EffectKind::plugin)
                {
                    if (auto record = hosted (fx.plugin, false))
                    {
                        slot.plugin = "Fruity Wrapper";
                        // the side input picked with LYNX's "SC" picker is FL's input 2 index (round 189j)
                        auto ref = fx.plugin;
                        if (ref.foreign.format == ForeignPlugin::Format::none)
                            withSideIndex (ref.inputs, t, fx.sidechain);
                        else if (withSideIndex (record->inputRoutes, t, fx.sidechain) && ! record->hasRoutes)
                        {
                            record->hasRoutes = true;
                            record->outputRoutes = { { 0, 1 } };
                        }
                        slot.state = writeWrapper (*record, false, ref.foreign.format == ForeignPlugin::Format::none ? &ref : nullptr);
                        slot.wrapperFlags = withFlDetached (slot.wrapperFlags, fx.plugin.detached); // (issue #116)
                        what = record->name + " (Fruity Wrapper)";
                    }
                }
                else if (const auto native = nativeLookState (fx))
                {
                    // a Master look's slot (round 189k): FL's own plugin with the slot's settings
                    slot.plugin = native->first;
                    slot.state = native->second;
                    what = native->first;
                }
                else if (originMatches (fx))
                {
                    slot.plugin = fx.flOrigin.plugin;
                    slot.state = fx.flOrigin.state;
                    what = fx.flOrigin.plugin;
                }
                else if (const auto sub = substituteFor (fx); sub && (options.lynxOnly == LynxOnly::substitute || t == 0))
                {
                    slot.plugin = sub->first;
                    slot.state = sub->second;
                    report.substituted.push_back (label + effectInfo (fx.kind).name + " → " + sub->first + " (FL's defaults)");
                }
                if (slot.plugin.empty())
                {
                    if (song.slotActive (fx))
                        report.dropped.push_back (label + effectInfo (fx.kind).name + " (LYNX only)");
                    continue;
                }
                if (! what.empty())
                {
                    report.exact.push_back (label + what);
                }
                slotMap[{ t, s }] = row;
                slotPlugin[{ t, s }] = slot.plugin;
                control (row, 0x1F00, fx.enabled ? 1 : 0);
                control (row, 0x1F01, std::clamp (iround (fx.mix * chainBlend * 12800.0), 0, 12800));
                if (chainBlend < 1.0f && ! blendNoted)
                {
                    blendNoted = true;
                    report.notes.push_back (insertLabel (song, t) + ": the chain's Blend (" + std::to_string (iround (chainBlend * 100.0))
                                            + "%) is written into each effect's Mix level (FL has no dry/wet for a whole chain)");
                }
                m.slots.push_back (std::move (slot));
                next = row + 1;
            }
            out.mixer.push_back (std::move (m));
        }
        fixSideInputs();
        if (! song.vcas.empty())
            report.notes.push_back ("VCA faders: FL Studio has none; their levels are baked into the member tracks' faders (their automation and mutes are left out)");
        // LYNX's pre-fader sends (round 190aq): FL has post-fader routes only, so they are written as those
        if (const auto pre = std::accumulate (song.mixer.begin(), song.mixer.end(), 0, [] (int n, const MixerTrack& m) {
                return n + (int) std::count_if (m.sends.begin(), m.sends.end(), [] (const MixerSend& s) { return s.pre; });
            }); pre > 0)
            report.notes.push_back ("Pre-fader sends: FL Studio's sends are post-fader; " + std::to_string (pre)
                                    + (pre == 1 ? " pre-fader send is" : " pre-fader sends are") + " written as post-fader (they follow the track's fader in FL)");
        report.exact.push_back ("Mixer: " + std::to_string (song.mixer.size()) + " tracks (names, colours, routes, sends, levels, EQ)");
    }

    // FL's plugins name a side input by its place k in the insert's inputs list (itself, then the inserts routing into
    // it by number; REA_rodada22 §1). An effect exported as its imported FL state still holds the k it was imported
    // with; when LYNX's side-chain source now sits at another place (an insert moved, round 177a, or a new source), k is
    // rewritten. A k that still names LYNX's source, or an effect without a LYNX source, keeps its bytes.
    void fixSideInputs()
    {
        std::map<int, std::vector<int>> inputs;
        for (const auto& m : out.mixer)
            for (const auto target : m.routes)
                if (target > 0 && target != m.index)
                    inputs[target].push_back (m.index);
        auto sourceAt = [&inputs] (int track, int k) {
            auto list = inputs[track];
            std::sort (list.begin(), list.end());
            list.erase (std::unique (list.begin(), list.end()), list.end());
            list.insert (list.begin(), track);
            return std::pair { list, k > 0 && k < (int) list.size() ? list[(std::size_t) k] : -1 };
        };
        auto rd = [] (const std::vector<std::uint8_t>& d, std::size_t at) {
            std::int32_t v = 0;
            if (at + 4 <= d.size())
                std::memcpy (&v, d.data() + at, 4);
            return (int) v;
        };
        auto fix = [&] (std::vector<std::uint8_t>& d, std::size_t at, int track, int source) {
            if (source <= 0 || at + 4 > d.size())
                return;
            const auto [list, now] = sourceAt (track, rd (d, at));
            if (now == source)
                return;
            const auto it = std::find (list.begin(), list.end(), source);
            if (it == list.end())
                return;
            const auto k = (std::int32_t) (it - list.begin());
            std::memcpy (d.data() + at, &k, 4);
        };
        for (auto& m : out.mixer)
        {
            const auto t = m.index;
            if (t <= 0 || t >= (int) song.mixer.size())
                continue;
            const auto& effects = song.mixer[(std::size_t) t].effects;
            for (int s = 0; s < (int) effects.size(); ++s)
            {
                const auto found = slotMap.find ({ t, s });
                if (found == slotMap.end())
                    continue;
                const auto& fx = effects[(std::size_t) s];
                if (! originMatches (fx))
                    continue;
                auto slot = std::find_if (m.slots.begin(), m.slots.end(), [row = found->second] (const flp::MixerInsert::Slot& x) { return x.index == row; });
                if (slot == m.slots.end() || slot->plugin != fx.flOrigin.plugin)
                    continue;
                auto& d = slot->state;
                if (slot->plugin == "Fruity Limiter" && rd (d, 0) >= 6)
                    fix (d, 0xA4, t, fx.sidechain);
                else if (slot->plugin == "Fruity Compressor" && rd (d, 0) == 3)
                    fix (d, 0x20, t, fx.sidechain);
                else if (slot->plugin == "LuxeVerb")
                    fix (d, 4, t, fx.sidechain);
                else if (slot->plugin == "Transmitter")
                    fix (d, 12 + 4 * (std::size_t) transmitter::count, t, fx.sidechain);
                else if (slot->plugin == "Transporter" && d.size() >= 4)
                    fix (d, 4 + 4 * (std::size_t) (rd (d, 0) >= 8 ? 22 : 20) + 2 + 12, t, fx.sidechain);
                else if (slot->plugin == "Vocodex")
                    if (const auto v = vocodex::parseState (d))
                    {
                        if (! v->lrEncoding)
                            fix (d, v->paramsAt + 80 + 20, t, fx.sidechain);
                        if (v->carIn >= 0)
                            fix (d, v->paramsAt + 80 + 24, t, fx.sidechain2);
                    }
            }
        }
    }

    void run()
    {
        choosePpq();
        globals();
        channels();
        mixer();    // (before the automations: their effect targets need the slots)
        patterns();
        automations();
        arrangements();
        out.mixerTrackCount = 0;
        out.header.channelCount = (std::uint16_t) out.channels.size();
    }
};
} // namespace

flp::Project toProject (const Song& input, const Options& options, Report& report)
{
    // FL 2026 has no Pattern Blocks: they become pattern clips (each pattern on a track of its own, locked to content)
    Song song = input;
    if (patternblocks::active (song))
    {
        const auto n = patternblocks::toClipsInEveryArrangement (song);
        if (n > 0)
            report.notes.push_back ("Pattern Blocks: " + std::to_string (n) + " block(s) written as pattern clips (FL 2026 has no blocks)");
    }
    // automation lanes (round 177b): FL has none; each becomes an automation clip on a free track
    if (const auto n = autolanes::lanesToClips (song); n > 0)
        report.notes.push_back ("Automation lanes: " + std::to_string (n) + " lane(s) written as automation clips (FL has no lanes)");
    // modulators (round 177b): FL has none; the modulated controls keep their own values
    for (const auto& m : song.modulators)
        if (! m.routes.empty())
            report.dropped.push_back ("Modulator '" + m.name + "' (" + std::to_string (m.routes.size())
                                      + " target(s)): FL has no modulators; the targets keep their own values");
    Converter c (song, options, report);
    c.run();
    return std::move (c.out);
}

std::vector<std::uint8_t> exportFlp (const Song& song, const Options& options, Report& report)
{
    return flp::writeProject (toProject (song, options, report), options.write);
}
} // namespace lynx::flpexport
