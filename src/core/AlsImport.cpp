#include "core/AlsImport.h"
#include "core/AlsDevices.h"
#include "core/Effects.h"
#include "core/Rack.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

#include "core/ClipLauncher.h"
#include "core/DynamicsSuite.h"
#include "core/FlWrapper.h"
#include "core/Gzip.h"
#include "core/LiveScales.h"
#include "core/MissingFiles.h"
#include "core/SessionOps.h"
#include "core/TimeStretch.h"
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
    // (round 190cf) a parameter of a Live device that became a LYNX effect: Live's own unit → LYNX's 0..1
    std::function<float (double)> law;
};

// (round 190ed) an automated parameter inside a rack, or one of a rack's own macros, kept until the rack has its place
// (an insert's slot or a channel): then a macro of that rack carries the envelope (core/Rack.h macros are the slot's
// automatable parameters 0 … 15)
struct RackAuto
{
    std::string id;         // the parameter's AutomationTarget Id
    std::vector<int> path;  // rack nodes from the rack being read down to the document that holds the target
    int node = -1;          // −1: macro `param` of that document; else the node whose parameter `param` the envelope moves
    int param = 0;
    std::function<float (double)> law; // Live's value → the target's 0..1
    float current = 0.0f;   // the target's 0..1 as imported (a new macro starts there: no change in sound)
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
    // round 190bq (Live's own devices → LYNX's): a track's Id → its insert (a compressor's side-chain input), the key
    // shift of the instrument just read (a Simpler in Slice mode) and of each channel, and whether the instrument
    // being read sits on a drum pad (a pad keeps the sample, tuning, level and pan only)
    std::map<int, int> insertOfTrack;
    int pendingKeyShift = 0;
    std::map<int, int> keyShift;
    bool inDrumPad = false;
    // round 190cf: Live's MIDI effects written into a channel's notes (in the chain's order), and how many leading
    // MIDI effects of the next rack read were already taken by the track's channel (a one-chain Instrument Rack)
    std::map<int, std::vector<alsdev::NoteEffect>> noteEffects;
    int skipLeadingMidi = 0;
    std::string signatureTarget; // the main track's TimeSignature AutomationTarget Id (round 190cg)
    // round 190ed: every AutomationTarget Id an arrangement envelope points at; where the rack being read collects its
    // automated parameters; targets on the channel being read (their index is set when the channel is placed)
    std::set<std::string> automated;
    std::vector<RackAuto>* rackAutos = nullptr;
    std::vector<std::pair<std::string, Target>> channelTargets;
    std::set<std::string> explained; // envelopes already listed with their own reason
    // round 190ed: a Peak Controller linked to a translated effect's parameter (Auto Filter's LFO / envelope follower),
    // placed once its slots are known: the controller's insert and slot (−1 = appended to that insert after every
    // track is read: a side-chain source), the effect's insert and slot
    struct PendingLink
    {
        int sourceInsert = -1, sourceSlot = -1;
        EffectSlot controller;
        int targetTrack = -1, targetSlot = -1, param = 0;
        std::vector<float> curve;
        std::vector<std::string> notes;
        std::string from, what;
    };
    std::vector<PendingLink> pendingLinks;

