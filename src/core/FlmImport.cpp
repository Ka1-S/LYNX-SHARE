#include "core/FlmImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque> // round 190dw item 4
#include <functional>
#include <map>
#include <optional>
#include <set>

#include "core/FlmDwSampler.h"
#include "core/FlmSlicer.h"
#include "core/SoundLibrary.h"
#include "core/Effects.h" // round 190fn: makeEffectSlot (the Rack FX container)
#include "core/FlmEffects.h"
#include "core/FlmNoteFx.h"
#include "core/FlmPad.h"
#include "core/SoundFont.h"
#include "core/GmsMobile.h"
#include "core/MiniSynth.h"
#include "core/PocketSuperSaw.h"
#include "core/TransistorBass.h"
#include "core/ThreeOsc.h"
#include "core/MissingFiles.h"
#include "core/MidiControls.h"
#include "core/ModelParams.h"
#include "core/ScaleKeys.h"

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

std::string moduleLabel (std::uint32_t type)
{
    const auto* name = flm::moduleName (type);
    return name != nullptr ? std::string (name) : "Module #" + std::to_string (type);
}

// META's dates, "YYYY-MM-DD HH-MM-SS" as the device wrote them → seconds since 1970 (the file has no time zone: read as
// UTC), 0 when the text is not such a date (round 190fn)
double flmDateSeconds (const std::string& text)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf (text.c_str(), "%d-%d-%d %d-%d-%d", &y, &mo, &d, &h, &mi, &se) != 6 || y < 1970 || y > 9999 || mo < 1 || mo > 12 || d < 1
        || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60)
        return 0.0;
    // days from 1970-01-01 (the civil-calendar formula)
    const auto yy = (long long) y - (mo <= 2 ? 1 : 0);
    const auto era = (yy >= 0 ? yy : yy - 399) / 400;
    const auto yoe = yy - era * 400;
    const auto doy = (153LL * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const auto days = era * 146097 + doe - 719468;
    return (double) (days * 86400LL + h * 3600LL + mi * 60LL + se);
}

// "5.1.0" vs "5.0.4": whether `version` is newer (dot-separated numbers; text that is not a version is never newer)
bool flmVersionNewer (const std::string& version, const std::string& known)
{
    const auto parts = [] (const std::string& v) {
        std::vector<int> out;
        int n = -1;
        for (const auto ch : v)
        {
            if (ch >= '0' && ch <= '9')
                n = (n < 0 ? 0 : n) * 10 + (ch - '0');
            else if (ch == '.' && n >= 0)
                out.push_back (n), n = -1;
            else
                return std::vector<int> {};
            if (n > 100000)
                return std::vector<int> {};
        }
        if (n >= 0)
            out.push_back (n);
        return out;
    };
    const auto a = parts (version), b = parts (known);
    if (a.empty() || b.empty())
        return false;
    return std::lexicographical_compare (b.begin(), b.end(), a.begin(), a.end());
}

struct Ctx
{
    const FlmImportOptions& options;
    FlpImportReport* report;
    Song song;
    std::map<std::string, int> audioChannels; // resolved path | pitch | insert → audioClip channel
    std::set<std::string> missingSeen;
    int cutGroups = 0;    // cut-group ids handed out (drum pads, round 190bs)
    int stepsPerBeat = 4; // TDIV's second byte (the song's +0xa9): the grid's steps per beat, and 384 / it = the modules' tick rate

    explicit Ctx (const FlmImportOptions& o) : options (o), report (o.common.report)
    {
        // round 190fn (O1): the user's own resources folders first, then FL's copy
        for (const auto& r : o.userResourceRoots)
            if (! r.empty() && std::find (resourceRoots.begin(), resourceRoots.end(), r) == resourceRoots.end())
                resourceRoots.push_back (r);
        if (! o.resourceRoot.empty() && std::find (resourceRoots.begin(), resourceRoots.end(), o.resourceRoot) == resourceRoots.end())
            resourceRoots.push_back (o.resourceRoot);
    }

    std::vector<std::string> resourceRoots; // where "@R@" paths may live, in search order

    // "@R@" + rel in every resources folder, in search order
    std::vector<std::string> inResources (const std::string& rel) const
    {
        std::vector<std::string> out;
        for (const auto& r : resourceRoots)
            out.push_back (joinPath (r, rel));
        return out;
    }

