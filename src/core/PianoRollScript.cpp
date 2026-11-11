#include "core/PianoRollScript.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/Python.h"
#include "core/ScriptDialogPy.h"
#include "pocketpy.h"

namespace lynx::prscript
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------
// The flpianoroll module (Python side), written from FL's published Piano roll script reference and the behaviour of
// FL's engine (docs/analise/REA_rodada190as-pyscript.md); LYNX's own code. `_data` (the score as the run starts) is
// set by the host before this source runs; _export() hands the result back.
constexpr const char* moduleSource = R"PY(
import _lynxpr as _h

def _i(x):
    if isinstance(x, float):
        return int(round(x))
    return int(x)

def _typename(v):
    return type(v).__name__

# FL's members are typed (CPython member slots): an int member refuses a float, a bool member anything but a bool
def _int(v):
    if v is True:
        return 1
    if v is False:
        return 0
    if not isinstance(v, int):
        raise TypeError("'" + _typename(v) + "' object cannot be interpreted as an integer")
    return v

def _float(v):
    if v is True:
        return 1.0
    if v is False:
        return 0.0
    if not isinstance(v, (int, float)):
        raise TypeError('must be real number, not ' + _typename(v))
    return float(v)

def _bool(v):
    if v is not True and v is not False:
        raise TypeError('attribute value type must be bool')
    return v

def _member(index, convert):
    def get(self):
        return self._v[index]
    def set(self, value):
        self._v[index] = convert(value)
    return property(get, set)

class Note:
    def __init__(self):
        self._v = [0, 0, 0, 0, 0.5, 0.78125, 0.5, 0, 128 / 255, 128 / 255, 0, False, False, False, False, 0]
        self._o = -1
    number = _member(0, _int)
    time = _member(1, _int)
    length = _member(2, _int)
    group = _member(3, _int)
    pan = _member(4, _float)
    velocity = _member(5, _float)
    release = _member(6, _float)
    color = _member(7, _int)
    fcut = _member(8, _float)
    fres = _member(9, _float)
    pitchofs = _member(10, _int)
    slide = _member(11, _bool)
    porta = _member(12, _bool)
    muted = _member(13, _bool)
    selected = _member(14, _bool)
    repeats = _member(15, _int)
    def clone(self):
        n = Note()
        n._v = self._v[:]
        n._o = self._o
        return n

def _text(v):
    if not isinstance(v, str):
        raise TypeError('must be str, not ' + _typename(v))
    return v

class Marker:
    def __init__(self):
        self._v = [0, '', 0, 4, 4, 0, '0,0,0,0,0,0,0,0,0,0,0,0']
    time = _member(0, _int)
    name = _member(1, _text)
    mode = _member(2, _int)
    tsnum = _member(3, _int)
    tsden = _member(4, _int)
    scale_root = _member(5, _int)
    scale_helper = _member(6, _text)

