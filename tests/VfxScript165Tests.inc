// Round 165d: VFX Script runs Python (core/Python.h, core/VfxScript.h)

#include "core/Python.h"
#include "core/VfxScript.h"
#include <ctime>

namespace vfxs165
{
using lynx::vfx::Event;

std::vector<std::uint8_t> state (const std::string& source, const std::vector<std::pair<std::string, std::string>>& params = {})
{
    auto s = lynx::vfx::defaultScript();
    s.setSource (source);
    if (! params.empty())
        s.setParameters (params);
    return lynx::vfx::writeScript (s);
}

struct Timed
{
    long at = 0; // absolute sample
    Event e;
};

// Plays `total` samples in blocks of n (the song playing from tick 0 at `bpm`, 44.1 kHz); `in` at absolute samples
std::vector<Timed> play (lynx::vfx::Processor& p, std::vector<Timed> in, long total, int n = 512, bool playing = true, double bpm = 120.0)
{
    std::vector<Timed> out;
    lynx::vfx::Transport t;
    t.sampleRate = 44100.0;
    t.bpm = bpm;
    t.playing = playing;
    std::size_t next = 0;
    for (long start = 0; start < total; start += n)
    {
        std::vector<Event> block;
        while (next < in.size() && in[next].at < start + n)
        {
            auto e = in[next++].e;
            e.offset = (int) (in[next - 1].at - start);
            block.push_back (e);
        }
        t.ticks = (double) start * bpm * 960.0 / (60.0 * t.sampleRate);
        lynx::vfx::EventList list;
        p.process (block.data(), (int) block.size(), n, t, list);
        list.sortByOffset();
        for (int k = 0; k < list.size(); ++k)
            out.push_back ({ start + list[k].offset, list[k] });
    }
    return out;
}

Timed on (long at, int key, float velocity = 0.8f, int colour = 0)
{
    return { at, vfx164::on (key, 0, colour, velocity) };
}

Timed off (long at, int key, int colour = 0)
{
    return { at, vfx164::off (key, 0, colour) };
}

std::vector<int> keysOn (const std::vector<Timed>& out)
{
    std::vector<int> keys;
    for (const auto& o : out)
        if (o.e.type == Event::Type::on)
            keys.push_back (o.e.voice.key);
    return keys;
}

const char* passThrough = R"(
import flvfx as vfx

class Child(vfx.Voice):
    parent = None

def onTriggerVoice(v):
    for shift in (0, 4, 7):
        c = Child(v)
        c.parent = v
        c.note += shift
        c.trigger()

def onReleaseVoice(v):
    for c in list(vfx.context.voices):
        if c.parent is v:
            c.release()
)";
} // namespace vfxs165

TEST (vfx_script_runs_python_on_the_notes)
{
    using namespace lynx::vfx;
    auto p = makeProcessor (Kind::script, vfxs165::state (vfxs165::passThrough));
    const auto out = vfxs165::play (*p, { vfxs165::on (100, 60, 0.5f), vfxs165::off (3000, 60) }, 4096);
    // three notes on at the note's sample, three offs at the release's
    CHECK (out.size() == 6);
    if (out.size() != 6)
        return;
    CHECK ((vfxs165::keysOn (out) == std::vector<int> { 60, 64, 67 }));
    for (int k = 0; k < 3; ++k)
    {
        CHECK (out[(std::size_t) k].at == 100 && std::abs (out[(std::size_t) k].e.voice.velocity - 0.5f) < 1e-6f);
        CHECK (out[(std::size_t) k + 3].at == 3000 && out[(std::size_t) k + 3].e.type == Event::Type::off);
    }
    CHECK (out[3].e.id == out[0].e.id);
}

