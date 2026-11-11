// --- Round 184a S11: "Guess from notes" for the project key (core/KeyGuess.h) on synthetic patterns.

#include "core/KeyGuess.h"

namespace keyguess184a
{
// One pattern on channel 0: `keys` in turn, each `len` ticks; channel 1 plays a kick on one key (percussion).
inline lynx::Song songOf (std::initializer_list<int> keys, std::int64_t len = lynx::ppq)
{
    lynx::Song s;
    s.channels.resize (2);
    s.patterns.resize (1);
    std::int64_t t = 0;
    for (auto k : keys)
    {
        lynx::Note n;
        n.channel = 0;
        n.key = k;
        n.start = t;
        n.length = len;
        s.patterns[0].notes.push_back (n);
        t += len;
    }
    for (int i = 0; i < 16; ++i) // a four-on-the-floor kick on C5, never a pitch
    {
        lynx::Note n;
        n.channel = 1;
        n.key = 61; // C#: would pull every guess off if it counted
        n.start = i * lynx::ppq;
        n.length = lynx::ppq / 4;
        s.patterns[0].notes.push_back (n);
    }
    return s;
}

inline int scaleIndex (const char* name)
{
    const auto& list = lynx::notetools::flReferenceScales();
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::string (list[(std::size_t) i].name) == name)
            return i;
    return -1;
}
} // namespace keyguess184a

TEST (simple184a_keyguess_major_minor_modes)
{
    using namespace lynx;
    using keyguess184a::scaleIndex;
    using keyguess184a::songOf;
    const auto major = scaleIndex ("Major (Ionian)"), minor = scaleIndex ("Minor Natural (Aeolian)");
    const auto dorian = scaleIndex ("Other Dorian"), mixolydian = scaleIndex ("Other Mixolydian"), harmonic = scaleIndex ("Minor Harmonic");
    CHECK (major >= 0 && minor >= 0 && dorian >= 0 && mixolydian >= 0 && harmonic >= 0);

    // C major: the scale up and down, starting and ending on C, with C and G held longest
    auto s = songOf ({ 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60, 60, 67 });
    auto g = keyguess::guess (s);
    CHECK (g.size() == 3);
    CHECK (g[0].key == (projectkey::Key { 0, major }));
    CHECK (g[0].notes == 13 && g[0].notesInScale == 13); // the kick is not counted
    CHECK (g[0].score > g[1].score);

    // A minor: the same notes, on A and E
    s = songOf ({ 57, 59, 60, 62, 64, 65, 67, 69, 64, 60, 57, 57, 64, 45, 45 });
    g = keyguess::guess (s);
    CHECK (g[0].key == (projectkey::Key { 9, minor }));

    // D Dorian: D minor with a B natural, centred on D
    s = songOf ({ 62, 64, 65, 67, 69, 71, 72, 74, 69, 71, 62, 62, 50, 50, 57 });
    g = keyguess::guess (s);
    CHECK (g[0].key == (projectkey::Key { 2, dorian }));

    // G Mixolydian: G major with F natural, centred on G
    s = songOf ({ 55, 57, 59, 60, 62, 64, 65, 67, 65, 62, 55, 55, 43, 43, 62 });
    g = keyguess::guess (s);
    CHECK (g[0].key == (projectkey::Key { 7, mixolydian }));

    // E harmonic minor: E minor with D#
    s = songOf ({ 52, 54, 55, 57, 59, 60, 63, 64, 63, 64, 59, 52, 40, 40, 59 });
    g = keyguess::guess (s);
    CHECK (g[0].key == (projectkey::Key { 4, harmonic }));
    CHECK (g[0].notesInScale == g[0].notes);
}

TEST (simple184a_keyguess_edges)
{
    using namespace lynx;
    using keyguess184a::songOf;
    // nothing pitched: no guess
    Song empty;
    CHECK (keyguess::guess (empty).empty());
    auto drumsOnly = songOf ({});
    CHECK (keyguess::guess (drumsOnly).empty());

    // a wrong note is counted out of the total, not dropped
    auto s = songOf ({ 60, 62, 64, 65, 67, 69, 71, 72, 60, 61 });
    auto g = keyguess::guess (s, 5);
    CHECK (g.size() == 5);
    CHECK (g[0].key.root == 0 && g[0].notes == 10 && g[0].notesInScale == 9);

    // muted notes are skipped; longer notes weigh more (a held A over short Cs makes A the centre)
    s = songOf ({ 60, 64, 67, 69, 72 });
    for (auto& n : s.patterns[0].notes)
        if (n.channel == 0 && n.key == 72)
            n.muted = true;
    CHECK (keyguess::histogram (s).keys.size() == 4);
    s = songOf ({ 57, 60, 64, 57, 62, 65, 57, 59, 64 }, lynx::ppq / 4);
    for (auto& n : s.patterns[0].notes)
        if (n.channel == 0 && n.key == 57)
            n.length = 4 * lynx::ppq;
    g = keyguess::guess (s);
    CHECK (g[0].key.root == 9);
    // the guess never changes the song
    const auto before = s.scaleHighlight;
    keyguess::guess (s);
    CHECK (s.scaleHighlight == before);
    // count 0
    CHECK (keyguess::guess (s, 0).empty());
}

TEST (simple184a_key_short_names)
{
    using namespace lynx;
    const auto order = projectkey::commonFirst();
    CHECK (projectkey::shortScaleName (order[0]) == "major");
    CHECK (projectkey::shortScaleName (order[1]) == "minor");
    for (int i = 0; i < keyguess::commonScales; ++i) // the menu's common scales all have a short name
        CHECK (projectkey::shortScaleName (order[(std::size_t) i]) != notetools::flReferenceScales()[(std::size_t) order[(std::size_t) i]].name);
    CHECK (projectkey::shortScaleName (order.back()) == notetools::flReferenceScales()[(std::size_t) order.back()].name); // the rest: FL's name
    CHECK (projectkey::shortScaleName (-2).empty() && projectkey::shortScaleName (9999).empty());
}
