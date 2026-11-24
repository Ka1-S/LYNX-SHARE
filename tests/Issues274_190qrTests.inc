// Round 190qr: issues #274-#281 (yannikthegenius, 2026-11-23).

#include "core/MarkerPlan.h"

// #281: FL's Time markers › Delete — the markers in the time selection, or all of them without one
TEST (issues190qr_281_time_markers_delete)
{
    using namespace lynx;
    const auto bar = (std::int64_t) 4 * ppq;
    const auto make = [bar] {
        Song s;
        for (int i = 0; i < 6; ++i)
        {
            Marker m;
            m.tick = i * bar;
            m.name = "M" + std::to_string (i);
            s.addMarker (m);
        }
        Marker sig;
        sig.tick = 2 * bar;
        sig.numerator = 3;
        sig.denominator = 4;
        s.addMarker (sig);
        return s;
    };
    auto song = make();
    CHECK (song.markers().size() == 7u);
    song.loopStart = 2 * bar;
    song.loopEnd = 4 * bar; // bars 3-4: M2, the 3/4 change, M3 (M4 sits on the selection's end: it stays)
    CHECK (markers::deleteInSelection (song) == 3);
    CHECK (song.markers().size() == 4u);
    for (const auto& m : song.markers())
        CHECK (m.tick < 2 * bar || m.tick >= 4 * bar);
    auto all = make();
    CHECK (markers::deleteInSelection (all) == 7); // no selection: every marker
    CHECK (all.markers().empty());
    CHECK (markers::deleteInSelection (all) == 0);
}
