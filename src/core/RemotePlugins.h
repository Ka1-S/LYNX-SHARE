#pragma once

// Online Sessions P5-A1 (docs/design/ONLINE_SESSIONS.md "P5-A1 as built", round 190ai): remote plugins — a guest uses
// a plugin that only the session's host has.
//
// The host shares a plugin with the session (opt-in per plugin, only while the session runs). A guest then puts it on
// a channel / an insert slot of their own: a PluginRef with the HOST's plugin id, `playedBy` = the host and no state.
// The guest edits notes, automation and parameter values (PluginRef::values); the host's computer plays the part with
// its plugin and renders it as stand-ins (the live stand-in machinery of round 169j), which travel to the guest as
// media. What crosses the network from a guest is music data and values in 0..1 — never a state blob, a preset or a
// file path — and nothing of the plugin itself ever leaves the host.
//
// This file has the pure parts (no JUCE): what the host tells guests about a shared plugin, the values on a
// PluginRef, what the host accepts from a guest, who renders what, and the limit on how fast values may change.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/LiveSession.h"
#include "core/Song.h"

namespace lynx::remoteplug
{
constexpr std::size_t maxShared = 32;   // plugins a host shares at once
constexpr std::size_t maxParams = 1024; // parameters listed per plugin (and values kept on a PluginRef)
constexpr int maxSteps = 32;            // a stepped parameter with up to this many steps lists their names
constexpr std::size_t maxText = 64;     // bytes of a name / unit / value text

// One parameter of a shared plugin as the guest's editor shows it. The value is always 0..1 (the plugin's own
// normalised range); `low` / `high` are the plugin's texts for 0 and 1 ("20 Hz", "20 kHz").
struct Param
{
    int index = 0;
    std::string name, unit, low, high;
    float def = 0.0f;                   // the plugin's default
    int steps = 0;                      // 0 = continuous, else the number of positions
    std::vector<std::string> stepNames; // (steps <= maxSteps) the text of each position
    bool operator== (const Param&) const = default;
};

// A plugin the host shares: its id on the HOST's computer (what a guest's PluginRef carries), how to show it, and
// its parameters.
struct Shared
{
    std::string id, name, vendor, format;
    bool instrument = false;
    std::vector<Param> params;
    bool operator== (const Shared&) const = default;
};

// What arrived from the network, cut to the limits above (text sizes, counts, values in 0..1, indices unique).
void clean (Shared& shared);
void clean (std::vector<Shared>& list);
const Shared* find (const std::vector<Shared>& list, const std::string& id) noexcept;

// --- the values on a PluginRef ---------------------------------------------------------------------------------------

float valueOf (const PluginRef& ref, int index, float fallback) noexcept;
// Sets one value (kept sorted by index); false when nothing changed or the index is out of range.
bool setValue (PluginRef& ref, int index, float value);
// Sorted, one per index, 0..1, at most maxParams, no NaN.
void cleanValues (PluginRef& ref);

// The reference a guest puts on a channel / slot for a shared plugin.
PluginRef makeRef (const Shared& shared, std::uint32_t playedBy);

// --- the hosted plugins of a song ------------------------------------------------------------------------------------

// Where a plugin reference lives: a channel (part = its key) or an effect slot of an insert (part = the insert).
struct Place
{
    live::Key part;
    int slot = -1; // effect slot; -1 = the channel's instrument
    auto operator<=> (const Place&) const = default;
};
// Every third-party plugin reference of the song's channels and insert slots (not the ones inside a Patcher).
void forEachRef (Song& song, const std::function<void (PluginRef&, const Place&)>& fn);
void forEachRef (const Song& song, const std::function<void (const PluginRef&, const Place&)>& fn);
PluginRef* refAt (Song& song, const Place& place);
const PluginRef* refAt (const Song& song, const Place& place);

// Who uses a plugin played by `by`: the part and its owner (for the host's "used by" list).
struct Use
{
    Place place;
    std::uint32_t owner = 0;
    std::string id;
};
std::vector<Use> uses (const Song& song, std::uint32_t by);

// --- the host's rules ------------------------------------------------------------------------------------------------

// What a guest's proposal may say about hosted plugins, applied to its carrier before the authority judges it: a
// reference played by someone can only be played by the host; it carries no state (the state of a shared plugin never
// crosses the network, in either direction) and clean values.
void sanitize (Song& carrier, std::uint32_t hostTag);

// How many hosted parameter values the patch changes against `confirmed` (what the limiter counts).
int valueEdits (const Song& confirmed, const live::Patch& patch);

// Parameter edits are rate-limited per guest: a bucket of `burst` value changes refilled at `perSecond`.
class Limiter
{
public:
    explicit Limiter (double perSecond = 40.0, double burst = 120.0) : rate (perSecond), capacity (burst) {}
    // Whether `edits` value changes by `tag` at `nowMs` are within the limit (they are counted when they are).
    bool allow (std::uint32_t tag, double nowMs, int edits);

private:
    struct Bucket
    {
        double tokens = 0.0, atMs = 0.0;
        bool used = false;
    };
    double rate, capacity;
    std::map<std::uint32_t, Bucket> buckets;
};

// The guest's parameter editor writes through this: slider moves are collected and written to the song a few times a
// second (never one edit per mouse move), and a value the session then undid — the host's render landed on the same
// channel at that moment, so the edit was refused as a conflict — is written again while it is fresh.
class Outbox
{
public:
    explicit Outbox (double everyMs = 200.0, double retryMs = 700.0, int tries = 5) : interval (everyMs), retry (retryMs), maxTries (tries) {}
    void set (int index, float value, double nowMs);
    // What the editor shows for a parameter while its value is on the way (nullopt: what the song has).
    std::optional<float> showing (int index) const;
    // The values to write now, given what the song has (empty: nothing, or too soon after the last write). Values that
    // arrived, and values tried `tries` times, are forgotten.
    std::vector<std::pair<int, float>> due (const PluginRef& ref, double nowMs);
    bool empty() const noexcept { return wanted.empty(); }

private:
    struct Entry
    {
        float value = 0.0f;
        double writtenMs = -1.0; // (-1: not written yet)
        int tries = 0;
    };
    double interval, retry, lastWriteMs = -1.0e12;
    int maxTries;
    std::map<int, Entry> wanted;
};

// Of the parts someone needs (`needed`), the ones participant `me` renders FOR THEIR OWNERS: parts of other people
// in which every plugin missing elsewhere is played by `me`, shared right now (`shared` by plugin id) and installed
// here (`missing`). live::toRender covers a participant's own parts.
std::vector<live::Key> toRender (const Song& song, const std::set<live::Key>& needed, std::uint32_t me,
                                 const std::function<bool (const PluginRef&)>& missing, const std::function<bool (const std::string& id)>& shared);

// The session is over: nobody plays anything for anyone any more. Clears `playedBy` (and, with `values`, the values:
// the host captures the plugin's real state first; a guest keeps the audio stand-ins only).
void endSession (Song& song, bool values = true);
} // namespace lynx::remoteplug
