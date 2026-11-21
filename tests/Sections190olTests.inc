// Round 190ol (0.1.31, research190oe P1 and P6): the Sections lane (core/Sections.h, core/SectionOps.h) — named song
// sections, moved / copied / duplicated / cleared / removed / resized / replaced / swapped with everything under them
// (clips of every track, lane automation, markers, the time signature); sections -> markers for the .flp / .als /
// DAWproject exports, scenes and export files from sections — and the Simple layout's time signature changes
// (core/SignatureEdit.h).

#include "core/AlsExport.h"
#include "core/DawProject.h"
#include "core/ExportOptions.h"
#include "core/FlpExport.h"
#include "core/SectionOps.h"
#include "core/Sections.h"
#include "core/SignatureEdit.h"
#include "flp/Flp.h"

namespace sections190ol
{
constexpr std::int64_t bar = 4 * (std::int64_t) lynx::ppq;

inline lynx::Clip clip (int track, int pattern, std::int64_t start, std::int64_t length, std::int64_t offset = 0)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = pattern;
    c.track = track;
    c.start = start;
    c.length = length;
    c.offset = offset;
    return c;
}

// Three sections of two bars, A B C. Track 0: one clip per section (patterns 0, 1, 2); track 1: one long clip over all
// three; track 0's lane: a different level in each section; a plain marker inside B.
inline lynx::Song threeSections()
{
    using namespace lynx;
    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer (2);
    song.playlistTracks.resize (2);
    song.patterns.resize (3);
    for (int i = 0; i < 3; ++i)
        song.patterns[(std::size_t) i].name = "P" + std::to_string (i);
    song.playlist = { clip (0, 0, 0, 2 * bar), clip (1, 0, 0, 6 * bar), clip (0, 1, 2 * bar, 2 * bar), clip (0, 2, 4 * bar, 2 * bar) };
    Automation a;
    a.name = "Vol";
    a.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
    a.points = { { 0, 0.1f, 0.0f }, { 2 * bar - 1, 0.1f, 0.0f }, { 2 * bar, 0.5f, 0.0f }, { 4 * bar - 1, 0.5f, 0.0f }, { 4 * bar, 0.9f, 0.0f },
                 { 6 * bar, 0.9f, 0.0f } };
    song.automations.push_back (a);
    song.playlistTracks[0].lanes.push_back ({ 0, 0 });
    song.addMarker ({ 3 * bar, "Hook", 0, 4 });
    song.sections() = { { 0, 2 * bar, "A", 0, 1 }, { 2 * bar, 2 * bar, "B", 0, 2 }, { 4 * bar, 2 * bar, "C", 0, 3 } };
    return song;
}

inline const lynx::Clip* at (const lynx::Song& s, int track, std::int64_t start)
{
    for (const auto& c : s.playlist)
        if (c.track == track && c.start == start)
            return &c;
    return nullptr;
}

inline std::string names (const lynx::Song& s)
{
    std::string out;
    for (const auto& x : s.sections())
        out += x.name;
    return out;
}

inline bool contiguous (const lynx::Song& s)
{
    std::int64_t next = 0;
    for (const auto& x : s.sections())
    {
        if (x.start != next)
            return false;
        next = x.end();
    }
    return true;
}
} // namespace sections190ol

