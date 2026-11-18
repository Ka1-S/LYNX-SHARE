#pragma once

// Issue #201 (round 190ll): "Rename and color..." (F2) and the colour items with several tracks selected colour all of
// them, not only the one the dialog or menu was opened on.
//
// FL defines it for the Mixer (manual, Mixer track menu: "Rename, color & icon... (F2) - Set the selected Mixer Track
// name, color & icon. [...] For a selection of tracks - The name/icon will only apply to unnamed tracks."): the colour
// goes to every selected track; the typed name to the track the dialog belongs to and to the selected tracks that have
// no name of their own. FL's manual says nothing for Playlist tracks or channels (only the Picker's clips and Color
// selected act on several): LYNX colours every selected one there too and renames only the one the dialog was opened on.
//
// Only what the dialog changed is spread: renaming alone keeps the other tracks' colours, a new colour alone keeps
// their names.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::selrename
{
// What a menu / dialog opened on `item` acts on: the selection when the item is in it, else the item alone (FL's rule
// for the Mixer's "selected track(s)" items; LYNX's for the Playlist's and the Channel Rack's)
inline std::vector<int> targets (int item, const std::vector<int>& selection)
{
    if (std::find (selection.begin(), selection.end(), item) == selection.end())
        return { item };
    auto list = selection;
    std::sort (list.begin(), list.end());
    list.erase (std::unique (list.begin(), list.end()), list.end());
    return list;
}

struct Result
{
    std::string oldName, newName;
    std::uint32_t oldColour = 0, newColour = 0;
    bool nameChanged() const { return newName != oldName; }
    bool colourChanged() const { return newColour != oldColour; }
};

// The Mixer (FL-exact): `item` takes the result as before (a blank name keeps its name); the other targets take the
// colour when it changed, and the name when it changed and they have none.
inline void applyToMixer (Song& song, int item, const std::vector<int>& list, const Result& r)
{
    const auto count = (int) song.mixer.size();
    if (item >= 0 && item < count)
    {
        if (! r.newName.empty())
            song.mixer[(std::size_t) item].name = r.newName;
        song.mixer[(std::size_t) item].colour = r.newColour;
    }
    for (const auto t : list)
    {
        if (t == item || t < 0 || t >= count)
            continue;
        auto& track = song.mixer[(std::size_t) t];
        if (r.colourChanged())
            track.colour = r.newColour;
        if (r.nameChanged() && ! r.newName.empty() && track.name.empty())
            track.name = r.newName;
    }
}

// Playlist tracks: `item` takes the name and colour; the other targets the colour when it changed
inline void applyToPlaylistTracks (Song& song, int item, const std::vector<int>& list, const Result& r)
{
    auto top = item;
    for (const auto t : list)
        top = std::max (top, t);
    if (top >= 0 && (int) song.playlistTracks.size() <= top)
        song.playlistTracks.resize ((std::size_t) top + 1);
    if (item >= 0)
    {
        song.playlistTracks[(std::size_t) item].name = r.newName;
        song.playlistTracks[(std::size_t) item].colour = r.newColour;
    }
    if (r.colourChanged())
        for (const auto t : list)
            if (t != item && t >= 0)
                song.playlistTracks[(std::size_t) t].colour = r.newColour;
}

// Channels: `item` takes the name and colour; the other targets the colour when it changed
inline void applyToChannels (Song& song, int item, const std::vector<int>& list, const Result& r)
{
    const auto count = (int) song.channels.size();
    if (item >= 0 && item < count)
    {
        song.channels[(std::size_t) item].name = r.newName;
        song.channels[(std::size_t) item].colour = r.newColour;
    }
    if (r.colourChanged())
        for (const auto t : list)
            if (t != item && t >= 0 && t < count)
                song.channels[(std::size_t) t].colour = r.newColour;
}

// A colour picked from a menu ("Change color", "Random color"): every target
inline void colourMixer (Song& song, const std::vector<int>& list, std::uint32_t colour)
{
    for (const auto t : list)
        if (t >= 0 && t < (int) song.mixer.size())
            song.mixer[(std::size_t) t].colour = colour;
}

inline void colourChannels (Song& song, const std::vector<int>& list, std::uint32_t colour)
{
    for (const auto t : list)
        if (t >= 0 && t < (int) song.channels.size())
            song.channels[(std::size_t) t].colour = colour;
}
} // namespace lynx::selrename
