#include "core/EdisonScript.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <random>

#include "core/Python.h"
#include "pocketpy.h"

namespace lynx::audioedit::script
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------
// The enveditor module (Python side), written from FL's published Edison Script Reference; LYNX's own code. The host
// part (_lynxedison) is native and works on the document and the script's own samples.
constexpr const char* enveditorSource = R"PY(
import _lynxedison as _h

NF_NumChannels = 1
NF_Format = 2
NF_Samplerate = 4
NF_All = 7
nfNumChannels = 1
nfFormat = 2
nfSamplerate = 4
nfAll = 7
TRUE = True
FALSE = False
ScriptPath = ''

def _i(x):
    if isinstance(x, float):
        return int(round(x))
    return int(x)

class Region:
    def __init__(self, _sample=-1, _index=-1):
        self._sid = _sample
        self._idx = _index
        self._local = ['', '', 0, 0, 0.0, -1]
    def _get(self, k):
        if self._sid < 0:
            return self._local[k]
        return _h.rget(self._sid, self._idx, k)
    def _set(self, k, v):
        if self._sid < 0:
            self._local[k] = v
        else:
            _h.rset(self._sid, self._idx, k, v)
    Name = property(lambda self: self._get(0), lambda self, v: self._set(0, str(v)))
    Info = property(lambda self: self._get(1), lambda self, v: self._set(1, str(v)))
    SampleStart = property(lambda self: self._get(2), lambda self, v: self._set(2, _i(v)))
    SampleEnd = property(lambda self: self._get(3), lambda self, v: self._set(3, _i(v)))
    Time = property(lambda self: self._get(4), lambda self, v: self._set(4, float(v)))
    KeyNum = property(lambda self: self._get(5), lambda self, v: self._set(5, _i(v)))
    def Copy(self, source):
        self.Name = source.Name
        self.Info = source.Info
        self.SampleStart = source.SampleStart
        self.SampleEnd = source.SampleEnd
        self.Time = source.Time
        self.KeyNum = source.KeyNum
    copy = Copy