TEST (sections190ol_model_add_names_and_time)
{
    using namespace lynx;
    using sections190ol::bar;
    Song song;
    song.bpm = 120.0;
    // GarageBand's "+": eight bars at the end of the last one, Intro, Verse, Chorus, Bridge, Outro, then "Section 6"
    for (int i = 0; i < 6; ++i)
        CHECK (sections::add (song) == i);
    CHECK (sections190ol::names (song) == "IntroVerseChorusBridgeOutroSection 6");
    CHECK (song.sections()[1].start == 8 * bar && song.sections()[5].end() == 48 * bar && sections190ol::contiguous (song));
    CHECK (sections::indexAt (song.sections(), 8 * bar) == 1 && sections::indexAt (song.sections(), 8 * bar - 1) == 0
           && sections::indexAt (song.sections(), 48 * bar) == -1);
    // the automatic colours by place; an own colour wins
    CHECK (sections::colourOf (song.sections(), 0) == sections::palette[0] && sections::colourOf (song.sections(), 9) == sections::palette[1]);
    song.sections()[2].colour = 0xFF112233u;
    CHECK (sections::colourOf (song.sections(), 2) == 0xFF112233u);
    // a section over a free range only (snapped to bars)
    Song free;
    free.bpm = 120.0;
    CHECK (sections::addRange (free, bar + 10, 3 * bar - 10, "X") == 0 && free.sections()[0].start == bar && free.sections()[0].length == 2 * bar);
    CHECK (sections::addRange (free, 2 * bar, 4 * bar) == -1);
    // the time commands keep them in step: inserted time grows the one it falls in and pushes the later ones
    auto list = song.sections();
    sections::insertTime (list, 4 * bar, bar);
    CHECK (list[0].length == 9 * bar && list[1].start == 9 * bar && list[5].end() == 49 * bar);
    sections::deleteTime (list, 0, 9 * bar); // (the first one goes entirely)
    CHECK (list.size() == 5 && list[0].name == "Verse" && list[0].start == 0);
    sections::deleteTime (list, 4 * bar, 12 * bar); // (half of Verse, half of Chorus)
    CHECK (list.size() == 5 && list[0].length == 4 * bar && list[1].start == 4 * bar && list[1].length == 4 * bar && list[2].start == 8 * bar);
    // normalize: sorted, overlaps cut, uids unique
    std::vector<SongSection> messy { { 8 * bar, 4 * bar, "b", 0, 7 }, { 0, 10 * bar, "a", 0, 7 }, { 20 * bar, 0, "empty", 0, 0 } };
    sections::normalize (messy);
    CHECK (messy.size() == 2 && messy[0].name == "a" && messy[0].length == 8 * bar && messy[0].uid != messy[1].uid);
    // arrange::pasteTime / deleteTime carry the lane along
    auto s = sections190ol::threeSections();
    const auto piece = arrange::cutTime (s, 2 * bar, 4 * bar);
    CHECK (sections190ol::names (s) == "AC" && s.sections()[1].start == 2 * bar);
    arrange::pasteTime (s, 0, piece);
    CHECK (s.sections()[0].name == "A" && s.sections()[0].start == 2 * bar && s.sections()[1].start == 4 * bar);
}

TEST (sections190ol_move_swaps_clips_lanes_and_markers_tick_exact)
{
    using namespace lynx;
    using namespace sections190ol;
    const auto original = threeSections();
    auto song = original;
    CHECK (! sections::move (song, 1, 1) && ! sections::move (song, 1, 2) && song.playlist == original.playlist && song.sections() == original.sections()
           && song.markers() == original.markers() && song.automations == original.automations);
    // B before A: B's clip, its lane level and its marker move to the front, A follows, C stays; the long clip on track 1
    // is cut at the sections' edges and each piece plays what it played before
    CHECK (sections::move (song, 1, 0));
    CHECK (names (song) == "BAC" && contiguous (song) && song.sections()[0].uid == 2 && song.sections()[1].uid == 1);
    CHECK (at (song, 0, 0) != nullptr && at (song, 0, 0)->pattern == 1 && at (song, 0, 0)->length == 2 * bar);
    CHECK (at (song, 0, 2 * bar) != nullptr && at (song, 0, 2 * bar)->pattern == 0);
    CHECK (at (song, 0, 4 * bar) != nullptr && at (song, 0, 4 * bar)->pattern == 2);
    CHECK (at (song, 1, 0) != nullptr && at (song, 1, 0)->offset == 2 * bar && at (song, 1, 0)->length == 2 * bar);
    CHECK (at (song, 1, 2 * bar) != nullptr && at (song, 1, 2 * bar)->offset == 0 && at (song, 1, 2 * bar)->length == 2 * bar);
    CHECK (at (song, 1, 4 * bar) != nullptr && at (song, 1, 4 * bar)->offset == 4 * bar);
    CHECK (song.playlist.size() == 6 && song.songLength() == original.songLength());
    CHECK (song.markers().size() == 1 && song.markers()[0].tick == bar && song.markers()[0].name == "Hook");
    const auto& lane = song.automations[0];
    for (const auto t : { (std::int64_t) 1, bar, 2 * bar - 2 })
        CHECK (std::abs (lane.valueAt ((double) t) - 0.5f) < 1e-6);                     // B's level
    for (const auto t : { 2 * bar + 1, 3 * bar, 4 * bar - 2 })
        CHECK (std::abs (lane.valueAt ((double) t) - 0.1f) < 1e-6);                     // A's
    CHECK (std::abs (lane.valueAt ((double) (5 * bar)) - 0.9f) < 1e-6);                 // C's
    // C to the front, then back after B: the order follows, the length never changes
    CHECK (sections::move (song, 2, 0) && names (song) == "CBA" && contiguous (song));
    CHECK (sections::move (song, 0, 3) && names (song) == "BAC" && contiguous (song));
    CHECK (at (song, 0, 4 * bar) != nullptr && at (song, 0, 4 * bar)->pattern == 2 && std::abs (song.automations[0].valueAt ((double) (5 * bar)) - 0.9f) < 1e-6);
    CHECK (song.songLength() == original.songLength());
    // undo is the engine's snapshot of the whole Song (one editSong): the --selftest sections190ol checks it bit for bit
}

