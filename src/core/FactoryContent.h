#pragma once

// LYNX's own factory content with keyword tags (round 190ke, owner decision D2 of coverage190jw: GarageBand's Loop
// Browser keywords — instrument, genre, mood — on LYNX's own content only; no third-party content is tagged).
//
// GarageBand (Apple's GarageBand User Guide for Mac, "Search for Apple Loops"): the Loop Browser filters loops by keyword
// buttons (instrument, genre, descriptors such as Dark / Cheerful, Relaxed / Intense, Clean / Distorted). LYNX ships no
// sampled content: its factory content is the sounds of its own instruments (core/SoundLibrary.h builtIn: the built-in
// synth's presets and LYNX's preset banks), the beats of Loop Starter's own genres (core/LoopStarter.h builtinPresets,
// one-bar drum grooves on the built-in kit) and the factory effect chains (core/ChainPresets.h). Each item gets tags of
// three kinds: what it is (the instrument), a genre and a mood, from its category and words in its name (and a bank
// preset's own search words). The Browser lists them under "LYNX content" in the Simple layout and its TAGS panel / the
// "#tag" search filter them with the user's own tags (core/BrowserTags.h); these tags are built in, never saved.
// No JUCE (tests/SimpleGb190keTests.inc).

#include <string>
#include <vector>

#include "core/BrowserTags.h"
#include "core/SimpleTracks.h"

namespace lynx::factorycontent
{
enum class Kind { sound, beat, chain };

struct Item
{
    Kind kind = Kind::sound;
    std::string key;    // the Browser's plugin key: "lynx-content/<kind>/<id>" (the tag store's item is "plugin:" + key)
    std::string name;   // shown as it is (preset names are not translated)
    std::string folder; // the folder under its kind: a sound's category, a beat's genre, a chain's category (English)
    std::string detail; // the second column: what makes it ("LYNX synth", "Trap", the chain's description)
    std::vector<std::string> tags;
    int index = -1;     // sound: into soundlib::builtIn(); chain: into chains::factory()
    std::string genre;  // beat: Loop Starter's genre id ("trap")
    std::string preset; // beat: the groove's name in loopstarter::builtinPresets (genre)
};

inline constexpr const char* keyPrefix = "lynx-content/";
inline bool isKey (const std::string& pluginKey) noexcept { return pluginKey.rfind (keyPrefix, 0) == 0; }

// Every item, sounds then beats then chains (built once)
const std::vector<Item>& items();
const Item* find (const std::string& pluginKey);
// The items' tags as a tag store (items under collections::pluginItem (key)), built once
const browsertags::Store& tags();
// The tag words of one name / category (exposed for the tests)
std::vector<std::string> soundTags (const std::string& name, const std::string& category, const std::vector<std::string>& bankWords = {});
std::vector<std::string> chainTags (const std::string& name, const std::string& category);
std::vector<std::string> beatTags (const std::string& genreId, const std::string& genreLabel);

// What a double-click on an item does (one undo step, the caller's): a beat becomes a Simple Drums track (Loop Starter's
// built-in kit playing that groove, seeded so the same item gives the same beat) whose clip repeats from bar `fromBar`
// (0-based) for `bars` bars; an effect chain goes onto insert `insert` (core/ChainPresets.h apply). Sounds are channels
// (soundlib::channelFor), added by the Browser as any instrument.
simpletracks::Result addBeat (Song& song, const Item& beat, int fromBar, int bars = 4);
bool applyChain (Song& song, const Item& chain, int insert);

// The user's store with the factory tags added (the TAGS panel, the filters): the user's own come first for a name in
// both (its case wins); items keep the tags of both.
browsertags::Store merged (const browsertags::Store& user);
} // namespace lynx::factorycontent
