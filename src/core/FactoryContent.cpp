#include "core/FactoryContent.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include "core/BrowserCollections.h"
#include "core/ChainPresets.h"
#include "core/DemoSongs.h"
#include "core/LoopStarter.h"
#include "core/LynxBanks.h"
#include "core/SoundLibrary.h"

namespace lynx::factorycontent
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// the name's words, lower case, split at anything that is not a letter or a digit ("Lo-Fi Keys" -> lo, fi, keys; also
// the whole name for words with a dash: "lo-fi")
std::set<std::string> wordsOf (const std::string& name)
{
    std::set<std::string> out;
    std::string w;
    const auto l = lower (name);
    for (const auto c : l + " ")
        if (std::isalnum ((unsigned char) c))
            w += c;
        else
        {
            if (! w.empty())
                out.insert (w);
            w.clear();
        }
    if (l.find ("lo-fi") != std::string::npos || l.find ("lofi") != std::string::npos)
        out.insert ("lofi");
    return out;
}

void add (std::vector<std::string>& tags, const std::string& tag)
{
    if (std::none_of (tags.begin(), tags.end(), [&tag] (const std::string& t) { return lower (t) == lower (tag); }))
        tags.push_back (tag);
}

// word in the name -> tags (genre and mood). GarageBand's keyword families: instrument, genre, descriptors.
struct Rule
{
    const char* word;
    std::vector<const char*> tags;
};

const std::vector<Rule>& rules()
{
    static const std::vector<Rule> r = {
        // genres
        { "808", { "Trap", "Hip hop", "Punchy" } },  { "trap", { "Trap" } },          { "reese", { "Drum & bass", "Dark" } },
        { "supersaw", { "EDM", "Bright" } },          { "acid", { "Techno", "Aggressive" } }, { "house", { "House" } },
        { "techno", { "Techno" } },                   { "lofi", { "Lo-fi", "Vintage", "Chill" } }, { "dusty", { "Lo-fi", "Vintage" } },
        { "tape", { "Lo-fi", "Vintage" } },           { "cassette", { "Lo-fi", "Vintage" } }, { "dub", { "Dubstep" } },
        { "wobble", { "Dubstep", "Aggressive" } },    { "retro", { "Synthwave", "Vintage" } }, { "synthwave", { "Synthwave" } },
        { "orchestra", { "Cinematic" } },             { "choir", { "Cinematic" } },    { "rise", { "EDM", "Cinematic" } },
        { "risers", { "EDM", "Cinematic" } },         { "gospel", { "Gospel" } },      { "jazz", { "Jazz" } },
        { "funk", { "Funk" } },                       { "disco", { "Disco" } },        { "chip", { "Chiptune" } },
        { "radio", { "Lo-fi", "Vintage" } },
        // moods / descriptors
        { "dark", { "Dark" } },        { "night", { "Dark", "Dreamy" } }, { "deep", { "Dark" } },     { "bright", { "Bright" } },
        { "glass", { "Bright", "Dreamy" } }, { "shimmer", { "Bright", "Dreamy" } }, { "bell", { "Bright", "Dreamy" } },
        { "warm", { "Warm" } },        { "round", { "Warm" } },        { "felt", { "Soft", "Warm" } }, { "soft", { "Soft" } },
        { "whisper", { "Soft" } },     { "air", { "Soft", "Dreamy" } }, { "dream", { "Dreamy" } },    { "space", { "Dreamy" } },
        { "canyon", { "Dreamy" } },    { "hall", { "Dreamy" } },       { "grit", { "Distorted", "Aggressive" } },
        { "dist", { "Distorted", "Aggressive" } }, { "fuzz", { "Distorted", "Aggressive" } }, { "crunch", { "Distorted" } },
        { "growl", { "Distorted", "Aggressive" } }, { "smash", { "Aggressive", "Punchy" } }, { "hard", { "Aggressive" } },
        { "punch", { "Punchy" } },     { "tight", { "Punchy", "Clean" } }, { "kick", { "Punchy" } },  { "snap", { "Punchy" } },
        { "clean", { "Clean" } },      { "clear", { "Clean" } },       { "pure", { "Clean" } },       { "sine", { "Clean" } },
        { "vintage", { "Vintage" } },  { "analog", { "Vintage", "Warm" } }, { "old", { "Vintage" } }, { "worn", { "Vintage" } },
        { "wide", { "Wide" } },        { "stage", { "Wide" } },        { "big", { "Wide" } },         { "chill", { "Chill" } },
        { "mellow", { "Chill", "Soft" } }, { "slap", { "Vintage" } },
    };
    return r;
}

