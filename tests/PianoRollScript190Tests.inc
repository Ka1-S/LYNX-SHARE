// --- Piano roll scripts (core/PianoRollScript.h) - round 190as (REA_rodada190as-pyscript.md)

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/KeyMap.h"
#include "core/PianoRollScript.h"

namespace prscript190
{
// a pattern with a C major arpeggio on channel 0 (4 notes, one per beat) and two notes of channel 1
lynx::Pattern pattern()
{
    lynx::Pattern p;
    const int keys[] = { 60, 64, 67, 72 };
    for (int i = 0; i < 4; ++i)
    {
        lynx::Note n;
        n.channel = 0;
        n.key = keys[i];
        n.start = (std::int64_t) i * lynx::ppq;
        n.length = lynx::ppq / 2;
        n.velocity = 0.5f + 0.125f * (float) i;
        p.notes.push_back (n);
    }
    for (int i = 0; i < 2; ++i)
    {
        lynx::Note n;
        n.channel = 1;
        n.key = 36;
        n.start = (std::int64_t) i * 2 * lynx::ppq + 10;
        n.length = 100;
        n.group = 3;
        p.notes.push_back (n);
    }
    p.sortNotes();
    return p;
}

std::vector<int> channelIndices (const lynx::Pattern& p, int channel)
{
    std::vector<int> out;
    for (std::size_t i = 0; i < p.notes.size(); ++i)
        if (p.notes[i].channel == channel)
            out.push_back ((int) i);
    return out;
}

struct Result
{
    lynx::prscript::Outcome outcome;
    std::vector<int> selected;
    int forms = 0;
};

// runs a script to its end on channel 0 of `p`, answering every form with its defaults changed by `set`
Result runAll (const std::string& src, lynx::Pattern& p, const std::vector<int>& selection = {}, const std::map<std::string, double>& set = {},
               double limitMs = 20000.0, std::int64_t timeFrom = 0, std::int64_t timeTo = 0)
{
    using namespace lynx::prscript;
    Result r;
    std::vector<Form> answers;
    for (int round = 0; round < 8; ++round)
    {
        auto score = scoreOf (p, 0, selection, 4, 4, timeFrom, timeTo, lynx::Note {});
        r.outcome = run (src, score, answers, limitMs);
        if (r.outcome.status != Outcome::Status::needsForm)
        {
            if (r.outcome.status == Outcome::Status::done)
                r.selected = apply (p, 0, score);
            r.forms = (int) answers.size();
            return r;
        }
        auto f = r.outcome.form;
        for (auto& c : f.controls)
            if (const auto it = set.find (c.name); it != set.end())
                c.value = it->second;
        answers.push_back (f);
    }
    return r;
}

bool done (const Result& r)
{
    if (r.outcome.status != lynx::prscript::Outcome::Status::done)
        std::printf ("  script: %s\n", r.outcome.error.c_str());
    return r.outcome.status == lynx::prscript::Outcome::Status::done;
}

bool failedWith (const Result& r, const char* text)
{
    const bool ok = r.outcome.status == lynx::prscript::Outcome::Status::failed && r.outcome.error.find (text) != std::string::npos;
    if (! ok)
        std::printf ("  expected an error with '%s', got status %d: %s\n", text, (int) r.outcome.status, r.outcome.error.c_str());
    return ok;
}
} // namespace prscript190

TEST (prscript190_reference_example_and_what_survives)
{
    // the example of FL's reference: every note muted; the other channel and LYNX-only note data stay
    auto p = prscript190::pattern();
    for (auto& n : p.notes)
        if (n.channel == 0)
            n.chance = 0.25f;
    const auto before = p;
    const auto r = prscript190::runAll ("from flpianoroll import *\n"
                                        "for i in range(score.noteCount) :\n"
                                        "  score.getNote(i).muted = True\n",
                                        p);
    CHECK (prscript190::done (r));
    CHECK (r.forms == 0);
    CHECK (p.notes.size() == before.notes.size());
    for (std::size_t i = 0; i < p.notes.size(); ++i)
    {
        CHECK (p.notes[i].muted == (p.notes[i].channel == 0));
        CHECK (p.notes[i].key == before.notes[i].key && p.notes[i].start == before.notes[i].start);
        CHECK (p.notes[i].chance == before.notes[i].chance);
        CHECK (p.notes[i].velocity == before.notes[i].velocity);
    }
}