TEST (vfx_script_ticks_play_notes_at_their_sample)
{
    using namespace lynx::vfx;
    // an eighth-note pulse (48 of 96 ticks), each half as long, from the song's start
    const char* src = R"(
import flvfx as vfx
def onTick():
    if vfx.context.isPlaying and vfx.context.ticks % 48 == 0:
        v = vfx.Voice()
        v.note = 72
        v.length = 24
        v.trigger()
)";
    auto p = makeProcessor (Kind::script, vfxs165::state (src));
    auto out = vfxs165::play (*p, {}, 44100);
    out.erase (std::remove_if (out.begin(), out.end(), [] (const vfxs165::Timed& o) { return o.at >= 44100; }), out.end());
    // 120 bpm: a tick is 44100 · 60 / (120 · 96) = 229.6875 samples; an eighth 11025
    std::vector<long> ons, offs;
    for (const auto& o : out)
        (o.e.type == Event::Type::on ? ons : offs).push_back (o.at);
    if (ons != std::vector<long> { 0, 11025, 22050, 33075 })
    {
        for (const auto& o : out)
            std::printf ("  %s at %ld key %d\n", o.e.type == Event::Type::on ? "on" : "off", o.at, o.e.voice.key);
    }
    CHECK ((ons == std::vector<long> { 0, 11025, 22050, 33075 }));
    // (24 ticks = 5512.5 samples: the half sample rounds either way)
    const long expectedOffs[] = { 5513, 16538, 27563, 38588 };
    CHECK (offs.size() == 4);
    for (std::size_t k = 0; k < offs.size() && k < 4; ++k)
        CHECK (std::abs (offs[k] - expectedOffs[k]) <= 1);
    CHECK (p->busy());
}

TEST (vfx_script_controls_values_and_output_controllers)
{
    using namespace lynx::vfx;
    const char* src = R"(
import flvfx as vfx
vfx.addOutputController('Level', 0.25)
def onTick():
    f = vfx.context.form
    vfx.setOutputController('Level', f.getInputValue('Main: Amount'))
    vfx.setOutputController('Steps', f.getInputValue('Steps') / 10.0)
    vfx.setOutputController('Mode', 1.0 if f.GetInputValue('Mode') == 2 and f.getInputValue('Name') == 'abc' else 0.0)
def createDialog():
    form = vfx.ScriptDialog('Title', 'What it does')
    form.addGroup('Main')
    form.addInputKnob('Amount', 0.5, -1, 1, hint='the amount')
    form.endGroup()
    form.addInputKnobInt('Steps', 3, 1, 8)
    form.AddInputCombo('Mode', 'One,Two,Three', 0)
    form.addInputCheckbox('On', 1)
    form.addInputText('Name', 'x')
    vfx.addOutputController('Steps', 0)
    vfx.addOutputController('Mode', 0)
    return form
)";
    auto settings = defaultScript();
    settings.setSource (src);
    const auto info = script::describe (settings);
    CHECK (info->compiled);
    if (! info->compiled)
    {
        std::printf ("  %s\n", info->error.c_str());
        return;
    }
    CHECK (info->title == "Title" && info->description == "What it does");
    CHECK (info->controls.size() == 5);
    CHECK (info->controls[0].name == "Main: Amount" && info->controls[0].type == script::Control::Type::knob && info->controls[0].hint == "the amount");
    CHECK (info->controls[2].options == (std::vector<std::string> { "One", "Two", "Three" }));
    CHECK ((info->outputs == std::vector<std::string> { "Level", "Steps", "Mode" }));
    // FL's encodings: a knob as 2^18-normalized, an int / combo as the number, a text as it is
    settings.setParameters ({ { "Main: Amount", "196608" }, { "Steps", "7" }, { "Mode", "2" }, { "On", "0" }, { "Name", "abc" } });
    auto v = script::valuesOf (*info, settings);
    CHECK (std::abs (v.numbers[0] - 0.5) < 1e-9 && v.numbers[1] == 7.0 && v.numbers[2] == 2.0 && v.numbers[3] == 0.0 && v.texts[4] == "abc");
    auto again = settings;
    script::writeValues (again, *info, v);
    CHECK (again.parameters() == settings.parameters());
    CHECK (script::encodeValue (info->controls[0], 0.0, {}) == "131072"); // (0 in −1..1: the middle)

    auto p = makeProcessor (Kind::script, writeScript (settings));
    CHECK (std::abs (p->controlOutput (0) - 0.25f) < 1e-6f); // (the default, set at the top level)
    vfxs165::play (*p, {}, 2048);
    CHECK (std::abs (p->controlOutput (0) - 0.5f) < 1e-6f);
    CHECK (std::abs (p->controlOutput (1) - 0.7f) < 1e-6f);
    CHECK (p->controlOutput (2) == 1.0f);
    // an automation input (Patcher cable) moves a control: 0..1 over its range
    p->setParameter (0, 0.0f);
    p->setParameter (1, 1.0f);
    vfxs165::play (*p, {}, 1024);
    CHECK (p->controlOutput (0) == 0.0f); // (−1, clamped to the output's 0..1)
    CHECK (std::abs (p->controlOutput (1) - 0.8f) < 1e-6f);
}

