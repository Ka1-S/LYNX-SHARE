#include "core/SoundLibrary.h"

#include <algorithm>
#include <cctype>

#include "core/LynxBanks.h"
#include "core/NativeEngines.h"
#include "core/PresetCycle.h"
#include "core/SoundFont.h"
#include "core/SynthPresets.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::soundlib
{
namespace
{
std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

bool has (const std::string& text, const char* word) { return text.find (word) != std::string::npos; }

// the engine's name for the list's second column, as the Channel rack's menus name it
std::string engineName (InstrumentKind kind)
{
    if (kind == InstrumentKind::synth)
        return "LYNX synth";
    const auto* engine = natives::instrumentEngine (kind);
    return engine != nullptr ? engine->name : kind == InstrumentKind::drumsynth ? "Fruity DrumSynth Live" : "LYNX";
}

bool drumKind (InstrumentKind kind) noexcept
{
    return kind == InstrumentKind::fruitkick || kind == InstrumentKind::drumpad || kind == InstrumentKind::drumaxx || kind == InstrumentKind::drumsynth;
}

void sortSounds (std::vector<Sound>& list)
{
    std::stable_sort (list.begin(), list.end(), [] (const Sound& a, const Sound& b) {
        if (a.category != b.category)
            return (int) a.category < (int) b.category;
        if (const auto c = presetcycle::compareNatural (a.name, b.name); c != 0)
            return c < 0;
        return presetcycle::compareNatural (a.origin, b.origin) < 0;
    });
}
} // namespace

const char* categoryName (Category c) noexcept
{
    switch (c)
    {
        case Category::keys: return "Keys";
        case Category::bass: return "Bass";
        case Category::lead: return "Lead";
        case Category::pad: return TR_NOOP ("Pad");
        case Category::pluck: return TR_NOOP ("Pluck");
        case Category::synths: return TR_NOOP ("Synths and samplers");
        case Category::bells: return TR_NOOP ("Bells");
        case Category::organ: return TR_NOOP ("Organ");
        case Category::strings: return TR_NOOP ("Strings");
        case Category::brass: return TR_NOOP ("Brass and winds");
        case Category::voices: return TR_NOOP ("Voices");
        case Category::guitar: return TR_NOOP ("Guitar");
        case Category::drums: return TR_NOOP ("Drums and 808");
        case Category::effects: return "Effects";
        default: return "";
    }
}

Category categoryFor (const std::string& family, const std::string& name, const std::vector<std::string>& tags)
{
    const auto f = lower (family);
    auto words = lower (name);
    for (const auto& t : tags)
        words += " " + lower (t);

    // words in the name win over the family: an "808" in the Bass folder is a drum-machine sound, a "Choir Pad" a voice
    static const std::pair<const char*, Category> byWord[] {
        { "808", Category::drums },      { "909", Category::drums },      { "kick", Category::drums },     { "snare", Category::drums },
        { "hihat", Category::drums },    { "hi-hat", Category::drums },   { "clap", Category::drums },     { "tom", Category::drums },
        { "cymbal", Category::drums },   { "drum", Category::drums },     { "perc", Category::drums },     { "choir", Category::voices },
        { "vox", Category::voices },     { "voice", Category::voices },   { "vocal", Category::voices },   { "organ", Category::organ },
        { "bell", Category::bells },     { "chime", Category::bells },    { "glock", Category::bells },    { "marimba", Category::bells },
        { "vibra", Category::bells },    { "xylo", Category::bells },     { "celesta", Category::bells },  { "kalimba", Category::bells },
        { "string", Category::strings }, { "violin", Category::strings }, { "cello", Category::strings },  { "viola", Category::strings },
        { "harp", Category::strings },   { "brass", Category::brass },    { "trumpet", Category::brass },  { "trombone", Category::brass },
        { "horn", Category::brass },     { "sax", Category::brass },      { "flute", Category::brass },    { "clarinet", Category::brass },
        { "oboe", Category::brass },     { "guitar", Category::guitar },  { "gtr", Category::guitar },     { "banjo", Category::guitar },
        { "sitar", Category::guitar },   { "koto", Category::guitar },
    };
    const auto wordAt = [&words] (const char* w) {
        // a whole word or a word's start ("bell" in "bells", not "tom" in "atom")
        for (auto at = words.find (w); at != std::string::npos; at = words.find (w, at + 1))
            if (at == 0 || ! std::isalpha ((unsigned char) words[at - 1]))
                return true;
        return false;
    };
    for (const auto& [word, category] : byWord)
        if (wordAt (word))
            return category;

    static const std::pair<const char*, Category> byFamily[] {
        { "drum", Category::drums },         { "percussive", Category::drums },   { "bass", Category::bass },       { "lead", Category::lead },
        { "arp", Category::lead },           { "pad", Category::pad },            { "chord", Category::pad },       { "pluck", Category::pluck },
        { "chromatic", Category::bells },    { "organ", Category::organ },        { "string", Category::strings },  { "ensemble", Category::strings },
        { "brass", Category::brass },        { "reed", Category::brass },         { "pipe", Category::brass },      { "guitar", Category::guitar },
        { "ethnic", Category::guitar },      { "fx", Category::effects },         { "effect", Category::effects },  { "texture", Category::effects },
        { "stretch", Category::effects },    { "rhythm", Category::effects },     { "piano", Category::keys },      { "keys", Category::keys },
    };
    for (const auto& [word, category] : byFamily)
        if (has (f, word))
            return category;
    if (wordAt ("bass") || wordAt ("sub"))
        return Category::bass;
    if (wordAt ("lead"))
        return Category::lead;
    if (wordAt ("pad"))
        return Category::pad;
    if (wordAt ("pluck"))
        return Category::pluck;
    if (wordAt ("noise") || wordAt ("riser") || wordAt ("fx"))
        return Category::effects;
    return Category::keys;
}

const std::vector<Sound>& builtIn()
{
    static const std::vector<Sound> list = [] {
        std::vector<Sound> out;
        const auto presets = synthPresets();
        for (std::size_t i = 0; i < presets.size(); ++i)
        {
            Sound s;
            s.name = i == 0 ? "Simple saw" : presets[i].first; // (the synth's "Default": named for what it is)
            s.category = i == 0 ? Category::lead : categoryFor ({}, presets[i].first);
            s.source = Source::synth;
            s.kind = InstrumentKind::synth;
            s.index = (int) i;
            s.origin = engineName (InstrumentKind::synth);
            out.push_back (std::move (s));
        }
        for (const auto kind : banks::kindsWithBanks())
        {
            if (kind == InstrumentKind::granulizer) // (it plays a sample of the user's: nothing to hear without one)
                continue;
            const auto& bank = banks::bankOf (kind);
            for (std::size_t i = 0; i < bank.size(); ++i)
            {
                if (bank[i].apply == nullptr || lower (bank[i].category) == "init")
                    continue;
                Sound s;
                s.name = bank[i].name;
                s.category = drumKind (kind) ? Category::drums : categoryFor (bank[i].category, bank[i].name, bank[i].tags);
                s.source = Source::bank;
                s.kind = kind;
                s.index = (int) i;
                s.origin = engineName (kind);
                out.push_back (std::move (s));
            }
        }
        sortSounds (out);
        return out;
    }();
    return list;
}

std::vector<Sound> fromFonts (const std::vector<sf::IndexedFont>& fonts)
{
    std::vector<Sound> out;
    for (const auto& font : fonts)
        for (const auto& p : font.presets)
        {
            Sound s;
            s.name = p.name.empty() ? font.name : p.name;
            s.category = categoryFor (sf::category (p.bank, p.program, p.name), p.name);
            s.source = Source::soundFont;
            s.kind = InstrumentKind::soundfont;
            s.fontPath = font.path;
            s.bank = p.bank;
            s.program = p.program;
            s.origin = font.name;
            out.push_back (std::move (s));
        }
    sortSounds (out);
    return out;
}

Category pluginCategory (const std::string& category, const std::string& name)
{
    // a word in the name says most ("Grand Piano", "808 Studio", "Strings Ensemble"); "Synth" or nothing in it falls
    // through to the plugin's own category
    const auto byName = categoryFor ({}, name);
    const auto n = lower (name);
    const bool nameSays = byName != Category::keys || has (n, "piano") || has (n, "keys") || has (n, "rhodes") || has (n, "wurli");
    if (nameSays)
        return byName;
    const auto c = lower (category);
    if (has (c, "drum") || has (c, "percussion"))
        return Category::drums;
    if (has (c, "piano") || has (c, "keys"))
        return Category::keys;
    if (has (c, "organ"))
        return Category::organ;
    if (has (c, "string"))
        return Category::strings;
    if (has (c, "bass"))
        return Category::bass;
    return Category::synths;
}

std::vector<Sound> fromPlugins (const std::vector<InstalledPlugin>& plugins)
{
    std::vector<Sound> out;
    for (const auto& p : plugins)
    {
        if (p.id.empty())
            continue;
        Sound s;
        s.name = p.name.empty() ? p.id : p.name;
        s.category = pluginCategory (p.category, s.name);
        s.source = Source::plugin;
        s.kind = InstrumentKind::plugin;
        s.pluginId = p.id;
        s.origin = p.vendor.empty() ? p.format : p.format.empty() ? p.vendor : p.vendor + " \xc2\xb7 " + p.format;
        out.push_back (std::move (s));
    }
    sortSounds (out);
    return out;
}

std::vector<int> filter (const std::vector<Sound>& list, Category category, const std::string& query)
{
    std::vector<std::string> words;
    {
        const auto q = lower (query);
        std::size_t at = 0;
        while (at < q.size())
        {
            while (at < q.size() && std::isspace ((unsigned char) q[at]))
                ++at;
            auto end = at;
            while (end < q.size() && ! std::isspace ((unsigned char) q[end]))
                ++end;
            if (end > at)
                words.push_back (q.substr (at, end - at));
            at = end;
        }
    }
    std::vector<int> out;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].category != category)
            continue;
        const auto text = lower (list[i].name + " " + list[i].origin);
        if (std::all_of (words.begin(), words.end(), [&text] (const std::string& w) { return text.find (w) != std::string::npos; }))
            out.push_back ((int) i);
    }
    return out;
}

