#include "core/FlmImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

#include "core/DirectWave.h"
#include "core/FlmEffects.h"
#include "core/MiniSynth.h"
#include "core/TransistorBass.h"
#include "core/ThreeOsc.h"
#include "core/MissingFiles.h"

// FL Studio Mobile → LYNX, round 182c (docs/analise/FLM_FORMAT.md, REA_rodada182c.md §5). Times: FLM clips and events
// count 128 ticks per beat and their lengths in beats; LYNX counts `ppq` (960) ticks per beat.

namespace lynx
{
namespace
{
std::int64_t ticksOf (double beats) noexcept { return (std::int64_t) std::llround (beats * ppq); }

std::string joinPath (const std::string& folder, const std::string& rel)
{
    if (folder.empty())
        return rel;
    const auto sep = folder.find ('\\') != std::string::npos && folder.find ('/') == std::string::npos ? '\\' : '/';
    auto out = folder;
    if (out.back() != '/' && out.back() != '\\')
        out += sep;
    for (const auto c : rel)
        out += (c == '/' || c == '\\') ? sep : c;
    return out;
}

std::string fileStem (const std::string& path)
{
    auto name = pathFileName (path);
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos && dot > 0)
        name.resize (dot);
    return name;
}

bool isAudioFile (const std::string& path)
{
    auto lower = path;
    std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    for (const auto* ext : { ".wav", ".mp3", ".ogg", ".flac", ".aif", ".aiff" })
        if (lower.size() > std::strlen (ext) && lower.compare (lower.size() - std::strlen (ext), std::string::npos, ext) == 0)
            return true;
    return false;
}

std::string moduleLabel (std::uint32_t type)
{
    const auto* name = flm::moduleName (type);
    return name != nullptr ? std::string (name) : "Module #" + std::to_string (type);
}

struct Ctx
{
    const FlmImportOptions& options;
    FlpImportReport* report;
    Song song;
    std::map<std::string, int> audioChannels; // resolved path | pitch | insert → audioClip channel
    std::set<std::string> missingSeen;
    int stepsPerBeat = 4; // TDIV's second byte (the song's +0xa9): the grid's steps per beat, and 384 / it = the modules' tick rate

    explicit Ctx (const FlmImportOptions& o) : options (o), report (o.common.report) {}

    void note (std::map<std::string, int> FlpImportReport::* list, const std::string& what)
    {
        if (report != nullptr)
            ++(report->*list)[what];
    }

    // The file a sample reference points at on this PC, or "" (reported as missing, under its expected path).
    // Candidates: the root's folder + the relative path, the PRST relative name next to the project, the path saved on
    // the device, the file name next to the project.
    std::string resolve (const std::vector<std::string>& candidates, const std::string& user)
    {
        const auto& o = options.common;
        std::string shown;
        for (const auto& c : candidates)
            if (! c.empty())
            {
                shown = c;
                break;
            }
        if (shown.empty())
            return {};
        if (const auto it = o.relocations.find (shown); it != o.relocations.end() && o.fileExists && o.fileExists (it->second))
            return it->second;
        if (o.fileExists)
            for (const auto& c : candidates)
                if (! c.empty() && o.fileExists (c))
                {
                    if (report != nullptr && c != shown)
                    {
                        ++report->relocatedSamples;
                        report->relocatedFiles.push_back ({ shown, c });
                    }
                    return c;
                }
        if (report != nullptr)
        {
            if (missingSeen.insert (shown).second)
                ++report->missingSamples;
            addMissingFile (report->missingFiles, shown, user);
        }
        return {};
    }

    // a path saved on the device mapped onto this PC: below the app's "Resources" folder → FL's copy of them; below
    // the app's user files (Android "…/com.imageline.FLM/files/", Windows "…/LocalState/") → the project's folder
    std::vector<std::string> deviceCandidates (const std::string& saved) const
    {
        std::vector<std::string> out;
        auto p = "/" + saved; // (FLM also saves "Resources/…" without a leading folder)
        std::replace (p.begin(), p.end(), '\\', '/');
        const auto below = [&p] (const std::string& marker) -> std::string {
            const auto at = p.find (marker);
            return at == std::string::npos ? std::string() : p.substr (at + marker.size());
        };
        if (const auto r = below ("/Resources/"); ! r.empty() && ! options.resourceRoot.empty())
            out.push_back (joinPath (options.resourceRoot, r));
        for (const auto* marker : { "/com.imageline.FLM/files/", "/LocalState/" })
            if (const auto r = below (marker); ! r.empty())
                out.push_back (joinPath (options.common.projectFolder, r));
        return out;
    }

