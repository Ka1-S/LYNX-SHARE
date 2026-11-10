#include "core/PedalHold.h"

// Round 187a: FL's "Foot pedal controls note off" (FLEngine 2026 [DECOMP] FUN_00fdb260, FUN_01182540, FUN_01001070,
// FUN_010002e0)

TEST (pedal187a_only_cc64_and_cc66_are_pedals)
{
    CHECK (lynx::PedalHold::modeFor (64, 127) == 1);
    CHECK (lynx::PedalHold::modeFor (64, 64) == 1);
    CHECK (lynx::PedalHold::modeFor (64, 63) == 0);
    CHECK (lynx::PedalHold::modeFor (66, 100) == 2);
    CHECK (lynx::PedalHold::modeFor (66, 0) == 0);
    CHECK (lynx::PedalHold::modeFor (65, 127) == -1); // (FL's mask 0x05 over CC 64..71: 64 and 66 only)
    CHECK (lynx::PedalHold::modeFor (67, 127) == -1);
    CHECK (lynx::PedalHold::modeFor (1, 127) == -1);
}

TEST (pedal187a_sustain_holds_note_offs_until_the_pedal_comes_up)
{
    lynx::PedalHold h;
    std::vector<int> released;
    auto rel = [&released] (int k) { released.push_back (k); };

    CHECK (! h.noteOff (0, 60)); // no pedal: nothing held
    CHECK (h.setMode (0, 1, rel));
    CHECK (! h.noteOn (0, 60));   // first strike after the pedal: plays
    CHECK (h.noteOff (0, 60));    // held back
    CHECK (! h.noteOn (0, 62));
    CHECK (h.noteOff (0, 62));
    CHECK (h.noteOn (0, 60));     // struck again while held: released first
    CHECK (h.noteOff (0, 60));
    CHECK (h.noteOff (1, 60) == false); // another channel has no pedal
    CHECK (! h.setMode (0, 2, rel)); // (FL: down stays down; sustain -> sostenuto changes nothing)
    CHECK (h.mode (0) == 1);
    CHECK (h.setMode (0, 0, rel));
    std::sort (released.begin(), released.end());
    CHECK (released == std::vector<int> ({ 60, 62 }));
    CHECK (! h.noteOff (0, 60)); // pedal up: plain note-offs again
    CHECK (! h.setMode (0, 0, rel));
}

TEST (pedal187a_sostenuto_holds_only_keys_down_when_pressed)
{
    lynx::PedalHold h;
    std::vector<int> released;
    auto rel = [&released] (int k) { released.push_back (k); };
    // C held, then the sostenuto pedal, then E played and released, then C released
    CHECK (h.setMode (3, 2, rel));
    CHECK (! h.noteOn (3, 64)); // a key struck after the pedal: plays and stops as usual
    CHECK (! h.noteOff (3, 64));
    CHECK (h.noteOff (3, 60));  // C was down before the pedal (its flag still 2): held back
    CHECK (! h.noteOn (3, 60)); // sostenuto never releases first
    CHECK (! h.noteOff (3, 60)); // ...and the new strike stops as usual
    CHECK (h.setMode (3, 0, rel));
    CHECK (released.empty()); // (the held one was struck again and ended)
    CHECK (! h.noteOff (3, 200)); // out of range
}

TEST (pedal187a_option_is_on_by_default_and_saved)
{
    // FL: KbHoldSupportBox "Checked": true; an old settings file (no key) reads as on
    lynx::prefs::MidiPrefs p;
    CHECK (p.footPedalNoteOff);
    CHECK (lynx::prefs::MidiPrefs::fromText ("velocity=hard").footPedalNoteOff);
    p.footPedalNoteOff = false;
    const auto back = lynx::prefs::MidiPrefs::fromText (p.toText());
    CHECK (back == p && ! back.footPedalNoteOff);
}