    // the scale of the first MIDI clip that has Live's clip scale switched on (round 190o)
    std::optional<projectkey::Key> clipKey;
    bool clipKeySeen = false;

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

bool instrumentFrom (Ctx& ctx, const Element& device, ChannelDef& channel, std::vector<DrumPad>* pads, int depth = 0);

// ---------------------------------------------------------------------------------------------------------------
// Racks (round 183c, docs/analise/REA_rodada183c.md §1–5 [DATA]): Live's Audio Effect / Instrument Racks become LYNX
// racks (a Patcher in rack mode, core/Rack.h) with their chains, zones, chain mixers, macros, variations and the macro
// mappings onto the devices LYNX keeps (plugins, nested racks, the chains' volume / pan, the chain selector). Live's own
// devices inside a chain are dropped and listed, as on a track.

// A macro mapping stored on a parameter: KeyMidi on channel 16, NoteOrController = the macro, MidiControllerRange =
// the range in the parameter's own units [DATA]. nullopt = not mapped.
struct LiveMapping
{
    int macro = 0;
    double min = 0.0, max = 1.0;
};
std::optional<LiveMapping> mappingOf (const Element& parameter)
{
    const auto key = parameter.child ("KeyMidi");
    if (! key || (int) key.number ("Channel", -1) != 16)
        return std::nullopt;
    LiveMapping m;
    m.macro = (int) key.number ("NoteOrController", -1);
    if (m.macro < 0 || m.macro >= rackMacroCount)
        return std::nullopt;
    m.min = parameter.number ("MidiControllerRange/Min", 0.0);
    m.max = parameter.number ("MidiControllerRange/Max", 1.0);
    return m;
}

void mapInto (PatcherDoc& doc, const LiveMapping& m, RackMapping target, float min, float max)
{
    target.min = std::clamp (min, 0.0f, 1.0f);
    target.max = std::clamp (max, 0.0f, 1.0f);
    rack::map (doc, m.macro, target);
}

// The plugin parameters of a PluginDevice that a macro drives (VST2: ParameterId is the parameter's index [model];
// VST3 ids are not indices: listed, not mapped)
void pluginMappings (Ctx& ctx, const Element& device, PatcherDoc& doc, int node)
{
    const auto vst3 = (bool) device.path ("PluginDesc/Vst3PluginInfo");
    for (const auto& p : device.path ("ParameterList").children ("PluginFloatParameter"))
        if (const auto m = mappingOf (p.child ("ParameterValue")))
        {
            if (vst3)
            {
                ctx.note (&FlpImportReport::approximated, "Macro mapping on a VST3 parameter (not kept)");
                continue;
            }
            RackMapping target;
            target.node = node;
            target.param = (int) p.number ("ParameterId", -1);
            if (target.param >= 0)
                mapInto (doc, *m, target, (float) m->min, (float) m->max);
        }
}

// (round 190ed) The envelopes on a plugin's parameters. Live keeps a VST2 parameter as 0..1 [DATA, model] and its
// ParameterId is its index [model], which LYNX's plugin parameters use too. (round 190ek) A VST3's ParameterId is the
// plugin's own ParamID and its envelope holds the parameter's plain value, between the MidiControllerRange Live saves
// for it [MEASURED in a Live 12.4 save, round 190ei: kHs Gain's "Gain" ParamID 1, range 0.0316 … 31.62, envelope points
// 0.2486 … 28.52; "mode" ParamID 414, 0 … 1; 126 unused entries ParameterId −1]. Such a parameter is carried as slot k
// = its place among the device's parameters with an id (FL's VST3 automation slots, core/FlWrapper.h, which the plugin
// host turns into the plugin's own order when it opens it: `vst3Slots` gets the ParamIDs), its value mapped linearly over
// that range (exact for 0 … 1, the usual range; another range is listed as approximate — the plugin's own curve
// between the ends is known only to the plugin). `inRack`: the host does not remap a plugin inside a rack: listed there.
void pluginAutos (Ctx& ctx, const Element& device, const std::string& plugin,
                  const std::function<void (const std::string& id, int param, float current, const std::string& name, std::function<float (double)> law)>& sink,
                  std::vector<std::int32_t>* vst3Slots = nullptr, bool inRack = false)
{
    const auto vst3 = (bool) device.path ("PluginDesc/Vst3PluginInfo");
    int slot = 0;
    for (const auto& p : device.path ("ParameterList").children ("PluginFloatParameter"))
    {
        const auto paramId = (std::int64_t) p.number ("ParameterId", -1);
        const auto mySlot = paramId >= 0 ? slot++ : -1;
        const auto id = targetId (p.child ("ParameterValue"));
        if (id.empty() || ctx.automated.count (id) == 0)
            continue;
        auto name = p.value ("ParameterName");
        if (! vst3)
        {
            if (paramId < 0)
                continue;
            sink (id, (int) paramId, (float) std::clamp (p.number ("ParameterValue/Manual", 0.0), 0.0, 1.0),
                  plugin + " " + (name.empty () ? "#" + std::to_string (paramId) : name), [] (double v) { return (float) std::clamp (v, 0.0, 1.0); });
            continue;
        }
        if (inRack || vst3Slots == nullptr || mySlot < 0)
        {
            ctx.note (&FlpImportReport::unsupported, "Automation of a VST3 plugin's parameter inside a rack (" + plugin + ": LYNX maps a VST3's own parameter ids on inserts and channels)");
            ctx.explained.insert (id);
            continue;
        }
        const auto lo = p.number ("ParameterValue/MidiControllerRange/Min", 0.0), hi = p.number ("ParameterValue/MidiControllerRange/Max", 1.0);
        const auto span = std::abs (hi - lo) > 1.0e-12 ? hi - lo : 1.0;
        if (std::abs (lo) > 1.0e-6 || std::abs (hi - 1.0) > 1.0e-6)
            ctx.note (&FlpImportReport::approximated, "Automation of a VST3 parameter with its own range (" + plugin + " " + name
                                                          + ": Live's plain values mapped linearly between the range's ends)");
        if ((std::size_t) mySlot >= vst3Slots->size())
            vst3Slots->resize ((std::size_t) mySlot + 1, -1);
        (*vst3Slots)[(std::size_t) mySlot] = (std::int32_t) paramId;
        const auto law = [lo, span] (double v) { return (float) std::clamp ((v - lo) / span, 0.0, 1.0); };
        sink (id, mySlot, law (p.number ("ParameterValue/Manual", lo)), plugin + " " + (name.empty () ? "#" + std::to_string (paramId) : name), law);
    }
    // every parameter with an id keeps its slot (the host gives the slots it does not list the next free ones)
    if (vst3 && vst3Slots != nullptr && ! vst3Slots->empty())
    {
        slot = 0;
        for (const auto& p : device.path ("ParameterList").children ("PluginFloatParameter"))
            if (const auto paramId = (std::int64_t) p.number ("ParameterId", -1); paramId >= 0)
            {
                if ((std::size_t) slot >= vst3Slots->size())
                    vst3Slots->resize ((std::size_t) slot + 1, -1);
                (*vst3Slots)[(std::size_t) slot++] = (std::int32_t) paramId;
            }
    }
}

// (round 190ek) The automation slots of a VST3 as FL's record 4 (u32 N + N × i32 ParamID) in its state (FL's record
// layout, which pluginOf writes), read by the plugin host to put each automation on the plugin's own parameter
void appendVst3Slots (ForeignPlugin& plugin, const std::vector<std::int32_t>& slots)
{
    if (slots.empty() || plugin.format != ForeignPlugin::Format::vst3 || plugin.state.size() < 4)
        return;
    auto put32 = [&plugin] (std::uint32_t v) { for (int k = 0; k < 4; ++k) plugin.state.push_back ((std::uint8_t) (v >> (8 * k))); };
    put32 (4);
    const auto size = (std::uint64_t) (4 + 4 * slots.size());
    for (int k = 0; k < 8; ++k)
        plugin.state.push_back ((std::uint8_t) (size >> (8 * k)));
    put32 ((std::uint32_t) slots.size());
    for (const auto id : slots)
        put32 ((std::uint32_t) id);
}

// (round 190ed) The macro of rack `doc` that drives a target `path` leads to from `depth`: the target itself when it is
// a macro of `doc`, else a free macro of `doc` (no mapping, no name of its own, not under an envelope itself) mapped
// over the target's whole range and set to the target's value (nothing moves; Rand and variations leave it alone).
// A target in a nested rack goes through a free macro of each rack on the way. −1 when no macro is free.
int bindMacro (PatcherDoc& doc, const std::vector<RackAuto>& all, const RackAuto& a, std::size_t depth)
{
    auto* r = rack::rackOf (doc);
    if (r == nullptr)
        return -1;
    if (depth == a.path.size() && a.node < 0)
        return a.param >= 0 && a.param < rackMacroCount ? a.param : -1;
    RackMapping target;
    auto value = a.current;
    if (depth == a.path.size())
    {
        target.node = a.node;
        target.param = a.param;
    }
    else
    {
        const auto n = a.path[depth];
        if (n < 0 || n >= (int) doc.nodes.size())
            return -1;
        auto& node = doc.nodes[(std::size_t) n];
        auto& inner = node.generator ? node.channel.patcher : node.effect.patcher;
        const auto k = bindMacro (inner, all, a, depth + 1);
        if (k < 0 || rack::rackOf (inner) == nullptr)
            return -1;
        target.node = n;
        target.param = k;
        value = rack::rackOf (inner)->macros[(std::size_t) k].value / 127.0f;
        r = rack::rackOf (doc);
    }
    // a second envelope reaching the same target uses the macro already bound to it
    for (int k = 0; k < rackMacroCount; ++k)
        for (const auto& m : r->macros[(std::size_t) k].mappings)
            if (m.target == RackMapping::Target::param && m.node == target.node && m.param == target.param && m.min == 0.0f && m.max == 1.0f)
                return k;
    std::set<int> reserved;
    for (const auto& other : all)
        if (other.node < 0 && other.path.size() == depth && std::equal (other.path.begin(), other.path.end(), a.path.begin()))
            reserved.insert (other.param);
    for (int k = 0; k < rackMacroCount; ++k)
    {
        auto& m = r->macros[(std::size_t) k];
        if (! m.mappings.empty() || ! m.name.empty() || reserved.count (k) != 0)
            continue;
        target.min = 0.0f;
        target.max = 1.0f;
        if (! rack::map (doc, k, target))
            return -1;
        r = rack::rackOf (doc);
        auto& bound = r->macros[(std::size_t) k];
        bound.name = a.name;
        bound.excludeFromRandom = bound.excludeFromVariations = true;
        while (r->shownMacros <= k && r->shownMacros < rackMacroCount)
            r->shownMacros = rack::moreMacros (r->shownMacros);
        rack::setMacro (doc, k, std::clamp (value, 0.0f, 1.0f) * 127.0f);
        rack::compile (doc);
        return k;
    }
    return -1;
}

// (round 190ed) A rack has its place: each envelope it collected targets the rack's macro that carries it
void placeRackAutos (Ctx& ctx, PatcherDoc& doc, const std::vector<RackAuto>& autos, const std::function<AutomationTarget (int macro)>& at,
                     bool onChannel = false)
{
    for (const auto& a : autos)
    {
        const auto k = bindMacro (doc, autos, a, 0);
        if (k < 0)
        {
            ctx.note (&FlpImportReport::unsupported, "Automation inside a rack whose macros are all in use (" + a.name + ")");
            ctx.explained.insert (a.id);
            continue;
        }
        if (onChannel)
            ctx.channelTargets.push_back ({ a.id, Target { at (k), ValueMap::plain, a.name, a.law } });
        else
            ctx.targets[a.id] = { at (k), ValueMap::plain, a.name, a.law };
    }
}

bool rackFrom (Ctx& ctx, const Element& device, PatcherDoc& doc, int depth);

// One of Live's own audio effects as the LYNX effect in its role, settings translated (round 190bq, core/AlsDevices.h).
// The report lists the pair — under "converted" when the maths is the same, under "kept but approximated" when the
// pair is fitted — and every setting that is clamped, fitted or has no counterpart. False: not a device LYNX maps (or
// its element lacks the parameters): the caller drops and lists it as before.
bool nativeEffectFrom (Ctx& ctx, const Element& d, EffectSlot& slot, std::vector<alsdev::ParamLink>* links = nullptr, std::string* name = nullptr,
                       std::optional<alsdev::Translated::Modulator>* modulator = nullptr)
{
    alsdev::Context context;
    context.bpm = ctx.song.bpm;
    auto t = alsdev::effectFromLiveDevice (d, context);
    if (! t)
        return false;
    // (round 190ed) a modulation from outside the effect (Auto Filter's LFO / envelope follower): the caller places
    // its Peak Controller and link; inside a rack chain no link can be made, so it is listed
    if (t->modulator)
    {
        if (modulator != nullptr)
            *modulator = std::move (t->modulator);
        else
            t->notes.dropped.push_back (t->modulator->what + " on the cutoff inside a rack");
    }
    if (links != nullptr)
        *links = t->links;
    if (name != nullptr)
        *name = t->from;
    if (t->side.on)
    {
        const auto it = t->side.trackId >= 0 ? ctx.insertOfTrack.find (t->side.trackId) : ctx.insertOfTrack.end();
        if (it != ctx.insertOfTrack.end())
        {
            t->slot.sidechain = it->second;
            if (t->side.otherTap)
                t->notes.approximated.push_back ("side-chain input taken after the source track's fader");
        }
        else
        {
            if (t->sideParam >= 0)
                t->slot.params[(std::size_t) t->sideParam] = 0.0f;
            t->notes.dropped.push_back ("side-chain input (its source is not a track of the set; the device hears its own input)");
        }
    }
    const auto pair = t->from + " -> " + t->to;
    if (t->approximate)
        ctx.note (&FlpImportReport::partialEffects, pair + " (settings fitted to LYNX's engine)");
    else
        ctx.note (&FlpImportReport::converted, pair);
    for (const auto& n : t->notes.approximated)
        ctx.note (&FlpImportReport::partialEffects, t->from + ": " + n);
    for (const auto& n : t->notes.dropped)
        ctx.note (&FlpImportReport::partialEffects, t->from + ": " + n + " (not applied)");
    slot = std::move (t->slot);
    return true;
}

// A Live member name as words: "FormantShift" → "Formant Shift"
std::string spacedName (std::string_view tag)
{
    std::string out;
    for (std::size_t i = 0; i < tag.size(); ++i)
    {
        const auto c = tag[i];
        const auto upper = c >= 'A' && c <= 'Z';
        const auto prevLower = i > 0 && ((tag[i - 1] >= 'a' && tag[i - 1] <= 'z') || (tag[i - 1] >= '0' && tag[i - 1] <= '9'));
        if (upper && prevLower)
            out += ' ';
        out += c;
    }
    return out;
}

// One device of a rack chain → a Patcher node (−1 = dropped and listed)
int rackDeviceFrom (Ctx& ctx, const Element& d, PatcherDoc& doc, int chain, bool instrumentRack, int depth, bool* native = nullptr,
                    std::set<std::string>* mappedLinks = nullptr)
{
    const auto tag = std::string (d.name());
    PatchNode node;
    if (tag == "PluginDevice")
    {
        const auto p = pluginOf (d);
        if (! p)
            return -1;
        // in an Instrument Rack chain, the chain's instrument: a VST3 Live marks as one (DeviceType 1 [DATA]) or a VST2
        // with no audio input (NumAudioInputs 0 [DATA]), when the chain has none yet
        const auto chainNodes = rack::devices (*rack::rackOf (doc), chain);
        const auto hasInstrument = std::any_of (chainNodes.begin(), chainNodes.end(), [&doc] (int n) { return doc.nodes[(std::size_t) n].generator; });
        const auto asInstrument = instrumentRack && ! hasInstrument
                                  && ((int) d.number ("PluginDesc/Vst3PluginInfo/DeviceType", 0) == 1
                                      || ((bool) d.path ("PluginDesc/VstPluginInfo") && (int) d.number ("PluginDesc/VstPluginInfo/NumAudioInputs", 2) == 0));
        if (asInstrument)
        {
            node.generator = true;
            node.channel.instrument = InstrumentKind::plugin;
            node.channel.plugin.name = p->name;
            node.channel.plugin.foreign = *p;
            node.channel.name = p->name;
        }
        else
        {
            node.effect.kind = EffectKind::plugin;
            node.effect.enabled = d.boolean ("On/Manual", true);
            node.effect.plugin.name = p->name;
            node.effect.plugin.foreign = *p;
        }
        ctx.note (&FlpImportReport::converted, "Plugin: " + p->name);
        const auto n = rack::addDevice (doc, chain, std::move (node));
        if (n >= 0)
        {
            pluginMappings (ctx, d, doc, n);
            pluginAutos (
                ctx, d, p->name,
                [&ctx, n] (const std::string& id, int param, float current, const std::string& name, std::function<float (double)> law) {
                    if (ctx.rackAutos != nullptr)
                        ctx.rackAutos->push_back ({ id, {}, n, param, std::move (law), current, name });
                },
                nullptr, true);
        }
        return n;
    }
    if ((tag == "AudioEffectGroupDevice" || tag == "InstrumentGroupDevice") && depth < 8)
    {
        auto inner = rack::makeRack (tag == "InstrumentGroupDevice" ? RackDoc::Kind::instrument : RackDoc::Kind::audioEffect);
        // (round 190ed) the inner rack's automated targets: reached from here through this node
        std::vector<RackAuto> innerAutos;
        auto* const outerAutos = std::exchange (ctx.rackAutos, &innerAutos);
        const auto read = rackFrom (ctx, d, inner, depth + 1);
        ctx.rackAutos = outerAutos;
        if (! read)
            return -1;
        if (tag == "InstrumentGroupDevice")
        {
            node.generator = true;
            node.channel.instrument = InstrumentKind::patcher;
            node.channel.name = d.value ("UserName");
            node.channel.patcher = std::move (inner);
        }
        else
        {
            node.effect = makeEffectSlot (EffectKind::patcher);
            node.effect.enabled = d.boolean ("On/Manual", true);
            node.effect.patcher = std::move (inner);
        }
        node.label = d.value ("UserName");
        const auto n = rack::addDevice (doc, chain, std::move (node));
        if (n < 0)
            return -1;
        if (ctx.rackAutos != nullptr)
            for (auto& a : innerAutos)
            {
                a.path.insert (a.path.begin(), n);
                ctx.rackAutos->push_back (std::move (a));
            }
        // the inner rack's macros mapped onto the outer rack's (0…127 → 0..1) [DATA]
        for (int k = 0; k < rackMacroCount; ++k)
            if (const auto m = mappingOf (d.child ("MacroControls." + std::to_string (k))))
            {
                RackMapping target;
                target.node = n;
                target.param = k;
                mapInto (doc, *m, target, (float) (m->min / 127.0), (float) (m->max / 127.0));
            }
        return n;
    }
    if (instrumentRack && isInstrumentDevice (tag))
    {
        ChannelDef channel;
        channel.name = d.value ("UserName");
        if (instrumentFrom (ctx, d, channel, nullptr, depth + 1))
        {
            node.generator = true;
            node.channel = std::move (channel);
            return rack::addDevice (doc, chain, std::move (node));
        }
        return -1;
    }
    if (isMidiEffect (tag))
    {
        ctx.note (&FlpImportReport::unsupported, deviceName (d));
        return -1;
    }
    // (round 190bq) Live's own effects LYNX has an engine for: the LYNX effect in the chain
    std::vector<alsdev::ParamLink> links;
    std::string from;
    if (! isInstrumentDevice (tag) && nativeEffectFrom (ctx, d, node.effect, &links, &from))
    {
        node.label = d.value ("UserName");
        const auto n = rack::addDevice (doc, chain, std::move (node));
        if (n >= 0 && native != nullptr)
            *native = true;
        // (round 190ed) envelopes on its parameters: carried by a macro of the rack once the rack has its place
        if (n >= 0 && ctx.rackAutos != nullptr)
            for (const auto& l : links)
                if (const auto id = targetId (d.path (l.live)); ! id.empty() && ctx.automated.count (id) != 0)
                    ctx.rackAutos->push_back ({ id, {}, n, l.param, l.value, doc.nodes[(std::size_t) n].effect.params[(std::size_t) l.param],
                                                from + " " + spacedName (pathFileName (l.live)) });
        // (round 190cf) the rack's macros drive the LYNX effect's parameters over the range the mapping holds, each
        // end through the parameter's law (between the ends the macro moves LYNX's control in a straight line)
        if (n >= 0)
            for (const auto& l : links)
                if (const auto m = mappingOf (d.path (l.live)))
                {
                    RackMapping target;
                    target.node = n;
                    target.param = l.param;
                    mapInto (doc, *m, target, l.value (m->min), l.value (m->max));
                    if (mappedLinks != nullptr)
                        mappedLinks->insert (l.live);
                }
        if (n >= 0)
            return n;
    }
    ctx.note (&FlpImportReport::droppedEffects, deviceName (d));
    return -1;
}

RackZone zoneOf (const Element& range, RackZone fallback)
{
    if (! range)
        return fallback;
    RackZone z;
    z.lo = std::clamp ((int) range.number ("Min", fallback.lo), 0, 127);
    z.hi = std::clamp ((int) range.number ("Max", fallback.hi), z.lo, 127);
    z.fadeLo = std::clamp ((int) range.number ("CrossfadeMin", z.lo), z.lo, z.hi);
    z.fadeHi = std::clamp ((int) range.number ("CrossfadeMax", z.hi), z.fadeLo, z.hi);
    return z;
}

// A Drum Rack pad (round 190ec, docs/analise/ALS_FORMAT.md §7): DrumBranch/BranchInfo {ReceivingNote = 128 − the key, 0 = all
// notes; SendingNote = the key played; ChokeGroup 0 none, 1…16} [DATA: the shipped kits, pad names against clip notes],
// the chain's sends (MixerDevice/SendInfos/AudioBranchSendInfo {Index, Send (linear gain, −70 dB floor = off), EnabledByUser})
// and its Audio To (RoutingHelper/Routable/Target "…/DeviceIn.<d>.R<k>,ChainIn" = return chain k; "AudioOut/None" = the
// rack's output) [DATA: Drums Lesson routes all 16 pads into "a Mix Bus" that way]
void drumPadFrom (const Element& branch, RackChain& chain, std::vector<std::vector<std::pair<int, RackSend>>>& sends, std::vector<int>& audioTo)
{
    const auto receiving = (int) branch.number ("BranchInfo/ReceivingNote", 128 - rack::drumFirstPad);
    chain.receive = receiving >= 1 && receiving <= 128 ? 128 - receiving : -1;
    chain.play = std::clamp ((int) branch.number ("BranchInfo/SendingNote", rack::drumPlayNote), 0, 127);
    chain.choke = std::clamp ((int) branch.number ("BranchInfo/ChokeGroup", 0), 0, rack::drumChokeGroups);
    std::vector<std::pair<int, RackSend>> mine;
    for (const auto& info : branch.path ("MixerDevice/SendInfos").children ("AudioBranchSendInfo"))
    {
        RackSend x;
        const auto level = info.number ("Send/Manual", 0.0);
        x.level = level > 0.000317 ? (float) std::clamp (level, 0.0, 1.0) : 0.0f; // (Live's floor, −70 dB, is −inf)
        x.on = info.boolean ("EnabledByUser", true);
        mine.emplace_back ((int) info.number ("Index", (double) mine.size()), x);
    }
    sends.push_back (std::move (mine));
    const auto target = branch.value ("MixerDevice/RoutingHelper/Routable/Target");
    int to = -1;
    if (const auto at = target.rfind (".R"); at != std::string::npos && target.find (",ChainIn", at) != std::string::npos)
        to = std::atoi (target.c_str() + at + 2);
    audioTo.push_back (to);
}

void drumReturnsFrom (Ctx& ctx, const Element& device, PatcherDoc& doc, int depth)
{
    for (const auto& branch : device.path ("ReturnBranches").children())
    {
        auto name = branch.value ("Name/UserName");
        if (name.empty()) // (Live's EffectiveName carries the letter it shows, "a Mix Bus": LYNX adds it itself)
        {
            name = branch.value ("Name/EffectiveName");
            const auto* before = rack::rackOf (doc);
            const auto letter = rack::returnLetter (before != nullptr ? (int) before->returns.size() : 0);
            if (name == letter)
                name.clear();
            else if (name.size() > 2 && name.compare (0, 2, letter + " ") == 0)
                name = name.substr (2);
        }
        const auto k = rack::addReturn (doc, name);
        auto* r = rack::rackOf (doc);
        if (k < 0)
            return;
        auto& ret = r->returns[(std::size_t) k];
        ret.active = branch.boolean ("MixerDevice/Speaker/Manual", true);
        ret.solo = branch.boolean ("IsSoloed", false);
        ret.volume = (float) std::clamp (branch.number ("MixerDevice/Volume/Manual", 1.0), 0.0, 2.0);
        ret.pan = (float) std::clamp (branch.number ("MixerDevice/Panorama/Manual", 0.0), -1.0, 1.0);
        const auto slot = (int) r->chains.size() + k;
        for (const auto& d : branch.path ("DeviceChain/AudioToAudioDeviceChain/Devices").children())
            rackDeviceFrom (ctx, d, doc, slot, false, depth, nullptr, nullptr);
        if (mappingOf (branch.path ("MixerDevice/Volume")) || mappingOf (branch.path ("MixerDevice/Panorama")))
            ctx.note (&FlpImportReport::approximated, "Drum rack: a macro on a return chain's volume or pan (not applied)");
    }
}

// Fills a new rack document from a Live rack device (the document comes from rack::makeRack: its first chain is replaced;
// from rack::makeDrumRack for a Drum Rack)
bool rackFrom (Ctx& ctx, const Element& device, PatcherDoc& doc, int depth)
{
    auto* r = rack::rackOf (doc);
    if (r == nullptr)
        return false;
    const auto instrumentRack = r->kind == RackDoc::Kind::instrument;
    std::array<std::vector<std::string>, rackMacroCount> droppedTargets;
    auto skipMidi = std::exchange (ctx.skipLeadingMidi, 0); // (round 190cf) only for the rack this call reads
    // a Drum Rack (round 190ec): its pads' Simplers play their Play note, so a Slice-mode Simpler on a pad must not move
    // the track's notes (the key shift is the track channel's own)
    const auto drum = r->drum;
    const auto keyShiftBefore = ctx.pendingKeyShift;
    std::vector<std::vector<std::pair<int, RackSend>>> drumSends; // [chain] (return index, send)
    std::vector<int> drumAudioTo;
    while (! r->chains.empty())
    {
        rack::removeChain (doc, 0);
        r = rack::rackOf (doc);
    }
    for (const auto& branch : device.path ("Branches").children())
    {
        auto name = branch.value ("Name/UserName");
        if (name.empty())
            name = branch.value ("Name/EffectiveName");
        const auto c = rack::addChain (doc, name.empty() ? "Chain" : name);
        r = rack::rackOf (doc);
        auto& chain = r->chains[(std::size_t) c];
        chain.active = branch.boolean ("MixerDevice/Speaker/Manual", true);
        chain.solo = branch.boolean ("IsSoloed", false);
        chain.volume = (float) std::clamp (branch.number ("MixerDevice/Volume/Manual", 1.0), 0.0, 2.0);
        chain.pan = (float) std::clamp (branch.number ("MixerDevice/Panorama/Manual", 0.0), -1.0, 1.0);
        chain.select = zoneOf (branch.child ("BranchSelectorRange"), RackZone { 0, 0, 0, 0 });
        chain.key = zoneOf (branch.path ("ZoneSettings/KeyRange"), RackZone {});
        chain.velocity = zoneOf (branch.path ("ZoneSettings/VelocityRange"), RackZone { 1, 127, 1, 127 });
        if (drum)
            drumPadFrom (branch, chain, drumSends, drumAudioTo);
        // the chain's devices (Live's chain kinds: AudioToAudio, MidiToAudio, MidiToMidi) [DATA]
        for (const auto* list : { "DeviceChain/AudioToAudioDeviceChain/Devices", "DeviceChain/MidiToAudioDeviceChain/Devices" })
            for (const auto& d : branch.path (list).children())
            {
                // (round 190cf) the leading MIDI effects the track's channel already took (a one-chain Instrument Rack)
                if (skipMidi > 0 && isMidiEffect (d.name()))
                {
                    --skipMidi;
                    continue;
                }
                skipMidi = 0;
                bool native = false;
                std::set<std::string> mappedLinks;
                if (rackDeviceFrom (ctx, d, doc, c, instrumentRack, depth, &native, &mappedLinks) >= 0 && ! native)
                    continue;
                // a dropped device's mapped parameters (for the macro names); on a Live device that became a LYNX effect
                // the macros drive the parameters that have a counterpart (round 190cf), the others are listed
                bool unmapped = false;
                for (const auto& parameter : d.children())
                    if (const auto m = mappingOf (parameter); m && mappedLinks.count (std::string (parameter.name())) == 0)
                    {
                        droppedTargets[(std::size_t) m->macro].push_back (spacedName (parameter.name()));
                        unmapped = true;
                    }
                if (native && unmapped)
                    ctx.note (&FlpImportReport::partialEffects, "Macro mapping onto a setting of a converted Live device that has no LYNX control (not applied)");
            }
        // macros on the chain's volume / pan (Live's linear gain and −1..1 → LYNX's 0..1 controls)
        if (const auto m = mappingOf (branch.path ("MixerDevice/Volume")))
        {
            RackMapping target;
            target.target = RackMapping::Target::chainVolume;
            target.chain = c;
            mapInto (doc, *m, target, rack::volumeValue ((float) m->min), rack::volumeValue ((float) m->max));
        }
        if (const auto m = mappingOf (branch.path ("MixerDevice/Panorama")))
        {
            RackMapping target;
            target.target = RackMapping::Target::chainPan;
            target.chain = c;
            mapInto (doc, *m, target, (float) ((m->min + 1.0) * 0.5), (float) ((m->max + 1.0) * 0.5));
        }
        r = rack::rackOf (doc);
    }
    if (drum) // the return chains, then the chains' sends and Audio To (round 190ec)
    {
        drumReturnsFrom (ctx, device, doc, depth);
        r = rack::rackOf (doc);
        for (int c = 0; c < (int) r->chains.size() && c < (int) drumSends.size(); ++c)
        {
            auto& chain = r->chains[(std::size_t) c];
            chain.sends.assign (r->returns.size(), RackSend {});
            for (const auto& [k, send] : drumSends[(std::size_t) c])
                if (k >= 0 && k < (int) r->returns.size())
                    chain.sends[(std::size_t) k] = send;
            const auto to = drumAudioTo[(std::size_t) c];
            chain.audioTo = to >= 0 && to < (int) r->returns.size() ? to : -1;
        }
        ctx.pendingKeyShift = keyShiftBefore;
        r->showPads = device.boolean ("ArePadsVisible", true);
        r->showIO = device.boolean ("IsMidiSectionVisible", false);
        r->showSends = device.boolean ("AreSendsVisible", false);
        r->showReturns = device.boolean ("IsReturnBranchesListVisible", false);
        r->padScroll = std::clamp ((int) device.number ("PadScrollPosition", rack::drumDefaultScroll), 0, rack::drumPadRows - 4);
    }
    else if (r->chains.empty())
        rack::addChain (doc);
    r = rack::rackOf (doc);
    for (int k = 0; k < rackMacroCount; ++k)
    {
        const auto n = std::to_string (k);
        auto& m = r->macros[(std::size_t) k];
        m.value = (float) std::clamp (device.number ("MacroControls." + n + "/Manual", 0.0), 0.0, 127.0);
        const auto shown = device.value ("MacroDisplayNames." + n);
        if (shown != "Macro " + std::to_string (k + 1))
            m.name = shown;
        const auto def = device.number ("MacroDefaults." + n, -1.0);
        m.defaultValue = def < 0.0 ? -1.0f : (float) std::min (def, 127.0);
        m.excludeFromRandom = device.boolean ("ExcludeMacroFromRandomization." + n, false);
        m.excludeFromVariations = device.boolean ("ExcludeMacroFromSnapshots." + n, false);
    }
    // Live names a macro that wasn't renamed after its one mapped parameter ("Macro 3" stored, "Formant Shift" shown
    // [SHOT]); when LYNX drops that device, the macro keeps the name Live showed
    for (int k = 0; k < rackMacroCount; ++k)
        if (auto& m = r->macros[(std::size_t) k]; m.name.empty() && m.mappings.empty() && droppedTargets[(std::size_t) k].size() == 1)
            m.name = droppedTargets[(std::size_t) k].front();
    r->shownMacros = std::clamp ((int) device.number ("NumVisibleMacroControls", 8), 1, rackMacroCount);
    // (round 190ed) the macros an envelope moves (0…127 → the slot's parameter k, 0..1)
    if (ctx.rackAutos != nullptr)
        for (int k = 0; k < rackMacroCount; ++k)
            if (const auto id = targetId (device.child ("MacroControls." + std::to_string (k))); ! id.empty() && ctx.automated.count (id) != 0)
                ctx.rackAutos->push_back ({ id, {}, -1, k, [] (double v) { return (float) std::clamp (v / 127.0, 0.0, 1.0); },
                                            r->macros[(std::size_t) k].value / 127.0f, rack::macroName (*r, k) });
    r->selector = (float) std::clamp (device.number ("ChainSelector/Manual", 0.0), 0.0, 127.0);
    r->selectorFiltersCtrl = device.boolean ("ChainSelectorFilterMidiCtrl", false);
    r->autoSelect = device.boolean ("IsAutoSelectEnabled", false);
    r->showMacros = device.boolean ("AreMacroControlsVisible", true);
    r->showChains = device.boolean ("IsBranchesListVisible", false);
    r->showDevices = device.boolean ("AreDevicesVisible", false);
    // the zone editor: Live keeps only whether it shows [DATA]; an Audio Effect Rack has only the Chain editor, the
    // others open on Key
    if (device.boolean ("IsRangesEditorVisible", false))
        r->zoneEditor = r->kind == RackDoc::Kind::audioEffect ? 2 : 0;
    r->showVariations = device.boolean ("AreMacroVariationsControlsVisible", false);
    for (const auto& snapshot : device.path ("MacroVariations/MacroSnapshots").children ("MacroSnapshot"))
    {
        RackVariation v;
        v.name = snapshot.value ("SnapshotName");
        for (int k = 0; k < rackMacroCount; ++k)
        {
            const auto n = std::to_string (k);
            v.has[(std::size_t) k] = snapshot.boolean ("MacroHasValue." + n, false);
            v.values[(std::size_t) k] = v.has[(std::size_t) k] ? (float) std::clamp (snapshot.number ("MacroValues." + n, 0.0), 0.0, 127.0) : -1.0f;
        }
        r->variations.push_back (std::move (v));
    }
    // the chain selector on a macro (0…127 → 0..1)
    if (const auto m = mappingOf (device.child ("ChainSelector")))
    {
        RackMapping target;
        target.target = RackMapping::Target::selector;
        mapInto (doc, *m, target, (float) (m->min / 127.0), (float) (m->max / 127.0));
    }
    // a mapped parameter follows its macro (Live drives it from the macro; its own stored value may be an old one [DATA:
    // a chain volume stored at 0 dB under a macro that puts it at −9.6 dB])
    for (int k = 0; k < rackMacroCount; ++k)
        if (! rack::rackOf (doc)->macros[(std::size_t) k].mappings.empty())
            rack::setMacro (doc, k, rack::rackOf (doc)->macros[(std::size_t) k].value);
    rack::rackOf (doc)->name = device.value ("UserName");
    rack::compile (doc);
    ctx.note (&FlpImportReport::converted, drum ? "Drum rack" : instrumentRack ? "Instrument rack" : "Audio effect rack");
    return true;
}

// Fills `channel` from a MIDI track's instrument device. Returns false for none.
bool instrumentFrom (Ctx& ctx, const Element& device, ChannelDef& channel, std::vector<DrumPad>* pads, int depth)
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
            // (round 190ed) envelopes on the plugin's parameters (the track's own instrument only)
            if (depth == 0 && ! ctx.inDrumPad)
            {
                std::vector<std::int32_t> slots;
                pluginAutos (
                    ctx, device, p->name,
                    [&ctx] (const std::string& id, int param, float, const std::string& name, std::function<float (double)> law) {
                        ctx.channelTargets.push_back ({ id, Target { AutomationTarget { AutomationTargetKind::channelParam, -1, 0, param }, ValueMap::plain, name, std::move (law) } });
                    },
                    &slots);
                appendVst3Slots (channel.plugin.foreign, slots); // (round 190ek)
            }
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
        // (round 190bq) a Simpler loops by its own Loop switch in Classic mode (SustainLoop is the Sampler's); a looped
        // sample stays a zone, which holds the loop points
        const auto simpler = tag == "OriginalSimpler" && (bool) device.path ("Globals/PlaybackMode");
        const auto simplerLoop = simpler && (int) device.number ("Globals/PlaybackMode", 0) == 0
                                 && device.boolean ("Player/LoopModulators/LoopOn/Manual", false);
        if (simpler)
            for (auto& zone : zones)
                zone.loopMode = 0;
        const auto firstPath = zones.front().path;
        const auto& z = zones.front();
        if (! simplerLoop && zones.size() == 1 && z.lowKey == 0 && z.highKey == 127 && z.lowVelocity <= 1 && z.highVelocity == 127 && std::abs (60 - z.rootKey) <= 24)
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
        // (round 190bq) the rest of a Simpler: transpose, volume, pan, filter, envelopes, LFO, voices, glide, one-shot
        // fades, slices, region, loop, warp (core/AlsDevices.h); what LYNX's Sampler lacks goes to the report
        if (simpler)
        {
            alsdev::Notes notes;
            const auto seconds = ctx.options.common.sampleSeconds ? ctx.options.common.sampleSeconds (firstPath) : 0.0;
            const auto r = alsdev::applySimpler (device, channel, ctx.song.bpm, seconds, notes);
            if (r.loop && ! channel.zones.empty())
                for (auto& zone : channel.zones)
                {
                    zone.loopStart = r.loopStart;
                    zone.loopEnd = r.loopEnd;
                    zone.loopMode = 1;
                }
            if (ctx.inDrumPad)
                ctx.note (&FlpImportReport::approximated, "Drum rack pad: Simpler settings other than sample, tuning, level and pan (not applied)");
            else
            {
                ctx.pendingKeyShift = r.keyShift;
                // (round 190ed) envelopes on the filter's frequency (Hz, the same law) and resonance (as the import
                // reads it) move the Sampler's Cutoff / Resonance knobs
                const auto filter = device.path ("Filter/Slot/Value/SimplerFilter");
                if (depth == 0 && device.boolean ("Filter/IsOn/Manual", false) && filter)
                {
                    if (const auto id = targetId (filter.child ("Freq")); ! id.empty() && ctx.automated.count (id) != 0)
                        ctx.channelTargets.push_back ({ id, Target { AutomationTarget { AutomationTargetKind::channelParam, -1, 0, samplerparam::cutoff }, ValueMap::plain,
                                                                     "Simpler Filter Freq", [] (double hz) { return samplerparam::toValue (samplerparam::cutoff, (float) std::clamp (hz, 20.0, 20000.0)); } } });
                    if (const auto id = targetId (filter.child ("Res")); ! id.empty() && ctx.automated.count (id) != 0)
                        ctx.channelTargets.push_back ({ id, Target { AutomationTarget { AutomationTargetKind::channelParam, -1, 0, samplerparam::resonance }, ValueMap::plain,
                                                                     "Simpler Filter Res", [] (double r) { return samplerparam::toValue (samplerparam::resonance, (float) std::clamp (r, 0.0, 1.0)); } } });
                }
                for (const auto& n : notes.approximated)
                    ctx.note (&FlpImportReport::approximated, "Simpler: " + n);
                for (const auto& n : notes.dropped)
                    ctx.note (&FlpImportReport::approximated, "Simpler: " + n + " (not applied)");
            }
        }
        ctx.note (&FlpImportReport::converted, tag == "OriginalSimpler" ? "Simpler" : "Sampler");
        return true;
    }
    if (tag == "DrumGroupDevice" && depth < 8 && ! ctx.options.drumPadsAsChannels) // (round 190ec: a LYNX Drum Rack)
    {
        auto doc = rack::makeDrumRack();
        if (rackFrom (ctx, device, doc, depth))
        {
            channel.instrument = InstrumentKind::patcher;
            channel.patcher = std::move (doc);
            return true;
        }
        return false;
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
                const auto wasPad = ctx.inDrumPad;
                ctx.inDrumPad = true;
                const auto made = instrumentFrom (ctx, inner, sub, nullptr, depth + 1);
                ctx.inDrumPad = wasPad;
                if (made && sub.instrument == InstrumentKind::sampler)
                {
                    // (round 190bq) the pad's Simpler volume and pan ride on the pad's layers and pan
                    const auto level = std::pow (10.0f, sub.sampler.gainDb / 20.0f);
                    if (sub.zones.empty())
                    {
                        SampleZone z;
                        z.path = sub.samplePath;
                        z.tune = sub.sampler.pitch; // (played at its own pitch on the pad's key)
                        z.gain = level;
                        pad.layers.push_back (z);
                    }
                    else
                    {
                        pad.layers = sub.zones;
                        for (auto& layer : pad.layers)
                        {
                            layer.gain *= level;
                            layer.tune += sub.sampler.pitch;
                        }
                    }
                    pad.pan = sub.pan;
                    break;
                }
            }
            pad.gain = (float) branch.number ("MixerDevice/Volume/Manual", 1);
            pad.pan = (float) std::clamp ((double) pad.pan + branch.number ("MixerDevice/Panorama/Manual", 0), -1.0, 1.0);
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
    if (tag == "InstrumentGroupDevice" && depth < 8) // (round 183c: a LYNX Instrument Rack)
    {
        auto doc = rack::makeRack (RackDoc::Kind::instrument);
        // (round 190ed) the envelopes inside the rack and on its macros (the track's own instrument only): the channel's
        // macro parameters
        std::vector<RackAuto> autos;
        auto* const saved = std::exchange (ctx.rackAutos, depth == 0 && ! ctx.inDrumPad ? &autos : nullptr);
        const auto read = rackFrom (ctx, device, doc, depth);
        ctx.rackAutos = saved;
        if (read)
        {
            channel.instrument = InstrumentKind::patcher;
            channel.patcher = std::move (doc);
            placeRackAutos (ctx, channel.patcher, autos, [] (int k) { return AutomationTarget { AutomationTargetKind::channelParam, -1, 0, k }; }, true);
            return true;
        }
        return false;
    }
    if (tag == "DrumGroupDevice" && depth < 8)
    {
        // a Drum Rack without pads (nested): its first chain's instrument
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
    // (round 190ed) Operator without FM in use and Drift: the built-in synth with their oscillators, filter and amp
    // envelope (core/AlsDevices.h); the report lists the pair as fitted and what has no place
    if (tag == "Operator" || tag == "Drift")
    {
        alsdev::Notes notes;
        ChannelDef synth = channel;
        if (tag == "Operator" ? alsdev::operatorToSynth (device, synth, ctx.song.bpm, notes) : alsdev::driftToSynth (device, synth, ctx.song.bpm, notes))
        {
            channel = std::move (synth);
            ctx.note (&FlpImportReport::partialEffects, tag + " -> built-in synth (settings fitted to LYNX's engine)");
            for (const auto& n : notes.approximated)
                ctx.note (&FlpImportReport::partialEffects, tag + ": " + n);
            for (const auto& n : notes.dropped)
                ctx.note (&FlpImportReport::partialEffects, tag + ": " + n + " (not applied)");
            return true;
        }
        if (tag == "Operator")
        {
            // FM (or a waveform whose order is not settled): no LYNX engine holds it yet
            ctx.note (&FlpImportReport::unsupported, deviceName (device) + " (FM synthesis: no LYNX engine for it yet)");
            return false;
        }
    }
    // Live's synths: no LYNX equivalent; the channel keeps a built-in synth, as for an unknown FL generator
    ctx.note (&FlpImportReport::unsupported, deviceName (device));
    return false;
}