    // the first resources folder that holds every one of `files` ("" = none)
    std::string resourceRootWith (std::initializer_list<std::string> files) const
    {
        const auto& exists = options.common.fileExists;
        if (! exists)
            return {};
        for (const auto& r : resourceRoots)
            if (std::all_of (files.begin(), files.end(), [&] (const std::string& f) { return exists (joinPath (r, f)); }))
                return r;
        return {};
    }

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
        if (const auto r = below ("/Resources/"); ! r.empty())
            for (const auto& c : inResources (r))
                out.push_back (c);
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
            {
                const auto in = inResources (s.path);
                if (in.empty())
                    candidates.push_back ({}); // (no resources folder: reported under the saved path)
                candidates.insert (candidates.end(), in.begin(), in.end());
            }
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
        return resolvedOrFlagged (resolve (candidates, user), s.root);
    }

    // round 190fn (O1): a factory file ("@R@") that was not found → the report says where to point LYNX at the app's
    // resources (once per import)
    bool resourceMissing = false;
    std::string resolvedOrFlagged (std::string found, const std::string& root)
    {
        if (found.empty() && root == "@R@")
            resourceMissing = true;
        return found;
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

// LYNX's channel volume for a linear gain up to 1: the inverse of channelGain, (11^v − 1) / 10
float channelVolumeFor (double gain) noexcept
{
    return (float) std::clamp (std::log (10.0 * std::max (0.0, gain) + 1.0) / std::log (11.0), 0.0, 1.0);
}

// A sample slot's parameter with the app's default for a file that stores fewer (FLM_FORMAT §6.1)
float slotParam (const flm::SampleRef& s, int index) noexcept
{
    return std::clamp (s.param (index, flm::samplepad::defaults[index]), 0.0f, 1.0f);
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
// Round 190bs: `fx` = the rack's note effects, run over a note lane's notes before `channelFor` (core/FlmNoteFx.h);
// `shifts` = a drum lane's time shift per kit sample, in beats (a note never starts before the clip does).
template <typename ChannelFor>
int addPattern (Ctx& ctx, const flm::Clip& clip, const std::string& name, bool drums, double swing, ChannelFor channelFor,
                flmnotefx::Chain* fx = nullptr, const std::vector<double>* shifts = nullptr)
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
    int repeats = 0, slides = 0, orphans = 0, pans = 0, pitches = 0, held_back = 0, shifted = 0;
    for (const auto& e : clip.events)
    {
        flmnotefx::Note played { e.key, velocityOf (e), e.pan == 127 ? 0.5f : (float) e.pan / 255.0f };
        const auto effects = fx != nullptr && ! drums && e.key >= 0 && ! fx->empty();
        if (effects && ! fx->apply (played))
        {
            ++held_back;
            continue;
        }
        const auto [channel, key] = channelFor (effects ? played.key : e.key);
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
        auto startBeat = (double) e.tick / flm::ticksPerBeat;
        if (drums && shifts != nullptr && e.key >= 0 && e.key < (int) shifts->size() && (*shifts)[(std::size_t) e.key] != 0.0)
        {
            startBeat = std::max (0.0, startBeat + (*shifts)[(std::size_t) e.key]);
            ++shifted;
        }
        n.start = ticksOf (startBeat);
        if (n.start >= p.lengthTicks)
            continue;
        n.length = std::clamp<std::int64_t> (ticksOf (e.length), 1, p.lengthTicks - n.start);
        n.velocity = effects ? played.velocity : velocityOf (e);
        n.pan = effects && played.pan != (e.pan == 127 ? 0.5f : (float) e.pan / 255.0f) ? std::clamp (2.0f * played.pan - 1.0f, -1.0f, 1.0f) : panOf (e);
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
            const auto startBeats = startBeat;
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
    count (shifted, "FL Studio Mobile: drum pad time shift");
    count (held_back, "FL Studio Mobile: notes a note effect holds back (silent in FLM too)");
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
    for (const auto& r : ctx.inResources ("oscdata.dat"))
        candidates.push_back (r);
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

// FLM's GMSynth → the "GMSynth (FLM)" engine (F4, round 190ab; core/GmsMobile.h): the module's 75 parameters as they
// are plus its CSTM chunk (the two waves, bend range, mod wheel). The engine reads FLM's own data files at run time —
// the waves' spectra and the filter table, in FL's copy of FLM's resources — so without them the channel stays on
// the built-in synth and is listed as not converted. Effect kinds the engine doesn't render yet are listed too.
bool gmSynthState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    namespace gm = gmsmobile;
    const auto& o = ctx.options.common;
    if (m.params.size() < (std::size_t) gm::paramCount || ! o.fileExists || ctx.resourceRoots.empty())
        return false;
    gm::Patch patch;
    for (int i = 0; i < gm::paramCount; ++i)
        patch.P[(std::size_t) i] = std::isfinite (m.params[(std::size_t) i]) ? m.params[(std::size_t) i] : 0.0f;
    if (m.custom.size() >= 20)
    {
        std::int32_t a, b;
        std::memcpy (&a, m.custom.data(), 4);
        std::memcpy (&b, m.custom.data() + 4, 4);
        std::memcpy (&patch.bendRange, m.custom.data() + 8, 4);
        std::memcpy (&patch.mwAmount, m.custom.data() + 12, 4);
        std::memcpy (&patch.mwDest, m.custom.data() + 16, 4);
        patch.waveA = a;
        patch.waveB = b;
    }
    const auto waveA = gm::waveFileName (patch.waveA), waveB = gm::waveFileName (patch.waveB);
    if (waveA.empty() || waveB.empty())
        return false;
    // (the engine reads all three files from one folder: the first resources folder that holds them)
    const auto root = ctx.resourceRootWith ({ "FilterTable_Ctf_Res.data", "GMSynthWaveshapes/" + waveA + ".spectra",
                                              "GMSynthWaveshapes/" + waveB + ".spectra" });
    if (root.empty())
        return false;
    auto state = gm::writeState (patch);
    if (! gm::readState (state))
        return false;
    c.instrument = InstrumentKind::gms;
    c.modelParams = { 1.0f }; // FL Studio Mobile's module, not the desktop GMS (core/GmsMobile.h)
    c.pluginState = std::move (state);
    c.samplePath = root;
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: GMSynth");
    for (int slot = 0; slot < 3; ++slot)
    {
        const auto base = (std::size_t) (gm::param::fxBase + gm::param::fxStride * slot);
        const auto kind = gm::fxKindOf (patch.P[base + gm::param::fxKind]);
        if (patch.P[base + gm::param::fxEnable] != 0.0f && ! gm::fx::implemented (kind))
            ctx.note (&FlpImportReport::partialEffects, std::string ("FL Studio Mobile: GMSynth effect ") + gm::fx::name (kind) + " (not rendered)");
    }
    return true;
}

// --- round 190by ---------------------------------------------------------------------------------------------------
// FLM's SuperSaw (module 23) → Pocket SuperSaw (core/PocketSuperSaw.h): the module's 60 parameters as they are (the
// engine reads FLM's own 0…1 values; FLM's parameter 0, Enable, becomes the channel's mute). No data file needed.
bool superSawState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    auto values = pocketSuperSawParams (m.params);
    if (values.empty())
        return false;
    c.instrument = InstrumentKind::pocketSuperSaw;
    c.modelParams = std::move (values);
    c.pluginState.clear();
    c.samplePath.clear();
    if (std::isfinite (m.params[0]) && m.params[0] < 0.5f)
        c.muted = true;
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: SuperSaw");
    return true;
}
// --- end of round 190by --------------------------------------------------------------------------------------------

// --- round 190de ---------------------------------------------------------------------------------------------------
// The module wrapper every module renders through, generators included (PROVEN-REA 0x180799750, FLM_FORMAT §7.4):
// Enable (parameter 0) below 0.5 or Mix 0 = the module is not rendered at all; else its output × the Post Gain
// 10^(2g − 1) (40g − 20 dB) is mixed into the rack's buffer at Mix with the Wet Pan's balance. On a generator channel:
// Enable off → muted (exact: FLM renders nothing), Post Gain × Mix → the channel's level (exact for the first
// generator of a rack without drum slots, whose dry buffer is silent; listed otherwise), Wet Pan → its pan.
// `enableAutomated` = an automation lane moves Enable: the channel then stays unmuted and the lane drives its volume.
// A generator's pitch-bend range: the semitones its bend input moves for the rack's full throw (FLM_FORMAT §7.4: the
// rack sends 4v − 2; DW Sampler halves it × its PB Range, PROVEN-REA 0x18077f560; SuperSaw halves it × int(11·P37 + 1),
// round 190ab; GMSynth halves it × its CSTM range {1, 2, 7, 12}, round 190u; MiniSynth adds it as semitones, PROVEN-REA
// 0x1806aea20). −1 = not traced.
float bendRangeOf (const flm::Module& m)
{
    switch (m.type)
    {
        case flm::module::dwSampler:
        {
            const auto c = flmdw::parseCustom (m.custom);
            return c ? (float) std::max (0, c->pbRange) : 4.0f;
        }
        case flm::module::miniSynth: return 2.0f;
        case flm::module::superSaw:
            return (float) std::clamp ((int) ((m.params.size() > 37 && std::isfinite (m.params[37]) ? m.params[37] : 0.1f) * 11.0f + 1.0f), 1, 12);
        case flm::module::gmSynth:
        {
            float r = 1.0f / 3.0f;
            if (m.custom.size() >= 12)
                std::memcpy (&r, m.custom.data() + 8, 4);
            static constexpr float ranges[] = { 1.0f, 2.0f, 7.0f, 12.0f };
            return std::isfinite (r) ? ranges[std::clamp ((int) std::lround (r * 3.0f), 0, 3)] : 2.0f;
        }
        // --- round 190dw item 4 (FLM_FORMAT §7.4): SoundFont Player's slot 75 (0x1807f0800) keeps half the rack's
        // 4v − 2 and its voices add it × 2 (a byte the constructor sets to 2, 0x1807ef331) semitones: ±2. Transistor
        // Bass keeps the base's empty slot 75 (0x1803debb0): it does not bend at all (0 = exact, nothing to play)
        case flm::module::soundFont: return 2.0f;
        case flm::module::transistorBass: return 0.0f;
        // --- end of round 190dw item 4
        default: return -1.0f;
    }
}

// The level the wrapper gives a generator: Post Gain × Mix
double generatorWrapperGain (const flm::Module& m) noexcept
{
    const auto r = [&m] (std::size_t i, float fallback) { return i < m.reserved.size() && std::isfinite (m.reserved[i]) ? std::clamp (m.reserved[i], 0.0f, 1.0f) : fallback; };
    return std::pow (10.0, 2.0 * (double) r (0, 0.5f) - 1.0) * (double) r (1, 1.0f);
}

void applyGeneratorWrapper (Ctx& ctx, const flm::Module& m, ChannelDef& c, bool enableAutomated, bool alone)
{
    const auto off = ! m.params.empty() && std::isfinite (m.params[0]) && m.params[0] < 0.5f;
    if (enableAutomated)
        c.muted = false; // (the Enable lane drives the channel's volume; an engine's own import may have muted it)
    else if (off)
    {
        c.muted = true;
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: switched-off instrument (muted)");
    }
    const auto r = [&m] (std::size_t i, float fallback) { return i < m.reserved.size() && std::isfinite (m.reserved[i]) ? std::clamp (m.reserved[i], 0.0f, 1.0f) : fallback; };
    const auto mix = r (1, 1.0f), wetPan = r (2, 0.5f);
    if (mix <= 0.0f)
    {
        c.muted = true;
        return;
    }
    const auto gain = generatorWrapperGain (m);
    if (std::abs (gain - 1.0) > 1.0e-4)
    {
        if (gain <= 1.0)
            c.volume = channelVolumeFor (gain * (double) channelGain (c.volume));
        else if (c.instrument == InstrumentKind::sampler)
            c.sampler.gainDb = (float) std::clamp ((double) c.sampler.gainDb + 20.0 * std::log10 (gain), -24.0, 24.0);
        else
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: instrument Post Gain above 0 dB (capped at 0 dB)");
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: instrument Post Gain / Mix");
        if (! alone) // (FLM's Post Gain and Mix also scale what the rack's buffer already holds: drum pads, earlier instruments)
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: instrument Post Gain / Mix in a rack with other sounds (the others are not changed)");
    }
    if (std::abs (wetPan - 0.5f) > 1.0e-4)
    {
        c.pan = std::clamp (c.pan + 2.0f * wetPan - 1.0f, -1.0f, 1.0f);
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: instrument Wet Pan");
    }
    if (enableAutomated && off)
        c.volume = 0.0f; // silent until the lane switches it on (generatorLaneTarget's "on" level is the wrapper's)
}

// An automation lane on a generator's parameter (round 190de, FLM_FORMAT §7.4): the LYNX target on channel `index`
// and the law from FLM's stored value (0…1) to the target's 0…1, or nullopt (`why` = the report's line). Every FLM
// engine LYNX has keeps FLM's own values, so the laws are the import's own:
// - Enable (0) on any generator: the channel's volume, on or silent (FLM stops rendering the module; LYNX's voices run
//   on silently: approximate in that a note held across the switch resumes where it would be, not where it stopped);
// - Pocket SuperSaw: FLM k → engine k − 1, as is; MiniSynth (FLM build): FLM k → desktop k − 1, as is (MW Dest with
//   the import's two-step fold); GMSynth (FLM): FLM k → the engine's P[k], as is; Transistor Bass: the 13 knobs through
//   transistorBassFlmState's map.
// --- round 190dw item 4: SoundFont Player's envelope knob k (1 Attack, 2 Decay, 3 Sustain, 4 Release) at value v as
// LYNX Soundfont's knob value — the laws of soundFontState below
float soundFontEnvelopeValue (int k, float v) noexcept
{
    v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;
    if (k == 3)
    {
        const auto n = std::clamp ((int) std::lround (127.0 * (double) v), 1, 127);
        return (float) std::max (1.0e-4, std::pow (10.0, -5.0 + 5.0 * std::pow (n / 127.0, 0.2)));
    }
    const auto t = 0.95 * (double) v + 0.05;
    const auto seconds = std::max (0.005, t * t * t * 10.0) * (k == 1 ? 1.0 : 2.5);
    return (float) std::clamp (std::log (seconds / 0.001) / std::log (20000.0), 1.0e-4, 1.0);
}
// --- end of round 190dw item 4
struct GeneratorLaneTarget
{
    AutomationTarget target;
    std::function<float (float)> value;
    bool approximate = false;
};
std::optional<GeneratorLaneTarget> generatorLaneTarget (const Song& song, int index, const flm::Module& m, int param, std::string& why)
{
    const auto& c = song.channels[(std::size_t) index];
    if (param == 0)
    {
        const auto on = channelVolumeFor (std::min (1.0, generatorWrapperGain (m)));
        return GeneratorLaneTarget { { AutomationTargetKind::channelVolume, index, 0, 0 }, [on] (float v) { return v >= 0.5f ? on : 0.0f; }, true };
    }
    const auto engine = [&] (int k, std::function<float (float)> law) -> std::optional<GeneratorLaneTarget> {
        const auto* p = modelparams::find (c.instrument, k);
        if (p == nullptr)
            return std::nullopt;
        return GeneratorLaneTarget { { AutomationTargetKind::channelParam, index, 0, k },
                                     [p, law] (float v) { return p->normalised (law (std::clamp (v, 0.0f, 1.0f))); } };
    };
    const auto same = [] (float v) { return v; };
    std::optional<GeneratorLaneTarget> out;
    switch (c.instrument)
    {
        case InstrumentKind::pocketSuperSaw:
            out = engine (param - 1, same);
            break;
        case InstrumentKind::minisynth:
            if (param == 4)
                out = engine (3, [] (float v) { return v >= 0.5f && v < 1.0f ? 1.0f : 0.0f; });
            else if (param >= 1 && param <= 43)
                out = engine (param - 1, same);
            break;
        case InstrumentKind::sampler:
            if (flmslicer::isChannel (c) && param >= 1 && param < flmslicer::paramCount) // FLM's Slicer: its own values
                out = GeneratorLaneTarget { { AutomationTargetKind::channelParam, index, 0, flmdw::automationBase + param },
                                            [] (float v) { return std::clamp (v, 0.0f, 1.0f); } };
            if (flmdw::isChannel (c) && param < flmdw::paramCount) // FLM's DW Sampler: its own values
                out = GeneratorLaneTarget { { AutomationTargetKind::channelParam, index, 0, flmdw::automationBase + param },
                                            [] (float v) { return std::clamp (v, 0.0f, 1.0f); } };
            break;
        case InstrumentKind::gms:
            if (c.modelParams.size() == 1 && c.modelParams[0] == 1.0f && param < gmsmobile::paramCount) // the FLM engine's own P[k]
                out = GeneratorLaneTarget { { AutomationTargetKind::channelParam, index, 0, param }, [] (float v) { return std::clamp (v, 0.0f, 1.0f); } };
            break;
        case InstrumentKind::transistorbass:
        {
            // FLM knob → the desktop patch value it was written to (transistorBassFlmState)
            static constexpr std::pair<int, int> map[] = { { 1, 1 }, { 2, 2 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 27 },
                                                           { 8, 28 }, { 9, 29 }, { 10, 31 }, { 11, 11 }, { 12, 12 }, { 13, 13 } };
            for (const auto& [k, j] : map)
                if (k == param)
                    out = engine (j, k == 9 ? std::function<float (float)> ([] (float v) { return std::sqrt (v); }) : same);
            break;
        }
        // --- round 190dw item 4
        case InstrumentKind::soundfont:
            // SoundFont Player's Attack / Decay / Sustain / Release (FLM_FORMAT §7.3) through the import's own laws onto
            // LYNX Soundfont's envelope knobs; they act only with Envelope = Custom, as in FLM (approximate: LYNX's
            // envelope shape, and FLM's voices may take a new value at another moment)
            if (m.type == flm::module::soundFont && param >= 1 && param <= 4 && m.params.size() > 5 && m.params[5] > 0.5f)
            {
                const int knob = param == 1 ? sf::pAttack : param == 2 ? sf::pDecay : param == 3 ? sf::pSustain : sf::pRelease;
                out = GeneratorLaneTarget { { AutomationTargetKind::channelParam, index, 0, knob },
                                            [param, knob] (float v) { return sf::normalise (knob, soundFontEnvelopeValue (param, v)); }, true };
            }
            break;
        case InstrumentKind::threeosc:
            // 3x Osc's Pan (113) is the channel's pan (threeOscState), plus the wrapper's Wet Pan
            if (m.type == flm::module::threeOsc && param == 113)
            {
                const auto wet = m.reserved.size() > 2 && std::isfinite (m.reserved[2]) ? std::clamp (m.reserved[2], 0.0f, 1.0f) : 0.5f;
                out = GeneratorLaneTarget { { AutomationTargetKind::channelPan, index, 0, 0 },
                                            [wet] (float v) { return std::clamp (std::clamp (v, 0.0f, 1.0f) + wet - 0.5f, 0.0f, 1.0f); } };
            }
            // --- round 190fe: every other 3x Osc parameter the engine moves while it plays (core/ThreeOsc.h, FLM's own
            // value through the import's laws); the cutoff / resonance and their offsets, the Volume and its envelope
            // amount fold the other half of their pair at its saved value
            else if (m.type == flm::module::threeOsc && threeOscIsFlm (c))
            {
                const auto saved = [&m] (int k, float fallback) {
                    return (std::size_t) k < m.params.size() && std::isfinite (m.params[(std::size_t) k]) ? std::clamp (m.params[(std::size_t) k], 0.0f, 1.0f) : fallback;
                };
                const auto target = [index] (int k, std::function<float (float)> law) {
                    return GeneratorLaneTarget { { AutomationTargetKind::channelParam, index, 0, threeosc::flmParamBase + k }, std::move (law) };
                };
                const auto unit = [] (float v) { return std::clamp (v, 0.0f, 1.0f); };
                if (param == 111 || param == 115)
                {
                    const auto other = saved (param == 111 ? 115 : 111, param == 111 ? 0.5f : 1.0f);
                    out = target (111, [param, other, unit] (float v) { v = unit (v); return unit (param == 111 ? v + 2.0f * other - 1.0f : other + 2.0f * v - 1.0f); });
                }
                else if (param == 112 || param == 116)
                {
                    const auto other = saved (param == 112 ? 116 : 112, param == 112 ? 0.5f : 0.0f);
                    out = target (112, [param, other, unit] (float v) { v = unit (v); return unit (param == 112 ? v + 2.0f * other - 1.0f : other + 2.0f * v - 1.0f); });
                }
                else if (param == 114 || param == 106)
                {
                    const auto other = saved (param == 114 ? 106 : 114, param == 114 ? 1.0f : 0.5f);
                    out = target (114, [param, other, unit] (float v) {
                        v = unit (v);
                        return param == 114 ? v * std::clamp (2.0f * other - 1.0f, 0.0f, 1.0f) : other * std::clamp (2.0f * v - 1.0f, 0.0f, 1.0f);
                    });
                }
                else if (threeosc::isFlmTarget (param))
                    out = target (param, unit);
            }
            // --- end of round 190fe
            break;
        // --- end of round 190dw item 4
        default: break;
    }
    if (! out)
        why = "FL Studio Mobile: automation of " + moduleLabel (m.type) + " parameter " + std::to_string (param) + " (not imported)";
    return out;
}
// --- end of round 190de --------------------------------------------------------------------------------------------

// --- round 190dw item 4: the file a module's CSTM "PATH" chunk names (u32 length + text, "@R@" = FLM's resources,
// "@L@" = the project's folder; the Slicer's and 3x Osc's layout), resolved like the song's samples; "" = no PATH
std::string flmCustomPath (Ctx& ctx, const flm::Module& m, const std::string& owner)
{
    const auto& b = m.custom;
    const auto u32 = [&b] (std::size_t at) {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k)
            v |= (std::uint32_t) b[at + (std::size_t) k] << (8 * k);
        return v;
    };
    std::string root, rel;
    for (std::size_t at = 0; at + 8 <= b.size();)
    {
        const auto size = (std::size_t) u32 (at + 4);
        if (at + 8 + size > b.size())
            break;
        if (std::memcmp (b.data() + at, "PATH", 4) == 0 && size >= 4)
        {
            const auto n = std::min<std::size_t> (u32 (at + 8), size - 4);
            std::string text ((const char*) b.data() + at + 12, n);
            if (text.size() >= 4 && text[0] == '@' && text[2] == '@' && text[3] == '\0')
                root = text.substr (0, 3), text = text.substr (4);
            if (const auto zero = text.find ('\0'); zero != std::string::npos)
                text.resize (zero);
            std::replace (text.begin(), text.end(), '\\', '/');
            rel = text;
        }
        at += 8 + size;
    }
    if (rel.empty())
        return {};
    std::vector<std::string> candidates;
    if (root == "@R@")
    {
        for (const auto& r : ctx.inResources (rel))
            candidates.push_back (r);
    }
    else if (root == "@L@")
        candidates.push_back (joinPath (ctx.options.common.projectFolder, rel));
    for (const auto& d : ctx.deviceCandidates (rel))
        candidates.push_back (d);
    if (root.empty())
        candidates.push_back (rel);
    candidates.push_back (joinPath (ctx.options.common.projectFolder, pathFileName (rel)));
    return ctx.resolvedOrFlagged (ctx.resolve (candidates, owner), root);
}
// --- end of round 190dw item 4

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
            // (round 190gw: FL's FLM rack draws this switch ON for a stored 0.5, live check 190gu B6, but the sound is
            // the envelope's OFF: with it on, the 50 % delay would hold every note of FL's all-0.5 test silent for about
            // four beats, and FL's render starts each note at once. So the switch plays from above 0.5, as REA 190n read)
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
    // --- round 190dw item 4: the custom shape (shape 6) plays the file the module's CSTM names (one "PATH" chunk: u32
    // length + text, written by 0x1807e8010 only when a file is set), as the desktop 3x Osc plays the channel's sample
    bool customShape = false;
    for (std::size_t k = 0; k < 3; ++k)
        customShape = customShape || rnd (6.0 * v (1 + k)) == 6;
    if (customShape)
        if (const auto file = flmCustomPath (ctx, m, c.name); ! file.empty())
        {
            c.samplePath = file;
            ctx.note (&FlpImportReport::converted, "FL Studio Mobile: 3x Osc custom shape");
        }
    // --- end of round 190dw item 4
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
    for (std::size_t k = 0; k < 3; ++k) // a custom shape without a file (round 190dw item 4: else it is read), Free Run
        partial = partial || (rnd (6.0 * v (1 + k)) == 6 && c.samplePath.empty()) || v (146 + k) > 0.5;
    partial = partial || 2.0 * v (114) * threeOscFlmLevel > 4.0;
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: 3x Osc");
    if (partial)
        ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: 3x Osc (custom shape, Free Run, arpeggiator chord, Hold or range not converted)");
    return true;
}

