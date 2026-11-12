#pragma once

// FL's Tools › One-click audio recording › "I want to record audio...": Into Edison audio editor/recorder | Into
// playlist as an audio clip (ToolsRecordIntoEdisonAction / ToolsRecordIntoPlaylistAction → FLEngine 2026 FUN_0120d380
// with mode 3 / 2; round 190dx, /mnt/e/lynx-work/flpl190dx/FL-FACTS.md). JUCE-free: what a pick does, decided from the
// song and the mixer's selection; the UI then arms, asks for an input, loads Edison and starts the transport.

#include <vector>

#include "core/Song.h"

namespace lynx::oneclick
{
enum class Into : int
{
    edison = 3,  // FL's mode 3 (the menu's default item)
    playlist = 2 // FL's mode 2
};

struct Plan
{
    // the mixer track FL works on: the selected one (LYNX: when the Master or nothing is selected, the first insert
    // with an audio input, else a free insert — FL always has a selected mixer track); −1 = none could be found
    int insert = -1;
    // Into playlist: the inserts that record. Already armed ones are kept as they are (FL records what is armed);
    // otherwise `insert` alone, which gets armed
    std::vector<int> record;
    bool armInsert = false; // `insert` must be armed first
    bool askInput = false;  // ... and it has no audio input yet: FL asks for one (cancelled: "Recording aborted")
    bool songMode = false;  // Into playlist switches to Song mode
    bool loadEdison = false; // Into Edison: an Edison set to record on play goes on `insert` ("Open audio editor and record")
    bool startPlayback = true; // both end with Play (Into playlist: with Record on)
    bool recordOn = false;
};

inline int targetInsert (const Song& song, int selected)
{
    const auto count = (int) song.mixer.size();
    if (selected > 0 && selected < count)
        return selected;
    for (int t = 1; t < count; ++t)
        if (song.mixer[(std::size_t) t].inputChannel >= 0)
            return t;
    const auto free = song.nextFreeInsert();
    return free > 0 && free < count ? free : -1;
}

inline Plan plan (const Song& song, const std::vector<int>& armed, int selected, Into into)
{
    Plan p;
    p.insert = targetInsert (song, selected);
    if (into == Into::edison)
    {
        p.loadEdison = p.insert > 0;
        p.startPlayback = p.insert > 0;
        return p;
    }
    p.songMode = true;
    p.recordOn = true;
    if (! armed.empty())
    {
        p.record = armed;
        return p;
    }
    if (p.insert <= 0)
    {
        p.startPlayback = p.recordOn = false;
        return p;
    }
    p.record = { p.insert };
    p.armInsert = true;
    p.askInput = song.mixer[(std::size_t) p.insert].inputChannel < 0;
    return p;
}
} // namespace lynx::oneclick