class Sample:
    def __init__(self, _id=None):
        self._id = _h.new() if _id is None else _id
    Length = property(lambda self: _h.len(self._id), lambda self, v: _h.setlen(self._id, _i(v)))
    NumChans = property(lambda self: _h.chans(self._id), lambda self, v: _h.setchans(self._id, _i(v)))
    SampleRate = property(lambda self: _h.rate(self._id), lambda self, v: _h.setrate(self._id, _i(v)))
    RegionCount = property(lambda self: _h.rcount(self._id))
    def GetSampleAt(self, position, channel):
        return _h.get(self._id, _i(position), _i(channel))
    def SetSampleAt(self, position, channel, value):
        _h.set(self._id, _i(position), _i(channel), float(value))
    def NormalizeFromTo(self, x1, x2, vol, onlyIfAbove=False):
        _h.op(self._id, 'norm', _i(x1), _i(x2), float(vol), 1.0 if onlyIfAbove else 0.0, 0.0)
    def AmpFromTo(self, x1, x2, vol):
        _h.op(self._id, 'amp', _i(x1), _i(x2), float(vol), 0.0, 0.0)
    def SilenceFromTo(self, x1, x2):
        _h.op(self._id, 'silence', _i(x1), _i(x2), 0.0, 0.0, 0.0)
    def SineFromTo(self, x1, x2, freq, phase, vol=1.0):
        _h.op(self._id, 'sine', _i(x1), _i(x2), float(freq), float(phase), float(vol))
    def NoiseFromTo(self, x1, x2, mode=1, vol=1.0):
        _h.op(self._id, 'noise', _i(x1), _i(x2), float(mode), float(vol), 0.0)
    def CenterFromTo(self, x1, x2):
        _h.op(self._id, 'center', _i(x1), _i(x2), 0.0, 0.0, 0.0)
    def ReverseFromTo(self, x1, x2):
        _h.op(self._id, 'reverse', _i(x1), _i(x2), 0.0, 0.0, 0.0)
    def ReversePolarityFromTo(self, x1, x2):
        _h.op(self._id, 'polarity', _i(x1), _i(x2), 0.0, 0.0, 0.0)
    def SwapChannelsFromTo(self, x1, x2):
        _h.op(self._id, 'swap', _i(x1), _i(x2), 0.0, 0.0, 0.0)
    def InsertSilence(self, x1, x2):
        _h.op(self._id, 'inssil', _i(x1), _i(x2), 0.0, 0.0, 0.0)
    def DeleteFromTo(self, x1, x2, copy=False):
        _h.op(self._id, 'delete', _i(x1), _i(x2), 1.0 if copy else 0.0, 0.0, 0.0)
    def TrimFromTo(self, x1, x2):
        _h.op(self._id, 'trim', _i(x1), _i(x2), 0.0, 0.0, 0.0)
    def PasteFromTo(self, source, x1, x2, mode=0):
        _h.paste(self._id, source._id, _i(x1), _i(x2), _i(mode))
    def LoadFromClipboard(self):
        return _h.clip(self._id)
    def LoadFromFile(self, filename):
        return _h.load(self._id, str(filename))
    def LoadFromFile_Ask(self):
        return _h.askload(self._id)
    def NormalizeFormat(self, source, mode=7):
        _h.normfmt(self._id, source._id, _i(mode))
    def ConvertToMono(self, mode=0):
        _h.mono(self._id, _i(mode))
    def MSToSamples(self, time):
        return float(time) * _h.rate(self._id) / 1000.0
    def SamplesToMS(self, time):
        return float(time) * 1000.0 / _h.rate(self._id)
    def GetRegion(self, index):
        index = _i(index)
        if index < 0 or index >= _h.rcount(self._id):
            raise IndexError('region index out of range')
        return Region(self._id, index)
    def AddRegion(self, name, sampleStart, sampleEnd=-1):
        return _h.radd(self._id, str(name), _i(sampleStart), _i(sampleEnd))
    def DeleteRegion(self, index):
        _h.rdel(self._id, _i(index))

class _Editor:
    SelectionStartS = property(lambda self: _h.sel(0))
    SelectionEndS = property(lambda self: _h.sel(1))

class _Utils:
    def ShowMessage(self, msg):
        _h.msg(str(msg))
    def ProgressMsg(self, msg, pos, total):
        pass
    showMessage = ShowMessage
    progressMsg = ProgressMsg

Editor = _Editor()
EditorSample = Sample(0)
Utils = _Utils()

def ShowMessage(msg):
    _h.msg(str(msg))

def ProgressMsg(msg, pos, total):
    pass

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

constexpr const char* needsFormText = "__lynx_needs_form__";

// ------------------------------------------------------------------------------------------------- the run's state
struct Context
{
    Document* editor = nullptr;
    std::deque<Document> temps;
    Range selection;
    const Host* host = nullptr;
    const std::vector<Form>* answers = nullptr;
    std::size_t asked = 0; // forms met so far (Execute, LoadFromFile_Ask)
    bool needsForm = false;
    Form captured;
    std::vector<std::string> messages;
    std::mt19937 random { 0x5eed };
};
thread_local Context* ctx = nullptr;

Document* docOf (py_i64 id)
{
    if (ctx == nullptr)
        return nullptr;
    if (id == 0)
        return ctx->editor;
    if (id > 0 && (std::size_t) id <= ctx->temps.size())
        return &ctx->temps[(std::size_t) id - 1];
    return nullptr;
}

bool badSample()
{
    return RuntimeError ("this Sample is not available");
}

// frames x1..x2 (both included, FL's convention) → [start, end) clamped to the audio
Range rangeOf (const Document& d, py_i64 x1, py_i64 x2)
{
    const auto n = d.audio.frames();
    Range r { std::clamp<std::int64_t> (x1, 0, n), std::clamp<std::int64_t> (x2 + 1, 0, n) };
    if (r.end < r.start)
        r.end = r.start;
    return r;
}

