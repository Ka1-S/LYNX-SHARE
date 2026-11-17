#pragma once

// Rollup 0.1.30 step 6 (round 190km; the owner answered yes to gaps190kf's question): LYNX's own tagged content (round
// 190ke's Loop Starter beats and sounds, core/FactoryContent.h) as rows of the Loop Browser (round gaps190kf,
// core/LoopKeywords.h), so a new user without sample folders still finds loops there.
//
// A beat is a pattern loop (LYNX's third Loop Type, GarageBand's Drummer loops: a groove the Channel Rack plays) at its
// genre's suggested tempo; a sound is a MIDI loop's instrument (GarageBand's green Software Instrument loops: notes
// played by a sound). Their keywords come from their folder and name words and from 190ke's built-in tags that name a
// keyword; the row's path is the tag store's item (collections::pluginItem), so the user's own tags and favourites of
// the item apply as for a file. Effect chains are not loops and are left out. JUCE-free: tests/Rollup030Tests.inc.

#include <cstdlib>
#include <string>
#include <vector>

#include "core/BrowserCollections.h"
#include "core/FactoryContent.h"
#include "core/LoopKeywords.h"

namespace lynx::factoryloops
{
// The row path of an item (the tag store's item for its plugin key), and back (nullptr: not LYNX content)
inline std::string pathOf (const factorycontent::Item& item) { return collections::pluginItem (item.key); }
inline const factorycontent::Item* itemOf (const std::string& path)
{
    const std::string prefix = collections::pluginItem (factorycontent::keyPrefix);
    return path.rfind (prefix, 0) == 0 ? factorycontent::find (path.substr (collections::pluginItem ("").size())) : nullptr;
}

// One item as the Loop Browser describes it (a chain: type 0, not a loop)
inline loopkeys::LoopInfo describe (const factorycontent::Item& item)
{
    // (the words of the folder and the name, as a file "Trap/Trap beat 1" would give; no extension: the type is set here)
    auto info = loopkeys::describe (item.folder + "/" + item.name, item.folder + "/" + item.name, item.tags);
    info.path = pathOf (item);
    info.name = item.name;
    info.type = item.kind == factorycontent::Kind::beat ? (int) loopkeys::pattern : item.kind == factorycontent::Kind::sound ? (int) loopkeys::midi : 0;
    if (item.kind == factorycontent::Kind::beat)
    {
        info.tempo = std::atof (item.detail.c_str()); // (the genre's suggested tempo: "140 BPM")
        info.keyRoot = -1;
        info.scale = loopkeys::Scale::neither; // (a drum groove: no tonal scale)
    }
    return info;
}

// Every beat and sound, in FactoryContent's order (sounds, then beats)
inline std::vector<loopkeys::LoopInfo> loops()
{
    std::vector<loopkeys::LoopInfo> out;
    for (const auto& item : factorycontent::items())
        if (item.kind != factorycontent::Kind::chain)
            out.push_back (describe (item));
    return out;
}
} // namespace lynx::factoryloops
