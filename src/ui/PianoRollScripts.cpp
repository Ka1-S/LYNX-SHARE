// The Piano roll's scripts (Tools › Scripts; FL's Piano roll scripts, round 190as, REA_rodada190as-pyscript.md): the
// menu, the run on a worker thread (core/PianoRollScript.h does the work on the Python thread), the form with its
// live preview, and the result as one undo step.

#include "ui/PianoRollComponent.h"

#include <thread>

#include "core/KeyMap.h"
#include "core/PianoRollScript.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/ScriptPanel.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
const char* const scriptWindowKey = "pianoroll-script";

// Documents/LYNX/Piano roll scripts, beside LYNX's other user folders (Projects, Templates, Mixer states, Plugin
// database), as the docs say (round 190bj: the first builds of round 190as used Documents/Piano roll scripts)
juce::File myScriptsFolder()
{
    return platform::documentsFolder().getChildFile ("LYNX").getChildFile ("Piano roll scripts");
}

// that first folder, still read (listed when it holds scripts) so no one's scripts go missing
juce::File oldScriptsFolder()
{
    return platform::documentsFolder().getChildFile ("Piano roll scripts");
}

// where scripts come from besides LYNX's own: the user's Documents/LYNX/Piano roll scripts, then FL Studio's folders
// (read in place): its factory scripts, the user's FL scripts and the ones FL downloaded
std::vector<std::pair<juce::String, juce::File>> scriptFolders()
{
    static const auto fl = [] {
        std::vector<std::pair<juce::String, juce::File>> out;
        const auto roots = findFlStudioRoots();
        auto add = [&out] (const juce::String& title, const std::string& root, std::initializer_list<const char*> parts) {
            if (root.empty())
                return;
            auto dir = platform::localFile (root);
            for (const auto* p : parts)
                dir = dir.getChildFile (p);
            if (dir.isDirectory())
                out.emplace_back (title, dir);
        };
        add ("FL Studio", roots.factoryData, { "System", "Config", "Piano roll scripts" });
        add ("FL Studio (user)", roots.userData, { "Settings", "Piano roll scripts" });
        add ("FL Studio (downloads)", roots.downloadsData, { "Piano roll scripts" });
        return out;
    }();
    std::vector<std::pair<juce::String, juce::File>> out;
    out.emplace_back (tr ("My scripts"), myScriptsFolder());
    if (oldScriptsFolder().isDirectory())
        out.emplace_back (tr ("My scripts (old folder)"), oldScriptsFolder());
    out.insert (out.end(), fl.begin(), fl.end());
    return out;
}
} // namespace

struct PianoRollComponent::ScriptJob
{
    ScriptRef ref;
    std::string source;
    int patternIndex = 0, channel = 0;
    Pattern start;                 // the pattern as the script started
    std::vector<int> selection;    // and its selection
    std::int64_t timeFrom = -1, timeTo = -1;
    Note drawDefaults;
    int beatsPerBar = 4, beatUnit = 4;
    std::vector<prscript::Form> answers;
    // the last preview: what the Piano roll shows now
    bool previewShown = false;
    prscript::Form previewForm;
    bool previewValid = false;     // previewForm's result is in the pattern
};

struct PianoRollComponent::ScriptResult
{
    prscript::Outcome outcome;
    Pattern pattern;
    std::vector<int> selection;
    std::int64_t timeFrom = -1, timeTo = -1;
};

juce::PopupMenu PianoRollComponent::scriptsMenu()
{
    juce::PopupMenu menu;
    addScriptItems (menu);
    return menu;
}

// FL's Tools menu lists the scripts in its second column, under "Scripts" (REA round 143, A1)
void PianoRollComponent::addScriptItems (juce::PopupMenu& menu)
{
    const bool free = ! scriptBusy;
    auto keyText = [] (const std::string& id) { return shortcutText ? shortcutText (id) : juce::String(); };
    // FL's order (live capture, shots/pyscript-fl-tools-menu.png): Run last script again, Open script folder..., the scripts
    menu.addItem (tr ("Run last script again") + keyText ("pianoRoll.runLastScript"), free && lastScript.key.isNotEmpty(), false, [this] { runLastScript(); });
    menu.addItem (tr ("Open script folder..."), true, false, [] {
        const auto dir = myScriptsFolder();
        dir.createDirectory();
        if (platform::testMode())
            platform::testModeLog ("open folder " + dir.getFullPathName());
        else
            dir.startAsProcess();
    });
    menu.addSeparator();
    menu.addSectionHeader ("LYNX SHARE");
    for (const auto& b : prscript::builtins())
    {
        ScriptRef r;
        r.name = juce::String::fromUTF8 (b.name);
        r.key = "lynx:" + r.name;
        r.source = b.source;
        menu.addItem (r.name, free, false, [this, r] { runScript (r); });
    }
    const auto mine = myScriptsFolder();
    for (const auto& [title, dir] : scriptFolders())
    {
        // (an FL folder without scripts is left out; My scripts always shows)
        if (dir != mine && dir.findChildFiles (juce::File::findFiles, true, "*.pyscript").isEmpty())
            continue;
        menu.addSectionHeader (title);
        auto addFolder = [this, free] (const juce::File& d, juce::PopupMenu& into, auto& self, int depth) -> bool {
            bool any = false;
            auto subs = d.findChildFiles (juce::File::findDirectories, false);
            subs.sort();
            for (const auto& s : subs)
            {
                juce::PopupMenu sub;
                if (depth < 3 && self (s, sub, self, depth + 1))
                {
                    into.addSubMenu (s.getFileName(), sub);
                    any = true;
                }
            }
            auto files = d.findChildFiles (juce::File::findFiles, false, "*.pyscript");
            files.sort();
            for (const auto& f : files)
            {
                ScriptRef r;
                r.name = f.getFileNameWithoutExtension();
                r.key = f.getFullPathName();
                r.file = f;
                into.addItem (r.name, free, false, [this, r] { runScript (r); });
                any = true;
            }
            return any;
        };
        if (! dir.isDirectory() || ! addFolder (dir, menu, addFolder, 0))
            menu.addItem (tr ("(no scripts)"), false, false, nullptr);
    }
}