class Pattern:
    def __init__(self):
        d = _data
        self._ppq = d[0]
        self._tsnum = d[1]
        self._tsden = d[2]
        self._hassel = d[3]
        self._sel = [d[4], d[5]]
        self._notes = []
        for t in d[6]:
            n = Note()
            n._v = list(t[:16])
            n._o = t[16]
            self._notes.append(n)
        self._markers = []
        for t in d[7]:
            m = Marker()
            m._v = list(t)
            self._markers.append(m)
        self._defaults = list(d[8])
        self._groups = d[9]
        self._root = d[10]
        self._scale = d[11]
    PPQ = property(lambda self: self._ppq)
    tsnum = property(lambda self: self._tsnum)
    tsden = property(lambda self: self._tsden)
    noteCount = property(lambda self: len(self._notes))
    markerCount = property(lambda self: len(self._markers))
    snap_root_note = property(lambda self: self._root)
    snap_scale_helper = property(lambda self: self._scale)
    def _length(self):
        end = 0
        for n in self._notes:
            end = max(end, n.time + n.length)
        return end
    length = property(_length)
    def getNote(self, index):
        index = _int(index)
        if index < 0 or index >= len(self._notes):
            return None
        return self._notes[index]
    def addNote(self, note):
        if not isinstance(note, Note):
            raise TypeError('addNote() argument must be flpianoroll.Note, not ' + _typename(note))
        self._notes.append(note)
    def deleteNote(self, index):
        index = _int(index)
        if index < 0 or index >= len(self._notes):
            raise IndexError('Invalid note index')
        del self._notes[index]
    def getMarker(self, index):
        index = _int(index)
        if index < 0 or index >= len(self._markers):
            return None
        return self._markers[index]
    def addMarker(self, marker):
        if not isinstance(marker, Marker):
            raise TypeError('addMarker() argument must be flpianoroll.Marker, not ' + _typename(marker))
        self._markers.append(marker)
    def deleteMarker(self, index):
        index = _int(index)
        if index < 0 or index >= len(self._markers):
            raise IndexError('Invalid marker index')
        del self._markers[index]
    def clearNotes(self, all=False):
        # with notes selected when the script started, only the notes still selected go (unless `all`)
        if not all and self._hassel:
            self._notes = [n for n in self._notes if not n.selected]
        else:
            self._notes = []
    def clearMarkers(self, all=False):
        if all:
            self._markers = []
            return
        # the markers inside the time range of the notes (FL: of the notes clearNotes() would remove)
        lo = 0x7fffffff
        hi = 0
        for n in self._notes:
            if not self._hassel or n.selected:
                lo = min(lo, n.time)
                hi = max(hi, n.time)
        if lo > hi:
            lo, hi = hi, lo
        self._markers = [m for m in self._markers if m.time < lo or m.time > hi]
    def clear(self, all=False):
        self.clearMarkers(all)
        self.clearNotes(all)
    def getTimelineSelection(self):
        return (self._sel[0], self._sel[1])
    def setTimelineSelection(self, x1, x2):
        self._sel = [_int(x1), _int(x2)]
    def getDefaultNoteProperties(self):
        n = Note()
        n._v = self._defaults[:]
        return n
    def getNextFreeGroupIndex(self):
        used = set(self._groups)
        for n in self._notes:
            used.add(n.group)
        index = 1
        while index in used and index < 0xffff:
            index += 1
        return index
    def loadScoreFromFile(self, *args):
        raise RuntimeError('loadScoreFromFile is not available in LYNX SHARE (scripts cannot read files)')

class TUtils:
    def ShowMessage(self, msg):
        _h.msg(str(msg))
    def ProgressMsg(self, msg, pos, total):
        pass
    def log(self, msg):
        _h.log(str(msg))

score = Pattern()
Utils = TUtils()

def _export():
    out = [len(score._notes)]
    for n in score._notes:
        out.extend(n._v)
        out.append(n._o)
    out.append(len(score._markers))
    for m in score._markers:
        out.extend(m._v)
    out.append(score._sel[0])
    out.append(score._sel[1])
    return out

def _drive(create, apply):
    form = create()
    if form is None:
        raise RuntimeError('no dialog was created in createDialog')
    if not isinstance(form, ScriptDialog):
        raise TypeError('createDialog function needs to return a ScriptDialog instance')
    if form.Execute() and apply is not None:
        apply(form)
)PY";

// ------------------------------------------------------------------------------------------------- the run's state
struct Context
{
    const std::vector<Form>* answers = nullptr;
    std::size_t asked = 0;
    bool needsForm = false;
    Form captured;
    std::vector<std::string> messages;
    std::string* log = nullptr;
};
thread_local Context* ctx = nullptr;

bool nMsg (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    PY_CHECK_ARG_TYPE (0, tp_str);
    if (ctx != nullptr)
        ctx->messages.emplace_back (py_tostr (py_arg (0)));
    py_newnone (py_retval());
    return true;
}

bool nLog (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    PY_CHECK_ARG_TYPE (0, tp_str);
    if (ctx != nullptr && ctx->log != nullptr)
    {
        *ctx->log += py_tostr (py_arg (0));
        *ctx->log += '\n';
    }
    py_newnone (py_retval());
    return true;
}

