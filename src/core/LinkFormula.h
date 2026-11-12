#pragma once
// FL's "Mapping formula" of a controller link (form TMIDIInputForm "Remote control settings", FormulaEdit "Input to
// output mapping formula"; round 190bt, docs/analise/REA_rodada190bt.md): an expression over the variable `Input`
// (the controller's position, 0…1) whose result, limited to 0…1, is the linked control's position. An empty formula or
// just "Input" (any letter case) is FL's "(default 1:1 mapping)".
//
// Clean-room: the function names, their argument counts and what each returns were read from FL's behaviour
// (static study); the parser and evaluator are LYNX's own.
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lynx::linkformula
{
// Is this text FL's default mapping (empty, or the single word Input)?
bool isDefaultMapping (const std::string& text);

class Formula
{
public:
    Formula() = default; // the default 1:1 mapping

    // Compiles `text`. On a mistake the result is the default mapping, `error` says what is wrong and `errorAt` is the
    // 0-based character where it was noticed (-1 when the text compiled, -2 for the default mapping, as FL reports).
    static Formula compile (const std::string& text, std::string* error = nullptr, int* errorAt = nullptr);

    bool isDefault() const noexcept { return program == nullptr; }
    // The expression's value for this input (the input itself for the default mapping).
    double evaluate (double input) const;
    // The control's position: evaluate(), limited to 0…1 (not-a-number gives 0).
    float map (float input) const;

private:
    struct Program;
    std::shared_ptr<const Program> program;
};

// FL's Tension(x, t): x bent towards the top (t > 0) or the bottom (t < 0); t = 0 leaves x alone.
double tension (double x, double t);

// The formula presets of FL's FormulaMenu, in its order. `header` rows are the menu's section titles;
// `separatorBefore` marks a dividing line above the row.
struct Preset
{
    const char* name;
    const char* formula; // nullptr = a section title
    bool separatorBefore = false;
};
const std::vector<Preset>& presets();

// FL's smoothing slider (SmoothSpeedWheel, -1024…1024, default 469): the time in milliseconds its hint shows, and
// the filter's number of poles (1 for negative positions, 3 for positive ones). Position 0 = no smoothing.
constexpr int smoothingMin = -1024, smoothingMax = 1024, smoothingDefault = 469;
double smoothingMs (int position);
int smoothingPoles (int position);

// A link's smoothing filter: `poles` equal one-pole low-passes in a row, each with the time constant time / poles.
struct Smoother
{
    double stage[3] {};
    bool started = false;

    // Moves towards `target` over `elapsedMs`; returns the smoothed position.
    float step (float target, double elapsedMs, int position);
    bool settled (float target) const noexcept;
};

// FL's link "Mode" (NoteLinkModeBox "How to treat a note or button link": Default / Hold / Latch / Inc / Stay; round
// 190cs, docs/analise/REA_rodada190bt.md section 7). The mode is stored in the link's flags and read by FL's link
// dispatcher [H 0xfdaec0]: Default and Hold pass every event on as it is (a note: velocity / 127 while held, 0 on its
// release); Latch, Inc and Stay act on presses only (an input above 0) and ignore releases. Latch switches to whichever
// of f(0) and f(1) is farther from the control's position; Inc adds f(resolution) [H 0x1051370]; Stay sets f(input).
enum class LinkMode : int
{
    standard = 0, // "Default": "Preserve controller default mode"
    hold,         // "Hold to keep at velocity"
    latch,        // "Press again to switch"
    inc,          // "Increment value"
    stay          // "Keep at velocity"
};
constexpr int linkModeCount = 5;
// The increment an event carries unless its device says otherwise: FL's event record starts at 1/128 [H 0xfd82b0].
constexpr double incrementResolution = 1.0 / 128.0;
// What one event does to the linked control: `input` 0...1 (a controller's position, a note's velocity / 127, 0 for a
// release); `current` the control's position now. Nothing = the event is ignored. The result is limited to 0...1.
std::optional<float> linkModeValue (int mode, const Formula& formula, float input, float current);
// Does the mode act on every event (as a plain controller does), or on presses only?
inline bool linkModeIsAbsolute (int mode) noexcept { return mode <= (int) LinkMode::hold || mode >= linkModeCount; }
} // namespace lynx::linkformula