TEST (sections190ol_copy_duplicate_remove_clear_resize)
{
    using namespace lynx;
    using namespace sections190ol;
    const auto original = threeSections();
    // copy A before C: the song grows by A's length, the copy has A's clips and level; the marker in B stays single
    auto song = original;
    CHECK (sections::copy (song, 0, 2) == 2);
    CHECK (names (song) == "ABAC" && contiguous (song) && song.songLength() == original.songLength() + 2 * bar);
    CHECK (at (song, 0, 4 * bar) != nullptr && at (song, 0, 4 * bar)->pattern == 0 && at (song, 0, 6 * bar)->pattern == 2);
    CHECK (at (song, 1, 4 * bar) != nullptr && at (song, 1, 4 * bar)->offset == 0 && at (song, 1, 6 * bar)->offset == 4 * bar);
    CHECK (std::abs (song.automations[0].valueAt ((double) (5 * bar)) - 0.1f) < 1e-6 && std::abs (song.automations[0].valueAt ((double) (7 * bar)) - 0.9f) < 1e-6);
    CHECK (song.sections()[2].uid != song.sections()[0].uid);
    // duplicate B: right after itself, its plain marker repeated with it (Live's Duplicate Time repeats locators too)
    song = original;
    CHECK (sections::duplicate (song, 1) == 2 && names (song) == "ABBC" && song.songLength() == original.songLength() + 2 * bar);
    CHECK (song.markers().size() == 2 && song.markers()[1].tick == 5 * bar && song.markers()[1].name == "Hook");
    CHECK (at (song, 0, 4 * bar) != nullptr && at (song, 0, 4 * bar)->pattern == 1 && at (song, 0, 6 * bar)->pattern == 2);
    // delete and close the gap: the song shrinks by B, C moves up, B's marker goes
    song = original;
    CHECK (sections::remove (song, 1, true) && names (song) == "AC" && contiguous (song) && song.songLength() == original.songLength() - 2 * bar);
    CHECK (song.markers().empty() && at (song, 0, 2 * bar) != nullptr && at (song, 0, 2 * bar)->pattern == 2);
    // delete the section only: the content stays where it is
    song = original;
    CHECK (sections::remove (song, 1, false) && names (song) == "AC" && song.playlist == original.playlist && song.markers() == original.markers());
    // GarageBand's first Delete: the clips under B go (the long clip is cut around it), the time and the section stay
    song = original;
    CHECK (sections::clipsIn (song, 1).size() == 2 && sections::clear (song, 1) && names (song) == "ABC");
    CHECK (at (song, 0, 2 * bar) == nullptr && at (song, 1, 0) != nullptr && at (song, 1, 0)->length == 2 * bar);
    CHECK (at (song, 1, 4 * bar) != nullptr && at (song, 1, 4 * bar)->offset == 4 * bar && song.songLength() == original.songLength());
    CHECK (sections::clipsIn (song, 1).empty() && ! sections::clear (song, 1));
    // the right edge: never into the next section, at least one bar, the content stays
    song = original;
    CHECK (! sections::resize (song, 0, 3 * bar));
    CHECK (sections::resize (song, 2, 9 * bar) && song.sections()[2].length == 5 * bar && song.playlist == original.playlist);
    CHECK (sections::resize (song, 2, 4 * bar) && song.sections()[2].length == bar);
}

