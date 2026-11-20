#pragma once

// LYNX substitutes for missing third-party plugins (docs/PLUGIN_FALLBACK.md).
//
// When a project (.flp or .lynx) uses a third-party plugin that isn't installed here, LYNX can put one of its own
// built-in effects "in the role of" that plugin: a reverb for a reverb, a delay for a delay... The slot keeps the
// ORIGINAL plugin reference byte for byte (EffectSlot::substituteFor), so the project saves it back unchanged and
// reopens with the real plugin on a machine that has it.
//
// What stands in for what is DATA: a text table (built in, extendable with registerPluginSubstitutes) of entries
// that name the third-party product (VST2 unique IDs, VST3 class IDs, CLAP ids, plugin / file names, Waves shell
// sub-plugin names), the LYNX effect, a default preset and, where the saved state is readable, a parameter map.
// Parameter maps use layouts that are visible in the saved data itself (VST2 float parameters, the attributes of a
// JUCE XML state, Waves' "RealWorld" parameter lists) and meanings known from public documentation or our own
// black-box measurements. Since 2026-11, by the project owner's decision, some stand-ins and translators (e.g. the
// FabFilter roles of round 190kw onwards) were also informed by clean-room static analysis of the third-party
// binaries: their laws and structure were described in our own words and LYNX's code was written from that
// description. No third-party code, tables, presets or other content is copied, and licence protections are never
// bypassed. The evidence for each case is in docs/analise/REA_*.md. Without a readable state the default preset is used.
//
// Does not depend on JUCE so it can be tested in isolation.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// One parameter of the substitute taken from the original plugin's saved state.
struct SubstituteParamMap
{
    enum class Source : std::uint8_t
    {
        vst2,  // FL's VST2 state (parseFlVst2State): float parameter `index` of the current program, 0..1
        xml,   // attribute `attribute` of the first element of a JUCE XML state ("VC2!" block or plain "<?xml")
        waves  // value `index` of the active setup of a Waves "PresetChunkXMLTree" (real-world units)
    };
    Source source = Source::vst2;
    int index = 0;
    std::string attribute;
    // target: a parameter of the LYNX effect (by position), or the slot's dry/wet (`toSlotMix`)
    int target = 0;
    bool toSlotMix = false;
    // linear map, clamped: in [inLo, inHi] -> out [outLo, outHi] (0..1 normalised parameter)
    float inLo = 0.0f, inHi = 1.0f, outLo = 0.0f, outHi = 1.0f;
    // or a point list ("| curve x:y x:y …": piecewise linear, clamped at the ends; "| steps x:y …": the y of the last
    // point whose x ≤ the value — enumerations), x ascending
    enum class Shape : std::uint8_t { linear, curve, steps };
    Shape shape = Shape::linear;
    std::vector<std::pair<float, float>> points;
};

struct SubstituteEntry
{
    std::string key;     // stable id of the entry ("valhalla-vintageverb")
    std::string product; // the third-party product, for display ("ValhallaVintageVerb")
    std::string vendor;
    bool instrument = false; // reserved: the loader substitutes effects only (instrument entries are not applied yet)

    // what identifies the product
    std::vector<std::uint32_t> vst2Ids;
    std::vector<std::array<std::uint8_t, 16>> vst3ClassIds; // TUID as laid out in memory (FL's record 0x34)
    std::vector<std::string> clapIds;
    std::vector<std::string> names;      // pluginNameKey of plugin / file names ("valhallavintageverb")
    std::vector<std::string> wavesNames; // pluginNameKey of a WaveShell sub-plugin's <PluginName> ("rcomp")

    // the LYNX effect and its default preset
    EffectKind effect = EffectKind::eq;
    std::vector<std::pair<int, float>> preset; // parameter position -> normalised value, over makeEffectSlot (effect)
    // round 190ly ("replaced = …", same items as preset): applied over the preset only when the slot replaces an original
    // device (project / import translation), not for one added by hand from a substitute menu
    std::vector<std::pair<int, float>> replacedPreset;
    float mix = 1.0f;                          // slot dry/wet of the substitute
    std::vector<SubstituteParamMap> params;
    // a built-in state translator (core/FabFilterStates.h: "pro-c3", "pro-mb", "pro-g", "saturn2") run after the
    // maps; it reads the vendor's state layout we observed black-box and only acts when that layout matches
    std::string translator;
    // VST2 parameters per program (AEffect::numParams), needed to find the current program in a state saved with
    // "all programs" (FL doesn't store the count); 0 = unknown (then only program 0 can be read from such a state)
    int vst2Params = 0;
};

