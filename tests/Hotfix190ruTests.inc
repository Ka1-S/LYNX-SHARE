// Round 190ru (0.1.31.3 hotfix): the .flp export's plugin song-start values (round 190rm's fix adab9002a, its test
// iox190rm_flp_export_plugin_song_start_values_on_the_plugins_range on 0.1.32, here under its own name).

#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/Effects.h"

TEST (hotfix190ru_flp_export_plugin_song_start_values_on_the_plugins_range)
{
    // Thief → LYNX → .flp → FL 2026 rendered full-scale DC from the first sample: the Fruity Balance volume's song-start
    // value (event 0xD8, raw 256 of 0..320 = 0.8) went back as 0.8 x 65536 = 52429 (205 times the level). The song-start
    // value of a plugin parameter is the plugin's own integer (flRawRange), as the import reads it.
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer.resize (std::max<std::size_t> (song.mixer.size(), 3));
    auto balance = makeEffectSlot (EffectKind::fruityBalance);
    balance.flOrigin.plugin = "Fruity Balance"; // (as an FL import keeps it: FL's own state, written back as it was)
    balance.flOrigin.state = { 0, 0, 0, 0, 0, 1, 0, 0 };
    balance.flOrigin.fingerprint = flpexport::fingerprint (balance);
    song.mixer[2].effects = { balance };
    const AutomationTarget volume { AutomationTargetKind::effectParam, 2, 0, 1 };
    song.setInitialValue (volume, 0.8f);
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    bool found = false;
    for (const auto& c : project.initControls)
        if (c.id.isMixer() && c.id.track() == 2 && c.id.param() == flp::ControlId::pluginParamBase + 1)
        {
            found = true;
            CHECK (c.value == 256);
        }
    CHECK (found);
    // and back: the import reads it as the same 0.8
    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (project, io);
    bool again = false;
    for (const auto& v : back.initialValues)
        if (v.target.kind == AutomationTargetKind::effectParam && v.target.param == 1)
            again = std::abs (v.value - 0.8f) < 1.0e-3f;
    CHECK (again);
}