TEST (prscript190_score_values_and_units)
{
    // PPQ and the time signature, FL's units (pan 0..1 with 0.5 = centre, pitchofs = fine pitch), Note() defaults
    auto p = prscript190::pattern();
    p.notes[0].pan = -1.0f;
    p.notes[0].finePitch = -30.0f;
    p.notes[0].modX = 0.25f;
    p.notes[0].colour = 5;
    p.notes[0].slide = true;
    const auto r = prscript190::runAll ("import flpianoroll as flp\n"
                                        "s = flp.score\n"
                                        "n = s.getNote(0)\n"
                                        "print(s.PPQ, s.tsnum, s.tsden, s.noteCount, s.markerCount, s.length)\n"
                                        "print(n.number, n.time, n.length, n.pan, n.pitchofs, n.fcut, n.color, n.slide, n.porta, n.selected, n.group)\n"
                                        "d = flp.Note()\n"
                                        "print(d.number, d.time, d.length, d.pan, d.velocity, d.release, d.color, d.pitchofs, d.repeats, d.muted)\n"
                                        "print(round(d.fcut * 255), round(d.fres * 255))\n"
                                        "print(s.getNote(99), s.getNote(-1), s.getTimelineSelection(), s.getNextFreeGroupIndex())\n"
                                        "print(s.snap_root_note, s.snap_scale_helper)\n",
                                        p);
    CHECK (prscript190::done (r));
    const auto expected = std::to_string (lynx::ppq) + " 4 4 4 0 " + std::to_string (3 * lynx::ppq + lynx::ppq / 2) + "\n"
                          "60 0 " + std::to_string (lynx::ppq / 2) + " 0.0 -30 0.25 5 True False False 0\n"
                          "0 0 0 0.5 0.78125 0.5 0 0 0 False\n"
                          "128 128\n"
                          "None None (0, -1) 1\n"
                          "0 0,0,0,0,0,0,0,0,0,0,0,0\n";
    if (r.outcome.log != expected)
        std::printf ("  log:\n%s", r.outcome.log.c_str());
    CHECK (r.outcome.log == expected);
}

TEST (prscript190_live_notes_add_clone_delete)
{
    // getNote() hands out the live note, addNote() takes the object itself (FL: changes after the call count, which
    // FL's Note repeater script relies on); clone() copies; deleteNote() of a bad index raises
    auto p = prscript190::pattern();
    const auto r = prscript190::runAll ("import flpianoroll as flp\n"
                                        "a = flp.score.getNote(1)\n"
                                        "print(a is flp.score.getNote(1))\n"
                                        "c = a.clone()\n"
                                        "print(c is a, c.number == a.number, c.time == a.time)\n"
                                        "flp.score.addNote(c)\n"
                                        "c.number = a.number + 12\n" // after addNote
                                        "c.velocity = 2.5\n"         // out of range: clamped when stored
                                        "c.pan = -3\n"
                                        "n = flp.Note()\n"
                                        "n.number = 48\n"
                                        "n.time = 100\n"
                                        "n.length = 50\n"
                                        "flp.score.addNote(n)\n"
                                        "print(flp.score.noteCount)\n"
                                        "flp.score.deleteNote(0)\n"
                                        "print(flp.score.noteCount, flp.score.getNote(0) is a)\n"
                                        "try:\n"
                                        "  flp.score.deleteNote(77)\n"
                                        "except Exception as e:\n"
                                        "  print('error', e)\n",
                                        p);
    CHECK (prscript190::done (r));
    CHECK (r.outcome.log == "True\nFalse True True\n6\n5 True\nerror Invalid note index\n");
    const auto mine = prscript190::channelIndices (p, 0);
    CHECK (mine.size() == 5);
    int found = 0;
    for (const int i : mine)
    {
        const auto& n = p.notes[(std::size_t) i];
        CHECK (n.key != 60); // the deleted note
        if (n.key == 76)
        {
            ++found;
            CHECK (n.start == lynx::ppq && n.velocity == 1.0f && n.pan == -1.0f);
        }
        if (n.key == 48)
        {
            ++found;
            CHECK (n.start == 100 && n.length == 50 && n.velocity == 0.78125f && n.pan == 0.0f && n.release == 0.5f);
        }
    }
    CHECK (found == 2);
    CHECK (std::is_sorted (p.notes.begin(), p.notes.end(), [] (const lynx::Note& a, const lynx::Note& b) { return a.start < b.start; }));
    CHECK (prscript190::channelIndices (p, 1).size() == 2);
}

