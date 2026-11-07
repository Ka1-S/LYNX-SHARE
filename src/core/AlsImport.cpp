#include "core/AlsImport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "core/FlWrapper.h"
#include "core/Gzip.h"
#include "core/MissingFiles.h"
#include "core/XmlLite.h"

// Evidence tags as in docs/analise/ALS_FORMAT.md: [DATA] the sets Live 12 ships, [BIN] Live 12.4's member-name table.

namespace lynx
{
namespace
{
using xml::Element;

constexpr double minusInfGain = 0.000316228; // Live's −70 dB fader floor = off [DATA: MidiControllerRange Min]
constexpr double beforeSongTime = -63072000.0; // the automation's "before the song" point [DATA]

std::int64_t ticksOf (double beats) noexcept { return (std::int64_t) std::llround (beats * ppq); }

int intOf (const std::string& s, int fallback = 0)
{
    char* end = nullptr;
    const auto v = std::strtol (s.c_str(), &end, 10);
    return end != s.c_str() ? (int) v : fallback;
}

double numberAttr (const Element& e, std::string_view a, double fallback = 0.0)
{
    if (! e.hasAttr (a))
        return fallback;
    const auto s = e.attr (a);
    char* end = nullptr;
    const auto v = std::strtod (s.c_str(), &end);
    return end != s.c_str() ? v : fallback;
}

std::string trackName (const Element& track)
{
    auto n = track.value ("Name/UserName");
    if (n.empty())
        n = track.value ("Name/EffectiveName");
    // Live's auto-numbering marker ("# Drums" → "1 Drums") is shown resolved in EffectiveName
    if (n.find ('#') != std::string::npos)
        if (const auto eff = track.value ("Name/EffectiveName"); ! eff.empty())
            n = eff;
    return n;
}

std::string fileStem (const std::string& path)
{
    auto name = pathFileName (path);
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos && dot > 0)
        name.resize (dot);
    return name;
}

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

// A parameter's automation target id ("AutomationTarget Id" under a parameter element) [DATA]
std::string targetId (const Element& parameter) { return parameter.path ("AutomationTarget").attr ("Id"); }

// How an automated value maps to LYNX's 0..1 for its target
enum class ValueMap { fader, pan, tempo, plain };

struct Target
{
    AutomationTarget target;
    ValueMap map = ValueMap::plain;
    std::string name;
};

struct Ctx
{
    const AlsImportOptions& options;
    FlpImportReport* report;
    Song song;
    std::unordered_map<std::string, Target> targets; // AutomationTarget Id → LYNX target
    std::map<std::string, int> audioChannels;        // resolved sample path (+ stretch mode) → audioClip channel
    int nextRow = 0;
    std::set<std::string> missingSeen;

    explicit Ctx (const AlsImportOptions& o) : options (o), report (o.common.report) {}

    void note (std::map<std::string, int> FlpImportReport::* list, const std::string& what)
    {
        if (report != nullptr)
            ++(report->*list)[what];
    }

