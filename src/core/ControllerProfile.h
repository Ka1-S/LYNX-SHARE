#pragma once

// MIDI controller profiles (round 190aw, phases A and B of REA_rodada190as §3.3; docs/CONTROLLERS.md has the file
// format). A profile is a small text file that says what a device's buttons, knobs, faders and pads do and what LYNX
// sends back (LEDs, motor faders, display text). It is data only: no code per device, so users add their own files.
// This header is the file's model and its parser; core/ControllerSession.h runs a profile against a Surface.
// No JUCE.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lynx::controller
{
// What a button can do (Surface::command) and what a transport LED can show (Surface::lit).
enum class Command : std::uint8_t
{
    play,            // starts; while playing: pauses
    stop,
    record,
    loop,            // pattern / song mode (lit in pattern mode: the pattern loops)
    metronome,
    tapTempo,
    rewind,          // one bar back
    forward,         // one bar forward
    undo,
    previousPattern,
    nextPattern,
    count
};

enum class MessageKind : std::uint8_t
{
    note, // note on (a note off counts as value 0)
    cc,
    bend  // pitch bend, 14 bits
};

// How a control's data byte is read.
enum class ValueMode : std::uint8_t
{
    absolute,       // 0…127 (bend: 0…16383) is the position
    relativeSign,   // bit 6 = direction (set = down), bits 0…5 = ticks (Mackie Control V-Pots)
    relativeTwos,   // 1…63 = up, 127…65 = down (two's complement)
    relativeOffset  // 64 = no move, 65… = up, 63… = down
};

enum class Action : std::uint8_t
{
    none,
    command,          // arg = Command
    jog,              // relative: the song position
    mixerVolume, mixerPan, mixerMute, mixerSolo, mixerArm, mixerSelect, mixerTouch, // strip
    masterVolume, masterTouch,
    mixerBankLeft, mixerBankRight,   // by the group's strip count
    mixerStripLeft, mixerStripRight, // by one
    channelVolume, channelPan, channelMute, channelSelect, // strip
    channelBankLeft, channelBankRight,
    channelPrevious, channelNext,    // the selected channel
    pluginParam,                     // strip = the knob on the page
    pluginPageLeft, pluginPageRight,
    padNote,                         // plays `key` (+ the range index) on the selected channels
    padStep,                         // toggles a step of the selected channel
    layer                            // arg = layer index
};

// Which value a feedback line sends.
enum class Feedback : std::uint8_t
{
    command, // arg = Command (its Surface::lit)
    mixerVolume, mixerPan, mixerMute, mixerSolo, mixerArm, mixerSelect, masterVolume,
    channelVolume, channelPan, channelMute, channelSelect,
    pluginParam,
    padStep,
    layer    // arg = layer index
};

// How a continuous value is sent.
enum class Encoding : std::uint8_t
{
    plain,    // 0…127 (bend: 14 bits, the low 4 left at 0)
    ringDot,  // Mackie V-Pot ring: one LED, 1…11
    ringPan,  // boost / cut from the centre
    ringWrap, // filled from the left
    ringSpread
};

// "selected" as a strip: the selected mixer track / channel instead of a place in the bank.
constexpr int selectedStrip = -1;

// A message, or a run of them: `count` consecutive numbers (or channels, with channelRange); element i is strip
// `strip + i`.
struct MessageRange
{
    MessageKind kind = MessageKind::cc;
    int channel = -1; // 0…15, −1 = any (inputs only)
    int number = 0;   // note / controller number (unused for bend)
    int count = 1;
    bool channelRange = false;

    // The element (0…count − 1) a message is, or −1.
    int element (MessageKind k, int ch, int num) const noexcept;
};

struct Binding
{
    MessageRange message;
    int value = -1;        // only this data value (−1 = any)
    int layer = -1;        // only while this layer is active (−1 = always)
    Action action = Action::none;
    int arg = 0;           // Command / layer index
    int strip = 0;         // the first element's strip (selectedStrip: the selected one)
    ValueMode mode = ValueMode::absolute;
    bool pickup = true;    // absolute controls: soft takeover
    float step = 0.01f;    // relative controls: the change per tick
    int key = 60;          // padNote: the first element's key
    int line = 0;
};

struct FeedbackLine
{
    Feedback what = Feedback::command;
    int arg = 0;
    int strip = 0;
    int layer = -1;
    MessageRange message;
    int on = 127, off = 0;
    Encoding encoding = Encoding::plain;
    int line = 0;
};

// What a display row shows.
enum class DisplayText : std::uint8_t
{
    // one text per cell (strip)
    mixerName, mixerVolume, mixerPan,
    channelName, channelVolume, channelPan,
    pluginParamName, pluginParamValue,
    // one text for the whole row
    selectedMixerName, selectedChannelName, pluginName, layerName, text
};

struct DisplayRow
{
    int row = 0;      // 0-based
    int layer = -1;
    DisplayText what = DisplayText::mixerName;
    std::string text; // DisplayText::text
};

// A SysEx text display: `write` is the message with the place holders below, sent once per changed cell.
struct Display
{
    enum Token : int { pos = -1, row = -2, cell = -3, text = -4 }; // {pos} = row × cells × width + cell × width
    std::vector<int> write; // bytes 0…255 or a Token
    int width = 7;          // characters per cell
    int cells = 8;          // cells per row
    std::vector<DisplayRow> rows;
};

struct Profile
{
    std::string id;       // the file's name without its extension
    std::string name;     // as the Controller type list shows it
    std::string maker;
    std::vector<std::string> sources; // where the map comes from (the maker's documents)
    std::vector<std::string> match;   // port name patterns ('*' = anything; case does not matter)
    std::vector<std::string> exclude;
    std::string group;    // devices of one group share the mixer bank; each takes the next `strips` strips
    int strips = 8;
    std::vector<std::string> layers; // names; the first is active at start
    std::vector<std::vector<std::uint8_t>> init, exit; // messages sent when the device opens / closes
    std::vector<Binding> bindings;
    std::vector<FeedbackLine> feedback;
    Display display;
    bool user = false;    // from the user's folder (the list labels it)
};

struct ParseResult
{
    Profile profile;
    std::vector<std::string> errors; // "line 12: unknown action 'mixer.volum'"
    bool ok() const noexcept { return errors.empty(); }
};

// Parses a profile file. `base` resolves "extends = <id>" (null: none known).
struct Library;
ParseResult parseProfile (const std::string& text, const std::string& id, const Library* base = nullptr);

// Does a port name match a pattern list ('*' wildcards, case-insensitive)? Returns the length of the longest matching
// pattern without its wildcards (0 = no match), so the most specific profile wins.
int matchScore (const std::vector<std::string>& patterns, const std::string& portName);

// The profiles LYNX knows: the built-in ones (resources/controllers, compiled in) and the user's.
struct Library
{
    std::vector<std::shared_ptr<const Profile>> profiles;
    std::vector<std::string> errors; // "<file>: line 3: ..." of the files that did not parse (they are left out)

    // Adds a file's profile (a user profile replaces a built-in one of the same id). Returns false when it has errors.
    bool add (const std::string& text, const std::string& id, bool user);
    std::shared_ptr<const Profile> find (const std::string& id) const;
    // The profile whose patterns match the port best (user profiles win a tie); null = a generic controller.
    std::shared_ptr<const Profile> detect (const std::string& portName) const;
    // A device's profile from its setting: "" = detect by the port name, "generic" = none, else that profile.
    std::shared_ptr<const Profile> resolve (const std::string& choice, const std::string& portName) const;

    // The built-in profiles.
    static Library builtIn();
};

constexpr const char* genericChoice = "generic";

// The output port that belongs to an input port: the same name, or the system's usual pairs ("MIDIIN2 (X)" /
// "MIDIOUT2 (X)", "… DAW Out" / "… DAW In", "… MIDI Out" / "… MIDI In", "… In" / "… Out"). Empty when there is none.
std::string pairedOutput (const std::string& inputName, const std::vector<std::string>& outputNames);
} // namespace lynx::controller