// --- the score as Python text (the module reads it as `_data`) ---------------------------------------------------
std::string quoted (const std::string& s)
{
    std::string out = "'";
    for (const char c : s)
    {
        switch (c)
        {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': break;
            default: out += c; break;
        }
    }
    return out + "'";
}

std::string real (double v)
{
    if (! std::isfinite (v))
        v = 0.0;
    char buf[40];
    std::snprintf (buf, sizeof buf, "%.9g", v);
    std::string s = buf;
    if (s.find_first_of (".en") == std::string::npos)
        s += ".0";
    return s;
}

const char* truth (bool b)
{
    return b ? "True" : "False";
}

std::string noteText (const ScriptNote& n)
{
    std::string s = "(";
    s += std::to_string (n.number) + "," + std::to_string (n.time) + "," + std::to_string (n.length) + "," + std::to_string (n.group) + ",";
    s += real (n.pan) + "," + real (n.velocity) + "," + real (n.release) + "," + std::to_string (n.color) + ",";
    s += real (n.fcut) + "," + real (n.fres) + "," + std::to_string (n.pitchofs) + ",";
    s += std::string (truth (n.slide)) + "," + truth (n.porta) + "," + truth (n.muted) + "," + truth (n.selected) + ",";
    s += std::to_string (n.repeats) + "," + std::to_string (n.origin) + ")";
    return s;
}

std::string dataText (const Score& sc)
{
    std::string s = "_data = (";
    s += std::to_string (sc.ppq) + "," + std::to_string (sc.tsnum) + "," + std::to_string (sc.tsden) + "," + truth (sc.hasSelection) + ",";
    s += std::to_string (sc.selectionStart) + "," + std::to_string (sc.selectionEnd) + ",[";
    for (const auto& n : sc.notes)
        s += noteText (n) + ",";
    s += "],[";
    for (const auto& m : sc.markers)
        s += "(" + std::to_string (m.time) + "," + quoted (m.name) + "," + std::to_string (m.mode) + "," + std::to_string (m.tsnum) + ","
             + std::to_string (m.tsden) + "," + std::to_string (m.scaleRoot) + "," + quoted (m.scaleHelper) + "),";
    s += "],";
    auto d = sc.defaults;
    d.origin = -1;
    const auto dt = noteText (d);
    s += dt.substr (0, dt.rfind (',')) + "),["; // (without the origin)
    for (const int g : sc.otherGroups)
        s += std::to_string (g) + ",";
    s += "]," + std::to_string (sc.snapRoot) + "," + quoted (sc.snapScale) + ")\n";
    return s;
}

// --- reading the result -------------------------------------------------------------------------------------------
struct Reader
{
    py_TValue list;
    int at = 0, size = 0;
    bool ok = true;

    py_Ref next()
    {
        if (at >= size)
        {
            ok = false;
            return nullptr;
        }
        return py_list_getitem (&list, at++);
    }
    std::int64_t integer()
    {
        auto* v = next();
        if (v == nullptr)
            return 0;
        if (py_isbool (v))
            return py_tobool (v) ? 1 : 0;
        if (py_isint (v))
            return py_toint (v);
        double d = 0.0;
        if (py_isfloat (v) && py_castfloat (v, &d) && std::isfinite (d))
            return (std::int64_t) std::llround (d);
        ok = false;
        return 0;
    }
    double number()
    {
        auto* v = next();
        if (v == nullptr)
            return 0.0;
        if (py_isbool (v))
            return py_tobool (v) ? 1.0 : 0.0;
        double d = 0.0;
        if ((py_isint (v) || py_isfloat (v)) && py_castfloat (v, &d))
            return std::isfinite (d) ? d : 0.0;
        ok = false;
        return 0.0;
    }
    bool flag()
    {
        auto* v = next();
        if (v == nullptr || ! py_isbool (v))
        {
            ok = v != nullptr && ok;
            return v != nullptr && py_isint (v) && py_toint (v) != 0;
        }
        return py_tobool (v);
    }
    std::string text()
    {
        auto* v = next();
        if (v == nullptr || ! py_isstr (v))
        {
            ok = false;
            return {};
        }
        return py_tostr (v);
    }
};