void ensureChannels (Document& d)
{
    if (d.audio.channels.empty())
        d.audio.channels.resize (2);
}

void setChannels (Document& d, int n)
{
    n = std::clamp (n, 1, 2);
    ensureChannels (d);
    if (d.audio.numChannels() == n)
        return;
    if (d.audio.frames() == 0)
    {
        d.audio.channels.assign ((std::size_t) n, {});
        return;
    }
    if (n == 1)
        toMono (d, MonoSource::merged);
    else
        toStereo (d);
}

void setRate (Document& d, double rate)
{
    if (rate <= 0.0 || rate == d.audio.sampleRate)
        return;
    if (d.audio.frames() > 1)
        resample (d, rate);
    else
        d.audio.sampleRate = rate;
}

void sortedMarkers (Document& d)
{
    sortMarkers (d);
}

constexpr py_i64 markerEnd = 0x7fffffff; // FL: a region whose end lies past the sample is a marker

// --- natives ---------------------------------------------------------------------------------------------------
bool argInt (int i, py_StackRef argv, py_i64& out)
{
    double d = 0.0;
    if (py_isint (py_arg (i)))
    {
        out = py_toint (py_arg (i));
        return true;
    }
    if (! py_castfloat (py_arg (i), &d))
        return false;
    out = (py_i64) std::llround (d);
    return true;
}

bool nNew (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (0);
    if (ctx == nullptr)
        return badSample();
    ctx->temps.emplace_back();
    ctx->temps.back().audio = silence (2, ctx->editor != nullptr && ctx->editor->audio.sampleRate > 0 ? ctx->editor->audio.sampleRate : 44100.0, 0);
    py_newint (py_retval(), (py_i64) ctx->temps.size());
    return true;
}

bool nLen (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    py_i64 id = 0;
    if (! argInt (0, argv, id))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    py_newint (py_retval(), d->audio.frames());
    return true;
}

bool nSetLen (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (2);
    py_i64 id = 0, n = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, n))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    ensureChannels (*d);
    n = std::clamp<py_i64> (n, 0, (py_i64) 1 << 31);
    for (auto& c : d->audio.channels)
        c.resize ((std::size_t) n, 0.0f);
    std::erase_if (d->markers, [n] (const Marker& m) { return m.start >= n; });
    for (auto& m : d->markers)
        m.end = std::min<std::int64_t> (m.end, n);
    py_newnone (py_retval());
    return true;
}

bool nChans (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    py_i64 id = 0;
    if (! argInt (0, argv, id))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    py_newint (py_retval(), std::max (1, d->audio.numChannels()));
    return true;
}

bool nSetChans (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (2);
    py_i64 id = 0, n = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, n))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    setChannels (*d, (int) n);
    py_newnone (py_retval());
    return true;
}

bool nRate (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    py_i64 id = 0;
    if (! argInt (0, argv, id))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    py_newint (py_retval(), (py_i64) std::llround (d->audio.sampleRate));
    return true;
}

bool nSetRate (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (2);
    py_i64 id = 0, r = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, r))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    // [REF] the scripts lower the rate to reduce it and NormalizeFormat back: the audio is converted (resampled)
    setRate (*d, (double) std::clamp<py_i64> (r, 1000, 768000));
    py_newnone (py_retval());
    return true;
}

bool nGet (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (3);
    py_i64 id = 0, pos = 0, ch = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, pos) || ! argInt (2, argv, ch))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    double v = 0.0;
    if (! d->audio.channels.empty() && pos >= 0 && pos < d->audio.frames())
    {
        const auto c = std::clamp<py_i64> (ch, 0, d->audio.numChannels() - 1);
        v = d->audio.channels[(std::size_t) c][(std::size_t) pos];
    }
    py_newfloat (py_retval(), v);
    return true;
}

bool nSet (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (4);
    py_i64 id = 0, pos = 0, ch = 0;
    double v = 0.0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, pos) || ! argInt (2, argv, ch) || ! py_castfloat (py_arg (3), &v))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    if (! d->audio.channels.empty() && pos >= 0 && pos < d->audio.frames() && ch >= 0 && ch < d->audio.numChannels())
        d->audio.channels[(std::size_t) ch][(std::size_t) pos] = std::isfinite (v) ? (float) v : 0.0f;
    py_newnone (py_retval());
    return true;
}

