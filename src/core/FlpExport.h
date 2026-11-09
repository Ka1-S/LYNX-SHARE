#pragma once

// Export to FL Studio (round 171e, idea by Tookolld): a LYNX song as an FL Studio 2026 project (.flp), the inverse of
// FlpImport. What FL has itself goes over as FL's own data: samplers and audio clips with their settings, FL's native
// generators and effects with their saved state, third-party plugins (VST2 / VST3 / CLAP) inside Fruity Wrapper, the
// patterns and notes, the Playlist (pattern, audio and automation clips, tracks, markers, arrangements), the mixer
// (names, colours, routes, sends, levels, the track EQ) and the automation links. What only LYNX has is either
// rendered to audio first (option A, the default: an instrument becomes an audio clip, an insert holding a LYNX-only
// effect is bounced) or replaced by the nearest FL plugin where a clean mapping exists (option B); the report says
// which. Formats: docs/analise/REA_rodada171e.md. No JUCE (the renders and the plugin lookups are the caller's).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"
#include "flp/Flp.h"
#include "flp/FlpWrite.h"

namespace lynx::flpexport
{
enum class LynxOnly
{
    renderAudio, // option A: LYNX-only instruments and inserts with LYNX-only effects become audio first
    substitute   // option B: the nearest FL plugin where a clean mapping exists, the rest is left out
};

struct Options
{
    LynxOnly lynxOnly = LynxOnly::renderAudio;
    // the path written for a sample, a plugin's data file... (the zipped package: a path relative to the project);
    // empty = as it is
    std::function<std::string (const std::string&)> samplePath;
    // a hosted plugin (kind plugin) as FL's wrapper keeps it: format, identifiers, name, path and the state in FL's
    // layout (record 0x35). nullopt = not known: the channel / slot is reported as left out. A plugin still waiting for
    // its FL original (PluginRef::foreign) or a substitute's original (EffectSlot::substituteFor) needs no lookup.
    std::function<std::optional<ForeignPlugin> (const PluginRef&, bool instrument)> pluginRecord;
    // a sample's length in seconds (0 = unknown): the start / end of an audio clip of a time-stretched channel are in the
    // sample's milliseconds
    std::function<double (const std::string&)> sampleSeconds;
    flp::WriteOptions write;
    int ppq = 0; // the file's PPQ (0 = 96 when the notes and clips fit it, else 960)
};

// What the export did, one line per thing, for the report window.
struct Report
{
    std::vector<std::string> exact;       // as FL's own data (settings, states)
    std::vector<std::string> rendered;    // rendered to audio first (option A)
    std::vector<std::string> substituted; // an FL plugin in place of a LYNX-only one (option B)
    std::vector<std::string> dropped;     // left out (no FL equivalent, nothing rendered)
    std::vector<std::string> notes;       // approximations worth knowing (e.g. a setting FL does not have)
};

// The channel volume at which a Playlist audio clip plays its file at unity, in FL and in LYNX: FL's default, 10000 of
// 12800. A rendered channel or insert is written with it (round 182b).
inline constexpr float audioClipUnityVolume = 10000.0f / 12800.0f;

// --- option A: what has to be rendered before the conversion ---
struct RenderPlan
{
    std::vector<int> channels; // LYNX-only instruments: frozen (their output before the mixer) to an audio clip
    std::vector<int> inserts;  // inserts holding LYNX-only effects: bounced (their output after the fader), downstream first
    bool empty() const noexcept { return channels.empty() && inserts.empty(); }
};
RenderPlan planRenders (const Song& song, const Options& options);

// The song copy that renders a frozen channel: only `channel` plays (the patterns and Playlist as they are, in Song mode),
// straight to a clean Master (no effects, unity, centre) — its own volume, pan and their automation are in the render.
Song makeFreezeSong (const Song& song, int channel);
// The song copy that renders insert `track` for the export: lynx::makeBounceSong, but the inserts feeding it reach the
// Master only through it (their own Master route is cut), so the render is that insert's output alone and the exported
// file doesn't play its sources twice (they keep their own inserts).
Song makeInsertRenderSong (const Song& song, int track);
// After the render (`wav`, `length` ticks from the song's start): the channel becomes an audio clip channel at unity
// that plays the render from tick 0 on a new Playlist track; its notes leave the patterns and the automation of its
// volume / pan / parameters stops (it is in the audio).
void applyFreeze (Song& song, int channel, const std::string& wav, std::int64_t length);
// Inserts: lynx::makeBounceSong renders one; after the render applyInsertRender = lynx::applyBounce (a new insert with
// an audio clip, the original muted), then the original's slots FL can't have are switched off (their sound is in the
// render) with their automation, and the report says so. Returns the new insert (−1 = none: the mixer is full).
int applyInsertRender (Song& song, int track, const std::string& wav, std::int64_t length, const Options& options, Report& report);
// applyFreeze + the report line
void applyChannelRender (Song& song, int channel, const std::string& wav, std::int64_t length, Report& report);

// --- what goes over exactly ---
// The fingerprint of what the import made of an FL plugin (FlOrigin::fingerprint): equal = unchanged since the import.
std::uint64_t fingerprint (const ChannelDef& channel);
std::uint64_t fingerprint (const EffectSlot& slot);
// Sets every FlOrigin's fingerprint (the end of FlpImport).
void stampOrigins (Song& song);

// The FL generator a channel is exported as ("" = a sampler / audio clip channel, or none: see the report)
std::string flGeneratorName (const ChannelDef& channel);
// true = the channel needs a render to reach FL (option A) — it has no FL equivalent of its own
bool needsFreeze (const ChannelDef& channel);
// true = the slot can be written as an FL effect (exactly or, with option B, as a substitute)
bool effectExportable (const EffectSlot& slot, const Options& options);

// Fruity Wrapper's state (0xD5, version 12, the records in FL 2026's order) for a hosted plugin, with the wrapper
// options from `ref` when given (else FL's defaults).
std::vector<std::uint8_t> writeWrapper (const ForeignPlugin& plugin, bool instrument, const PluginRef* ref = nullptr);
// VST3 state in FL's layout (record 0x35) from the component and controller states.
std::vector<std::uint8_t> flVst3State (const std::vector<std::uint8_t>& component, const std::vector<std::uint8_t>& controller);

// The conversion. The song is used as it is (option A: render and apply first).
flp::Project toProject (const Song& song, const Options& options, Report& report);
// toProject + flp::writeProject
std::vector<std::uint8_t> exportFlp (const Song& song, const Options& options, Report& report);

// FL 2026's default Fruity Limiter state (option B's substitute for LYNX's Limiter)
std::vector<std::uint8_t> fruityLimiterDefaultState();
// LYNX's 3x Osc with no state, as FL's own version-14 state
std::vector<std::uint8_t> defaultThreeOscState();

// FL value helpers (exposed for tests)
int envelopeTimeValue (double seconds) noexcept; // inverse of flp::Channel::envelopeTime (v ≥ 101)
int slideTimeValue (float beats) noexcept;       // inverse of the porta time n = round((27^(v/1024) − 1)·48/26)/192
} // namespace lynx::flpexport