void addFromWords (std::vector<std::string>& tags, const std::set<std::string>& words)
{
    for (const auto& r : rules())
        if (words.count (r.word) > 0)
            for (const auto* t : r.tags)
                add (tags, t);
}

// a sound category (soundlib::categoryName) -> the instrument tag and the genre / mood its sounds usually have
void addCategory (std::vector<std::string>& tags, const std::string& category)
{
    static const std::map<std::string, std::vector<const char*>> map = {
        { "Keys", { "Keys", "Pop" } },      { "Bass", { "Bass" } },          { "Lead", { "Lead", "EDM" } },
        { "Pad", { "Pad", "Ambient", "Dreamy" } }, { "Pluck", { "Pluck", "House", "Pop" } }, { "Synths and samplers", { "Synth" } },
        { "Bells", { "Bells", "Dreamy" } }, { "Organ", { "Organ", "Gospel" } }, { "Strings", { "Strings", "Cinematic" } },
        { "Brass and winds", { "Brass", "Cinematic" } }, { "Voices", { "Vocals" } }, { "Guitar", { "Guitar", "Rock" } },
        { "Drums and 808", { "Drums" } },       { "Drums", { "Drums" } },           { "Effects", { "FX" } },
        // the chain presets' categories
        { "Voice", { "Vocals" } },          { "Lo-fi", { "Lo-fi", "Vintage", "Chill" } }, { "Space", { "Ambient", "Dreamy" } },
    };
    if (const auto it = map.find (category); it != map.end())
        for (const auto* t : it->second)
            add (tags, t);
}
} // namespace

std::vector<std::string> soundTags (const std::string& name, const std::string& category, const std::vector<std::string>& bankWords)
{
    std::vector<std::string> tags;
    addCategory (tags, category);
    auto words = wordsOf (name);
    for (const auto& w : bankWords)
        for (const auto& part : wordsOf (w))
            words.insert (part);
    addFromWords (tags, words);
    return tags;
}

std::vector<std::string> chainTags (const std::string& name, const std::string& category)
{
    std::vector<std::string> tags;
    addCategory (tags, category);
    add (tags, "Effect chain");
    addFromWords (tags, wordsOf (name));
    return tags;
}

std::vector<std::string> beatTags (const std::string& genreId, const std::string& genreLabel)
{
    std::vector<std::string> tags { "Drums", "Beat" };
    add (tags, genreLabel);
    static const std::map<std::string, std::vector<const char*>> mood = {
        { "boom bap", { "Hip hop", "Vintage", "Chill" } }, { "drill", { "Dark", "Aggressive" } }, { "edm", { "Bright", "Punchy" } },
        { "hip hop", { "Punchy" } },  { "house", { "Punchy", "Bright" } }, { "lo-fi", { "Vintage", "Chill" } },
        { "techno", { "Dark", "Punchy" } }, { "trap", { "Hip hop", "Dark", "Punchy" } },
    };
    if (const auto it = mood.find (genreId); it != mood.end())
        for (const auto* t : it->second)
            add (tags, t);
    return tags;
}