bool nOp (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (7);
    PY_CHECK_ARG_TYPE (1, tp_str);
    py_i64 id = 0, x1 = 0, x2 = 0;
    double a = 0.0, b = 0.0, c = 0.0;
    if (! argInt (0, argv, id) || ! argInt (2, argv, x1) || ! argInt (3, argv, x2) || ! py_castfloat (py_arg (4), &a)
        || ! py_castfloat (py_arg (5), &b) || ! py_castfloat (py_arg (6), &c))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    ensureChannels (*d);
    const std::string op = py_tostr (py_arg (1));
    const auto r = rangeOf (*d, x1, x2);
    auto& ch = d->audio.channels;
    if (op == "inssil")
    {
        if (x2 >= x1)
            insertSilence (*d, std::clamp<std::int64_t> (x1, 0, d->audio.frames()), x2 - x1 + 1);
    }
    else if (r.empty())
    {
    }
    else if (op == "amp")
        amp (*d, r, (float) a);
    else if (op == "norm")
    {
        const auto p = peak (d->audio, r);
        if (p > 0.0f && a > 0.0 && ! (b >= 0.5 && p <= (float) a))
            for (auto& x : ch)
                for (auto i = r.start; i < r.end; ++i)
                    x[(std::size_t) i] = (float) ((double) x[(std::size_t) i] * a / (double) p);
    }
    else if (op == "silence")
        clear (*d, r);
    else if (op == "sine")
    {
        // [REF] freq in Hz, phase in cycles, vol linear; every channel; the phase counts from x1
        const auto rate = d->audio.sampleRate > 0.0 ? d->audio.sampleRate : 44100.0;
        for (auto i = r.start; i < r.end; ++i)
        {
            const auto v = (float) (c * std::sin (2.0 * 3.14159265358979323846 * (b + a * (double) (i - x1) / rate)));
            for (auto& x : ch)
                x[(std::size_t) i] = v;
        }
    }
    else if (op == "noise")
    {
        std::uniform_real_distribution<double> uni (-1.0, 1.0);
        std::normal_distribution<double> gauss (0.0, 1.0);
        for (auto i = r.start; i < r.end; ++i)
            for (auto& x : ch)
                x[(std::size_t) i] = (float) (b * (a >= 0.5 ? gauss (ctx->random) : uni (ctx->random)));
    }
    else if (op == "center")
        removeDc (*d, r);
    else if (op == "reverse")
        reverse (*d, r);
    else if (op == "polarity")
        invert (*d, r);
    else if (op == "swap")
    {
        if (ch.size() == 2)
            for (auto i = r.start; i < r.end; ++i)
                std::swap (ch[0][(std::size_t) i], ch[1][(std::size_t) i]);
    }
    else if (op == "delete")
    {
        if (a >= 0.5 && ctx->host != nullptr && ctx->host->setClipboard)
            ctx->host->setClipboard (copy (d->audio, r));
        erase (*d, r);
    }
    else if (op == "trim")
        trim (*d, r);
    else
        return ValueError ("unknown operation");
    py_newnone (py_retval());
    return true;
}

bool nPaste (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (5);
    py_i64 id = 0, src = 0, x1 = 0, x2 = 0, mode = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, src) || ! argInt (2, argv, x1) || ! argInt (3, argv, x2) || ! argInt (4, argv, mode))
        return false;
    auto* d = docOf (id);
    auto* s = docOf (src);
    if (d == nullptr || s == nullptr)
        return badSample();
    ensureChannels (*d);
    // [REF] the block: the source from its start, x2 − x1 + 1 frames long (x2 = 0: the whole source), at x1
    auto block = conform (s->audio, std::max (1, d->audio.numChannels()), d->audio.sampleRate);
    if (x2 != 0 && x2 >= x1)
        for (auto& c : block.channels)
            c.resize (std::min<std::size_t> (c.size(), (std::size_t) (x2 - x1 + 1)));
    const auto at = std::clamp<std::int64_t> (x1, 0, d->audio.frames());
    if (block.frames() > 0)
    {
        if (mode == 0)
            insert (*d, at, block);
        else if (mode == 1)
        {
            const auto end = std::min (d->audio.frames(), at + block.frames());
            if (end > at)
                pasteReplace (*d, Range { at, end }, copy (block, Range { 0, end - at }));
        }
        else
            pasteMix (*d, at, block);
    }
    py_newnone (py_retval());
    return true;
}