void PianoRollComponent::runLastScript()
{
    if (lastScript.key.isNotEmpty())
        runScript (lastScript);
}

void PianoRollComponent::showScriptText (const juce::String& title, const juce::String& text)
{
    if (platform::testMode())
    {
        platform::testModeLog (title + ": " + text);
        return;
    }
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, title, text, tr ("OK"), this);
}

void PianoRollComponent::runScriptForSnapshot (const juce::String& name)
{
    for (const auto& b : prscript::builtins())
        if (name == b.name)
        {
            ScriptRef r;
            r.name = name;
            r.key = "lynx:" + name;
            r.source = b.source;
            runScript (r);
            return;
        }
    for (const auto& [title, dir] : scriptFolders())
    {
        const auto f = dir.getChildFile (name + ".pyscript");
        if (f.existsAsFile())
        {
            ScriptRef r;
            r.name = name;
            r.key = f.getFullPathName();
            r.file = f;
            runScript (r);
            return;
        }
    }
}

void PianoRollComponent::runScript (const ScriptRef& ref)
{
    if (scriptBusy)
        return;
    if (auto* open = findToolWindow (scriptWindowKey)) // (a script's form is still open: it goes first, its preview undone)
        closeToolWindowOf (*open->getContentComponent());
    auto job = std::make_shared<ScriptJob>();
    job->ref = ref;
    if (ref.file != juce::File())
    {
        juce::MemoryBlock bytes;
        if (! ref.file.loadFileAsData (bytes) || ! juce::CharPointer_UTF8::isValidString (static_cast<const char*> (bytes.getData()), (int) bytes.getSize()))
        {
            showScriptText (tr ("Error"), tr ("Failed to load the script file. Please make sure it is saved in utf8-format."));
            return;
        }
        job->source.assign (static_cast<const char*> (bytes.getData()), bytes.getSize());
    }
    else
        job->source = ref.source;
    lastScript = ref;
    const auto& song = engine.getSong();
    if (song.patterns.empty())
        return;
    job->patternIndex = song.currentPattern;
    job->channel = engine.getSelectedChannel();
    job->start = song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)];
    job->selection = selection;
    job->timeFrom = timeFrom;
    job->timeTo = timeTo;
    job->beatsPerBar = juce::jmax (1, song.beatsPerBar);
    job->beatUnit = meterUnit (song.beatUnit);
    job->drawDefaults.channel = job->channel;
    job->drawDefaults.length = lastLength;
    scriptStep (job, job->answers, false);
}

// runs the script on a worker thread (the Python thread does the work; the UI never waits for it)
void PianoRollComponent::scriptStep (std::shared_ptr<ScriptJob> job, std::vector<scriptform::Form> answers, bool preview)
{
    if (! preview)
        scriptBusy = true;
    const auto ticket = ++scriptTicket;
    juce::Component::SafePointer<PianoRollComponent> self (this);
    std::thread ([self, job, answers = std::move (answers), preview, ticket] {
        auto r = std::make_shared<ScriptResult>();
        r->pattern = job->start;
        auto score = prscript::scoreOf (job->start, job->channel, job->selection, job->beatsPerBar, job->beatUnit, job->timeFrom, job->timeTo, job->drawDefaults);
        r->outcome = prscript::run (job->source, score, answers, preview ? 5000.0 : 20000.0);
        if (r->outcome.status == prscript::Outcome::Status::done)
        {
            r->selection = prscript::apply (r->pattern, job->channel, score);
            r->timeFrom = score.selectionEnd > score.selectionStart ? score.selectionStart : -1;
            r->timeTo = score.selectionEnd > score.selectionStart ? score.selectionEnd : -1;
        }
        juce::MessageManager::callAsync ([self, job, r, preview, ticket] {
            if (self != nullptr)
                self->scriptFinished (job, *r, preview, ticket);
        });
    }).detach();
}