TEST (sections190ol_replace_swap_and_meters)
{
    using namespace lynx;
    using namespace sections190ol;
    const auto original = threeSections();
    // Command-drag A onto C: C holds A's content and keeps its name
    auto song = original;
    CHECK (sections::replace (song, 0, 2) && names (song) == "ABC" && contiguous (song));
    CHECK (at (song, 0, 4 * bar) != nullptr && at (song, 0, 4 * bar)->pattern == 0 && at (song, 1, 4 * bar)->offset == 0);
    CHECK (std::abs (song.automations[0].valueAt ((double) (5 * bar)) - 0.1f) < 1e-6);
    // the drop onto another section: the two swap places
    song = original;
    CHECK (sections::swap (song, 0, 2) && names (song) == "CBA" && contiguous (song));
    CHECK (at (song, 0, 0)->pattern == 2 && at (song, 0, 2 * bar)->pattern == 1 && at (song, 0, 4 * bar)->pattern == 0);
    CHECK (song.markers().size() == 1 && song.markers()[0].tick == 3 * bar);
    song = original;
    CHECK (sections::swap (song, 1, 0) && names (song) == "BAC");

    // a 7/8 section keeps its meter when it moves to the front, and the 4/4 section after it keeps 4/4
    Song meter;
    meter.bpm = 120.0;
    meter.patterns.resize (1);
    const auto seven = 7 * (std::int64_t) ppq / 2; // a 7/8 bar
    meter.addMarker ({ 8 * (std::int64_t) ppq, "7/8", 7, 8 });
    meter.addMarker ({ 8 * (std::int64_t) ppq + 2 * seven, "4/4", 4, 4 });
    meter.sections() = { { 0, 2 * bar, "A", 0, 1 }, { 2 * bar, 2 * seven, "B", 0, 2 }, { 2 * bar + 2 * seven, 2 * bar, "C", 0, 3 } };
    CHECK (sections::move (meter, 1, 0) && names (meter) == "BAC");
    const auto bars = meter.barsBetween (0, 2 * seven + 4 * bar);
    CHECK (bars.size() == 6 && bars[0].length == seven && bars[1].length == seven && bars[2].tick == 2 * seven && bars[2].length == bar
           && bars[5].length == bar && bars[5].number == 6);
    CHECK (std::none_of (meter.markers().begin(), meter.markers().end(), [] (const Marker& m) { return m.name.find ('\x01') != std::string::npos; }));
    // and back: no change left behind that changes nothing
    CHECK (sections::move (meter, 0, 2) && names (meter) == "ABC");
    const auto back = meter.barsBetween (0, 4 * bar + 2 * seven);
    CHECK (back.size() == 6 && back[0].length == bar && back[2].length == seven && back[4].length == bar);
    CHECK (std::count_if (meter.markers().begin(), meter.markers().end(), [] (const Marker& m) { return m.isSignature() && m.tick == 0; }) == 0);
}

TEST (sections190ol_markers_exports_and_scenes)
{
    using namespace lynx;
    using namespace sections190ol;
    auto song = threeSections();
    // sections -> markers (once per name and place) and markers -> sections
    const auto withSections = sections::markersWithSections (song.markers(), song.sections());
    CHECK (withSections.size() == 4 && withSections[0].name == "A" && withSections[2].name == "Hook" && withSections[3].tick == 4 * bar);
    auto marked = song;
    CHECK (sections::toMarkers (marked) == 3 && sections::toMarkers (marked) == 0 && marked.markers().size() == 4);
    marked.sections().clear();
    CHECK (sections::fromMarkers (marked) == 4 && marked.sections().size() == 4 && marked.sections()[1].name == "B"
           && marked.sections()[2].name == "Hook" && marked.sections()[2].length == bar);

    // .flp: FL has no Sections lane — plain time markers at the sections' starts, said in the report
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (! project.arrangements.empty());
    if (! project.arrangements.empty())
    {
        const auto& m = project.arrangements.front().markers;
        CHECK (m.size() == 4 && std::count_if (m.begin(), m.end(), [] (const flp::TimeMarker& x) { return x.name == "B" && x.type == 0; }) == 1);
    }
    CHECK (std::any_of (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.find ("Sections lane") != std::string::npos; }));
    // a project without sections writes what it always wrote
    auto plain = song;
    plain.sections().clear();
    flpexport::Report plainReport;
    const auto plainProject = flp::parseProject (flpexport::exportFlp (plain, options, plainReport));
    CHECK (! plainProject.arrangements.empty() && plainProject.arrangements.front().markers.size() == 1);
    // .als: locators; DAWproject: markers
    const auto als = exportAls (song);
    CHECK (als.xml.find ("<Name Value=\"B\"") != std::string::npos && als.xml.find ("<Name Value=\"Hook\"") != std::string::npos);
    const auto daw = exportDawProject (song);
    CHECK (daw.projectXml.find ("name=\"C\"") != std::string::npos);

    // export: one file per section (the lane's), named after them
    exportopts::Options o;
    o.songSections = true;
    const auto list = exportopts::exportSections (song, o);
    CHECK (list.size() == 3 && list[1].name == "B" && list[1].start == 2 * bar && list[1].end == 4 * bar);
    CHECK (exportopts::exportsSections (song, o) && exportopts::fileCount (song, o) == 3);
    o.range = exportopts::Range::selection;
    CHECK (! exportopts::exportsSections (song, o));
    exportopts::Options markersOnly;
    markersOnly.sections = true;
    CHECK (exportopts::exportSections (song, markersOnly).size() == 2); // (the marker split: Start, Hook)
    CHECK (exportopts::fromText (exportopts::toText (o)).songSections);

    // Clip-launch: 3 sections -> 3 scenes named after them, each track's clip of the section in its slot
    auto grid = threeSections();
    CHECK (sections::createScenes (grid) == 0 && grid.clipGrid.has_value());
    if (grid.clipGrid)
    {
        CHECK (grid.clipGrid->scenes.size() >= 3 && grid.clipGrid->scenes[1].name == "B" && grid.clipGrid->scenes[2].colour == sections::palette[2]);
        const auto* b0 = launcher::clipAt (*grid.clipGrid, 0, 1);
        const auto* b1 = launcher::clipAt (*grid.clipGrid, 1, 1);
        CHECK (b0 != nullptr && b0->pattern == 1 && b0->length == 2 * bar);
        CHECK (b1 != nullptr && b1->pattern == 0 && b1->offset == 2 * bar && b1->length == 2 * bar);
        // a second run adds after the scenes in use
        CHECK (sections::createScenes (grid) == 3 && grid.clipGrid->scenes[5].name == "C");
    }
    Song none;
    CHECK (sections::createScenes (none) == -1 && ! none.clipGrid.has_value());
}