std::optional<ChannelDef> channelFor (const Sound& sound)
{
    switch (sound.source)
    {
        case Source::synth:
        {
            const auto presets = synthPresets();
            if (sound.index < 0 || sound.index >= (int) presets.size())
                return std::nullopt;
            ChannelDef def;
            def.instrument = InstrumentKind::synth;
            def.synth = presets[(std::size_t) sound.index].second;
            def.name = sound.name;
            return def;
        }
        case Source::bank:
        {
            ChannelDef def;
            def.instrument = sound.kind;
            auto out = banks::withPreset (def, sound.index);
            if (out)
                out->name = sound.name;
            return out;
        }
        case Source::soundFont:
        {
            if (sound.fontPath.empty())
                return std::nullopt;
            auto def = sf::makeChannel (sound.fontPath, sound.bank, sound.program);
            def.name = sound.name;
            return def;
        }
        case Source::plugin:
        {
            if (sound.pluginId.empty())
                return std::nullopt;
            ChannelDef def;
            def.name = sound.name;
            def.instrument = InstrumentKind::plugin;
            def.plugin.id = sound.pluginId;
            def.plugin.name = sound.name;
            return def;
        }
    }
    return std::nullopt;
}

std::optional<ChannelDef> withSound (const ChannelDef& current, const Sound& sound)
{
    const auto fresh = channelFor (sound);
    if (! fresh)
        return std::nullopt;
    auto out = current;
    out.instrument = fresh->instrument;
    out.samplePath = fresh->samplePath;
    out.zones = fresh->zones;
    out.plugin = fresh->plugin;
    out.controllerState = fresh->controllerState;
    out.modelParams = fresh->modelParams;
    out.pluginState = fresh->pluginState;
    out.sampler = fresh->sampler;
    out.synth = fresh->synth;
    out.patcher = fresh->patcher;
    return out;
}