// One of Live's MIDI effects in front of a track's instrument (round 190cf, core/AlsDevices.h): the Arpeggiator becomes
// the channel's arpeggiator; Pitch, Velocity, Chord and Note Length are written into the notes of the track's clips
// (LYNX has no live note processor inside a channel for them), in the chain's order. False = no counterpart, or a
// setting that cannot be kept: the caller lists the device as unsupported, as before.
bool midiEffectFrom (Ctx& ctx, const Element& d, ChannelDef& channel, std::vector<alsdev::NoteEffect>& effects)
{
    const auto tag = d.name();
    if (tag != "MidiArpeggiator" && tag != "MidiPitcher" && tag != "MidiVelocity" && tag != "MidiChord" && tag != "MidiNoteLength")
        return false;
    alsdev::Context context;
    context.bpm = ctx.song.bpm;
    alsdev::Notes notes;
    std::string name;
    if (tag == "MidiArpeggiator")
    {
        ArpSettings arp;
        if (channel.arp.enabled() || ! alsdev::arpeggiatorToArp (d, context, arp, notes))
            return false;
        name = "Arpeggiator";
        if (! d.boolean ("On/Manual", true))
        {
            ctx.note (&FlpImportReport::approximated, name + " (switched off in the set: left out)");
            return true;
        }
        channel.arp = arp;
        ctx.note (&FlpImportReport::converted, "Arpeggiator -> channel arpeggiator");
    }
    else
    {
        const auto e = alsdev::noteEffectFromLiveDevice (d, context, notes);
        // behind an arpeggiator only what gives the same notes either way round can be written into the clips
        if (! e || (channel.arp.enabled() && (e->kind == alsdev::NoteEffect::Kind::chord || e->kind == alsdev::NoteEffect::Kind::length)))
            return false;
        name = e->name;
        if (! d.boolean ("On/Manual", true))
        {
            ctx.note (&FlpImportReport::approximated, name + " (switched off in the set: left out)");
            return true;
        }
        effects.push_back (*e);
        ctx.note (&FlpImportReport::approximated, name + " (MIDI effect): written into the notes of the track's clips; notes played live are not changed");
    }
    for (const auto& n : notes.approximated)
        ctx.note (&FlpImportReport::approximated, name + ": " + n);
    for (const auto& n : notes.dropped)
        ctx.note (&FlpImportReport::approximated, name + ": " + n + " (not applied)");
    return true;
}