int narrow (std::int64_t v)
{
    return (int) std::clamp<std::int64_t> (v, INT32_MIN, INT32_MAX);
}

bool readResult (Score& sc, std::string& error)
{
    py_GlobalRef module = py_getmodule ("flpianoroll");
    py_ItemRef fn = module != nullptr ? py_getdict (module, py_name ("_export")) : nullptr;
    if (fn == nullptr)
    {
        error = "flpianoroll is not loaded";
        return false;
    }
    py_TValue f = *fn;
    if (! py_call (&f, 0, nullptr))
    {
        error = python::takeError();
        return false;
    }
    Reader r;
    r.list = *py_retval();
    if (! py_islist (&r.list))
    {
        error = "the score could not be read";
        return false;
    }
    r.size = py_list_len (&r.list);
    std::vector<ScriptNote> notes;
    const auto count = r.integer();
    for (std::int64_t i = 0; i < count && r.ok; ++i)
    {
        ScriptNote n;
        n.number = narrow (r.integer());
        n.time = r.integer();
        n.length = r.integer();
        n.group = narrow (r.integer());
        n.pan = r.number();
        n.velocity = r.number();
        n.release = r.number();
        n.color = narrow (r.integer());
        n.fcut = r.number();
        n.fres = r.number();
        n.pitchofs = narrow (r.integer());
        n.slide = r.flag();
        n.porta = r.flag();
        n.muted = r.flag();
        n.selected = r.flag();
        n.repeats = narrow (r.integer());
        n.origin = narrow (r.integer());
        notes.push_back (n);
    }
    std::vector<ScriptMarker> markers;
    const auto markerCount = r.integer();
    for (std::int64_t i = 0; i < markerCount && r.ok; ++i)
    {
        ScriptMarker m;
        m.time = r.integer();
        m.name = r.text();
        m.mode = narrow (r.integer());
        m.tsnum = narrow (r.integer());
        m.tsden = narrow (r.integer());
        m.scaleRoot = narrow (r.integer());
        m.scaleHelper = r.text();
        markers.push_back (std::move (m));
    }
    const auto x1 = r.integer(), x2 = r.integer();
    if (! r.ok)
    {
        error = "the score could not be read";
        return false;
    }
    sc.notes = std::move (notes);
    sc.markers = std::move (markers);
    sc.selectionStart = x1;
    sc.selectionEnd = x2;
    return true;
}

bool installModule (const Score& score, std::string& error)
{
    py_GlobalRef host = py_newmodule ("_lynxpr");
    py_bindfunc (host, "msg", nMsg);
    py_bindfunc (host, "log", nLog);
    scriptform::bindExecute (host);
    py_GlobalRef module = py_newmodule ("flpianoroll");
    const auto source = dataText (score) + moduleSource + scriptform::dialogPython();
    if (! py_exec (source.c_str(), "flpianoroll", EXEC_MODE, module))
    {
        error = python::takeError();
        return false;
    }
    return true;
}

const Form* answerOrAsk (Form&& form)
{
    if (ctx->answers != nullptr && ctx->asked < ctx->answers->size())
        return &(*ctx->answers)[ctx->asked++];
    ctx->needsForm = true;
    ctx->captured = std::move (form);
    return nullptr;
}

// FL's byte stores (REA §4): value * scale rounded into 0..255, read back as value / scale
double stored (double v, double scale)
{
    return std::clamp (std::round (v * scale), 0.0, 255.0) / scale;
}
} // namespace