    std::string resolveSample (const flm::SampleRef& s, const std::string& user)
    {
        const auto& folder = options.common.projectFolder;
        std::vector<std::string> candidates;
        if (! s.path.empty())
        {
            if (s.root == "@R@")
                candidates.push_back (options.resourceRoot.empty() ? std::string() : joinPath (options.resourceRoot, s.path));
            else
                candidates.push_back (joinPath (folder, s.path)); // "@L@" (and a file without a root)
        }
        if (! s.relName.empty() && s.root != "@R@")
            candidates.push_back (joinPath (folder, s.relName));
        for (const auto& c : deviceCandidates (s.savedPath))
            candidates.push_back (c);
        candidates.push_back (s.savedPath);
        if (const auto name = pathFileName (s.path.empty() ? s.savedPath : s.path); ! name.empty())
            candidates.push_back (joinPath (folder, name));
        return resolve (candidates, user);
    }
};

// One pass of a looped clip: at `at` beats into the clip, the content from `from` for `length` beats.
struct Pass
{
    double at, from, length;
};

std::vector<Pass> passesOf (const flm::Clip& clip)
{
    std::vector<Pass> out;
    const auto total = clip.timelineLength();
    const auto loop = clip.loopLength;
    if (total <= 1e-9)
        return out;
    if (loop <= 1e-9)
    {
        out.push_back ({ 0.0, std::max (0.0, clip.offset), total });
        return out;
    }
    double at = 0.0, from = std::fmod (std::max (0.0, clip.offset), loop);
    for (int guard = 0; at < total - 1e-9 && guard < 4096; ++guard)
    {
        const auto take = std::min (loop - from, total - at);
        if (take <= 1e-9)
        {
            from = 0.0;
            continue;
        }
        out.push_back ({ at, from, take });
        at += take;
        from = 0.0;
    }
    return out;
}

// FLM's fader law (FLM_FORMAT §3, PROVEN-REA 0x1805943b0): gain = 10^(3.3·v^0.6 − 3), i.e. 66·v^0.6 − 60 dB; 0 dB at
// v ≈ 0.853, +6 dB at the top
float flmGain (float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (v <= 0.0f)
        return 0.0f;
    return (float) std::pow (10.0, 3.3 * std::pow ((double) v, 0.6) - 3.0);
}

// FLM's 3x Osc against LYNX's 3x Osc at the same velocity (A/B inside FL, round 190u): the level is the same
// (0.00 dB on the default patch) and FLM sounds one octave below the desktop plugin (key 60 with coarse 0 = 261.6 Hz,
// the desktop's 523.3 Hz), so the channel's pitch carries −12 semitones
constexpr double threeOscFlmLevel = 1.0;
constexpr float threeOscFlmPitch = -12.0f;

float velocityOf (const flm::Event& e) noexcept { return std::clamp ((float) e.velocity / 255.0f, 0.0f, 1.0f); }

// A note's pan: FLM plays byte / 255 (0 = left, 1 = right); 127 is the centre every editor writes (they read the byte
// as byte / 254) and stays the centre here (FLM's own 127 / 255 leaves the right side 0.03 dB down)
float panOf (const flm::Event& e) noexcept
{
    return e.pan == 127 ? 0.0f : std::clamp ((2.0f * (float) e.pan - 255.0f) / 255.0f, -1.0f, 1.0f);
}

// The pattern holding one content of a note or drum lane: `channelFor(key)` is the channel and key a note plays on
// (channel < 0 = dropped). The note bytes beyond the velocity, as FLM's clip compiler (0x18075d6b0) and song scheduler
// (0x180770330) play them (PROVEN-REA, rounds 190u / 190ab):
// - pan (byte 15) on every note;
// - drum lanes: the pitch (bytes 16–17, p = u16 / 65534) plays the pad at 2^(2p − 1) of its speed, one octave either
//   way: here the key and fine pitch of the sampler's note;
// - repeat (byte 18, n > 0): n more note-ons of the same key, velocity and pan at i / (n + 1) of the note's 1/16 step
//   (W = 1/4 + swing / 8 beats on even steps, 1/4 − swing / 8 on odd ones; swing = the lane's own, 0 on note lanes in
//   every file seen). FLM sends one note-off, at the note's end: the extra notes end there too;
// - slide (byte 19, note lanes only: a drum lane plays it as a hit): no note-on; the held note (the highest plain note
//   sounding at the slide's start) glides to the slide's key over the slide's length — LYNX's slide note. A slide
//   with no held note plays nothing in FLM and is dropped. Not converted: FLM also glides the velocity and the pan.
template <typename ChannelFor>
int addPattern (Ctx& ctx, const flm::Clip& clip, const std::string& name, bool drums, double swing, ChannelFor channelFor)
{
    Pattern p;
    p.name = name;
    const auto lengthBeats = clip.loopLength > 1e-9 ? clip.loopLength : clip.timelineLength();
    p.lengthTicks = std::max<std::int64_t> (1, ticksOf (lengthBeats));
    const auto held = [&clip] (const flm::Event& s) {
        for (const auto& o : clip.events)
            if (o.slide == 0 && o.key >= 0 && o.length > 0.0 && o.tick <= s.tick
                && (double) s.tick < (double) o.tick + o.length * flm::ticksPerBeat)
                return true;
        return false;
    };
    int repeats = 0, slides = 0, orphans = 0, pans = 0, pitches = 0;
    for (const auto& e : clip.events)
    {
        const auto [channel, key] = channelFor (e.key);
        if (channel < 0)
            continue;
        const auto slide = ! drums && e.slide != 0;
        if (slide && ! held (e))
        {
            ++orphans;
            continue;
        }
        Note n;
        n.channel = channel;
        n.key = std::clamp (key, 0, 127);
        n.start = ticksOf ((double) e.tick / flm::ticksPerBeat);
        if (n.start >= p.lengthTicks)
            continue;
        n.length = std::clamp<std::int64_t> (ticksOf (e.length), 1, p.lengthTicks - n.start);
        n.velocity = velocityOf (e);
        n.pan = panOf (e);
        pans += n.pan != 0.0f ? 1 : 0;
        n.slide = slide;
        slides += slide ? 1 : 0;
        if (drums && e.value != 0x7FFF)
        {
            const auto semitones = 12.0 * (2.0 * std::min ((double) e.value / 65534.0, 1.0) - 1.0);
            const auto whole = std::round (semitones);
            n.key = std::clamp (n.key + (int) whole, 0, 127);
            n.finePitch = (float) (100.0 * (semitones - whole));
            ++pitches;
        }
        p.notes.push_back (n);
        if (e.repeat > 0 && ! slide)
        {
            const auto startBeats = (double) e.tick / flm::ticksPerBeat;
            const auto odd = ((int) (startBeats * 4.0) & 1) != 0;
            const auto step = odd ? 0.25 - swing * 0.125 : 0.25 + swing * 0.125;
            const auto end = n.start + n.length;
            for (int i = 1; i <= e.repeat; ++i)
            {
                // (FLM's own arithmetic: the fraction and the offset in float, the tick truncated)
                const auto tick = (int) ((double) ((float) ((double) ((float) i / (float) (e.repeat + 1)) * step) * 128.0f) + startBeats * 128.0);
                auto hit = n;
                hit.start = ticksOf ((double) tick / flm::ticksPerBeat);
                if (hit.start <= n.start || hit.start >= p.lengthTicks)
                    continue;
                hit.length = drums ? std::clamp<std::int64_t> (n.length, 1, p.lengthTicks - hit.start) : std::max<std::int64_t> (1, end - hit.start);
                p.notes.push_back (hit);
                ++repeats;
            }
        }
    }
    p.sortNotes();
    ctx.song.patterns.push_back (std::move (p));
    const auto count = [&ctx] (int n, const char* what) {
        if (n > 0 && ctx.report != nullptr)
            ctx.report->converted[what] += n;
    };
    count (pans, "FL Studio Mobile: note pan");
    count (pitches, "FL Studio Mobile: drum note pitch");
    count (repeats, "FL Studio Mobile: note repeat (extra hits)");
    count (slides, "FL Studio Mobile: slide note");
    if (orphans > 0 && ctx.report != nullptr)
        ctx.report->unsupported["FL Studio Mobile: slide note without a held note (FLM plays nothing)"] += orphans;
    return (int) ctx.song.patterns.size() - 1;
}

void addPatternClips (Ctx& ctx, const flm::Clip& clip, int pattern, int row)
{
    const auto start = (double) clip.startTick / flm::ticksPerBeat;
    for (const auto& pass : passesOf (clip))
    {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = pattern;
        c.track = row;
        c.start = ticksOf (start + pass.at);
        c.length = std::max<std::int64_t> (1, ticksOf (pass.length));
        c.offset = ticksOf (pass.from);
        ctx.song.playlist.push_back (c);
    }
}

// FLM's DW Sampler playing a DirectWave program (.dwp: "DwPr" + DirectWave's own saved state, FLM_FORMAT §7) → LYNX's
// DirectWave engine (slice F3). The program is looked for in FL's copy of FLM's resources ("DirectWave Samples…") and
// next to the project; its zones' files ("%ILSharedData%\DirectWave\<program>\<file>.wav") in the program's folder.
bool directWaveProgram (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    const auto& o = ctx.options.common;
    if (! o.readFile || ! o.fileExists)
        return false;
    auto lower = m.presetFile;
    std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
    if (lower.size() < 4 || lower.compare (lower.size() - 4, 4, ".dwp") != 0)
        return false;
    std::vector<std::string> candidates = ctx.deviceCandidates (m.presetFile);
    if (! m.presetName.empty())
    {
        if (! ctx.options.resourceRoot.empty())
            for (const auto* folder : { "DirectWave Samples", "DirectWave Samples EXP" })
                candidates.push_back (joinPath (joinPath (ctx.options.resourceRoot, folder), m.presetName + ".dwp"));
        candidates.push_back (joinPath (o.projectFolder, m.presetName + ".dwp"));
    }
    candidates.push_back (m.presetFile);
    const auto program = ctx.resolve (candidates, c.name);
    if (program.empty())
        return false;
    const auto bytes = o.readFile (program);
    if (bytes.size() < 8 || std::memcmp (bytes.data(), "DwPr", 4) != 0)
        return false;
    const std::vector<std::uint8_t> state (bytes.begin() + 4, bytes.end());
    const auto slash = program.find_last_of ("/\\");
    const auto folder = slash == std::string::npos ? std::string() : program.substr (0, slash);
    const auto existing = [&] (const std::string& zonePath) -> std::string {
        auto p = zonePath;
        std::replace (p.begin(), p.end(), '\\', '/');
        const auto file = pathFileName (p);
        const auto parentEnd = p.find_last_of ('/');
        const auto parentStart = parentEnd == std::string::npos || parentEnd == 0 ? std::string::npos : p.find_last_of ('/', parentEnd - 1);
        std::vector<std::string> zoneCandidates;
        if (parentEnd != std::string::npos)
            zoneCandidates.push_back (joinPath (folder, p.substr (parentStart == std::string::npos ? 0 : parentStart + 1)));
        zoneCandidates.push_back (joinPath (folder, file));
        return ctx.resolve (zoneCandidates, c.name);
    };
    if (! dw::importChannel (c, state, existing, o.readFile, o.saveEmbedded))
        return false;
    c.keyLow = m.keyLow;
    c.keyHigh = m.keyHigh;
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: DW Sampler (DirectWave program)");
    return true;
}

// FLM's MiniSynth → LYNX's MiniSynth engine (F4). FLM's parameters 1…43 are the desktop plugin's 0…42, in the same
// order (the two parameter-name tables, FLM_FORMAT §7.1 and REA_rodada41 §1.2; PROVEN-REA setter 0x1806af050 and
// A/B inside FL, round 190n); FLM's 0 is the module's Enable. The
// state is written the way the desktop plugin saves it ("SSLF" + its .flms preset: signature, HEAD with the name, DATA
// "0rev" + 43 floats); the wave tables come from oscdata.dat, which FLM's resources and FL's MiniSynth folder share.
bool miniSynthState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    const auto& o = ctx.options.common;
    if (m.params.size() < 44 || ! o.fileExists)
        return false;
    std::vector<std::string> candidates;
    if (! ctx.options.resourceRoot.empty())
        candidates.push_back (joinPath (ctx.options.resourceRoot, "oscdata.dat"));
    if (! o.roots.factoryData.empty())
        candidates.push_back (joinPath (o.roots.factoryData, "Plugins/Fruity/Generators/Minisynth/oscdata.dat"));
    std::string oscdata;
    for (const auto& p : candidates)
        if (o.fileExists (p))
        {
            oscdata = p;
            break;
        }
    if (oscdata.empty())
        return false;
    std::vector<std::uint8_t> body { 0x99, 'F', 'L', 'S', 0x0D, 0x0A, 0x1A, 0x0A };
    const auto u16 = [&body] (std::uint16_t v) { body.push_back ((std::uint8_t) v); body.push_back ((std::uint8_t) (v >> 8)); };
    const auto u32 = [&body] (std::uint32_t v) { for (int k = 0; k < 4; ++k) body.push_back ((std::uint8_t) (v >> (8 * k))); };
    const std::string name = "FL Studio Mobile";
    body.insert (body.end(), { 'H', 'E', 'A', 'D' });
    u32 ((std::uint32_t) (6 + 2 * name.size()));
    u16 (1);
    u16 (1);
    u16 ((std::uint16_t) name.size());
    for (const auto ch : name)
        u16 ((std::uint16_t) (unsigned char) ch);
    body.insert (body.end(), { 'D', 'A', 'T', 'A' });
    u32 (4 + 43 * 4);
    body.insert (body.end(), { '0', 'r', 'e', 'v' });
    for (std::size_t i = 1; i <= 43; ++i)
    {
        std::uint32_t bits;
        auto v = std::clamp (m.params[i], 0.0f, 1.0f);
        // MW Dest has three steps in FLM (filter, LFO amount, OSC Mod) and two on the desktop (below 1 = filter,
        // 1 = LFO amount); OSC Mod has no desktop twin, so its wheel amount goes to 0 (spec-minisynth-diff §3.2)
        if (i == 4)
            v = v >= 0.5f && v < 1.0f ? 1.0f : 0.0f;
        if (i == 5 && m.params[4] >= 1.0f)
            v = 0.0f;
        std::memcpy (&bits, &v, 4);
        u32 (bits);
    }
    std::vector<std::uint8_t> state { 'S', 'S', 'L', 'F' };
    const auto size = (std::uint32_t) body.size();
    for (int k = 0; k < 4; ++k)
        state.push_back ((std::uint8_t) (size >> (8 * k)));
    state.insert (state.end(), body.begin(), body.end());
    if (! miniSynthStateUsable (state))
        return false;
    c.instrument = InstrumentKind::minisynth;
    c.modelParams = { 1.0f }; // FLM's build of the engine: velocity as it is, balance pan, 5 voices (core/MiniSynth.h)
    c.pluginState = std::move (state);
    c.samplePath = oscdata;
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: MiniSynth");
    return true;
}