// (round 190ed) The Peak Controller of a translated effect's modulation (core/AlsDevices.h Translated::Modulator): in
// front of the effect on its insert, or (a side-chain envelope follower) at the end of the source track's insert once
// every track is read; the link is made then (finishLinks)
void placeModulator (Ctx& ctx, const Element& d, MixerTrack& insert, int track, alsdev::Translated::Modulator& mod, std::vector<alsdev::ParamLink>& links,
                     const std::string& from)
{
    // the parameter the link moves keeps no envelope of its own (the link would fight it)
    for (auto it = links.begin(); it != links.end(); ++it)
        if (it->param == mod.param)
        {
            if (const auto id = targetId (d.path (it->live)); ! id.empty() && ctx.automated.count (id) != 0)
            {
                ctx.note (&FlpImportReport::partialEffects, from + ": automation of the cutoff under its " + mod.what + " (the link moves the cutoff; not carried)");
                ctx.explained.insert (id);
            }
            links.erase (it);
            break;
        }
    Ctx::PendingLink link;
    link.controller = std::move (mod.controller);
    link.targetTrack = track;
    link.param = mod.param;
    link.curve = std::move (mod.curve);
    link.notes = std::move (mod.notes);
    link.from = from;
    link.what = mod.what;
    if (mod.sideTrackId >= 0)
        if (const auto it = ctx.insertOfTrack.find (mod.sideTrackId); it != ctx.insertOfTrack.end())
        {
            link.sourceInsert = it->second;
            link.notes.push_back ("envelope follower on the side-chain source: its Peak Controller sits at the end of that track's insert");
            ctx.pendingLinks.push_back (std::move (link));
            return;
        }
    if (mod.sideTrackId >= 0 || mod.sideOther)
        link.notes.push_back ("side-chain input of the envelope follower (its source is not a track of the set; the follower hears the effect's input)");
    if (insert.effects.size() + 2 > (std::size_t) maxEffectSlots)
    {
        ctx.note (&FlpImportReport::partialEffects, from + ": " + mod.what + " on the cutoff (no free effect slot for its Peak Controller) (not applied)");
        return;
    }
    insert.effects.push_back (std::move (link.controller));
    link.sourceInsert = track;
    link.sourceSlot = (int) insert.effects.size() - 1;
    ctx.pendingLinks.push_back (std::move (link));
}