// FLM's SoundFont Player (type 52) → LYNX Soundfont (round 190bs; FLM_FORMAT §7.3, PROVEN-REA unless noted). The
// module's own state (CSTM) is one chunk "SF2D": the font's path (u32 length + text) and the chosen preset's index in
// the font (i32). Parameters: 1 Attack, 2 Decay, 3 Sustain, 4 Release, 5 Envelope (above 0.5 = Custom: the four knobs
// replace the font's envelope; else Original).
// Custom times: T = max(0.005, (0.95·v + 0.05)³ · 10) seconds. FLM's attack reaches the top after T; its decay and
// release are exponential and fall 40 dB in T. LYNX Soundfont's decay and release knobs are SoundFont times (100 dB),
// so they get 2.5·T: the same slope, an approximation where LYNX's envelope shape differs. Sustain: the gain
// 10^(−5 + 5·(n / 127)^0.2), n = round(127·v) kept in 1…127. FLM plays velocities linearly: the linear velocity curve.
bool soundFontState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    const auto& o = ctx.options.common;
    const auto& b = m.custom;
    const auto u32 = [&b] (std::size_t at) {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k)
            v |= (std::uint32_t) b[at + (std::size_t) k] << (8 * k);
        return v;
    };
    std::string saved;
    int patch = 0;
    if (b.size() >= 12 && std::memcmp (b.data(), "SF2D", 4) == 0)
    {
        const auto length = (std::size_t) u32 (8);
        if (length <= b.size() - 12)
        {
            saved.assign ((const char*) b.data() + 12, length);
            if (const auto zero = saved.find ('\0'); zero != std::string::npos)
                saved.resize (zero);
            if (12 + length + 4 <= b.size())
                patch = (int) u32 (12 + length);
        }
    }
    if (saved.empty())
        saved = m.presetFile;
    if (saved.empty())
        return false;
    std::vector<std::string> candidates = ctx.deviceCandidates (saved);
    const auto file = pathFileName (saved);
    for (const auto& r : ctx.resourceRoots)
    {
        candidates.push_back (joinPath (r, saved));
        candidates.push_back (joinPath (joinPath (r, "SoundFonts"), file));
    }
    candidates.push_back (joinPath (o.projectFolder, file));
    candidates.push_back (saved);
    const auto name = c.name;
    const auto font = ctx.resolve (candidates, name);
    // the preset: FLM stores its place in the font's preset list (the list's order = the file's: INFERRED)
    int bank = 0, program = 0;
    bool presetFound = patch == 0;
    if (! font.empty() && o.readFile)
        if (const auto info = sf::scanFont (o.readFile (font)); info.valid && patch >= 0 && patch < (int) info.presets.size())
        {
            bank = info.presets[(std::size_t) patch].bank;
            program = info.presets[(std::size_t) patch].program;
            presetFound = true;
        }
    const auto mixer = c.mixerTrack;
    c = sf::makeChannel (font, bank, program);
    c.name = name;
    c.mixerTrack = mixer;
    c.volume = 1.0f;
    const auto v = [&m] (std::size_t i, float fallback) { return i < m.params.size() ? std::clamp (m.params[i], 0.0f, 1.0f) : fallback; };
    c.modelParams[sf::pVelocityCurve] = 1.0f;
    if (v (5, 0.0f) > 0.5f)
    {
        const auto seconds = [&v] (std::size_t i, float fallback) {
            const auto t = 0.95 * (double) v (i, fallback) + 0.05;
            return std::max (0.005, t * t * t * 10.0);
        };
        const auto knob = [] (double s) { return (float) std::clamp (std::log (s / 0.001) / std::log (20000.0), 1.0e-4, 1.0); };
        c.modelParams[sf::pAttack] = knob (seconds (1, 0.0f));
        c.modelParams[sf::pDecay] = knob (2.5 * seconds (2, 0.0f));
        c.modelParams[sf::pRelease] = knob (2.5 * seconds (4, 0.0f));
        const auto n = std::clamp ((int) std::lround (127.0 * (double) v (3, 1.0f)), 1, 127);
        c.modelParams[sf::pSustain] = (float) std::max (1.0e-4, std::pow (10.0, -5.0 + 5.0 * std::pow (n / 127.0, 0.2)));
    }
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: SoundFont Player (LYNX Soundfont)");
    ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: SoundFont Player (level and envelope shape approximated)");
    if (! presetFound)
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: SoundFont Player preset not found in the font (first preset used)");
    return true;
}

// --- round 190de ---------------------------------------------------------------------------------------------------
// FLM's DW Sampler → FLM's own engine (core/FlmDwSampler.h, FLM_FORMAT §7.5) on a Sampler channel. Its zones come from
// the module's CSTM: ZONE records, or a PATh / PATH to a preset file — a DirectWave program (.dwp, read as FLM reads
// it: zones, key / velocity ranges, gain, loops, envelopes) or a single sample (one zone: root 48, every key, looping
// on the file's own loop if it has one) — and without CSTM from the module's preset file. A module with no zone is
// silent, as in FLM. The zones' files: "@R@" = FLM's resources (FL's copy, then its "… EXP" content-pack folders),
// "@L@" = the user's library (the project's folder in a share-zip); a program's zones next to the program.
// A stand-in for a DW Sampler whose every sample is missing on this PC (FLM's content packs are not in FL's copy of
// FLM; coordinator's decision, round 190de): the closest of LYNX's own sounds (core/SoundLibrary.h) in the category
// the program's name, folder and the channel's name give (a "Grand Piano" → keys, preferring a piano), on an engine
// that needs no file. A song never goes silent because a content pack is absent; the import report names the
// missing files and the stand-in. The module's Pitch and Fine Tune stay on the channel's pitch.
bool dwStandIn (Ctx& ctx, const flm::Module& m, ChannelDef& c, const std::string& hint, const std::string& shown)
{
    namespace sl = soundlib;
    auto words = hint + " " + c.name;
    std::transform (words.begin(), words.end(), words.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
    std::replace (words.begin(), words.end(), '/', ' ');
    std::replace (words.begin(), words.end(), '_', ' ');
    if (words.find ("sfx") != std::string::npos)
        words += " fx"; // ("Misc SFX", "Cops SFX": effects)
    const auto category = sl::categoryFor ({}, words);
    static constexpr InstrumentKind selfContained[] = { InstrumentKind::synth, InstrumentKind::harmless, InstrumentKind::sytrus, InstrumentKind::harmor,
                                                        InstrumentKind::ogun, InstrumentKind::dx10, InstrumentKind::poizone, InstrumentKind::simsynth,
                                                        InstrumentKind::plucked, InstrumentKind::sakura };
    const auto& list = sl::builtIn();
    const sl::Sound* best = nullptr;
    int bestScore = -1;
    for (const auto i : sl::filter (list, category))
    {
        const auto& sound = list[(std::size_t) i];
        if (std::find (std::begin (selfContained), std::end (selfContained), sound.kind) == std::end (selfContained))
            continue;
        auto name = sound.name;
        std::transform (name.begin(), name.end(), name.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
        int score = 0;
        // a word of the instrument in the sound's name ("piano", "strings", "bass"…), then a piano for keys
        for (std::size_t at = 0; at < words.size();)
        {
            const auto end = words.find (' ', at);
            const auto word = words.substr (at, end == std::string::npos ? std::string::npos : end - at);
            if (word.size() >= 4 && name.find (word) != std::string::npos)
                score += 10;
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
        if (category == sl::Category::keys && name.find ("piano") != std::string::npos)
            score += 5;
        if (score > bestScore)
            best = &sound, bestScore = score;
    }
    if (best == nullptr)
        return false;
    auto standIn = sl::withSound (c, *best);
    if (! standIn)
        return false;
    const auto keyLow = c.keyLow, keyHigh = c.keyHigh;
    c = std::move (*standIn);
    c.keyLow = keyLow, c.keyHigh = keyHigh;
    const auto v = [&m] (std::size_t k, float fallback) { return k < m.params.size() && std::isfinite (m.params[k]) ? m.params[k] : fallback; };
    c.sampler.pitch = std::clamp ((float) flmdw::pitchSemitones (v (3, 0.5f)) + 2.0f * v (4, 0.5f) - 1.0f, -24.0f, 24.0f);
    ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: DW Sampler \"" + (shown.empty() ? c.name : shown) + "\": every sample missing, plays LYNX's \""
                                                    + best->name + "\" as a stand-in until they are found (re-import then)");
    return true;
}

bool dwSamplerState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    const auto& o = ctx.options.common;
    auto custom = flmdw::parseCustom (m.custom);
    if (! custom)
    {
        if (m.presetFile.empty() && m.presetName.empty())
            return false;
        custom = flmdw::Custom {};
        custom->hasPath = true;
        custom->path = m.presetFile.empty() ? m.presetName : m.presetFile;
        std::replace (custom->path.begin(), custom->path.end(), '\\', '/');
    }
    auto values = m.params;
    values.resize ((std::size_t) flmdw::paramCount, -1.0f);
    for (int k = (int) m.params.size(); k < flmdw::paramCount; ++k)
        values[(std::size_t) k] = flmdw::defaults[k];
    const auto user = c.name;
    // a saved path → the candidates on this PC
    const auto candidatesOf = [&] (const std::string& root, const std::string& rel) {
        std::vector<std::string> out;
        if (root == "@R@")
        {
            for (const auto& res : ctx.resourceRoots)
            {
                out.push_back (joinPath (res, rel));
                // FLM's "migrated factory preset" folders: the content packs
                for (const auto* pack : { "DirectWave Samples/", "Drumaxx/", "Sliced Beats/" })
                    if (rel.rfind (pack, 0) == 0)
                        out.push_back (joinPath (res, std::string (pack).substr (0, std::strlen (pack) - 1) + " EXP/" + rel.substr (std::strlen (pack))));
            }
        }
        else if (root == "@L@")
            out.push_back (joinPath (o.projectFolder, rel));
        for (const auto& d : ctx.deviceCandidates (rel))
            out.push_back (d);
        if (root.empty())
            out.push_back (rel);
        if (const auto name = pathFileName (rel); ! name.empty())
            out.push_back (joinPath (o.projectFolder, name));
        return out;
    };
    std::vector<flmdw::ZoneRecord> zones;
    std::vector<std::string> paths; // each zone's file on this PC ("" = missing)
    if (! custom->hasPath)
    {
        zones = custom->zones;
        for (const auto& z : zones)
            paths.push_back (ctx.resolvedOrFlagged (ctx.resolve (candidatesOf (z.root, z.path), user), z.root));
    }
    else if (! custom->path.empty())
    {
        const auto file = ctx.resolvedOrFlagged (ctx.resolve (candidatesOf (custom->pathRoot, custom->path), user), custom->pathRoot);
        auto lower = custom->path;
        std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
        if (lower.size() > 4 && lower.compare (lower.size() - 4, 4, ".dwp") == 0)
        {
            if (! file.empty() && o.readFile)
            {
                const auto bytes = o.readFile (file);
                zones = flmdw::parseProgram ({ bytes.data(), bytes.size() });
                const auto folder = file.substr (0, std::max<std::size_t> (file.find_last_of ("/\\"), 0));
                for (const auto& z : zones)
                {
                    // "%ILSharedData%/DirectWave/<program>/<file>" → the program's folder + "<program>/<file>", or the file
                    const auto name = pathFileName (z.path);
                    std::vector<std::string> near;
                    if (const auto cut = z.path.find_last_of ('/'); cut != std::string::npos && cut > 0)
                        if (const auto parent = z.path.find_last_of ('/', cut - 1); parent != std::string::npos)
                            near.push_back (joinPath (folder, z.path.substr (parent + 1)));
                    near.push_back (joinPath (folder, name));
                    paths.push_back (ctx.resolve (near, user));
                }
                // FLM: Attack / Release = the last zone's envelope (a PATh's own values follow)
                if (! zones.empty())
                {
                    values[flmdw::param::attack] = std::clamp (zones.back().attack, 0.0f, 1.0f);
                    values[flmdw::param::release] = std::clamp (zones.back().release, 0.0f, 1.0f);
                }
            }
        }
        else if (! file.empty())
        {
            flmdw::ZoneRecord z; // a single sample: FLM's default zone
            z.path = file;
            z.loopMode = 2; // (it loops on the file's own loop, if it has one: the engine takes it from the decoded file)
            zones.push_back (z);
            paths.push_back (file);
        }
        if (custom->pathFloats)
        {
            values[flmdw::param::attack] = std::clamp (custom->pathAttack, 0.0f, 1.0f);
            values[flmdw::param::release] = std::clamp (custom->pathRelease, 0.0f, 1.0f);
        }
    }
    // every sample missing (or the program file): a stand-in; some missing: the others play, the report lists them
    const auto found = (int) std::count_if (paths.begin(), paths.end(), [] (const std::string& p) { return ! p.empty(); });
    const auto programMissing = zones.empty() && custom->hasPath && ! custom->path.empty();
    if ((! zones.empty() && found == 0) || programMissing)
    {
        // what the instrument is: the program's name, else the first zone's folder and file
        auto shown = custom->hasPath ? fileStem (custom->path) : std::string(), hint = shown;
        if (shown.empty() && ! zones.empty())
        {
            const auto& z = zones.front().path;
            const auto cut = z.find_last_of ('/');
            hint = cut == std::string::npos ? fileStem (z) : z.substr (0, cut);
            shown = hint;
            if (const auto program = hint.find_last_of ('/'); program != std::string::npos)
                shown = hint.substr (program + 1), hint = shown + " " + hint.substr (0, program);
        }
        c.keyLow = m.keyLow;
        c.keyHigh = m.keyHigh;
        c.volume = 1.0f;
        if (dwStandIn (ctx, m, c, hint, shown))
            return true;
    }
    if (found > 0 && found < (int) zones.size() && ctx.report != nullptr)
        ctx.report->partialEffects["FL Studio Mobile: DW Sampler with some samples missing (the others play; the missing ones are listed)"] += 1;
    const auto mixer = c.mixerTrack;
    const auto name = c.name;
    c = ChannelDef {};
    c.name = name;
    c.mixerTrack = mixer;
    c.instrument = InstrumentKind::sampler;
    c.volume = 1.0f; // (velocity is linear in FLM's voice; the level is the rack's, on the insert)
    std::vector<bool> decays;
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        c.zones.push_back (flmdw::sampleZone (zones[i], paths[i]));
        decays.push_back (zones[i].decay > 0.0f);
    }
    if (! c.zones.empty())
        c.samplePath = c.zones.front().path; // (what the Sampler page shows)
    c.keyLow = m.keyLow;
    c.keyHigh = m.keyHigh;
    c.modelParams = flmdw::channelParams (values, custom->pbRange, custom->smoothAttack, decays);
    ctx.note (&FlpImportReport::converted, zones.size() > 1 ? "FL Studio Mobile: DW Sampler (multisample, FLM's engine)" : "FL Studio Mobile: DW Sampler (FLM's engine)");
    if (zones.empty())
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: DW Sampler without a sample (silent, as in FLM)");
    return true;
}

