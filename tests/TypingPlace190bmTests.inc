// Round 190bm (Discord report, German keyboard: "Z and Y are swapped, annoying if you use the keyboard as a keyboard"):
// the typing keyboard goes by the key's place, not its letter, as FL's does (FL's key table is indexed by the scan
// codes 2..0x35, FUN_01182670 / FUN_011828c0, REA round 169f). The UI asks the OS for the scan code of the key that
// gives a character; here simulated layouts stand in for the OS. Included from tests.cpp.

namespace typingplace190bm
{
// What each key of the typing block types without Shift, in the order of a US keyboard's
// 1234567890-= QWERTYUIOP[] ASDFGHJKL;' ` \ ZXCVBNM,./ (Latin-1 code points, letters upper case as the UI's key codes)
using Layout = std::vector<int>;
inline const char usKeys[] = "1234567890-=QWERTYUIOP[]ASDFGHJKL;'`\\ZXCVBNM,./";

inline Layout us()
{
    return Layout (std::begin (usKeys), std::end (usKeys) - 1);
}
inline Layout qwertz() // German: Z and Y swapped, sharp s, umlauts
{
    return { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xdf, 0xb4, 'Q', 'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', 0xfc, '+',
             'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0xf6, 0xe4, '^', '#', 'Y', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '-' };
}
inline Layout azerty() // French: A/Q and Z/W swapped, M right of L, digits only with Shift
{
    return { '&', 0xe9, '"', '\'', '(', '-', 0xe8, '_', 0xe7, 0xe0, ')', '=', 'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '^', '$',
             'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', 0xf9, 0xb2, '*', 'W', 'X', 'C', 'V', 'B', 'N', ',', ';', ':', '!' };
}
inline Layout dvorak()
{
    return { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '[', ']', '\'', ',', '.', 'P', 'Y', 'F', 'G', 'C', 'R', 'L', '/', '=',
             'A', 'O', 'E', 'U', 'I', 'D', 'H', 'T', 'N', 'S', '-', '`', '\\', ';', 'Q', 'J', 'K', 'X', 'B', 'M', 'W', 'V', 'Z' };
}

// The OS's answer for a layout: the scan code of the key that types `keyCode` (either case), 0 for none
inline lynx::keys::typingpiano::ScanCodeLookup lookupOf (Layout layout)
{
    return [layout = std::move (layout)] (int keyCode) {
        const auto fold = [] (int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; };
        for (std::size_t i = 0; i < layout.size(); ++i)
            if (fold (layout[i]) == fold (keyCode))
                return lynx::keys::typingpiano::scanCodeOfUsKey (usKeys[i]);
        return 0;
    };
}
} // namespace typingplace190bm

TEST (typingplace190bm_scan_codes_are_the_us_keyboard)
{
    using namespace lynx::keys::typingpiano;
    // PC set 1 (= evdev): 1 = 0x02, Q = 0x10, A = 0x1E, ` = 0x29, \ = 0x2B, Z = 0x2C, / = 0x35
    CHECK (usKeyOfScanCode (0x02) == '1' && usKeyOfScanCode (0x0D) == '=' && usKeyOfScanCode (0x10) == 'Q' && usKeyOfScanCode (0x1B) == ']');
    CHECK (usKeyOfScanCode (0x1E) == 'A' && usKeyOfScanCode (0x28) == '\'' && usKeyOfScanCode (0x29) == '`' && usKeyOfScanCode (0x2B) == '\\');
    CHECK (usKeyOfScanCode (0x2C) == 'Z' && usKeyOfScanCode (0x15) == 'Y' && usKeyOfScanCode (0x35) == '/');
    // Backspace, Tab, Enter, Ctrl, the left Shift, the ISO key and anything outside: no key of the block
    for (int scan : { 0, 1, 0x0E, 0x0F, 0x1C, 0x1D, 0x2A, 0x36, 0x39, 0x56, -3, 400 })
        CHECK (usKeyOfScanCode (scan) == 0);
    // every key of the block has one scan code, and back
    std::set<int> seen;
    for (const char* k = typingplace190bm::usKeys; *k != 0; ++k)
    {
        const auto scan = scanCodeOfUsKey (*k);
        CHECK (scan >= 0x02 && scan <= 0x35 && usKeyOfScanCode (scan) == *k);
        seen.insert (scan);
    }
    CHECK (seen.size() == 47);
    CHECK (scanCodeOfUsKey ('z') == 0x2C && scanCodeOfUsKey (0) == 0 && scanCodeOfUsKey (' ') == 0);
}

TEST (typingplace190bm_every_layout_plays_as_the_us_keyboard)
{
    using namespace lynx::keys;
    using namespace typingplace190bm;
    const std::pair<const char*, Layout> layouts[] = { { "US", us() }, { "QWERTZ", qwertz() }, { "AZERTY", azerty() }, { "Dvorak", dvorak() } };
    for (const auto& [name, layout] : layouts)
    {
        CHECK (layout.size() == 47);
        const auto lookup = lookupOf (layout);
        for (std::size_t i = 0; i < layout.size(); ++i)
        {
            // the key at place i types layout[i]; it plays what the US key of that place plays, in every FL layout
            const auto place = typingpiano::physicalKey (layout[i], lookup);
            CHECK (place == usKeys[i]);
            for (int l = 0; l < typingpiano::layoutCount(); ++l)
            {
                const auto got = typingPianoKey (letterOfKeyCode (place), 4, 0, l), want = typingPianoKey (usKeys[i], 4, 0, l);
                CHECK (got.taken == want.taken && got.notes == want.notes);
            }
        }
    }
}

TEST (typingplace190bm_german_keyboard_z_row_starts_left_of_x)
{
    using namespace lynx::keys;
    using namespace typingplace190bm;
    const auto piano = typingpiano::pianoLayout();
    const auto play = [piano] (int keyCode, const typingpiano::ScanCodeLookup& lookup) {
        return typingPianoKey (letterOfKeyCode (typingpiano::physicalKey (keyCode, lookup)), 4, 0, piano);
    };
    // the report: on QWERTZ the key left of X types Y. It is the Z row's first note (MIDI 48), and the key between T and
    // U (types Z) is the Q row's A (MIDI 69). By character they were the other way round.
    const auto german = lookupOf (qwertz());
    CHECK (play ('Y', german).notes == std::vector<int> { 48 });
    CHECK (play ('Z', german).notes == std::vector<int> { 69 });
    CHECK (play ('y', german).notes == std::vector<int> { 48 }); // (Linux hands the lower-case character)
    CHECK (typingPianoNote ('Y') == 69 && typingPianoNote ('Z') == 48); // (the US keyboard, unchanged)
    // AZERTY: the Q row starts at the key that types A; the number row's black keys play without Shift (2 = e acute)
    const auto french = lookupOf (azerty());
    CHECK (play ('A', french).notes == std::vector<int> { 60 });
    CHECK (play ('W', french).notes == std::vector<int> { 48 });
    CHECK (play (0xe9, french).notes == std::vector<int> { 61 });
    CHECK (play (',', french).notes == typingPianoKey ('M', 4, 0, piano).notes);
    // Dvorak: the key at Z's place types a semicolon
    CHECK (play (';', lookupOf (dvorak())).notes == std::vector<int> { 48 });

    // a key whose place the OS can't tell stays by character (as before); so does everything without a lookup
    CHECK (typingpiano::physicalKey ('Z', [] (int) { return 0; }) == 'Z');
    CHECK (typingpiano::physicalKey ('Z', nullptr) == 'Z');
    // a key at a place outside the block (the ISO key left of Z, scan 0x56) is no typing key, whatever it types
    CHECK (typingpiano::physicalKey ('Z', [] (int) { return 0x56; }) == 0);
    CHECK (! play ('<', [] (int) { return 0x56; }).taken);
}