// FLM's Transistor Bass → LYNX's Transistor Bass engine in FLM's build (F4, round 190u): the module is the desktop
// plugin's "V1 legacy" engine at 1× oversampling inside its own note layer, with 13 knobs. The state is a desktop one
// holding the equivalent patch (core/TransistorBass.h: transistorBassFlmState); the channel needs no file of an FL
// installation.
bool transistorBassState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    auto state = transistorBassFlmState (m.params);
    if (state.empty() || ! transistorBassStateUsable (state))
        return false;
    c.instrument = InstrumentKind::transistorbass;
    c.modelParams = { 1.0f }; // FLM's module around the engine (core/TransistorBass.h)
    c.pluginState = std::move (state);
    c.samplePath.clear();
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: Transistor Bass");
    return true;
}

// FLM's 3x Osc → LYNX's 3x Osc engine and FL's channel settings (F4, round 190u). FLM's module is the desktop
// plugin plus the channel's voice processor, echo delay, arpeggiator, polyphony and tracking as 148 parameters 0…1,
// and it carries its own converter from the desktop's raw values (REA spec of round 190n), so every parameter goes
// back to the raw value FL keeps and from there to the field the .flp import fills. The state is version 14
// (core/ThreeOsc.h: 21 parameters, invert × 3, AM, HQ, the 364-byte channel snapshot).
bool threeOscState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    if (m.params.size() < 146) // (146…148, Free Run, have no desktop twin)
        return false;
    const auto v = [&m] (std::size_t k) { return k < m.params.size() && std::isfinite (m.params[k]) ? std::clamp ((double) m.params[k], 0.0, 1.0) : 0.0; };
    const auto rnd = [] (double x) { return (std::int32_t) std::lround (x); };
    std::vector<std::uint8_t> state (460, 0);
    const auto put = [&state] (std::size_t at, std::int32_t x) { std::memcpy (state.data() + at, &x, 4); };
    put (0, 14);
    for (std::size_t k = 0; k < 3; ++k)
    {
        const auto at = 4 + 4 * 7 * k;
        put (at, rnd (128.0 * (v (19 + k) - 0.5)));      // pan
        put (at + 4, rnd (6.0 * v (1 + k)));             // shape
        put (at + 8, rnd (48.0 * (v (13 + k) - 0.5)));   // coarse
        put (at + 12, rnd (200.0 * (v (16 + k) - 0.5))); // fine
        put (at + 16, rnd (128.0 * (v (7 + k) - 0.5)));  // stereo phase
        put (at + 20, rnd (100.0 * (v (10 + k) - 0.5))); // stereo detune
        state[0x58 + k] = v (4 + k) > 0.5 ? 1 : 0;       // invert
    }
    put (4 + 4 * 6, rnd (128.0 * v (22)));  // mix 2
    put (4 + 4 * 13, rnd (128.0 * v (23))); // mix 3
    put (4 + 4 * 20, rnd (64.0 * v (26)));  // phase randomness
    state[0x5b] = v (24) > 0.5 ? 1 : 0;     // osc 3 AM
    state[0x5c] = v (25) > 0.5 ? 1 : 0;     // HQ

    // the channel snapshot: five records (pan, volume, Mod X = cutoff, Mod Y = resonance, pitch) of 17 values. FLM's
    // times are T(v) = round((e^(6.908755·v) − 1)·B) ticks, the desktop's law on raw / 65536; a raw below 101 is 0.
    // The module's ticks: 384 / X per beat, X = the song's steps per beat (TDIV's second byte, 4 in every file seen):
    // 96 per beat (PROVEN-REA: the byte the module reads is the one the song loader fills from TDIV; A/B inside FL,
    // round 190ab: the volume envelope of setting E1 fits 230-sample ticks = PPQ 96 at 120 BPM, not PPQ 128)
    const auto ppq = (int) std::lround (384.0 / (double) std::clamp (ctx.stepsPerBeat, 1, 32));
    const auto time = [&] (std::size_t k) { return std::clamp (rnd (65536.0 * v (k)), 0, 65536); };
    for (std::size_t j = 0; j < 5; ++j)
    {
        std::int32_t r[17] = { j == 1 ? 4 : 0, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, j == 1 ? -101 : 0 };
        // the LFO
        if (v (27 + j) > 0.5)
            r[0] |= 0x20; // global phase
        if (v (32 + j) > 0.5)
            r[0] |= 2;    // tempo
        r[13] = rnd (2.0 * v (37 + j));
        r[9] = time (42 + j);
        r[10] = time (47 + j);
        r[11] = rnd ((j == 0 ? 128.0 : j == 4 ? 4800.0 : 256.0) * (v (52 + j) - 0.5));
        r[12] = time (57 + j);
        if (j >= 1) // the envelope (FLM has none on the pan)
        {
            const auto e = j - 1;
            const auto tempo = v (66 + e) > 0.5;
            r[1] = v (62 + e) > 0.5 ? 1 : 0;
            if (tempo)
                r[0] |= 1;
            r[2] = time (70 + e);
            r[3] = time (74 + e);
            // an attack of no tick starts the envelope at its top: the desktop's attack raw 100
            if (std::lround ((std::exp (6.908755 * v (74 + e)) - 1.0) * 49.152 / (double) std::clamp (ctx.stepsPerBeat, 1, 32) * (tempo ? 1.0 : ctx.song.bpm / 120.0)) == 0)
                r[3] = 100;
            r[4] = time (82 + e);
            r[5] = time (86 + e);
            r[6] = rnd (128.0 * v (94 + e));
            r[7] = time (98 + e);
            // the amount in the record's unit, as the LFO's: Mod X / Mod Y 256, the pitch 4800 = ±2400 cents (A/B inside
            // FL, round 190ab, setting P1: 0.56 starts 288 cents up)
            r[8] = e == 0 ? 0 : rnd ((j == 4 ? 4800.0 : 256.0) * (v (106 + e) - 0.5));
            r[14] = rnd (256.0 * (v (78 + e) - 0.5));
            r[15] = rnd (256.0 * (v (90 + e) - 0.5));
            r[16] = rnd (256.0 * (v (102 + e) - 0.5));
        }
        std::memcpy (state.data() + 0x60 + 0x44 * j, r, sizeof (r));
    }
    // the filter: cutoff and resonance with FLM's offsets (2v − 1) folded in
    put (0x60 + 0x15c, std::clamp (rnd (256.0 * (v (111) + 2.0 * v (115) - 1.0)), 0, 256));
    put (0x60 + 0x160, std::clamp (rnd (256.0 * (v (112) + 2.0 * v (116) - 1.0)), 0, 256));
    put (0x60 + 0x168, std::clamp (rnd (7.0 * v (110)), 0, 7));
    if (! threeOscStateUsable (state))
        return false;

    c.instrument = InstrumentKind::threeosc;
    c.pluginState = std::move (state);
    c.samplePath.clear();
    // the level: linear in the velocity, FLM's Volume multiplier (×2v) and volume-envelope amount (×2(v − ½))
    const auto gain = 2.0 * v (114) * std::clamp (2.0 * (v (106) - 0.5), 0.0, 1.0);
    c.modelParams = { (float) ppq, 1.0f, (float) (threeOscFlmLevel * gain) };
    c.sampler.pitch = threeOscFlmPitch;
    c.pan = (float) std::clamp (2.0 * v (113) - 1.0, -1.0, 1.0);
    c.maxPolyphony = std::clamp (rnd (99.0 * v (117)), 0, 99);

    // FL's time knobs: n = round((27^(r / 1024) − 1)·48/26) steps of 1/192 beat
    const auto knobBeats = [] (int r) { return std::max (1.0, std::round ((std::pow (27.0, r / 1024.0) - 1.0) * 48.0 / 26.0)) / 192.0; };
    auto& misc = c.misc;
    misc.mono = v (120) > 0.5;
    misc.porta = c.synth.porta = c.sampler.porta = v (119) > 0.5;
    misc.portaBeats = c.synth.portaBeats = c.sampler.portaBeats = (float) knobBeats ((int) (1660.0 * v (118)));
    misc.gateBeats = v (129) >= 1.0 ? 0.0f : (float) knobBeats ((int) (997.0 * v (129) + 450.0));
    misc.key.mid = std::clamp (rnd (120.0 - 120.0 * v (121)), 0, 131);
    // (not clamped at ±1: A/B inside FL, round 190ab, setting V1 — 0.8 pans 2.4 / 48 a key, hard right 20 keys up)
    misc.key.pan = (float) (8.0 * (v (122) - 0.5));
    misc.key.modX = (float) (2.0 * (v (123) - 0.5));
    misc.key.modY = (float) (2.0 * (v (124) - 0.5));
    misc.velocity.mid = std::clamp (rnd (120.0 * v (125)), 0, 128);
    misc.velocity.pan = (float) std::clamp (8.0 * (v (126) - 0.5), -1.0, 1.0);
    misc.velocity.modX = (float) (2.0 * (v (127) - 0.5));
    misc.velocity.modY = (float) (2.0 * (v (128) - 0.5));
    misc.echo.echoes = std::clamp (rnd (9.0 * v (130) + 1.0), 1, 10);
    misc.echo.feedback = (float) (2.0 * v (131));
    misc.echo.timeBeats = (float) std::clamp (2.0 * v (132), 1.0 / 192.0, 16.0);
    // (A/B inside FL, round 190ab, setting G1: 0.6 pans each echo 0.8 further — twice the 4v − 2 of FLM's converter)
    misc.echo.pan = (float) std::clamp (8.0 * v (133) - 4.0, -1.0, 1.0);
    misc.echo.pitch = (float) (2400.0 * (v (134) - 0.5));
    misc.echo.modX = (float) (2.0 * v (135) - 1.0);
    misc.echo.modY = (float) (2.0 * v (136) - 1.0);
    misc.echo.pingPong = v (137) != 0.0;
    misc.echo.fat = v (138) != 0.0;

    static constexpr int directions[] = { 0, 1, 2, 3, 5, 4 }; // FL's 4 = sticky, 5 = random
    bool partial = false;
    if (const auto direction = std::clamp (rnd (5.0 * v (139)), 0, 5); direction > 0)
    {
        auto& a = c.arp;
        a.direction = directions[direction];
        a.range = std::clamp (rnd (6.0 * v (142)), 1, 4);
        a.stepBeats = (float) std::clamp (knobBeats ((int) (997.0 * v (140) + 450.0)), 1.0 / 192.0, 4.0);
        a.gate = (float) std::clamp (v (141), 0.05, 1.0);
        a.octaveRepeat = std::clamp (rnd (5.0 * v (143) + 1.0), 1, 6);
        a.slide = v (144) > 0.5;
        // FLM's chord list: "Auto", "Auto (sustain)", "(none)", then 88 chords (the default 1 / 90 is "Auto (sustain)")
        const auto chord = rnd (90.0 * v (145) - 2.0);
        a.chordSustain = chord != -2;
        if (chord == 0)
            a.chord = { 0 };
        partial = chord > 0 || v (140) >= 1.0 || rnd (6.0 * v (142)) == 0 || rnd (6.0 * v (142)) > 4; // a named chord, Hold, ranges LYNX lacks
    }
    for (std::size_t k = 0; k < 3; ++k)
        partial = partial || rnd (6.0 * v (1 + k)) == 6 || v (146 + k) > 0.5; // a custom shape (its file is not read), Free Run
    partial = partial || 2.0 * v (114) * threeOscFlmLevel > 4.0;
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: 3x Osc");
    if (partial)
        ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: 3x Osc (custom shape, Free Run, arpeggiator chord, Hold or range not converted)");
    return true;
}