// FLM's Slicer → FLM's own engine (core/FlmSlicer.h, FLM_FORMAT §7.7) on a Sampler channel: the CSTM's SENS (the
// detector's sensitivity; 0 = the file's markers) and PATH (the sample); slice 0 plays on the module's low key (FLM
// saves the key range it gave the slices)
bool slicerState (Ctx& ctx, const flm::Module& m, ChannelDef& c)
{
    const auto& b = m.custom;
    const auto u32 = [&b] (std::size_t at) {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k)
            v |= (std::uint32_t) b[at + (std::size_t) k] << (8 * k);
        return v;
    };
    float sensitivity = 0.25f;
    std::string root, rel;
    for (std::size_t at = 0; at + 8 <= b.size();)
    {
        const auto size = (std::size_t) u32 (at + 4);
        if (at + 8 + size > b.size())
            break;
        if (std::memcmp (b.data() + at, "SENS", 4) == 0 && size >= 4)
            std::memcpy (&sensitivity, b.data() + at + 8, 4);
        else if (std::memcmp (b.data() + at, "PATH", 4) == 0 && size >= 4)
        {
            const auto n = std::min<std::size_t> (u32 (at + 8), size - 4);
            std::string text ((const char*) b.data() + at + 12, n);
            if (text.size() >= 4 && text[0] == '@' && text[2] == '@' && text[3] == '\0')
                root = text.substr (0, 3), text = text.substr (4);
            if (const auto zero = text.find ('\0'); zero != std::string::npos)
                text.resize (zero);
            std::replace (text.begin(), text.end(), '\\', '/');
            rel = text;
        }
        at += 8 + size;
    }
    if (rel.empty())
        return false;
    std::vector<std::string> candidates;
    if (root == "@R@")
    {
        for (const auto& r : ctx.inResources (rel))
            candidates.push_back (r);
    }
    else if (root == "@L@")
        candidates.push_back (joinPath (ctx.options.common.projectFolder, rel));
    for (const auto& d : ctx.deviceCandidates (rel))
        candidates.push_back (d);
    if (root.empty())
        candidates.push_back (rel);
    candidates.push_back (joinPath (ctx.options.common.projectFolder, pathFileName (rel)));
    c.instrument = InstrumentKind::sampler;
    c.samplePath = ctx.resolvedOrFlagged (ctx.resolve (candidates, c.name), root);
    c.volume = 1.0f;
    c.modelParams = flmslicer::channelParams (m.params, std::isfinite (sensitivity) ? sensitivity : 0.25f, m.keyLow);
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: Slicer (FLM's engine)");
    return true;
}
// --- end of round 190de --------------------------------------------------------------------------------------------

// --- round 190de ---------------------------------------------------------------------------------------------------
// A rack with an Arpeggiator or a Note Echo (FLM_FORMAT §7.8): both act in song time — the arpeggiator's steps sit on
// the song's grid from beat 0, echoes run past their clip — so the rack's notes are taken from every note lane as the
// song plays them (each clip's loop passes), run through the rack's note effects in the rack's order (Transpose,
// Scale and Randomizer per note, Arpeggiator and Note Echo on the whole timeline) and written as one pattern that
// starts with the song. Returns the pattern (−1: no notes). Slide notes and note repeats are left out (listed).
int bakeTimeline (Ctx& ctx, const flm::Channel& ch, const std::vector<const flm::Module*>& modules, int channel, int keyLow, int keyHigh,
                  const std::string& name)
{
    std::vector<flmnotefx::TimedNote> notes;
    int skipped = 0;
    for (const auto& lane : ch.lanes)
    {
        if (lane.kind() != flm::LaneKind::notes)
            continue;
        std::map<std::uint32_t, const flm::Clip*> content; // (clips with one content id share the first one's events)
        for (const auto& clip : lane.clips)
        {
            const auto* source = content.emplace (clip.contentId, &clip).first->second;
            const auto start = (double) clip.startTick / flm::ticksPerBeat;
            for (const auto& pass : passesOf (clip))
                for (const auto& e : source->events)
                {
                    if (e.key < 0)
                        continue;
                    if (e.slide != 0 || e.repeat > 0)
                        ++skipped;
                    if (e.slide != 0)
                        continue;
                    const auto beat = (double) e.tick / flm::ticksPerBeat;
                    if (beat < pass.from - 1e-9 || beat >= pass.from + pass.length - 1e-9)
                        continue;
                    const auto at = start + pass.at + beat - pass.from;
                    const auto end = std::min (at + std::max (e.length, 1.0 / flm::ticksPerBeat), start + pass.at + pass.length);
                    notes.push_back ({ at, end, e.key, velocityOf (e), e.pan == 127 ? 0.5f : (float) e.pan / 255.0f });
                }
        }
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const flmnotefx::TimedNote& a, const flmnotefx::TimedNote& b) { return a.start < b.start; });
    if (notes.empty())
        return -1;
    // the note effects in the rack's order
    flmnotefx::Chain segment;
    bool random = false;
    const auto flush = [&] {
        if (segment.empty())
            return;
        random = random || segment.random();
        std::vector<flmnotefx::TimedNote> kept;
        for (auto n : notes)
        {
            flmnotefx::Note one { n.key, n.velocity, n.pan };
            if (! segment.apply (one))
                continue;
            n.key = one.key, n.velocity = one.velocity, n.pan = one.pan;
            kept.push_back (n);
        }
        notes = std::move (kept);
        segment = flmnotefx::Chain {};
    };
    for (const auto* m : modules)
    {
        if (flmnotefx::timeEffectActive (m->type, m->params))
        {
            flush();
            bool drawn = false;
            notes = m->type == flmnotefx::arpeggiator ? flmnotefx::arpeggiate (m->params, notes, ctx.song.bpm, ctx.song.beatsPerBar, drawn)
                                                       : flmnotefx::echo (m->params, notes, ctx.song.bpm, drawn);
            random = random || drawn;
        }
        else
            segment.add (m->type, m->params);
    }
    flush();
    Pattern p;
    p.name = name + " (note effects)";
    double last = 0.0;
    int outside = 0;
    for (const auto& n : notes)
    {
        if (n.key < keyLow || n.key > keyHigh)
        {
            ++outside;
            continue;
        }
        Note out;
        out.channel = channel;
        out.key = std::clamp (n.key, 0, 127);
        out.start = ticksOf (n.start);
        out.length = std::max<std::int64_t> (1, ticksOf (n.end - n.start));
        out.velocity = n.velocity;
        out.pan = std::clamp (2.0f * n.pan - 1.0f, -1.0f, 1.0f);
        p.notes.push_back (out);
        last = std::max (last, n.end);
    }
    const auto bar = (double) std::max (1, ctx.song.beatsPerBar);
    p.lengthTicks = std::max<std::int64_t> (ticksOf (bar), ticksOf (std::ceil (last / bar) * bar));
    p.sortNotes();
    ctx.song.patterns.push_back (std::move (p));
    ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: notes of a rack with an Arpeggiator or Note Echo (one pattern over the song)");
    if (random)
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: random note effect (drawn once at import; FLM draws at every playback)");
    if (skipped > 0 && ctx.report != nullptr)
        ctx.report->partialEffects["FL Studio Mobile: slide notes or note repeats in a rack with an Arpeggiator or Note Echo (not played)"] += skipped;
    if (outside > 0 && ctx.report != nullptr)
        ctx.report->converted["FL Studio Mobile: notes outside the instrument's key range (silent in FLM too)"] += outside;
    return (int) ctx.song.patterns.size() - 1;
}
// --- end of round 190de --------------------------------------------------------------------------------------------

