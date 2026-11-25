// Round 190rc, issue #288 (hotfix candidate): FL presets of LYNX's native generators open that generator (not LYNX's synth
// approximation), and the channel window's preset list holds Toxic's .tbio, GMS' .gmsynth and Morphine's .mrp.
// The app's --backlog190rctest walks every engine's presets with FL's own files.

#include "core/NativeEngines.h"

// #288: a preset's generator from the file (no FL needed for the extension-only kinds)
TEST (hotfix190rc_288_preset_kind_by_extension)
{
    using namespace lynx;
    natives::FlAccess none;
    CHECK (natives::presetKind ("/x/Bass.tbio", none) == InstrumentKind::toxic);
    CHECK (natives::presetKind ("/x/Pad.gmsynth", none) == InstrumentKind::gms);
    CHECK (natives::presetKind ("/x/Choir.mrp", none) == InstrumentKind::morphine);
    CHECK (! natives::presetKind ("/x/Kick.wav", none).has_value());
    CHECK (! natives::presetKind ("/x/Missing.fst", none).has_value()); // (nothing to read)
}

TEST (hotfix190rc_288_preset_extension_of_kind)
{
    using namespace lynx;
    CHECK (natives::presetExtensionOf (InstrumentKind::toxic) == ".tbio");
    CHECK (natives::presetExtensionOf (InstrumentKind::gms) == ".gmsynth");
    CHECK (natives::presetExtensionOf (InstrumentKind::morphine) == ".mrp");
    CHECK (natives::presetExtensionOf (InstrumentKind::sytrus) == ".fst");
    CHECK (natives::presetExtensionOf (InstrumentKind::harmor) == ".fst");
}
