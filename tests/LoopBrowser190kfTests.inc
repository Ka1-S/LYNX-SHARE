// Round gaps190kf (item 3): the Loop Browser's keywords and filters (core/LoopKeywords.h) — GarageBand's keyword
// buttons (several picked = all of them), the Scale menu (Any / Minor / Major / Neither / Good for Both), Loop Types,
// favourites, keys near the project key, LYNX's tempo filter and the keyword layout (swap / place / reset).
// Evidence: /mnt/e/lynx-work/gaps190kf/loops/NOTES.md.

#include "core/LoopKeywords.h"

TEST (loops190kf_words_split_folders_names_camel_and_digits)
{
    using namespace lynx::loopkeys;
    CHECK ((words ("/lib/Drum Loops/DarkPad90_Am.wav") == std::vector<std::string> { "lib", "drum", "loops", "dark", "pad", "90", "am" }));
    CHECK ((words ("C:\\Packs\\808s\\Hi-Hat 01.WAV") == std::vector<std::string> { "c", "packs", "808s", "hi", "hat", "01" }));
    CHECK (words ("").empty());
}

TEST (loops190kf_keywords_from_names_folders_and_implied)
{
    using namespace lynx::loopkeys;
    // a kick in a Kicks folder: Kick and the family it implies (All Drums)
    auto k = describe ("/s/Drums/Kicks/Kick Hard 01.wav", "Drums/Kicks/Kick Hard 01.wav");
    CHECK (k.keywords.count ("kick") == 1);
    CHECK (k.keywords.count ("all drums") == 1);
    CHECK (k.keywords.count ("intense") == 1); // ("hard")
    CHECK (k.type == audio);
    CHECK (k.scale == Scale::neither); // drums have no scale
    // "bass drum" is a kick, not a bass
    k = describe ("/s/Bass Drum 2.wav", "Bass Drum 2.wav");
    CHECK (k.keywords.count ("kick") == 1);
    CHECK (k.keywords.count ("bass") == 0);
    // hi-hat written three ways
    for (const auto* n : { "Hi-Hat open.wav", "HiHat.wav", "closed hat 3.wav" })
        CHECK (describe (std::string ("/s/") + n, n).keywords.count ("hi-hat") == 1);
    // plurals and camelCase; a synth bass is a bass too
    k = describe ("/s/SynthBass Reese 140bpm Fm.wav", "SynthBass Reese 140bpm Fm.wav");
    CHECK (k.keywords.count ("synth bass") == 1);
    CHECK (k.keywords.count ("bass") == 1);
    CHECK (k.tempo == 140.0);
    CHECK (k.keyRoot == 5);
    CHECK (k.scale == Scale::minor);
    // the user's own folder names above the place don't count (wordsOf), the place's do
    k = describe ("/home/rock/Music/Pads/Warm Pad.wav", "Pads/Warm Pad.wav");
    CHECK (k.keywords.count ("rock") == 0);
    CHECK (k.keywords.count ("pads") == 1);
    CHECK (k.keywords.count ("synths") == 1); // (a pad implies Synths)
}

TEST (loops190kf_tags_and_factory_keywords)
{
    using namespace lynx::loopkeys;
    // a user tag that names a keyword (by id or label, any case) gives it; other tags don't make keywords
    auto k = describe ("/s/take 7.wav", "take 7.wav", { "Dark", "my best", "ELEC PIANO" }, { "lo-fi" });
    CHECK (k.keywords.count ("dark") == 1);
    CHECK (k.keywords.count ("elec piano") == 1);
    CHECK (k.keywords.count ("lo-fi") == 1);
    CHECK (k.keywords.size() == 3);
    // a factory keyword's implied ones come too
    k = describe ("/f/a.mid", "a.mid", {}, { "kick" });
    CHECK (k.keywords.count ("all drums") == 1);
    CHECK (k.type == midi);
    CHECK (find ("Hi-Hat") != nullptr);
    CHECK (find ("nonsense") == nullptr);
}