// What a substitute is matched on and translated from: the original plugin's identity and its own saved state.
struct PluginIdentity
{
    ForeignPlugin::Format format = ForeignPlugin::Format::none;
    std::string name, path;
    std::uint32_t vst2Id = 0;
    std::array<std::uint8_t, 16> classId {};
    bool hasClassId = false;
    std::string clapId;
    // VST2: FL's state layout (the 0x35 record / LYNX's VST2 host state); VST3: the component state; CLAP: the raw state
    std::vector<std::uint8_t> state;
};

// The identity of an .flp plugin (FL's wrapper record); for VST3 the state is the component part of FL's state.
PluginIdentity pluginIdentity (const ForeignPlugin& plugin);

// Parses entries in the table format (docs/PLUGIN_FALLBACK.md). Lines it can't use are skipped and counted in
// `errors` (when given). Entries whose LYNX effect name is unknown are dropped (counted as errors).
std::vector<SubstituteEntry> parseSubstituteTable (std::string_view text, int* errors = nullptr);

// The table in use: the built-in entries plus the registered ones (later registrations win on the same key).
const std::vector<SubstituteEntry>& pluginSubstitutes();
// Adds entries (table text) — the hook other LYNX modules use to offer themselves as substitutes. Returns how many
// entries were added. Call it from the UI/message thread before projects open.
int registerPluginSubstitutes (std::string_view tableText);

// The entry standing in for `plugin` (effect or instrument), or null. Matches by VST2 ID, VST3 class ID, CLAP id,
// Waves sub-plugin name (read from the state), then by plugin / file name (a WaveShell file name never matches
// by name: the shell holds many plugins).
const SubstituteEntry* findPluginSubstitute (const PluginIdentity& plugin, bool instrument);

// The built-in effect slot standing in for the plugin: the entry's preset, then the parameters the saved state lets
// us translate. `translated` (when given) gets how many parameters came from the state.
EffectSlot makeSubstituteSlot (const SubstituteEntry& entry, const PluginIdentity* original = nullptr, int* translated = nullptr);

// Replaces a plugin effect slot (kind == plugin) by its substitute, keeping the original reference in
// `substituteFor` and the slot's on/off, dry/wet (multiplied by the substitute's) and side-chain routing.
// False (slot unchanged) when no entry matches. `identity` is the original's identity (pluginIdentity for .flp).
bool substituteEffectSlot (EffectSlot& slot, const PluginIdentity& identity);

// What the whole project says about the settings a substitute can't read from its own original (round 113): the key
// and scale of the song, taken from the pitch correctors whose saved state is readable (Auto-Tune Pro). A pitch
// corrector whose own key is unknown (Waves Tune, an unreadable state) then corrects to the project's key instead of
// guessing one. Round 120: Waves Tune Real-Time states name their scale too (wavesTuneScale).
struct SubstituteContext
{
    int key = -1;   // 0 = C … 11 = B; −1 = unknown
    int scale = -1; // LYNX Tune's scale index (vocal::Scale); −1 = unknown
    bool knowsKey() const noexcept { return key >= 0 && scale >= 0; }
};
// The context of a song: every plugin slot (hosted, unresolved or already substituted) whose original state names a
// key; the most common key / scale wins (ties: the first slot's).
SubstituteContext substituteContextOf (const Song& song);
// As above, using `context` for what the original's own state doesn't tell.
bool substituteEffectSlot (EffectSlot& slot, const PluginIdentity& identity, const SubstituteContext& context);

