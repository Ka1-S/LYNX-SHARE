#include "core/HostKeyGate.h"
#include "core/FlWrapper.h"
#include "core/FlpExport.h"

TEST (keys96_gate_keeps_a_taken_key_whole)
{
    lynx::HostKeyGate gate;
    int asked = 0;
    auto yes = [&asked] { ++asked; return true; };
    auto no = [&asked] { ++asked; return false; };

    // taken: the press, its repeats and its key-up all stay away from the plugin; asked once
    CHECK (gate.message ('Q', true, false, yes));
    CHECK (gate.message ('Q', true, true, yes));
    CHECK (gate.message ('Q', true, true, no));
    CHECK (asked == 1);
    CHECK (gate.message ('Q', false, false, no));
    CHECK (asked == 1);
    // released: nothing is left behind, the next key-up is the plugin's
    CHECK (! gate.message ('Q', false, false, no));

    // left alone: the plugin gets the whole key
    CHECK (! gate.message ('W', true, false, no));
    CHECK (asked == 2);
    CHECK (! gate.message ('W', true, true, yes));
    CHECK (asked == 2);
    CHECK (! gate.message ('W', false, false, yes));

    // a repeat of a key pressed before the plugin had the focus isn't a new press
    CHECK (! gate.message ('E', true, true, yes));
    CHECK (asked == 2);

    // two keys at once are independent
    CHECK (gate.message ('A', true, false, yes));
    CHECK (! gate.message ('S', true, false, no));
    CHECK (! gate.message ('S', false, false, no));
    CHECK (gate.message ('A', false, false, no));

    // out of range never touches anything
    CHECK (! gate.message (300, true, false, yes));
    CHECK (! gate.message (300, false, false, yes));
}

TEST (keys187a_a_taken_key_repeats_for_the_host)
{
    // round 187a: a held key the host took repeats as over the main window (a shortcut runs again); one the plugin
    // got repeats for the plugin only
    lynx::HostKeyGate gate;
    int asked = 0, again = 0;
    auto yes = [&asked] { ++asked; return true; };
    auto no = [&asked] { ++asked; return false; };
    auto rep = [&again] { ++again; };

    CHECK (gate.message (0x20, true, false, yes, rep)); // Space
    CHECK (gate.holds (0x20));
    CHECK (gate.message (0x20, true, true, yes, rep));
    CHECK (gate.message (0x20, true, true, no, rep));
    CHECK (asked == 1 && again == 2);
    CHECK (gate.message (0x20, false, false, no, rep));
    CHECK (! gate.holds (0x20) && again == 2);

    CHECK (! gate.message (0x26, true, false, no, rep)); // Up, left to the plugin
    CHECK (! gate.message (0x26, true, true, yes, rep));
    CHECK (asked == 2 && again == 2);
    CHECK (! gate.message (0x26, false, false, yes, rep));

    // a repeat of a press made before the focus came here is nobody's new press
    CHECK (! gate.message (0x27, true, true, yes, rep));
    CHECK (asked == 2 && again == 2);
}

TEST (keys96_options_off_for_a_new_plugin)
{
    // FL: the Troubleshooting box is off for a new plugin [UI: E:\lynx-work\pain181c\shots\d21.png]; the plugin gets
    // keys first ("Typing keyboard priority" on: FL's General option "Disable typing keyboard priority by default" is
    // off, its registry value 0 on the study install, 2026-11-09)
    lynx::PluginRef ref;
    CHECK (! ref.allowHostKeys);
    CHECK (! ref.hostKeysFirst);

    auto other = ref;
    other.allowHostKeys = true;
    CHECK (! (ref == other)); // a project with it on differs (undo, modified)
    other = ref;
    other.hostKeysFirst = true;
    CHECK (! (ref == other));
}

TEST (keys96_option_reads_from_fl_wrapper_flags)
{
    // FL 2026: the same project saved with the option off and on differs in this one bit of the second flags word
    // (+21; E:\lynx-work\pain181c\flp\off.flp, on.flp, byte 45291: 00 -> 08)
    lynx::ForeignPlugin f;
    lynx::PluginRef ref;
    f.hasWrapperOptions = true;
    f.wrapperVersion = 12;
    f.wrapperFlags = 0xB4007D8Cu;
    f.hasWrapperFlags2 = true;
    f.wrapperFlags2 = 0;
    lynx::applyFlWrapperOptions (f, ref);
    CHECK (! ref.allowHostKeys);
    f.wrapperFlags2 = 0x800u;
    lynx::applyFlWrapperOptions (f, ref);
    CHECK (ref.allowHostKeys && ref.allNotesOffOnStop); // its neighbour 0x1000 untouched
}

TEST (keys96_option_writes_to_fl_wrapper_flags)
{
    using namespace lynx;
    ForeignPlugin p;
    p.format = ForeignPlugin::Format::vst3;
    p.name = "Synth";
    p.path = "C:/x/Synth.vst3";
    p.hasClassId = true;
    p.classId[0] = 0xAB;
    p.state = flpexport::flVst3State ({ 1, 2, 3 }, { 4 });
    for (const bool notesOff : { true, false })
    {
        PluginRef ref;
        ref.allNotesOffOnStop = notesOff;
        ForeignPlugin back;
        CHECK (parseFlWrapper (flpexport::writeWrapper (p, true, &ref), back) && back.hasWrapperFlags2);
        CHECK ((back.wrapperFlags2 & 0x800u) == 0); // off: FL's default record
        CHECK (((back.wrapperFlags2 & 0x1000u) != 0) == ! notesOff);
        ref.allowHostKeys = true;
        CHECK (parseFlWrapper (flpexport::writeWrapper (p, true, &ref), back) && back.hasWrapperFlags2);
        CHECK ((back.wrapperFlags2 & 0x800u) != 0);
        CHECK (((back.wrapperFlags2 & 0x1000u) != 0) == ! notesOff); // its neighbour untouched
        PluginRef read;
        applyFlWrapperOptions (back, read);
        CHECK (read.allowHostKeys);
    }
}