Outcome run (const std::string& source, Score& score, const std::vector<Form>& answers, double timeLimitMs)
{
    Outcome out;
    Context c;
    c.answers = &answers;
    c.log = &out.log;
    python::runSync ([&] {
        const auto vm = python::acquireVm();
        if (vm < 0)
        {
            out.error = "Python is busy (too many scripts are running).";
            return;
        }
        py_setvmctx (nullptr);
        python::setPrint ([&out] (const std::string& t) { out.log += t; });
        ctx = &c;
        const scriptform::Asker ask = [] (Form&& f) { return answerOrAsk (std::move (f)); };
        scriptform::setAsker (&ask);
        std::string error;
        bool ok = installModule (score, error);
        if (ok)
        {
            python::setTimeLimit (timeLimitMs);
            const auto text = scriptform::normalized (source);
            ok = py_exec (text.c_str(), "<script>", EXEC_MODE, nullptr);
            if (ok && py_getglobal (py_name ("createDialog")) != nullptr)
            {
                // FL's preview style: the form createDialog() returns, then apply(form) with its values
                const bool hasApply = py_getglobal (py_name ("apply")) != nullptr;
                const std::string driver = std::string ("import flpianoroll as __lynx_fl\n__lynx_fl._drive(createDialog, ") + (hasApply ? "apply" : "None") + ")\n";
                ok = py_exec (driver.c_str(), "<script>", EXEC_MODE, nullptr);
            }
            if (! ok)
                error = python::takeError();
            else if (! c.needsForm)
            {
                ok = readResult (score, error);
            }
            python::setTimeLimit (0.0);
        }
        if (c.needsForm)
        {
            out.status = Outcome::Status::needsForm;
            out.form = c.captured;
        }
        else if (! ok)
        {
            out.status = Outcome::Status::failed;
            out.error = std::move (error);
        }
        else
            out.status = Outcome::Status::done;
        scriptform::setAsker (nullptr);
        ctx = nullptr;
        python::setPrint (nullptr);
        python::releaseVm (vm);
    });
    out.messages = std::move (c.messages);
    return out;
}

// ----------------------------------------------------------------------------------------------- pattern <-> score
namespace
{
ScriptNote scriptNoteOf (const Note& n)
{
    ScriptNote s;
    s.number = n.key;
    s.time = n.start;
    s.length = n.length;
    s.group = n.group;
    s.pan = std::clamp (((double) n.pan + 1.0) * 0.5, 0.0, 1.0);
    s.velocity = n.velocity;
    s.release = n.release;
    s.color = n.colour & 15;
    s.fcut = n.modX;
    s.fres = n.modY;
    s.pitchofs = (int) std::lround (n.finePitch);
    s.slide = n.slide;
    s.porta = n.porta;
    s.muted = n.muted;
    return s;
}
} // namespace

Score scoreOf (const Pattern& pattern, int channel, const std::vector<int>& selection, int beatsPerBar, int beatUnit, std::int64_t timeFrom,
               std::int64_t timeTo, const Note& drawDefaults)
{
    Score sc;
    sc.tsnum = beatsPerBar > 0 ? beatsPerBar : 4;
    sc.tsden = beatUnit > 0 ? beatUnit : 4;
    std::vector<char> selected (pattern.notes.size(), 0);
    for (const int i : selection)
        if (i >= 0 && (std::size_t) i < pattern.notes.size() && pattern.notes[(std::size_t) i].channel == channel)
        {
            selected[(std::size_t) i] = 1;
            sc.hasSelection = true;
        }
    for (std::size_t i = 0; i < pattern.notes.size(); ++i)
    {
        const auto& n = pattern.notes[i];
        const bool seen = n.channel == channel && (! sc.hasSelection || selected[i] != 0);
        if (! seen)
        {
            if (n.group > 0 && std::find (sc.otherGroups.begin(), sc.otherGroups.end(), n.group) == sc.otherGroups.end())
                sc.otherGroups.push_back (n.group);
            continue;
        }
        auto s = scriptNoteOf (n);
        s.selected = selected[i] != 0;
        s.origin = (int) sc.source.size();
        sc.notes.push_back (s);
        sc.sourceIndex.push_back ((int) i);
        sc.source.push_back (n);
    }
    for (const auto& m : pattern.markers)
    {
        ScriptMarker s;
        s.time = m.tick;
        s.name = m.name;
        s.mode = m.isSignature() ? Marker::timeSignature : m.action;
        s.tsnum = m.isSignature() ? m.numerator : 4;
        s.tsden = m.isSignature() ? m.denominator : 4;
        sc.markers.push_back (std::move (s));
    }
    if (timeTo > timeFrom)
    {
        sc.selectionStart = timeFrom;
        sc.selectionEnd = timeTo;
    }
    sc.defaults = scriptNoteOf (drawDefaults);
    sc.defaults.time = 0;
    return sc;
}

