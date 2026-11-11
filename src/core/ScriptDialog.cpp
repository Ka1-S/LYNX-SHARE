#include "core/ScriptDialog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>

#include "core/Python.h"
#include "core/ScriptDialogPy.h"

namespace lynx::scriptform
{
namespace
{
// The ScriptDialog class (Python side), written from FL's published script references (Edison's and the Piano roll's:
// the same class in both); LYNX's own code. `_h.execute` is the host's native (bindExecute).
constexpr const char* dialogSource = R"PY(
class ScriptDialog:
    def __init__(self, title='', description=''):
        self._title = str(title)
        self._description = str(description)
        self._group = ''
        self._controls = []
        self._values = {}
        self.restoreFormValues = True
    def _add(self, kind, name, value, lo, hi, options, hint):
        name = str(name)
        full = name if self._group == '' else self._group + ': ' + name
        self._controls.append([kind, full, value, lo, hi, options, str(hint)])
        self._values[full] = value
    def AddInput(self, name, value, hint=''):
        self._add(0, name, float(value), 0.0, 1.0, [], hint)
    def AddInputKnob(self, name, value, min, max, hint=''):
        self._add(1, name, float(value), float(min), float(max), [], hint)
    def AddInputKnobInt(self, name, value, min, max, hint=''):
        self._add(2, name, _i(value), _i(min), _i(max), [], hint)
    def AddInputCombo(self, name, valueList, value, hint=''):
        if isinstance(valueList, str):
            options = valueList.split(',')
        else:
            options = [str(x) for x in valueList]
        self._add(3, name, _i(value), 0, len(options) - 1, options, hint)
    def AddInputCheckbox(self, name, value, hint=''):
        self._add(4, name, True if value else False, 0, 1, [], hint)
    def AddInputText(self, name, value, hint=''):
        self._add(5, name, str(value), 0, 0, [], hint)
    def AddInputSurface(self, presetName):
        self._add(6, presetName, 0.0, 0.0, 0.0, [], '')
    def AddGroup(self, name):
        self._group = str(name)
    def EndGroup(self):
        self._group = ''
    def SetText(self, description):
        self._description = str(description)
    def GetInputValue(self, name):
        name = str(name)
        if name in self._values:
            return self._values[name]
        return 0
    def SetNormalizedValue(self, name, value):
        name = str(name)
        for c in self._controls:
            if c[1] == name and c[0] in (0, 1, 2, 3, 4):
                v = c[3] + float(value) * (c[4] - c[3])
                if c[0] in (2, 3):
                    v = _i(v)
                if c[0] == 4:
                    v = float(value) >= 0.5
                self._values[name] = v
    def Execute(self):
        answer = _h.execute(self._title, self._description, self._controls, self.restoreFormValues)
        for k in range(len(self._controls)):
            self._values[self._controls[k][1]] = answer[k]
        return True
    addInput = AddInput
    addInputKnob = AddInputKnob
    addInputKnobInt = AddInputKnobInt
    addInputCombo = AddInputCombo
    addInputCheckbox = AddInputCheckbox
    addInputText = AddInputText
    addInputSurface = AddInputSurface
    addGroup = AddGroup
    endGroup = EndGroup
    setText = SetText
    getInputValue = GetInputValue
    setNormalizedValue = SetNormalizedValue
    execute = Execute
)PY";

thread_local const Asker* asker = nullptr;

std::string textOf (py_Ref v)
{
    if (py_isstr (v))
        return py_tostr (v);
    if (! py_str (v))
    {
        python::takeError();
        return {};
    }
    return py_tostr (py_retval());
}

double numberOf (py_Ref v)
{
    if (py_isbool (v))
        return py_tobool (v) ? 1.0 : 0.0;
    double d = 0.0;
    if (! py_castfloat (v, &d))
    {
        python::takeError();
        return 0.0;
    }
    return d;
}

// ScriptDialog.Execute(): title, description, the controls ([kind, name, value, min, max, options, hint] each)
bool nExecute (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (4);
    if (asker == nullptr)
        return RuntimeError ("no script host");
    Form f;
    f.title = textOf (py_arg (0));
    f.description = textOf (py_arg (1));
    f.restoreValues = py_tobool (py_arg (3));
    py_TValue list = *py_arg (2);
    if (! py_islist (&list))
        return TypeError ("controls");
    for (int i = 0; i < py_list_len (&list); ++i)
    {
        py_TValue item = *py_list_getitem (&list, i);
        if (! py_islist (&item) || py_list_len (&item) < 7)
            continue;
        Control c;
        c.type = (Control::Type) std::clamp ((int) numberOf (py_list_getitem (&item, 0)), 0, 6);
        c.name = textOf (py_list_getitem (&item, 1));
        if (c.type == Control::Type::text)
            c.text = textOf (py_list_getitem (&item, 2));
        else
            c.value = numberOf (py_list_getitem (&item, 2));
        c.minimum = numberOf (py_list_getitem (&item, 3));
        c.maximum = numberOf (py_list_getitem (&item, 4));
        py_TValue options = *py_list_getitem (&item, 5);
        if (py_islist (&options))
            for (int k = 0; k < py_list_len (&options); ++k)
                c.options.push_back (textOf (py_list_getitem (&options, k)));
        c.hint = textOf (py_list_getitem (&item, 6));
        f.controls.push_back (std::move (c));
    }
    const auto controls = f.controls;
    const auto* answer = (*asker) (std::move (f));
    if (answer == nullptr)
        return RuntimeError (needsFormText);
    // the values, typed as the controls return them
    py_newlistn (py_retval(), (int) controls.size());
    py_TValue out = *py_retval();
    for (std::size_t k = 0; k < controls.size(); ++k)
    {
        const auto& c = controls[k];
        const auto* a = k < answer->controls.size() ? &answer->controls[k] : &c;
        py_TValue v;
        switch (c.type)
        {
            case Control::Type::knobInt:
            case Control::Type::combo: py_newint (&v, (py_i64) std::llround (a->value)); break;
            case Control::Type::checkbox: py_newbool (&v, a->value >= 0.5); break;
            case Control::Type::text: py_newstr (&v, a->text.c_str()); break;
            default: py_newfloat (&v, a->value); break;
        }
        py_list_setitem (&out, (int) k, &v);
    }
    py_assign (py_retval(), &out);
    return true;
}

// Python joins adjacent string literals ('doesn''t' is "doesnt"; FL's Limiter.pyscript relies on it); pocketpy does
// not, so plain literals next to each other on one line become one before the script runs
std::string joinAdjacentStrings (const std::string& s)
{
    std::string out;
    out.reserve (s.size());
    std::size_t i = 0;
    auto isIdent = [] (char c) { return std::isalnum ((unsigned char) c) != 0 || c == '_'; };
    while (i < s.size())
    {
        const char c = s[i];
        if (c == '#') // a comment, to the end of the line
        {
            const auto e = s.find ('\n', i);
            const auto end = e == std::string::npos ? s.size() : e;
            out.append (s, i, end - i);
            i = end;
            continue;
        }
        if (c != '\'' && c != '"')
        {
            out += c;
            ++i;
            continue;
        }
        const bool prefixed = i > 0 && isIdent (s[i - 1]);
        if (s.compare (i, 3, std::string (3, c)) == 0) // a triple-quoted string: copied as it is
        {
            const auto e = s.find (std::string (3, c), i + 3);
            const auto end = e == std::string::npos ? s.size() : e + 3;
            out.append (s, i, end - i);
            i = end;
            continue;
        }
        // a one-line string
        std::size_t j = i + 1;
        while (j < s.size() && s[j] != c && s[j] != '\n')
            j += s[j] == '\\' ? 2 : 1;
        if (j >= s.size() || s[j] != c)
        {
            out.append (s, i, std::min (j, s.size()) - i);
            i = std::min (j, s.size());
            continue;
        }
        out.append (s, i, j - i); // (the closing quote is written below, unless the next literal joins this one)
        i = j + 1;
        for (;;)
        {
            std::size_t k = i;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t'))
                ++k;
            if (prefixed || k >= s.size() || (s[k] != '\'' && s[k] != '"') || s.compare (k, 3, std::string (3, s[k])) == 0)
                break;
            const char q = s[k];
            std::size_t m = k + 1;
            while (m < s.size() && s[m] != q && s[m] != '\n')
                m += s[m] == '\\' ? 2 : 1;
            if (m >= s.size() || s[m] != q)
                break;
            // its text inside this literal's quotes (an unescaped quote of ours gets a backslash)
            for (std::size_t x = k + 1; x < m; ++x)
            {
                if (s[x] == '\\')
                {
                    out.append (s, x, 2);
                    ++x;
                    continue;
                }
                if (s[x] == c)
                    out += '\\';
                out += s[x];
            }
            i = m + 1;
        }
        out += c;
    }
    return out;
}

std::mutex memoryLock;
std::map<std::string, Form> memory;
} // namespace