TEST (vfx_script_errors_are_reported_and_never_hang)
{
    using namespace lynx::vfx;
    // a syntax error: the line and the message, as FL's editor shows them
    const auto bad = script::describe (std::string ("import flvfx as vfx\ndef onTick(:\n    pass\n"));
    CHECK (! bad->compiled && bad->error.find ("SyntaxError") != std::string::npos && bad->error.find ("line 2") != std::string::npos);
    // a runtime error in a callback: logged once, then counted; the script keeps running
    State st;
    st.kind = Kind::script;
    st.tag = 0x165d0001;
    const char* src = R"(
import flvfx as vfx
print('hello from the script')
n = 0
def onTick():
    global n
    n += 1
    if n % 2 == 0:
        raise ValueError('even tick')
    vfx.setOutputController('n', min(1.0, n / 1000.0))
vfx.addOutputController('n', 0)
)";
    auto p = makeProcessor (Kind::script, vfxs165::state (src));
    p->setTag (st.tag);
    vfxs165::play (*p, {}, 8192);
    const auto monitor = script::monitorOf (st.tag);
    CHECK (monitor != nullptr);
    if (monitor == nullptr)
        return;
    const auto log = monitor->log();
    CHECK (log.find ("hello from the script") != std::string::npos);
    CHECK (log.find ("ValueError: even tick") != std::string::npos);
    CHECK (log.find ("the same error again") != std::string::npos);
    CHECK (monitor->status().empty());
    CHECK (p->controlOutput (0) > 0.01f);

    // an endless loop: interrupted, the instance stops and says why; the audio thread never waits long
    script::setRealtime (true);
    auto loop = makeProcessor (Kind::script, vfxs165::state ("import flvfx as vfx\ndef onTriggerVoice(v):\n    while True:\n        try:\n            pass\n        except:\n            pass\n"));
    loop->setTag (0x165d0002);
    const auto begin = std::chrono::steady_clock::now();
    double worst = 0.0;
    std::vector<double> times;
    for (int b = 0; b < 400; ++b)
    {
        lynx::vfx::EventList out;
        lynx::vfx::Transport t;
        auto e = vfx164::on (60);
        const auto before = std::chrono::steady_clock::now();
        loop->process (&e, b == 0 ? 1 : 0, 512, t, out);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - before).count();
        worst = std::max (worst, ms);
        times.push_back (ms);
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    script::setRealtime (false);
    const auto m2 = script::monitorOf (0x165d0002);
    // (round 167e: the limit is 250 ms of the Python thread's CPU time, so on a busy machine it may come after the
    // 400 blocks: waited for, up to the 1 s wall-clock backstop and some)
    for (int w = 0; w < 300 && m2 != nullptr && m2->status().find ("took too long") == std::string::npos; ++w)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    CHECK (m2 != nullptr && m2->status().find ("took too long") != std::string::npos);
    std::sort (times.begin(), times.end());
    const auto p95 = times[times.size() * 95 / 100];
    std::printf ("  endless loop: stopped; block wait 95th percentile %.2f ms, worst %.2f ms, %.0f ms in all\n", p95, worst,
                 std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - begin).count());
    // the wait is bounded by the budget (2 ms at most); the worst block also carries the OS scheduler's noise on a
    // loaded machine (a test thread has no real-time priority)
    CHECK (p95 < 2.5);
    CHECK (worst < 50.0);
}

extern "C" size_t py_lynx_heap_limit; // (third_party/pocketpy/pocketpy.c: LYNX's heap limit, 768 MB)

