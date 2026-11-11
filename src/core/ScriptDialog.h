#pragma once

// The form of a script's ScriptDialog (FL's class of that name, the same in Edison's audio scripts and the Piano
// roll's scripts): the controls a script asks for, what the user answered, and the last answers per script. Shared by
// core/EdisonScript.h and core/PianoRollScript.h (round 190as; the code came from round 167c's EdisonScript.cpp).
//
// The Python thread never waits for the user: a script that reaches ScriptDialog.Execute() without an answer stops
// there (the run reports the form), and is run again from the start with the answers.

#include <functional>
#include <string>
#include <vector>

namespace lynx::scriptform
{
struct Control
{
    enum class Type
    {
        value,    // AddInput: a number (0..1 by default)
        knob,     // AddInputKnob: a float in min..max
        knobInt,  // AddInputKnobInt
        combo,    // AddInputCombo: the option's index
        checkbox, // AddInputCheckbox
        text,     // AddInputText
        surface   // AddInputSurface (a Control Surface preset): not available in LYNX, shown as a note
    };
    Type type = Type::value;
    std::string name, hint; // name: "Group: Label" inside a group
    double value = 0.0, minimum = 0.0, maximum = 1.0;
    std::vector<std::string> options;
    std::string text;
};

struct Form
{
    std::string title, description;
    std::vector<Control> controls;
    bool restoreValues = true; // ScriptDialog.restoreFormValues: the last run's values come back
};

// a form the user answers between two runs: the answer when there is one, else null (the run stops for the form)
using Asker = std::function<const Form* (Form&&)>;
// (Python thread) who answers ScriptDialog.Execute() of the script running now; null when none runs
void setAsker (const Asker* asker);

// The script's text as LYNX's Python takes it: line ends, no BOM, adjacent string literals joined
std::string normalized (const std::string& source);

// the error text of a run that stopped for a form
inline constexpr const char* needsFormText = "__lynx_needs_form__";
bool isNeedsForm (const std::string& error);

// The last values a form had (per script key), for restoreFormValues; remember() after the user confirms
void remember (const std::string& scriptKey, const Form& form);
void restore (const std::string& scriptKey, Form& form);
} // namespace lynx::scriptform