// The channel playing a rack's first generator (F1: FLM's DW Sampler with one sample → a sampler, F3; any other
// generator → the built-in synth, listed in the report until its engine is mapped, F4).
ChannelDef instrumentChannel (Ctx& ctx, const flm::Rack& rack, const flm::Channel& ch, int insert)
{
    ChannelDef c;
    c.name = ch.name.empty() ? ch.typeName : ch.name;
    c.mixerTrack = insert;
    c.volume = 1.0f; // 0 dB: FLM's level is the rack's, on the channel's insert (F2)
    const auto gen = std::find_if (rack.modules.begin(), rack.modules.end(), [] (const flm::Module& m) { return flm::isGenerator (m.type); });
    if (gen == rack.modules.end())
    {
        ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: channel without an instrument");
        return c;
    }
    if (gen->type == flm::module::dwSampler && isAudioFile (gen->presetFile.empty() ? gen->presetName : gen->presetFile))
    {
        // a single sample (a .dwp multisample program is DirectWave's, F4): the module's file, saved on the device;
        // its relative name is relative to the user library, the .flm's folder in a share-zip
        const auto& folder = ctx.options.common.projectFolder;
        std::vector<std::string> candidates;
        if (! gen->presetName.empty() && isAudioFile (gen->presetName))
            candidates.push_back (joinPath (folder, gen->presetName));
        for (const auto& c : ctx.deviceCandidates (gen->presetFile))
            candidates.push_back (c);
        candidates.push_back (gen->presetFile);
        if (const auto name = pathFileName (gen->presetName.empty() ? gen->presetFile : gen->presetName); ! name.empty())
            candidates.push_back (joinPath (folder, name));
        c.instrument = InstrumentKind::sampler;
        c.samplePath = ctx.resolve (candidates, c.name);
        c.keyLow = gen->keyLow;
        c.keyHigh = gen->keyHigh;
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: DW Sampler (one sample)");
        return c;
    }
    if (gen->type == flm::module::dwSampler && directWaveProgram (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::miniSynth && miniSynthState (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::transistorBass && transistorBassState (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::threeOsc && threeOscState (ctx, *gen, c))
        return c;
    ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: " + moduleLabel (gen->type) + (gen->type == flm::module::dwSampler ? " (multisample)" : ""));
    return c;
}

int audioChannelFor (Ctx& ctx, const std::string& path, int insert, const std::string& name)
{
    const auto key = path + "|" + std::to_string (insert);
    if (const auto it = ctx.audioChannels.find (key); it != ctx.audioChannels.end())
        return it->second;
    ChannelDef c;
    c.name = name.empty() ? fileStem (path) : name;
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = path;
    c.volume = 10000.0f / 12800.0f; // a Playlist audio clip at unity (as the .als import, Instruments.cpp audioClipUnity)
    c.mixerTrack = insert;
    ctx.song.channels.push_back (std::move (c));
    const auto index = (int) ctx.song.channels.size() - 1;
    ctx.audioChannels[key] = index;
    return index;
}

void addAudioClip (Ctx& ctx, const flm::Clip& clip, int row, int insert, const std::string& user)
{
    if (! clip.sample)
        return;
    const auto& s = *clip.sample;
    const auto path = ctx.resolveSample (s, "Audio clip: " + user);
    if (path.empty())
        return;
    const auto length = clip.timelineLength();
    if (length <= 1e-9)
        return;
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = audioChannelFor (ctx, path, insert, fileStem (path));
    c.track = row;
    c.start = ticksOf ((double) clip.startTick / flm::ticksPerBeat);
    c.length = std::max<std::int64_t> (1, ticksOf (length));
    c.offset = std::max<std::int64_t> (0, ticksOf (clip.offset));
    if (std::abs (s.pitch) > 1e-9)
    {
        c.pitch = (float) std::clamp (s.pitch, -12.0, 12.0);
        if (std::abs (s.pitch) > 12.0)
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: audio clip pitch beyond ±12 semitones");
    }
    if (std::abs (s.stretch - 1.0) > 1e-6)
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: audio clip time-stretch (played unstretched)");
    ctx.song.playlist.push_back (c);
}
// An automation lane on the rack itself (module 0) that LYNX has a target for: Level, Pan and the send levels
// (FLM_FORMAT §3, the rack's parameter names PROVEN-REA 0x18059eb20). A module's parameters wait for their engines
// (F4/F5); Mute, Solo, Pitch Bend, Aftertouch, the MIDI CCs and the send switches have no LYNX automation target.
bool importableRackLane (const flm::Lane& lane) noexcept
{
    namespace rp = flm::rackparam;
    return lane.targetModule == 0 && ! lane.clips.empty()
           && (lane.targetParam == rp::level || lane.targetParam == rp::pan
               || (lane.targetParam >= rp::sendLevelBase && lane.targetParam < rp::sendLevelBase + 0x1000));
}

// What the report calls a lane that isn't imported
std::string skippedLaneLabel (const flm::Lane& lane)
{
    namespace rp = flm::rackparam;
    if (lane.targetModule != 0)
        return "FL Studio Mobile: automation of module parameters (not imported yet)";
    const auto p = lane.targetParam;
    if (p == rp::mute || p == rp::solo)
        return "FL Studio Mobile: Mute/Solo automation (LYNX automates neither)";
    if (p == rp::pitchBend || p == rp::aftertouch)
        return "FL Studio Mobile: Pitch Bend/Aftertouch automation (not imported)";
    if (p >= rp::firstMidiCc && p <= rp::lastMidiCc)
        return "FL Studio Mobile: MIDI CC automation (not imported)";
    if (p >= rp::sendEnableBase && p < rp::sendEnableBase + 0x1000)
        return "FL Studio Mobile: send on/off automation (not imported)";
    if (p >= rp::sendLevelBase && p < rp::sendLevelBase + 0x1000)
        return "FL Studio Mobile: send level automation without that route";
    return "FL Studio Mobile: automation of an unknown rack parameter";
}

// One importable rack lane (F6) as LYNX automation clips on Playlist row `rowIndex`. `target` is the clip's target;
// `fader` = the values go through FLM's fader law onto LYNX's fader position (Level, send levels), else they are
// direct (Pan). Each FLM clip becomes one clip, its points repeated over the clip's loop passes. FLM saves an
// automation clip as its drawn curve already sampled: a point every value step of 512 (1/128 of the range) or less,
// the user's control points among them, with two points on one tick for a jump (FLM_FORMAT §4, PROVEN-FILE over the
// 20 sample projects). Straight lines through those points are FLM's own curve to within that step, so the point
// shapes need no maths of their own.
void addAutomationClips (Ctx& ctx, const flm::Lane& lane, AutomationTarget target, const std::function<float (std::uint16_t)>& valueOf, int rowIndex,
                         const std::string& name)
{
    for (const auto& clip : lane.clips)
    {
        Automation a;
        a.name = name;
        a.target = target;
        for (const auto& pass : passesOf (clip))
            for (const auto& e : clip.events)
            {
                const auto beat = (double) e.tick / flm::ticksPerBeat;
                if (beat < pass.from - 1e-9 || beat > pass.from + pass.length + 1e-9)
                    continue;
                AutomationPoint pt;
                pt.tick = ticksOf (pass.at + beat - pass.from);
                pt.value = valueOf (e.value);
                if (! a.points.empty() && pt.tick < a.points.back().tick)
                    continue;
                a.points.push_back (pt);
            }
        if (a.points.empty())
            continue;
        ctx.song.automations.push_back (std::move (a));
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) ctx.song.automations.size() - 1;
        c.track = rowIndex;
        c.start = ticksOf ((double) clip.startTick / flm::ticksPerBeat);
        c.length = std::max<std::int64_t> (1, ticksOf (clip.timelineLength()));
        ctx.song.playlist.push_back (c);
    }
}
void addAutomationClips (Ctx& ctx, const flm::Lane& lane, AutomationTarget target, bool fader, int rowIndex, const std::string& name)
{
    addAutomationClips (ctx, lane, target, [fader] (std::uint16_t raw) {
        const auto v = (float) raw / 65535.0f;
        return fader ? faderPosition (std::min (flmGain (v), maxFaderGain)) : std::clamp (v, 0.0f, 1.0f);
    }, rowIndex, name);
}

// An effect module mapped onto LYNX slots (F5): where its slots are on the insert
struct MappedModule
{
    const flm::Module* module = nullptr;
    int insert = 0, firstSlot = 0, slotCount = 0;
};

// The automation lanes of an effect module's parameter (F5): one target per LYNX parameter the FLM parameter moves
// (an EQ band's knobs, every band of a steep Filter), each through FLM's own law (core/FlmEffects.h). The first target
// takes the lane's row, the others rows of their own right after it.
bool addEffectLane (Ctx& ctx, const flm::Lane& lane, const MappedModule& mm, int rowIndex)
{
    std::vector<std::uint16_t> raws;
    for (const auto& clip : lane.clips)
        for (const auto& e : clip.events)
            raws.push_back (e.value);
    std::sort (raws.begin(), raws.end());
    raws.erase (std::unique (raws.begin(), raws.end()), raws.end());
    if (raws.empty())
        return false;
    std::vector<float> values;
    values.reserve (raws.size());
    for (const auto r : raws)
        values.push_back ((float) r / 65535.0f);
    const auto targets = flmfx::automate (*mm.module, lane.targetParam, values);
    std::vector<const flmfx::LaneTarget*> kept;
    for (const auto& t : targets)
        if (t.slot < mm.slotCount)
            kept.push_back (&t);
    // (a knob that reshapes a whole curve, like Autoduck's Curve, would need a row per point: listed instead)
    if (kept.empty() || kept.size() > 9)
        return false;
    auto& song = ctx.song;
    const auto baseName = song.playlistTracks[(std::size_t) rowIndex].name;
    for (std::size_t k = 0; k < kept.size(); ++k)
    {
        const auto& t = *kept[k];
        auto row = rowIndex;
        if (k > 0)
        {
            PlaylistTrack extra;
            extra.name = baseName + " (" + std::to_string (k + 1) + ")";
            song.playlistTracks.insert (song.playlistTracks.begin() + rowIndex + (int) k, extra);
            for (auto& c : song.playlist) // rows after it move down
                if (c.track >= rowIndex + (int) k)
                    ++c.track;
            row = rowIndex + (int) k;
        }
        AutomationTarget target { t.kind, mm.insert, mm.firstSlot + t.slot, t.param };
        addAutomationClips (ctx, lane, target, [&raws, &t] (std::uint16_t raw) {
            const auto it = std::lower_bound (raws.begin(), raws.end(), raw);
            return t.values[(std::size_t) (it - raws.begin())];
        }, row, song.playlistTracks[(std::size_t) row].name);
    }
    return true;
}
} // namespace

std::string flmResourceRootIn (const std::string& flInstallFolder)
{
    if (flInstallFolder.empty())
        return {};
    return joinPath (flInstallFolder, "Plugins/Fruity/Generators/FL Studio Mobile/Installed");
}

bool isFlmData (const std::uint8_t* data, std::size_t size) { return flm::isFlmData (data, size); }

std::optional<Song> importFlm (const std::vector<std::uint8_t>& fileBytes, const FlmImportOptions& options, std::string* error)
{
    auto project = flm::parse ({ fileBytes.data(), fileBytes.size() }, error);
    if (! project)
        return std::nullopt;
    return importFlmProject (*project, options);
}

std::optional<Song> importFlmProject (const flm::Project& project, const FlmImportOptions& options)
{
    Ctx ctx (options);
    auto& song = ctx.song;
    song.channels.clear();
    song.patterns.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (0);
    song.mode = PlayMode::song;
    song.arrangements = { Arrangement { "Arrangement", {}, 0, 0, {} } };
    song.info.title = project.title;
    song.bpm = std::clamp (project.tempo > 0.0 ? project.tempo : 120.0, minTempo, maxTempo);
    // TDIV: beats per bar and the grid's steps per beat (FLM_FORMAT §TDIV, round 190ab: the second byte is not a note
    // value; LYNX's own grid stays at four steps per beat)
    ctx.stepsPerBeat = project.denominator;
    song.beatsPerBar = std::clamp (project.numerator, 1, 32);
    // FLM pans by balance — the rack's pan (RPRM [1]) and a drum pad's note pan (gain w·min(1, 2(1 − p)) left,
    // w·min(1, 2p) right): the louder side keeps its level, the other falls linearly. That is FL's triangular law
    song.panTriangular = true;

    // channels in FLM's display order (the master first), each with its rack
    std::vector<std::size_t> order (project.channels.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (std::size_t a, std::size_t b) {
        const auto& ca = project.channels[a];
        const auto& cb = project.channels[b];
        if ((ca.kind == flm::kindMaster) != (cb.kind == flm::kindMaster))
            return ca.kind == flm::kindMaster;
        return ca.order < cb.order;
    });

    static const flm::Rack noRack {};
    struct RackLane
    {
        const flm::Lane* lane;
        int insert, row;
    };
    std::vector<RackLane> rackLanes; // importable rack automation lanes (F6), filled after the routing
    std::map<int, int> insertOfId;                          // FLM channel id → insert
    std::vector<std::pair<int, const flm::Channel*>> routes; // insert, channel (its SEND chunks)
    for (const auto i : order)
    {
        const auto& ch = project.channels[i];
        const auto& rack = i < project.racks.size() ? project.racks[i] : noRack;
        const auto name = ch.name.empty() ? ch.typeName : ch.name;

        // the insert: FLM's master is LYNX's Master; every other channel (bus/AUX tracks too) gets one
        int insert = 0;
        if (ch.kind != flm::kindMaster && (int) song.mixer.size() < maxMixerTracks)
        {
            MixerTrack m;
            m.name = name;
            song.mixer.push_back (std::move (m));
            insert = (int) song.mixer.size() - 1;
        }
        // the rack's level and pan on it (F2: RPRM [0] volume with FLM's fader law, [1] pan 0…1 → −1…1)
        if (rack.params.size() >= 2)
        {
            auto& m = song.mixer[(std::size_t) insert];
            const auto gain = flmGain (rack.params[0]);
            if (gain > maxFaderGain * 1.0001f)
                ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: level above +5.6 dB (capped)");
            m.volume = faderPosition (std::min (gain, maxFaderGain));
            m.pan = std::clamp (rack.params[1] * 2.0f - 1.0f, -1.0f, 1.0f);
        }
        // [2] Mute and [3] Solo (the names PROVEN-REA; stored as switches, on = above the middle: INFERRED, every sample
        // has 0); [4] Pitch Bend: FLM's setter plays the rack's voices at a ratio of 2^(4v − 2), i.e. 48·v − 24
        // semitones (PROVEN-REA 0x18059e0c0), which a LYNX sampler takes on its pitch knob (−24…24); [5] Aftertouch
        // goes to the modules and isn't imported
        float bendSemitones = 0.0f;
        {
            namespace rp = flm::rackparam;
            const auto param = [&rack] (int i, float fallback) { return i < (int) rack.params.size() ? rack.params[(std::size_t) i] : fallback; };
            auto& m = song.mixer[(std::size_t) insert];
            if (param (rp::mute, 0.0f) >= 0.5f)
                m.muted = true;
            if (param (rp::solo, 0.0f) >= 0.5f && insert > 0)
                m.solo = true;
            bendSemitones = std::clamp (48.0f * param (rp::pitchBend, 0.5f) - 24.0f, -24.0f, 24.0f);
            if (std::abs (bendSemitones) < 0.01f)
                bendSemitones = 0.0f;
            if (param (rp::aftertouch, 0.0f) > 1e-3f)
                ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: channel aftertouch (not imported)");
        }
        if (ch.channelId >= 0)
            insertOfId[ch.channelId] = insert;
        routes.push_back ({ insert, &ch });

        // effects (F5): the mapped ones on the insert, in the rack's order (core/FlmEffects.h); the others are listed.
        // Generators past the first are not imported yet; analysis-only modules change nothing.
        std::map<std::uint32_t, MappedModule> mappedModules; // instance id → its slots
        bool firstGenerator = true;
        for (const auto& m : rack.modules)
        {
            if (flm::isGenerator (m.type))
            {
                if (! firstGenerator)
                    ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: " + moduleLabel (m.type) + " (second instrument in a rack)");
                firstGenerator = false;
                continue;
            }
            if (m.type == flm::module::tuner || m.type == flm::module::analyzer)
                continue;
            const auto live = std::any_of (ch.lanes.begin(), ch.lanes.end(), [&m] (const flm::Lane& l) {
                return l.kind() == flm::LaneKind::automation && l.targetModule == (int) m.instanceId && ! l.clips.empty();
            });
            auto mapped = flmfx::map (m, live);
            auto& effects = song.mixer[(std::size_t) insert].effects;
            if (mapped.slots.empty())
            {
                ctx.note (&FlpImportReport::droppedEffects, "FL Studio Mobile: " + moduleLabel (m.type));
                continue;
            }
            if (effects.size() + mapped.slots.size() > (std::size_t) maxEffectSlots)
            {
                ctx.note (&FlpImportReport::droppedEffects, "FL Studio Mobile: " + moduleLabel (m.type) + " (the insert's effect slots are full)");
                continue;
            }
            mappedModules[m.instanceId] = { &m, insert, (int) effects.size(), (int) mapped.slots.size() };
            for (auto& slot : mapped.slots)
                effects.push_back (std::move (slot));
            ctx.note (&FlpImportReport::converted, mapped.label);
            for (const auto& what : mapped.partial)
                ctx.note (&FlpImportReport::partialEffects, what);
        }

        // the rack's held pitch bend on a channel it creates
        const auto bend = [&] (ChannelDef& c) {
            if (bendSemitones == 0.0f)
                return;
            if (c.instrument == InstrumentKind::sampler)
            {
                c.sampler.pitch = std::clamp (c.sampler.pitch + bendSemitones, -24.0f, 24.0f);
                ctx.note (&FlpImportReport::converted, "FL Studio Mobile: channel pitch bend (on the sampler's pitch)");
            }
            else
                ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: channel pitch bend on an instrument other than a sampler (not imported)");
        };
        int instrument = -1;          // this channel's instrument (note lanes)
        std::vector<int> kitChannels; // this channel's drum kit (drum lanes)
        const auto instrumentIndex = [&] {
            if (instrument < 0)
            {
                song.channels.push_back (instrumentChannel (ctx, rack, ch, insert));
                instrument = (int) song.channels.size() - 1;
                bend (song.channels.back());
            }
            return instrument;
        };
        const auto kitIndex = [&] {
            if (kitChannels.empty())
            {
                for (std::size_t k = 0; k < rack.kit.size(); ++k)
                {
                    const auto& s = rack.kit[k];
                    ChannelDef c;
                    c.name = ! s.name.empty() ? s.name : fileStem (s.path.empty() ? s.savedPath : s.path);
                    if (c.name.empty())
                        c.name = name + " " + std::to_string (k + 1);
                    c.instrument = InstrumentKind::sampler;
                    c.samplePath = ctx.resolveSample (s, name + ": " + c.name);
                    c.mixerTrack = insert;
                    c.volume = 1.0f;
                    bend (c);
                    song.channels.push_back (std::move (c));
                    kitChannels.push_back ((int) song.channels.size() - 1);
                }
                if (! rack.kit.empty())
                    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: drum kit (one sampler channel per sample)");
            }
            return kitChannels;
        };

        // the channel's instruments exist even without clips (an empty track, a new-song template's kit)
        if (ch.kind == flm::kindNormal)
        {
            if (! rack.kit.empty())
                kitIndex();
            if (std::any_of (rack.modules.begin(), rack.modules.end(), [] (const flm::Module& m) { return flm::isGenerator (m.type); })
                || std::any_of (ch.lanes.begin(), ch.lanes.end(), [] (const flm::Lane& l) { return l.kind() == flm::LaneKind::notes && ! l.clips.empty(); }))
                instrumentIndex();
        }

        for (const auto& lane : ch.lanes)
        {
            const auto kind = lane.kind();
            if (kind == flm::LaneKind::automation)
            {
                if (lane.clips.empty())
                    continue;
                if (lane.targetModule > 0)
                {
                    // an effect module's parameter (F5): its LYNX slots exist now
                    const auto mm = mappedModules.find ((std::uint32_t) lane.targetModule);
                    if (mm == mappedModules.end())
                    {
                        ctx.note (&FlpImportReport::unsupported, skippedLaneLabel (lane));
                        continue;
                    }
                    PlaylistTrack row;
                    row.name = lane.name.empty() ? name : name + " " + lane.name;
                    song.playlistTracks.push_back (row);
                    if (addEffectLane (ctx, lane, mm->second, (int) song.playlistTracks.size() - 1))
                        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: effect automation");
                    else
                    {
                        song.playlistTracks.pop_back();
                        ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: automation of an effect switch or layout (not imported)");
                    }
                    continue;
                }
                if (! importableRackLane (lane))
                {
                    ctx.note (&FlpImportReport::unsupported, skippedLaneLabel (lane));
                    continue;
                }
                // its row now (FLM's lane order); its clips once every insert and route exists
                PlaylistTrack row;
                row.name = lane.name.empty() ? name : name + " " + lane.name;
                song.playlistTracks.push_back (row);
                rackLanes.push_back ({ &lane, insert, (int) song.playlistTracks.size() - 1 });
                continue;
            }
            if (kind == flm::LaneKind::other || lane.clips.empty())
                continue;

            PlaylistTrack row;
            row.name = lane.name.empty() ? name : lane.name;
            row.mixerTrack = insert;
            row.mode = kind == flm::LaneKind::audio ? PlaylistTrackMode::audio : PlaylistTrackMode::instrument;
            song.playlistTracks.push_back (row);
            const auto rowIndex = (int) song.playlistTracks.size() - 1;

            if (kind == flm::LaneKind::audio)
            {
                for (const auto& clip : lane.clips)
                    addAudioClip (ctx, clip, rowIndex, insert, row.name);
                continue;
            }

            // note / drum lanes: one pattern per content id, each clip's loop passes on the Playlist
            std::map<std::uint32_t, int> patternOf;
            int dropped = 0;
            for (const auto& clip : lane.clips)
            {
                auto it = patternOf.find (clip.contentId);
                if (it == patternOf.end())
                {
                    const auto pname = patternOf.empty() ? row.name : row.name + " (" + std::to_string (patternOf.size() + 1) + ")";
                    int pattern;
                    if (kind == flm::LaneKind::drums)
                    {
                        const auto kit = kitIndex();
                        pattern = addPattern (ctx, clip, pname, true, std::clamp (lane.swing, 0.0, 1.0), [&] (int key) -> std::pair<int, int> {
                            if (key < 0 || key >= (int) kit.size())
                            {
                                ++dropped;
                                return { -1, 0 };
                            }
                            return { kit[(std::size_t) key], 60 };
                        });
                    }
                    else
                    {
                        const auto channel = instrumentIndex();
                        pattern = addPattern (ctx, clip, pname, false, std::clamp (lane.swing, 0.0, 1.0),
                                              [channel] (int key) -> std::pair<int, int> { return { key < 0 ? -1 : channel, key }; });
                    }
                    it = patternOf.emplace (clip.contentId, pattern).first;
                }
                addPatternClips (ctx, clip, it->second, rowIndex);
            }
            if (kind == flm::LaneKind::drums)
            {
                song.playlistTracks[(std::size_t) rowIndex].channel = kitChannels.empty() ? -1 : kitChannels.front();
                if (dropped > 0)
                    ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: drum notes without a kit sample (dropped)");
            }
            else
                song.playlistTracks[(std::size_t) rowIndex].channel = instrument;
        }
    }
    // routing (F2): a SEND is (target channel id, …, linear gain capped at 1, …, …, level 0…1 on FLM's fader law)
    // [FLM_FORMAT §4]; the master's id is its route to the Master, another channel's a send to that insert. A channel
    // with SENDs but none to the master doesn't reach it directly; one without any keeps LYNX's default route.
    int masterId = -1;
    for (const auto& c : project.channels)
        if (c.kind == flm::kindMaster)
            masterId = c.channelId < 0 ? 0 : c.channelId;
    for (const auto& [insert, ch] : routes)
    {
        if (insert <= 0 || ch->sends.empty())
            continue;
        auto& m = song.mixer[(std::size_t) insert];
        bool toMaster = false;
        for (const auto& w : ch->sends)
        {
            if (w.empty())
                continue;
            const auto target = (int) w[0];
            float level = 0.853f;
            if (w.size() >= 6)
                std::memcpy (&level, &w[5], 4);
            const auto gain = std::min (flmGain (level), maxFaderGain);
            if (target == masterId)
            {
                toMaster = true;
                m.masterLevel = gain;
            }
            else if (const auto it = insertOfId.find (target); it != insertOfId.end() && it->second > 0 && it->second != insert)
                m.sends.push_back ({ it->second, gain });
        }
        m.toMaster = toMaster;
    }

    // the rack automation lanes (F6): Level and Pan on the channel's insert (the Master's for the master channel), a
    // send level on that route's level knob (its destination = the channel id after 0x2000)
    for (const auto& [lane, insert, row] : rackLanes)
    {
        namespace rp = flm::rackparam;
        const auto& rowName = song.playlistTracks[(std::size_t) row].name;
        const auto p = lane->targetParam;
        if (p == rp::level || p == rp::pan)
        {
            const auto kind = p == rp::level ? AutomationTargetKind::mixerVolume : AutomationTargetKind::mixerPan;
            addAutomationClips (ctx, *lane, AutomationTarget { kind, insert, 0, 0 }, p == rp::level, row, rowName);
            ctx.note (&FlpImportReport::converted, p == rp::level ? "FL Studio Mobile: Level automation" : "FL Studio Mobile: Pan automation");
            continue;
        }
        const auto destinationId = p - rp::sendLevelBase;
        int destination = -1;
        if (destinationId == masterId)
            destination = 0;
        else if (const auto it = insertOfId.find (destinationId); it != insertOfId.end() && it->second > 0)
            destination = it->second;
        if (insert <= 0 || destination < 0 || destination == insert || ! song.routes (insert, destination))
        {
            ctx.note (&FlpImportReport::unsupported, skippedLaneLabel (*lane));
            continue;
        }
        addAutomationClips (ctx, *lane, AutomationTarget { AutomationTargetKind::mixerSend, insert, destination, 0 }, true, row, rowName);
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: send level automation");
    }

    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    if (song.patterns.empty())
    {
        Pattern first;
        first.name = "Pattern 1";
        song.patterns.push_back (std::move (first));
    }
    song.currentPattern = 0;
    return std::move (ctx.song);
}
} // namespace lynx
