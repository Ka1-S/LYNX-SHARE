// Round 190on (research190oe P2, P8, P10 and the Online Sessions "remove a guest" decision): the command palette's matcher,
// Tab to transient, stacked controller lanes, and the invite code change when the host removes someone.

// ==== 190on P2: the command palette
#include "core/ActionSearch.h"
#include "core/KeyMap.h"

namespace research190on
{
inline std::string ids (const std::vector<lynx::actionsearch::Entry>& e, const std::string& query, const std::vector<std::string>& recent = {})
{
    std::string s;
    for (auto i : lynx::actionsearch::find (e, query, recent))
        s += e[(std::size_t) i].id;
    return s;
}

// "English" = "Translation" lines of a resources/i18n table
inline std::map<std::string, std::string> table (const std::string& lang)
{
    std::map<std::string, std::string> out;
    std::ifstream in ("resources/i18n/" + lang + ".txt");
    std::string line;
    const auto unquote = [] (const std::string& s, std::size_t& at) {
        std::string v;
        if (at >= s.size() || s[at] != '"')
            return v;
        for (++at; at < s.size() && s[at] != '"'; ++at)
        {
            if (s[at] == '\\' && at + 1 < s.size())
                ++at;
            v += s[at];
        }
        ++at;
        return v;
    };
    while (std::getline (in, line))
    {
        std::size_t at = 0;
        if (line.empty() || line[0] != '"')
            continue;
        const auto key = unquote (line, at);
        at = line.find ('"', at);
        if (at == std::string::npos)
            continue;
        out[key] = unquote (line, at);
    }
    return out;
}
} // namespace research190on

TEST (research190on_p2_palette_matcher_ranks_and_folds)
{
    using namespace lynx::actionsearch;
    using research190on::ids;
    std::vector<Entry> e {
        { "pref", "Preferences", "Preferences", "Options", "" },
        { "pr", "Piano roll", "Piano roll", "View", "F7" },
        { "met", "Toggle the metronome", "Toggle the metronome", "Everywhere", "Ctrl+M" },
        { "edit", "Edição de áudio", "Audio editing", "Ferramentas", "" },
        { "uber", "ÜBER das Projekt", "About the project", "Hilfe", "" },
        { "ru", "Микшер", "Mixer", "Окна", "F9" },
    };
    CHECK (ids (e, "pr").substr (0, 6) == "prpref"); // "pr" = Piano Roll's initials, before "Preferences" (a prefix)
    CHECK (ids (e, "pr") == "prprefuber");             // (then a word that starts with it: "Projekt")
    CHECK (ids (e, "edicao") == "edit");       // accents and case don't matter
    CHECK (ids (e, "EDIÇÃO") == "edit");
    CHECK (ids (e, "audio") == "edit");        // the English name too
    CHECK (ids (e, "uber") == "uber");
    CHECK (ids (e, "МИКШ") == "ru");            // Cyrillic capitals fold
    CHECK (ids (e, "mtrnm") == "met");          // the loose match: the letters in order from a word start
    {
        using lynx::actionsearch::fold;
        using lynx::actionsearch::looseMatch;
        CHECK (looseMatch (fold ("Song position to the next transient"), fold ("nxt trnsnt")));
        CHECK (! looseMatch (fold ("Move the playback marker to the start"), fold ("metro"))); // (letters scattered over words)
        CHECK (looseMatch (fold ("Metronome on/off"), fold ("mtrnm")));
        CHECK (! looseMatch (fold ("Metronome"), fold ("me"))); // (fewer than three letters)
    }
    CHECK (ids (e, "tm") == "met");             // initials of consecutive words: "the metronome"
    CHECK (ids (e, "zz").empty());
    CHECK (ids (e, "xq").empty());
    // empty query: everything in the list's order; recent ones first
    CHECK (ids (e, "") == "prefprmetedituberru");
    CHECK (ids (e, "", { "ru", "met" }) == "rumetprefpredituber");
    // within one rank the recent one first
    std::vector<Entry> two { { "a", "Metronome sound", "Metronome sound", "", "" }, { "b", "Metronome volume", "Metronome volume", "", "" } };
    CHECK (ids (two, "metro") == "ab");
    CHECK (ids (two, "metro", { "b" }) == "ba");
    // the recent list: latest first, once each, at most 20
    std::vector<std::string> recent;
    for (int i = 0; i < 25; ++i)
        noteRun (recent, "id" + std::to_string (i % 22));
    CHECK (recent.size() == 20 && recent.front() == "id2");
    noteRun (recent, "id10");
    CHECK (recent.front() == "id10" && std::count (recent.begin(), recent.end(), std::string ("id10")) == 1);
    // the old #228 expectations hold
    std::vector<Entry> old {
        { "a", "Toggle the metronome", "Toggle the metronome", "Everywhere", "Ctrl+M" },
        { "b", "Metronome sound", "Metronome sound", "Everywhere", "" },
        { "c", "Select the next mixer track", "Select the next mixer track", "Mixer", "Right" },
        { "d", "Metronome-Klang", "Metronome sound (de)", "Überall", "" },
    };
    CHECK (ids (old, "metro") == "bda");
    CHECK (ids (old, "mixer next") == "c");
    CHECK (ids (old, "ctrl+m") == "a");
    CHECK (ids (old, "?") == "abcd");
}