TEST (sections190ol_simple_time_signature_changes)
{
    using namespace lynx;
    using sections190ol::bar;
    // 4/4 -> 7/8 at bar 3 -> 4/4 at bar 5: the bars, their numbers and lengths follow
    Song song;
    song.bpm = 120.0;
    const auto seven = 7 * (std::int64_t) ppq / 2;
    CHECK (signatureedit::set (song, 2 * bar + 100, 7, 8) == 2 * bar); // (on the bar under the pointer)
    CHECK (signatureedit::set (song, 2 * bar + 2 * seven, 4, 4) == 2 * bar + 2 * seven);
    const auto bars = song.barsBetween (0, 2 * bar + 2 * seven + 2 * bar);
    CHECK (bars.size() == 6 && bars[2].tick == 2 * bar && bars[2].length == seven && bars[3].length == seven && bars[4].number == 5 && bars[4].length == bar);
    CHECK (song.markers().size() == 2 && song.markers()[0].name == "7/8" && song.markers()[1].numerator == 4);
    // the top bar's box: the start's signature is the song's meter; a change that repeats what is in force is not kept
    CHECK (signatureedit::atStart (song) == std::pair (4, 4) && signatureedit::set (song, 0, 3, 4) == 0 && song.beatsPerBar == 3 && song.markers().size() == 2);
    signatureedit::set (song, 0, 4, 4);
    CHECK (signatureedit::set (song, 5 * bar, 4, 4) > 0 && song.markers().size() == 2); // (4/4 already: nothing added)
    // a new change at a bar replaces the one there; Delete takes it away
    signatureedit::set (song, 2 * bar, 5, 4);
    CHECK (song.markers().size() == 2 && song.markers()[0].numerator == 5 && signatureedit::changeAt (song, 2 * bar) == 0);
    CHECK (signatureedit::remove (song, 2 * bar) && ! signatureedit::remove (song, 2 * bar) && song.markers().size() == 1);
    // an FL project's change at tick 0 is what the start box edits
    Song fl;
    fl.addMarker ({ 0, "6/8", 6, 8 });
    CHECK (signatureedit::atStart (fl) == std::pair (6, 8));
    signatureedit::set (fl, 0, 12, 8);
    CHECK (fl.markers().size() == 1 && fl.markers()[0].numerator == 12 && fl.beatsPerBar == 4);
    CHECK (signatureedit::clampDenominator (3) == 4 && signatureedit::clampDenominator (32) == 16 && signatureedit::text ({ 7, 8 }) == "7 / 8");
}