// (round 190ed) Every track is read: the side-chain controllers go to the end of their source inserts and each link is made
void finishLinks (Ctx& ctx)
{
    auto& song = ctx.song;
    for (auto& link : ctx.pendingLinks)
    {
        if (link.targetSlot < 0 || link.sourceInsert < 0 || link.sourceInsert >= (int) song.mixer.size())
            continue;
        if (link.sourceSlot < 0)
        {
            auto& source = song.mixer[(std::size_t) link.sourceInsert];
            if (source.effects.size() >= (std::size_t) maxEffectSlots)
            {
                ctx.note (&FlpImportReport::partialEffects, link.from + ": " + link.what + " on the cutoff (no free effect slot on the side-chain source) (not applied)");
                continue;
            }
            source.effects.push_back (std::move (link.controller));
            link.sourceSlot = (int) source.effects.size() - 1;
        }
        Automation a;
        a.name = song.mixer[(std::size_t) link.targetTrack].name + " " + link.from + " cutoff (" + link.what + ")";
        a.target = { AutomationTargetKind::effectParam, link.targetTrack, link.targetSlot, link.param };
        a.source.track = link.sourceInsert;
        a.source.slot = link.sourceSlot;
        a.source.output = 0; // peak + LFO
        a.curve = link.curve;
        song.automations.push_back (std::move (a));
        ctx.note (&FlpImportReport::partialEffects, link.from + " " + link.what + " -> Peak Controller linked to the Filter's cutoff");
        for (const auto& n : link.notes)
            ctx.note (&FlpImportReport::partialEffects, link.from + ": " + n);
    }
    ctx.pendingLinks.clear();
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
                // (round 190ed) envelopes on the plugin's parameters
                const auto track = (int) (&insert - ctx.song.mixer.data()), at = (int) insert.effects.size() - 1;
                std::vector<std::int32_t> slots;
                pluginAutos (
                    ctx, d, p->name,
                    [&ctx, track, at] (const std::string& id, int param, float, const std::string& name, std::function<float (double)> law) {
                        ctx.targets[id] = { AutomationTarget { AutomationTargetKind::effectParam, track, at, param }, ValueMap::plain, name, std::move (law) };
                    },
                    &slots);
                appendVst3Slots (insert.effects.back().plugin.foreign, slots); // (round 190ek)
            }
            continue;
        }
        if (tag == "AudioEffectGroupDevice" && insert.effects.size() < (std::size_t) maxEffectSlots) // (round 183c)
        {
            auto slot = rack::makeRackSlot (RackDoc::Kind::audioEffect);
            slot.enabled = d.boolean ("On/Manual", true);
            // (round 190ed) the envelopes inside the rack and on its macros: the slot's macro parameters
            std::vector<RackAuto> autos;
            auto* const saved = std::exchange (ctx.rackAutos, &autos);
            const auto read = rackFrom (ctx, d, slot.patcher, 0);
            ctx.rackAutos = saved;
            if (read)
            {
                insert.effects.push_back (std::move (slot));
                const auto track = (int) (&insert - ctx.song.mixer.data()), at = (int) insert.effects.size() - 1;
                placeRackAutos (ctx, insert.effects.back().patcher, autos, [track, at] (int k) {
                    return AutomationTarget { AutomationTargetKind::effectParam, track, at, k };
                });
            }
            continue;
        }
        if (isMidiEffect (tag))
        {
            ctx.note (&FlpImportReport::unsupported, deviceName (d));
            continue;
        }
        // (round 190bq) Live's own effects LYNX has an engine for: the LYNX effect, settings translated
        std::vector<alsdev::ParamLink> links;
        std::string from;
        std::optional<alsdev::Translated::Modulator> modulator;
        if (EffectSlot slot; insert.effects.size() < (std::size_t) maxEffectSlots && nativeEffectFrom (ctx, d, slot, &links, &from, &modulator))
        {
            const auto track = (int) (&insert - ctx.song.mixer.data());
            // (round 190ed) the modulation's Peak Controller: in front of the effect, or at the end of the side-chain
            // source's insert once every track is read; the link drives the parameter the modulation moves, so that
            // parameter's own envelope is not carried as well
            if (modulator)
                placeModulator (ctx, d, insert, track, *modulator, links, from);
            insert.effects.push_back (std::move (slot));
            if (modulator && ! ctx.pendingLinks.empty() && ctx.pendingLinks.back().targetSlot == -1)
                ctx.pendingLinks.back().targetSlot = (int) insert.effects.size() - 1;
            // (round 190cf) automation of the device's parameters follows onto the LYNX effect's, by the same laws
            for (auto& l : links)
                if (const auto id = targetId (d.path (l.live)); ! id.empty())
                    ctx.targets[id] = { AutomationTarget { AutomationTargetKind::effectParam, track, (int) insert.effects.size() - 1, l.param },
                                        ValueMap::plain, from + " " + spacedName (pathFileName (l.live)), std::move (l.value) };
            continue;
        }
        // (round 190cf) a device LYNX could hold, on an insert whose slots are all taken, is named with that reason
        if (insert.effects.size() >= (std::size_t) maxEffectSlots && alsdev::effectFromLiveDevice (d).has_value())
            ctx.note (&FlpImportReport::droppedEffects, deviceName (d) + " (no free effect slot: an insert holds " + std::to_string (maxEffectSlots) + ")");
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

