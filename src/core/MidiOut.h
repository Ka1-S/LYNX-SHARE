#pragma once

// FL's MIDI Out generator as LYNX SHARE plays it (REA_rodada167i §1, from FL's own "MIDI Out_x64.dll": its state
// reader/writer, its parameter handler and its editor form). 72 controls on 8 pages of 9 knobs; each control is a CC,
// RPN, NRPN, channel aftertouch or note that its knob's value sends; the patch (program), bank and port; what a reset
// sends. FL's parameter 0 is the patch, parameters 1…72 are the controls (automation clips, pattern events, links).
//
// The state is FL's own `0xD5` blob (any version FL wrote: 1…7), so .flp projects, FL's .fst presets and LYNX
// projects share it.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lynx
{
class Instrument;
}

namespace lynx::midiout
{
constexpr int controlCount = 72, pageCount = 8, knobsPerPage = 9;
constexpr int programParam = 0; // FL's parameter 0: the patch; parameter 1 + i: control i

enum class Type
{
    cc = 0,         // CC `number` = the value (14-bit when the range passes 127: CC number gets the high 7 bits, CC number + 32 the low ones)
    rpn = 1,        // CC 101 / 100 = the number, then data entry CC 6 (+ CC 38 when 14-bit)
    nrpn = 2,       // CC 99 / 98 = the number, then data entry
    aftertouch = 3, // channel pressure = the value
    note = 4        // the value is the key, `number` the velocity; `noteOff` = the value that only ends the note
};

struct Control
{
    int number = -1;     // −1 = unassigned (FL's "right click to configure")
    bool active = false; // FL keeps an assigned control's number with bit 31 set while it is deactivated
    Type type = Type::cc;
    int min = 0, max = 127;
    int channel = -1; // MIDI channel override 0…15; −1 = the MIDI Out's channel
    int noteOff = 0;  // Note: the value that sends only the note off
    std::string name, shortName;

    bool assigned() const noexcept { return number >= 0; }
    bool sends() const noexcept { return number >= 0 && active; }
    bool operator== (const Control&) const = default;
};

// FL's "On reset" options (the Options menu): what a reset sends besides the bank and the active controls
enum ResetFlag : int
{
    resetPitch = 1,      // Send pitch
    resetBendRange = 2,  // Send pitch bend range (RPN 0 = 12 semitones)
    resetCutNotes = 4,   // Cut playing notes (the Note controls' notes)
    resetPan = 8,        // Send pan
    resetVolume = 16,    // Send volume
    resetProgram = 32,   // Send program change
    resetAll = 63
};

struct State
{
    int version = 7;
    int channel = 0;         // MIDI channel 0…15
    int reserved = -1;       // FL's word at 8 (always −1)
    int bankMsb = -1, bankLsb = -1; // −1 = not sent
    int page = 0;            // the page shown (0…7)
    int port = 0;            // output port 0…255
    bool sendPanVolPitch = true; // "Send pan, volume and pitch"
    bool mapNoteColour = false;  // "Map note color to MIDI channel"
    int program = 0;         // the patch 1…128; 0 = none
    std::array<int, controlCount> values {};
    std::array<Control, controlCount> controls {};
    std::array<std::string, pageCount> pageNames;
    int resetFlags = resetAll; // FL: a state of version < 6 takes them all from "Send pan, volume and pitch" (+ cut notes)