TEST (loops190kf_loop_types_by_extension)
{
    using namespace lynx::loopkeys;
    CHECK (typeOf ("a.wav") == audio);
    CHECK (typeOf ("a.AIFF") == audio);
    CHECK (typeOf ("a.flac") == audio);
    CHECK (typeOf ("a.mid") == midi);
    CHECK (typeOf ("a.fsc") == pattern);
    CHECK (typeOf ("a.fst") == 0);
    CHECK (typeOf ("noext") == 0);
}

TEST (loops190kf_scale_from_the_name)
{
    using namespace lynx::loopkeys;
    CHECK (describe ("/s/Keys 120 Am.wav").scale == Scale::minor);
    CHECK (describe ("/s/Keys 120 Cmaj.wav").scale == Scale::major);
    CHECK (describe ("/s/Keys F# major.wav").scale == Scale::major);
    // a root alone (loop packs' "Bass_120_G"): good for both
    const auto g = describe ("/s/Bass_120_G.wav");
    CHECK (g.scale == Scale::both);
    CHECK (g.keyRoot == 7);
    CHECK (keyText (g) == "G");
    CHECK (keyText (describe ("/s/Keys 120 Am.wav")) == "Am");
    // a tonal loop with no key: unknown (shown under Any only)
    CHECK (describe ("/s/Piano Ballad.wav").scale == Scale::any);
}

TEST (loops190kf_tempo_from_the_name)
{
    using namespace lynx::loopkeys;
    CHECK (describe ("/s/Beat 98 bpm.wav").tempo == 98.0);
    CHECK (describe ("/s/Drum Loop_128.wav").tempo == 128.0); // (a bare number in a loop's name)
    CHECK (describe ("/s/Snare 100.wav").tempo == 0.0);       // (a bare number in a one-shot's name is not a tempo)
    CHECK (tempoText (describe ("/s/Beat 98 bpm.wav")) == "98");
    CHECK (tempoText (describe ("/s/Snare 100.wav")).empty());
}

TEST (loops190kf_keyword_buttons_narrow_to_all_picked)
{
    using namespace lynx::loopkeys;
    // GarageBand: "Clicking multiple buttons narrows the results to those loops that match all of the selected keywords"
    const std::vector<LoopInfo> loops { describe ("/s/Dark Piano 90 Am.wav", "Dark Piano 90 Am.wav"), describe ("/s/Piano Happy 120 C maj.wav", "Piano Happy 120 C maj.wav"),
                                        describe ("/s/Dark Pad Loop 70 Dm.wav", "Dark Pad Loop 70 Dm.wav"), describe ("/s/Kick 1.wav", "Kick 1.wav"),
                                        describe ("/s/Trap Beat 140 bpm.wav", "Trap Beat 140 bpm.wav"), describe ("/s/Lead Arp.mid", "Lead Arp.mid") };
    const auto count = [&loops] (const Filter& f, std::vector<bool> fav = {}) {
        int n = 0;
        for (std::size_t i = 0; i < loops.size(); ++i)
            n += matches (loops[i], f, i < fav.size() && fav[i]) ? 1 : 0;
        return n;
    };
    Filter f;
    CHECK (! f.active());
    CHECK (count (f) == 6);
    f.keywords = { "piano" };
    CHECK (count (f) == 2);
    f.keywords = { "piano", "dark" };
    CHECK (count (f) == 1);
    f.keywords = { "dark" };
    CHECK (count (f) == 2);
    // the counts behind the buttons: with Dark picked, Piano still finds 1, Kick none (dimmed)
    const auto counts = keywordCounts (loops, f, [] (std::size_t) { return false; });
    CHECK (counts.at ("piano") == 1);
    CHECK (counts.at ("kick") == 0);
    CHECK (counts.at ("dark") == 2);
    // the Scale menu
    f = {};
    f.scale = Scale::minor;
    CHECK (count (f) == 2);
    f.scale = Scale::major;
    CHECK (count (f) == 1);
    f.scale = Scale::neither;
    CHECK (count (f) == 2); // (the kick and the trap beat)
    // Loop Types: MIDI only
    f = {};
    f.types = midi;
    CHECK (count (f) == 1);
    f.types = audio | pattern;
    CHECK (count (f) == 5);
    // favourites only (GarageBand's Favorites button)
    f = {};
    f.favouritesOnly = true;
    CHECK (count (f, { false, true, false, false, true }) == 2);
    // the search text (the Browser's own query language) on the name
    f = {};
    f.text = "dark NOT pad";
    CHECK (count (f) == 1);
}