TEST (prscript190_selection_limits_what_the_script_sees)
{
    // with notes selected the script sees only those (FL: the visible flag of the score, REA §3); clearNotes() without
    // True removes the still-selected ones, and the notes left selected come back as the selection
    auto p = prscript190::pattern();
    const auto mine = prscript190::channelIndices (p, 0);
    const std::vector<int> selection { mine[1], mine[2] };
    auto q = p;
    auto r = prscript190::runAll ("import flpianoroll as flp\n"
                                  "print(flp.score.noteCount, [flp.score.getNote(i).number for i in range(flp.score.noteCount)])\n"
                                  "print([flp.score.getNote(i).selected for i in range(flp.score.noteCount)])\n"
                                  "flp.score.getNote(0).selected = False\n"
                                  "n = flp.Note()\n"
                                  "n.number = 50\n"
                                  "n.length = 10\n"
                                  "flp.score.addNote(n)\n"
                                  "flp.score.clearNotes()\n" // removes only note 67 (still selected)
                                  "print(flp.score.noteCount)\n"
                                  "n.selected = True\n",
                                  q, selection);
    CHECK (prscript190::done (r));
    CHECK (r.outcome.log == "2 [64, 67]\n[True, True]\n2\n");
    CHECK (prscript190::channelIndices (q, 0).size() == 4); // 60, 64, 72 + the new 50
    CHECK (r.selected.size() == 1);
    if (r.selected.size() == 1)
        CHECK (q.notes[(std::size_t) r.selected[0]].key == 50);
    for (const auto& n : q.notes)
        CHECK (n.key != 67);

    // without a selection: all of the channel's notes, clearNotes() removes them all, the other channel stays
    q = p;
    r = prscript190::runAll ("import flpianoroll as flp\nprint(flp.score.noteCount)\nflp.score.clearNotes(False)\nprint(flp.score.noteCount)\n", q);
    CHECK (prscript190::done (r));
    CHECK (r.outcome.log == "4\n0\n");
    CHECK (q.notes.size() == 2 && q.notes[0].channel == 1);

    // getNextFreeGroupIndex() also counts the groups of the notes the script does not see (channel 1 uses group 3)
    q = p;
    q.notes[(std::size_t) mine[0]].group = 1;
    q.notes[(std::size_t) mine[1]].group = 2;
    r = prscript190::runAll ("import flpianoroll as flp\nprint(flp.score.getNextFreeGroupIndex())\n", q);
    CHECK (r.outcome.log == "4\n");
}

TEST (prscript190_members_are_typed_as_in_fl)
{
    // FL's Note members are typed slots: an int member refuses a float, a bool member anything but a bool
    auto p = prscript190::pattern();
    const auto before = p;
    CHECK (prscript190::failedWith (prscript190::runAll ("import flpianoroll as flp\nflp.score.getNote(0).time = 1.5\n", p), "TypeError"));
    CHECK (prscript190::failedWith (prscript190::runAll ("import flpianoroll as flp\nflp.score.getNote(0).selected = 1\n", p), "TypeError"));
    CHECK (prscript190::failedWith (prscript190::runAll ("import flpianoroll as flp\nflp.score.getNote(0).velocity = 'x'\n", p), "TypeError"));
    CHECK (prscript190::failedWith (prscript190::runAll ("import flpianoroll as flp\nflp.score.addNote(5)\n", p), "TypeError"));
    CHECK (p == before); // a failed script changes nothing
    // an int into a float member is fine
    CHECK (prscript190::done (prscript190::runAll ("import flpianoroll as flp\nflp.score.getNote(0).velocity = 1\n", p)));
    CHECK (p.notes[0].velocity == 1.0f);
}