const std::vector<Item>& items()
{
    static const std::vector<Item> all = [] {
        std::vector<Item> out;
        // sounds
        const auto& sounds = soundlib::builtIn();
        for (int i = 0; i < (int) sounds.size(); ++i)
        {
            const auto& s = sounds[(std::size_t) i];
            Item it;
            it.kind = Kind::sound;
            it.index = i;
            it.name = s.name;
            it.folder = soundlib::categoryName (s.category);
            it.detail = s.origin;
            std::vector<std::string> bankWords;
            if (s.source == soundlib::Source::bank)
                if (const auto& bank = banks::bankOf (s.kind); s.index >= 0 && s.index < (int) bank.size())
                    bankWords = bank[(std::size_t) s.index].tags;
            it.tags = soundTags (s.name, it.folder, bankWords);
            it.key = std::string (keyPrefix) + "sound/" + std::to_string ((int) s.kind) + "/" + s.name;
            out.push_back (std::move (it));
        }
        // beats: Loop Starter's own genres and grooves (built-in kit)
        const auto config = loopstarter::builtinConfig();
        for (const auto* g : config.listed())
        {
            int n = 0;
            for (const auto& p : loopstarter::builtinPresets (g->id))
            {
                Item it;
                it.kind = Kind::beat;
                it.genre = g->id;
                it.preset = p.name;
                it.name = g->label + " beat " + std::to_string (++n);
                it.folder = g->label;
                it.detail = std::to_string ((int) std::lround (g->defaultTempo)) + " BPM";
                it.tags = beatTags (g->id, g->label);
                it.key = std::string (keyPrefix) + "beat/" + g->id + "/" + p.name;
                out.push_back (std::move (it));
            }
        }
        // effect chains
        const auto& chains = chains::factory();
        for (int i = 0; i < (int) chains.size(); ++i)
        {
            const auto& c = chains[(std::size_t) i];
            Item it;
            it.kind = Kind::chain;
            it.index = i;
            it.name = c.name;
            it.folder = c.category;
            it.detail = c.description;
            it.tags = chainTags (c.name, c.category);
            it.key = std::string (keyPrefix) + "chain/" + c.name;
            out.push_back (std::move (it));
        }
        return out;
    }();
    return all;
}

const Item* find (const std::string& pluginKey)
{
    static const std::map<std::string, std::size_t> index = [] {
        std::map<std::string, std::size_t> m;
        const auto& all = items();
        for (std::size_t i = 0; i < all.size(); ++i)
            m.emplace (all[i].key, i);
        return m;
    }();
    const auto it = index.find (pluginKey);
    return it == index.end() ? nullptr : &items()[it->second];
}

const browsertags::Store& tags()
{
    static const browsertags::Store store = [] {
        browsertags::Store s;
        for (const auto& it : items())
            for (const auto& t : it.tags)
                s.set (collections::pluginItem (it.key), t, true);
        return s;
    }();
    return store;
}

simpletracks::Result addBeat (Song& song, const Item& beat, int fromBar, int bars)
{
    simpletracks::Result r;
    if (beat.kind != Kind::beat)
        return r;
    static const auto config = loopstarter::builtinConfig();
    loopstarter::Sources sources;
    sources.config = &config;
    for (auto& p : loopstarter::builtinPresets (beat.genre))
        if (p.name == beat.preset)
            sources.presets[beat.genre].push_back (std::move (p));
    if (sources.presets[beat.genre].empty())
        return r;
    std::uint64_t seed = 1469598103934665603ull; // (FNV-1a of the key: the same item, the same beat)
    for (const auto c : beat.key)
        seed = (seed ^ (unsigned char) c) * 1099511628211ull;
    const auto plan = loopstarter::generate (sources, beat.genre, seed);
    simpletracks::Request q;
    q.kind = simpletracks::Kind::drums;
    q.name = "Drums";
    q.kit = loopstarter::kitChannels (plan);
    q.pattern = loopstarter::kitPattern (plan);
    r = simpletracks::addTrack (song, q);
    if (r.ok() && r.pattern >= 0)
    {
        fromBar = std::max (0, fromBar);
        const auto every = std::max (1, (int) (song.patterns[(std::size_t) r.pattern].effectiveLength (song.meter()) / std::max<std::int64_t> (1, song.barTicks())));
        demos::detail::repeatClip (song, r.track, r.pattern, fromBar, fromBar + std::max (1, bars), every);
    }
    return r;
}

bool applyChain (Song& song, const Item& chain, int insert)
{
    const auto& all = chains::factory();
    if (chain.kind != Kind::chain || chain.index < 0 || chain.index >= (int) all.size() || insert <= 0)
        return false;
    return chains::apply (song, insert, all[(std::size_t) chain.index]);
}

browsertags::Store merged (const browsertags::Store& user)
{
    auto out = user;
    const auto& factory = tags();
    for (const auto& [key, name] : factory.names)
        out.names.emplace (key, name); // (a name the user has keeps the user's case)
    for (const auto& [item, keys] : factory.items)
        out.items[item].insert (keys.begin(), keys.end());
    return out;
}
} // namespace lynx::factorycontent