TEST (loops190kf_tempo_and_near_key_filters)
{
    using namespace lynx::loopkeys;
    const std::vector<LoopInfo> loops { describe ("/s/A 90 bpm Am.wav"), describe ("/s/B 120 bpm Em.wav"), describe ("/s/C 128 bpm.wav"), describe ("/s/Kick.wav") };
    Filter f;
    const auto [lo, hi] = tempoBounds (1, 125.0); // near the project tempo (±6 %)
    f.tempoMin = lo;
    f.tempoMax = hi;
    CHECK (matches (loops[1], f, false));
    CHECK (matches (loops[2], f, false));
    CHECK (! matches (loops[0], f, false));
    CHECK (! matches (loops[3], f, false)); // (no tempo known)
    CHECK ((tempoBounds (0, 120.0) == std::pair<double, double> { 0.0, 0.0 }));
    CHECK ((tempoBounds (3, 120.0) == std::pair<double, double> { 90.0, 110.0 }));
    // GarageBand's "within two semitones of the project key": project in G (7): Am (9) yes, Em (4) no; no key stays
    f = {};
    f.nearKey = 7;
    CHECK (matches (loops[0], f, false));
    CHECK (! matches (loops[1], f, false));
    CHECK (matches (loops[3], f, false));
    CHECK (rootDistance (11, 1) == 2);
    CHECK (rootDistance (0, 6) == 6);
}

TEST (loops190kf_vocabulary_grid_and_layout)
{
    using namespace lynx::loopkeys;
    // three categories; ids unique; the instrument grid starts with GarageBand's first row's families
    std::set<std::string> ids;
    for (const auto& k : vocabulary())
        CHECK (ids.insert (k.id).second);
    const auto inst = ofCategory (Category::instrument);
    CHECK (inst.size() == 48);
    CHECK (std::string (inst[0]->id) == "all drums");
    CHECK (std::string (inst[1]->id) == "piano");
    CHECK (std::string (inst[2]->id) == "guitars");
    CHECK (std::string (inst[3]->id) == "brass");
    CHECK (ofCategory (Category::genre).size() == 20);
    CHECK (ofCategory (Category::descriptor).size() == 20);
    // every implied keyword exists
    for (const auto& k : vocabulary())
        for (const auto* a : k.also)
            CHECK (find (a) != nullptr);
    // the layout: swap two buttons, put another keyword on a place, save / load, reset
    auto l = Layout::defaults();
    CHECK (l.swap (Category::instrument, 0, 1));
    CHECK (l.order[0][0] == "piano");
    CHECK (l.place (Category::genre, 0, "jazz")); // (jazz was shown: the two swap)
    CHECK (l.order[1][0] == "jazz");
    CHECK (! l.swap (Category::genre, 0, 99));
    const auto back = Layout::fromText (l.toText());
    CHECK (back == l);
    CHECK (Layout::fromText ("instrument=nonsense,piano\n").order[0].size() == 48);
    CHECK (Layout::fromText ("instrument=nonsense,piano\n").order[0][0] == "piano");
    CHECK (Layout::fromText ("") == Layout::defaults());
}

TEST (loops190kf_factory_source_hook)
{
    using namespace lynx::loopkeys;
    // round 190ke's factory tags plug in here (a thread-safe lookup by path); none by default
    CHECK (factoryKeywords ("/x/a.wav").empty());
    setFactorySource ([] (const std::string& path) { return path == "/lynx/Starter Beat.wav" ? std::vector<std::string> { "hip hop", "beats" } : std::vector<std::string> {}; });
    const auto k = describe ("/lynx/Starter Beat.wav", "Starter Beat.wav", {}, factoryKeywords ("/lynx/Starter Beat.wav"));
    CHECK (k.keywords.count ("hip hop") == 1);
    CHECK (k.keywords.count ("all drums") == 1); // (beats implies All Drums)
    CHECK (factoryKeywords ("/x/a.wav").empty());
    setFactorySource (nullptr);
    CHECK (factoryKeywords ("/lynx/Starter Beat.wav").empty());
}
