#pragma once

// LYNX's key map: every keyboard shortcut lives in one remappable table (docs/WORKFLOW_FL.md,
// P1 item 14). Platform-neutral (no JUCE): the UI converts its key events into a Chord, asks
// the map which action it triggers in the focused panel, and runs it.
//
// Typing piano: while it is on, the unmodified keys of the piano layout play notes and win over any
// shortcut bound to them; other plain printable keys only work for actions marked `whilePiano`.
// Modified chords (Ctrl/Alt), F-keys and the non-printable keys (Space, Enter, arrows, Delete...)
// always work.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::keys
{
// Key codes: printable keys use their upper-case ASCII code ('A', '1', '+'); the others these values.
enum Key : int
{
    space = ' ',
    f1 = 0x10001, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
    up = 0x10020, down, left, right, pageUp, pageDown, home, end,
    enter = 0x10030, escape, tab, backspace, del, insert,
    numpadPlus = 0x10040, numpadMinus, numpadMultiply, numpadDivide,
    numpad0 = 0x10050, numpad1, numpad2, numpad3, numpad4, numpad5, numpad6, numpad7, numpad8, numpad9
};

enum Modifier : std::uint8_t
{
    none = 0,
    ctrl = 1,
    shift = 2,
    alt = 4
};

struct Chord
{
    int key = 0;
    std::uint8_t mods = none;

    bool operator== (const Chord&) const = default;
    bool isPrintable() const noexcept; // a plain character key (letter, digit, punctuation), Space excluded
    bool isValid() const noexcept { return key != 0; }
};

// "Ctrl+Shift+F4", "Alt+Up", "Numpad +", "Space"... (English key names, used in the JSON file and menus)
std::string toString (const Chord& chord);
// toString for the screen: on macOS with FL-for-Mac's key names (Ctrl -> ⌘, Alt -> ⌥; core/ModifierMap.h). The saved
// keymap always uses toString.
std::string displayString (const Chord& chord);
std::optional<Chord> parseChord (const std::string& text);

// Where a binding applies. Global bindings work everywhere; the others only while that panel has focus
// (and win over a global binding of the same chord).
enum class Context : std::uint8_t { global, channelRack, pianoRoll, playlist, mixer, browser };

struct Action
{
    const char* id;          // stable identifier ("pattern.next"), the key of the JSON file
    const char* description; // English, translated by the UI
    Context context;
    std::vector<Chord> defaults;
    bool whilePiano = false; // a plain printable key that keeps working while the typing piano is on
};

// The built-in actions with LYNX's default chords.
const std::vector<Action>& actions();
const Action* findAction (const std::string& id);

// PC keyboard as a piano: MIDI note of a key (upper-case code), or -1. Two rows, Z S X D C V G B H N J M and
// Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P, the upper one an octave above. As FL 2026 (round 168t2, measured in FL:
// R plays F5, E plays E5, P plays E6 in FL's names, where C5 = MIDI 60): the lower row starts at `octave` (FL's
// "Octave" 2..6 in the typing keyboard button's right-click menu, default 4: Z = MIDI 48), shifted up by
// `root` semitones (FL's "Root note", C..B = 0..11). Notes outside 0..127 give -1.
namespace typingpiano
{
    constexpr int defaultOctave = 4, minOctave = 2, maxOctave = 6;
    constexpr int clampOctave (int o) noexcept { return o < minOctave ? minOctave : (o > maxOctave ? maxOctave : o); }
    constexpr int clampRoot (int r) noexcept { return r < 0 ? 0 : (r > 11 ? 11 : r); }
}
int typingPianoNote (int key, int octave = typingpiano::defaultOctave, int root = 0) noexcept;

// FL's "Layout" list in the same menu, from FL 2026's System/Config/Typing to piano/*.scr (written as data, named as
// FL's files and ordered as FL's menu): FPC, Janko, Performance mode, Piano (the default), Slicex, then the scales and
// chord maps. FL's "Layout Piano" plays more than typingPianoNote's two rows (L ; , . / [ ] = too) and locks 1 4 8 - F K '.
namespace typingpiano
{
    int layoutCount() noexcept;
    const char* layoutName (int layout) noexcept; // "" outside 0..layoutCount()-1
    int layoutIndex (const std::string& name) noexcept; // the Piano layout for an unknown name
    int pianoLayout() noexcept;
    int performanceLayout() noexcept; // FL's "Layout Performance mode" (its notes launch the Playlist's clips)
}
// What `key` (a plain one: no Ctrl / Alt / Shift) does in `layout` while the typing keyboard is on. `taken`: the piano
// has the key (a note, or FL's LOCK: no note, no shortcut); not taken = FL's FREE, the key stays a shortcut. `notes`:
// FL's notes at octave 4, root C, moved by `octave` and `root` in every layout (several for the chord layouts; notes
// outside 0..127 are dropped, the key stays taken).
struct TypingKey
{
    std::vector<int> notes;
    bool taken = false;
};
TypingKey typingPianoKey (int key, int octave, int root, int layout);
std::vector<int> typingPianoNotes (int key, int octave, int root, int layout); // typingPianoKey().notes

// The typing keyboard goes by the key's PLACE, not its letter, as FL's does (FL's key table is indexed by the scan
// codes 2..0x35, FUN_01182670 / FUN_011828c0, round 169f): on a German keyboard (QWERTZ) the key left of X plays the
// Z row's first note although it types Y; on AZERTY the Q row starts at the key that types A, and the number row plays
// without Shift. Shortcuts (Ctrl+Z...) stay by character.
namespace typingpiano
{
    // The key a US keyboard has at PC scan code `scanCode` (set 1: 0x02 = '1' ... 0x10 = 'Q' ... 0x2C = 'Z' ... 0x35 =
    // '/'; the same numbers as Linux's evdev key codes), upper case; 0 for a key outside the typing block (the ISO
    // key next to the left Shift, 0x56, has no US place).
    int usKeyOfScanCode (int scanCode) noexcept;
    int scanCodeOfUsKey (int key) noexcept; // the reverse; 0 for none
    // The scan code of the key that gives `keyCode` (the UI's key code: the key's unshifted character in the user's
    // keyboard layout); 0 = not known (no such key in the layout, or the platform can't tell).
    using ScanCodeLookup = std::function<int (int keyCode)>;
    // The US key at the place of the key that gives `keyCode`: what typingPianoKey() is asked for. A key whose place
    // is not known stays as it is (by character, as before); a key at a place outside the typing block gives 0.
    int physicalKey (int keyCode, const ScanCodeLookup& scanCodeOf);
}

// The upper-case printable character of a key code (JUCE's getKeyCode()), or 0 for any other key. On Windows JUCE's
// F-keys, numpad keys and arrows are VK | 0x10000: a plain towupper() of them (16-bit) gave F4 -> 'S', F1 -> 'P',
// Numpad1 -> 'A', so the editors took them for tool letters (issue #12, found by round 165q).
constexpr int letterOfKeyCode (int code) noexcept
{
    return code > ' ' && code < 0x7f ? (code >= 'a' && code <= 'z' ? code - 'a' + 'A' : code) : 0;
}

class KeyMap
{
public:
    KeyMap(); // the defaults

    // Action triggered by `chord` with `focus` focused; "" for none. `typingPiano`: the piano is on.
    std::string find (const Chord& chord, Context focus, bool typingPiano) const;

    // Like find(), but only an action the user gave `chord` himself (not one of its default keys). The main window
    // runs it before the panels' own keys, so a key you chose always does what you chose (issue #12: Metronome on
    // 'C' did nothing because the focused Playlist took 'C' for its Knife).
    std::string findUserBound (const Chord& chord, Context focus, bool typingPiano) const;
    // The actions of the other places (global vs a panel) bound to `chord` too: where both apply, the user's own key
    // wins over a default one, else the panel's (the Shortcut manager tells which).
    std::vector<std::string> shadowedBy (const Chord& chord, const std::string& actionId) const;

    // A key a secondary window (Undo history, Settings, Tempo tapper, Quick-add, Audio editor, a plugin or channel
    // window...) did not use: the global action it runs there, or "". FL runs its shortcuts from those windows too
    // (round 165q's find for issue #12: Space and F12 did nothing while such a window had the focus). Only global
    // actions (a panel's keys belong to the panel), and nothing a text field types (`typingIntoText`: a text field has
    // the focus, so its unmodified letters, Space, arrows, Enter... stay text editing).
    std::string findFromSecondaryWindow (const Chord& chord, bool typingIntoText, bool typingPiano) const;
    // Issue #117: a key the typing piano takes (`pianoTakes`: typing keyboard to piano on and the layout plays it) plays
    // its note from a secondary window too (a LYNX instrument's or effect's window, a tool window), as over the main
    // window and as FL does; only a focused text field keeps it.
    static bool secondaryWindowPlaysNote (bool pianoTakes, bool typingIntoText) noexcept { return pianoTakes && ! typingIntoText; }

    // True when the chord plays a note (piano on, unmodified, mapped key).
    static bool playsNote (const Chord& chord, bool typingPiano) noexcept;

    const std::vector<Chord>& chordsFor (const std::string& actionId) const;
    void setChords (const std::string& actionId, std::vector<Chord> chords); // [] = unbound
    void resetToDefaults();

    // Overrides from a JSON object {"action.id": "Ctrl+L", "other.id": ["F4", "Shift+F4"], "x": ""}.
    // Unknown ids and unreadable chords are reported in `errors` and skipped. Returns false if the
    // text is not a JSON object.
    bool loadJson (const std::string& json, std::vector<std::string>* errors = nullptr);
    std::string toJson() const; // every action with its current chords

    // Shortcut manager: the actions other than `except` that `chord` triggers in `context` (same context only:
    // a panel's binding wins over a global one there, so those don't clash).
    std::vector<std::string> actionsWith (const Chord& chord, Context context, const std::string& except = {}) const;
    bool isDefault (const std::string& actionId) const;
    // Only the actions whose chords differ from the defaults, as loadJson reads them (what keys.json keeps).
    std::string overridesJson() const;

    // Pairs of actions bound to the same chord in overlapping contexts (for the help sheet / tests).
    std::vector<std::pair<std::string, std::string>> conflicts() const;

    // Every binding on F1…F12 (any context, the user's own keys included), ordered F1 → F12 and per key plain, Shift,
    // Ctrl, Alt, then the combinations (the shortcut sheet's "Function keys" column, issue #23).
    struct Binding
    {
        Chord chord;
        std::string actionId;
        Context context;
    };
    std::vector<Binding> functionKeyBindings() const;

private:
    std::vector<std::vector<Chord>> chords; // parallel to actions()
};

// F10 / F11 (MIDI settings / Project info), FL's rule (FLEngine 0x13545f0, round 167o): when the Settings window is
// open on the asked page the key closes it; open on another page it switches to the asked one; closed, it opens.
enum class SettingsKey { open, switchPage, close };
constexpr SettingsKey settingsKeyPress (bool windowOpen, int shownPage, int askedPage) noexcept
{
    return ! windowOpen ? SettingsKey::open : (shownPage == askedPage ? SettingsKey::close : SettingsKey::switchPage);
}
} // namespace lynx::keys