TEST (prscript190_markers_and_timeline_selection)
{
    auto p = prscript190::pattern();
    lynx::Marker a;
    a.tick = lynx::ppq;
    a.name = "Verse 'one'";
    p.markers.push_back (a);
    lynx::Marker sig;
    sig.tick = 8 * lynx::ppq;
    sig.numerator = 3;
    sig.denominator = 4;
    sig.action = lynx::Marker::timeSignature;
    p.markers.push_back (sig);
    auto q = p;
    auto r = prscript190::runAll ("import flpianoroll as flp\n"
                                  "s = flp.score\n"
                                  "print(s.markerCount, s.getMarker(0).name, s.getMarker(0).time, s.getMarker(0).mode)\n"
                                  "print(s.getMarker(1).mode, s.getMarker(1).tsnum, s.getMarker(1).tsden, s.getMarker(5))\n"
                                  "m = flp.Marker()\n"
                                  "print(m.time, m.name == '', m.mode, m.tsnum, m.tsden)\n"
                                  "m.name = 'Drop'\n"
                                  "m.time = 2 * s.PPQ\n"
                                  "s.addMarker(m)\n"
                                  "s.deleteMarker(0)\n"
                                  "print(s.getTimelineSelection())\n"
                                  "s.setTimelineSelection(10, 500)\n",
                                  q, {}, {}, 20000.0, 960, 1920);
    CHECK (prscript190::done (r));
    CHECK (r.outcome.log == "2 Verse 'one' 960 0\n8 3 4 None\n0 True 0 4 4\n(960, 1920)\n");
    CHECK (q.markers.size() == 2);
    if (q.markers.size() == 2)
    {
        CHECK (q.markers[0].name == "Drop" && q.markers[0].tick == 2 * lynx::ppq && q.markers[0].action == 0);
        CHECK (q.markers[1].isSignature() && q.markers[1].numerator == 3);
    }

    // clearMarkers(): only the markers inside the notes' time range (FL); clearMarkers(True): all of them
    q = p;
    r = prscript190::runAll ("import flpianoroll as flp\nflp.score.clearMarkers()\nprint(flp.score.markerCount)\n", q);
    CHECK (r.outcome.log == "1\n");
    CHECK (q.markers.size() == 1 && q.markers[0].isSignature());
    q = p;
    r = prscript190::runAll ("import flpianoroll as flp\nflp.score.clear(True)\nprint(flp.score.markerCount, flp.score.noteCount)\n", q);
    CHECK (r.outcome.log == "0 0\n");
    CHECK (q.markers.empty() && q.notes.size() == 2);
}

