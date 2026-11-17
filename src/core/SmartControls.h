#pragma once

// The Simple dock's Controls tab (round 190ke, owner decision D4 of coverage190jw: GarageBand's Smart Controls in the
// Simple layout). GarageBand (Apple's GarageBand User Guide for Mac, "Intro to Smart Controls"): one pane under the
// tracks with a set of big labelled screen controls for the selected track's patch, each driving one or more of its
// key parameters (an instrument's cutoff, resonance and envelope, the amp's gain, the effects' amounts). LYNX: eight big
// knobs per track. Each knob drives one control the track already has — a parameter of its instrument (LYNX Synth,
// Sampler, Soundfont and the FL engines' knobs, through the same AutomationTarget the automation clips use), a one-knob
// effect of its chain (its knob), another effect's Mix or parameter, or the track's Volume, Pan and Reverb send — so
// every other layout shows the same values in the same places. LYNX picks eight per instrument and chain (`defaults`);
// the user can point any knob at another control (`candidates`), kept on the track's insert (MixerTrack::smartControls,
// saved in the project). No JUCE (tests/SimpleGb190keTests.inc).

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::smartcontrols
{
constexpr int knobCount = 8;

enum class Source : std::uint8_t
{
    none,      // an empty knob (nothing left to map)
    target,    // an AutomationTarget of the track's channel or insert (index: resolved to them)
    goal,      // a one-knob effect of the insert's chain: its knob (target.slot = the slot)
    reverbSend // the track's send to the Simple layout's shared reverb (core/SimpleTracks.h)
};

enum class Group : std::uint8_t { sound, effects, track };

struct Control
{
    Source source = Source::none;
    AutomationTarget target; // index: the channel (channel kinds) or the insert (mixer / effect kinds) once resolved
    bool operator== (const Control&) const = default;
};

using Mapping = std::array<Control, knobCount>;

// The track's channel (−1: none, e.g. an audio track) and insert (> 0)
struct Owner
{
    int channel = -1, insert = -1;
};

// What the knobs show: a name (English, the UI translates it; preset names and plugin parameters stay as they are), the
// group above it (GarageBand's section titles) and the effect it belongs to ("" for the instrument and the track)
struct Label
{
    std::string name, effect;
    Group group = Group::track;
};

bool valid (const Song& song, const Owner& owner, const Control& c);
Label labelOf (const Song& song, const Owner& owner, const Control& c);
// 0…1 of the knob, and the move (false: the control is gone)
float valueOf (const Song& song, const Owner& owner, const Control& c);
bool setValue (Song& song, const Owner& owner, const Control& c, float value);
// The value a double-click puts back (the control's default; 0.5 when it has none of its own)
float defaultValueOf (const Song& song, const Owner& owner, const Control& c);

// The instrument's key parameters, in GarageBand's order of importance (filter, envelope, character); at most `most`
std::vector<Control> instrumentControls (const Song& song, const Owner& owner, int most = 4);
// Every control the track offers (the remap menu): the instrument's parameters, then each effect's, then the track's
std::vector<Control> candidates (const Song& song, const Owner& owner);
// LYNX's default mapping: up to four of the instrument's key parameters, the chain's one-knob effects (else its effects'
// Mix), then Reverb, Pan and Volume; the rest of the chain fills what is left, then empty knobs.
Mapping defaults (const Song& song, const Owner& owner);
// The mapping in use: the user's (MixerTrack::smartControls), knob by knob, where it is still valid; the default's
// knob elsewhere
Mapping mappingOf (const Song& song, const Owner& owner);

// The text kept on the insert: one entry per knob, ';'-separated: "d" (the default's knob), "n" (empty),
// "t:<kind>:<slot>:<param>", "g:<slot>", "r" (the channel / insert are not written: a knob belongs to its track)
std::string encode (const std::array<std::optional<Control>, knobCount>& user);
std::array<std::optional<Control>, knobCount> decode (const std::string& text);
// Points knob `knob` of the track at `control` (nullopt: back to the default's); the insert's text is rewritten ("" when
// every knob is the default's). False for a bad knob or insert.
bool remap (Song& song, const Owner& owner, int knob, std::optional<Control> control);
// Every knob back to LYNX's default mapping
bool reset (Song& song, const Owner& owner);
} // namespace lynx::smartcontrols