bool nClip (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    py_i64 id = 0;
    if (! argInt (0, argv, id))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    Audio a;
    const bool ok = ctx->host != nullptr && ctx->host->clipboard && ctx->host->clipboard (a) && ! a.channels.empty();
    if (ok)
    {
        d->audio = std::move (a);
        d->markers.clear();
    }
    py_newbool (py_retval(), ok);
    return true;
}

bool loadInto (Document& d, const std::string& path)
{
    Audio a;
    if (ctx->host == nullptr || ! ctx->host->loadFile || ! ctx->host->loadFile (path, a) || a.channels.empty())
        return false;
    d.audio = std::move (a);
    d.markers.clear();
    return true;
}

bool nLoad (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (2);
    PY_CHECK_ARG_TYPE (1, tp_str);
    py_i64 id = 0;
    if (! argInt (0, argv, id))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    py_newbool (py_retval(), loadInto (*d, py_tostr (py_arg (1))));
    return true;
}

// a form the user answers between two runs: the answer of this form if there is one, else the run stops for it
const Form* answerOrAsk (Form&& form)
{
    if (ctx->answers != nullptr && ctx->asked < ctx->answers->size())
        return &(*ctx->answers)[ctx->asked++];
    ctx->needsForm = true;
    ctx->captured = std::move (form);
    return nullptr;
}

bool nAskLoad (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    py_i64 id = 0;
    if (! argInt (0, argv, id))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    Form f;
    f.title = "Open";
    Control c;
    c.type = Control::Type::text;
    c.name = "File";
    c.hint = "file";
    f.controls.push_back (c);
    f.restoreValues = false;
    const auto* answer = answerOrAsk (std::move (f));
    if (answer == nullptr)
        return RuntimeError (needsFormText);
    const bool ok = ! answer->controls.empty() && ! answer->controls[0].text.empty() && loadInto (*d, answer->controls[0].text);
    py_newbool (py_retval(), ok);
    return true;
}

bool nNormFmt (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (3);
    py_i64 id = 0, src = 0, mode = 7;
    if (! argInt (0, argv, id) || ! argInt (1, argv, src) || ! argInt (2, argv, mode))
        return false;
    auto* d = docOf (id);
    auto* s = docOf (src);
    if (d == nullptr || s == nullptr)
        return badSample();
    if ((mode & 1) != 0)
        setChannels (*d, std::max (1, s->audio.numChannels()));
    if ((mode & 4) != 0)
        setRate (*d, s->audio.sampleRate);
    // (NF_Format: LYNX keeps every sample as 32-bit float)
    py_newnone (py_retval());
    return true;
}

bool nMono (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (2);
    py_i64 id = 0, mode = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, mode))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    if (d->audio.numChannels() == 2)
        toMono (*d, mode == 1 ? MonoSource::left : mode == 2 ? MonoSource::right : MonoSource::merged);
    py_newnone (py_retval());
    return true;
}

bool nRCount (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    py_i64 id = 0;
    if (! argInt (0, argv, id))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    py_newint (py_retval(), (py_i64) d->markers.size());
    return true;
}