TEST (prscript190_preview_style_form_and_messages)
{
    // createDialog() + apply(form): the first run stops for the form, the next applies it with the answers
    const std::string src = "import flpianoroll as flp\n"
                            "def createDialog():\n"
                            "  form = flp.ScriptDialog('Transpose', 'Moves the notes.')\n"
                            "  form.AddInputKnobInt('Semitones', 2, -24, 24)\n"
                            "  form.addInputCheckbox('Mute', False)\n"
                            "  form.AddInputCombo('Mode', 'a,b,c', 1)\n"
                            "  return form\n"
                            "def apply(form):\n"
                            "  for i in range(flp.score.noteCount):\n"
                            "    flp.score.getNote(i).number += form.GetInputValue('Semitones')\n"
                            "    flp.score.getNote(i).muted = form.getInputValue('Mute')\n"
                            "  flp.Utils.ShowMessage('mode ' + str(form.GetInputValue('Mode')))\n"
                            "  flp.Utils.log('logged')\n";
    auto p = prscript190::pattern();
    auto score = lynx::prscript::scoreOf (p, 0, {}, 4, 4, 0, 0, lynx::Note {});
    const auto first = lynx::prscript::run (src, score, {});
    CHECK (first.status == lynx::prscript::Outcome::Status::needsForm);
    CHECK (first.form.title == "Transpose" && first.form.description == "Moves the notes." && first.form.controls.size() == 3);
    if (first.form.controls.size() == 3)
    {
        CHECK (first.form.controls[0].name == "Semitones" && first.form.controls[0].value == 2.0 && first.form.controls[0].minimum == -24.0);
        CHECK (first.form.controls[2].options.size() == 3 && first.form.controls[2].value == 1.0);
    }
    const auto r = prscript190::runAll (src, p, {}, { { "Semitones", 7.0 }, { "Mute", 1.0 } });
    CHECK (prscript190::done (r));
    CHECK (r.forms == 1);
    CHECK (r.outcome.messages.size() == 1 && r.outcome.messages[0] == "mode 1");
    CHECK (r.outcome.log == "logged\n");
    const auto mine = prscript190::channelIndices (p, 0);
    CHECK (p.notes[(std::size_t) mine[0]].key == 67 && p.notes[(std::size_t) mine[0]].muted);

    // FL's messages for a createDialog() that returns no ScriptDialog
    CHECK (prscript190::failedWith (prscript190::runAll ("def createDialog():\n  return 5\ndef apply(form):\n  pass\n", p),
                                    "createDialog function needs to return a ScriptDialog instance"));
    CHECK (prscript190::failedWith (prscript190::runAll ("def createDialog():\n  pass\ndef apply(form):\n  pass\n", p), "no dialog was created in createDialog"));

    // a script that shows its own dialog (Execute) works the same way
    const auto own = prscript190::runAll ("from flpianoroll import *\n"
                                          "form = ScriptDialog('Own', '')\n"
                                          "form.addInputKnob('Velocity', 0.5, 0, 1)\n"
                                          "if form.execute():\n"
                                          "  for i in range(score.noteCount):\n"
                                          "    score.getNote(i).velocity = form.getInputValue('Velocity')\n",
                                          p, {}, { { "Velocity", 0.25 } });
    CHECK (prscript190::done (own) && own.forms == 1);
    CHECK (p.notes[(std::size_t) mine[1]].velocity == 0.25f);
}

TEST (prscript190_timeout_and_sandbox)
{
    auto p = prscript190::pattern();
    const auto before = p;
    // a script that never ends is stopped
    CHECK (prscript190::failedWith (prscript190::runAll ("import flpianoroll as flp\nwhile True:\n  pass\n", p, {}, {}, 150.0), "TimeoutError"));
    // no files, no programs, no network, no file reader
    for (const char* src : { "open('/etc/passwd')\n", "open('lynx-test-file.txt', 'w')\n", "import os\nos.system('echo hi')\n", "import socket\n",
                             "import subprocess\n", "import urllib\n", "__import__('os')\n",
                             "import flpianoroll as flp\nflp.score.loadScoreFromFile('x.fsc')\n" })
    {
        const auto r = prscript190::runAll (src, p);
        CHECK (r.outcome.status == lynx::prscript::Outcome::Status::failed);
        if (r.outcome.status != lynx::prscript::Outcome::Status::failed)
            std::printf ("  ran: %s", src);
    }
    CHECK (! std::filesystem::exists ("lynx-test-file.txt"));
    CHECK (p == before);
    // and the interpreter is fine afterwards
    CHECK (prscript190::done (prscript190::runAll ("import flpianoroll as flp\nflp.score.getNote(0).muted = True\n", p)));
}

TEST (prscript190_lynx_scripts_run)
{
    for (const auto& b : lynx::prscript::builtins())
    {
        auto p = prscript190::pattern();
        const auto before = p;
        const auto r = prscript190::runAll (b.source, p);
        CHECK (prscript190::done (r));
        CHECK (! (p == before));
        if (p == before)
            std::printf ("  %s changed nothing\n", b.name);
    }
    // Octave layers: one copy per note, an octave up
    auto p = prscript190::pattern();
    CHECK (prscript190::done (prscript190::runAll (lynx::prscript::builtins()[1].source, p)));
    CHECK (prscript190::channelIndices (p, 0).size() == 8);
    // Mirror in time: the last note's end becomes the first note's start
    p = prscript190::pattern();
    CHECK (prscript190::done (prscript190::runAll (lynx::prscript::builtins()[2].source, p)));
    const auto mine = prscript190::channelIndices (p, 0);
    CHECK (p.notes[(std::size_t) mine[0]].key == 72 && p.notes[(std::size_t) mine[0]].start == 0);
}