TEST (vfx_script_memory_runaway_is_stopped)
{
    using namespace lynx::vfx;
    // a script that keeps allocating is stopped at LYNX's heap limit, with a message; the memory comes back.
    // (round 190fg: the test runs with the limit lowered to 64 MB. Filling the real 768 MB costs the Python thread
    // ~0.5 s of page faults on an idle machine; under six parallel test shards plus builds it once took past the
    // call's time limit (3 s of CPU, 12 s wall), and the script was stopped as "took too long" before the memory
    // stop: the test failed though the product did what it should. The heap counter and the stop are the same at any
    // limit; the shipped limit is checked on its own.)
    CHECK (py_lynx_heap_limit == ((size_t) 768 << 20));
    const auto shipped = py_lynx_heap_limit;
    py_lynx_heap_limit = (size_t) 64 << 20;
    auto p = makeProcessor (Kind::script, vfxs165::state ("import flvfx as vfx\nkeep = []\ndef onTick():\n    while True:\n        try:\n            keep.append([0] * 400000)\n        except:\n            pass\n"));
    p->setTag (0x165d0005);
    const auto cpuBefore = std::clock();
    const auto wallBefore = std::chrono::steady_clock::now();
    vfxs165::play (*p, {}, 1024);
    auto m = script::monitorOf (0x165d0005);
    CHECK (m != nullptr && m->status().find ("Stopped: the script used more than 64 MB of memory") != std::string::npos);
    if (m != nullptr)
        std::printf ("  %s\n  (process CPU %.0f ms, wall %.0f ms)\n", m->status().c_str(), 1000.0 * (double) (std::clock() - cpuBefore) / CLOCKS_PER_SEC,
                     std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - wallBefore).count());
    m.reset();
    p.reset(); // (its VM is released: the next script starts with a small heap)
    // the memory came back: a list of 1.5 million items (its storage grows to 32 MB) fits under the same 64 MB limit
    auto q = makeProcessor (Kind::script, vfxs165::state ("import flvfx as vfx\nbig = [0] * 1500000\ndef onTick():\n    vfx.setOutputController('ok', 1)\nvfx.addOutputController('ok', 0)\n"));
    vfxs165::play (*q, {}, 512);
    CHECK (q->controlOutput (0) == 1.0f);
    q.reset();
    py_lynx_heap_limit = shipped;
}

// Round 190fg: one operation that asks for more than the heap has room for, in a single allocation, stops the script
// with the same memory message before anything is allocated (list() of a long iterator: as soon as it passes the limit). Before, an allocation past twice the limit failed and
// pocketpy aborted the process ('[0] * 300_000_000': c11_vector__reserve "out of memory") or wrote through a null
// pointer ('x' * 2_000_000_000): one line of a script took LYNX down.
TEST (vfx_script_one_huge_allocation_is_stopped_not_fatal)
{
    using namespace lynx::vfx;
    // (at a 64 MB limit, as the test above: list(range(...)) fills the heap up to the limit first)
    const auto shipped = py_lynx_heap_limit;
    py_lynx_heap_limit = (size_t) 64 << 20;
    const char* lines[] = { "big = [0] * 300000000", "big = 'x' * 2000000000", "big = 'xy' * 1500000000", "big = [1, 2] * 2000000000",
                            "big = list(range(200000000))" };
    int tag = 0x165d0100;
    for (const auto* line : lines)
    {
        const auto src = std::string ("import flvfx as vfx\ndef onTick():\n    try:\n        ") + line + "\n    except:\n        pass\n";
        auto p = makeProcessor (Kind::script, vfxs165::state (src));
        p->setTag (++tag);
        vfxs165::play (*p, {}, 1024);
        auto m = script::monitorOf (tag);
        CHECK (m != nullptr && m->status().find ("more than 64 MB") != std::string::npos);
        if (m != nullptr && m->status().find ("more than 64 MB") == std::string::npos)
            std::printf ("  %s: %s\n", line, m->status().c_str());
    }
    // a repeat that fits is untouched
    auto ok = makeProcessor (Kind::script, vfxs165::state ("import flvfx as vfx\ns = 'ab' * 1000\nl = [0, 1] * 1000\nr = list(range(5000))\n"
                                                           "def onTick():\n    vfx.setOutputController('ok', 1 if len(s) == 2000 and len(l) == 2000 and len(r) == 5000 else 0)\n"
                                                           "vfx.addOutputController('ok', 0)\n"));
    vfxs165::play (*ok, {}, 512);
    CHECK (ok->controlOutput (0) == 1.0f);
    ok.reset();
    py_lynx_heap_limit = shipped;
}