bool nRGet (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (3);
    py_i64 id = 0, i = 0, field = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, i) || ! argInt (2, argv, field))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    if (i < 0 || i >= (py_i64) d->markers.size())
        return IndexError ("region index out of range");
    const auto& m = d->markers[(std::size_t) i];
    switch (field)
    {
        case 0: py_newstr (py_retval(), m.name.c_str()); break;
        case 1: py_newstr (py_retval(), m.info.c_str()); break;
        case 2: py_newint (py_retval(), m.start); break;
        case 3: py_newint (py_retval(), m.isRegion() ? m.end - 1 : markerEnd); break;
        case 4: py_newfloat (py_retval(), m.time); break;
        default: py_newint (py_retval(), m.keyNum); break;
    }
    return true;
}

bool nRSet (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (4);
    py_i64 id = 0, i = 0, field = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, i) || ! argInt (2, argv, field))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    if (i < 0 || i >= (py_i64) d->markers.size())
        return IndexError ("region index out of range");
    auto& m = d->markers[(std::size_t) i];
    py_i64 v = 0;
    double f = 0.0;
    switch (field)
    {
        case 0:
        case 1:
            if (! py_isstr (py_arg (3)))
                return TypeError ("a text is expected");
            (field == 0 ? m.name : m.info) = py_tostr (py_arg (3));
            break;
        case 2:
        {
            if (! argInt (3, argv, v))
                return false;
            const bool region = m.isRegion();
            m.start = std::clamp<std::int64_t> (v, 0, d->audio.frames());
            m.end = region ? std::max (m.end, m.start + 1) : m.start;
            break;
        }
        case 3:
            if (! argInt (3, argv, v))
                return false;
            // an end past the sample makes a marker; else the region ends after frame v
            m.end = v >= d->audio.frames() || v < m.start ? m.start : v + 1;
            if (v == d->audio.frames() - 1 && v >= m.start)
                m.end = d->audio.frames();
            break;
        case 4:
            if (! py_castfloat (py_arg (3), &f))
                return false;
            m.time = f;
            break;
        default:
            if (! argInt (3, argv, v))
                return false;
            m.keyNum = (int) v;
            break;
    }
    py_newnone (py_retval());
    return true;
}

bool nRAdd (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (4);
    PY_CHECK_ARG_TYPE (1, tp_str);
    py_i64 id = 0, s = 0, e = 0;
    if (! argInt (0, argv, id) || ! argInt (2, argv, s) || ! argInt (3, argv, e))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    Marker m;
    m.name = py_tostr (py_arg (1));
    m.start = std::clamp<std::int64_t> (s, 0, d->audio.frames());
    m.end = e >= m.start && e < d->audio.frames() ? e + 1 : m.start;
    d->markers.push_back (m);
    sortedMarkers (*d);
    py_i64 index = 0;
    for (std::size_t k = 0; k < d->markers.size(); ++k)
        if (d->markers[k].start == m.start && d->markers[k].end == m.end && d->markers[k].name == m.name)
            index = (py_i64) k;
    py_newint (py_retval(), index);
    return true;
}

bool nRDel (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (2);
    py_i64 id = 0, i = 0;
    if (! argInt (0, argv, id) || ! argInt (1, argv, i))
        return false;
    auto* d = docOf (id);
    if (d == nullptr)
        return badSample();
    if (i >= 0 && i < (py_i64) d->markers.size())
        d->markers.erase (d->markers.begin() + (long) i);
    py_newnone (py_retval());
    return true;
}

bool nSel (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    py_i64 which = 0;
    if (! argInt (0, argv, which) || ctx == nullptr || ctx->editor == nullptr)
        return false;
    // no selection: the whole sample (what Edison's tools work on)
    const auto n = ctx->editor->audio.frames();
    auto r = clamp (*ctx->editor, ctx->selection);
    if (r.empty())
        r = { 0, n };
    py_newint (py_retval(), which == 0 ? r.start : std::max<std::int64_t> (r.start, r.end - 1));
    return true;
}

bool nMsg (int argc, py_StackRef argv)
{
    PY_CHECK_ARGC (1);
    PY_CHECK_ARG_TYPE (0, tp_str);
    if (ctx != nullptr)
        ctx->messages.emplace_back (py_tostr (py_arg (0)));
    py_newnone (py_retval());
    return true;
}

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
    if (ctx == nullptr)
        return RuntimeError ("no editor");
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
    const auto* answer = answerOrAsk (std::move (f));
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

