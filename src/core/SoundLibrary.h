#pragma once

// The Simple workflow's sound picker (round 190bu, Simple layout S6, decision I of WHAT-LYNX-LACKS.md): one list of
// ready sounds by category, made only of what LYNX has itself — the built-in synth's presets (core/SynthPresets.h) and
// LYNX's own preset banks of its engines (core/LynxBanks.h) — plus the presets of the user's own SoundFont files
// (core/SoundFontIndex.h) and, since round 190dg, the instrument plugins installed on the computer (the plugin host's
// scanned list; one entry per plugin, its own presets stay in its own window). No sampled content ships with LYNX. A sound is a recipe for a channel: picking one puts its
// instrument and settings on the track's channel and leaves the channel's own settings (name, level, pan, insert,
// colour, arpeggiator…) alone, so the Channel rack and every other layout show an ordinary channel.
// No JUCE.

#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"
#include "core/SoundFontIndex.h"

namespace lynx::soundlib
{
enum class Category : int { keys, bass, lead, pad, pluck, synths, bells, organ, strings, brass, voices, guitar, drums, effects, count };
const char* categoryName (Category c) noexcept; // English (the UI translates it)

enum class Source : int { synth, bank, soundFont, plugin };

struct Sound
{
    std::string name;                   // "Deep Sub"; preset names are shown as they are
    Category category = Category::keys;
    Source source = Source::synth;
    InstrumentKind kind = InstrumentKind::synth;
    int index = 0;                      // synth: the preset in synthPresets(); bank: the preset in banks::bankOf (kind)
    std::string fontPath;               // soundFont: the file and its preset
    int bank = 0, program = 0;
    std::string pluginId;               // plugin: the plugin host's identifier (JUCE's createIdentifierString)
    std::string origin;                 // what it is made with, for the list's second column ("LYNX synth", the font's name)
    bool operator== (const Sound& o) const
    {
        return source == o.source && kind == o.kind && index == o.index && fontPath == o.fontPath && bank == o.bank && program == o.program
               && pluginId == o.pluginId;
    }
};

// The category of a preset from its bank category / General MIDI family ("Bass", "Synth pad", "Drums"…), refined by
// words in its name and tags ("808" and "kick" are drums, "choir" is a voice, "bell" a bell…).
Category categoryFor (const std::string& family, const std::string& name, const std::vector<std::string>& tags = {});

// LYNX's own sounds, by category then name; `engineName` names an engine for the second column (default: none shown).
// Built once ("Init" presets are left out: they are starting points, not sounds).
const std::vector<Sound>& builtIn();
// The presets of the user's SoundFont files, by category then name
std::vector<Sound> fromFonts (const std::vector<sf::IndexedFont>& fonts);

// An installed instrument plugin as the plugin host lists it (round 190dg)
struct InstalledPlugin
{
    std::string id, name, vendor, format;
    std::string category; // the plugin's own ("Instrument|Synth", "Instrument|Drum", "Synth"…; may be empty)
};
// The category of an installed instrument: words in its name first ("808", "Piano", "Strings"…), then its own category
// (Drum, Piano, Organ…), else Synths and samplers.
Category pluginCategory (const std::string& category, const std::string& name);
// The installed instrument plugins as sounds (one each, named as the plugin, the vendor and format in the second
// column), by category then name. Picking one loads the plugin on the channel with its own default sound.
std::vector<Sound> fromPlugins (const std::vector<InstalledPlugin>& plugins);

// The indices of the sounds of `list` in `category` whose name or origin holds every word of `query` (case-insensitive;
// "" = all of the category), in list order.
std::vector<int> filter (const std::vector<Sound>& list, Category category, const std::string& query = {});

// A new channel that plays the sound (named after it). nullopt when it can't be built (a bad index).
std::optional<ChannelDef> channelFor (const Sound& sound);
// `current` playing the sound instead of its own: the instrument and its settings from the sound, everything else
// (name, level, pan, insert, colour, group, arpeggiator, Misc tab, key labels) kept.
std::optional<ChannelDef> withSound (const ChannelDef& current, const Sound& sound);
// Whether two channels play the same sound (the instrument and its settings; not the channel's own settings)
bool sameSound (const ChannelDef& a, const ChannelDef& b);
// The sound of `list` that `channel` plays now (-1: none of them, e.g. edited by hand or another instrument)
int find (const std::vector<Sound>& list, const ChannelDef& channel);
// The previous / next sound of the same category in `list` from position `current` (wrapping); with current < 0 the
// first (delta > 0) or last sound of `fallback`. -1 for an empty category.
int step (const std::vector<Sound>& list, int current, int delta, Category fallback = Category::keys);
// The notes an audition of the sound plays, one after the other: a chord's notes, low notes for a bass, a kit's kick,
// snare and hat, one hit for a single drum.
std::vector<int> auditionKeys (const Sound& sound);
} // namespace lynx::soundlib