    State();
    bool operator== (const State&) const = default;
};

// FL's state (any version 1…7); nullopt when it does not parse.
std::optional<State> parse (const std::vector<std::uint8_t>& state);
// FL's version 7 layout (what FL 2026 writes).
std::vector<std::uint8_t> write (const State& state);
// The state of a channel: parsed, or FL's defaults when empty / unreadable.
State stateOf (const std::vector<std::uint8_t>& state);
// The plugin state inside an FL preset file (.fst: FLhd/FLdt events, the 0xD5 one); empty when none.
std::vector<std::uint8_t> stateFromFst (const std::vector<std::uint8_t>& file);
// A preset file FL reads back (.fst: FLhd + FLdt with the plugin's name and its state).
std::vector<std::uint8_t> writeFst (const State& state);

// A control's knob position 0..1 ↔ its value (min…max).
int valueFromNormal (const Control& c, float normal) noexcept;
float normalFromValue (const Control& c, int value) noexcept;
// The program (patch) parameter: 0..1 ↔ 0…128.
int programFromNormal (float normal) noexcept;
float normalFromProgram (int program) noexcept;

// FL's parameter k (0 = the patch, 1…72 = the controls) as 0..1, and set from 0..1 (automation's "init" value, the
// editor's links): a value set on a deactivated control switches it on, as FL does. False when k is out of range.
float paramValue (const State& s, int k) noexcept;
bool setParamValue (State& s, int k, float normal) noexcept;
// "Patch", or the control's label / "Control 12" (automation clip and link names)
std::string paramName (const State& s, int k);

// What the knob shows under it: the short name, else the full name, else "CC 74" / "Aftertouch"; "" when unassigned.
std::string label (const Control& c);
// "CC", "RPN", "NRPN", "Channel aftertouch", "Note" (FL's type names)
const char* typeName (Type t) noexcept;
// "Modulation wheel CC 1 Chan #3": the full name, the type and number, the channel override (FL's knob hint)
std::string description (const Control& c);

// The messages FL's MIDI Out sends, packed status | data1 << 8 | data2 << 16 (a 0xC0 / 0xD0 status has no data 2).
using Message = std::uint32_t;
inline Message pack (int status, int d1, int d2 = 0) noexcept
{
    return (Message) (status & 0xff) | ((Message) (d1 & 127) << 8) | ((Message) (d2 & 127) << 16);
}

// Control i set to `value` (clamped to its range): what it sends (nothing when unassigned or inactive). `held` is the
// note a Note control holds (−1 none), updated. Returns the number of messages written (at most 6).
int controlMessages (const State& s, int i, int value, int& held, Message* out) noexcept;
// The patch (FL sends it with "Send program change" on): 0xC0 program − 1; none for 0.
int programMessages (const State& s, Message* out) noexcept;
// A reset (FL's Reset button, "Send all control values now", the channel switched on, playback start): pitch bend
// range, bank, pan / volume / pitch (if their option is on and a value is known: `pan`, `volume` 0…127, `bend`
// 0…16383, −1 = unknown), the patch, every active control's value. `held` (72 entries) are the Note controls' notes:
// with "Cut playing notes" they end first. Returns the number written (`out` must hold maxResetMessages).
constexpr int maxResetMessages = 8 + 1 + 3 + 1 + controlCount * 6 + controlCount;
int resetMessages (const State& s, int pan, int volume, int bend, std::array<int, controlCount>& held, Message* out) noexcept;
// "Reset all controllers" (CC 121), the first thing FL's Reset button sends.
Message resetAllControllers (const State& s) noexcept;

// The channel's pan (−1..1), volume (0..1) and pitch (cents) as the MIDI Out sends them with "Send pan, volume and
// pitch": CC 10, CC 7, pitch bend (±1200 cents = the whole bend).
int panValue (float pan) noexcept;
int volumeValue (float volume) noexcept;
int bendValue (float cents) noexcept;

// FL's own "MIDITrack.fst" (Data\System): the controls a MIDI Out made by "Import MIDI data" gets — page 1 "Basic
// controllers" CC 1, 2, 4, 5, 91, 93; page 2 "Boolean controllers" CC 64…69 (knobs 10…15), all deactivated until
// they get a value. LYNX builds the same layout itself (FL's file is used when the user has it).
State importTrackState();
// FL's "Import MIDI data" with MIDI Out channels: the parameter a CC of the file goes to (1…72; −1 = not imported). FL
// imports CC 1, 2, 4, 5, 91, 93, 64…69 onto those knobs (CC 7 / 10 go to the channel's volume / pan).
int importParamForCc (int cc) noexcept;

// The audio-thread instrument of a MIDI Out channel: its knobs' values (configure: the song's; setParameter:
// automation, links) become the messages above, taken by the rack (takeMidiMessages) for the plugins on its port.
std::unique_ptr<Instrument> makeInstrument();
// The editor's Reset button (`allControllersFirst`: CC 121 first) and "Send all control values now": the MIDI Out's
// instrument sends a reset on its next block. Does nothing for another instrument.
void requestReset (Instrument& instrument, bool allControllersFirst);
} // namespace lynx::midiout