TEST (vfx_script_voice_changes_reach_the_sounding_note)
{
    using namespace lynx::vfx;
    const char* src = R"(
import flvfx as vfx
def onTriggerVoice(v):
    c = vfx.Voice(v)
    c.parent = v
    c.trigger()
def onTick():
    for c in vfx.context.voices:
        c.copyFrom(c.parent)
        c.note += 0.5
        c.pan = -1
)";
    auto p = makeProcessor (Kind::script, vfxs165::state (src));
    const auto out = vfxs165::play (*p, { vfxs165::on (0, 60) }, 1024);
    bool pitch = false, pan = false;
    for (const auto& o : out)
        if (o.e.type == Event::Type::prop)
        {
            pitch = pitch || (o.e.prop == Prop::pitch && std::abs (o.e.value - 50.0f) < 1e-3f);
            pan = pan || (o.e.prop == Prop::pan && o.e.value == -1.0f);
        }
    CHECK (pitch && pan);
    CHECK (! out.empty() && out[0].e.type == Event::Type::on && out[0].e.voice.key == 60);
}

TEST (vfx_script_python_has_what_fl_scripts_use)
{
    using namespace lynx::vfx;
    // CPython features pocketpy lacks that FL's factory scripts use: LYNX adds or patches them
    const char* src = "import flvfx as vfx\nimport re, random, sys, math, time\n"
                      "text = '''two lines \\\nas one'''\n"
                      "class Mine(vfx.Voice):\n    def __init__(self):\n        super().__init__()\n        self.extra: int = 3\n"
                      "def check():\n"
                      "    assert re.sub(r'choices\\((.*?)\\)( |$)', r'choices(\\1)[0] ', 'choices([1,2],k=1)') == 'choices([1,2],k=1)[0] '\n"
                      "    assert re.findall(r'\\d+', 'a1b22c333') == ['1', '22', '333']\n"
                      "    m = re.match(r'(?P<w>[a-z]+)(\\d)', 'ab7x')\n"
                      "    assert m.group('w') == 'ab' and m.group(2) == '7' and m.span() == (0, 3)\n"
                      "    assert re.split(r',\\s*', 'a, b,c') == ['a', 'b', 'c']\n"
                      "    assert pow(2, 10, 1000) == 24 and pow(2, 3) == 8\n"
                      "    assert sys.float_info.epsilon < 1e-15\n"
                      "    random.seed(1.5)\n"
                      "    g = [random.gauss(0, 1) for i in range(2000)]\n"
                      "    assert abs(sum(g) / len(g)) < 0.15\n"
                      "    assert next((x for x in [1, 5, 9] if x > 4), None) == 5\n"
                      "    assert sum(x * x for x in range(4)) == 14\n"
                      "    assert text == 'two lines as one'\n"
                      "    assert Mine().extra == 3 and Mine().note == 60.0\n"
                      "    assert time.time_ns() > 0\n"
                      "    assert '42'.isdigit() and not '4a'.isdigit() and 'ab'.isalpha() and 'a1'.isalnum() and ' '.isspace()\n"
                      "    assert 'AB'.isupper() and 'ab'.islower() and 'hello world'.title() == 'Hello World'\n"
                      "    assert 'a,b,c'.rsplit(',', 1) == ['a,b', 'c'] and 'a.b.c'.rpartition('.') == ('a.b', '.', 'c')\n"
                      "    assert 'abcabc'.rfind('b') == 4 and 'ab'.center(6, '*') == '**ab**' and 'x'.capitalize() == 'X'\n"
                      "    vfx.setOutputController('ok', 1)\n"
                      "vfx.addOutputController('ok', 0)\n"
                      "def onTick():\n    check()\n";
    auto p = makeProcessor (Kind::script, vfxs165::state (src));
    p->setTag (0x165d0003);
    vfxs165::play (*p, {}, 512);
    CHECK (p->controlOutput (0) == 1.0f);
    if (p->controlOutput (0) != 1.0f)
        if (const auto m = script::monitorOf (0x165d0003))
            std::printf ("  %s\n", m->log().c_str());
    // the sandbox: no files, no processes
    auto q = makeProcessor (Kind::script, vfxs165::state ("import flvfx as vfx\nimport os\n"));
    q->setTag (0x165d0004);
    const auto m = script::monitorOf (0x165d0004);
    CHECK (m != nullptr && m->status().find ("ImportError") != std::string::npos);
    CHECK (! script::describe (std::string ("x = open('a.txt', 'w')\n"))->compiled);
}