// A set's or a clip's ScaleInformation as a project key (round 190o) [LIVE 12.4 save: Root 0…11, Name = the index into
// Live's scale chooser; Live 11: RootNote and the name as text]; nullopt when FL's list (which holds the project key)
// has no scale with the same degrees.
std::optional<projectkey::Key> liveScaleOf (const Element& owner)
{
    const auto info = owner.child ("ScaleInformation");
    if (! info)
        return std::nullopt;
    const auto name = info.value ("Name");
    const auto numeric = ! name.empty() && std::all_of (name.begin(), name.end(), [] (char ch) { return ch >= '0' && ch <= '9'; });
    const auto index = numeric ? std::atoi (name.c_str()) : livescales::indexOfName (name);
    return livescales::keyFor ((int) info.number ("Root", info.number ("RootNote", 0)), index);
}

int patternFromMidiClip (Ctx& ctx, const Element& clip, int channel, double length, const ClipLoop* loopOverride = nullptr)
{
    if (! ctx.clipKeySeen && clip.boolean ("IsInKey", false))
    {
        ctx.clipKeySeen = true;
        ctx.clipKey = liveScaleOf (clip);
    }
    Pattern p;
    p.name = clip.value ("Name");
    p.colour = alsColour ((int) clip.number ("Color", -1));
    const auto passes = passesOf (loopOverride != nullptr ? *loopOverride : loopOf (clip), length);
    // Live 12's probability groups (round 187c) [LIVE 12.4 save: NoteProbabilityGroup { GroupType 0 = Play All, 1 = Play
    // One; Notes/NoteId; Probability (the group's: its notes keep their own for an ungroup) }]: NoteId → group
    struct ChanceGroup
    {
        int index = 0;
        bool playOne = false;
        double chance = 1.0;
    };
    std::map<int, ChanceGroup> groupOfNote;
    int groupCount = 0;
    for (const auto& g : clip.path ("Notes/NoteProbabilityGroups").children ("NoteProbabilityGroup"))
    {
        const ChanceGroup group { ++groupCount, g.number ("GroupType", 0) == 1, std::clamp (g.number ("Probability", 1), 0.0, 1.0) };
        for (const auto& id : g.path ("Notes").children ("NoteId"))
            groupOfNote[(int) numberAttr (id, "NoteId", -1)] = group;
    }
    for (const auto& keyTrack : clip.path ("Notes/KeyTracks").children ("KeyTrack"))
    {
        const auto key = (int) keyTrack.number ("MidiKey", 60);
        for (const auto& n : keyTrack.path ("Notes").children ("MidiNoteEvent"))
        {
            const auto t = numberAttr (n, "Time"), d = numberAttr (n, "Duration", 0.25);
            const auto grouped = groupOfNote.find ((int) numberAttr (n, "NoteId", -1));
            for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex)
            {
                const auto& pass = passes[passIndex];
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
                // Live's note Chance and Velocity Range (round 187c; Live 12 leaves out the defaults 1 and 0 [DATA])
                note.chance = (float) std::clamp (numberAttr (n, "Probability", 1), 0.0, 1.0);
                note.velocityRange = (float) std::clamp (numberAttr (n, "VelocityDeviation", 0) / 127.0, -1.0, 1.0);
                if (grouped != groupOfNote.end()) // each pass of a looped clip draws its own groups
                {
                    note.chanceGroup = (int) passIndex * groupCount + grouped->second.index;
                    note.chanceOne = grouped->second.playOne;
                    note.chance = (float) grouped->second.chance;
                }
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
                     double beatsPitch = 0.0, const std::vector<WarpPoint>& warp = {})
{
    auto key = path + "|" + std::to_string (mixerTrack) + "|" + std::to_string (warped ? warpMode : -1) + "|"
               + std::to_string (std::llround (stretchBeats * 1.0e6)) + "|" + std::to_string (std::llround (beatsPitch * 100.0));
    for (const auto& w : warp) // (clips of one file with different markers need channels of their own)
        key += "|" + std::to_string (std::llround (w.seconds * 1.0e6)) + ":" + std::to_string (std::llround (w.beats * 1.0e6));
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
    c.sampler.warp = warp;
    ctx.song.channels.push_back (std::move (c));
    const auto index = (int) ctx.song.channels.size() - 1;
    ctx.audioChannels[key] = index;
    return index;
}

// `out`: the clips go there instead of the Playlist; `loopOverride`: the content played instead of the clip's own Loop
void addAudioClip (Ctx& ctx, const Element& clip, int row, int mixerTrack, double at, double length, const std::string& user,
                   std::vector<Clip>* out = nullptr, const ClipLoop* loopOverride = nullptr)
{
    const auto path = ctx.resolveSample (clip.child ("SampleRef"), "Audio clip: " + user);
    if (path.empty() || length <= 0)
        return;
    const auto warped = clip.boolean ("IsWarped", true);
    const auto mode = (int) clip.number ("WarpMode", 0);
    const auto bpm = ctx.song.bpm;

    // warp: beats of the clip's content ↔ seconds of the sample, piecewise linear [DATA: WarpMarker SecTime/BeatTime].
    // `rate` is the one between the first and the last marker; a clip stretched offline keeps every segment (round 175a,
    // M1: the channel's warp markers); one stretched in real time plays at `rate`.
    double rate = bpm / 60.0, beatAtZero = 0.0; // beats per sample second; the content beat at sample time 0
    std::vector<std::pair<double, double>> marks;
    if (warped)
    {
        for (const auto& m : clip.path ("WarpMarkers").children ("WarpMarker"))
            marks.push_back ({ numberAttr (m, "SecTime"), numberAttr (m, "BeatTime") });
        std::sort (marks.begin(), marks.end());
        if (marks.size() >= 2 && marks.back().first > marks.front().first + 1e-6 && marks.back().second > marks.front().second)
            rate = (marks.back().second - marks.front().second) / (marks.back().first - marks.front().first);
        if (! marks.empty())
            beatAtZero = marks.front().second - rate * marks.front().first;
    }
    // more than one tempo inside the clip (Live writes one marker past the last real one, so 2 segments can still be one)
    const auto bends = [&] {
        double maxDev = 0;
        for (const auto& [sec, beat] : marks)
            maxDev = std::max (maxDev, std::abs (beatAtZero + rate * sec - beat));
        return marks.size() > 2 && maxDev > 1.0 / 64.0;
    }();
    auto stretch = warped ? std::clamp (rate * 60.0 / bpm, 0.01, 100.0) : 1.0;
    // a warped clip that keeps its pitch, whose file length the set records [DATA: SampleRef DefaultDuration /
    // DefaultSampleRate]: the channel stretches the whole file offline with the clip's warp mode (round 175a, N4:
    // LYNX's Beats engine only runs offline), to the file's length in content beats; the clip then plays it as is
    auto fileFrames = clip.number ("SampleRef/DefaultDuration", 0), fileRate = clip.number ("SampleRef/DefaultSampleRate", 0);
    // (a set without the file's length, as LYNX writes them: markers that bend the tempo still need it; the file's own)
    if (bends && ! (fileFrames > 0 && fileRate > 0) && ctx.options.common.sampleSeconds)
        if (const auto seconds = ctx.options.common.sampleSeconds (path); seconds > 0.0)
        {
            fileFrames = seconds;
            fileRate = 1.0;
        }
    // Beats with a transpose: Live resamples each piece (higher and shorter, still on its beat), so the transposition is
    // rendered offline with the stretch, keyed by the pitch, and the clip itself plays untransposed [LIVE: Beats-up5]
    const auto transpose = std::clamp (clip.number ("PitchCoarse", 0) + clip.number ("PitchFine", 0) / 100.0, -48.0, 48.0);
    const auto bakePitch = warped && mode == 0 && transpose != 0.0;
    double fileBeats = 0.0, beatsPitch = 0.0;
    std::vector<WarpPoint> warp;
    if (warped && mode != 3 && fileFrames > 0 && fileRate > 0 && (std::abs (stretch - 1.0) > 1.0e-9 || bakePitch || bends))
    {
        stretch = 1.0;
        beatsPitch = bakePitch ? transpose : 0.0;
        if (bends)
        {
            // every segment: the file from its start to its end, in content beats, with the markers kept for the render
            StretchMap beatsOf;
            for (const auto& [sec, beat] : marks)
                if (beatsOf.empty() || (sec > beatsOf.back().first + 1e-9 && beat > beatsOf.back().second + 1e-9))
                    beatsOf.push_back ({ sec, beat });
            beatAtZero = stretchMapOutput (beatsOf, 0.0);
            fileBeats = stretchMapOutput (beatsOf, fileFrames / fileRate) - beatAtZero;
            for (const auto& [sec, beat] : beatsOf)
                warp.push_back ({ sec, beat });
        }
        else
            fileBeats = rate * fileFrames / fileRate;
    }
    else if (bends && ctx.report != nullptr)
        ++ctx.report->partialEffects["Warp markers (one tempo per clip)"]; // stretched in real time: one rate
    // content position (beats, or seconds when not warped) → LYNX playback ticks from the sample's start
    const auto contentTicks = [&] (double v) { return warped ? (v - beatAtZero) * ppq : v * bpm / 60.0 * ppq; };
    const auto channel = audioChannelFor (ctx, path, mixerTrack, mode, warped, fileBeats, beatsPitch, warp);
    const auto loop = loopOverride != nullptr ? *loopOverride : loopOf (clip);
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
        (out != nullptr ? *out : ctx.song.playlist).push_back (c);
    }
}

void addMidiClip (Ctx& ctx, const Element& clip, int row, int channel, double at, double length, std::vector<Clip>* out = nullptr,
                  const ClipLoop* loopOverride = nullptr)
{
    if (length <= 0)
        return;
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = patternFromMidiClip (ctx, clip, channel, length, loopOverride);
    c.track = row;
    c.start = ticksOf (at);
    c.length = ticksOf (length);
    c.muted = clip.boolean ("Disabled", false);
    (out != nullptr ? *out : ctx.song.playlist).push_back (c);
}

// A clip's or scene's FollowAction block [DATA: section 5; numbers LIVE: REA_rodada181a.md, read in Live 12's Clip View]
ClipFollow followOf (const Element& f)
{
    ClipFollow c;
    c.enabled = f.boolean ("FollowActionEnabled", false);
    c.a = (FollowKind) std::clamp ((int) f.number ("FollowActionA", 4), 0, 9);
    c.b = (FollowKind) std::clamp ((int) f.number ("FollowActionB", 0), 0, 9); // [INFERRED] B uses A's numbers
    c.chanceA = std::clamp ((int) std::lround (f.number ("FollowChanceA", 100)), 0, 100);
    c.chanceB = std::clamp ((int) std::lround (f.number ("FollowChanceB", 0)), 0, 100);
    // the jump target: Live shows the stored number itself, 1-based (5 shows "5", 0 an empty field) [LIVE, 181c];
    // LYNX's jump row counts from 0. [INFERRED] the number counts scenes from the top, not clips of the group
    c.jumpA = std::clamp ((int) f.number ("JumpIndexA", 1) - 1, 0, launcher::maxScenes - 1);
    c.jumpB = std::clamp ((int) f.number ("JumpIndexB", 1) - 1, 0, launcher::maxScenes - 1);
    c.linked = f.boolean ("IsLinked", true);
    c.iterations = std::clamp ((int) std::lround (f.number ("LoopIterations", 1)), 1, 999);
    c.ticks = std::max<std::int64_t> (1, ticksOf (f.number ("FollowTime", 4)));
    return c;
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
        // the time signature and its changes along the Arrangement (round 190cg): EnumEvents holding the signature
        // code, the first one "before the song" [DATA: every stored set has that one event, Value = Manual]; each later
        // one becomes a time signature marker [INFERRED: Time in beats as in every other envelope]
        if (! id.empty() && id == ctx.signatureTarget)
        {
            for (const auto& e : env.path ("Automation/Events").children())
            {
                int num = 4, den = 4;
                const auto time = numberAttr (e, "Time");
                if (e.name() != "EnumEvent" || time <= 0.0 || ! alsTimeSignature ((int) numberAttr (e, "Value"), num, den))
                    continue;
                Marker m;
                m.tick = ticksOf (time);
                m.numerator = num;
                m.denominator = den;
                m.action = Marker::timeSignature;
                ctx.song.addMarker (m);
            }
            continue;
        }
        const auto it = ctx.targets.find (id);
        if (it == ctx.targets.end())
        {
            if (ctx.explained.count (id) == 0)
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
            a.points.push_back ({ ticksOf (std::max (0.0, t) - first), it->second.law ? std::clamp (it->second.law (v), 0.0f, 1.0f) : mapValue (it->second.map, v), 0.0f });
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
        ctx.targets[id] = { AutomationTarget { AutomationTargetKind::mixerVolume, insert, 0, 0 }, ValueMap::fader, "volume", {} };
    if (const auto id = targetId (mixer.child ("Pan")); ! id.empty())
        ctx.targets[id] = { AutomationTarget { AutomationTargetKind::mixerPan, insert, 0, 0 }, ValueMap::pan, "pan", {} };
    (void) name;
}

// ---------------------------------------------------------------------------------------------------------------
// Session view → the clip grid (round 181c). Each track's ClipSlotList holds one ClipSlot per scene [DATA: section 5];
// launch settings use the numbers read in Live 12's Clip View [LIVE: REA_rodada181a.md]. A grid clip plays one pass of
// what Live loops: a looping clip becomes its loop region with the slot's Loop on, a one-shot its start…end span.
void sessionToGrid (Ctx& ctx, const Element& set, const std::vector<Element>& tracks, const std::vector<int>& rowOf,
                    const std::vector<int>& channelOf, const std::vector<int>& insertOf, const std::vector<Element>& scenes)
{
    auto& song = ctx.song;
    bool any = false;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const auto kind = tracks[i].name();
        if (rowOf[i] < 0 || (kind != "MidiTrack" && kind != "AudioTrack"))
            continue;
        const auto list = tracks[i].path ("DeviceChain/MainSequencer/ClipSlotList").children ("ClipSlot");
        for (std::size_t s = 0; s < list.size(); ++s)
        {
            if ((int) s >= launcher::maxScenes)
            {
                ctx.note (&FlpImportReport::unsupported, "Session scenes past LYNX's grid limit");
                break;
            }
            bool placed = false;
            for (const auto& clip : list[s].path ("ClipSlot/Value").children())
            {
                if (clip.name() != "MidiClip" && clip.name() != "AudioClip")
                    continue;
                const auto own = loopOf (clip);
                auto body = own;
                if (own.on)
                {
                    body.start = own.loopStart;
                    body.end = own.loopEnd;
                    if (std::abs (own.start - own.loopStart) > 1e-6)
                        ctx.note (&FlpImportReport::approximated, "Session clip whose start marker is inside its loop (it starts at the loop start)");
                }
                body.on = false; // one pass: the grid's Loop repeats the clip
                auto length = body.end - body.start;
                if (clip.name() == "AudioClip" && ! clip.boolean ("IsWarped", true))
                    length *= song.bpm / 60.0; // an unwarped clip's loop values are seconds [DATA]
                if (length <= 1e-9)
                    continue;
                std::vector<Clip> made;
                if (clip.name() == "MidiClip" && channelOf[i] >= 0)
                    addMidiClip (ctx, clip, rowOf[i], channelOf[i], 0.0, length, &made, &body);
                else if (clip.name() == "AudioClip")
                    addAudioClip (ctx, clip, rowOf[i], insertOf[i], 0.0, length, song.mixer[(std::size_t) insertOf[i]].name, &made, &body);
                if (made.empty())
                    continue;
                const auto id = launcher::setClip (song, rowOf[i], (int) s, made.front());
                if (id < 0)
                {
                    ctx.note (&FlpImportReport::unsupported, "Session clips past LYNX's grid limits");
                    continue;
                }
                auto& slot = song.clipGrid->slots[(std::size_t) id];
                slot.name = clip.value ("Name");
                slot.colour = alsColour ((int) clip.number ("Color", -1));
                slot.mode = (LaunchMode) std::clamp ((int) clip.number ("LaunchMode", 0), 0, 3);
                slot.quant = (LaunchQuant) std::clamp ((int) clip.number ("LaunchQuantisation", 0), 0, 14);
                slot.legato = clip.boolean ("Legato", false);
                slot.loop = own.on;
                slot.follow = followOf (clip.child ("FollowAction"));
                any = placed = true;
                break; // one clip per slot
            }
            if (! placed && ! list[s].boolean ("HasStop", true))
            {
                launcher::ensureGrid (song);
                launcher::setStopButton (song, rowOf[i], (int) s, false);
            }
        }
    }
    if (! any)
    {
        song.clipGrid.reset(); // only stop buttons, no clip: a set without Session clips adds no grid
        return;
    }
    auto& grid = launcher::ensureGrid (song);
    if (grid.scenes.size() < scenes.size())
        grid.scenes.resize (std::min<std::size_t> (scenes.size(), (std::size_t) launcher::maxScenes));
    for (std::size_t s = 0; s < scenes.size() && s < grid.scenes.size(); ++s)
    {
        auto& sc = grid.scenes[s];
        sc.name = scenes[s].value ("Name");
        sc.colour = alsColour ((int) scenes[s].number ("Color", -1));
        if (scenes[s].boolean ("IsTempoEnabled", false))
            sc.tempo = std::clamp (scenes[s].number ("Tempo", 120), 10.0, 999.0);
        int n = 4, d = 4;
        if (scenes[s].boolean ("IsTimeSignatureEnabled", false) && alsTimeSignature ((int) scenes[s].number ("TimeSignatureId", 201), n, d))
        {
            sc.beatsPerBar = n;
            sc.beatUnit = session::sceneBeatUnit (d); // (round 190em: the scene keeps its denominator)
        }
        if (scenes[s].boolean ("FollowAction/FollowActionEnabled", false))
            ctx.note (&FlpImportReport::unsupported, "Scene follow actions");
    }
    // the set-wide value has no Global entry: a clip's number is that + 1 [LIVE: 9 = 1/8 = clip 10]
    grid.globalQuant = (LaunchQuant) std::clamp ((int) set.number ("GlobalQuantisation", 4) + 1, 1, 14);
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
        ctx.targets[id] = { AutomationTarget { AutomationTargetKind::tempo, 0, 0, 0 }, ValueMap::tempo, "tempo", {} };
    ctx.signatureTarget = targetId (mainMixer.child ("TimeSignature"));
    // the tempo envelope's shown range [DATA: Mixer/TempoAutomationViewBottom / Top, 60 and 200 in every stored set]
    if (const auto low = mainMixer.number ("TempoAutomationViewBottom", 0), high = mainMixer.number ("TempoAutomationViewTop", 0); high > low && low > 0)
    {
        song.tempoViewLow = std::clamp (low, minTempo, maxTempo);
        song.tempoViewHigh = std::clamp (high, song.tempoViewLow + 1.0, maxTempo);
    }
    int num = 4, den = 4;
    alsTimeSignature ((int) mainMixer.number ("TimeSignature/Manual", 201), num, den);
    // the song's meter, any denominator (round 190fg; before, a signature that was not n/4 became a marker at 0)
    if (meterUnit (den) == den && num >= 1 && num <= 32)
    {
        song.beatsPerBar = num;
        song.beatUnit = den;
    }
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
    // (round 190ed) the parameters an arrangement envelope moves
    for (const auto& owner : { main.child ("AutomationEnvelopes") })
        for (const auto& env : owner.path ("Envelopes").children ("AutomationEnvelope"))
            ctx.automated.insert (env.path ("EnvelopeTarget/PointeeId").attr ("Value"));
    for (const auto& t : tracks)
        for (const auto& env : t.path ("AutomationEnvelopes/Envelopes").children ("AutomationEnvelope"))
            ctx.automated.insert (env.path ("EnvelopeTarget/PointeeId").attr ("Value"));
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
                ctx.targets[id] = { AutomationTarget { AutomationTargetKind::mixerSend, insertOf[i], returns[k], 0 }, ValueMap::fader, "send", {} };
            if (level > minusInfGain * 1.0001 && sends[k].boolean ("EnabledByUser", true))
                m.sends.push_back ({ returns[k], (float) std::min (level, (double) maxFaderGain) });
        }
    }

    // Live's Pre / Post toggle of each return (LiveSet/SendsPre, one SendPreBool per return, in order): "Pre" taps every
    // track's send to that return before the track's mixer, which is LYNX's pre-fader send (round 190aq)
    {
        const auto toggles = set.child ("SendsPre").children ("SendPreBool");
        for (std::size_t k = 0; k < toggles.size() && k < returns.size(); ++k)
            if (toggles[k].attr ("Value") == "true")
                for (auto& m : song.mixer)
                    for (auto& send : m.sends)
                        if (send.target == returns[k])
                            send.pre = true;
    }

    // channels, devices and arrangement clips
    std::vector<int> channelOf (tracks.size(), -1);
    std::vector<std::vector<DrumPad>> drumPads (tracks.size());
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].hasAttr ("Id"))
            ctx.insertOfTrack[intOf (tracks[i].attr ("Id"), -1)] = insertOf[i];
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
            std::vector<alsdev::NoteEffect> noteEffects; // (round 190cf)
            for (std::size_t d = 0; d < devices.size(); ++d)
            {
                const auto tag = devices[d].name();
                if (isMidiEffect (tag))
                {
                    if (! midiEffectFrom (ctx, devices[d], c, noteEffects))
                        ctx.note (&FlpImportReport::unsupported, deviceName (devices[d]));
                    firstEffect = d + 1;
                    continue;
                }
                if (isInstrumentDevice (tag) || tag == "PluginDevice")
                {
                    // (round 190cf) an Instrument Rack with one chain: the MIDI effects at the head of that chain sit in
                    // front of the instrument just as the track's own do
                    ctx.skipLeadingMidi = 0;
                    if (const auto branches = devices[d].path ("Branches").children(); tag == "InstrumentGroupDevice" && branches.size() == 1)
                        for (const auto& inner : branches.front().path ("DeviceChain/MidiToAudioDeviceChain/Devices").children())
                        {
                            if (! isMidiEffect (inner.name()) || ! midiEffectFrom (ctx, inner, c, noteEffects))
                                break;
                            ++ctx.skipLeadingMidi;
                        }
                    ctx.pendingKeyShift = 0;
                    instrumentFrom (ctx, devices[d], c, &drumPads[i]);
                    ctx.skipLeadingMidi = 0;
                    firstEffect = d + 1;
                }
                break;
            }
            song.channels.push_back (std::move (c));
            channelOf[i] = (int) song.channels.size() - 1;
            // (round 190ed) the envelopes on the channel's instrument (a plugin's parameters, an Instrument Rack's macros,
            // a Simpler's filter)
            for (auto& [id, target] : ctx.channelTargets)
            {
                target.target.index = channelOf[i];
                ctx.targets[id] = std::move (target);
            }
            ctx.channelTargets.clear();
            if (! noteEffects.empty())
                ctx.noteEffects[channelOf[i]] = std::move (noteEffects);
            if (ctx.pendingKeyShift != 0)
                ctx.keyShift[channelOf[i]] = ctx.pendingKeyShift;
            ctx.pendingKeyShift = 0;
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

    finishLinks (ctx); // (round 190ed)

    // Session clips: in the clip grid (round 181c, the default: the user's choice of 2026-11-08), or on the Playlist
    // after the arrangement, one section per scene (an option; the default before)
    const auto scenes = set.child ("Scenes").children ("Scene");
    if (options.sessionOnPlaylist)
    {
        std::int64_t end = 0;
        for (const auto& c : song.playlist)
            end = std::max (end, c.end());
        const auto bar = song.barAt (0).length; // (the song's meter, or the signature marker at 0)
        const auto roundUp = [bar] (std::int64_t t) { return bar > 0 ? (t + bar - 1) / bar * bar : t; };
        auto cursor = roundUp (end);
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
    }
    else
        sessionToGrid (ctx, set, tracks, rowOf, channelOf, insertOf, scenes);

    // (round 190bq) a Simpler in Slice mode: Live's first slice plays on C1, LYNX's on key 60 — its notes move up
    for (const auto& [channel, shift] : ctx.keyShift)
        for (auto& pattern : song.patterns)
            for (auto& n : pattern.notes)
                if (n.channel == channel)
                    n.key = std::clamp (n.key + shift, 0, 127);

    // (round 190cf) Live's MIDI effects that LYNX keeps as notes: each note of the channel through the chain, in order
    for (const auto& [channel, effects] : ctx.noteEffects)
        for (auto& pattern : song.patterns)
        {
            std::vector<Note> made;
            for (const auto& n : pattern.notes)
            {
                if (n.channel != channel)
                {
                    made.push_back (n);
                    continue;
                }
                const auto lengthBeats = (double) n.length / (double) ppq;
                std::vector<alsdev::NoteEdit> edits { { n.key, n.velocity, lengthBeats } };
                for (const auto& effect : effects)
                {
                    std::vector<alsdev::NoteEdit> next;
                    for (const auto& e : edits)
                        for (const auto& o : alsdev::applyNoteEffect (effect, e))
                            next.push_back (o);
                    edits = std::move (next);
                }
                for (const auto& e : edits)
                {
                    auto note = n;
                    note.key = std::clamp (e.key, 0, 127);
                    note.velocity = e.velocity;
                    if (e.lengthBeats != lengthBeats)
                        note.length = std::max<std::int64_t> (1, ticksOf (e.lengthBeats));
                    made.push_back (note);
                }
            }
            pattern.notes = std::move (made);
            pattern.sortNotes();
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
    // (round 190cg: with the Loop switch off the brace keeps its place [DATA: LoopOn false with LoopStart / LoopLength
    // in every stored set]; LYNX keeps the range too, switched off)
    if (const auto tr = set.child ("Transport"); tr.number ("LoopLength", 0) > 0)
    {
        song.loopStart = ticksOf (std::max (0.0, tr.number ("LoopStart", 0)));
        song.loopEnd = song.loopStart + ticksOf (tr.number ("LoopLength", 0));
        song.loopOff = ! tr.boolean ("LoopOn", false);
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
    // Live's Scale Mode → the project key (round 190o): the set's own scale when it is on (Live 12), else the scale of
    // the first clip that has one on (Live 11 has clip scales only). LYNX has one key per project.
    if (set.boolean ("InKey", false))
        projectkey::set (song, liveScaleOf (set));
    else if (ctx.clipKeySeen)
        projectkey::set (song, ctx.clipKey);
    return std::move (ctx.song);
}
} // namespace lynx