const char* dialogPython()
{
    return dialogSource;
}

void bindExecute (py_GlobalRef hostModule)
{
    py_bindfunc (hostModule, "execute", nExecute);
}

void setAsker (const Asker* a)
{
    asker = a;
}

std::string normalized (const std::string& source)
{
    std::string s;
    s.reserve (source.size() + 1);
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] == '\r')
        {
            if (i + 1 < source.size() && source[i + 1] == '\n')
                continue;
            s += '\n';
            continue;
        }
        s += source[i];
    }
    if (s.rfind ("\xEF\xBB\xBF", 0) == 0)
        s.erase (0, 3);
    s += '\n';
    return joinAdjacentStrings (s);
}

bool isNeedsForm (const std::string& error)
{
    return error.find (needsFormText) != std::string::npos;
}

void remember (const std::string& scriptKey, const Form& form)
{
    std::lock_guard<std::mutex> l (memoryLock);
    memory[scriptKey + "\n" + form.title] = form;
}

void restore (const std::string& scriptKey, Form& form)
{
    if (! form.restoreValues)
        return;
    std::lock_guard<std::mutex> l (memoryLock);
    const auto it = memory.find (scriptKey + "\n" + form.title);
    if (it == memory.end())
        return;
    for (auto& c : form.controls)
        for (const auto& old : it->second.controls)
            if (old.name == c.name && old.type == c.type)
            {
                c.value = std::clamp (old.value, std::min (c.minimum, c.maximum), std::max (c.minimum, c.maximum));
                if (c.type == Control::Type::value || c.type == Control::Type::text)
                {
                    c.value = old.value;
                    c.text = old.text;
                }
            }
}
} // namespace lynx::scriptform