bool PianoRollComponent::scriptTargetStill (const ScriptJob& job) const
{
    const auto& song = engine.getSong();
    return song.currentPattern == job.patternIndex && engine.getSelectedChannel() == job.channel && ! song.patterns.empty();
}

// puts `pattern`'s notes and markers into the Piano roll's pattern (the rest of the pattern stays as it is)
void PianoRollComponent::showScriptPattern (const Pattern& pattern, const std::vector<int>& select, bool undoable)
{
    editNotes ([&pattern] (Pattern& p) {
        p.notes = pattern.notes;
        p.markers = pattern.markers;
    }, undoable);
    setSelection (select);
    repaint();
}

void PianoRollComponent::scriptFinished (std::shared_ptr<ScriptJob> job, ScriptResult& r, bool preview, std::uint64_t ticket)
{
    using S = prscript::Outcome::Status;
    auto& out = r.outcome;
    if (preview)
    {
        // only the newest preview counts, and only while its form is still open
        if (ticket != scriptTicket || findToolWindow (scriptWindowKey) == nullptr || out.status != S::done || ! scriptTargetStill (*job))
            return;
        showScriptPattern (r.pattern, r.selection, false);
        job->previewShown = true;
        job->previewValid = true;
        return;
    }

    scriptBusy = false;
    if (out.status == S::failed)
    {
        // FL's wording (its script form: "Error|There was an error in the Python script:")
        showScriptText (tr ("Script error"), juce::String::fromUTF8 (out.error.c_str()));
        return;
    }
    juce::Component::SafePointer<PianoRollComponent> self (this);
    if (out.status == S::needsForm)
    {
        auto form = out.form;
        const auto key = job->ref.key.toStdString();
        scriptform::restore (key, form);
        ScriptPanel::Options options;
        options.acceptText = tr ("Accept"); // (FL's button)
        options.previewOn = true;           // (FL: Preview is ticked)
        options.dismissed = [self, job] {
            // closed without Accept: the preview goes, the notes are as before
            if (self != nullptr && job->previewShown && self->scriptTargetStill (*job))
                self->showScriptPattern (job->start, job->selection, false);
            job->previewShown = false;
        };
        auto panel = std::make_unique<ScriptPanel> (
            form,
            [self, job, key] (const scriptform::Form& f) {
                scriptform::remember (key, f);
                if (self == nullptr)
                    return;
                // what the preview shows is what is accepted (a script with random choices is not run again)
                if (job->previewValid && job->previewShown && job->previewForm.controls.size() == f.controls.size() && self->scriptTargetStill (*job)
                    && std::equal (f.controls.begin(), f.controls.end(), job->previewForm.controls.begin(),
                                   [] (const scriptform::Control& a, const scriptform::Control& b) { return a.value == b.value && a.text == b.text; }))
                {
                    const auto shown = self->engine.getSong().patterns[(std::size_t) job->patternIndex];
                    const auto select = self->selection;
                    self->showScriptPattern (job->start, job->selection, false);
                    self->commitScript (*job, shown, select, self->timeFrom, self->timeTo, false);
                    return;
                }
                if (job->previewShown && self->scriptTargetStill (*job))
                    self->showScriptPattern (job->start, job->selection, false);
                job->previewShown = false;
                job->answers.push_back (f);
                self->scriptStep (job, job->answers, false);
            },
            [self, job] (const scriptform::Form& f) {
                if (self == nullptr)
                    return;
                job->previewForm = f;
                job->previewValid = false;
                auto answers = job->answers;
                answers.push_back (f);
                self->scriptStep (job, std::move (answers), true);
            },
            nullptr, options);
        // (FL's window carries the form's title)
        openToolWindow (std::move (panel), form.title.empty() ? job->ref.name : juce::String::fromUTF8 (form.title.c_str()), scriptWindowKey);
        return;
    }

    // done
    for (const auto& m : out.messages)
        showScriptText (tr ("Script message"), juce::String::fromUTF8 (m.c_str()));
    if (! scriptTargetStill (*job) || ! (engine.getSong().patterns[(std::size_t) job->patternIndex] == job->start))
        return; // (the notes changed while the script ran: its result is dropped)
    commitScript (*job, r.pattern, r.selection, r.timeFrom, r.timeTo, true);
}

// the script's result as one undo step (none when it changed nothing)
void PianoRollComponent::commitScript (const ScriptJob& job, const Pattern& result, const std::vector<int>& select, std::int64_t from, std::int64_t to,
                                       bool takeTimeSelection)
{
    if (! (result.notes == job.start.notes && result.markers == job.start.markers))
        showScriptPattern (result, select, true);
    else
        setSelection (select);
    if (takeTimeSelection && (from != job.timeFrom || to != job.timeTo))
        setTimeSelection (from, to);
    if (platform::testMode())
        platform::testModeLog ("piano roll script " + job.ref.name + " done");
}
} // namespace lynx