bool sameSound (const ChannelDef& a, const ChannelDef& b)
{
    // (the fields withSound takes from a sound)
    return a.instrument == b.instrument && a.samplePath == b.samplePath && a.modelParams == b.modelParams && a.pluginState == b.pluginState
           && a.synth == b.synth && a.sampler == b.sampler && a.zones == b.zones && a.plugin == b.plugin && a.controllerState == b.controllerState
           && a.patcher == b.patcher;
}

int find (const std::vector<Sound>& list, const ChannelDef& channel)
{
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].kind != channel.instrument)
            continue;
        if (list[i].source == Source::soundFont && list[i].fontPath != channel.samplePath)
            continue;
        if (list[i].source == Source::plugin) // (a running plugin's saved state is its own: the plugin is the sound)
        {
            if (channel.plugin.id == list[i].pluginId)
                return (int) i;
            continue;
        }
        if (const auto c = channelFor (list[i]); c && sameSound (*c, channel))
            return (int) i;
    }
    return -1;
}

int step (const std::vector<Sound>& list, int current, int delta, Category fallback)
{
    const auto category = current >= 0 && current < (int) list.size() ? list[(std::size_t) current].category : fallback;
    const auto same = filter (list, category);
    if (same.empty())
        return -1;
    const auto it = std::find (same.begin(), same.end(), current);
    const auto at = it == same.end() ? -1 : (int) (it - same.begin());
    return same[(std::size_t) presetcycle::step (at, delta, (int) same.size())];
}

std::vector<int> auditionKeys (const Sound& sound)
{
    if (sound.category == Category::drums)
    {
        // a kit (General MIDI's places: kick, snare, closed hat) or one drum on middle C
        const bool kit = sound.source == Source::soundFont || sound.kind == InstrumentKind::drumaxx || sound.kind == InstrumentKind::drumsynth;
        return kit ? std::vector<int> { 36, 38, 42, 36 } : std::vector<int> { 60 };
    }
    if (sound.category == Category::bass)
        return { 36, 43, 48 };
    return { 60, 64, 67 };
}
} // namespace lynx::soundlib
