#include "core/AlsExport.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#include "core/AlsImport.h"
#include "core/AlsSkeleton.h"
#include "core/ClipLauncher.h"
#include "core/Effects.h"
#include "core/FlWrapper.h"
#include "core/Gzip.h"
#include "core/Inflate.h"
#include "core/LiveScales.h"
#include "core/OggVorbis.h"
#include "core/Patcher.h"
#include "core/Rack.h"
#include "core/TimeStretch.h"
#include "core/XmlLite.h"

// Evidence tags as in docs/analise/ALS_FORMAT.md. [LIVE] = checked by opening the result in Live 12.4.

namespace lynx
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------
// A small mutable XML tree for writing

struct Node
{
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;
    std::vector<Node> kids;

    Node* child (std::string_view n)
    {
        for (auto& k : kids)
            if (k.name == n)
                return &k;
        return nullptr;
    }
    Node* at (std::string_view path)
    {
        Node* e = this;
        while (e != nullptr && ! path.empty())
        {
            const auto slash = path.find ('/');
            e = e->child (path.substr (0, slash));
            path = slash == std::string_view::npos ? std::string_view {} : path.substr (slash + 1);
        }
        return e;
    }
    void set (std::string_view key, std::string value)
    {
        for (auto& a : attrs)
            if (a.first == key)
            {
                a.second = std::move (value);
                return;
            }
        attrs.emplace_back (std::string (key), std::move (value));
    }
    std::string get (std::string_view key) const
    {
        for (const auto& a : attrs)
            if (a.first == key)
                return a.second;
        return {};
    }
    bool has (std::string_view key) const
    {
        return std::any_of (attrs.begin(), attrs.end(), [key] (const auto& a) { return a.first == key; });
    }
    // The "Value" of the member at `path` (false when the skeleton has no such member)
    bool value (std::string_view path, std::string v)
    {
        if (auto* e = at (path))
        {
            e->set ("Value", std::move (v));
            return true;
        }
        return false;
    }
    Node& add (Node n)
    {
        kids.push_back (std::move (n));
        return kids.back();
    }
};

Node fromElement (const xml::Element& e)
{
    Node n;
    n.name = std::string (e.name());
    n.attrs = e.attributes();
    n.text = e.text();
    for (const auto& c : e.children())
        n.kids.push_back (fromElement (c));
    return n;
}

void write (const Node& n, std::string& out, int depth)
{
    out.append ((std::size_t) depth, '\t');
    out += '<';
    out += n.name;
    for (const auto& [k, v] : n.attrs)
    {
        out += ' ';
        out += k;
        out += "=\"";
        out += xml::escape (v);
        out += '"';
    }
    if (n.kids.empty() && n.text.empty())
    {
        out += " />\n";
        return;
    }
    out += '>';
    if (! n.text.empty())
        out += xml::escape (n.text);
    if (! n.kids.empty())
    {
        out += '\n';
        for (const auto& k : n.kids)
            write (k, out, depth + 1);
        out.append ((std::size_t) depth, '\t');
    }
    out += "</";
    out += n.name;
    out += ">\n";
}

std::string num (double v)
{
    if (std::abs (v) < 1e-12)
        return "0";
   #if defined (__APPLE__)
    // Floating-point std::to_chars needs macOS 13.3 and LYNX targets 11.0: shortest precision that reads back exactly.
    for (int precision = 15;; ++precision)
    {
        std::ostringstream text;
        text.imbue (std::locale::classic());
        text << std::setprecision (precision) << v;
        std::istringstream back (text.str());
        back.imbue (std::locale::classic());
        double read = 0.0;
        back >> read;
        if (read == v || precision == 17)
            return text.str();
    }
   #else
    char buf[64];
    const auto r = std::to_chars (buf, buf + sizeof buf, v);
    return std::string (buf, r.ptr);
   #endif
}
std::string num (std::int64_t v) { return std::to_string (v); }
std::string num (int v) { return std::to_string (v); }
std::string boolean (bool b) { return b ? "true" : "false"; }