bool installModule (std::string& error)
{
    py_GlobalRef host = py_newmodule ("_lynxedison");
    py_bindfunc (host, "new", nNew);
    py_bindfunc (host, "len", nLen);
    py_bindfunc (host, "setlen", nSetLen);
    py_bindfunc (host, "chans", nChans);
    py_bindfunc (host, "setchans", nSetChans);
    py_bindfunc (host, "rate", nRate);
    py_bindfunc (host, "setrate", nSetRate);
    py_bindfunc (host, "get", nGet);
    py_bindfunc (host, "set", nSet);
    py_bindfunc (host, "op", nOp);
    py_bindfunc (host, "paste", nPaste);
    py_bindfunc (host, "clip", nClip);
    py_bindfunc (host, "load", nLoad);
    py_bindfunc (host, "askload", nAskLoad);
    py_bindfunc (host, "normfmt", nNormFmt);
    py_bindfunc (host, "mono", nMono);
    py_bindfunc (host, "rcount", nRCount);
    py_bindfunc (host, "rget", nRGet);
    py_bindfunc (host, "rset", nRSet);
    py_bindfunc (host, "radd", nRAdd);
    py_bindfunc (host, "rdel", nRDel);
    py_bindfunc (host, "sel", nSel);
    py_bindfunc (host, "msg", nMsg);
    py_bindfunc (host, "execute", nExecute);
    if (! py_exec (enveditorSource, "enveditor", EXEC_MODE, py_newmodule ("enveditor")))
    {
        error = python::takeError();
        return false;
    }
    return true;
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
    return s;
}

bool isNeedsForm (const std::string& error)
{
    return error.find (needsFormText) != std::string::npos;
}

std::mutex memoryLock;
std::map<std::string, Form> memory;
} // namespace

Outcome run (const std::string& source, Document& doc, Range selection, const std::vector<Form>& answers, const Host& host)
{
    Outcome out;
    Context c;
    c.editor = &doc;
    c.selection = selection;
    c.host = &host;
    c.answers = &answers;
    ensureChannels (doc);
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
        auto finish = [&] (bool ok, std::string error) {
            python::setTimeLimit (0.0);
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
        };
        std::string error;
        if (! installModule (error))
            finish (false, error);
        else
        {
            python::setTimeLimit (120000.0);
            const auto text = normalized (source);
            if (! py_exec (text.c_str(), "<script>", EXEC_MODE, nullptr))
            {
                const auto e = python::takeError();
                finish (false, isNeedsForm (e) ? std::string() : e);
            }
            else if (auto* create = py_getglobal (py_name ("createDialog")); create != nullptr && py_getglobal (py_name ("apply")) != nullptr)
            {
                // FL's preview style: the form createDialog() returns, then apply(form) with its values
                py_TValue f = *create;
                bool ok = py_call (&f, 0, nullptr);
                py_TValue form = *py_retval();
                if (ok)
                    ok = py_getattr (&form, py_name ("Execute"));
                if (ok)
                {
                    py_TValue exec = *py_retval();
                    ok = py_call (&exec, 0, nullptr);
                }
                if (ok)
                {
                    py_TValue applyFn = *py_getglobal (py_name ("apply"));
                    ok = py_call (&applyFn, 1, &form);
                }
                const auto e = ok ? std::string() : python::takeError();
                finish (ok, isNeedsForm (e) ? std::string() : e);
            }
            else
                finish (true, {});
        }
        ctx = nullptr;
        python::setPrint (nullptr);
        python::releaseVm (vm);
    });
    out.messages = std::move (c.messages);
    if (out.status == Outcome::Status::done)
        sortMarkers (doc);
    return out;
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