std::vector<int> apply (Pattern& pattern, int channel, const Score& score)
{
    // the notes the script saw go; its notes come in
    std::vector<char> gone (pattern.notes.size(), 0);
    for (const int i : score.sourceIndex)
        if (i >= 0 && (std::size_t) i < gone.size())
            gone[(std::size_t) i] = 1;
    std::vector<std::pair<Note, bool>> all;
    all.reserve (pattern.notes.size() + score.notes.size());
    for (std::size_t i = 0; i < pattern.notes.size(); ++i)
        if (gone[i] == 0)
            all.emplace_back (pattern.notes[i], false);
    for (const auto& s : score.notes)
    {
        Note n;
        if (s.origin >= 0 && (std::size_t) s.origin < score.source.size())
            n = score.source[(std::size_t) s.origin]; // (keeps what scripts do not know: chance, velocity range...)
        n.channel = channel;
        n.key = std::clamp (s.number, 0, 127);
        n.start = std::clamp<std::int64_t> (s.time, 0, INT32_MAX);
        n.length = std::clamp<std::int64_t> (s.length, 1, INT32_MAX); // (FL keeps 0; a LYNX note is at least a tick long)
        n.group = std::clamp (s.group, 0, 0xffff);
        // FL stores pan, velocity and release as value * 128 in a byte, Mod X / Y as value * 255 (REA §4)
        n.pan = (float) (std::clamp (stored (s.pan, 128.0), 0.0, 1.0) * 2.0 - 1.0);
        n.velocity = (float) std::clamp (stored (s.velocity, 128.0), 0.0, 1.0);
        n.release = (float) std::clamp (stored (s.release, 128.0), 0.0, 1.0);
        n.colour = s.color & 15;
        n.modX = (float) std::clamp (stored (s.fcut, 255.0), 0.0, 1.0);
        n.modY = (float) std::clamp (stored (s.fres, 255.0), 0.0, 1.0);
        // FL: the byte is |pitchofs + 120|
        n.finePitch = (float) (std::clamp (std::abs (s.pitchofs + 120), 0, 240) - 120);
        n.slide = s.slide;
        n.porta = s.porta;
        n.muted = s.muted;
        all.emplace_back (n, s.selected);
    }
    std::stable_sort (all.begin(), all.end(), [] (const auto& a, const auto& b) { return a.first.start < b.first.start; });
    pattern.notes.clear();
    std::vector<int> selected;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        pattern.notes.push_back (all[i].first);
        if (all[i].second)
            selected.push_back ((int) i);
    }

    pattern.markers.clear();
    for (const auto& s : score.markers)
    {
        Marker m;
        m.tick = std::clamp<std::int64_t> (s.time, 0, INT32_MAX);
        m.name = s.name;
        m.action = std::clamp (s.mode, 0, 12); // (FL clamps the mode to 0..12)
        if (m.action == Marker::timeSignature)
        {
            m.numerator = std::clamp (s.tsnum, 1, 64);
            m.denominator = std::clamp (s.tsden, 1, 64);
        }
        pattern.markers.push_back (std::move (m));
    }
    std::stable_sort (pattern.markers.begin(), pattern.markers.end(), [] (const Marker& a, const Marker& b) { return a.tick < b.tick; });
    return selected;
}

