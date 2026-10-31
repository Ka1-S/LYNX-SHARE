// --- Edison's audio scripts (core/EdisonScript.h) - round 167c (REA_rodada167c.md §2)

#include "core/EdisonScript.h"

namespace edscript167
{
lynx::audioedit::Document ramp (int frames, int channels = 2, double rate = 44100.0)
{
    lynx::audioedit::Document d;
    d.audio = lynx::audioedit::silence (channels, rate, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            d.audio.channels[(std::size_t) c][(std::size_t) i] = (float) (i % 100) / 100.0f * (c == 0 ? 1.0f : -1.0f);
    return d;
}

// runs a script to its end, answering every form with its own defaults changed by `set` (name → value)
lynx::audioedit::script::Outcome runAll (const std::string& src, lynx::audioedit::Document& doc, lynx::audioedit::Range sel,
                                         const std::map<std::string, double>& set = {}, int* forms = nullptr,
                                         const lynx::audioedit::script::Host& host = {})
{
    using namespace lynx::audioedit::script;
    std::vector<Form> answers;
    for (int round = 0; round < 8; ++round)
    {
        auto copy = doc;
        auto out = run (src, copy, sel, answers, host);
        if (out.status != Outcome::Status::needsForm)
        {
            if (out.status == Outcome::Status::done)
                doc = std::move (copy);
            if (forms != nullptr)
                *forms = (int) answers.size();
            return out;
        }
        auto f = out.form;
        for (auto& c : f.controls)
            if (const auto it = set.find (c.name); it != set.end())
                c.value = it->second;
        answers.push_back (f);
    }
    return {};
}
} // namespace edscript167

TEST (edscript167_preview_style_and_selection)
{
    using namespace lynx::audioedit;
    // FL's Amp.pyscript shape: createDialog() + apply(form); the selection's frames, both ends included
    const std::string amp = "from enveditor import *\n"
                            "def createDialog():\n"
                            "  form=ScriptDialog('Amp', 'Simple amplification.')\n"
                            "  form.AddInputKnob('Volume', 1, 0, 2)\n"
                            "  return form\n"
                            "def apply(form):\n"
                            "  EditorSample.AmpFromTo(Editor.SelectionStartS, Editor.SelectionEndS, form.GetInputValue('Volume'))\n";
    auto doc = edscript167::ramp (1000);
    const auto before = doc.audio.channels[0];
    auto copy = doc;
    const auto first = script::run (amp, copy, Range { 100, 200 }, {}, {});
    CHECK (first.status == script::Outcome::Status::needsForm && first.form.title == "Amp" && first.form.controls.size() == 1);
    CHECK (first.form.controls[0].type == script::Control::Type::knob && first.form.controls[0].value == 1.0 && first.form.controls[0].maximum == 2.0);
    int forms = 0;
    const auto out = edscript167::runAll (amp, doc, Range { 100, 200 }, { { "Volume", 2.0 } }, &forms);
    CHECK (out.status == script::Outcome::Status::done && forms == 1);
    CHECK (doc.audio.channels[0][150] == before[150] * 2.0f && doc.audio.channels[0][99] == before[99] && doc.audio.channels[0][200] == before[200]);
    // no selection: the whole sample
    auto all = edscript167::ramp (500);
    edscript167::runAll (amp, all, Range {}, { { "Volume", 0.0 } });
    CHECK (all.audio.channels[0][499] == 0.0f && all.audio.channels[1][1] == 0.0f);
}

TEST (edscript167_execute_style_regions_and_messages)
{
    using namespace lynx::audioedit;
    // FL's Play to end.pyscript shape: a form at the top level (Execute), regions, Utils
    const std::string src = "from enveditor import *\n"
                            "form=ScriptDialog('Play To End','Converts markers.')\n"
                            "form.AddInputCombo('Reset','No,Yes',0)\n"
                            "if form.Execute():\n"
                            "  for i in range(EditorSample.RegionCount):\n"
                            "    r=EditorSample.GetRegion(i)\n"
                            "    r.SampleEnd = EditorSample.Length-1 if form.GetInputValue('Reset')==0 else r.SampleStart+9\n"
                            "  Utils.ShowMessage('done %d' % EditorSample.RegionCount)\n";
    auto doc = edscript167::ramp (1000);
    addMarker (doc, 100, "a");
    addMarker (doc, 400, "b");
    auto out = edscript167::runAll (src, doc, Range {});
    CHECK (out.status == script::Outcome::Status::done && out.messages.size() == 1 && out.messages[0] == "done 2");
    CHECK (doc.markers.size() == 2 && doc.markers[0].isRegion() && doc.markers[0].end == 1000 && doc.markers[1].end == 1000);
    auto doc2 = edscript167::ramp (1000);
    addMarker (doc2, 100, "a");
    edscript167::runAll (src, doc2, Range {}, { { "Reset", 1.0 } });
    CHECK (doc2.markers[0].start == 100 && doc2.markers[0].end == 110);
    // a marker reads as a region ending past the sample; AddRegion / DeleteRegion; Info, KeyNum
    const std::string regions = "from enveditor import *\n"
                                "assert EditorSample.GetRegion(0).SampleEnd > EditorSample.Length\n"
                                "i = EditorSample.AddRegion('loop', 10, 19)\n"
                                "r = EditorSample.GetRegion(i)\n"
                                "r.Info = 'x'\n"
                                "r.KeyNum = 60\n"
                                "EditorSample.DeleteRegion(EditorSample.RegionCount - 1)\n";
    auto doc3 = edscript167::ramp (1000);
    addMarker (doc3, 500, "m");
    out = edscript167::runAll (regions, doc3, Range {});
    CHECK (out.status == script::Outcome::Status::done);
    CHECK (doc3.markers.size() == 1 && doc3.markers[0].name == "loop" && doc3.markers[0].start == 10 && doc3.markers[0].end == 20
           && doc3.markers[0].info == "x" && doc3.markers[0].keyNum == 60);
}

TEST (edscript167_sample_api)
{
    using namespace lynx::audioedit;
    // FL's generator / FX script idioms: an empty editor gets a length, temp samples, paste modes, rate changes
    const std::string gen = "from enveditor import *\n"
                            "if EditorSample.Length<=0:\n"
                            "  EditorSample.Length = round(EditorSample.MSToSamples(1000))\n"
                            "EditorSample.SineFromTo(Editor.SelectionStartS, Editor.SelectionEndS, 441, 0.25, 0.5)\n"
                            "t = Sample()\n"
                            "t.NumChans = 1\n"
                            "t.Length = 100\n"
                            "t.SineFromTo(0, 99, 441, 0.25, 0.25)\n"
                            "EditorSample.PasteFromTo(t, 0, 99, 2)\n";
    Document doc;
    doc.audio = silence (2, 44100.0, 0);
    auto out = edscript167::runAll (gen, doc, Range {});
    CHECK (out.status == script::Outcome::Status::done);
    CHECK (doc.audio.frames() == 44100 && doc.audio.numChannels() == 2);
    // the sine starts at its phase (0.25 cycle = peak): 0.5 + the pasted 0.25 at frame 0, 0.5 after the paste
    CHECK (std::abs (doc.audio.channels[0][0] - 0.75f) < 1e-4f && std::abs (doc.audio.channels[1][0] - 0.75f) < 1e-4f);
    CHECK (std::abs (doc.audio.channels[0][100] - 0.5f) < 1e-4f); // 441 Hz: 100 frames = one cycle

    // Destructoid's way: a copy at a lower rate, processed, back to the editor's format; same length
    const std::string fx = "from enveditor import *\n"
                           "t = Sample()\n"
                           "t.SampleRate = EditorSample.SampleRate\n"
                           "t.NumChans = EditorSample.NumChans\n"
                           "t.PasteFromTo(EditorSample, 0, 0, 0)\n"
                           "assert t.Length == EditorSample.Length\n"
                           "t.SampleRate = 11025\n"
                           "assert abs(t.Length - EditorSample.Length / 4) <= 1\n"
                           "t.NormalizeFormat(EditorSample, NF_All)\n"
                           "assert abs(t.Length - EditorSample.Length) <= 4 and t.SampleRate == EditorSample.SampleRate\n"
                           "EditorSample.ReverseFromTo(0, 9)\n"
                           "EditorSample.DeleteFromTo(0, 9)\n"
                           "EditorSample.InsertSilence(0, 4)\n"
                           "EditorSample.NormalizeFromTo(0, EditorSample.Length - 1, 0.5)\n"
                           "EditorSample.ConvertToMono(1)\n";
    auto d2 = edscript167::ramp (4000);
    out = edscript167::runAll (fx, d2, Range {});
    CHECK (out.status == script::Outcome::Status::done);
    CHECK (d2.audio.frames() == 4000 - 10 + 5 && d2.audio.numChannels() == 1 && d2.audio.channels[0][0] == 0.0f);
    CHECK (std::abs (peak (d2.audio, Range { 0, d2.audio.frames() }) - 0.5f) < 1e-5f);

    // errors come back with Python's message; the document is untouched
    auto d3 = edscript167::ramp (100);
    auto copy = d3;
    out = script::run ("from enveditor import *\nEditorSample.GetRegion(3)\n", copy, Range {}, {}, {});
    CHECK (out.status == script::Outcome::Status::failed && out.error.find ("IndexError") != std::string::npos);
    out = script::run ("x = = 1\n", copy, Range {}, {}, {});
    CHECK (out.status == script::Outcome::Status::failed && ! out.error.empty());
    // LoadFromClipboard through the host
    script::Host host;
    host.clipboard = [] (Audio& a) {
        a = silence (1, 22050.0, 10);
        a.channels[0][3] = 0.5f;
        return true;
    };
    out = edscript167::runAll ("from enveditor import *\nc = Sample()\nc.LoadFromClipboard()\nassert c.Length == 10 and c.SampleRate == 22050\nEditorSample.SetSampleAt(0, 0, c.GetSampleAt(3, 0))\n", d3,
                               Range {}, {}, nullptr, host);
    CHECK (out.status == script::Outcome::Status::done && d3.audio.channels[0][0] == 0.5f);
}

TEST (edscript167_builtins_and_form_memory)
{
    using namespace lynx::audioedit;
    // LYNX's own scripts all run (their defaults) on a short stereo sample and on an empty editor
    for (const auto& b : script::builtins())
    {
        auto doc = edscript167::ramp (2000);
        addMarker (doc, 100);
        addMarker (doc, 900);
        const auto out = edscript167::runAll (b.source, doc, Range { 0, 1000 });
        CHECK (out.status == script::Outcome::Status::done);
        if (out.status != script::Outcome::Status::done)
            std::printf ("  builtin %s/%s: %s\n", b.folder, b.name, out.error.c_str());
    }
    // restoreFormValues: the last confirmed values come back for the same script
    script::Form f;
    f.title = "Amp";
    script::Control c;
    c.type = script::Control::Type::knob;
    c.name = "Volume";
    c.value = 1.7;
    c.maximum = 2.0;
    f.controls.push_back (c);
    script::remember ("amp.pyscript", f);
    f.controls[0].value = 1.0;
    script::restore ("amp.pyscript", f);
    CHECK (f.controls[0].value == 1.7);
    f.controls[0].value = 1.0;
    f.restoreValues = false;
    script::restore ("amp.pyscript", f);
    CHECK (f.controls[0].value == 1.0);
}