TEST (research190on_p2_palette_key_and_languages)
{
    using namespace lynx;
    using namespace lynx::keys;
    const auto* palette = findAction ("view.commandPalette");
    CHECK (palette != nullptr);
    if (palette != nullptr)
    {
        CHECK (palette->context == Context::global);
        CHECK (palette->defaults.size() == 1 && palette->defaults[0] == (Chord { 'Y', (std::uint8_t) (ctrl | shift) }));
    }
    KeyMap map;
    CHECK (map.conflicts().empty());
    for (const auto context : { Context::global, Context::channelRack, Context::pianoRoll, Context::playlist, Context::mixer, Context::browser })
        CHECK (map.find (Chord { 'Y', (std::uint8_t) (ctrl | shift) }, context, true) == "view.commandPalette"); // (typing piano on too)
    // FL 2026's Ctrl+Shift+P (Patterns › Open in project browser) and Ctrl+Shift+Space (play) are not the palette's
    CHECK (map.find (Chord { 'P', (std::uint8_t) (ctrl | shift) }, Context::global, false) != "view.commandPalette");
    CHECK (map.find (Chord { space, (std::uint8_t) (ctrl | shift) }, Context::global, false) != "view.commandPalette");
    CHECK (findAction ("help.searchActions") != nullptr && findAction ("help.searchActions")->defaults.empty()); // ("?" stays)

    // matching in the UI language and in English: pt-BR and de tables, without accents
    for (const auto& [lang, query] : std::vector<std::pair<std::string, std::string>> { { "pt-BR", "metronomo" }, { "de", "metronom" } })
    {
        const auto t = research190on::table (lang);
        std::vector<actionsearch::Entry> entries;
        for (const auto& a : actions())
        {
            const auto it = t.find (a.description);
            entries.push_back ({ a.id, it != t.end() ? it->second : std::string (a.description), a.description, "", "" });
        }
        const auto hits = actionsearch::find (entries, query);
        bool metronome = false;
        for (const auto i : hits)
            metronome = metronome || std::string (entries[(std::size_t) i].id).find ("etronome") != std::string::npos;
        CHECK (! hits.empty() && metronome);
        CHECK (! actionsearch::find (entries, "metronome").empty()); // (English)
    }
}

// ==== 190on P8: Tab to transient
#include "core/ClipWarp.h"
#include "core/TransientNav.h"

namespace research190on
{
// a drum loop: `hits` sharp noise bursts `period` seconds apart (the first at `first`), 44.1 kHz mono
inline lynx::SampleData drumLoop (int hits, double first, double period, double seconds)
{
    lynx::SampleData s;
    s.sampleRate = 44100.0;
    s.left.assign ((std::size_t) (seconds * s.sampleRate), 0.0f);
    std::uint32_t seed = 12345;
    for (int h = 0; h < hits; ++h)
    {
        const auto at = (std::size_t) std::llround ((first + h * period) * s.sampleRate);
        for (std::size_t i = 0; i < (std::size_t) (0.12 * s.sampleRate) && at + i < s.left.size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto noise = (float) ((double) (seed >> 8) / (double) (1u << 24) * 2.0 - 1.0);
            s.left[at + i] = 0.8f * noise * (float) std::exp (-(double) i / (0.02 * s.sampleRate));
        }
    }
    return s;
}
} // namespace research190on