TEST (vfx_script_late_answers_play_at_the_next_block)
{
    using namespace lynx::vfx;
    // a script slower than the real-time budget: its note comes later, at the start of the first block after it
    // answered. (Round 167e made the margins larger; round 190at removed them: the macOS CI still failed now and then
    // because the audio thread, preempted for longer than the script's 40 ms between handing the block over and
    // looking at the answer, found it on time. The test now holds the Python thread while the first block runs, so the
    // answer cannot be on time whatever the scheduling, and releases it after that block returned.)
    const char* src = R"(
import flvfx as vfx, time
def onTriggerVoice(v):
    c = vfx.Voice(v)
    c.trigger()
)";
    auto p = makeProcessor (Kind::script, vfxs165::state (src));
    script::setRealtime (true);
    script::holdScriptThread (true);
    lynx::vfx::Transport t;
    lynx::vfx::EventList first, later;
    auto e = vfx164::on (60, 100);
    p->process (&e, 1, 512, t, first);
    script::holdScriptThread (false);
    int blocks = 0;
    for (; blocks < 400 && later.size() == 0; ++blocks)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
        p->process (nullptr, 0, 512, t, later);
    }
    script::setRealtime (false);
    CHECK (first.size() == 0);
    CHECK (later.size() == 1 && later[0].offset == 0 && later[0].voice.key == 60);
    // offline (export) it waits: the note is at its sample
    lynx::vfx::EventList exact;
    auto p2 = makeProcessor (Kind::script, vfxs165::state (src));
    p2->process (&e, 1, 512, t, exact);
    CHECK (exact.size() == 1 && exact[0].offset == 100);
}