// The project key as Live's scale (round 190o) [LIVE 12.4 save: ScaleInformation { Root, Name = the scale chooser's
// index } beside the set's InKey and each MIDI clip's IsInKey; Live 11: a clip's ScaleInformation { RootNote, Name as
// text }]. A project with no key, or with a scale Live has none like, is written with Scale Mode off (C Major).
void writeLiveScale (Node& owner, const Song& song, std::string_view inKeyMember)
{
    const auto key = projectkey::get (song);
    const auto index = key ? livescales::indexFor (*key) : -1;
    auto* info = owner.child ("ScaleInformation");
    owner.value (inKeyMember, boolean (index >= 0 && info != nullptr)); // (the Live 12 skeleton's set has Scale Mode on)
    if (index < 0 || info == nullptr)
        return;
    if (! info->value ("Root", num (key->root)))
        info->value ("RootNote", num (key->root));
    if (auto* name = info->child ("Name"))
    {
        const auto old = name->get ("Value");
        const auto numeric = ! old.empty() && std::all_of (old.begin(), old.end(), [] (char ch) { return ch >= '0' && ch <= '9'; });
        name->set ("Value", numeric ? num (index) : std::string (livescales::scales()[(std::size_t) index].name));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Skeletons (core/AlsSkeleton.h): one map of element kind → element per version, decoded once

struct Skeletons
{
    std::map<std::string, Node> kinds;
    const Node& operator[] (const std::string& k) const
    {
        static const Node none;
        const auto it = kinds.find (k);
        return it != kinds.end() ? it->second : none;
    }
};

const Skeletons& skeletons (AlsVersion v)
{
    static Skeletons cache[2];
    static std::once_flag once[2];
    const auto i = v == AlsVersion::live11 ? 1 : 0;
    std::call_once (once[i], [i] {
        const auto* data = i == 1 ? alsskeleton::live11 : alsskeleton::live12;
        const auto size = i == 1 ? sizeof (alsskeleton::live11) : sizeof (alsskeleton::live12);
        const auto raw = i == 1 ? alsskeleton::live11Size : alsskeleton::live12Size;
        const auto text = inflateZlib (data, size, false, raw + 16);
        if (! text)
            return;
        xml::Document doc;
        if (! doc.parse (std::string (text->begin(), text->end())))
            return;
        for (const auto& kind : doc.root().children ("Kind"))
            if (const auto inner = kind.children(); ! inner.empty())
                cache[i].kinds[kind.attr ("name")] = fromElement (inner.front());
    });
    return cache[i];
}

// ---------------------------------------------------------------------------------------------------------------

constexpr double minGain = 0.0003162277571; // −70 dB: Live's fader floor [DATA]
constexpr double maxGain = 1.99526238;      // +6 dB [DATA]
constexpr double beforeSong = -63072000.0;  // [DATA]

double beats (std::int64_t ticks) { return (double) ticks / (double) ppq; }

int warpModeOf (const SamplerSettings& s)
{
    if (! s.keepPitch)
        return 3; // Re-Pitch
    switch (s.stretchMethod)
    {
        case StretchMethod::transients:
        case StretchMethod::beats: return 0;      // Beats
        case StretchMethod::mono:
        case StretchMethod::tones: return 1;      // Tones
        case StretchMethod::texture: return 2;    // Texture
        default: return 4;                        // Complex
    }
}

// What one Live track stands for
struct LiveTrack
{
    enum class Kind { midi, audio, group, ret } kind = Kind::midi;
    std::string name;
    std::uint32_t colour = 0;
    int insert = 0;   // the LYNX mixer insert whose fader / effects / sends the track carries (0: none of its own)
    int channel = -1; // midi: the channel
    int row = -1;     // audio: the Playlist track
    int group = -1;   // index of its group track in the list (-1: none)
    int bus = -1;     // the insert its own insert routes into (a group), -1: none
    bool ownsInsert = true; // carries its insert's sends and effects (false: a member of a shared insert's group)
    double gain = 1.0;
    float pan = 0.0f;
    bool muted = false, solo = false;
};

struct Exporter
{
    const Song& song;
    const AlsExportOptions& options;
    const Skeletons& sk;
    AlsExportResult& result;
    bool live11 = false;
    std::set<std::string> noted;
    int sceneCount = 1; // the set's scenes: the clip grid's rows (round 181c), else one

    void note (const std::string& what)
    {
        if (noted.insert (what).second)
            result.notes.push_back (what);
    }

    std::string mainName() const { return live11 ? "MasterTrack" : "MainTrack"; }
    std::string mainOut() const { return live11 ? "AudioOut/Master" : "AudioOut/Main"; }

    // ---- plugin devices
    bool pluginDevice (const PluginRef& ref, bool instrument, bool on, Node& out)
    {
        ForeignPlugin f = ref.foreign;
        if (f.format == ForeignPlugin::Format::none && options.pluginRecord)
            if (auto record = options.pluginRecord (ref, instrument))
            {
                f = std::move (*record);
                if (! ref.name.empty())
                    f.name = ref.name;
            }
        if (f.format == ForeignPlugin::Format::vst3)
        {
            out = sk["PluginDeviceVst3"];
            std::vector<std::uint8_t> component, controller;
            if (! f.state.empty() && ! splitFlVst3State (f.state, component, controller))
                note ("Plugin state not carried over: " + f.name);
            const auto fields = alsVst3Fields (f.classId);
            for (auto* info : { out.at ("PluginDesc/Vst3PluginInfo"), out.at ("PluginDesc/Vst3PluginInfo/Preset/Vst3Preset") })
            {
                if (info == nullptr)
                    continue;
                for (int k = 0; k < 4; ++k)
                    info->value ("Uid/Fields." + std::to_string (k), num (fields[(std::size_t) k]));
                info->value ("DeviceType", instrument ? "1" : "2");
                info->value ("Name", f.name);
            }
            if (auto* preset = out.at ("PluginDesc/Vst3PluginInfo/Preset/Vst3Preset"))
            {
                if (auto* p = preset->child ("ProcessorState"))
                    p->text = alsHexEncode (component);
                if (auto* c = preset->child ("ControllerState"))
                    c->text = alsHexEncode (controller);
            }
        }
        else if (f.format == ForeignPlugin::Format::vst2)
        {
            out = sk["PluginDeviceVst2"];
            auto* info = out.at ("PluginDesc/VstPluginInfo");
            if (info == nullptr)
                return false;
            info->value ("Path", f.path);
            info->value ("PlugName", f.name);
            info->value ("UniqueId", num ((std::int64_t) f.vst2Id));
            info->value ("Category", instrument ? "2" : "1");
            if (auto* preset = info->at ("Preset/VstPreset"))
            {
                preset->value ("UniqueId", num ((std::int64_t) f.vst2Id));
                FlVst2State st;
                if (! f.state.empty() && parseFlVst2State (f.state, st))
                {
                    if (! st.chunk.empty())
                    {
                        preset->value ("Type", num ((std::int64_t) 0x46426368)); // 'FBCh' [LIVE]
                        if (auto* b = preset->child ("Buffer"))
                            b->text = alsHexEncode (st.chunk);
                    }
                    else
                        note ("Plugin settings without a chunk not carried over: " + f.name);
                    preset->value ("ProgramNumber", num (st.currentProgram));
                }
            }
        }
        else
            return false;
        out.value ("On/Manual", boolean (on));
        return true;
    }


    // ---- racks (round 183c, REA_rodada183c): a LYNX rack as Live's Audio Effect / Instrument / MIDI Effect Rack, with
    // its chains, zones, chain mixers, macros, variations and the macro mappings onto what Live gets (plugins, nested
    // racks, chain volume / pan, the selector). LYNX's built-in effects in a chain are listed, as on a track.

    // A macro mapping on a parameter element: Live's KeyMidi (channel 16, the macro) before its Manual, and the range in
    // the parameter's own units [DATA]
    void addMapping (Node& parameter, int macro, double min, double max)
    {
        Node key = sk["MacroKeyMidi"];
        key.value ("NoteOrController", num (macro));
        auto at = std::find_if (parameter.kids.begin(), parameter.kids.end(), [] (const Node& k) { return k.name == "Manual"; });
        parameter.kids.insert (at, std::move (key));
        if (auto* range = parameter.child ("MidiControllerRange"))
        {
            range->value ("Min", num (min));
            range->value ("Max", num (max));
        }
    }

    bool rackDevice (const PatcherDoc& doc, bool on, const std::string& label, Node& out, int depth = 0)
    {
        const auto* r = rack::rackOf (doc);
        if (r == nullptr || depth > 8)
            return false;
        const auto kind = r->kind;
        const std::string tag = kind == RackDoc::Kind::instrument ? "InstrumentGroupDevice" : kind == RackDoc::Kind::midiEffect ? "MidiEffectGroupDevice" : "AudioEffectGroupDevice";
        const std::string branchTag = kind == RackDoc::Kind::instrument ? "InstrumentBranch" : kind == RackDoc::Kind::midiEffect ? "MidiEffectBranch" : "AudioEffectBranch";
        const std::string chainPath = kind == RackDoc::Kind::instrument ? "DeviceChain/MidiToAudioDeviceChain/Devices"
                                      : kind == RackDoc::Kind::midiEffect ? "DeviceChain/MidiToMidiDeviceChain/Devices"
                                                                         : "DeviceChain/AudioToAudioDeviceChain/Devices";
        out = sk[tag];
        if (out.name.empty())
            return false;
        out.value ("On/Manual", boolean (on));
        out.value ("UserName", ! r->name.empty() ? r->name : label);
        // where each node went: its device element, to put the macro mappings on
        std::map<int, std::pair<int, int>> placeOf; // node → (chain, device index)
        auto* branches = out.child ("Branches");
        for (int c = 0; c < (int) r->chains.size(); ++c)
        {
            const auto& chain = r->chains[(std::size_t) c];
            Node b = sk[branchTag];
            b.set ("Id", num (c));
            b.value ("Name/EffectiveName", chain.name);
            b.value ("Name/UserName", chain.name);
            b.value ("IsSoloed", boolean (chain.solo));
            b.value ("IsSelected", boolean (c == r->selectedChain));
            b.value ("MixerDevice/Speaker/Manual", boolean (chain.active));
            b.value ("MixerDevice/Volume/Manual", num (std::clamp ((double) chain.volume, minGain, maxGain)));
            b.value ("MixerDevice/Panorama/Manual", num ((double) chain.pan));
            auto zone = [&b] (const std::string& path, const RackZone& z) {
                b.value (path + "/Min", num (z.lo));
                b.value (path + "/Max", num (z.hi));
                b.value (path + "/CrossfadeMin", num (z.fadeLo));
                b.value (path + "/CrossfadeMax", num (z.fadeHi));
            };
            zone ("BranchSelectorRange", chain.select);
            zone ("ZoneSettings/KeyRange", chain.key);
            zone ("ZoneSettings/VelocityRange", chain.velocity);
            if (auto* devices = b.at (chainPath))
                for (const auto n : chain.nodes)
                {
                    if (n < 0 || n >= (int) doc.nodes.size())
                        continue;
                    const auto& node = doc.nodes[(std::size_t) n];
                    Node d;
                    bool made = false;
                    if (node.generator && node.channel.instrument == InstrumentKind::plugin)
                        made = pluginDevice (node.channel.plugin, true, true, d);
                    else if (node.generator && rack::isRack (node.channel))
                        made = rackDevice (node.channel.patcher, true, node.label, d, depth + 1);
                    else if (! node.generator && node.effect.kind == EffectKind::plugin)
                        made = pluginDevice (node.effect.plugin, false, node.effect.enabled, d);
                    else if (! node.generator && rack::isRack (node.effect))
                        made = rackDevice (node.effect.patcher, node.effect.enabled, node.label, d, depth + 1);
                    if (! made)
                    {
                        note ("Rack device not exported (LYNX built-in or not VST2 / VST3): " + patcher::nodeName (node) + " in " + chain.name);
                        continue;
                    }
                    d.set ("Id", num ((int) devices->kids.size()));
                    placeOf[n] = { c, (int) devices->kids.size() };
                    devices->add (std::move (d));
                }
            if (branches != nullptr)
                branches->add (std::move (b));
        }
        for (int k = 0; k < rackMacroCount; ++k)
        {
            const auto n = std::to_string (k);
            const auto& m = r->macros[(std::size_t) k];
            out.value ("MacroControls." + n + "/Manual", num ((double) m.value));
            out.value ("MacroDisplayNames." + n, m.name.empty() ? "Macro " + std::to_string (k + 1) : m.name);
            out.value ("MacroDefaults." + n, num (m.defaultValue < 0.0f ? -1.0 : (double) m.defaultValue));
            out.value ("ExcludeMacroFromRandomization." + n, boolean (m.excludeFromRandom));
            out.value ("ExcludeMacroFromSnapshots." + n, boolean (m.excludeFromVariations));
        }
        out.value ("NumVisibleMacroControls", num (r->shownMacros));
        out.value ("ChainSelector/Manual", num ((double) r->selector));
        out.value ("ChainSelectorFilterMidiCtrl", boolean (r->selectorFiltersCtrl));
        out.value ("IsAutoSelectEnabled", boolean (r->autoSelect));
        out.value ("AreMacroControlsVisible", boolean (r->showMacros));
        out.value ("IsBranchesListVisible", boolean (r->showChains));
        out.value ("AreDevicesVisible", boolean (r->showDevices));
        out.value ("IsRangesEditorVisible", boolean (r->zoneEditor >= 0));
        out.value ("AreMacroVariationsControlsVisible", boolean (r->showVariations));
        if (auto* snapshots = out.at ("MacroVariations/MacroSnapshots"))
            for (int i = 0; i < (int) r->variations.size(); ++i)
            {
                const auto& v = r->variations[(std::size_t) i];
                Node s = sk["MacroSnapshot"];
                s.set ("Id", num (i));
                s.value ("SnapshotName", v.name);
                for (int k = 0; k < rackMacroCount; ++k)
                {
                    const auto has = v.has[(std::size_t) k];
                    s.value ("MacroValues." + std::to_string (k), num (has ? (double) v.values[(std::size_t) k] : -1.0));
                    s.value ("MacroHasValue." + std::to_string (k), boolean (has));
                }
                snapshots->add (std::move (s));
            }
        // the mappings
        for (int k = 0; k < rackMacroCount; ++k)
            for (const auto& x : r->macros[(std::size_t) k].mappings)
            {
                Node* parameter = nullptr;
                double lo = x.min, hi = x.max;
                switch (x.target)
                {
                    case RackMapping::Target::selector:
                        parameter = out.child ("ChainSelector");
                        lo *= 127.0, hi *= 127.0;
                        break;
                    case RackMapping::Target::chainVolume:
                    case RackMapping::Target::chainPan:
                        if (branches != nullptr && x.chain >= 0 && x.chain < (int) branches->kids.size())
                            parameter = branches->kids[(std::size_t) x.chain].at (x.target == RackMapping::Target::chainVolume ? "MixerDevice/Volume" : "MixerDevice/Panorama");
                        if (x.target == RackMapping::Target::chainVolume)
                            lo = rack::volumeGain (x.min), hi = rack::volumeGain (x.max);
                        else
                            lo = x.min * 2.0 - 1.0, hi = x.max * 2.0 - 1.0;
                        break;
                    case RackMapping::Target::param:
                    {
                        const auto place = placeOf.find (x.node);
                        if (place == placeOf.end() || branches == nullptr)
                            break;
                        auto* devices = branches->kids[(std::size_t) place->second.first].at (chainPath);
                        if (devices == nullptr)
                            break;
                        auto& device = devices->kids[(std::size_t) place->second.second];
                        if (device.name == "AudioEffectGroupDevice" || device.name == "InstrumentGroupDevice" || device.name == "MidiEffectGroupDevice")
                        {
                            if (x.param < rackMacroCount)
                                parameter = device.child ("MacroControls." + std::to_string (x.param));
                            lo *= 127.0, hi *= 127.0;
                        }
                        else if (device.name == "PluginDevice" && device.at ("PluginDesc/VstPluginInfo") != nullptr)
                        {
                            // a VST2 parameter (Live keeps 0..1 and the parameter's index as its id [DATA]); VST3 ids are not
                            // LYNX's indices: listed
                            if (auto* list = device.child ("ParameterList"))
                            {
                                Node p = sk["PluginFloatParameter"];
                                p.set ("Id", num ((int) list->kids.size()));
                                p.value ("ParameterName", "");
                                p.value ("ParameterId", num (x.param));
                                p.value ("VisualIndex", num ((int) list->kids.size()));
                                p.value ("ParameterValue/Manual", num (rack::mappedValue (x, r->macros[(std::size_t) k].value)));
                                if (auto* lastUser = p.child ("LastUserRange"))
                                {
                                    lastUser->value ("First", "Invalid");
                                    lastUser->value ("Last", "Invalid");
                                }
                                list->add (std::move (p));
                                parameter = list->kids.back().child ("ParameterValue");
                            }
                        }
                        else
                            note ("Macro mapping not exported (a VST3 parameter): " + patcher::nodeName (doc.nodes[(std::size_t) x.node]));
                        break;
                    }
                }
                if (parameter != nullptr)
                    addMapping (*parameter, k, lo, hi);
            }
        return true;
    }

    // ---- tracks
    Node trackNode (const LiveTrack& t, int trackId, int returns, const std::vector<int>& groupIds)
    {
        const char* kind = t.kind == LiveTrack::Kind::midi ? "MidiTrack" : t.kind == LiveTrack::Kind::audio ? "AudioTrack"
                         : t.kind == LiveTrack::Kind::group                                   ? "GroupTrack"
                                                                                              : "ReturnTrack";
        Node n = sk[kind];
        n.set ("Id", num (trackId));
        n.value ("Name/EffectiveName", t.name);
        n.value ("Name/UserName", t.name);
        n.value ("Color", num (std::max (0, alsColourIndex (t.colour))));
        n.value ("TrackGroupId", num (t.group >= 0 ? groupIds[(std::size_t) t.group] : -1));
        // a group's audio input as Live saves it. The Live 11 group template (from a public 11.0.12 set) carries an input
        // routed to a track path of that set ("p46251/p41230/p36996"): Live 12.4 crashes loading it ("invalid map<K, T>
        // key", round 175a: every Live 11 export with a group track) [LIVE, DATA: Live 11.3 / 12 groups]
        if (auto* in = n.at ("DeviceChain/AudioInputRouting"); in != nullptr && t.kind == LiveTrack::Kind::group)
        {
            in->value ("Target", "AudioIn/External/S0");
            in->value ("UpperDisplayString", "Ext. In");
            in->value ("LowerDisplayString", "1/2");
        }
        if (auto* out = n.at ("DeviceChain/AudioOutputRouting"))
        {
            // an insert that feeds only other inserts (FL's "Master" send off): no output, its sends carry it
            const auto noOutput = t.group < 0 && t.insert > 0 && t.kind != LiveTrack::Kind::ret && ! song.mixer[(std::size_t) t.insert].toMaster;
            out->value ("Target", t.group >= 0 ? "AudioOut/GroupTrack" : noOutput ? "AudioOut/None" : mainOut());
            out->value ("UpperDisplayString", t.group >= 0 ? "Group" : noOutput ? "No Output" : "Master");
            out->value ("LowerDisplayString", "");
        }
        if (auto* mixer = n.at ("DeviceChain/Mixer"))
        {
            mixer->value ("Volume/Manual", num (std::clamp (t.gain, minGain, maxGain)));
            mixer->value ("Pan/Manual", num ((double) std::clamp (t.pan, -1.0f, 1.0f)));
            mixer->value ("Speaker/Manual", boolean (! t.muted));
            mixer->value ("SoloSink", boolean (t.solo));
            if (auto* sends = mixer->child ("Sends"))
            {
                sends->kids.clear();
                for (int r = 0; r < returns; ++r)
                {
                    Node h = sk["TrackSendHolder"];
                    h.set ("Id", num (r));
                    h.value ("Send/Manual", num (minGain));
                    // a return track's sends to the returns are off, as Live saves them [LIVE: its default set and
                    // demo sets; round 182b]
                    if (t.kind == LiveTrack::Kind::ret)
                        h.value ("EnabledByUser", "false");
                    sends->add (std::move (h));
                }
            }
        }
        // a group track: one GroupTrackSlot per scene in its own Slots list, and NO freeze clip slots (Live 11.0–12.2
        // save it empty; "List of group track freeze sequencer slots not empty" otherwise) [LIVE, DATA]
        if (auto* groupSlots = n.child ("Slots"); groupSlots != nullptr && ! groupSlots->kids.empty())
        {
            const Node slot = groupSlots->kids.front();
            groupSlots->kids.clear();
            for (int s = 0; s < sceneCount; ++s)
            {
                Node one = slot;
                one.set ("Id", num (s));
                groupSlots->add (std::move (one));
            }
        }
        // one clip slot per scene, in the main and the freeze sequencer [LIVE: §9 rule 6]
        for (const auto* seq : { "DeviceChain/MainSequencer/ClipSlotList", "DeviceChain/FreezeSequencer/ClipSlotList" })
            if (auto* slots = n.at (seq))
            {
                slots->kids.clear();
                // group and return tracks keep no clip slots of their own: Live saves a return track's freeze list
                // empty too, and Live 12.4 crashed (access violation) loading a set whose return had one [LIVE,
                // round 182b: Thief / Africa's Calling / Mic Check Ready exports]
                if (t.kind == LiveTrack::Kind::group || t.kind == LiveTrack::Kind::ret)
                    continue;
                for (int s = 0; s < sceneCount; ++s)
                {
                    Node slot = sk["ClipSlot"];
                    slot.set ("Id", num (s));
                    slots->add (std::move (slot));
                }
            }
        return n;
    }

    void setSend (Node& track, int returnIndex, double level)
    {
        if (auto* sends = track.at ("DeviceChain/Mixer/Sends"); sends != nullptr && returnIndex < (int) sends->kids.size())
            sends->kids[(std::size_t) returnIndex].value ("Send/Manual", num (std::clamp (level, minGain, maxGain)));
    }

    void addDevice (Node& track, Node device)
    {
        if (auto* devices = track.at ("DeviceChain/DeviceChain/Devices"))
        {
            device.set ("Id", num ((int) devices->kids.size()));
            devices->add (std::move (device));
        }
    }

    void insertEffects (Node& track, int insert)
    {
        if (insert < 0 || insert >= (int) song.mixer.size())
            return;
        // round 190bu: a Live track has no dry/wet for its whole device chain
        if (const auto& m = song.mixer[(std::size_t) insert]; m.chainBlend < 1.0f && ! m.effects.empty())
            note ("The effect chain's Blend is not exported (the devices play at full): " + m.name);
        for (const auto& fx : song.mixer[(std::size_t) insert].effects)
        {
            if (fx.kind == EffectKind::plugin)
            {
                Node d;
                if (pluginDevice (fx.plugin, false, fx.enabled, d))
                {
                    addDevice (track, std::move (d));
                    continue;
                }
            }
            if (rack::isRack (fx)) // (round 183c)
            {
                Node d;
                if (rackDevice (fx.patcher, fx.enabled, {}, d))
                {
                    addDevice (track, std::move (d));
                    continue;
                }
            }
            note ((fx.kind == EffectKind::plugin ? "Plugin not exported (not VST2 or VST3, or not found): " : "Effect not exported (LYNX built-in): ")
                  + (fx.plugin.name.empty() ? std::string (effectInfo (fx.kind).name) : fx.plugin.name) + " on " + song.mixer[(std::size_t) insert].name);
        }
    }

    // ---- clips
    Node midiClip (const Pattern& p, int channel, std::int64_t start, std::int64_t length, std::int64_t offset, bool muted, int id)
    {
        Node c = sk["MidiClip"];
        c.set ("Id", num (id));
        c.set ("Time", num (beats (start)));
        const auto len = beats (length);
        c.value ("CurrentStart", num (beats (start)));
        c.value ("CurrentEnd", num (beats (start + length)));
        c.value ("Name", p.name);
        c.value ("Color", num (std::max (0, alsColourIndex (p.colour))));
        c.value ("Disabled", boolean (muted));
        writeLiveScale (c, song, "IsInKey");
        if (auto* loop = c.child ("Loop"))
        {
            loop->value ("LoopStart", "0");
            loop->value ("LoopEnd", num (len));
            loop->value ("StartRelative", "0");
            loop->value ("LoopOn", "false");
            loop->value ("OutMarker", num (len));
            loop->value ("HiddenLoopStart", "0");
            loop->value ("HiddenLoopEnd", num (len));
        }
        std::map<int, std::vector<const Note*>> byKey;
        for (const auto& n : p.notes)
            if (n.channel == channel && n.start >= offset && n.start < offset + length)
                byKey[n.key].push_back (&n);
        auto* keyTracks = c.at ("Notes/KeyTracks");
        int noteId = 1, kt = 0;
        // Live 12's probability groups (round 187c): each group's notes by NoteId [LIVE 12.4 save: NoteProbabilityGroup
        // { GroupType 0 = Play All / 1 = Play One, Notes/NoteId, Probability, GroupId }]
        struct ChanceGroup
        {
            bool playOne = false;
            float chance = 1.0f;
            std::vector<int> ids;
        };
        std::map<int, ChanceGroup> chanceGroups;
        if (keyTracks != nullptr)
        {
            keyTracks->kids.clear();
            for (const auto& [key, notes] : byKey)
            {
                Node track = sk["KeyTrack"];
                track.set ("Id", num (kt++));
                track.value ("MidiKey", num (key));
                auto* list = track.child ("Notes");
                if (list == nullptr)
                    continue;
                list->kids.clear();
                for (const auto* n : notes)
                {
                    Node e;
                    e.name = "MidiNoteEvent";
                    e.set ("Time", num (beats (n->start - offset)));
                    e.set ("Duration", num (beats (std::min (n->length, offset + length - n->start))));
                    e.set ("Velocity", num (std::clamp ((double) n->velocity * 127.0, 1.0, 127.0)));
                    // Live 11 writes every attribute; Live 12 leaves out the default ones (same order) [DATA]
                    // the note's Velocity Range and Chance (round 187c)
                    const auto deviation = std::round (std::clamp ((double) n->velocityRange * 127.0, -127.0, 127.0) * 1.0e4) / 1.0e4;
                    if (live11 || deviation != 0.0)
                        e.set ("VelocityDeviation", num (deviation));
                    e.set ("OffVelocity", num (std::clamp ((double) n->release * 127.0, 0.0, 127.0)));
                    if (live11 || n->chance < 1.0f)
                        e.set ("Probability", num (std::clamp ((double) n->chance, 0.0, 1.0)));
                    if (live11 || n->muted)
                        e.set ("IsEnabled", n->muted ? "false" : "true");
                    if (n->chanceGroup > 0 && ! live11)
                    {
                        auto& g = chanceGroups[n->chanceGroup];
                        g.playOne = n->chanceOne;
                        g.chance = n->chance;
                        g.ids.push_back (noteId);
                    }
                    e.set ("NoteId", num (noteId++));
                    list->add (std::move (e));
                }
                keyTracks->add (std::move (track));
            }
        }
        c.value ("Notes/NoteIdGenerator/NextId", num (noteId));
        if (auto* groups = c.at ("Notes/NoteProbabilityGroups"); groups != nullptr && ! chanceGroups.empty())
        {
            int index = 0;
            for (const auto& [id, g] : chanceGroups)
            {
                if (g.ids.size() < 2)
                    continue; // (a group cut down to one note by the clip's span plays as that note)
                Node group;
                group.name = "NoteProbabilityGroup";
                group.set ("Id", num (index++));
                Node type;
                type.name = "GroupType";
                type.set ("Value", g.playOne ? "1" : "0");
                group.add (std::move (type));
                Node ids;
                ids.name = "Notes";
                for (auto noteIdOf : g.ids)
                {
                    Node one;
                    one.name = "NoteId";
                    one.set ("NoteId", num (noteIdOf));
                    ids.add (std::move (one));
                }
                group.add (std::move (ids));
                Node probability;
                probability.name = "Probability";
                probability.set ("Value", num (std::clamp ((double) g.chance, 0.0, 1.0)));
                group.add (std::move (probability));
                Node groupId;
                groupId.name = "GroupId";
                groupId.set ("Value", num (index));
                group.add (std::move (groupId));
                groups->add (std::move (group));
            }
            c.value ("Notes/ProbabilityGroupIdGenerator/NextId", num (index + 1));
        }
        return c;
    }

    Node audioClip (const Clip& clip, int id)
    {
        Node c = sk["AudioClip"];
        const auto& ch = song.channels[(std::size_t) clip.channel];
        c.set ("Id", num (id));
        c.set ("Time", num (beats (clip.start)));
        c.value ("CurrentStart", num (beats (clip.start)));
        c.value ("CurrentEnd", num (beats (clip.end())));
        c.value ("Name", ch.name);
        c.value ("Disabled", boolean (clip.muted));
        const auto mode = warpModeOf (ch.sampler);
        c.value ("IsWarped", "true");
        c.value ("WarpMode", num (mode));
        // a Beats channel's offline transposition (stretchPitch) is the clip's transpose in Live
        const auto baked = mode == 0 && ch.sampler.keepPitch && ch.sampler.stretchBeats > 0.0f ? (double) ch.sampler.stretchPitch : 0.0;
        const auto pitch = mode == 3 ? 0.0 : std::clamp ((double) clip.pitch + baked, -48.0, 48.0);
        const auto coarse = std::round (pitch);
        c.value ("PitchCoarse", num (coarse));
        c.value ("PitchFine", num ((pitch - coarse) * 100.0));
        c.value ("SampleVolume", num ((double) clip.gain));
        // content beats ↔ sample seconds: one rate, the clip's stretch [import: stretch = beats per second · 60 / bpm]
        const auto seconds = options.sampleSeconds ? options.sampleSeconds (ch.samplePath) : 0.0;
        const auto spanSeconds = seconds > 0.0 ? seconds : 1.0;
        // a channel stretched to a length in beats (an imported warped clip, FL's stretch): its beats per file second
        const auto beatsPerSecond = (ch.sampler.keepPitch && ch.sampler.stretchBeats > 0.0f && seconds > 0.0 ? (double) ch.sampler.stretchBeats / seconds
                                                                                                             : song.bpm / 60.0)
                                    * clip.stretch;
        // the markers: the file's start and end, and in between the channel's warp markers when they bend the tempo
        // (round 175a, M1), each at its content beat (the file spans stretchBeats · the clip's stretch)
        std::vector<std::pair<double, double>> points { { 0.0, 0.0 }, { spanSeconds, spanSeconds * beatsPerSecond } };
        if (ch.sampler.keepPitch && ch.sampler.stretchBeats > 0.0f && seconds > 0.0
            && ! warpStretchMap (ch.sampler.warp, seconds * 1000.0, 1000.0, (double) ch.sampler.stretchBeats * 1000.0).empty())
        {
            StretchMap beatsOf;
            for (const auto& w : ch.sampler.warp)
                if (beatsOf.empty() || (w.seconds > beatsOf.back().first + 1e-9 && w.beats > beatsOf.back().second + 1e-9))
                    beatsOf.push_back ({ w.seconds, w.beats });
            const auto b0 = stretchMapOutput (beatsOf, 0.0), b1 = stretchMapOutput (beatsOf, seconds);
            const auto scale = (double) ch.sampler.stretchBeats * clip.stretch / (b1 - b0);
            points = { { 0.0, 0.0 } };
            for (const auto& [sec, beat] : beatsOf)
                if (sec > 1e-6 && sec < seconds - 1e-6)
                    points.push_back ({ sec, (beat - b0) * scale });
            points.push_back ({ seconds, (b1 - b0) * scale });
        }
        if (auto* markers = c.child ("WarpMarkers"))
        {
            markers->kids.clear();
            for (std::size_t k = 0; k < points.size(); ++k)
            {
                Node m;
                m.name = "WarpMarker";
                m.set ("Id", num ((int) k));
                m.set ("SecTime", num (points[k].first));
                m.set ("BeatTime", num (points[k].second));
                markers->add (std::move (m));
            }
        }
        const auto from = beats (clip.offset) + (double) clip.offsetFraction / ppq;
        const auto to = from + beats (clip.length);
        if (auto* loop = c.child ("Loop"))
        {
            loop->value ("LoopStart", num (from));
            loop->value ("LoopEnd", num (to));
            loop->value ("StartRelative", "0");
            loop->value ("LoopOn", "false");
            loop->value ("OutMarker", num (to));
            loop->value ("HiddenLoopStart", num (from));
            loop->value ("HiddenLoopEnd", num (to));
        }
        c.value ("Fades/FadeInLength", num (beats (clip.fadeIn)));
        c.value ("Fades/FadeOutLength", num (beats (clip.fadeOut)));
        if (auto* ref = c.at ("SampleRef/FileRef"))
        {
            bool relative = false;
            const auto path = options.samplePath ? options.samplePath (ch.samplePath, relative) : ch.samplePath;
            ref->value ("RelativePathType", relative ? "1" : "0");
            ref->value ("RelativePath", relative ? path : std::string());
            ref->value ("Path", relative ? std::string() : path);
            ref->value ("Type", "1");
            ref->value ("LivePackName", "");
            ref->value ("LivePackId", "");
            ref->value ("OriginalFileSize", "0");
            ref->value ("OriginalCrc", "0");
        }
        if (auto* sref = c.child ("SampleRef"))
        {
            sref->value ("LastModDate", "0");
            sref->value ("DefaultDuration", "0");
            sref->value ("DefaultSampleRate", "0");
        }
        if (clip.reversed)
            note ("Reversed audio clips are exported forwards");
        return c;
    }

    // ---- the clip grid → Session view (round 181c). Launch numbers as read in Live 12's Clip View [LIVE: REA_rodada181a.md]
    void sessionSettings (Node& c, const ClipGridSlot& slot, double contentStart, double contentEnd)
    {
        if (! slot.name.empty())
            c.value ("Name", slot.name);
        if (slot.colour != 0)
            c.value ("Color", num (std::max (0, alsColourIndex (slot.colour))));
        c.value ("LaunchMode", num ((int) slot.mode));
        c.value ("LaunchQuantisation", num ((int) slot.quant));
        c.value ("Legato", boolean (slot.legato));
        if (auto* loop = c.child ("Loop"))
        {
            loop->value ("LoopStart", num (contentStart));
            loop->value ("LoopEnd", num (contentEnd));
            loop->value ("StartRelative", "0");
            loop->value ("LoopOn", boolean (slot.loop));
            loop->value ("OutMarker", num (contentEnd));
        }
        const auto& f = slot.follow;
        c.value ("FollowAction/FollowTime", num (beats (f.ticks)));
        c.value ("FollowAction/IsLinked", boolean (f.linked));
        c.value ("FollowAction/LoopIterations", num (f.iterations));
        c.value ("FollowAction/FollowActionA", num ((int) f.a));
        c.value ("FollowAction/FollowActionB", num ((int) f.b));
        c.value ("FollowAction/FollowChanceA", num (f.chanceA));
        c.value ("FollowAction/FollowChanceB", num (f.chanceB));
        c.value ("FollowAction/JumpIndexA", num (f.jumpA + 1)); // Live's jump number is 1-based [LIVE, 181c]
        c.value ("FollowAction/JumpIndexB", num (f.jumpB + 1));
        c.value ("FollowAction/FollowActionEnabled", boolean (f.enabled));
    }

    // The slot node of `scene` in a track's main clip slot list (nullptr past the list)
    Node* sessionSlot (Node& track, int scene)
    {
        auto* slots = track.at ("DeviceChain/MainSequencer/ClipSlotList");
        return slots != nullptr && scene >= 0 && scene < (int) slots->kids.size() ? &slots->kids[(std::size_t) scene] : nullptr;
    }

    // Puts a session clip in a slot; false when the slot already holds one (two LYNX clips met in one Live slot)
    bool putSessionClip (Node& track, int scene, Node clip)
    {
        auto* slot = sessionSlot (track, scene);
        auto* value = slot != nullptr ? slot->at ("ClipSlot/Value") : nullptr;
        if (value == nullptr || ! value->kids.empty())
            return false;
        value->add (std::move (clip));
        return true;
    }

    // ---- automation
    // `eventName` "EnumEvent": whole-number values at exact times, the first event already the "before the song" one
    void addEnvelope (Node& owner, const std::string& pointee, const std::vector<std::pair<double, double>>& events, int& envelopeId,
                      const char* eventName = "FloatEvent")
    {
        auto* list = owner.at ("AutomationEnvelopes/Envelopes");
        if (list == nullptr || events.empty() || pointee.empty())
            return;
        Node env = sk["AutomationEnvelope"];
        env.set ("Id", num (envelopeId++));
        env.value ("EnvelopeTarget/PointeeId", pointee);
        if (auto* ev = env.at ("Automation/Events"))
        {
            ev->kids.clear();
            int id = 0;
            const auto enums = std::string (eventName) == "EnumEvent";
            auto add = [&] (double t, double v) {
                Node e;
                e.name = eventName;
                e.set ("Id", num (id++));
                e.set ("Time", num (t));
                e.set ("Value", enums ? num ((int) std::lround (v)) : num (v));
                ev->add (std::move (e));
            };
            // Live's own envelopes open with the "before the song" event; a point at the very start is that event [DATA]
            add (beforeSong, events.front().second);
            for (std::size_t i = events.front().first <= 0.0 ? 1 : 0; i < events.size(); ++i)
                add (events[i].first, events[i].second);
        }
        list->add (std::move (env));
    }

    // ---------------------------------------------------------------------------------------------------------------
    void run()
    {
        live11 = sk.kinds.count ("Root") > 0 && sk["Root"].get ("MinorVersion").rfind ("11.", 0) == 0;
        Node root = sk["Root"];
        root.set ("Creator", options.creator);
        Node set = sk["LiveSet"];

        // which channels play notes (in any pattern placed on the Playlist, or the current pattern for a pattern-mode song)
        std::vector<const Clip*> patternClips, audioClips, automationClips;
        for (const auto& c : song.playlist)
        {
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
                patternClips.push_back (&c);
            else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
                audioClips.push_back (&c);
            else if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < (int) song.automations.size())
                automationClips.push_back (&c);
            else if (c.kind != ClipKind::pattern && c.kind != ClipKind::audio && c.kind != ClipKind::automation)
                note ("Playlist clips of this kind are not exported (video, pattern blocks...)");
        }
        // the clip grid → Session view (round 181c): one Live scene per grid row
        std::vector<const Clip*> gridPatternClips, gridAudioClips;
        const ClipGrid* grid = song.clipGrid ? &*song.clipGrid : nullptr;
        if (grid != nullptr)
            for (const auto& c : grid->clips)
            {
                if (c.length <= 0 || c.gridSlot < 0 || c.gridSlot >= (int) grid->slots.size())
                    continue;
                if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
                    gridPatternClips.push_back (&c);
                else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
                    gridAudioClips.push_back (&c);
            }
        const auto session = ! gridPatternClips.empty() || ! gridAudioClips.empty();
        if (session)
            sceneCount = std::clamp ((int) grid->scenes.size(), 1, launcher::maxScenes);

        Clip patternModeClip;
        if (patternClips.empty() && audioClips.empty() && ! session && ! song.patterns.empty())
        {
            const auto cur = std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
            patternModeClip.pattern = cur;
            patternModeClip.length = std::max<std::int64_t> (song.patterns[(std::size_t) cur].lengthTicks, (std::int64_t) song.beatsPerBar * ppq);
            patternClips.push_back (&patternModeClip);
            note ("The song had no Playlist clips: the current pattern is exported at the start");
        }
        std::vector<bool> plays (song.channels.size(), false);
        for (const auto* c : gridPatternClips)
            for (const auto& n : song.patterns[(std::size_t) c->pattern].notes)
                if (n.channel >= 0 && n.channel < (int) plays.size())
                    plays[(std::size_t) n.channel] = true;
        for (const auto* c : patternClips)
            for (const auto& n : song.patterns[(std::size_t) c->pattern].notes)
                if (n.channel >= 0 && n.channel < (int) plays.size())
                    plays[(std::size_t) n.channel] = true;

        // the Live tracks
        std::vector<LiveTrack> tracks;
        std::map<int, int> perInsert; // insert → how many tracks use it
        for (std::size_t i = 0; i < song.channels.size(); ++i)
        {
            const auto& ch = song.channels[i];
            if (ch.instrument == InstrumentKind::audioClip)
                continue;
            // (a plugin or an Instrument Rack (round 183c) is a device of its own: its track goes out even with no notes)
            const auto plugin = ch.instrument == InstrumentKind::plugin || rack::isRack (ch);
            if (! plays[i] && ! plugin)
                continue;
            LiveTrack t;
            t.kind = LiveTrack::Kind::midi;
            t.name = ch.name;
            t.colour = ch.colour;
            t.channel = (int) i;
            t.insert = std::clamp (ch.mixerTrack, 0, (int) song.mixer.size() - 1);
            // (a hosted plugin in a project with FL's levels plays √2 over the curve, round 190ad)
            t.gain = channelGain (ch.volume) * (plugin && song.flPluginLevels ? 1.41421356f : 1.0f);
            t.pan = ch.pan;
            tracks.push_back (t);
            if (! plugin && ch.instrument != InstrumentKind::plugin)
                note ("Instrument not exported (LYNX built-in, notes kept): " + ch.name);
        }
        std::map<int, std::vector<const Clip*>> rows;
        for (const auto* c : audioClips)
            rows[c->track].push_back (c);
        for (const auto* c : gridAudioClips)
            rows[c->track].push_back (c);
        for (const auto& [row, clips] : rows)
        {
            LiveTrack t;
            t.kind = LiveTrack::Kind::audio;
            t.row = row;
            t.name = row < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) row].name.empty()
                         ? song.playlistTracks[(std::size_t) row].name
                         : song.channels[(std::size_t) clips.front()->channel].name;
            t.colour = row < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) row].colour : 0;
            const auto& ch = song.channels[(std::size_t) clips.front()->channel];
            t.insert = std::clamp (ch.mixerTrack, 0, (int) song.mixer.size() - 1);
            // a Playlist audio clip plays at unity at FL's default channel volume [Instruments.cpp audioClipUnity]
            t.gain = channelGain (ch.volume) / channelGain (10000.0f / 12800.0f);
            t.pan = ch.pan;
            tracks.push_back (t);
            for (const auto* c : clips)
                if (song.channels[(std::size_t) c->channel].mixerTrack != ch.mixerTrack)
                    note ("Audio clips of one Playlist track on different mixer inserts: the first insert is used (" + t.name + ")");
        }
        for (const auto& t : tracks)
            if (t.insert > 0)
                ++perInsert[t.insert];

        // buses: an insert with no track of its own that other inserts route into (their only output, no Master)
        // becomes a group track, as Live's own groups import (AlsImport: the members' inserts feed the group's insert)
        auto busOf = [&] (int m) -> int {
            if (m <= 0 || m >= (int) song.mixer.size())
                return -1;
            const auto& mx = song.mixer[(std::size_t) m];
            if (mx.toMaster || mx.sends.size() != 1)
                return -1;
            const auto target = mx.sends.front().target;
            return target > 0 && target < (int) song.mixer.size() && target != m && perInsert.count (target) == 0 ? target : -1;
        };

        // returns: the other inserts with no track of their own that inserts send to
        std::set<int> sendTargets;
        for (std::size_t m = 1; m < song.mixer.size(); ++m)
            for (const auto& s : song.mixer[m].sends)
                if (s.target != busOf ((int) m))
                    sendTargets.insert (s.target);
        std::set<int> buses;
        for (std::size_t m = 1; m < song.mixer.size(); ++m)
            if (const auto b = busOf ((int) m); b > 0)
                buses.insert (b);
        std::vector<int> returnInsert;
        std::map<int, int> returnOf; // insert → return index
        for (const auto target : sendTargets)
            if (target > 0 && target < (int) song.mixer.size() && perInsert.count (target) == 0 && ! buses.count (target))
            {
                returnOf[target] = (int) returnInsert.size();
                returnInsert.push_back (target);
            }

        // groups: an insert shared by several tracks (the tracks keep the channel's own level), and buses; a group
        // routed into a bus nests in that bus's group. Items in order of first appearance, then listed depth-first:
        // Live lists a group's tracks right after it.
        std::vector<LiveTrack> items;
        std::vector<int> parentOf;   // per item: the insert of its group (-1: top level)
        std::map<int, int> groupItem; // group insert → item
        std::function<void (int, int)> ensureGroup = [&] (int insert, int depth) {
            if (groupItem.count (insert) || depth > 16)
                return;
            const auto parent = busOf (insert);
            if (parent > 0)
                ensureGroup (parent, depth + 1);
            const auto& m = song.mixer[(std::size_t) insert];
            LiveTrack g;
            g.kind = LiveTrack::Kind::group;
            g.name = m.name;
            g.colour = m.colour;
            g.insert = insert;
            g.bus = parent;
            g.gain = faderGain (m.volume);
            g.pan = m.pan;
            g.muted = m.muted;
            g.solo = m.solo;
            groupItem[insert] = (int) items.size();
            items.push_back (g);
            parentOf.push_back (parent > 0 && groupItem.count (parent) ? parent : -1);
        };
        for (const auto& t0 : tracks)
        {
            auto t = t0;
            const auto shared = t.insert > 0 && perInsert[t.insert] > 1;
            int parent = -1;
            if (shared)
            {
                ensureGroup (t.insert, 0);
                parent = t.insert;
                t.ownsInsert = false;
            }
            else if (t.insert > 0)
            {
                // the insert folds into the track: its fader multiplies, its pan adds, its state is the track's
                const auto& m = song.mixer[(std::size_t) t.insert];
                t.gain *= faderGain (m.volume);
                t.pan = std::clamp (t.pan + m.pan, -1.0f, 1.0f);
                t.muted = m.muted;
                t.solo = m.solo;
                t.bus = busOf (t.insert);
                if (t.bus > 0)
                {
                    ensureGroup (t.bus, 0);
                    parent = t.bus;
                }
            }
            items.push_back (t);
            parentOf.push_back (parent);
        }
        std::vector<LiveTrack> ordered;
        {
            std::vector<int> newIndex (items.size(), -1);
            std::function<void (std::size_t, int)> emit = [&] (std::size_t i, int groupIndex) {
                newIndex[i] = (int) ordered.size();
                ordered.push_back (items[i]);
                ordered.back().group = groupIndex;
                if (items[i].kind == LiveTrack::Kind::group)
                {
                    const auto self = newIndex[i];
                    for (std::size_t j = 0; j < items.size(); ++j)
                        if (parentOf[j] == items[i].insert && newIndex[j] < 0 && j != i)
                            emit (j, self);
                }
            };
            for (std::size_t i = 0; i < items.size(); ++i)
                if (parentOf[i] < 0 && newIndex[i] < 0)
                    emit (i, -1);
        }
        for (const auto r : returnInsert)
        {
            const auto& m = song.mixer[(std::size_t) r];
            LiveTrack t;
            t.kind = LiveTrack::Kind::ret;
            t.name = m.name;
            t.colour = m.colour;
            t.insert = r;
            t.gain = faderGain (m.volume);
            t.pan = m.pan;
            t.muted = m.muted;
            t.solo = m.solo;
            ordered.push_back (t);
        }
        for (std::size_t m = 1; m < song.mixer.size(); ++m)
            if (perInsert.count ((int) m) == 0 && ! returnOf.count ((int) m) && ! buses.count ((int) m) && ! song.mixer[m].effects.empty())
                note ("Mixer insert with effects but no channel not exported: " + song.mixer[m].name);

        // nodes
        const auto returns = (int) returnInsert.size();
        std::vector<int> trackIds (ordered.size());
        for (std::size_t i = 0; i < ordered.size(); ++i)
            trackIds[i] = 8 + (int) i;
        std::vector<Node> nodes;
        int clipId = 0;
        for (std::size_t i = 0; i < ordered.size(); ++i)
        {
            const auto& t = ordered[i];
            Node n = trackNode (t, trackIds[i], returns, trackIds);
            // sends of the insert this track carries (a member of a group: the group carries them)
            if (t.insert > 0 && t.ownsInsert)
                for (const auto& s : song.mixer[(std::size_t) t.insert].sends)
                {
                    if (s.target == t.bus)
                        continue; // the route into its group
                    if (const auto it = returnOf.find (s.target); it != returnOf.end())
                        setSend (n, it->second, s.level);
                    else
                        note ("Mixer routing between inserts not exported (only sends to return-style inserts): " + song.mixer[(std::size_t) t.insert].name);
                }

            if (t.kind == LiveTrack::Kind::midi)
            {
                const auto& ch = song.channels[(std::size_t) t.channel];
                if (ch.instrument == InstrumentKind::plugin)
                {
                    Node d;
                    if (pluginDevice (ch.plugin, true, true, d))
                        addDevice (n, std::move (d));
                    else
                        note ("Plugin not exported (not VST2 or VST3, or not found): " + ch.name);
                }
                else if (rack::isRack (ch)) // (round 183c: an Instrument Rack)
                {
                    Node d;
                    if (rackDevice (ch.patcher, true, {}, d))
                        addDevice (n, std::move (d));
                }
                if (auto* events = n.at ("DeviceChain/MainSequencer/ClipTimeable/ArrangerAutomation/Events"))
                    for (const auto* c : patternClips)
                    {
                        const auto& p = song.patterns[(std::size_t) c->pattern];
                        const auto has = std::any_of (p.notes.begin(), p.notes.end(), [&] (const Note& x) {
                            return x.channel == t.channel && x.start >= c->offset && x.start < c->offset + c->length;
                        });
                        if (has)
                            events->add (midiClip (p, t.channel, c->start, c->length, c->offset, c->muted, clipId++));
                    }
                for (const auto* c : gridPatternClips)
                {
                    const auto& p = song.patterns[(std::size_t) c->pattern];
                    const auto has = std::any_of (p.notes.begin(), p.notes.end(), [&] (const Note& x) {
                        return x.channel == t.channel && x.start >= c->offset && x.start < c->offset + c->length;
                    });
                    if (! has)
                        continue;
                    const auto& slot = grid->slots[(std::size_t) c->gridSlot];
                    Node clip = midiClip (p, t.channel, 0, c->length, c->offset, c->muted, clipId++);
                    sessionSettings (clip, slot, 0.0, beats (c->length));
                    if (! putSessionClip (n, slot.scene, std::move (clip)))
                        note ("Grid clips of one channel in the same scene: only the first is exported (" + ch.name + ")");
                }
            }
            else if (t.kind == LiveTrack::Kind::audio)
            {
                if (auto* events = n.at ("DeviceChain/MainSequencer/Sample/ArrangerAutomation/Events"))
                    for (const auto* c : audioClips)
                        if (c->track == t.row)
                            events->add (audioClip (*c, clipId++));
                for (const auto* c : gridAudioClips)
                    if (c->track == t.row)
                    {
                        const auto& slot = grid->slots[(std::size_t) c->gridSlot];
                        Node clip = audioClip (*c, clipId++);
                        const auto from = beats (c->offset) + (double) c->offsetFraction / ppq;
                        sessionSettings (clip, slot, from, from + beats (c->length));
                        putSessionClip (n, slot.scene, std::move (clip));
                    }
            }
            // an empty slot without its stop button: the Playlist track's (an instrument track's channel, or the row
            // of the channel's first grid clip)
            if (session && (t.kind == LiveTrack::Kind::midi || t.kind == LiveTrack::Kind::audio))
            {
                auto row = t.kind == LiveTrack::Kind::audio ? t.row : -1;
                for (std::size_t r = 0; row < 0 && r < song.playlistTracks.size(); ++r)
                    if (song.playlistTracks[r].mode == PlaylistTrackMode::instrument && song.playlistTracks[r].channel == t.channel)
                        row = (int) r;
                for (const auto* c : gridPatternClips)
                {
                    if (row >= 0)
                        break;
                    for (const auto& x : song.patterns[(std::size_t) c->pattern].notes)
                        if (x.channel == t.channel)
                        {
                            row = c->track;
                            break;
                        }
                }
                for (int sc = 0; row >= 0 && sc < sceneCount; ++sc)
                    if (auto* slot = sessionSlot (n, sc); slot != nullptr && ! launcher::hasStopButton (*grid, row, sc))
                        if (const auto* v = slot->at ("ClipSlot/Value"); v != nullptr && v->kids.empty())
                            slot->value ("HasStop", "false");
            }
            if (t.ownsInsert)
                insertEffects (n, t.insert);
            nodes.push_back (std::move (n));
        }

        // main track: tempo, time signature, master fader and effects
        auto* main = set.child (mainName());
        if (main != nullptr)
        {
            main->value ("DeviceChain/Mixer/Tempo/Manual", num (song.bpm));
            // the signature at the song start: a signature marker there, else the project's n/4 (round 190cg)
            int startNum = song.beatsPerBar, startDen = 4;
            for (const auto& m : song.markers())
                if (m.isSignature() && m.tick <= 0)
                {
                    startNum = m.numerator;
                    startDen = m.denominator;
                }
            main->value ("DeviceChain/Mixer/TimeSignature/Manual", num (alsTimeSignatureCode (startNum, startDen)));
            if (song.tempoViewHigh > song.tempoViewLow) // the tempo envelope's shown range [DATA]
            {
                main->value ("DeviceChain/Mixer/TempoAutomationViewBottom", num (song.tempoViewLow));
                main->value ("DeviceChain/Mixer/TempoAutomationViewTop", num (song.tempoViewHigh));
            }
            if (! song.mixer.empty())
            {
                main->value ("DeviceChain/Mixer/Volume/Manual", num (std::clamp ((double) faderGain (song.mixer[0].volume), minGain, maxGain)));
                main->value ("DeviceChain/Mixer/Pan/Manual", num ((double) song.mixer[0].pan));
                if (auto* devices = main->at ("DeviceChain/DeviceChain/Devices"))
                    devices->kids.clear();
                insertEffects (*main, 0);
            }
            if (auto* envs = main->at ("AutomationEnvelopes/Envelopes"))
                envs->kids.clear();
        }
        if (auto* list = set.child ("Tracks"))
        {
            list->kids.clear();
            for (auto& n : nodes)
                list->add (std::move (n));
        }
        if (auto* pre = set.child ("SendsPre"))
        {
            pre->kids.clear();
            for (int r = 0; r < returns; ++r)
            {
                Node b;
                b.name = "SendPreBool";
                b.set ("Id", num (r));
                // LYNX's pre-fader sends (round 190aq): Live's switch is per return, so a return is "Pre" when every
                // send into it is pre-fader (a mix of both stays "Post", noted below)
                const auto target = returnInsert[(std::size_t) r];
                int preSends = 0, postSends = 0;
                for (std::size_t m = 1; m < song.mixer.size(); ++m)
                    for (const auto& s : song.mixer[m].sends)
                        if (s.target == target && returnOf.count (s.target) != 0 && s.target != busOf ((int) m))
                            (s.pre ? preSends : postSends)++;
                if (preSends > 0 && postSends > 0)
                    note ("Pre-fader sends: Live switches Pre / Post per return track, so the sends to " + song.mixer[(std::size_t) target].name
                          + " are all exported post-fader");
                b.set ("Value", preSends > 0 && postSends == 0 ? "true" : "false");
                pre->add (std::move (b));
            }
        }
        if (auto* scenes = set.child ("Scenes"))
        {
            scenes->kids.clear();
            for (int i = 0; i < sceneCount; ++i)
            {
                Node s = sk["Scene"];
                s.set ("Id", num (i));
                if (session && i < (int) grid->scenes.size())
                {
                    const auto& g = grid->scenes[(std::size_t) i];
                    s.value ("Name", g.name);
                    s.value ("Color", g.colour != 0 ? num (std::max (0, alsColourIndex (g.colour))) : std::string ("-1"));
                    if (g.tempo > 0.0)
                    {
                        s.value ("Tempo", num (g.tempo));
                        s.value ("IsTempoEnabled", "true");
                    }
                    if (g.beatsPerBar > 0)
                    {
                        s.value ("TimeSignatureId", num (alsTimeSignatureCode (g.beatsPerBar, g.beatUnit)));
                        s.value ("IsTimeSignatureEnabled", "true");
                    }
                }
                scenes->add (std::move (s));
            }
        }
        // the set-wide quantisation has no Global entry: Live's number is the clip number − 1 [LIVE: 9 = 1/8]
        if (session && grid->globalQuant != LaunchQuant::global)
            set.value ("GlobalQuantisation", num ((int) grid->globalQuant - 1));
        if (auto* locs = set.at ("Locators/Locators"))
        {
            locs->kids.clear();
            int id = 0;
            for (const auto& m : song.markers())
            {
                if (m.action != Marker::none || m.numerator > 0)
                    continue;
                Node l = sk["Locator"];
                l.set ("Id", num (id++));
                l.value ("Time", num (beats (m.tick)));
                l.value ("Name", m.name);
                locs->add (std::move (l));
            }
        }
        writeLiveScale (set, song, "InKey");
        if (auto* tr = set.child ("Transport"))
        {
            // (round 190cg: the range goes over with the Loop switch off too, as Live keeps its brace)
            const auto loop = song.loopEnd > song.loopStart;
            tr->value ("LoopOn", boolean (song.loopPlays()));
            if (loop)
            {
                tr->value ("LoopStart", num (beats (song.loopStart)));
                tr->value ("LoopLength", num (beats (song.loopEnd - song.loopStart)));
            }
        }

        // ids: every pointee unique, then the envelopes, then NextPointeeId above them all [§9 rules 2–4]
        int next = 1000;
        std::function<void (Node&)> renumber = [&] (Node& e) {
            const auto target = e.name == "Pointee" || (e.name.size() > 6 && e.name.compare (e.name.size() - 6, 6, "Target") == 0)
                                || e.name.rfind ("ControllerTargets.", 0) == 0;
            if (target && e.has ("Id"))
                e.set ("Id", num (next++));
            for (auto& k : e.kids)
                renumber (k);
        };
        renumber (set);

        // automation: mixer volume / pan of a track's insert, the Master, tempo
        int envelopeId = 0;
        std::map<std::string, std::pair<Node*, std::vector<std::pair<double, double>>>> envelopes; // pointee → owner, events
        auto* trackList = set.child ("Tracks");
        auto ownerOf = [&] (int insert) -> Node* {
            if (insert == 0)
                return main;
            for (std::size_t i = 0; i < ordered.size(); ++i)
                if (ordered[i].insert == insert && ordered[i].ownsInsert)
                    return &trackList->kids[i];
            return nullptr;
        };
        for (const auto* c : automationClips)
        {
            const auto& a = song.automations[(std::size_t) c->automation];
            Node* owner = nullptr;
            std::string path;
            std::function<double (float)> map;
            switch (a.target.kind)
            {
                case AutomationTargetKind::tempo:
                    owner = main;
                    path = "DeviceChain/Mixer/Tempo/AutomationTarget";
                    map = [] (float v) { return tempoFromValue (v); };
                    break;
                case AutomationTargetKind::mixerVolume:
                    owner = ownerOf (a.target.index);
                    path = "DeviceChain/Mixer/Volume/AutomationTarget";
                    map = [] (float v) { return std::clamp ((double) faderGain (v), minGain, maxGain); };
                    break;
                case AutomationTargetKind::mixerPan:
                    owner = ownerOf (a.target.index);
                    path = "DeviceChain/Mixer/Pan/AutomationTarget";
                    map = [] (float v) { return (double) v * 2.0 - 1.0; };
                    break;
                default: break;
            }
            if (owner == nullptr)
            {
                note ("Automation not exported: " + a.name);
                continue;
            }
            const auto* t = owner->at (path);
            if (t == nullptr)
                continue;
            auto& slot = envelopes[t->get ("Id")];
            slot.first = owner;
            for (const auto& p : a.points)
                slot.second.push_back ({ beats (c->start + p.tick), map (p.value) });
        }
        for (auto& [pointee, entry] : envelopes)
        {
            std::stable_sort (entry.second.begin(), entry.second.end(), [] (const auto& x, const auto& y) { return x.first < y.first; });
            addEnvelope (*entry.first, pointee, entry.second, envelopeId);
        }
        // time signature changes along the Arrangement (round 190cg): the main track's envelope of signature codes, the
        // first event "before the song" [DATA: the stored sets' own envelope], one event per signature marker after it
        if (main != nullptr)
            if (const auto* target = main->at ("DeviceChain/Mixer/TimeSignature/AutomationTarget"); target != nullptr && target->has ("Id"))
            {
                int num = song.beatsPerBar, den = 4;
                std::vector<std::pair<double, double>> changes;
                for (const auto& m : song.markers())
                {
                    if (! m.isSignature())
                        continue;
                    if (m.tick <= 0)
                    {
                        num = m.numerator;
                        den = m.denominator;
                    }
                    else
                        changes.push_back ({ beats (m.tick), (double) alsTimeSignatureCode (m.numerator, m.denominator) });
                }
                if (! changes.empty())
                {
                    changes.insert (changes.begin(), { beforeSong, (double) alsTimeSignatureCode (num, den) });
                    addEnvelope (*main, target->get ("Id"), changes, envelopeId, "EnumEvent");
                }
            }

        int maxId = next;
        std::function<void (const Node&)> scan = [&] (const Node& e) {
            if (e.has ("Id"))
            {
                const auto v = e.get ("Id");
                int x = 0;
                if (std::from_chars (v.data(), v.data() + v.size(), x).ec == std::errc())
                    maxId = std::max (maxId, x);
            }
            for (const auto& k : e.kids)
                scan (k);
        };
        scan (set);
        set.value ("NextPointeeId", num (maxId + 10));

        root.kids.clear();
        root.add (std::move (set));
        result.xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        write (root, result.xml, 0);
        const auto* data = reinterpret_cast<const std::uint8_t*> (result.xml.data());
        result.bytes = gzip::compressStored (data, result.xml.size());
    }
};
} // namespace