// The key and scale an Auto-Tune Pro component state was saved with (its <Custom> element's ScaleGivenKey /
// ScaleGivenScale), as LYNX Tune's key index and scale (vocal::Scale index); nullopt when not readable or a scale
// LYNX Tune has no equivalent for. Exposed for the tests.
std::optional<std::pair<int, int>> autoTuneProKey (const std::vector<std::uint8_t>& state);
// The scale a Waves Tune Real-Time state was saved with (round 120): the active setup's <Scale> element (its root and
// the twelve note statuses above it, "x" = excluded). `scale` is the LYNX Tune scale whose notes on `key` are exactly
// these, else vocal::Scale::custom with `mask` (bit k = pitch class k, absolute) holding them. nullopt when absent.
// The parameters an Auto-Tune Pro state holds (its ParamsData: X<n> = parameter n in real units; [EXEC] round 120:
// 1 scale, 2 key, 4 retune speed ms, 61 humanize, 90 flex-tune, 62 natural vibrato dB). Empty when not readable.
std::map<int, double> autoTuneProParams (const std::vector<std::uint8_t>& state);
// Round 128: the active scale's per-note edits of an Auto-Tune Pro state (legacy <Scales> block): {bypass, remove},
// bit k = absolute pitch class k. `lynxScale` = vocal::Scale major / minor / chromatic (others: nullopt).
std::optional<std::pair<std::uint16_t, std::uint16_t>> autoTuneProNoteEdits (const std::vector<std::uint8_t>& state, int key, int lynxScale);
struct PitchCorrectorScale
{
    int key = 0;
    int scale = 0;
    std::uint16_t mask = 0xfff;
};
std::optional<PitchCorrectorScale> wavesTuneScale (const std::vector<std::uint8_t>& state);
// The key / scale of any readable pitch-corrector state (Auto-Tune Pro, Waves Tune Real-Time), as above.
std::optional<PitchCorrectorScale> pitchCorrectorScale (const std::vector<std::uint8_t>& state);
// Fresh Air's two "air" amounts (the two floats of its saved state, 0..1: mid and high air); nullopt when the state
// doesn't have that layout. Exposed for the tests.
std::optional<std::pair<float, float>> freshAirAmounts (const std::vector<std::uint8_t>& state);

// A substitute slot back to its original plugin (kind = plugin, plugin = substituteFor). False if not a substitute.
bool restoreOriginalPlugin (EffectSlot& slot);

// Display name of the original plugin of a substitute slot ("" when not a substitute).
std::string substituteOriginalName (const EffectSlot& slot);

// --- state readers (exposed for tests) --------------------------------------------------------------------------
// The attributes of the first element of the JUCE XML state inside `state` (after "VC2!" or the first "<?xml").
std::vector<std::pair<std::string, std::string>> juceXmlStateAttributes (const std::vector<std::uint8_t>& state);
// The <PluginName> of a Waves preset chunk ("" when not a Waves state).
std::string wavesPluginName (const std::vector<std::uint8_t>& state);
// The name of the preset the original plugin was saved with (round 135): a Valhalla state's `presetName` attribute,
// a Waves chunk's <Preset Name="...">; "" when none is stored.
std::string originalPresetName (const std::vector<std::uint8_t>& state);
// The "RealWorld" parameter values of the active setup of a Waves preset chunk ("*" = unused -> NaN).
std::vector<double> wavesRealWorldParams (const std::vector<std::uint8_t>& state);
// Round 135: the FNV-1a hash of a Soundtoys state's PRESET payload (nullopt when not one), and how many of her
// Soundtoys states (read black-box in the official trials) are translated by it.
std::optional<std::uint64_t> soundtoysStateHash (const std::vector<std::uint8_t>& state);
int knownSoundtoysStates() noexcept;
// Round 190kv: the Soundtoys factory presets. The fingerprint of a state's encoded payload without its volatile fields
// (0 when the state has none); the fingerprints known for a plug-in's widget name ("EchoBoy", "Decapitator", …); a known
// fingerprint applied to a slot of the matching LYNX effect (the number of parameters set, 0 when not known).
std::uint64_t soundtoysFactoryFingerprint (const std::vector<std::uint8_t>& state);
std::vector<std::uint64_t> soundtoysFactoryFingerprints (std::string_view widget);
int applySoundtoysFactoryPreset (std::string_view widget, std::uint64_t fingerprint, EffectSlot& slot);

// --- lossless text form of an .flp plugin reference (.lynx keeps it for substitutes) ------------------------------
// Base64 of a versioned binary record of every ForeignPlugin field; foreignFromText(foreignToText(p)) == p, the
// state byte for byte.
std::string foreignToText (const ForeignPlugin& plugin);
bool foreignFromText (const std::string& text, ForeignPlugin& out);

std::string base64Encode (const std::uint8_t* data, std::size_t size);
bool base64Decode (const std::string& text, std::vector<std::uint8_t>& out);
// Round 136: the FNV-1a 64 hash of a Soundtoys state's encoded preset (the bytes after "PRESET =", without the final ';').
// LYNX does not decode those presets; her saved states are recognised by this hash (core/SoundtoysStates.inc).
std::uint64_t soundtoysPresetHash (const std::vector<std::uint8_t>& state);

} // namespace lynx
