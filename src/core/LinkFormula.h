#pragma once
// FL's "Mapping formula" of a controller link (form TMIDIInputForm "Remote control settings", FormulaEdit "Input to
// output mapping formula"; round 190bt, docs/analise/REA_rodada190bt.md): an expression over the variable `Input`
// (the controller's position, 0…1) whose result, limited to 0…1, is the linked control's position. An empty formula or
// just "Input" (any letter case) is FL's "(default 1:1 mapping)".
//
// Clean-room: the function names, their argument counts and what each returns were read from FL's behaviour
// (static study); the parser and evaluator are LYNX's own.
#include <memory>
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
} // namespace lynx::linkformula