AlsExportResult exportAls (const Song& song, const AlsExportOptions& options)
{
    AlsExportResult result;
    const auto& sk = skeletons (options.version);
    if (sk.kinds.empty())
    {
        result.notes.push_back ("internal error: the set skeletons could not be read");
        return result;
    }
    Exporter e { song, options, sk, result, false, {} };
    e.run();
    return result;
}
bool alsSampleNeedsWav (const std::string& extension, const std::uint8_t* head, std::size_t size)
{
    std::string ext;
    for (const auto c : extension)
        if (c != '.')
            ext += (char) std::tolower ((unsigned char) c);
    const auto starts = [&] (const char* magic, std::size_t at) {
        const auto n = std::char_traits<char>::length (magic);
        return size >= at + n && std::equal (magic, magic + n, head + at);
    };
    const auto ogg = starts ("OggS", 0);
    const auto riffWave = (starts ("RIFF", 0) || starts ("RF64", 0)) && starts ("WAVE", 8);
    if (ext == "wav" || ext == "wave")
        return ! riffWave || oggvorbis::isOggWav (head, size); // raw Ogg, Ogg-in-WAV or anything else named .wav
    if (ext == "ogg")
        return ! ogg && riffWave; // a WAV file named .ogg
    if (ext == "aif" || ext == "aiff" || ext == "flac" || ext == "mp3")
        return ogg || riffWave; // the content is not what the name says
    return true; // WavPack (.wv) and every other type LYNX decodes
}
} // namespace lynx