TEST (research190on_p8_tab_to_transient_visits_a_drum_loops_hits)
{
    using namespace lynx;
    const double first = 0.05, period = 0.25;
    const auto sample = research190on::drumLoop (8, first, period, 2.1);
    const auto attacks = clipwarp::attacks (sample);
    CHECK (attacks.size() == 8);
    Song song;
    song.bpm = 120.0;
    ChannelDef ch;
    ch.instrument = InstrumentKind::audioClip;
    song.channels.push_back (ch);
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = 0;
    c.start = 4 * ppq; // bar 2
    c.length = 4 * ppq;
    song.playlist.push_back (c);
    const auto stops = transientnav::stops (song, { 0 }, [&] (int) { return attacks; });
    CHECK (stops.size() == 8);
    // 8 presses from the clip's start visit the 8 hits in order, each within 1 ms
    const auto ticksPerSecond = song.bpm / 60.0 * ppq;
    std::int64_t position = c.start;
    for (int k = 0; k < 8; ++k)
    {
        const auto next = transientnav::step (stops, position, 1);
        CHECK (next.has_value());
        if (! next)
            break;
        const auto seconds = (double) (*next - c.start) / ticksPerSecond;
        CHECK (std::abs (seconds - (first + k * period)) <= 0.001);
        position = *next;
    }
    CHECK (! transientnav::step (stops, position, 1)); // nothing after the last
    // and back
    const auto back = transientnav::step (stops, position, -1);
    CHECK (back && *back == stops[6]);
    CHECK (! transientnav::step (stops, stops.front(), -1));
    // Shift: the time selection grows to the next / previous stop
    using transientnav::Range;
    auto sel = transientnav::extend ({}, stops[2], stops, 1);
    CHECK (sel == (Range { stops[2], stops[3] }));
    sel = transientnav::extend (sel, stops[2], stops, 1);
    CHECK (sel == (Range { stops[2], stops[4] }));
    sel = transientnav::extend (sel, stops[2], stops, -1);
    CHECK (sel == (Range { stops[1], stops[4] }));
    CHECK (transientnav::extend (Range { stops[0], stops[7] }, 0, stops, 1) == (Range { stops[0], stops[7] })); // nothing beyond
    CHECK (transientnav::extend ({}, stops[3], stops, -1) == (Range { stops[2], stops[3] }));
    // a clip whose playing part starts after two hits (offset) only has the six later ones, shifted
    song.playlist[0].offset = (std::int64_t) std::llround (0.5 * ticksPerSecond);
    const auto later = transientnav::clipStops (song, song.playlist[0], attacks);
    CHECK (later.size() == 6);
    // a clip with no attack stops at its edges; a reversed one has no attacks
    const auto edges = transientnav::stops (song, { 0 }, [] (int) { return std::vector<double> {}; });
    CHECK (edges == (std::vector<std::int64_t> { c.start, c.start + c.length }));
    song.playlist[0].reversed = true;
    CHECK (transientnav::clipStops (song, song.playlist[0], attacks).empty());
    // the audio editor's frames: attacks, else markers and ends
    CHECK (transientnav::frameStops ({ 30, 10, 30 }, { 0, 100 }) == (std::vector<std::int64_t> { 10, 30 }));
    CHECK (transientnav::frameStops ({}, { 100, 0, 50 }) == (std::vector<std::int64_t> { 0, 50, 100 }));
    // the actions exist, unbound in every layout (Tab is FL's, Shift+Tab Live's, Alt+arrows the Playlist's nudge)
    for (const auto* id : { "playlist.nextTransient", "playlist.prevTransient", "playlist.extendToNextTransient", "playlist.extendToPrevTransient",
                            "audioEditor.nextTransient", "audioEditor.prevTransient", "audioEditor.extendToNextTransient", "audioEditor.extendToPrevTransient" })
    {
        const auto* a = keys::findAction (id);
        CHECK (a != nullptr && a->defaults.empty());
    }
}

// ==== 190on P10: stacked controller lanes
#include "core/LynxExtras.h"
#include "core/StackedLanes.h"

