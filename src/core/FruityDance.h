#pragma once

// LYNX Dance (round 165e, docs/analise/REA_rodada165e.md): FL's Fruity Dance role — an animated dancer over the
// workspace whose moves the channel's notes pick — with LYNX's own procedural character (no FL artwork). No sound.
//
// FL's behaviour, read in its plugin (REA round 68 §8 and round 165e): a note picks a move (the keys from B4 up: 0 =
// Freestyle, then the moves in order), the keys from C4 down play the same moves mirrored, F4 hides the dancer and F#4
// shows it; the note itself makes no sound and ends at once — the move stays until the next note. The animation follows
// the song position, with a speed setting. The saved state (int version; seven ints: move, mirrored, shown, a scale,
// speed, x, y (the dancer's bottom centre in the main window); one int and four flags of display options; version 2 adds
// a picture path) is read when an FL project is imported.
//
// Sprite sheets (issue #121, docs/analise/REA_rodada190ba-121.md): FL's dancer is a picture file of 8 frames across
// and one row per move, with the moves' names in a text file beside it (the picture's name with ".txt" in place of
// its extension, one name per line). A channel with such a sheet shows its frames instead of LYNX's character; the
// rules below (`sheet…`, `SheetAnimator`) are FL's, read in its plugin.
// No JUCE here (tests/Visual165Tests.inc, tests/Dance190baTests.inc).

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lynx
{
class Instrument;
}

namespace lynx::dance
{
enum Move : int { freestyle, waiting, stepping, jumping, zombie, waving, hula, windmill, flail, spin, held };
constexpr int moveCount = 11;
const char* moveName (int move) noexcept; // English (the UI translates)

struct Settings
{
    int move = stepping;
    bool mirrored = false;
    bool visible = true;
    float scale = 1.0f;      // size (FL's scale value / 64)
    float speed = 1.0f;      // moves per beat relative to the default
    int x = 412, y = 500;    // the dancer's bottom centre in the main window, in pixels (FL's defaults)
    std::uint32_t colour = 0xffff3048; // the outfit (ARGB)
    float blend = 0.25f;     // a sprite sheet's "Blend" (FL's motion blending: the part of each frame that fades to the next)
    std::string sheet;       // a sprite sheet's picture file (UTF-8; empty: LYNX's own dancer)
    // FL's display options (round 190bf, FL's editor: Sync changes, Solid, Keep in front, On desktop, Reflect; all on and
    // Reflect 64 / 256 in a new Fruity Dance). sync: a move changes at the next cycle's start (off: at once); solid:
    // the dancer takes the mouse (off: clicks go through it); keepInFront: kept in front of the panels and docked
    // plugins; onDesktop: FL's "Don't minimize window along with application" (kept for FL; LYNX's dancers live in the
    // main window); reflect: the opacity of the faded mirror image under the dancer.
    bool sync = true, solid = true, keepInFront = true, onDesktop = true;
    float reflect = 0.25f;

    bool operator== (const Settings&) const = default;
};

// LYNX's own state ("LYNX dance 1", key=value lines)
std::vector<std::uint8_t> toState (const Settings& s);
std::optional<Settings> fromState (const std::vector<std::uint8_t>& state);
// FL's Fruity Dance state (40 bytes or more); nullopt = not one. `flFactoryData`: FL's install folder (what FL's
// %FLStudioFactoryData% stands for, e.g. C:\Program Files\Image-Line\FL Studio 2026), to find a sheet FL saved
// inside it; "" = not known (such a path then stays as FL wrote it).
std::optional<Settings> fromFl (const std::vector<std::uint8_t>& state, const std::string& flFactoryData = {});
// FL's state for an exported .flp (version 2: the settings, the display options, the sheet's file: as FL writes it,
// "%FLStudioFactoryData%\<rest>" for a file inside `flFactoryData`, else the full path; UTF-8, the length in bytes)
std::vector<std::uint8_t> toFl (const Settings& s, const std::string& flFactoryData = {});
// The sheet's file as FL keeps it (round 190bf, fl190be Q5: FL 2026 saved its own Count.png as
// "%FLStudioFactoryData%\Plugins\Fruity\Generators\Fruity Dance\Artwork\Count.png" and a copy elsewhere as its
// full path, never relative to the project) and back
extern const char* const flFactoryToken; // "%FLStudioFactoryData%"
std::string expandFlSheetPath (const std::string& stored, const std::string& flFactoryData);
std::string flSheetPath (const std::string& path, const std::string& flFactoryData);
// either (LYNX's text first, then FL's bytes); the defaults when neither
Settings read (const std::vector<std::uint8_t>& state);

// What a note does (FL's key map)
struct KeyAction
{
    enum class Kind { none, move, hide, show };
    Kind kind = Kind::none;
    int move = 0;
    bool mirrored = false;
};
KeyAction actionForKey (int key) noexcept;           // LYNX's own dancer (moveCount moves)
KeyAction actionForKey (int key, int moves) noexcept; // a dancer with that many moves, Freestyle included
constexpr int firstMoveKey = 59;   // B4: Freestyle; C5 … A5: the moves
constexpr int firstMirrorKey = 48; // C4: Freestyle mirrored; down to D#3
constexpr int hideKey = 53, showKey = 54;

// The body at a moment: angles in radians (0 = hanging down for limbs, upright for the torso), lengths in body units
// (the figure is about 1.0 tall), the origin between the feet.
struct Pose
{
    float x = 0.0f, y = 0.0f;  // the whole body's offset (y up)
    float lean = 0.0f;         // torso tilt (positive = to the right)
    float head = 0.0f;         // head tilt
    float turn = 1.0f;         // −1 … 1: the body's facing (a spin squeezes it through 0)
    float squash = 1.0f;       // vertical squash on landings
    float hipShift = 0.0f;     // hips sideways
    std::array<float, 2> shoulder {}, elbow {}, hip {}, knee {}; // [0] left, [1] right (as seen)
    float tail = 0.0f;