// The channel playing one of a rack's generators (F1: FLM's DW Sampler with one sample → a sampler, F3; any other
// generator → its LYNX engine, or the built-in synth listed in the report until its engine is mapped, F4).
ChannelDef instrumentChannel (Ctx& ctx, const flm::Module* gen, const flm::Channel& ch, int insert)
{
    ChannelDef c;
    c.name = ch.name.empty() ? ch.typeName : ch.name;
    c.mixerTrack = insert;
    c.volume = 1.0f; // 0 dB: FLM's level is the rack's, on the channel's insert (F2)
    if (gen == nullptr)
    {
        ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: channel without an instrument");
        return c;
    }
    if (gen->type == flm::module::dwSampler && dwSamplerState (ctx, *gen, c)) // round 190de: FLM's own DW Sampler
        return c;
    if (gen->type == flm::module::miniSynth && miniSynthState (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::transistorBass && transistorBassState (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::threeOsc && threeOscState (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::gmSynth && gmSynthState (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::soundFont && soundFontState (ctx, *gen, c))
        return c;
    if (gen->type == flm::module::superSaw && superSawState (ctx, *gen, c)) // round 190by
        return c;
    if (gen->type == flm::module::slicer && slicerState (ctx, *gen, c)) // round 190de
        return c;
    ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: " + moduleLabel (gen->type) + (gen->type == flm::module::dwSampler ? " (multisample)" : ""));
    return c;
}

// The channel an audio clip plays on: one per file, insert and `variant` (the clip's Speed in semitones on the channel's
// pitch, which resamples as FLM's Speed does; a channel that keeps the pitch for stretched clips; a channel of its own
// for a clip with a level or pan envelope)
int audioChannelFor (Ctx& ctx, const std::string& path, int insert, const std::string& name, const std::string& variant = {},
                     float semitones = 0.0f, bool keepPitch = false)
{
    const auto key = path + "|" + std::to_string (insert) + variant;
    if (const auto it = ctx.audioChannels.find (key); it != ctx.audioChannels.end())
        return it->second;
    ChannelDef c;
    c.name = name.empty() ? fileStem (path) : name;
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = path;
    c.volume = audioClipUnityVolume; // a Playlist audio clip at unity (as the .als import, Instruments.cpp audioClipUnity)
    c.mixerTrack = insert;
    c.sampler.pitch = semitones;
    c.sampler.keepPitch = keepPitch;
    ctx.song.channels.push_back (std::move (c));
    const auto index = (int) ctx.song.channels.size() - 1;
    ctx.audioChannels[key] = index;
    return index;
}

void addAutomationClips (Ctx& ctx, const flm::Lane& lane, AutomationTarget target, const std::function<float (std::uint16_t)>& valueOf, int rowIndex,
                         const std::string& name);

// An audio clip (round 190bs: FLM_FORMAT §6.1, PROVEN-REA). The clip's sample is a slot like a drum pad's, with the
// same eleven parameters, and the clip's own events move them (|key| − 1 = the parameter, value u16 / 65534):
// - Level → the clip's gain; a Level that moves → the clip's gain is its top and LYNX automates the volume of a
//   channel the clip has to itself; Pan likewise;
// - Speed (whole semitones, a resampling) → the channel's pitch; the sample's own pitch (whole semitones, the length
//   stays) → the clip's pitch; its stretch (a length ratio, used only with a stretch mode other than 0) → the clip's
//   stretch on a channel that keeps the pitch;
// - Mute, reverse → the clip's;
// - a clip longer than its loop starts the sample again at every loop length: one LYNX clip per pass.
// `envelopeRow` = the Playlist row of this lane's clip envelopes (−1 until one is needed).
void addAudioClip (Ctx& ctx, const flm::Clip& clip, int row, int insert, const std::string& user, int& envelopeRow)
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
    namespace sp = flm::samplepad;
    const auto unit = [] (const flm::Event& e) { return std::min ((float) e.value / 65534.0f, 1.0f); };

    std::vector<flm::Event> levelEvents, panEvents;
    auto speed = slotParam (s, sp::speed);
    bool haveSpeed = false, speedMoves = false, others = false;
    for (const auto& e : clip.events)
    {
        const auto index = std::abs (e.key) - 1;
        if (index == sp::level)
            levelEvents.push_back (e);
        else if (index == sp::pan)
            panEvents.push_back (e);
        else if (index == sp::speed)
        {
            speedMoves = speedMoves || (haveSpeed && unit (e) != speed);
            if (! haveSpeed)
                speed = unit (e);
            haveSpeed = true;
        }
        else
            others = true;
    }
    double gain = flmpad::levelGain (slotParam (s, sp::level)), peak = 0.0;
    bool levelMoves = false;
    if (! levelEvents.empty())
    {
        for (const auto& e : levelEvents)
        {
            peak = std::max (peak, (double) flmpad::levelGain (unit (e)));
            levelMoves = levelMoves || unit (e) != unit (levelEvents.front());
        }
        gain = levelMoves ? peak : (double) flmpad::levelGain (unit (levelEvents.front()));
    }
    auto pan = std::clamp (2.0f * slotParam (s, sp::pan) - 1.0f, -1.0f, 1.0f);
    bool panMoves = false;
    if (! panEvents.empty())
    {
        for (const auto& e : panEvents)
            panMoves = panMoves || unit (e) != unit (panEvents.front());
        pan = panMoves ? 0.0f : std::clamp (2.0f * unit (panEvents.front()) - 1.0f, -1.0f, 1.0f);
    }
    const auto semitones = flmpad::speedSemitones (speed);
    const auto stretched = s.stretchMode != 0 && s.stretch > 0.0 && std::abs (s.stretch - 1.0) > 1e-6;
    auto variant = semitones != 0 ? "|p" + std::to_string (semitones) : std::string();
    if (stretched)
        variant += "|k";
    if (levelMoves || panMoves)
        variant += "|e" + std::to_string (ctx.song.channels.size());
    // round 190de: the slot's EG A/D/S/R, Length and Sample Start play as on a drum pad (the same slot object and
    // voice, FLM_FORMAT §6.1): FLM's pad envelope on a channel of their own (a clip ends its voice at its end, so no
    // release starts at the note's end: the "holds notes" flag stays off)
    std::vector<float> pad;
    if (slotParam (s, sp::egAttack) >= 0.001f || slotParam (s, sp::egSustain) < 1.0f || slotParam (s, sp::length) > 0.0f
        || std::abs (slotParam (s, sp::start) - 0.5f) > 1.0e-6f)
    {
        pad = { slotParam (s, sp::egAttack), slotParam (s, sp::egDecay), slotParam (s, sp::egSustain), slotParam (s, sp::egRelease),
                slotParam (s, sp::length), slotParam (s, sp::start), 0.0f };
        for (const auto v : pad)
            variant += "|" + std::to_string (v);
    }
    const auto channel = audioChannelFor (ctx, path, insert, fileStem (path), variant, (float) semitones, stretched);
    if (! pad.empty())
    {
        ctx.song.channels[(std::size_t) channel].sampler.flmPad = pad;
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: audio clip envelope, Length or Sample Start");
    }

    Clip c;
    c.kind = ClipKind::audio;
    c.channel = channel;
    c.track = row;
    c.gain = std::abs (gain - 1.0) < 1.0e-4 ? 1.0f : (float) gain;
    c.pan = pan;
    c.muted = slotParam (s, sp::mute) > 0.5f;
    c.reversed = s.reverse;
    if (const auto pitch = std::round (s.pitch); pitch != 0.0) // (FLM shifts by whole semitones)
    {
        c.pitch = (float) std::clamp (pitch, -12.0, 12.0);
        if (std::abs (pitch) > 12.0)
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: audio clip pitch beyond ±12 semitones");
    }
    if (stretched)
    {
        c.stretch = s.stretch;
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: audio clip time-stretch (LYNX's stretcher, approximated)");
    }
    const auto start = (double) clip.startTick / flm::ticksPerBeat;
    const auto passes = passesOf (clip);
    for (const auto& pass : passes)
    {
        auto one = c;
        one.start = ticksOf (start + pass.at);
        one.length = std::max<std::int64_t> (1, ticksOf (pass.length));
        one.offset = std::max<std::int64_t> (0, ticksOf (pass.from));
        ctx.song.playlist.push_back (one);
    }
    if (passes.size() > 1)
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: looped audio clip");

    if (c.gain != 1.0f || levelMoves)
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: audio clip level");
    if (c.pan != 0.0f || panMoves)
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: audio clip pan");
    if (semitones != 0)
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: audio clip speed");
    if (levelMoves || panMoves)
    {
        if (envelopeRow < 0)
        {
            PlaylistTrack t;
            t.name = user + " (clip envelope)";
            ctx.song.playlistTracks.push_back (t);
            envelopeRow = (int) ctx.song.playlistTracks.size() - 1;
        }
        flm::Lane lane;
        lane.clips.push_back (clip);
        lane.clips.front().sample.reset();
        const auto& rowName = ctx.song.playlistTracks[(std::size_t) envelopeRow].name;
        if (levelMoves)
        {
            lane.clips.front().events = levelEvents;
            const auto unity = (double) channelGain (audioClipUnityVolume);
            addAutomationClips (ctx, lane, AutomationTarget { AutomationTargetKind::channelVolume, channel, 0, 0 }, [peak, unity] (std::uint16_t raw) {
                const auto g = (double) flmpad::levelGain (std::min ((float) raw / 65534.0f, 1.0f));
                return channelVolumeFor (unity * g / std::max (peak, 1.0e-9));
            }, envelopeRow, rowName);
        }
        if (panMoves)
        {
            lane.clips.front().events = panEvents;
            addAutomationClips (ctx, lane, AutomationTarget { AutomationTargetKind::channelPan, channel, 0, 0 },
                                [] (std::uint16_t raw) { return std::min ((float) raw / 65534.0f, 1.0f); }, envelopeRow, rowName);
        }
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: audio clip envelope (automation)");
    }
    if (speedMoves)
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: audio clip Speed automation (the first value is used)");
    if (others)
        ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: audio clip automation of other sample settings (not imported)");
    if (slotParam (s, sp::solo) > 0.5f)
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: audio clip Solo (not imported)");
}

// A drum pad's own settings on its sampler channel (round 190bs; the laws: core/FlmPad.h, FLM_FORMAT §6.1). Level → the
// channel's volume (above 0 dB: the sampler's gain), Pan → its pan, Speed → the sampler's pitch, Mute / another pad's
// Solo → muted, reverse → reversed; EG A/D/S/R, Length and Sample Start → FLM's own pad envelope in the sampler
// (SamplerSettings::flmPad), kept only when one of them does something. `hold` = the kit's lane holds its notes.
void applyPad (Ctx& ctx, const flm::SampleRef& s, ChannelDef& c, bool soloElsewhere, bool hold, double bpm)
{
    namespace sp = flm::samplepad;
    bool changed = false;
    if (const auto gain = (double) flmpad::levelGain (slotParam (s, sp::level)); std::abs (gain - 1.0) >= 1.0e-4)
    {
        if (gain < 1.0)
            c.volume = channelVolumeFor (gain);
        else
            c.sampler.gainDb = (float) (20.0 * std::log10 (gain));
        changed = true;
    }
    if (const auto pan = std::clamp (2.0f * slotParam (s, sp::pan) - 1.0f, -1.0f, 1.0f); pan != 0.0f)
    {
        c.pan = pan;
        changed = true;
    }
    if (const auto semitones = flmpad::speedSemitones (slotParam (s, sp::speed)); semitones != 0)
    {
        c.sampler.pitch = (float) semitones;
        changed = true;
    }
    const auto solo = slotParam (s, sp::solo) > 0.5f;
    if (slotParam (s, sp::mute) > 0.5f || (soloElsewhere && ! solo))
    {
        c.muted = true;
        changed = true;
    }
    if (s.reverse)
    {
        c.sampler.reverse = true;
        changed = true;
    }
    const auto attack = slotParam (s, sp::egAttack), sustain = slotParam (s, sp::egSustain), length = slotParam (s, sp::length),
               start = slotParam (s, sp::start);
    if (attack >= 0.001f || sustain < 1.0f || length > 0.0f || std::abs (start - 0.5f) > 1.0e-6f || hold)
    {
        c.sampler.flmPad = { attack, slotParam (s, sp::egDecay), sustain, slotParam (s, sp::egRelease), length, start, hold ? 1.0f : 0.0f };
        changed = true;
    }
    if (changed)
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: drum pad settings (level, pan, speed, mute, envelope)");
    // (a pad's output link is applied once every channel's insert exists: round 190dw item 4, importFlmProject)
    // round 190de: the slot's stretch and pitch (STR1 / legacy STRC, FLM_FORMAT §6): FLM plays a copy of the file
    // ratio × as long (only with a stretch mode other than 0) shifted by whole semitones at the same length. LYNX: the
    // sampler's stretch to a fixed length with "keep pitch", whose pitch knob then shifts without changing the length;
    // the pad's Speed (a resampling) becomes the same shift plus a length of 2^(−st/12): equal in effect. LYNX's own
    // stretcher (FLM's modes are not named): approximate in sound, exact in length and pitch.
    const auto stretched = s.stretchMode != 0 && s.stretch > 0.0 && std::abs (s.stretch - 1.0) > 1e-6;
    const auto shift = std::round (s.pitch);
    if (stretched || std::abs (shift) >= 1.0)
    {
        const auto& o = ctx.options.common;
        const auto seconds = o.sampleSeconds && ! c.samplePath.empty() ? o.sampleSeconds (c.samplePath) : 0.0;
        if (seconds > 0.0 && bpm > 0.0)
        {
            const auto speed = (double) c.sampler.pitch; // (Speed, whole semitones)
            c.sampler.keepPitch = true;
            c.sampler.stretchBeats = (float) ((stretched ? s.stretch : 1.0) * seconds * std::exp2 (-speed / 12.0) * bpm / 60.0);
            c.sampler.pitch = (float) std::clamp (speed + shift, -24.0, 24.0);
            ctx.note (&FlpImportReport::converted, "FL Studio Mobile: drum pad time-stretch or pitch shift");
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: drum pad time-stretch (LYNX's stretcher; a note's own pitch shifts it instead of resampling)");
        }
        else
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: drum pad time-stretch or pitch shift (played as recorded)");
    }
}
// An automation lane on the rack itself (module 0) that LYNX has a target for: Level, Pan and the send levels
// (FLM_FORMAT §3, the rack's parameter names PROVEN-REA 0x18059eb20). A module's parameters wait for their engines
// (F4/F5); Mute, Solo, Pitch Bend, Aftertouch, the MIDI CCs and the send switches have no LYNX automation target.
// --- round 190dw item 4: the rack's setter (0x18059e0c0) gives CC #7 the Level's storage and law and CC #8 / #10 the
// Pan's: such a lane is that rack lane (−1 = no alias)
int rackCcAlias (int param) noexcept
{
    namespace rp = flm::rackparam;
    if (param == rp::firstMidiCc + 7)
        return rp::level;
    if (param == rp::firstMidiCc + 8 || param == rp::firstMidiCc + 10)
        return rp::pan;
    return -1;
}
// --- end of round 190dw item 4
bool importableRackLane (const flm::Lane& lane) noexcept
{
    namespace rp = flm::rackparam;
    return lane.targetModule == 0 && ! lane.clips.empty()
           && (lane.targetParam == rp::level || lane.targetParam == rp::pan
               || rackCcAlias (lane.targetParam) >= 0 // round 190dw item 4
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
    if (p == rp::firstMidiCc + 64) // round 190dw item 4 (the rack releases sustained notes when it falls from above ½)
        return "FL Studio Mobile: sustain pedal automation (not imported)";
    if (p >= rp::firstMidiCc && p <= rp::lastMidiCc) // round 190dw item 4: other CCs only move controller-linked parameters
        return "FL Studio Mobile: MIDI CC automation without a linked parameter (no effect in FL Studio Mobile)";
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
    // round 190fn: an Enable lane drives the slots' mix, so the slots themselves stay switched on
    if (lane.targetParam == 0)
        for (int k = 0; k < mm.slotCount; ++k)
            if (const auto at = (std::size_t) (mm.firstSlot + k); mm.insert >= 0 && mm.insert < (int) song.mixer.size() && at < song.mixer[(std::size_t) mm.insert].effects.size())
                song.mixer[(std::size_t) mm.insert].effects[at].enabled = true;
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

// --- round 190de ---------------------------------------------------------------------------------------------------
// One lane as automation of several channels (a Layer's children, a kit): the first on a new Playlist row named after
// the lane, the others on rows of their own right after it. `targetOf(k)` gives channel k's target and law. At most
// nine channels (the effect lanes' cap), else nothing is added.
bool addLaneOnChannels (Ctx& ctx, const flm::Lane& lane, const std::vector<int>& channels, const std::string& rackName,
                        const std::function<std::optional<GeneratorLaneTarget> (int)>& targetOf, bool& approximate)
{
    std::vector<GeneratorLaneTarget> targets;
    for (const auto k : channels)
        if (auto t = targetOf (k))
            targets.push_back (std::move (*t));
    if (targets.empty() || targets.size() > 9)
        return false;
    auto& song = ctx.song;
    const auto baseName = lane.name.empty() ? rackName : rackName + " " + lane.name;
    for (std::size_t k = 0; k < targets.size(); ++k)
    {
        PlaylistTrack row;
        row.name = k == 0 ? baseName : baseName + " (" + std::to_string (k + 1) + ")";
        song.playlistTracks.push_back (row);
        const auto rowIndex = (int) song.playlistTracks.size() - 1;
        const auto law = targets[k].value;
        approximate = approximate || targets[k].approximate;
        addAutomationClips (ctx, lane, targets[k].target, [law] (std::uint16_t raw) { return law ((float) raw / 65535.0f); }, rowIndex,
                            song.playlistTracks[(std::size_t) rowIndex].name);
    }
    return true;
}

// A lane on a generator's parameter (FLM_FORMAT §7.4), on the channel(s) playing that module
bool addGeneratorLane (Ctx& ctx, const flm::Lane& lane, const flm::Module& module, const std::vector<int>& channels, const std::string& rackName,
                       std::string& why)
{
    bool approximate = false;
    if (! addLaneOnChannels (ctx, lane, channels, rackName, [&] (int k) { return generatorLaneTarget (ctx.song, k, module, lane.targetParam, why); }, approximate))
        return false;
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: instrument parameter automation");
    if (approximate)
        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: instrument Enable automation (the channel's volume on or off)");
    return true;
}

// The rack's Pitch Bend lane (FLM_FORMAT §7.4, PROVEN-REA 0x18059e0c0: the rack hands 4v − 2 to every module's bend
// input and plays its drum slots at 2^(4v − 2)) on every channel the rack plays whose bend range is known: LYNX's
// pitch-bend automation, value v as it is, which LYNX plays as (2v − 1) × the channel's bend range — the range the
// import set to the module's own (bendRangeOf; 24 semitones on drum slots). The voices follow the bend as LYNX's
// engines follow the channel's pitch.
bool addBendLane (Ctx& ctx, const flm::Lane& lane, const std::vector<int>& channels, const std::string& rackName, std::string& why)
{
    bool approximate = false;
    const auto ok = addLaneOnChannels (ctx, lane, channels, rackName, [] (int k) -> std::optional<GeneratorLaneTarget> {
        return GeneratorLaneTarget { { AutomationTargetKind::channelParam, k, 0, midictl::pitchBendBase }, [] (float v) { return std::clamp (v, 0.0f, 1.0f); } };
    }, approximate);
    if (! ok)
    {
        why = channels.empty() ? std::string() : "FL Studio Mobile: Pitch Bend automation on a rack of more than nine sounds (not imported)";
        return false;
    }
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: Pitch Bend automation");
    return true;
}
// --- end of round 190de --------------------------------------------------------------------------------------------

// --- round 190dw item 4 ----------------------------------------------------------------------------------------------
// The mod wheel (FLM_FORMAT §7.4): the rack hands a CC #1 value as it is to every module's slot 74 (0x18059e0c0). Only
// MiniSynth (0x1806aea00), GMSynth (0x18064db80) and SuperSaw (0x1807f8d20) keep their own; every other module has the
// base's empty one. LYNX: the channel's CC #1 parameter on those engines' FLM builds (core/MiniSynth, GmsMobile,
// PocketSuperSaw take it as the module's raw wheel).
bool takesFlmModWheel (const ChannelDef& c) noexcept
{
    return (c.instrument == InstrumentKind::gms && gmsIsFlm (c)) || c.instrument == InstrumentKind::pocketSuperSaw
           || (c.instrument == InstrumentKind::minisynth && miniSynthIsFlm (c));
}
bool addWheelLane (Ctx& ctx, const flm::Lane& lane, const std::vector<int>& channels, const std::string& rackName, std::string& why)
{
    if (channels.empty())
        return false; // (no instrument of the rack reads the wheel: nothing to play)
    bool approximate = false;
    const auto ok = addLaneOnChannels (ctx, lane, channels, rackName, [] (int k) -> std::optional<GeneratorLaneTarget> {
        return GeneratorLaneTarget { { AutomationTargetKind::channelParam, k, 0, midictl::ccBase + 1 }, [] (float v) { return std::clamp (v, 0.0f, 1.0f); } };
    }, approximate);
    if (! ok)
    {
        why = "FL Studio Mobile: mod-wheel automation on a rack of more than nine sounds (not imported)";
        return false;
    }
    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: mod-wheel automation");
    return true;
}
// true when the rack's CCLK links CC `cc` to a module parameter
bool rackLinksCc (const flm::Rack& rack, int cc) noexcept
{
    return std::any_of (rack.ccLinks.begin(), rack.ccLinks.end(), [cc] (const flm::Rack::CcLink& l) { return l.cc == cc && l.module > 0; });
}
// --- end of round 190dw item 4 ---------------------------------------------------------------------------------------
} // namespace

std::string flmResourceRootIn (const std::string& flInstallFolder)
{
    if (flInstallFolder.empty())
        return {};
    return joinPath (flInstallFolder, "Plugins/Fruity/Generators/FL Studio Mobile/Installed");
}

bool looksLikeFlmResources (const std::string& folder, const std::function<bool (const std::string&)>& exists)
{
    if (folder.empty() || ! exists)
        return false;
    // the app's own resource folders and data files (the same names in FL's copy and in the Android app's pack,
    // round 190bn's index of it): any one of them marks the folder
    for (const auto* marker : { "Drum Samples", "DirectWave Samples", "Synth Presets2", "Loop Samples", "Drum Templates",
                                "GMSynthWaveshapes", "3xOsc Shapes", "oscdata.dat", "FilterTable_Ctf_Res.data" })
        if (exists (joinPath (folder, marker)))
            return true;
    return false;
}

std::string flmResourceRootFrom (const std::string& picked, const std::function<bool (const std::string&)>& exists)
{
    if (picked.empty() || looksLikeFlmResources (picked, exists))
        return picked;
    for (const auto* below : { "Resources", "Installed", "Plugins/Fruity/Generators/FL Studio Mobile/Installed" })
        if (const auto inside = joinPath (picked, below); looksLikeFlmResources (inside, exists))
            return inside;
    return picked;
}

bool isFlmData (const std::uint8_t* data, std::size_t size) { return flm::isFlmData (data, size); }

std::optional<Song> importFlm (const std::vector<std::uint8_t>& fileBytes, const FlmImportOptions& options, std::string* error)
{
    auto project = flm::parse ({ fileBytes.data(), fileBytes.size() }, error);
    if (! project)
        return std::nullopt;
    return importFlmProject (*project, options);
}

namespace
{
// "SSLF" + u32 size + an FLM project body ("10LF" + chunks): the FL Studio Mobile Rack plugins' state (FLM_FORMAT §8;
// the layout of FL's own "FL Studio Mobile Rack.fst" / "FL Studio Mobile Rack FX.fst" and their presets)
std::optional<flm::Rack> rackOfPluginState (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 16 || std::memcmp (state.data(), "SSLF", 4) != 0)
        return std::nullopt;
    std::uint32_t size = 0;
    std::memcpy (&size, state.data() + 4, 4);
    const auto body = std::min<std::size_t> (size, state.size() - 8);
    auto project = flm::parse ({ state.data() + 8, body }, nullptr, true);
    if (! project || project->racks.size() != 1)
        return std::nullopt;
    return project->racks.front();
}
} // namespace

std::optional<EffectSlot> flmRackFxSlot (const std::vector<std::uint8_t>& state, FlpImportReport* report)
{
    const auto note = [report] (std::map<std::string, int> FlpImportReport::* list, const std::string& what) {
        if (report != nullptr)
            ++(report->*list)[what];
    };
    const auto rack = rackOfPluginState (state);
    if (! rack)
    {
        note (&FlpImportReport::droppedEffects, "FL Studio Mobile Rack FX (state not readable)");
        return std::nullopt;
    }
    std::vector<EffectSlot> chain;
    std::vector<std::pair<std::string, std::string>> lines; // (converted, partial…) noted only when the slot is kept
    for (const auto& m : rack->modules)
    {
        if (m.type == flm::module::tuner || m.type == flm::module::analyzer)
            continue; // (sound-neutral)
        if (flm::isGenerator (m.type) || flm::moduleCategory (m.type) == flm::ModuleCategory::noteEffect)
        {
            note (&FlpImportReport::droppedEffects, "FL Studio Mobile Rack FX: " + moduleLabel (m.type) + " (an effect rack plays no notes)");
            continue;
        }
        auto mapped = flmfx::map (m);
        if (mapped.slots.empty())
        {
            note (&FlpImportReport::droppedEffects, "FL Studio Mobile: " + moduleLabel (m.type));
            continue;
        }
        if (chain.size() + mapped.slots.size() > (std::size_t) maxEffectSlots)
        {
            note (&FlpImportReport::droppedEffects, "FL Studio Mobile: " + moduleLabel (m.type) + " (the rack holds more than LYNX's slots)");
            continue;
        }
        for (auto& slot : mapped.slots)
            chain.push_back (std::move (slot));
        note (&FlpImportReport::converted, mapped.label);
        for (const auto& what : mapped.partial)
            note (&FlpImportReport::partialEffects, what);
    }
    const auto param = [&rack] (int i, float fallback) {
        return i < (int) rack->params.size() && std::isfinite (rack->params[(std::size_t) i]) ? rack->params[(std::size_t) i] : fallback;
    };
    namespace rp = flm::rackparam;
    // the rack's level (FLM's fader law, RPRM [0]) and its pan (a balance, [1]) after the modules, as on a .flm's insert
    const auto gain = flmGain (param (rp::level, 0.853124f));
    const auto pan = std::clamp (param (rp::pan, 0.5f) * 2.0f - 1.0f, -1.0f, 1.0f);
    if (chain.empty() && std::abs (gain - 1.0f) < 1.0e-3f && std::abs (pan) < 1.0e-4f)
    {
        note (&FlpImportReport::converted, "FL Studio Mobile Rack FX (empty)");
        return std::nullopt; // (nothing to do: the slot passes the sound)
    }
    if (std::abs (pan) > 1.0e-4f)
    {
        auto utility = makeEffectSlot (EffectKind::utility);
        utility.params[0] = 0.5f; // 0 dB
        utility.params[1] = std::clamp (0.5f + 0.5f * pan, 0.0f, 1.0f); // (the utility's balance is FLM's: p > 0 lowers the left by 1 − p)
        chain.push_back (std::move (utility));
    }
    auto container = makeEffectSlot (EffectKind::parallel);
    container.branches.push_back ({ std::move (chain), std::min (gain, maxFaderGain) });
    container.enabled = param (rp::mute, 0.0f) < 0.5f;
    note (&FlpImportReport::converted, "FL Studio Mobile Rack FX");
    return container;
}

std::optional<FlmRackChannel> importFlmRackState (const std::vector<std::uint8_t>& state, const FlmImportOptions& options, double bpm,
                                                  const std::string& channelName, std::string* why)
{
    const auto fail = [why] (const std::string& text) -> std::optional<FlmRackChannel> {
        if (why != nullptr)
            *why = text;
        return std::nullopt;
    };
    auto decoded = rackOfPluginState (state);
    if (! decoded)
        return fail ("FL Studio Mobile Rack (state not readable)");
    auto rack = std::move (*decoded);
    rack.kind = flm::kindNormal;
    // what one channel of FL's cannot hold the way FLM plays it: more than one instrument, drum slots (their own
    // channels in a .flm), stages (rack parts), note effects (baked into a .flm's notes; an .flp's notes are FL's)
    int generators = 0;
    bool effectBeforeInstrument = false, effectSeen = false, noteEffect = false;
    for (const auto& m : rack.modules)
    {
        if (flm::isGenerator (m.type))
        {
            ++generators;
            effectBeforeInstrument = effectBeforeInstrument || effectSeen;
        }
        else if (flm::moduleCategory (m.type) == flm::ModuleCategory::noteEffect)
            noteEffect = noteEffect || m.params.empty() || ! std::isfinite (m.params[0]) || m.params[0] >= 0.5f; // (parameter 0 = Enable)
        else if (m.type != flm::module::tuner && m.type != flm::module::analyzer)
            effectSeen = true;
    }
    if (! rack.kit.empty())
        return fail ("FL Studio Mobile Rack (drum kit: not reproduced yet)");
    if (generators != 1)
        return fail (generators == 0 ? "FL Studio Mobile Rack (no instrument)" : "FL Studio Mobile Rack (several instruments: not reproduced yet)");
    if (effectBeforeInstrument)
        return fail ("FL Studio Mobile Rack (an effect before the instrument: not reproduced yet)");
    if (noteEffect)
        return fail ("FL Studio Mobile Rack (note effects: not reproduced yet)");

    // a one-channel FLM song: the master and the rack's channel, no lanes
    flm::Project song;
    song.tempo = bpm;
    flm::Channel master;
    master.kind = flm::kindMaster;
    master.channelId = 0;
    master.name = "Master";
    flm::Channel channel;
    channel.kind = flm::kindNormal;
    channel.channelId = 1;
    channel.order = 1.0;
    channel.name = channelName;
    flm::Rack masterRack;
    masterRack.kind = flm::kindMaster;
    song.racks = { masterRack, rack };
    song.channels = { master, channel };

    FlpImportReport own;
    auto o = options;
    o.common.report = &own;
    const auto imported = importFlmProject (song, o);
    if (! imported || imported->channels.size() != 1 || imported->mixer.size() != 2)
        return fail ("FL Studio Mobile Rack (not reproduced yet)");
    const auto& c = imported->channels.front();
    if (c.instrument == InstrumentKind::synth || c.mixerTrack != 1)
        return fail ("FL Studio Mobile Rack (" + moduleLabel (std::find_if (rack.modules.begin(), rack.modules.end(), [] (const flm::Module& m) {
                                                     return flm::isGenerator (m.type);
                                                 })->type) + ": not reproduced)");
    FlmRackChannel out;
    out.channel = c;
    out.insert = imported->mixer[1];
    const MixerTrack plain;
    out.insertUsed = ! out.insert.effects.empty() || std::abs (out.insert.volume - plain.volume) > 1.0e-4f || std::abs (out.insert.pan) > 1.0e-4f
                     || out.insert.muted;
    // the sub-import's report lines count in the caller's report
    if (auto* r = options.common.report; r != nullptr)
    {
        for (const auto list : { &FlpImportReport::converted, &FlpImportReport::approximated, &FlpImportReport::unsupported,
                                 &FlpImportReport::droppedEffects, &FlpImportReport::partialEffects })
            for (const auto& [what, n] : own.*list)
                if (what.rfind ("FL Studio Mobile factory content", 0) != 0 || (r->*list).count (what) == 0)
                    (r->*list)[what] += n;
        r->missingSamples += own.missingSamples;
        r->relocatedSamples += own.relocatedSamples;
        r->missingFiles.insert (r->missingFiles.end(), own.missingFiles.begin(), own.missingFiles.end());
        r->relocatedFiles.insert (r->relocatedFiles.end(), own.relocatedFiles.begin(), own.relocatedFiles.end());
    }
    return out;
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
    // META's credits (round 190fn, format-0020…0023; FLM_FORMAT §2 META) on Settings › Info: the author, the notes, the
    // creation date and the time spent on the song
    song.info.author = project.author;
    song.info.comments = project.notes;
    song.info.created = flmDateSeconds (project.created);
    if (std::isfinite (project.editSeconds) && project.editSeconds > 0.0 && project.editSeconds < 1.0e9)
        song.info.workSeconds = project.editSeconds;
    // HEAD's app version (format-0018): FLM warns about a song from a newer app; LYNX's reader was last checked against
    // the files of FLM 5.0.4, so a newer one is named in the report (its new fields, if any, are not read)
    if (flmVersionNewer (project.appVersion, "5.0.4"))
        ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile " + project.appVersion + " (newer than this import knows: new settings may be missing)");
    song.bpm = std::clamp (project.tempo > 0.0 ? project.tempo : 120.0, minTempo, maxTempo);
    // KEYB: the keyboard's chord set becomes the project's chord set for LYNX's Chord pad (round 190ds, core/ScaleKeys.h)
    if (! project.chordSetJson.empty())
    {
        int dropped = 0;
        song.keyboardChords = scalekeys::parseFlmChordSet (project.chordSetJson, &dropped);
        if (dropped > 0)
            ctx.note (&FlpImportReport::unsupported, "FL Studio Mobile: chord set extensions outside two octaves");
    }
    // HEAD's key locker: the project key (round 190ds; Chromatic / a scale FL's list lacks: no key)
    if (const auto key = scalekeys::flmKeyLocker (project.root, project.scale))
        projectkey::set (song, key);
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
    std::deque<flm::Lane> linkedLanes; // round 190dw item 4: a CC lane's copies on the parameters CCLK links to it
    std::vector<std::pair<int, int>> outputLinks; // round 190dw item 4: (LYNX channel, FLM channel id) of linked drum pads
    std::map<int, int> insertOfId;                          // FLM channel id → insert
    std::vector<std::pair<int, const flm::Channel*>> routes; // insert, channel (its SEND chunks)
    for (const auto i : order)
    {
        const auto& ch = project.channels[i];
        const auto& rack = i < project.racks.size() ? project.racks[i] : noRack;
        const auto name = ch.name.empty() ? ch.typeName : ch.name;
        // round 190fn (mixer-0016): the channel's colour (CHHD's hue, FLM's own HSL law) on everything this FLM channel
        // becomes — its channels, Playlist rows and inserts (the Master keeps LYNX's) — once the channel is done
        struct Colours
        {
            Song& song;
            std::size_t firstChannel, firstRow, firstInsert;
            std::uint32_t colour;
            ~Colours()
            {
                if (colour == 0)
                    return;
                for (auto k = firstChannel; k < song.channels.size(); ++k)
                {
                    auto& c = song.channels[k];
                    if (c.colour == 0)
                        c.colour = colour;
                    if (c.mixerTrack > 0 && c.mixerTrack < (int) song.mixer.size() && song.mixer[(std::size_t) c.mixerTrack].colour == 0)
                        song.mixer[(std::size_t) c.mixerTrack].colour = colour;
                }
                for (auto k = firstRow; k < song.playlistTracks.size(); ++k)
                    if (song.playlistTracks[k].colour == 0)
                        song.playlistTracks[k].colour = colour;
                for (auto k = std::max<std::size_t> (1, firstInsert); k < song.mixer.size(); ++k)
                    if (song.mixer[k].colour == 0)
                        song.mixer[k].colour = colour;
            }
        } colours { song, song.channels.size(), song.playlistTracks.size(), song.mixer.size(),
                    ch.hue >= 0.0f && ch.kind != flm::kindMaster ? flm::channelColour (ch.hue) : 0u };

        // the insert: FLM's master is LYNX's Master; every other channel (bus/AUX tracks too) gets one
        int insert = 0;
        if (ch.kind != flm::kindMaster && (int) song.mixer.size() < maxMixerTracks)
        {
            MixerTrack m;
            m.name = name;
            song.mixer.push_back (std::move (m));
            insert = (int) song.mixer.size() - 1;
        }
        else if (ch.kind != flm::kindMaster) // round 190fn (mixer-0013): said, not silent
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: channels past LYNX's insert count (on the Master, without their own level and effects)");
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
            // (round 190dw item 4: [5] Aftertouch reaches no sound — every module keeps the base's empty slot 76, and
            // the rack stores it only: nothing to import)
        }
        if (ch.channelId >= 0)
            insertOfId[ch.channelId] = insert;
        routes.push_back ({ insert, &ch });

        // effects (F5): the mapped ones on the insert, in the rack's order (core/FlmEffects.h); the others are listed.
        // Generators past the first are not imported yet; analysis-only modules change nothing.
        std::map<std::uint32_t, MappedModule> mappedModules; // instance id → its slots
        // Round 190bs (PROVEN-REA: the rack's note dispatch 0x18059bfa0 and render 0x18059d3b0). Every generator of the
        // rack plays every note inside its key range: they all become channels, under a Layer when there are two or
        // more. The rack's note effects run over its notes first, wherever they sit (core/FlmNoteFx.h). The rack's audio
        // is one buffer the modules work on in order, so an effect only hears the generators before it: on a LYNX
        // insert it hears them all, which is listed.
        std::vector<const flm::Module*> generators;
        flmnotefx::Chain noteFx;
        bool effectSeen = false, generatorAfterEffect = false;
        // round 190de: an Arpeggiator or a Note Echo acts in time: the rack's notes are then baked over the song
        // (bakeTimeline below), every note effect in its place in the rack's order
        std::vector<const flm::Module*> noteModules;
        const auto timeline = std::any_of (rack.modules.begin(), rack.modules.end(), [] (const flm::Module& m) {
            return flmnotefx::timeEffectActive (m.type, m.params);
        });
        // --- round 190fe: the rack's stages (PROVEN-REA 0x18059d3b0 and the wrapper 0x180799750, FLM_FORMAT §6.1 / §7.4).
        // The rack's sounds are one buffer the modules work on in order: an effect hears only what comes before it, and
        // an instrument's wrapper scales what the buffer holds by (1 − Mix) + Post Gain · Mix. A new stage starts at an
        // instrument that follows an effect, or whose wrapper scales sounds already there; each stage but the last is an
        // insert of its own (no route to the Master) sending into the next stage at that factor, and the last stage is
        // the channel's own insert. The drum pads enter the first stage. One stage = one insert, as before.
        std::map<const flm::Module*, int> stageOf;
        std::set<const flm::Module*> scaledByStage; // instruments whose wrapper factor is the route into their stage
        std::vector<int> stageInserts { insert };
        {
            std::vector<float> entry { 1.0f };
            int stage = 0;
            bool effectInStage = false, sounds = ! rack.kit.empty();
            for (const auto& m : rack.modules)
            {
                if (flm::isGenerator (m.type))
                {
                    const auto off = ! m.params.empty() && std::isfinite (m.params[0]) && m.params[0] < 0.5f;
                    const auto mix = m.reserved.size() > 1 && std::isfinite (m.reserved[1]) ? std::clamp ((double) m.reserved[1], 0.0, 1.0) : 1.0;
                    const auto factor = off || mix <= 0.0 ? 1.0 : 1.0 - mix + generatorWrapperGain (m);
                    const auto scales = sounds && std::abs (factor - 1.0) > 1.0e-4;
                    if (effectInStage || scales)
                    {
                        ++stage;
                        entry.push_back (scales ? (float) factor : 1.0f);
                        if (scales)
                            scaledByStage.insert (&m);
                        effectInStage = false;
                    }
                    stageOf[&m] = stage;
                    sounds = true;
                    continue;
                }
                if (flm::moduleCategory (m.type) == flm::ModuleCategory::noteEffect || m.type == flm::module::tuner || m.type == flm::module::analyzer)
                    continue;
                effectInStage = true;
                stageOf[&m] = stage;
            }
            if (stage > 0 && insert > 0 && (int) song.mixer.size() + stage <= maxMixerTracks)
            {
                stageInserts.assign ((std::size_t) stage + 1, insert);
                for (int k = 0; k < stage; ++k)
                {
                    MixerTrack part;
                    part.name = name + " (rack part " + std::to_string (k + 1) + ")";
                    part.toMaster = false;
                    song.mixer.push_back (std::move (part));
                    stageInserts[(std::size_t) k] = (int) song.mixer.size() - 1;
                }
                for (int k = 0; k < stage; ++k)
                {
                    const auto level = entry[(std::size_t) k + 1];
                    if (level > maxFaderGain * 1.0001f)
                        ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: instrument Post Gain on the rack's other sounds above +5.6 dB (capped)");
                    song.mixer[(std::size_t) stageInserts[(std::size_t) k]].sends.push_back ({ stageInserts[(std::size_t) k + 1], std::min (level, maxFaderGain) });
                }
                ctx.note (&FlpImportReport::converted, "FL Studio Mobile: rack whose instruments and effects take turns (one insert per part)");
            }
            else
                scaledByStage.clear();
        }
        const auto stageInsert = [&] (const flm::Module* m) {
            const auto it = stageOf.find (m);
            return it == stageOf.end() || stageInserts.size() == 1 ? insert : stageInserts[(std::size_t) it->second];
        };
        // --- end of round 190fe
        for (const auto& m : rack.modules)
        {
            if (flm::isGenerator (m.type))
            {
                generators.push_back (&m);
                generatorAfterEffect = generatorAfterEffect || effectSeen;
                continue;
            }
            if (flm::moduleCategory (m.type) == flm::ModuleCategory::noteEffect)
            {
                const auto on = m.params.empty() || m.params[0] >= 0.5f;
                noteModules.push_back (&m);
                if (timeline)
                {
                    if (flmnotefx::timeEffectActive (m.type, m.params))
                        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: " + moduleLabel (m.type) + " (note effect, baked over the song)");
                    else if (on && (m.type == flmnotefx::transpose || m.type == flmnotefx::scale || m.type == flmnotefx::randomizer))
                        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: " + moduleLabel (m.type) + " (note effect)");
                    continue;
                }
                if (noteFx.add (m.type, m.params))
                    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: " + moduleLabel (m.type) + " (note effect)");
                else if (on)
                    ctx.note (&FlpImportReport::droppedEffects, "FL Studio Mobile: " + moduleLabel (m.type) + " (note effect, not converted)");
                continue;
            }
            if (m.type == flm::module::tuner || m.type == flm::module::analyzer)
                continue;
            const auto live = std::any_of (ch.lanes.begin(), ch.lanes.end(), [&m] (const flm::Lane& l) {
                return l.kind() == flm::LaneKind::automation && l.targetModule == (int) m.instanceId && ! l.clips.empty();
            });
            auto mapped = flmfx::map (m, live);
            const auto effectInsert = stageInsert (&m); // round 190fe
            auto& effects = song.mixer[(std::size_t) effectInsert].effects;
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
            effectSeen = true;
            mappedModules[m.instanceId] = { &m, effectInsert, (int) effects.size(), (int) mapped.slots.size() };
            for (auto& slot : mapped.slots)
                effects.push_back (std::move (slot));
            ctx.note (&FlpImportReport::converted, mapped.label);
            for (const auto& what : mapped.partial)
                ctx.note (&FlpImportReport::partialEffects, what);
        }

        if (generatorAfterEffect && stageInserts.size() == 1) // (round 190fe: else the rack's parts are inserts of their own)
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: an instrument placed after an effect in its rack (the effect processes it too)");
        if (noteFx.random())
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: Randomizer (drawn once at import; FLM draws at every playback)");

        // round 190de: lanes on the rack's Pitch Bend and on its generators' parameters (FLM_FORMAT §7.4)
        const auto bendLane = std::any_of (ch.lanes.begin(), ch.lanes.end(), [] (const flm::Lane& l) {
            return l.kind() == flm::LaneKind::automation && l.targetModule == 0 && l.targetParam == flm::rackparam::pitchBend && ! l.clips.empty();
        });
        const auto enableLane = [&ch] (const flm::Module& g) {
            return std::any_of (ch.lanes.begin(), ch.lanes.end(), [&g] (const flm::Lane& l) {
                return l.kind() == flm::LaneKind::automation && l.targetModule == (int) g.instanceId && l.targetParam == 0 && ! l.clips.empty();
            });
        };
        std::map<std::uint32_t, std::vector<int>> generatorChannels; // module instance → its LYNX channel(s)
        std::vector<int> bendChannels;                               // every channel the rack's pitch bend moves

        // the rack's held pitch bend on a channel it creates (round 190de: a Pitch Bend lane plays it instead, below)
        // Round 190de (FLM_FORMAT §7.4): the rack hands its Pitch Bend 4v − 2 to every module's bend input, which bends by
        // its own range (half the value × the range); its drum slots play at 2^(4v − 2). `range` = the semitones of the
        // full throw 2v − 1 for this channel (−1: a module whose bend was not traced). The held value moves the channel's
        // PITCH knob (every LYNX engine transposes its voices by it); a Pitch Bend lane plays as LYNX's bend, whose range
        // is set to the same semitones.
        const auto bendThrow = bendSemitones / 24.0f;
        const auto bend = [&] (ChannelDef& c, float range) {
            if (range == 0.0f) // round 190dw item 4: a module that ignores the rack's bend (the wheel does nothing either)
            {
                c.misc.pitchRange = 0;
                return;
            }
            if (range > 0.0f)
                c.misc.pitchRange = (int) std::lround (range);
            if (bendThrow == 0.0f || bendLane)
                return;
            if (range <= 0.0f)
            {
                ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: channel pitch bend on an instrument whose bend range is not known (not imported)");
                return;
            }
            const auto bent = c.sampler.pitch + bendThrow * range;
            c.sampler.pitch = std::clamp (bent, -24.0f, 24.0f);
            ctx.note (&FlpImportReport::converted, "FL Studio Mobile: channel pitch bend (on the channel's pitch)");
            if (bent != c.sampler.pitch)
                ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: channel pitch bend beyond ±24 semitones (capped)");
        };
        int instrument = -1;          // this channel's instrument (note lanes): a Layer when the rack has several
        int keyLow = 0, keyHigh = 127; // a single instrument's key range (a Layer's children have their own)
        std::vector<int> kitChannels; // this channel's drum kit (drum lanes)
        const auto instrumentIndex = [&] {
            if (instrument >= 0)
                return instrument;
            if (generators.size() <= 1)
            {
                song.channels.push_back (instrumentChannel (ctx, generators.empty() ? nullptr : generators.front(), ch, insert));
                instrument = (int) song.channels.size() - 1;
                if (! generators.empty()) // round 190fe: its stage of the rack
                    song.channels.back().mixerTrack = stageInsert (generators.front());
                const auto range = generators.empty() ? -1.0f : bendRangeOf (*generators.front());
                bend (song.channels.back(), range);
                if (range > 0.0f)
                    bendChannels.push_back (instrument);
                if (! generators.empty())
                {
                    applyGeneratorWrapper (ctx, *generators.front(), song.channels.back(), enableLane (*generators.front()),
                                           rack.kit.empty() || scaledByStage.count (generators.front()) > 0); // round 190de, 190fe
                    generatorChannels[generators.front()->instanceId].push_back (instrument);
                    keyLow = song.channels.back().keyLow = generators.front()->keyLow;
                    keyHigh = song.channels.back().keyHigh = generators.front()->keyHigh;
                }
                return instrument;
            }
            ChannelDef layer;
            layer.name = name;
            layer.instrument = InstrumentKind::layer;
            layer.mixerTrack = insert;
            layer.volume = 1.0f; // (a Layer multiplies its children by its own volume: 0 dB)
            song.channels.push_back (std::move (layer));
            instrument = (int) song.channels.size() - 1;
            for (const auto* g : generators)
            {
                auto c = instrumentChannel (ctx, g, ch, insert);
                c.mixerTrack = stageInsert (g); // round 190fe: its stage of the rack
                c.name = name + " (" + moduleLabel (g->type) + ")";
                c.keyLow = g->keyLow;
                c.keyHigh = g->keyHigh;
                const auto range = bendRangeOf (*g);
                bend (c, range);
                applyGeneratorWrapper (ctx, *g, c, enableLane (*g), (rack.kit.empty() && g == generators.front()) || scaledByStage.count (g) > 0); // round 190de, 190fe
                song.channels.push_back (std::move (c));
                song.channels[(std::size_t) instrument].layer.children.push_back ((int) song.channels.size() - 1);
                generatorChannels[g->instanceId].push_back ((int) song.channels.size() - 1);
                if (range > 0.0f)
                    bendChannels.push_back ((int) song.channels.size() - 1);
            }
            ctx.note (&FlpImportReport::converted, "FL Studio Mobile: rack with several instruments (Layer)");
            return instrument;
        };
        const auto kitIndex = [&] {
            if (kitChannels.empty())
            {
                const auto soloed = std::any_of (rack.kit.begin(), rack.kit.end(), [] (const flm::SampleRef& s) { return slotParam (s, flm::samplepad::solo) > 0.5f; });
                const auto holds = std::any_of (ch.lanes.begin(), ch.lanes.end(), [] (const flm::Lane& l) {
                    return l.kind() == flm::LaneKind::drums && l.holdsNotes() && ! l.clips.empty();
                });
                for (std::size_t k = 0; k < rack.kit.size(); ++k)
                {
                    const auto& s = rack.kit[k];
                    ChannelDef c;
                    c.name = ! s.name.empty() ? s.name : fileStem (s.path.empty() ? s.savedPath : s.path);
                    if (c.name.empty())
                        c.name = name + " " + std::to_string (k + 1);
                    c.instrument = InstrumentKind::sampler;
                    c.samplePath = ctx.resolveSample (s, name + ": " + c.name);
                    c.mixerTrack = stageInserts.front(); // (round 190fe: the drum pads enter the rack's first part)
                    c.volume = 1.0f;
                    applyPad (ctx, s, c, soloed, holds, song.bpm);
                    if (! s.polyphonic)
                        c.sampler.cut = true; // a new hit silences the pad's own voices
                    bend (c, 24.0f); // (drum slots: 2^(4v − 2))
                    song.channels.push_back (std::move (c));
                    kitChannels.push_back ((int) song.channels.size() - 1);
                    if (s.outputChannel >= 0) // round 190dw item 4
                        outputLinks.push_back ({ kitChannels.back(), s.outputChannel });
                    bendChannels.push_back (kitChannels.back());
                }
                // cut groups A / B / C: a hit fades out the group's *other* pads, never its own (FLM_FORMAT §6.1). A
                // pair is two one-way links; a bigger group shares one id with ChannelDef::cutOthersOnly (round 190fe),
                // so each member cuts the others and keeps its own voices (a monophonic pad still cuts itself through
                // the sampler's own cut, as in FLM)
                for (int group = 1; group <= 3; ++group)
                {
                    std::vector<int> members;
                    for (std::size_t k = 0; k < rack.kit.size(); ++k)
                        if (rack.kit[k].cutGroup == group)
                            members.push_back (kitChannels[k]);
                    if (members.size() < 2)
                        continue;
                    if (members.size() == 2)
                    {
                        auto& a = song.channels[(std::size_t) members[0]];
                        auto& b = song.channels[(std::size_t) members[1]];
                        a.cutGroup = b.cutBy = ++ctx.cutGroups;
                        b.cutGroup = a.cutBy = ++ctx.cutGroups;
                    }
                    else
                    {
                        const auto id = ++ctx.cutGroups;
                        for (const auto m : members)
                        {
                            auto& c = song.channels[(std::size_t) m];
                            c.cutGroup = c.cutBy = id;
                            c.cutOthersOnly = true;
                        }
                    }
                    if (holds) // round 190fe: a held note's end cuts the group's other pads too (0x1807503a0)
                        for (const auto m : members)
                            song.channels[(std::size_t) m].cutAtNoteEnd = true;
                    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: drum pad cut group");
                }
                if (! rack.kit.empty())
                    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: drum kit (one sampler channel per sample)");
            }
            return kitChannels;
        };

        // the channel's instruments exist even without clips (an empty track, a new-song template's kit)
        std::vector<double> kitShifts; // each kit sample's time shift, beats
        for (const auto& s : rack.kit)
            kitShifts.push_back (s.timeShift);
        if (ch.kind == flm::kindNormal)
        {
            if (! rack.kit.empty())
                kitIndex();
            if (! generators.empty()
                || std::any_of (ch.lanes.begin(), ch.lanes.end(), [] (const flm::Lane& l) { return l.kind() == flm::LaneKind::notes && ! l.clips.empty(); }))
                instrumentIndex();
        }

        // round 190de: a rack with an Arpeggiator or a Note Echo: its note lanes' notes on the song timeline, through
        // the rack's note effects in order, as one pattern over the song (placed on the first note lane's row)
        int timelinePattern = -1;
        if (timeline && ch.kind == flm::kindNormal)
            timelinePattern = bakeTimeline (ctx, ch, noteModules, instrumentIndex(), keyLow, keyHigh, name);

        // --- round 190dw item 4: FLM's rack hands every CC to its modules' CC handler first (0x180799210), which sets
        // the parameters CCLK links to that CC to the CC's value: such a lane also plays as a lane on each linked
        // parameter (the copies follow the CC lane)
        std::vector<const flm::Lane*> laneOrder;
        for (const auto& l : ch.lanes)
        {
            laneOrder.push_back (&l);
            if (l.kind() == flm::LaneKind::automation && l.targetModule == 0 && l.targetParam >= flm::rackparam::firstMidiCc
                && l.targetParam <= flm::rackparam::lastMidiCc)
                for (const auto& link : rack.ccLinks)
                    if (link.cc == l.targetParam - flm::rackparam::firstMidiCc && link.module > 0)
                    {
                        auto copy = l;
                        copy.targetModule = link.module;
                        copy.targetParam = link.param;
                        linkedLanes.push_back (std::move (copy));
                        laneOrder.push_back (&linkedLanes.back());
                    }
        }
        for (const auto* lanePointer : laneOrder)
        // --- end of round 190dw item 4
        {
            const auto& lane = *lanePointer;
            const auto kind = lane.kind();
            if (kind == flm::LaneKind::notes && timelinePattern != -1 && ! lane.clips.empty())
            {
                // (the rack's notes are in the baked pattern: its first note lane holds it, once, from the song's start)
                PlaylistTrack row;
                row.name = lane.name.empty() ? name : lane.name;
                row.mixerTrack = insert;
                row.mode = PlaylistTrackMode::instrument;
                row.channel = instrument;
                song.playlistTracks.push_back (row);
                if (timelinePattern >= 0)
                {
                    Clip c;
                    c.kind = ClipKind::pattern;
                    c.pattern = timelinePattern;
                    c.track = (int) song.playlistTracks.size() - 1;
                    c.start = 0;
                    c.length = song.patterns[(std::size_t) timelinePattern].lengthTicks;
                    song.playlist.push_back (c);
                    timelinePattern = -2; // (placed)
                }
                continue;
            }
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
                        // round 190de: a generator's parameter on the channel(s) that play it
                        if (const auto gc = generatorChannels.find ((std::uint32_t) lane.targetModule); gc != generatorChannels.end())
                        {
                            const flm::Module* module = nullptr;
                            for (const auto& m : rack.modules)
                                if ((int) m.instanceId == lane.targetModule)
                                    module = &m;
                            std::string why;
                            if (module != nullptr && addGeneratorLane (ctx, lane, *module, gc->second, name, why))
                                continue;
                            ctx.note (&FlpImportReport::unsupported, why.empty() ? skippedLaneLabel (lane) : why);
                            continue;
                        }
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
                if (lane.targetModule == 0 && lane.targetParam == flm::rackparam::pitchBend) // round 190de
                {
                    std::string why;
                    if (! addBendLane (ctx, lane, bendChannels, name, why))
                        ctx.note (&FlpImportReport::unsupported, why.empty() ? skippedLaneLabel (lane) : why);
                    continue;
                }
                // --- round 190dw item 4: Aftertouch and the mod wheel (FLM_FORMAT §7.4)
                if (lane.targetModule == 0 && lane.targetParam == flm::rackparam::aftertouch)
                {
                    ctx.note (&FlpImportReport::converted, "FL Studio Mobile: Aftertouch automation (no FL Studio Mobile instrument responds to it)");
                    continue;
                }
                if (lane.targetModule == 0 && lane.targetParam == flm::rackparam::firstMidiCc + 1)
                {
                    std::vector<int> wheels;
                    for (const auto& m : rack.modules)
                        if (const auto gc = generatorChannels.find (m.instanceId); gc != generatorChannels.end())
                            for (const auto k : gc->second)
                                if (takesFlmModWheel (song.channels[(std::size_t) k]))
                                    wheels.push_back (k);
                    std::string why;
                    if (! addWheelLane (ctx, lane, wheels, name, why))
                        ctx.note (why.empty() ? &FlpImportReport::converted : &FlpImportReport::unsupported,
                                  why.empty() ? "FL Studio Mobile: mod-wheel automation on instruments that ignore the wheel (nothing to play)" : why);
                    continue;
                }
                // --- end of round 190dw item 4
                if (! importableRackLane (lane))
                {
                    // round 190dw item 4: a CC lane plays through its linked parameters (above); one without a link and
                    // without a role of its own (1 wheel, 7 level, 8 / 10 pan, 64 sustain) does nothing in FLM either
                    const auto cc = lane.targetParam - flm::rackparam::firstMidiCc;
                    if (lane.targetParam >= flm::rackparam::firstMidiCc && lane.targetParam <= flm::rackparam::lastMidiCc && cc != 64)
                    {
                        if (! rackLinksCc (rack, cc))
                            ctx.note (&FlpImportReport::converted, skippedLaneLabel (lane));
                        continue;
                    }
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
                int envelopeRow = -1;
                for (const auto& clip : lane.clips)
                    addAudioClip (ctx, clip, rowIndex, insert, row.name, envelopeRow);
                continue;
            }

            // note / drum lanes: one pattern per content id, each clip's loop passes on the Playlist
            std::map<std::uint32_t, int> patternOf;
            int dropped = 0, outside = 0;
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
                        }, nullptr, &kitShifts);
                    }
                    else
                    {
                        const auto channel = instrumentIndex();
                        pattern = addPattern (ctx, clip, pname, false, std::clamp (lane.swing, 0.0, 1.0), [&] (int key) -> std::pair<int, int> {
                            if (key < 0)
                                return { -1, 0 };
                            if (key < keyLow || key > keyHigh) // (FLM hands the instrument only the notes of its key range)
                            {
                                ++outside;
                                return { -1, 0 };
                            }
                            return { channel, key };
                        }, &noteFx);
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
            {
                song.playlistTracks[(std::size_t) rowIndex].channel = instrument;
                if (outside > 0 && ctx.report != nullptr)
                    ctx.report->converted["FL Studio Mobile: notes outside the instrument's key range (silent in FLM too)"] += outside;
            }
        }
    }
    // routing (F2): a SEND is (target channel id, …, linear gain capped at 1, …, …, level 0…1 on FLM's fader law)
    // [FLM_FORMAT §4]; the master's id is its route to the Master, another channel's a send to that insert. A channel
    // with SENDs but none to the master doesn't reach it directly; one without any keeps LYNX's default route.
    int masterId = -1;
    for (const auto& c : project.channels)
        if (c.kind == flm::kindMaster)
            masterId = c.channelId < 0 ? 0 : c.channelId;
    // --- round 190dw item 4: a drum pad's output link (LINk's fourth word, FLM_FORMAT §6): FLM renders a linked pad
    // into the target channel's buffer, with the sounds that channel receives (0x18075ba30, called from the channel
    // mix 0x180778520), and not into its own rack (0x180747e56). LYNX: the pad's channel plays into the target's insert
    for (const auto& [k, id] : outputLinks)
    {
        auto target = -1;
        if (id == masterId)
            target = 0;
        else if (const auto it = insertOfId.find (id); it != insertOfId.end())
            target = it->second;
        if (target < 0)
        {
            ctx.note (&FlpImportReport::partialEffects, "FL Studio Mobile: drum pad linked to a channel the song lacks (plays in its own)");
            continue;
        }
        song.channels[(std::size_t) k].mixerTrack = target;
        ctx.note (&FlpImportReport::converted, "FL Studio Mobile: drum pad output link (plays through the target channel's insert)");
    }
    // --- end of round 190dw item 4
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
            auto gain = std::min (flmGain (level), maxFaderGain);
            // a short SEND (no [5]): FLM's reader rebuilds the level from the linear gain [2] (FLM_FORMAT §4 SEND, PROVEN-REA
            // 0x180758ac0), so the route plays at that gain (round 190fn, format-0049)
            if (w.size() >= 3 && w.size() < 6)
            {
                float linear = 1.0f;
                std::memcpy (&linear, &w[2], 4);
                if (std::isfinite (linear) && linear >= 0.0f)
                    gain = std::min (linear, std::min (1.0f, maxFaderGain));
            }
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
        const auto p = rackCcAlias (lane->targetParam) >= 0 ? rackCcAlias (lane->targetParam) : lane->targetParam; // round 190dw item 4
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
    // round 190fn (O1): factory files missing → say where LYNX looks for them (it never ships Image-Line content)
    if (ctx.resourceMissing)
        ctx.note (&FlpImportReport::unsupported, ctx.options.userResourceRoots.empty()
                                                     ? "FL Studio Mobile factory content not found: add the app's Resources folder in Settings > Files"
                                                     : "FL Studio Mobile factory content not found in the Resources folders of Settings > Files");
    return std::move (ctx.song);
}
} // namespace lynx