// ------------------------------------------------------------------------------------------------- LYNX's scripts
const std::vector<Builtin>& builtins()
{
    // LYNX's own small scripts (not FL's): examples of the API that also do something useful
    static const std::vector<Builtin> list = {
        { "Velocity ramp", R"PY(import flpianoroll as flp

def createDialog():
    form = flp.ScriptDialog('Velocity ramp', 'Fades the velocity of the notes from one level to another over their time span.')
    form.AddInputKnob('Start velocity', 0.3, 0, 1)
    form.AddInputKnob('End velocity', 1, 0, 1)
    form.AddInputKnob('Curve', 0, -1, 1, hint='Below 0 the change comes late, above 0 early')
    return form

def apply(form):
    notes = [flp.score.getNote(i) for i in range(flp.score.noteCount)]
    if not notes:
        return
    first = min(n.time for n in notes)
    span = max(n.time for n in notes) - first
    a = form.GetInputValue('Start velocity')
    b = form.GetInputValue('End velocity')
    power = 2 ** (-2 * form.GetInputValue('Curve'))
    for n in notes:
        x = (n.time - first) / span if span > 0 else 1
        n.velocity = a + (b - a) * x ** power
)PY" },
        { "Octave layers", R"PY(import flpianoroll as flp

def createDialog():
    form = flp.ScriptDialog('Octave layers', 'Adds a copy of every note some octaves above or below it.')
    form.AddInputKnobInt('Octaves', 1, -4, 4)
    form.AddInputKnob('Layer velocity', 0.7, 0, 1, hint='Velocity of the copies, relative to their note')
    form.AddInputCheckbox('Colour the copies', False)
    return form

def apply(form):
    shift = 12 * form.GetInputValue('Octaves')
    if shift == 0:
        return
    for note in [flp.score.getNote(i) for i in range(flp.score.noteCount)]:
        number = note.number + shift
        if number < 0 or number > 127:
            continue
        copy = note.clone()
        copy.number = number
        copy.velocity = note.velocity * form.GetInputValue('Layer velocity')
        if form.GetInputValue('Colour the copies'):
            copy.color = (note.color + 1) & 15
        flp.score.addNote(copy)
)PY" },
        { "Mirror in time", R"PY(import flpianoroll as flp

# plays the notes backwards: each note's end becomes its start, mirrored inside the notes' time span
notes = [flp.score.getNote(i) for i in range(flp.score.noteCount)]
if notes:
    first = min(n.time for n in notes)
    last = max(n.time + n.length for n in notes)
    for n in notes:
        n.time = first + last - (n.time + n.length)
)PY" },
        { "Thin out", R"PY(import flpianoroll as flp
import random

def createDialog():
    form = flp.ScriptDialog('Thin out', 'Removes or mutes some of the notes.')
    form.AddInputCombo('Which', ['Every 2nd', 'Every 3rd', 'Every 4th', 'At random'], 0)
    form.AddInputKnob('Random amount', 0.3, 0, 1, hint='Share of the notes that go (At random)')
    form.AddInputKnobInt('Seed', 1, 1, 99)
    form.AddInputCheckbox('Mute instead of delete', True)
    return form

def apply(form):
    which = form.GetInputValue('Which')
    random.seed(form.GetInputValue('Seed'))
    amount = form.GetInputValue('Random amount')
    mute = form.GetInputValue('Mute instead of delete')
    notes = [flp.score.getNote(i) for i in range(flp.score.noteCount)]
    notes.sort(key=lambda n: (n.time, n.number))
    out = []
    for k in range(len(notes)):
        if which == 3:
            hit = random.random() < amount
        else:
            hit = k % (which + 2) == which + 1
        if hit:
            out.append(notes[k])
    for n in out:
        if mute:
            n.muted = True
        else:
            for i in range(flp.score.noteCount):
                if flp.score.getNote(i) is n:
                    flp.score.deleteNote(i)
                    break
)PY" },
    };
    return list;
}
} // namespace lynx::prscript