TEST (research190on_p10_stacked_lanes)
{
    using namespace lynx;
    using namespace lynx::stackedlanes;
    CHECK (! enabled (false, false)); // Pattern layout: a LYNX extra, off
    CHECK (enabled (false, true) && enabled (true, false)); // on, or the Clip-launch layout
    // heights: in proportion, at least minHeight, adding up
    auto h = heights ({ 1.0f, 1.0f, 1.0f, 1.0f }, 200, 0);
    CHECK (h == (std::vector<int> { 50, 50, 50, 50 }));
    h = heights ({ 2.0f, 1.0f, 1.0f }, 160, 0);
    CHECK (h[0] + h[1] + h[2] == 160 && h[0] > h[1] && h[1] >= minHeight);
    h = heights ({ 5.0f, 0.2f }, 100, 0);
    CHECK (h[1] >= minHeight && h[0] + h[1] == 100);
    h = heights ({ 1.0f, 1.0f, 1.0f }, 30, 0); // too little room: shared, never negative
    CHECK (h[0] >= 0 && h[0] + h[1] + h[2] == 30);
    // dragging the line between lanes 0 and 1 by 20 px
    std::vector<float> w { 1.0f, 1.0f, 1.0f };
    const auto before = heights (w, 300, 0);
    dragBoundary (w, 300, 0, 20);
    const auto after = heights (w, 300, 0);
    CHECK (std::abs (after[0] - (before[0] + 20)) <= 1 && std::abs (after[1] - (before[1] - 20)) <= 1 && after[2] == before[2]);
    dragBoundary (w, 300, 0, -1000); // never under minHeight
    CHECK (heights (w, 300, 0)[0] >= minHeight);
    // ui-options.txt round trip
    std::vector<Lane> lanes (3);
    lanes[0].property = 2;
    lanes[0].weight = 1.5f;
    lanes[1].target = AutomationTarget { AutomationTargetKind::mixerSend, 1, 2, 3 };
    lanes[2].property = 5;
    const auto text = toText (lanes, 0.75f);
    float bottom = 0.0f;
    const auto back = fromText (text, &bottom);
    CHECK (back == lanes && std::abs (bottom - 0.75f) < 1e-6f);
    CHECK (fromText ("p99:1 x:2 junk p3:1").size() == 1);
    CHECK (fromText ("p1:1 p2:1 p3:1 p4:1 p5:1 p6:1").size() == (std::size_t) maxAdded);
    // listed as a LYNX extra
    bool listed = false;
    for (const auto& item : extras::items)
        listed = listed || (item.id == "pianoRoll.stackedLanes" && item.settingsKey == "ui-options.txt:prStackedLanesExtra");
    CHECK (listed);
    CHECK (keys::findAction ("pianoRoll.stackedLanes") != nullptr && keys::findAction ("pianoRoll.stackedLanes")->defaults.empty());
}

// ==== 190on Online: removing a guest
#include "core/LiveCrypto.h"
#include "core/LiveRekey.h"

TEST (research190on_online_remove_guest_new_code_for_new_joins)
{
    using namespace lynx;
    livecrypto::Invite now { "abcdefghij", livecrypto::newSecret() };
    const auto next = liverekey::rotated (now);
    CHECK (next.room == now.room && next.secret != now.secret);
    CHECK (livecrypto::parseCode (livecrypto::formatCode (next)).has_value());
    const auto oldKeys = livecrypto::derive (now), newKeys = livecrypto::derive (next);
    CHECK (oldKeys.relayToken != newKeys.relayToken && oldKeys.key != newKeys.key);
    // the relay takes the new token (43..128 url-safe characters)
    CHECK (newKeys.relayToken.size() >= 43 && newKeys.relayToken.find_first_not_of ("0123456789abcdef") == std::string::npos);
    CHECK (liverekey::kickBody (2, true, newKeys.relayToken) == "{\"peer\":2,\"ban\":true,\"token\":\"" + newKeys.relayToken + "\"}");
    CHECK (liverekey::kickBody (3, true, {}) == "{\"peer\":3,\"ban\":true}");
    CHECK (liverekey::kickBody (-1, false, {}) == "{\"peer\":0,\"ban\":false}");
    // the switch as LiveSessionManager does it: a guest still in the session opens what was sealed with the old key on
    // the way (its previous opener) and what comes with the new one; the removed guest's old key opens nothing new
    livecrypto::Sealer oldSealer (oldKeys.key), newSealer (newKeys.key);
    livecrypto::Opener removedGuest (oldKeys.key);
    livecrypto::Opener previous (oldKeys.key), current (newKeys.key);
    const std::vector<std::uint8_t> hello { 'h', 'i' };
    const auto inFlight = oldSealer.seal (hello);
    const auto after = newSealer.seal (hello);
    auto open = [&] (const std::vector<std::uint8_t>& frame) {
        auto plain = current.open (frame);
        return plain ? plain : previous.open (frame);
    };
    CHECK (open (inFlight).has_value());
    CHECK (open (after).has_value());
    CHECK (! removedGuest.open (after).has_value());
    CHECK (! open (inFlight).has_value()); // (a replay is still refused)
}