    // A sample reference → a path that exists on this PC ("" = missing; the saved path is reported)
    std::string resolveSample (const Element& sampleRef, const std::string& user)
    {
        const auto fileRef = sampleRef.child ("FileRef");
        if (! fileRef)
            return {};
        const auto type = (int) fileRef.number ("RelativePathType", 0);
        auto rel = fileRef.value ("RelativePath");
        if (rel.empty()) // Live ≤ 10 style: a list of folder names [model]
            for (const auto& el : fileRef.path ("RelativePath").children ("RelativePathElement"))
                rel += (rel.empty() ? "" : "/") + el.attr ("Dir");
        const auto saved = fileRef.value ("Path");
        const auto& exists = options.common.fileExists;

        std::vector<std::string> candidates;
        if (const auto it = options.common.relocations.find (saved); it != options.common.relocations.end())
            candidates.push_back (it->second);
        if (! rel.empty())
        {
            if (type == 1 || type == 0)
                candidates.push_back (joinPath (options.common.projectFolder, rel));
            for (const auto& root : options.libraryRoots)
                candidates.push_back (joinPath (root, rel));
        }
        if (! saved.empty())
            candidates.push_back (saved);
        // a set moved without its folder structure: the file next to it, or in its Samples folders
        if (const auto name = pathFileName (saved.empty() ? rel : saved); ! name.empty())
            for (const auto* sub : { "", "Samples/Imported", "Samples/Recorded", "Samples/Processed/Consolidate", "Samples/Processed/Crop",
                                     "Samples/Processed/Freeze", "Samples/Processed/Reverse" })
                candidates.push_back (joinPath (options.common.projectFolder, std::string (sub).empty() ? name : std::string (sub) + "/" + name));

        if (exists)
            for (const auto& c : candidates)
                if (! c.empty() && exists (c))
                {
                    if (report != nullptr && c != saved && ! saved.empty() && c != candidates.front())
                    {
                        ++report->relocatedSamples;
                        report->relocatedFiles.push_back ({ saved, c });
                    }
                    return c;
                }

        const auto shown = saved.empty() ? rel : saved;
        if (report != nullptr && ! shown.empty())
        {
            if (missingSeen.insert (shown).second)
                ++report->missingSamples;
            addMissingFile (report->missingFiles, shown, user);
        }
        return {};
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Plugins [BIN: AVstPluginInfo / AVst3PluginInfo / AVst3Preset / AVstPreset members]

std::optional<ForeignPlugin> pluginOf (const Element& device)
{
    const auto desc = device.child ("PluginDesc");
    if (! desc)
        return std::nullopt;
    ForeignPlugin p;
    if (const auto v3 = desc.child ("Vst3PluginInfo"))
    {
        p.format = ForeignPlugin::Format::vst3;
        p.name = v3.value ("Name");
        std::array<std::int32_t, 4> f {};
        for (int k = 0; k < 4; ++k)
            f[(std::size_t) k] = (std::int32_t) std::strtoll (v3.value ("Uid/Fields." + std::to_string (k), "0").c_str(), nullptr, 10);
        p.classId = alsVst3ClassId (f);
        p.hasClassId = f != std::array<std::int32_t, 4> {};
        const auto preset = v3.path ("Preset/Vst3Preset");
        const auto component = alsHexDecode (preset.child ("ProcessorState").text());
        const auto controller = alsHexDecode (preset.child ("ControllerState").text());
        // FL's VST3 record layout (FlWrapper.h splitFlVst3State), which the plugin resolver turns into JUCE's state
        if (! component.empty() || ! controller.empty())
        {
            auto put32 = [&p] (std::uint32_t v) { for (int k = 0; k < 4; ++k) p.state.push_back ((std::uint8_t) (v >> (8 * k))); };
            auto put64 = [&p] (std::uint64_t v) { for (int k = 0; k < 8; ++k) p.state.push_back ((std::uint8_t) (v >> (8 * k))); };
            auto record = [&] (std::uint32_t id, const std::vector<std::uint8_t>& bytes) {
                put32 (id);
                put64 (bytes.size());
                p.state.insert (p.state.end(), bytes.begin(), bytes.end());
            };
            put32 (1);
            std::vector<std::uint8_t> head (64, 0);
            head[0] = 1;
            record (1, head);
            record (3, component);
            record (2, controller);
        }
        return p;
    }
    if (const auto v2 = desc.child ("VstPluginInfo"))
    {
        p.format = ForeignPlugin::Format::vst2;
        p.name = v2.value ("PlugName");
        p.path = v2.value ("Path");
        p.vst2Id = (std::uint32_t) std::strtoll (v2.value ("UniqueId", "0").c_str(), nullptr, 10);
        const auto preset = v2.path ("Preset/VstPreset");
        const auto buffer = alsHexDecode (preset.child ("Buffer").text());
        if (! buffer.empty())
        {
            FlVst2State s;
            s.currentProgram = (std::int32_t) preset.number ("ProgramNumber", 0);
            const auto params = (std::size_t) std::max (0.0, preset.number ("ParameterCount", 0));
            // VstPreset/Type is the fxp/fxb four-character code [LIVE: kHs Gain VST2 saved by Live 12.4 = 'FBCh', Buffer
            // = the raw effGetChunk bytes]: 'FBCh' / 'FPCh' → a chunk; 'FxBk' / 'FxCk' → the parameter values
            const auto type = (std::uint32_t) (std::int64_t) preset.number ("Type", 0);
            const auto usesChunks = type == 0x46426368u || type == 0x46506368u;
            if (! usesChunks && params > 0 && buffer.size() == params * 4)
            {
                // a plugin without chunks: its parameter values, 32-bit floats [model; TODO-LIVE: the byte order]
                const auto big = preset.number ("ByteOrder", 0) != 0;
                for (std::size_t k = 0; k < params; ++k)
                {
                    std::uint32_t u = 0;
                    for (int b = 0; b < 4; ++b)
                        u |= (std::uint32_t) buffer[k * 4 + (std::size_t) (big ? 3 - b : b)] << (8 * b);
                    float f;
                    std::memcpy (&f, &u, 4);
                    s.params.push_back (f);
                }
            }
            else
            {
                s.flags = 8; // the plugin uses chunks
                s.chunk = buffer;
            }
            p.state = writeFlVst2State (s);
        }
        return p;
    }
    if (const auto au = desc.child ("AuPluginInfo"))
    {
        p.format = ForeignPlugin::Format::other; // macOS Audio Units: kept by name only
        p.name = au.value ("Name");
        return p;
    }
    return std::nullopt;
}

// Live's instrument devices [DATA: the device tags in the shipped sets]
bool isInstrumentDevice (std::string_view tag)
{
    static const std::set<std::string_view> names { "OriginalSimpler", "MultiSampler", "InstrumentGroupDevice", "DrumGroupDevice",
                                                    "Operator", "InstrumentVector", "Drift", "InstrumentMeld", "UltraAnalog", "LoungeLizard",
                                                    "StringStudio", "Collision", "Tension", "InstrumentImpulse", "MxDeviceInstrument",
                                                    "DrumSampler" };
    return names.count (tag) > 0;
}
bool isMidiEffect (std::string_view tag) { return tag.starts_with ("Midi") || tag == "MxDeviceMidiEffect"; }

// A readable name for a native device, for the report ("Eq8" → "EQ Eight" is Live's own naming; the report keeps
// the stored device name so nothing trademarked is invented)
std::string deviceName (const Element& device)
{
    auto n = device.value ("UserName");
    return n.empty() ? std::string (device.name()) : n + " (" + std::string (device.name()) + ")";
}

// The first sample part of a Simpler / Sampler as a zone
std::vector<SampleZone> zonesOf (Ctx& ctx, const Element& device, const std::string& user)
{
    std::vector<SampleZone> zones;
    for (const auto& part : device.path ("Player/MultiSampleMap/SampleParts").children ("MultiSamplePart"))
    {
        SampleZone z;
        z.path = ctx.resolveSample (part.child ("SampleRef"), user);
        if (z.path.empty())
            continue;
        z.rootKey = (int) part.number ("RootKey", 60);
        z.lowKey = (int) part.number ("KeyRange/Min", 0);
        z.highKey = (int) part.number ("KeyRange/Max", 127);
        z.lowVelocity = (int) part.number ("VelocityRange/Min", 1);
        z.highVelocity = (int) part.number ("VelocityRange/Max", 127);
        z.tune = (float) (part.number ("Detune", 0) / 100.0);
        z.gain = (float) part.number ("Volume", 1);
        if (part.number ("SustainLoop/Mode", 0) != 0)
        {
            z.loopStart = (std::int64_t) part.number ("SustainLoop/Start", 0);
            z.loopEnd = (std::int64_t) part.number ("SustainLoop/End", 0);
            z.loopMode = 1;
        }
        zones.push_back (std::move (z));
    }
    return zones;
}

// Simpler's envelope (seconds from Live's ms) [DATA: Envelope/AttackTime … in the device, ms]
void applySimplerEnvelope (const Element& device, SamplerSettings& s)
{
    const auto env = device.child ("VolumeAndPan").child ("Envelope") ? device.path ("VolumeAndPan/Envelope") : device.child ("Envelope");
    if (! env)
        return;
    const auto ms = [&env] (const char* name, double fallback) { return env.number (std::string (name) + "/Manual", fallback) / 1000.0; };
    s.attack = (float) std::clamp (ms ("AttackTime", 0.1), 0.0, 2.0);
    s.decay = (float) std::max (0.0, ms ("DecayTime", 600));
    s.sustain = (float) std::clamp (env.number ("SustainLevel/Manual", 1), 0.0, 1.0);
    s.release = (float) std::clamp (ms ("ReleaseTime", 50), 0.0, 4.0);
}

// Fills `channel` from a MIDI track's instrument device. Returns false for none.
bool instrumentFrom (Ctx& ctx, const Element& device, ChannelDef& channel, std::vector<DrumPad>* pads, int depth = 0)
{
    const auto tag = std::string (device.name());
    if (tag == "PluginDevice")
    {
        if (const auto p = pluginOf (device))
        {
            channel.instrument = InstrumentKind::plugin;
            channel.plugin.name = p->name;
            channel.plugin.foreign = *p;
            ctx.note (&FlpImportReport::converted, "Plugin: " + p->name);
            return true;
        }
        return false;
    }
    if (tag == "OriginalSimpler" || tag == "MultiSampler")
    {
        auto zones = zonesOf (ctx, device, channel.name);
        if (zones.empty())
        {
            ctx.note (&FlpImportReport::unsupported, deviceName (device) + " (sample missing)");
            return false;
        }
        channel.instrument = InstrumentKind::sampler;
        const auto& z = zones.front();
        if (zones.size() == 1 && z.lowKey == 0 && z.highKey == 127 && z.lowVelocity <= 1 && z.highVelocity == 127 && std::abs (60 - z.rootKey) <= 24)
        {
            channel.samplePath = z.path;
            channel.sampler.pitch = (float) (60 - z.rootKey) + z.tune;
            channel.sampler.gainDb = z.gain > 0.0f ? 20.0f * std::log10 (z.gain) : -24.0f;
        }
        else
        {
            channel.samplePath = z.path;
            channel.zones = std::move (zones);
        }
        applySimplerEnvelope (device, channel.sampler);
        // one-shot (Player/PlaybackMode 1 [DATA name]): the whole sample plays (release 0 = one-shot in LYNX)
        if ((int) device.number ("Globals/PlaybackMode", 0) == 1)
            channel.sampler.release = 0.0f;
        ctx.note (&FlpImportReport::converted, tag == "OriginalSimpler" ? "Simpler" : "Sampler");
        return true;
    }
    if (tag == "DrumGroupDevice" && pads != nullptr)
    {
        for (const auto& branch : device.path ("Branches").children ("DrumBranch"))
        {
            DrumPad pad;
            pad.name = branch.value ("Name/UserName");
            if (pad.name.empty())
                pad.name = branch.value ("Name/EffectiveName");
            // BranchInfo/ReceivingNote = 128 − the pad's MIDI key [DATA: 92 ↔ C1 (36) in the drum lesson set]
            pad.key = 128 - (int) branch.number ("BranchInfo/ReceivingNote", 92);
            pad.cutGroup = (int) branch.number ("BranchInfo/ChokeGroup", 0);
            pad.cutBy = pad.cutGroup;
            for (const auto& inner : branch.path ("DeviceChain/MidiToAudioDeviceChain/Devices").children())
            {
                ChannelDef sub;
                sub.name = pad.name;
                if (instrumentFrom (ctx, inner, sub, nullptr, depth + 1) && sub.instrument == InstrumentKind::sampler)
                {
                    if (sub.zones.empty())
                    {
                        SampleZone z;
                        z.path = sub.samplePath;
                        z.tune = sub.sampler.pitch; // (played at its own pitch on the pad's key)
                        pad.layers.push_back (z);
                    }
                    else
                        pad.layers = sub.zones;
                    break;
                }
            }
            pad.gain = (float) branch.number ("MixerDevice/Volume/Manual", 1);
            pad.pan = (float) branch.number ("MixerDevice/Panorama/Manual", 0);
            if (! pad.layers.empty())
                pads->push_back (std::move (pad));
        }
        if (pads->empty())
            return false;
        channel.instrument = InstrumentKind::sampler;
        channel.samplePath = pads->front().layers.front().path;
        ctx.note (&FlpImportReport::converted, "Drum rack");
        return true;
    }
    if ((tag == "InstrumentGroupDevice" || tag == "DrumGroupDevice") && depth < 8)
    {
        // a rack: its first chain's instrument (the other chains and the macros are not kept yet)
        for (const auto& branch : device.path ("Branches").children())
            for (const auto& inner : branch.path ("DeviceChain/MidiToAudioDeviceChain/Devices").children())
                if (isInstrumentDevice (inner.name()) || inner.name() == "PluginDevice")
                    if (instrumentFrom (ctx, inner, channel, nullptr, depth + 1))
                    {
                        ctx.note (&FlpImportReport::approximated, "Instrument rack (first chain)");
                        return true;
                    }
        return false;
    }
    // Live's synths: no LYNX equivalent; the channel keeps a built-in synth, as for an unknown FL generator
    ctx.note (&FlpImportReport::unsupported, deviceName (device));
    return false;
}

// Audio effects of a track (after its instrument): plugins carried over, the rest listed
void effectsFrom (Ctx& ctx, const std::vector<Element>& devices, MixerTrack& insert)
{
    for (const auto& d : devices)
    {
        const auto tag = d.name();
        if (tag == "PluginDevice")
        {
            if (const auto p = pluginOf (d); p && insert.effects.size() < (std::size_t) maxEffectSlots)
            {
                EffectSlot slot;
                slot.kind = EffectKind::plugin;
                slot.enabled = d.boolean ("On/Manual", true);
                slot.plugin.name = p->name;
                slot.plugin.foreign = *p;
                insert.effects.push_back (std::move (slot));
                ctx.note (&FlpImportReport::converted, "Plugin: " + p->name);
            }
            continue;
        }
        if (isMidiEffect (tag))
            ctx.note (&FlpImportReport::unsupported, deviceName (d));
        else
            ctx.note (&FlpImportReport::droppedEffects, deviceName (d));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Clips

struct ClipLoop
{
    double start = 0, end = 0;           // clip-content span played first [start, end)
    double loopStart = 0, loopEnd = 0;   // with `on`
    bool on = false;
};

ClipLoop loopOf (const Element& clip)
{
    const auto l = clip.child ("Loop");
    ClipLoop c;
    c.loopStart = l.number ("LoopStart", 0);
    c.loopEnd = l.number ("LoopEnd", 0);
    c.on = l.boolean ("LoopOn", false) && c.loopEnd > c.loopStart;
    c.start = c.loopStart + l.number ("StartRelative", 0);
    c.end = c.on ? c.loopEnd : l.number ("OutMarker", c.loopEnd);
    if (c.end <= c.start)
        c.end = c.start + std::max (0.0, c.loopEnd - c.loopStart);
    return c;
}

// The content ranges a clip of `length` beats plays, as (clip time, content start, content end) passes [DATA: Loop]
struct Pass
{
    double at, from, to;
};
std::vector<Pass> passesOf (const ClipLoop& loop, double length)
{
    std::vector<Pass> passes;
    if (length <= 0)
        return passes;
    double at = 0;
    auto from = loop.start, to = loop.end;
    for (int guard = 0; at < length - 1e-9 && guard < 4096; ++guard)
    {
        const auto take = std::min (to - from, length - at);
        if (take <= 1e-9)
            break;
        passes.push_back ({ at, from, from + take });
        at += take;
        if (! loop.on)
            break;
        from = loop.loopStart;
        to = loop.loopEnd;
    }
    return passes;
}

int patternFromMidiClip (Ctx& ctx, const Element& clip, int channel, double length)
{
    Pattern p;
    p.name = clip.value ("Name");
    p.colour = alsColour ((int) clip.number ("Color", -1));
    const auto passes = passesOf (loopOf (clip), length);
    for (const auto& keyTrack : clip.path ("Notes/KeyTracks").children ("KeyTrack"))
    {
        const auto key = (int) keyTrack.number ("MidiKey", 60);
        for (const auto& n : keyTrack.path ("Notes").children ("MidiNoteEvent"))
        {
            const auto t = numberAttr (n, "Time"), d = numberAttr (n, "Duration", 0.25);
            for (const auto& pass : passes)
            {
                if (t < pass.from - 1e-9 || t >= pass.to - 1e-9)
                    continue;
                Note note;
                note.channel = channel;
                note.key = std::clamp (key, 0, 127);
                note.start = ticksOf (pass.at + t - pass.from);
                note.length = std::max<std::int64_t> (1, std::min (ticksOf (d), ticksOf (length) - note.start));
                note.velocity = (float) std::clamp (numberAttr (n, "Velocity", 100) / 127.0, 0.0, 1.0);
                note.release = (float) std::clamp (numberAttr (n, "OffVelocity", 64) / 127.0, 0.0, 1.0);
                note.muted = n.hasAttr ("IsEnabled") && n.attr ("IsEnabled") == "false";
                p.notes.push_back (note);
            }
        }
    }
    p.lengthTicks = ticksOf (length);
    p.sortNotes();
    ctx.song.patterns.push_back (std::move (p));
    return (int) ctx.song.patterns.size() - 1;
}

// Live's warp mode → LYNX's warp modes (round 175a, N4): Re-Pitch (3) resamples; Beats (0), Tones (1), Texture (2)
// map to LYNX's modes of the same names, Complex (4) and Complex Pro (6) to Complex [DATA: WarpMode values]
void applyWarpMode (int mode, SamplerSettings& s)
{
    s.keepPitch = mode != 3;
    s.stretchMethod = mode == 0 ? StretchMethod::beats : mode == 1 ? StretchMethod::tones : mode == 2 ? StretchMethod::texture : StretchMethod::complex;
}

// `stretchBeats` > 0: the channel plays the file stretched offline to that many beats at the song tempo (the warp
// modes' own engines, core/TimeStretch); 0: the clips stretch it in real time (their `stretch`)
int audioChannelFor (Ctx& ctx, const std::string& path, int mixerTrack, int warpMode, bool warped, double stretchBeats = 0.0,
                     double beatsPitch = 0.0)
{
    const auto key = path + "|" + std::to_string (mixerTrack) + "|" + std::to_string (warped ? warpMode : -1) + "|"
                     + std::to_string (std::llround (stretchBeats * 1.0e6)) + "|" + std::to_string (std::llround (beatsPitch * 100.0));
    if (const auto it = ctx.audioChannels.find (key); it != ctx.audioChannels.end())
        return it->second;
    ChannelDef c;
    c.name = fileStem (path);
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = path;
    // 0 dB: a Playlist audio clip plays at unity at FL's default channel volume (10000 / 12800; Instruments.cpp
    // audioClipUnity) [measured, round 175a: 1.0 played Live's warped vocal 5.2 dB too loud]
    c.volume = 10000.0f / 12800.0f;
    c.mixerTrack = mixerTrack;
    if (warped)
        applyWarpMode (warpMode, c.sampler);
    c.sampler.stretchBeats = (float) stretchBeats;
    c.sampler.stretchPitch = (float) beatsPitch;
    ctx.song.channels.push_back (std::move (c));
    const auto index = (int) ctx.song.channels.size() - 1;
    ctx.audioChannels[key] = index;
    return index;
}

void addAudioClip (Ctx& ctx, const Element& clip, int row, int mixerTrack, double at, double length, const std::string& user)
{
    const auto path = ctx.resolveSample (clip.child ("SampleRef"), "Audio clip: " + user);
    if (path.empty() || length <= 0)
        return;
    const auto warped = clip.boolean ("IsWarped", true);
    const auto mode = (int) clip.number ("WarpMode", 0);
    const auto bpm = ctx.song.bpm;

    // warp: beats of the clip's content ↔ seconds of the sample, piecewise linear [DATA: WarpMarker SecTime/BeatTime].
    // LYNX stretches a clip at one rate: the one between the first and the last marker (slice N4 adds the segments).
    double rate = bpm / 60.0, beatAtZero = 0.0; // beats per sample second; the content beat at sample time 0
    if (warped)
    {
        std::vector<std::pair<double, double>> marks;
        for (const auto& m : clip.path ("WarpMarkers").children ("WarpMarker"))
            marks.push_back ({ numberAttr (m, "SecTime"), numberAttr (m, "BeatTime") });
        std::sort (marks.begin(), marks.end());
        if (marks.size() >= 2 && marks.back().first > marks.front().first + 1e-6 && marks.back().second > marks.front().second)
            rate = (marks.back().second - marks.front().second) / (marks.back().first - marks.front().first);
        if (! marks.empty())
            beatAtZero = marks.front().second - rate * marks.front().first;
        if (marks.size() > 3 && ctx.report != nullptr)
        {
            // more than one segment (Live writes one marker past the end): an approximation until N4
            double maxDev = 0;
            for (const auto& [sec, beat] : marks)
                maxDev = std::max (maxDev, std::abs (beatAtZero + rate * sec - beat));
            if (maxDev > 1.0 / 64.0)
                ++ctx.report->partialEffects["Warp markers (one tempo per clip)"];
        }
    }
    // content position (beats, or seconds when not warped) → LYNX playback ticks from the sample's start
    const auto contentTicks = [&] (double v) { return warped ? (v - beatAtZero) * ppq : v * bpm / 60.0 * ppq; };
    auto stretch = warped ? std::clamp (rate * 60.0 / bpm, 0.01, 100.0) : 1.0;
    // a warped clip that keeps its pitch, whose file length the set records [DATA: SampleRef DefaultDuration /
    // DefaultSampleRate]: the channel stretches the whole file offline with the clip's warp mode (round 175a, N4:
    // LYNX's Beats engine only runs offline), to the file's length in content beats; the clip then plays it as is
    const auto fileFrames = clip.number ("SampleRef/DefaultDuration", 0), fileRate = clip.number ("SampleRef/DefaultSampleRate", 0);
    // Beats with a transpose: Live resamples each piece (higher and shorter, still on its beat), so the transposition is
    // rendered offline with the stretch, keyed by the pitch, and the clip itself plays untransposed [LIVE: Beats-up5]
    const auto transpose = std::clamp (clip.number ("PitchCoarse", 0) + clip.number ("PitchFine", 0) / 100.0, -48.0, 48.0);
    const auto bakePitch = warped && mode == 0 && transpose != 0.0;
    double fileBeats = 0.0, beatsPitch = 0.0;
    if (warped && mode != 3 && fileFrames > 0 && fileRate > 0 && (std::abs (stretch - 1.0) > 1.0e-9 || bakePitch))
    {
        fileBeats = rate * fileFrames / fileRate;
        stretch = 1.0;
        beatsPitch = bakePitch ? transpose : 0.0;
    }
    const auto channel = audioChannelFor (ctx, path, mixerTrack, mode, warped, fileBeats, beatsPitch);
    const auto loop = loopOf (clip);
    // not warped: the loop values are seconds [DATA: IsWarped=false clips]; the clip length is in beats either way
    auto unwarpedLoop = loop;
    if (! warped)
        for (auto* v : { &unwarpedLoop.start, &unwarpedLoop.end, &unwarpedLoop.loopStart, &unwarpedLoop.loopEnd })
            *v *= bpm / 60.0;
    const auto passes = passesOf (warped ? loop : unwarpedLoop, length);
    for (const auto& pass : passes)
    {
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = channel;
        c.track = row;
        c.start = ticksOf (at + pass.at);
        c.length = std::max<std::int64_t> (1, ticksOf (pass.to - pass.from));
        const auto off = warped ? contentTicks (pass.from) : pass.from * ppq;
        c.offset = (std::int64_t) std::llround (std::max (0.0, off));
        c.offsetFraction = (float) (std::max (0.0, off) - (double) c.offset);
        c.stretch = stretch;
        c.gain = (float) clip.number ("SampleVolume", 1);
        // Re-Pitch ignores the clip's transpose: the tempo alone sets the pitch [LIVE: +5 st on a Re-Pitch clip renders
        // at the original pitch, 2026-11-06]
        c.pitch = (warped && mode == 3) || beatsPitch != 0.0 ? 0.0f : (float) std::clamp (transpose, -12.0, 12.0);
        c.muted = clip.boolean ("Disabled", false);
        if (&pass == &passes.front())
            c.fadeIn = std::clamp (ticksOf (clip.number ("Fades/FadeInLength", 0)), (std::int64_t) 0, c.length);
        if (&pass == &passes.back())
            c.fadeOut = std::clamp (ticksOf (clip.number ("Fades/FadeOutLength", 0)), (std::int64_t) 0, c.length);
        ctx.song.playlist.push_back (c);
    }
}

void addMidiClip (Ctx& ctx, const Element& clip, int row, int channel, double at, double length)
{
    if (length <= 0)
        return;
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = patternFromMidiClip (ctx, clip, channel, length);
    c.track = row;
    c.start = ticksOf (at);
    c.length = ticksOf (length);
    c.muted = clip.boolean ("Disabled", false);
    ctx.song.playlist.push_back (c);
}

// ---------------------------------------------------------------------------------------------------------------
// Automation

float mapValue (ValueMap m, double v)
{
    switch (m)
    {
        case ValueMap::fader: return faderPosition ((float) std::clamp (v <= minusInfGain * 1.0001 ? 0.0 : v, 0.0, (double) maxFaderGain));
        case ValueMap::pan: return (float) std::clamp ((v + 1.0) * 0.5, 0.0, 1.0);
        case ValueMap::tempo: return std::clamp (valueFromTempo (v), 0.0f, 1.0f);
        case ValueMap::plain: break;
    }
    return (float) std::clamp (v, 0.0, 1.0);
}

void addAutomation (Ctx& ctx, const Element& envelopes, const std::string& owner, int& automationRow)
{
    for (const auto& env : envelopes.path ("Envelopes").children ("AutomationEnvelope"))
    {
        const auto id = env.path ("EnvelopeTarget/PointeeId").attr ("Value");
        const auto it = ctx.targets.find (id);
        if (it == ctx.targets.end())
        {
            ctx.note (&FlpImportReport::unsupported, "Automation of a device parameter (" + owner + ")");
            continue;
        }
        std::vector<std::pair<double, double>> events;
        for (const auto& e : env.path ("Automation/Events").children())
            if (e.name() == "FloatEvent")
                events.push_back ({ numberAttr (e, "Time"), numberAttr (e, "Value") });
        if (events.empty())
            continue;
        std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        // the "before the song" point: the value at the start
        for (auto& e : events)
            if (e.first <= beforeSongTime + 1.0)
                e.first = 0.0;
        if (events.size() == 1)
            continue; // a single value: the parameter's own setting already holds it
        Automation a;
        a.name = owner + " " + it->second.name;
        a.target = it->second.target;
        const auto first = std::max (0.0, events.front().first);
        for (const auto& [t, v] : events)
            a.points.push_back ({ ticksOf (std::max (0.0, t) - first), mapValue (it->second.map, v), 0.0f });
        ctx.song.automations.push_back (std::move (a));
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) ctx.song.automations.size() - 1;
        c.track = automationRow++;
        c.start = ticksOf (first);
        c.length = std::max<std::int64_t> (ppq, ctx.song.automations.back().points.back().tick);
        ctx.song.playlist.push_back (c);
    }
}

void registerMixerTargets (Ctx& ctx, const Element& mixer, int insert, const std::string& name)
{
    if (const auto id = targetId (mixer.child ("Volume")); ! id.empty())
        ctx.targets[id] = { AutomationTarget { AutomationTargetKind::mixerVolume, insert, 0, 0 }, ValueMap::fader, "volume" };
    if (const auto id = targetId (mixer.child ("Pan")); ! id.empty())
        ctx.targets[id] = { AutomationTarget { AutomationTargetKind::mixerPan, insert, 0, 0 }, ValueMap::pan, "pan" };
    (void) name;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------

bool alsTimeSignature (int code, int& numerator, int& denominator) noexcept
{
    if (code < 0 || code > 494)
        return false;
    numerator = code % 99 + 1;
    denominator = 1 << (code / 99);
    return true;
}

int alsTimeSignatureCode (int numerator, int denominator) noexcept
{
    int log = 0;
    while ((1 << log) < denominator && log < 4)
        ++log;
    return std::clamp (numerator, 1, 99) - 1 + 99 * log;
}

std::array<std::uint8_t, 16> alsVst3ClassId (const std::array<std::int32_t, 4>& f) noexcept
{
    // COM-compatible FUID memory layout (Windows): l1 little-endian, l2's halves swapped and little-endian, l3 and l4
    // big-endian — the inverse of PluginHost's vst3NormalisedHash grouping
    std::array<std::uint8_t, 16> d {};
    const auto l1 = (std::uint32_t) f[0], l2 = (std::uint32_t) f[1], l3 = (std::uint32_t) f[2], l4 = (std::uint32_t) f[3];
    d[0] = (std::uint8_t) l1; d[1] = (std::uint8_t) (l1 >> 8); d[2] = (std::uint8_t) (l1 >> 16); d[3] = (std::uint8_t) (l1 >> 24);
    d[4] = (std::uint8_t) (l2 >> 16); d[5] = (std::uint8_t) (l2 >> 24); d[6] = (std::uint8_t) l2; d[7] = (std::uint8_t) (l2 >> 8);
    for (int k = 0; k < 4; ++k)
    {
        d[(std::size_t) (8 + k)] = (std::uint8_t) (l3 >> (24 - 8 * k));
        d[(std::size_t) (12 + k)] = (std::uint8_t) (l4 >> (24 - 8 * k));
    }
    return d;
}

std::array<std::int32_t, 4> alsVst3Fields (const std::array<std::uint8_t, 16>& d) noexcept
{
    const auto make = [] (std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t e) { return (a << 24) | (b << 16) | (c << 8) | e; };
    return { (std::int32_t) make (d[3], d[2], d[1], d[0]), (std::int32_t) make (d[5], d[4], d[7], d[6]),
             (std::int32_t) make (d[8], d[9], d[10], d[11]), (std::int32_t) make (d[12], d[13], d[14], d[15]) };
}

std::vector<std::uint8_t> alsHexDecode (const std::string& text)
{
    std::vector<std::uint8_t> out;
    out.reserve (text.size() / 2);
    int hi = -1;
    for (const auto c : text)
    {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        else return {};
        if (hi < 0)
            hi = v;
        else
        {
            out.push_back ((std::uint8_t) (hi * 16 + v));
            hi = -1;
        }
    }
    return hi < 0 ? out : std::vector<std::uint8_t> {};
}

std::string alsHexEncode (const std::vector<std::uint8_t>& bytes, std::size_t lineBytes)
{
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve (bytes.size() * 2 + bytes.size() / std::max<std::size_t> (1, lineBytes) * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (lineBytes > 0 && i > 0 && i % lineBytes == 0)
            out += '\n';
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

// LYNX's own 70-colour approximation of a 14 × 5 hue grid (columns: hues, the last one greys; rows: from light to
// dark). Not Live's colour values: only the hue order, so imported tracks keep their colour families.
std::uint32_t alsColour (int index) noexcept
{
    if (index < 0 || index >= 70)
        return 0;
    const auto col = index % 14, row = index / 14;
    static const float light[5] = { 0.72f, 0.60f, 0.48f, 0.38f, 0.28f };
    static const float sat[5] = { 0.75f, 0.85f, 0.70f, 0.55f, 0.45f };
    const auto l = light[row];
    const auto s = col == 13 ? 0.0f : sat[row];
    const auto h = (float) col / 13.0f * 330.0f; // red → magenta
    const auto c = (1.0f - std::abs (2.0f * l - 1.0f)) * s;
    const auto x = c * (1.0f - std::abs (std::fmod (h / 60.0f, 2.0f) - 1.0f));
    const auto m = l - c / 2.0f;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    const auto to8 = [m] (float v) { return (std::uint32_t) std::clamp ((int) std::lround ((v + m) * 255.0f), 0, 255); };
    return 0xFF000000u | (to8 (r) << 16) | (to8 (g) << 8) | to8 (b);
}

int alsColourIndex (std::uint32_t argb) noexcept
{
    if (argb == 0)
        return -1;
    auto best = 0;
    auto bestD = 1 << 30;
    for (int i = 0; i < 70; ++i)
    {
        const auto c = alsColour (i);
        const auto dr = (int) ((c >> 16) & 255) - (int) ((argb >> 16) & 255), dg = (int) ((c >> 8) & 255) - (int) ((argb >> 8) & 255),
                   db = (int) (c & 255) - (int) (argb & 255);
        if (const auto d = dr * dr + dg * dg + db * db; d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

bool isAlsData (const std::uint8_t* data, std::size_t size)
{
    std::string head;
    if (gzip::isGzip (data, size))
    {
        // only the start is needed: inflate a bounded prefix
        const auto all = gzip::decompress (data, size);
        if (! all)
            return false;
        head.assign (all->begin(), all->begin() + (std::ptrdiff_t) std::min<std::size_t> (all->size(), 512));
    }
    else
        head.assign (data, data + std::min<std::size_t> (size, 512));
    return head.find ("<Ableton") != std::string::npos;
}

namespace
{
bool parseSet (const std::vector<std::uint8_t>& bytes, xml::Document& doc, std::string* error)
{
    std::string text;
    if (gzip::isGzip (bytes.data(), bytes.size()))
    {
        auto raw = gzip::decompress (bytes.data(), bytes.size());
        if (! raw)
        {
            if (error != nullptr)
                *error = "the set is damaged (it could not be decompressed)";
            return false;
        }
        text.assign (raw->begin(), raw->end());
    }
    else
        text.assign (bytes.begin(), bytes.end());
    std::string why;
    if (! doc.parse (std::move (text), &why))
    {
        if (error != nullptr)
            *error = "the set is damaged (" + why + ")";
        return false;
    }
    return true;
}

void collectPlugins (const Element& e, std::vector<FlpPluginUse>& out)
{
    for (const auto& c : e.children())
    {
        if (c.name() == "PluginDevice")
        {
            if (auto p = pluginOf (c))
            {
                const auto desc = c.child ("PluginDesc");
                // VST3 DeviceType 1 = instrument; VST2 Category 2 = kPlugCategSynth [LIVE: 2 / 1 for an effect]
                const auto instrument = (int) desc.number ("Vst3PluginInfo/DeviceType", 0) == 1 || (int) desc.number ("VstPluginInfo/Category", 0) == 2;
                out.push_back ({ std::move (*p), instrument });
            }
            continue;
        }
        collectPlugins (c, out);
    }
}
} // namespace

std::optional<Song> importAls (const std::vector<std::uint8_t>& bytes, const AlsImportOptions& options, std::string* error)
{
    xml::Document doc;
    if (! parseSet (bytes, doc, error))
        return std::nullopt;
    return importAlsXml (doc, options, error);
}

std::vector<FlpPluginUse> alsForeignPlugins (const std::vector<std::uint8_t>& bytes)
{
    std::vector<FlpPluginUse> out;
    xml::Document doc;
    if (parseSet (bytes, doc, nullptr))
        collectPlugins (doc.root(), out);
    return out;
}

std::optional<Song> importAlsXml (const xml::Document& doc, const AlsImportOptions& options, std::string* error)
{
    const auto root = doc.root();
    const auto fail = [error] (const char* why) -> std::optional<Song> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (root.name() != "Ableton" || ! root.child ("LiveSet"))
        return fail ("not a Live set");
    // Live 8 and later write MajorVersion 4 / 5; older sets are not XML at all
    if (const auto major = intOf (root.attr ("MajorVersion"), 0); major < 4)
        return fail ("this Live set version is not supported");

    Ctx ctx (options);
    auto& song = ctx.song;
    song.channels.clear();
    song.patterns.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (0);
    song.mode = PlayMode::song;
    song.arrangements = { Arrangement { "Arrangement", {}, 0, 0, {} } };

    const auto set = root.child ("LiveSet");
    auto main = set.child ("MainTrack"); // Live 12
    if (! main)
        main = set.child ("MasterTrack"); // Live 11 and older [BIN: MasterTrack legacy paths]
    const auto mainMixer = main.path ("DeviceChain/Mixer");
    song.bpm = std::clamp (mainMixer.number ("Tempo/Manual", 120), minTempo, maxTempo);
    if (const auto id = targetId (mainMixer.child ("Tempo")); ! id.empty())
        ctx.targets[id] = { AutomationTarget { AutomationTargetKind::tempo, 0, 0, 0 }, ValueMap::tempo, "tempo" };
    int num = 4, den = 4;
    alsTimeSignature ((int) mainMixer.number ("TimeSignature/Manual", 201), num, den);
    if (den == 4)
        song.beatsPerBar = num;
    else
    {
        Marker m;
        m.tick = 0;
        m.numerator = num;
        m.denominator = den;
        m.action = Marker::timeSignature;
        song.addMarker (m);
    }
    song.mixer[0].volume = mapValue (ValueMap::fader, mainMixer.number ("Volume/Manual", 1));
    song.mixer[0].pan = (float) mainMixer.number ("Pan/Manual", 0);
    registerMixerTargets (ctx, mainMixer, 0, "Main");

    // tracks: one insert each (returns too), in order
    const auto tracks = set.child ("Tracks").children();
    std::vector<int> insertOf (tracks.size(), 0), rowOf (tracks.size(), -1);
    std::map<std::string, int> groupInsert, groupRow;
    std::vector<int> returns;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if ((int) song.mixer.size() >= maxMixerTracks)
            break;
        MixerTrack m;
        m.name = trackName (tracks[i]);
        m.colour = alsColour ((int) tracks[i].number ("Color", -1));
        song.mixer.push_back (std::move (m));
        insertOf[i] = (int) song.mixer.size() - 1;
        const auto kind = tracks[i].name();
        if (kind == "ReturnTrack")
            returns.push_back (insertOf[i]);
        else
        {
            PlaylistTrack row;
            row.name = song.mixer.back().name;
            row.colour = song.mixer.back().colour;
            row.grouped = tracks[i].value ("TrackGroupId", "-1") != "-1";
            row.mixerTrack = insertOf[i];
            row.mode = kind == "AudioTrack" ? PlaylistTrackMode::audio : PlaylistTrackMode::unassigned;
            song.playlistTracks.push_back (row);
            rowOf[i] = (int) song.playlistTracks.size() - 1;
            if (kind == "GroupTrack")
            {
                groupInsert[tracks[i].attr ("Id")] = insertOf[i];
                song.playlistTracks.back().collapsed = ! tracks[i].boolean ("TrackUnfolded", true);
            }
        }
    }

    // mixer settings and routing
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if (insertOf[i] == 0)
            continue;
        const auto& t = tracks[i];
        auto& m = song.mixer[(std::size_t) insertOf[i]];
        const auto mixer = t.path ("DeviceChain/Mixer");
        m.volume = mapValue (ValueMap::fader, mixer.number ("Volume/Manual", 1));
        m.pan = (float) std::clamp (mixer.number ("Pan/Manual", 0), -1.0, 1.0);
        m.muted = ! mixer.boolean ("Speaker/Manual", true);
        m.solo = mixer.boolean ("SoloSink", false);
        registerMixerTargets (ctx, mixer, insertOf[i], m.name);
        const auto out = t.value ("DeviceChain/AudioOutputRouting/Target", "AudioOut/Main");
        const auto group = t.value ("TrackGroupId", "-1");
        if (out == "AudioOut/GroupTrack" && groupInsert.count (group))
        {
            m.toMaster = false;
            m.sends.push_back ({ groupInsert[group], 1.0f });
        }
        else if (out == "AudioOut/None")
            m.toMaster = false;
        else if (out.rfind ("AudioOut/External", 0) == 0)
            ; // a hardware output: LYNX plays it through the Master (the device's outputs are chosen in LYNX)
        const auto sends = mixer.path ("Sends").children ("TrackSendHolder");
        for (std::size_t k = 0; k < sends.size() && k < returns.size(); ++k)
        {
            if (returns[k] == insertOf[i])
                continue;
            const auto level = sends[k].number ("Send/Manual", 0);
            if (const auto id = targetId (sends[k].child ("Send")); ! id.empty())
                ctx.targets[id] = { AutomationTarget { AutomationTargetKind::mixerSend, insertOf[i], returns[k], 0 }, ValueMap::fader, "send" };
            if (level > minusInfGain * 1.0001 && sends[k].boolean ("EnabledByUser", true))
                m.sends.push_back ({ returns[k], (float) std::min (level, (double) maxFaderGain) });
        }
    }

    // channels, devices and arrangement clips
    std::vector<int> channelOf (tracks.size(), -1);
    std::vector<std::vector<DrumPad>> drumPads (tracks.size());
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& t = tracks[i];
        const auto kind = t.name();
        const auto devices = t.path ("DeviceChain/DeviceChain/Devices").children();
        auto& insert = song.mixer[(std::size_t) insertOf[i]];
        if (kind == "MidiTrack")
        {
            ChannelDef c;
            c.name = insert.name;
            c.colour = insert.colour;
            c.volume = 1.0f;
            c.mixerTrack = insertOf[i];
            std::size_t firstEffect = 0;
            for (std::size_t d = 0; d < devices.size(); ++d)
            {
                const auto tag = devices[d].name();
                if (isMidiEffect (tag))
                {
                    ctx.note (&FlpImportReport::unsupported, deviceName (devices[d]));
                    firstEffect = d + 1;
                    continue;
                }
                if (isInstrumentDevice (tag) || tag == "PluginDevice")
                {
                    instrumentFrom (ctx, devices[d], c, &drumPads[i]);
                    firstEffect = d + 1;
                }
                break;
            }
            song.channels.push_back (std::move (c));
            channelOf[i] = (int) song.channels.size() - 1;
            effectsFrom (ctx, { devices.begin() + (std::ptrdiff_t) firstEffect, devices.end() }, insert);
            song.playlistTracks[(std::size_t) rowOf[i]].mode = PlaylistTrackMode::instrument;
            song.playlistTracks[(std::size_t) rowOf[i]].channel = channelOf[i];
            for (const auto& clip : t.path ("DeviceChain/MainSequencer/ClipTimeable/ArrangerAutomation/Events").children ("MidiClip"))
            {
                const auto start = clip.number ("CurrentStart", numberAttr (clip, "Time"));
                addMidiClip (ctx, clip, rowOf[i], channelOf[i], start, clip.number ("CurrentEnd", start) - start);
            }
        }
        else
        {
            effectsFrom (ctx, devices, insert);
            if (kind == "AudioTrack")
                for (const auto& clip : t.path ("DeviceChain/MainSequencer/Sample/ArrangerAutomation/Events").children ("AudioClip"))
                {
                    const auto start = clip.number ("CurrentStart", numberAttr (clip, "Time"));
                    addAudioClip (ctx, clip, rowOf[i], insertOf[i], start, clip.number ("CurrentEnd", start) - start, insert.name);
                }
        }
        if (! t.path ("TakeLanes/TakeLanes").children ("TakeLane").empty())
            ctx.note (&FlpImportReport::unsupported, "Take lanes (only the main lane is kept)");
    }

    // Session clips: after the arrangement, one section per scene [the user's choice, 2026-11-06]
    std::int64_t end = 0;
    for (const auto& c : song.playlist)
        end = std::max (end, c.end());
    const auto bar = den == 4 ? (std::int64_t) song.beatsPerBar * ppq : (std::int64_t) num * ppq * 4 / den;
    const auto roundUp = [bar] (std::int64_t t) { return bar > 0 ? (t + bar - 1) / bar * bar : t; };
    auto cursor = roundUp (end);
    const auto scenes = set.child ("Scenes").children ("Scene");
    for (std::size_t s = 0; s < std::max<std::size_t> (scenes.size(), 1); ++s)
    {
        double longest = 0;
        struct Slot
        {
            std::size_t track;
            Element clip;
        };
        std::vector<Slot> slots;
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const auto list = tracks[i].path ("DeviceChain/MainSequencer/ClipSlotList").children ("ClipSlot");
            if (s >= list.size())
                continue;
            const auto value = list[s].path ("ClipSlot/Value");
            for (const auto& clip : value.children())
                if (clip.name() == "MidiClip" || clip.name() == "AudioClip")
                {
                    slots.push_back ({ i, clip });
                    longest = std::max (longest, clip.number ("CurrentEnd", 0) - clip.number ("CurrentStart", 0));
                }
        }
        if (slots.empty())
            continue;
        const auto at = (double) cursor / ppq;
        for (const auto& [i, clip] : slots)
        {
            const auto length = clip.number ("CurrentEnd", 0) - clip.number ("CurrentStart", 0);
            if (clip.name() == "MidiClip" && channelOf[i] >= 0)
                addMidiClip (ctx, clip, rowOf[i], channelOf[i], at, length);
            else if (clip.name() == "AudioClip" && rowOf[i] >= 0)
                addAudioClip (ctx, clip, rowOf[i], insertOf[i], at, length, song.mixer[(std::size_t) insertOf[i]].name);
        }
        Marker m;
        m.tick = cursor;
        m.name = s < scenes.size() ? scenes[s].value ("Name") : std::string();
        if (m.name.empty())
            m.name = "Scene " + std::to_string (s + 1);
        song.addMarker (m);
        cursor = roundUp (cursor + std::max<std::int64_t> (bar, ticksOf (longest)));
        ctx.note (&FlpImportReport::approximated, "Session clips (placed after the arrangement, one section per scene)");
    }

    // drum racks: one sampler channel per pad (as an FL drum sampler); later channels shift, so from the last one
    for (std::size_t i = tracks.size(); i-- > 0;)
        if (channelOf[i] >= 0 && ! drumPads[i].empty())
            splitDrumChannel (song, channelOf[i], drumPads[i]);

    // locators
    for (const auto& loc : set.path ("Locators/Locators").children ("Locator"))
    {
        Marker m;
        m.tick = ticksOf (std::max (0.0, loc.number ("Time", 0)));
        m.name = loc.value ("Name");
        song.addMarker (m);
    }
    // the arrangement loop
    if (const auto tr = set.child ("Transport"); tr.boolean ("LoopOn", false))
    {
        song.loopStart = ticksOf (tr.number ("LoopStart", 0));
        song.loopEnd = song.loopStart + ticksOf (tr.number ("LoopLength", 0));
    }

    // automation (rows under the tracks)
    int automationRow = (int) song.playlistTracks.size();
    addAutomation (ctx, main.child ("AutomationEnvelopes"), "Main", automationRow);
    for (std::size_t i = 0; i < tracks.size(); ++i)
        addAutomation (ctx, tracks[i].child ("AutomationEnvelopes"), song.mixer[(std::size_t) insertOf[i]].name, automationRow);

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