    static Pose mix (const Pose& a, const Pose& b, float t) noexcept;
};
// `cycles`: the move's own clock (one cycle = one beat at speed 1, the song's beats while playing); `seed` varies
// Freestyle's choices. Deterministic.
Pose pose (int move, double cycles, std::uint32_t seed = 0) noexcept;
// Freestyle: the move of cycle `n` (never Waiting or Held)
int freestyleMove (std::int64_t cycleIndex, std::uint32_t seed) noexcept;

// ---------------------------------------------------------------------------------------------------------------
// Sprite sheets in FL's format
// ---------------------------------------------------------------------------------------------------------------
constexpr int sheetColumns = 8; // frames per move (FL's sheet object is made with 8)
constexpr int maxMoves = 128;

// The names file's lines (CR, LF or both end a line; the last line needs no ending; a UTF-8 mark is skipped; bytes
// that are not UTF-8 are read as Latin-1). Empty lines count: each line is a row of the picture.
std::vector<std::string> sheetNames (const std::string& text);
// FL's own ten names, used when a sheet has no names file
std::vector<std::string> defaultSheetNames();
// "C:/a/Dancer.png" -> "C:/a/Dancer.txt" (FL replaces the extension)
std::string sheetNamesPath (const std::string& picturePath);

struct SheetLayout
{
    int rows = 1;                        // the names file's lines, at least 1
    int frameWidth = 0, frameHeight = 0; // width / 8, height / rows
    bool valid = false;                  // FL's "Bitmap size is incorrect." when false
};
// FL's rule: the frame is width / 8 by height / rows (whole pixels), and the picture must be a whole number of frames
// each way.
SheetLayout sheetLayout (int width, int height, int nameLines) noexcept;

// With a sheet of `rows` rows the move value 0 is Freestyle and value v shows row v - 1; the last row is not a move:
// it is shown while the dancer is held with the mouse. So there are `rows` values (0 … rows - 1).
int sheetMoveCount (int rows) noexcept;
// The move list as FL shows it: "Freestyle", then the names without the last one
std::vector<std::string> sheetMoveNames (const std::vector<std::string>& names);
int sheetHeldRow (int rows) noexcept; // rows - 1

// What to draw at a moment: a frame, and the next one over it at `mix` (0 = none)
struct SheetFrame
{
    int row = 0, column = 0;
    int nextRow = 0, nextColumn = 0;
    float mix = 0.0f;      // the next frame's opacity over this one
    bool mirrored = false; // the whole picture flipped left-right
    bool operator== (const SheetFrame&) const = default;
};

// FL's animation of a sheet. One cycle = the row's 8 frames. The row only changes when a cycle begins ("Sync
// changes", FL's default): not playing = row 0 (FL's "Waiting"); Freestyle = a random row from the second to the
// one before the last, drawn again every cycle; else the move's row. Inside a frame the last `blend` part fades to
// the next frame (which is already the coming row's first frame at the end of a cycle).
class SheetAnimator
{
public:
    explicit SheetAnimator (std::uint32_t seed = 1) noexcept;
    // `cycles`: the dancer's clock (as `pose`); `move`: the move value; `held`: dragged with the mouse (the last row,
    // blend 0.7)
    // `sync`: FL's "Sync changes" (off: a move other than Freestyle shows at once instead of at the cycle's start)
    SheetFrame advance (double cycles, int rows, int move, bool mirrored, bool playing, float blend, bool held = false, bool sync = true) noexcept;
    int row() const noexcept { return shownRow; }

private:
    int rowFor (int rows, int move, bool playing) const noexcept;
    void draw (int rows) noexcept;
    std::uint32_t random;
    double lastPhase = 2.0; // (above any phase: the first call begins a cycle)
    int shownRow = 0, freestyleRow = 0, drawnFor = -1;
    bool shownMirrored = false;
};

// The silent instrument of a dance channel: its notes go to the UI through a small lock-free queue (the audio thread
// only stores the key and counts).
class Signals
{
public:
    void push (int key) noexcept;
    // UI: the keys since `seen` (oldest first, at most 16), `seen` moved on
    int pull (std::uint32_t& seen, int* keys, int max) const noexcept;

private:
    std::array<std::atomic<int>, 16> ring {};
    std::atomic<std::uint32_t> count { 0 };
};
std::unique_ptr<Instrument> makeInstrument();
Signals* signalsOf (Instrument* instrument) noexcept; // nullptr: not a dance channel
} // namespace lynx::dance