TEST (vfx_script_runs_fl_factory_scripts)
{
    using namespace lynx::vfx;
    if (realFlpDir.empty())
        return;
    namespace fs = std::filesystem;
    const auto dir = fs::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / "VFX Script";
    if (! fs::is_directory (dir))
        return;
    auto load = [&] (const fs::path& file) -> std::optional<ScriptSettings> {
        std::ifstream in (file, std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
        const auto st = stateFromFlPreset (bytes, Kind::script);
        return st ? parseScript (*st) : std::nullopt;
    };
    int files = 0, compiled = 0;
    double slowest = 0.0;
    std::string slowestName;
    for (const auto& entry : fs::recursive_directory_iterator (dir))
    {
        if (entry.path().extension() != ".fst")
            continue;
        const auto s = load (entry.path());
        if (! s)
            continue;
        ++files;
        const auto info = script::describe (*s);
        if (info->compiled)
            ++compiled;
        else
            std::printf ("  %s: %s\n", entry.path().filename().string().c_str(), info->error.c_str());
        // each one runs 2 s with a held chord and the song playing, without an error
        auto p = makeProcessor (Kind::script, writeScript (*s));
        p->setTag (0x165d1000 + (std::uint32_t) files);
        const auto begin = std::chrono::steady_clock::now();
        vfxs165::play (*p, { vfxs165::on (0, 60), vfxs165::on (0, 64), vfxs165::on (0, 67), vfxs165::off (44100, 60), vfxs165::off (44100, 64), vfxs165::off (44100, 67) },
                       88200);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - begin).count();
        if (ms > slowest)
            slowest = ms, slowestName = entry.path().filename().string();
        if (const auto m = script::monitorOf (0x165d1000 + (std::uint32_t) files))
            if (m->log().find ("Error") != std::string::npos)
                std::printf ("  %s ran with errors:\n%s\n", entry.path().filename().string().c_str(), m->log().c_str());
    }
    std::printf ("  %d of %d FL scripts compile; slowest: %s, 2 s of audio in %.1f ms\n", compiled, files, slowestName.c_str(), slowest);
    CHECK (files >= 80 && compiled == files);

    // FL's Snap To Scale (spec S6): Enable, C major, Down: 61 → 60, 63 → 62; Up: 61 → 62
    if (auto s = load (dir / "Snap To Scale.fst"))
    {
        const auto info = script::describe (*s);
        auto v = script::valuesOf (*info, *s);
        auto set = [&] (const char* name, double value) {
            if (const auto i = info->index (name); i >= 0)
                v.numbers[(std::size_t) i] = value;
        };
        set ("Snap to Scale: Enable", 1);
        set ("Snap to Scale: Key", 0);
        set ("Snap to Scale: Scale", 0);
        set ("Snap to Scale: Snap Direction", 1);
        script::writeValues (*s, *info, v);
        auto p = makeProcessor (Kind::script, writeScript (*s));
        auto keys = vfxs165::keysOn (vfxs165::play (*p, { vfxs165::on (0, 61), vfxs165::on (10, 63), vfxs165::on (20, 60) }, 512));
        CHECK ((keys == std::vector<int> { 60, 62, 60 }));
        set ("Snap to Scale: Snap Direction", 0);
        script::writeValues (*s, *info, v);
        p->configure (writeScript (*s));
        keys = vfxs165::keysOn (vfxs165::play (*p, { vfxs165::on (0, 61) }, 512));
        CHECK ((keys == std::vector<int> { 62 }));
    }
    // FL's Arpeggiator (Up, 2 per beat, 0.75 long): C E G held → C E G C… every 48 ticks
    if (auto s = load (dir / "Arpeggiator.fst"))
    {
        auto p = makeProcessor (Kind::script, writeScript (*s));
        const auto out = vfxs165::play (*p, { vfxs165::on (0, 60), vfxs165::on (0, 64), vfxs165::on (0, 67) }, 44100);
        std::vector<int> keys;
        std::vector<long> at;
        for (const auto& o : out)
            if (o.e.type == Event::Type::on)
                keys.push_back (o.e.voice.key), at.push_back (o.at);
        CHECK (keys.size() >= 4 && keys[0] == 60 && keys[1] == 64 && keys[2] == 67 && keys[3] == 60);
        CHECK (at.size() >= 2 && at[1] - at[0] == 11025);
    }
    // ... synced, Speed 3 (as in FL's render, round 165d): the arpeggio stops when the held notes are released
    if (auto s = load (dir / "Arpeggiator.fst"))
    {
        const auto info = script::describe (*s);
        auto v = script::valuesOf (*info, *s);
        for (auto [name, value] : { std::pair { "Sync", 1.0 }, { "Speed", 3.0 } })
            if (const auto i = info->index (name); i >= 0)
                v.numbers[(std::size_t) i] = value;
        script::writeValues (*s, *info, v);
        auto p = makeProcessor (Kind::script, writeScript (*s));
        const auto out = vfxs165::play (*p, { vfxs165::on (5512, 60), vfxs165::on (5512, 64), vfxs165::on (6890, 67), vfxs165::off (97387, 60),
                                              vfxs165::off (97387, 64), vfxs165::off (98765, 67) },
                                        176400);
        long last = 0;
        int ons = 0;
        for (const auto& o : out)
            if (o.e.type == Event::Type::on)
                last = o.at, ++ons;
        if (last > 99000)
            std::printf ("  synced arpeggio: %d notes, the last at %ld\n", ons, last);
        CHECK (ons >= 10 && last <= 99000);
    }
    // FL's Note Formula (spec S7): "note + 12", also for a note at the render's first sample (FL has ticked before)
    if (auto s = load (dir / "Note Formula Examples" / "1 - Raise the pitch by one octave.fst"))
    {
        auto p = makeProcessor (Kind::script, writeScript (*s));
        auto keys = vfxs165::keysOn (vfxs165::play (*p, { vfxs165::on (300, 60) }, 2048));
        CHECK ((keys == std::vector<int> { 72 }));
        auto first = makeProcessor (Kind::script, writeScript (*s));
        keys = vfxs165::keysOn (vfxs165::play (*first, { vfxs165::on (0, 60) }, 2048));
        CHECK ((keys == std::vector<int> { 72 }));
    }
}