// ------------------------------------------------------------------------------------------------- LYNX's scripts
const std::vector<Builtin>& builtins()
{
    // LYNX's own small scripts (not FL's): examples of the API that also do something useful
    static const std::vector<Builtin> list = {
        { "Amp", "Gain", R"PY(from enveditor import *

def createDialog():
    form = ScriptDialog('Gain', 'Changes the level of the selection (the whole sample without one).')
    form.AddInputKnob('Gain (dB)', 0, -24, 24)
    return form

def apply(form):
    gain = 10 ** (form.GetInputValue('Gain (dB)') / 20)
    EditorSample.AmpFromTo(Editor.SelectionStartS, Editor.SelectionEndS, gain)
)PY" },
        { "FX", "Bit crusher", R"PY(from enveditor import *

def createDialog():
    form = ScriptDialog('Bit crusher', 'Keeps only the given number of bits of each sample.')
    form.AddInputKnobInt('Bits', 8, 1, 24)
    form.AddInputKnob('Mix', 1, 0, 1)
    return form

def apply(form):
    steps = 2 ** (form.GetInputValue('Bits') - 1)
    wet = form.GetInputValue('Mix')
    for n in range(Editor.SelectionStartS, Editor.SelectionEndS + 1):
        for c in range(EditorSample.NumChans):
            s = EditorSample.GetSampleAt(n, c)
            EditorSample.SetSampleAt(n, c, s + (round(s * steps) / steps - s) * wet)
)PY" },
        { "FX", "Ring modulator", R"PY(from enveditor import *
import math

def createDialog():
    form = ScriptDialog('Ring modulator', 'Multiplies the selection by a sine.')
    form.AddInputKnob('Frequency (Hz)', 220, 1, 5000)
    form.AddInputKnob('Mix', 1, 0, 1)
    return form

def apply(form):
    f = form.GetInputValue('Frequency (Hz)')
    wet = form.GetInputValue('Mix')
    x1 = Editor.SelectionStartS
    w = 2 * math.pi * f / EditorSample.SampleRate
    for n in range(x1, Editor.SelectionEndS + 1):
        m = math.sin(w * (n - x1))
        for c in range(EditorSample.NumChans):
            s = EditorSample.GetSampleAt(n, c)
            EditorSample.SetSampleAt(n, c, s * (1 - wet) + s * m * wet)
)PY" },
        { "Generation", "Sine tone", R"PY(from enveditor import *

def createDialog():
    form = ScriptDialog('Sine tone', 'Writes a sine over the selection (a 1 s sample when the editor is empty).')
    form.AddInputKnob('Frequency (Hz)', 440, 1, 20000)
    form.AddInputKnob('Volume', 0.5, 0, 1)
    return form

def apply(form):
    if EditorSample.Length <= 0:
        EditorSample.Length = round(EditorSample.MSToSamples(1000))
    EditorSample.SineFromTo(Editor.SelectionStartS, Editor.SelectionEndS, form.GetInputValue('Frequency (Hz)'), 0, form.GetInputValue('Volume'))
)PY" },
        { "Generation", "Noise", R"PY(from enveditor import *

def createDialog():
    form = ScriptDialog('Noise', 'Writes noise over the selection (a 1 s sample when the editor is empty).')
    form.AddInputCombo('Type', 'White (uniform),Gaussian', 1)
    form.AddInputKnob('Volume', 0.25, 0, 1)
    return form

def apply(form):
    if EditorSample.Length <= 0:
        EditorSample.Length = round(EditorSample.MSToSamples(1000))
    EditorSample.NoiseFromTo(Editor.SelectionStartS, Editor.SelectionEndS, form.GetInputValue('Type'), form.GetInputValue('Volume'))
)PY" },
        { "Regions", "Markers to regions", R"PY(from enveditor import *

# every marker becomes a region that lasts until the next one (the last one: until the end)
for i in range(EditorSample.RegionCount):
    r = EditorSample.GetRegion(i)
    if i == EditorSample.RegionCount - 1:
        r.SampleEnd = EditorSample.Length - 1
    else:
        r.SampleEnd = EditorSample.GetRegion(i + 1).SampleStart - 1
)PY" },
    };
    return list;
}
} // namespace lynx::audioedit::script