TEST (prscript190_fl_scripts_run_unmodified)
{
    // the scripts FL Studio ships, read from the installation (never copied): each runs to its end with its own
    // default form values and does what its name says
    if (realFlpDir.empty())
        return;
    const auto dir = std::filesystem::path (realFlpDir).parent_path() / "System" / "Config" / "Piano roll scripts";
    if (! std::filesystem::is_directory (dir))
    {
        std::printf ("  (no Piano roll scripts in this FL installation: skipped)\n");
        return;
    }
    auto read = [&dir] (const char* name) {
        std::ifstream in (dir / name, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    };
    int ran = 0;
    {
        const auto src = read ("Euclidean.pyscript");
        CHECK (! src.empty());
        auto p = prscript190::pattern();
        const auto r = prscript190::runAll (src, p);
        CHECK (prscript190::done (r) && r.forms == 1);
        // 8 + 13 + 18 onsets over 32 steps of a 16th, added to the 4 notes
        CHECK (prscript190::channelIndices (p, 0).size() == 4 + 8 + 13 + 18);
        ran += prscript190::done (r);
    }
    {
        const auto src = read ("Humanize.pyscript");
        auto p = prscript190::pattern();
        const auto before = p;
        const auto r = prscript190::runAll (src, p, {}, { { "Seed", 3.0 } });
        CHECK (prscript190::done (r) && r.forms == 1);
        CHECK (p.notes.size() == before.notes.size());
        CHECK (! (p == before));
        auto q = before; // the same seed gives the same result
        CHECK (prscript190::done (prscript190::runAll (src, q, {}, { { "Seed", 3.0 } })));
        CHECK (p == q);
        ran += prscript190::done (r);
    }
    {
        const auto src = read ("Note repeater sprinkler.pyscript");
        auto p = prscript190::pattern();
        const auto r = prscript190::runAll (src, p);
        CHECK (prscript190::done (r) && r.forms == 1);
        CHECK (prscript190::channelIndices (p, 0).size() == 4 * (1 + 3)); // 3 repeats of each note
        ran += prscript190::done (r);
    }
    {
        const auto src = read ("Select by velocity and more.pyscript");
        auto p = prscript190::pattern();
        // durations up to 100 ticks at the script's defaults: make two notes that short
        const auto mine = prscript190::channelIndices (p, 0);
        p.notes[(std::size_t) mine[0]].length = 50;
        p.notes[(std::size_t) mine[3]].length = 80;
        const auto r = prscript190::runAll (src, p);
        CHECK (prscript190::done (r) && r.forms == 1);
        CHECK (r.selected.size() == 2);
        ran += prscript190::done (r);
    }
    {
        const auto src = read ("Arpeggiator.pyscript");
        auto p = prscript190::pattern();
        for (auto& n : p.notes) // a held chord
            if (n.channel == 0)
            {
                n.start = 0;
                n.length = 2 * lynx::ppq;
            }
        const auto r = prscript190::runAll (src, p, {}, { { "Seed", 5.0 }, { "Note Prob", 1.0 } });
        CHECK (prscript190::done (r) && r.forms == 1);
        // 16th steps over two beats: 8 notes, each a 16th long, walking up the chord
        const auto mine = prscript190::channelIndices (p, 0);
        CHECK (mine.size() == 8);
        for (const int i : mine)
            CHECK (p.notes[(std::size_t) i].length == lynx::ppq / 4);
        ran += prscript190::done (r);
    }
    std::printf ("  FL Studio's own Piano roll scripts run unmodified: %d of 5\n", ran);
}

TEST (prscript190_run_last_script_key)
{
    // FL's key for Tools > Scripts > Run last script again (live capture): Ctrl+Alt+Y in the Piano roll
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.find ({ 'Y', ctrl | alt }, Context::pianoRoll, false) == "pianoRoll.runLastScript");
    CHECK (map.find ({ 'Y', alt }, Context::pianoRoll, false) == "pianoRoll.flip");
    CHECK (map.conflicts().empty());
}
