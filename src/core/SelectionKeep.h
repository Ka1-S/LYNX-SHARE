#pragma once

// Round 190rs (G-31, 190rl FIX-LIST 12): an editor's selection over an undo / redo, kept by content. Notes and Playlist
// clips have no stable id and are re-sorted after a move, so a selection kept by index lit up other items after "select,
// move, Ctrl+Z". The editor keeps the items' keys (channel, key, start, length for a note; track, kind, source, start,
// length for a clip) in index order as it last saw them; after an undo / redo the selection is found again:
// - an item whose key is still there keeps its selection (duplicates count: two equal selected notes, two kept);
// - the items that changed are paired in order (the k-th item of the old list that is gone with the k-th new item) when
//   there are as many of each (a move, a transposition, a length change: the undone or redone edit of those items), and
//   a new item is selected when its old partner was. When items were added or removed, only the kept ones stay selected.
// JUCE-free; `Key` needs operator<.

#include <algorithm>
#include <map>
#include <vector>

namespace lynx::selkeep
{
template <typename Key>
std::vector<int> remap (const std::vector<Key>& before, const std::vector<int>& selected, const std::vector<Key>& after)
{
    std::vector<char> chosen (before.size(), 0);
    for (const auto i : selected)
        if (i >= 0 && i < (int) before.size())
            chosen[(std::size_t) i] = 1;

    std::map<Key, int> afterCount, beforeCount, keptChosen;
    for (const auto& k : after)
        ++afterCount[k];
    std::vector<int> gone; // old items with no equal new one, in index order
    for (int i = 0; i < (int) before.size(); ++i)
    {
        const auto& k = before[(std::size_t) i];
        ++beforeCount[k];
        if (auto a = afterCount.find (k); a != afterCount.end() && a->second > 0)
        {
            --a->second;
            if (chosen[(std::size_t) i])
                ++keptChosen[k];
        }
        else
            gone.push_back (i);
    }

    std::vector<int> out, appeared; // appeared: new items with no equal old one, in index order
    for (int j = 0; j < (int) after.size(); ++j)
    {
        const auto& k = after[(std::size_t) j];
        if (auto b = beforeCount.find (k); b != beforeCount.end() && b->second > 0)
        {
            --b->second;
            if (auto s = keptChosen.find (k); s != keptChosen.end() && s->second > 0)
            {
                --s->second;
                out.push_back (j);
            }
        }
        else
            appeared.push_back (j);
    }
    if (gone.size() == appeared.size())
        for (std::size_t n = 0; n < gone.size(); ++n)
            if (chosen[(std::size_t) gone[n]])
                out.push_back (appeared[n]);
    std::sort (out.begin(), out.end());
    return out;
}
} // namespace lynx::selkeep
