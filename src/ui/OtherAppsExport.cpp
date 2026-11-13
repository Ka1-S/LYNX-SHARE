#include "ui/OtherAppsExport.h"

#include <map>

#include "core/MessageThreadOwned.h"
#include "core/QuickRender.h"
#include "engine/Exporter.h"
#include "engine/FlStudioExport.h"
#include "ui/AlsExportDialog.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }
} // namespace

namespace otherapps
{
bool writeMidi (const Song& song, const std::vector<Note>& notes, const juce::File& file, juce::String& error)
{
    // as MidiFileIo::savePattern (FL's MIDI export: one track per channel, the note colour = the MIDI channel), plus the key
    std::map<int, juce::MidiMessageSequence> byChannel;
    for (const auto& n : notes)
    {
        if (n.muted)
            continue;
        auto& seq = byChannel[n.channel];
        const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));
        const auto midiChannel = juce::jlimit (1, 16, n.colour + 1);
        seq.addEvent (juce::MidiMessage::noteOn (midiChannel, n.key, velocity), (double) n.start);
        seq.addEvent (juce::MidiMessage::noteOff (midiChannel, n.key), (double) (n.start + std::max<std::int64_t> (1, n.length)));
    }
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60'000'000.0 / juce::jmax (1.0, song.bpm))), 0.0);
    meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::jmax (1, song.beatsPerBar), 4), 0.0);
    if (const auto key = exportbundle::keySignature (song))
        meta.addEvent (juce::MidiMessage::keySignatureMetaEvent (key->sharps, key->minor), 0.0);
    midi.addTrack (meta);
    for (auto& [channel, seq] : byChannel)
    {
        const auto name = channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].name : std::string ("Channel");
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, fromUtf8 (name)), 0.0);
        seq.sort();
        seq.updateMatchedPairs();
        midi.addTrack (seq);
    }
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (out.failedToOpen() || ! midi.writeTo (out))
    {
        error = tr ("Could not write {0}", { file.getFullPathName() });
        return false;
    }
    return true;
}

bool padWav (const juce::File& file, juce::int64 frames, juce::String& error)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr)
    {
        error = tr ("Could not read {0}", { file.getFullPathName() });
        return false;
    }
    if (reader->lengthInSamples >= frames)
        return true;
    const auto channels = (int) reader->numChannels;
    juce::AudioBuffer<float> buffer (channels, (int) frames);
    buffer.clear();
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, channels > 1);
    const auto rate = reader->sampleRate;
    const auto bits = (int) reader->bitsPerSample;
    reader.reset();
    const auto temp = file.getSiblingFile (file.getFileName() + ".part");
    {
        auto stream = std::make_unique<juce::FileOutputStream> (temp);
        if (stream->failedToOpen())
        {
            error = tr ("Could not write {0}", { temp.getFullPathName() });
            return false;
        }
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, (unsigned int) channels, bits, {}, 0));
        if (writer == nullptr)
        {
            error = tr ("Could not write {0}", { temp.getFullPathName() });
            return false;
        }
        stream.release(); // (the writer owns it)
        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            error = tr ("Could not write {0}", { temp.getFullPathName() });
            return false;
        }
    }
    if (! temp.moveFileTo (file))
    {
        temp.deleteFile();
        error = tr ("Could not write {0}", { file.getFullPathName() });
        return false;
    }
    return true;
}

namespace
{
juce::int64 wavFrames (const juce::File& file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    return reader != nullptr ? reader->lengthInSamples : 0;
}

void addToZip (juce::ZipFile::Builder& zip, const juce::File& folder, const juce::File& root)
{
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, true))
        zip.addFile (f, 6, f.getRelativePathFrom (root).replaceCharacter ('\\', '/'));
}
} // namespace

bool write (const Song& original, const juce::String& projectName, const exportbundle::Options& options, const juce::File& parent,
            const Context& context, Result& result)
{
    result = {};
    const auto song = exportbundle::prepared (original);
    const auto plan = exportbundle::plan (song, projectName.toStdString(), options);
    parent.createDirectory();
    auto folder = parent.getChildFile (fromUtf8 (plan.folder));
    if (folder.exists())
        folder = parent.getNonexistentChildFile (fromUtf8 (plan.folder), {}, true);
    if (! folder.createDirectory())
    {
        result.failed.add (tr ("Could not write {0}", { folder.getFullPathName() }));
        return false;
    }
    result.folder = folder;
    const auto fileOf = [&folder] (const std::string& relative) { return folder.getChildFile (fromUtf8 (relative)); };

    // the steps, for the progress bar: each stem, the mixdown, the MIDI files, the Live set, the FL project (counts 2)
    const auto steps = (double) plan.stems.size() + (plan.mixdown.empty() ? 0.0 : 1.0) + 1.0 + 1.0 + 2.0;
    double done = 0.0;
    const auto keepGoing = [&] (double p, const juce::String& what) {
        if (result.cancelled)
            return false;
        if (context.progress && ! context.progress (juce::jlimit (0.0, 1.0, p), what))
            result.cancelled = true;
        return ! result.cancelled;
    };
    const auto stepProgress = [&] (double weight, const juce::String& what) {
        return [&keepGoing, base = done / steps, weight = weight / steps, what] (double p) { return keepGoing (base + p * weight, what); };
    };

    // stems and mixdown: the same range, the song's start to its end (the longest tail added to all of them below)
    exportopts::Options render;
    render.format = exportopts::Format::wav;
    render.wavBits = 24;
    render.sampleRate = context.sampleRate;
    render.range = exportopts::Range::selection;
    render.tail = exportopts::TailMode::leave;
    render.masterEffects = false; // stems: the mix before the Master
    std::vector<juce::File> audio;
    for (const auto& stem : plan.stems)
    {
        const auto what = tr ("Stem: {0}", { fromUtf8 (stem.name) });
        if (! keepGoing (done / steps, what))
            break;
        quickrender::Plan range;
        range.clips = stem.clips;
        range.start = 0;
        range.end = std::max<std::int64_t> (1, plan.endTick);
        const auto file = fileOf (stem.path);
        file.getParentDirectory().createDirectory();
        juce::String error;
        if (context.render (quickrender::makePlanRenderSong (song, range), render, file, stepProgress (1.0, what), error))
        {
            result.written.add (fromUtf8 (stem.path));
            audio.push_back (file);
        }
        else if (! result.cancelled)
            result.failed.add (fromUtf8 (stem.path) + ": " + error);
        done += 1.0;
    }
    if (! plan.mixdown.empty() && ! result.cancelled)
    {
        const auto what = tr ("Mixdown");
        auto whole = song;
        whole.loopStart = 0;
        whole.loopEnd = std::max<std::int64_t> (1, plan.endTick);
        auto o = render;
        o.masterEffects = true;
        const auto file = fileOf (plan.mixdown);
        juce::String error;
        if (context.render (whole, o, file, stepProgress (1.0, what), error))
        {
            result.written.add (fromUtf8 (plan.mixdown));
            audio.push_back (file);
        }
        else if (! result.cancelled)
            result.failed.add (fromUtf8 (plan.mixdown) + ": " + error);
        done += 1.0;
    }
    // one length for all: the longest (the music plus the longest tail)
    if (! result.cancelled)
    {
        juce::int64 longest = 0;
        for (const auto& f : audio)
            longest = std::max (longest, wavFrames (f));
        for (const auto& f : audio)
        {
            juce::String error;
            if (! padWav (f, longest, error))
                result.failed.add (f.getRelativePathFrom (folder) + ": " + error);
        }
    }

    // MIDI
    if (! result.cancelled && keepGoing (done / steps, tr ("MIDI files")))
    {
        const auto midi = [&] (const std::string& path, const std::vector<Note>& notes) {
            juce::String error;
            if (writeMidi (song, notes, fileOf (path), error))
                result.written.add (fromUtf8 (path));
            else
                result.failed.add (fromUtf8 (path) + ": " + error);
        };
        for (const auto& m : plan.midi)
            midi (m.path, exportbundle::trackNotes (song, m));
        if (! plan.allMidi.empty())
            midi (plan.allMidi, exportbundle::songNotes (song));
        done += 1.0;
    }

    // the Live set (its writer looks the plugins up on the message thread)
    if (! result.cancelled && keepGoing (done / steps, tr ("Ableton Live set")))
    {
        juce::File als;
        juce::StringArray notes;
        juce::String error;
        const auto name = fromUtf8 (plan.name);
        context.onMessageThread ([&] { als = writeAlsProject (original, context.host, AlsVersion::live12, true, folder, name, notes, error); });
        if (als != juce::File())
            result.written.add (als.getRelativePathFrom (folder).replaceCharacter ('\\', '/'));
        else if (! result.cancelled)
            result.failed.add (fromUtf8 (plan.als) + ": " + (error.isNotEmpty() ? error : tr ("Cancelled")));
        done += 1.0;
    }

    // the FL Studio project (what only LYNX has is rendered to audio next to it, as File › Export › FL Studio project's
    // default option)
    if (! result.cancelled && keepGoing (done / steps, tr ("FL Studio project")))
    {
        const auto target = fileOf (plan.flp);
        const auto renders = folder.getChildFile (fromUtf8 (plan.name) + " audio");
        flstudio::Written written;
        juce::String error;
        const auto base = done / steps;
        const auto ok = flstudio::exportProject (
            original, context.flOptions, target, false, renders, context.sampleRate,
            [&context] (const Song& s, const ExportSettings& settings, const juce::File& wav, const std::function<bool (double)>& progress, juce::String& err) {
                return context.render (s, settings.toOptions(), wav, progress, err);
            },
            [&keepGoing, base, weight = 2.0 / steps] (double p, const juce::String& what) { return keepGoing (base + p * weight, what); },
            written, error);
        if (ok)
        {
            result.written.add (fromUtf8 (plan.flp));
            if (renders.isDirectory())
                result.written.add (renders.getFileName() + "/");
        }
        else if (! result.cancelled)
            result.failed.add (fromUtf8 (plan.flp) + ": " + error);
        done += 2.0;
    }

    if (result.cancelled)
    {
        folder.deleteRecursively();
        result.folder = juce::File();
        return false;
    }

    // README
    {
        auto timeline = song;
        timeline.loopStart = 0;
        timeline.loopEnd = std::max<std::int64_t> (1, plan.endTick);
        const auto seconds = exportMusicSeconds (timeline, render);
        std::vector<std::string> written, failed;
        for (const auto& w : result.written)
            written.push_back (w.toStdString());
        for (const auto& f : result.failed)
            failed.push_back (f.toStdString());
        if (fileOf (plan.readme).replaceWithText (fromUtf8 (exportbundle::readme (song, plan, seconds, written, failed))))
            result.written.add (fromUtf8 (plan.readme));
        else
            result.failed.add (fromUtf8 (plan.readme));
    }

    if (plan.zip)
    {
        keepGoing (1.0, tr ("Zipping the folder"));
        juce::ZipFile::Builder zip;
        addToZip (zip, folder, folder.getParentDirectory());
        const auto target = parent.getNonexistentChildFile (folder.getFileName(), ".zip", true);
        bool ok = false;
        {
            juce::FileOutputStream out (target);
            ok = ! out.failedToOpen() && zip.writeToStream (out, nullptr);
        }
        if (ok)
        {
            folder.deleteRecursively();
            result.folder = target;
        }
        else
        {
            target.deleteFile();
            result.failed.add (target.getFileName() + ": " + tr ("Could not write {0}", { target.getFullPathName() }));
        }
    }
    return true;
}
} // namespace otherapps

namespace
{
// a render's job: made and deleted on the message thread (plugins), a Cancel never waits for it (ExportDialog.cpp)
struct RenderJob : MessageThreadOwned<ExportJob>
{
    explicit RenderJob (juce::Thread& t)
        : MessageThreadOwned<ExportJob> ([] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); },
                                         [&t] { return t.threadShouldExit(); })
    {
    }
};

class BundleReportPanel final : public juce::Component
{
public:
    BundleReportPanel (const juce::File& target, const juce::StringArray& written, const juce::StringArray& failed)
        : file (target)
    {
        title.setText (tr ("{0} saved.", { target.getFileName() }), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::body, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (title);
        juce::String text;
        if (! failed.isEmpty())
        {
            text << tr ("Not written:") << "\n";
            for (const auto& f : failed)
                text << "  " << f << "\n";
            text << "\n";
        }
        text << tr ("Written:") << "\n";
        for (const auto& w : written)
            text << "  " << w << "\n";
        body.setMultiLine (true);
        body.setReadOnly (true);
        body.setScrollbarsShown (true);
        body.setCaretVisible (false);
        body.setFont (theme::font (theme::type::small));
        body.setText (text, false);
        addAndMakeVisible (body);
        show.onClick = [this] { file.revealToUser(); };
        close.onClick = [this] {
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                w->exitModalState (0);
        };
        addAndMakeVisible (show);
        addAndMakeVisible (close);
        setSize (620, 440);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (24));
        auto buttons = r.removeFromBottom (28);
        close.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        show.setBounds (buttons.removeFromRight (140));
        r.removeFromBottom (8);
        r.removeFromTop (4);
        body.setBounds (r);
    }

private:
    juce::File file;
    juce::Label title;
    juce::TextEditor body;
    juce::TextButton show { tr ("Show in folder") }, close { tr ("Close") };
};

void showReport (const otherapps::Result& result)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr ("Export for other apps");
    options.content.setOwned (fitContentToScreen (std::make_unique<BundleReportPanel> (result.folder, result.written, result.failed)).release());
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

class BundleThread final : public juce::ThreadWithProgressWindow
{
public:
    BundleThread (AudioEngine& engine, juce::String name, exportbundle::Options o, juce::File parentFolder)
        : juce::ThreadWithProgressWindow (tr ("Exporting for other apps..."), true, true),
          song (engine.getSong()),
          projectName (std::move (name)),
          options (o),
          parent (std::move (parentFolder))
    {
        context.host = &engine.getPluginHost();
        context.flOptions = flstudio::optionsFor (&engine.getPluginHost(), flpexport::LynxOnly::renderAudio, song);
        const auto rate = engine.getTransport().sampleRate.load();
        context.sampleRate = rate > 0.0 ? rate : 44100.0;
    }

    void run() override
    {
        context.render = [this] (const Song& s, const exportopts::Options& o, const juce::File& file, const std::function<bool (double)>& progress,
                                 juce::String& error) {
            RenderJob slot (*this);
            auto* job = slot.create ([&] { return std::make_unique<ExportJob> (s, o, file); });
            if (job == nullptr)
                return false;
            const auto ok = job->run (progress, error);
            slot.release();
            return ok;
        };
        // a call on the message thread that a Cancel does not wait for once it has not started (see MessageThreadOwned.h)
        context.onMessageThread = [this] (std::function<void()> f) {
            struct State
            {
                std::atomic<bool> taken { false };
                juce::WaitableEvent done;
            };
            auto state = std::make_shared<State>();
            auto call = std::make_shared<std::function<void()>> (std::move (f));
            juce::MessageManager::callAsync ([state, call] {
                if (state->taken.exchange (true))
                    return; // (abandoned)
                (*call)();
                state->done.signal();
            });
            while (! state->done.wait (50))
                if (threadShouldExit() && ! state->taken.exchange (true))
                    return; // never started: abandoned
        };
        context.progress = [this] (double p, const juce::String& what) {
            setProgress (p);
            setStatusMessage (what);
            return ! threadShouldExit();
        };
        ok = otherapps::write (song, projectName, options, parent, context, result);
    }

    void threadComplete (bool cancelled) override
    {
        if (result.cancelled || cancelled)
        {
            if (result.folder.isDirectory())
                result.folder.deleteRecursively();
        }
        else if (ok)
            showReport (result);
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"), result.failed.joinIntoString ("\n"));
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    Song song;
    juce::String projectName;
    exportbundle::Options options;
    juce::File parent;
    otherapps::Context context;
    otherapps::Result result;
    bool ok = false;
};

// Round 190fo: one track as a file (the Simple workflow's track menu, as the original's "export track" does): its stem
// as a WAV (the same render as the bundle's stems: the track's clips from the song's start, 24-bit, no Master effects)
class TrackStemThread final : public juce::ThreadWithProgressWindow
{
public:
    TrackStemThread (AudioEngine& engine, int trackIndex, juce::File target)
        : juce::ThreadWithProgressWindow (tr ("Exporting the track..."), true, true), song (exportbundle::prepared (engine.getSong())),
          track (trackIndex), file (std::move (target))
    {
        const auto rate = engine.getTransport().sampleRate.load();
        sampleRate = rate > 0.0 ? rate : 44100.0;
    }

    void run() override
    {
        const auto plan = exportbundle::plan (song, "track");
        const auto stem = std::find_if (plan.stems.begin(), plan.stems.end(), [this] (const exportbundle::TrackFile& t) { return t.track == track; });
        if (stem == plan.stems.end())
        {
            error = tr ("The track has nothing to play.");
            return;
        }
        exportopts::Options render;
        render.format = exportopts::Format::wav;
        render.wavBits = 24;
        render.sampleRate = sampleRate;
        render.range = exportopts::Range::selection;
        render.tail = exportopts::TailMode::leave;
        render.masterEffects = false;
        quickrender::Plan range;
        range.clips = stem->clips;
        range.start = 0;
        range.end = std::max<std::int64_t> (1, plan.endTick);
        RenderJob slot (*this);
        auto* job = slot.create ([&] { return std::make_unique<ExportJob> (quickrender::makePlanRenderSong (song, range), render, file); });
        if (job == nullptr)
            return;
        ok = job->run ([this] (double p) {
            setProgress (p);
            return ! threadShouldExit();
        }, error);
        slot.release();
    }

    void threadComplete (bool cancelled) override
    {
        if (! ok && ! cancelled)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"), error);
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    Song song;
    int track;
    juce::File file;
    double sampleRate = 44100.0;
    juce::String error;
    bool ok = false;
};

class OtherAppsPanel final : public juce::Component
{
public:
    OtherAppsPanel (AudioEngine& e, juce::File folder, juce::String name, bool live)
        : engine (e), suggestedFolder (std::move (folder)), projectName (std::move (name)), canExport (live)
    {
        const auto& song = engine.getSong();
        plan = exportbundle::plan (exportbundle::prepared (song), projectName.toStdString());
        title.setText (tr ("Export for other apps"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::body, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (title);
        intro.setText (tr ("One folder that Ableton Live, FL Studio and other music apps can open."), juce::dontSendNotification);
        facts.setText (fromUtf8 (exportbundle::tempoText (song)) + "  ·  " + juce::String (juce::jmax (1, song.beatsPerBar)) + "/4  ·  "
                           + tr ("Key: {0}", { projectkey::get (song) ? fromUtf8 (exportbundle::keyText (song)) : tr ("none") }),
                       juce::dontSendNotification);
        for (auto* l : { &intro, &facts, &contents })
        {
            l->setFont (theme::font (theme::type::small));
            l->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            l->setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (l);
        }
        heading.setText (tr ("In the folder"), juce::dontSendNotification);
        heading.setFont (theme::font (theme::type::body, true));
        heading.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (heading);
        mixdownButton.setToggleState (lastMixdown, juce::dontSendNotification);
        zipButton.setToggleState (lastZip, juce::dontSendNotification);
        for (auto* b : { &mixdownButton, &zipButton })
        {
            b->onClick = [this] { update(); };
            addAndMakeVisible (b);
        }
        exportButton.onClick = [this] { chooseFolder(); };
        cancelButton.onClick = [this] { closeWindow(); };
        exportButton.setEnabled (canExport && ! plan.empty());
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);
        update();
        setSize (540, 352);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        // the list of files on a slightly darker card
        g.setColour (theme::colour::bg1);
        g.fillRoundedRectangle (card.toFloat(), 6.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        title.setBounds (r.removeFromTop (22));
        intro.setBounds (r.removeFromTop (20));
        facts.setBounds (r.removeFromTop (20));
        r.removeFromTop (8);
        heading.setBounds (r.removeFromTop (22));
        // the card fits its lines (6 or 7, the "zipped" line included)
        card = r.removeFromTop (14 + 15 * juce::jmax (1, juce::StringArray::fromLines (contents.getText().trim()).size()));
        contents.setBounds (card.reduced (10, 6));
        r.removeFromTop (8);
        mixdownButton.setBounds (r.removeFromTop (24));
        zipButton.setBounds (r.removeFromTop (24));
        auto buttons = r.removeFromBottom (28);
        cancelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (120));
    }

private:
    void update()
    {
        lastMixdown = mixdownButton.getToggleState();
        lastZip = zipButton.getToggleState();
        juce::String text;
        const auto dot = juce::String::fromUTF8 ("\xe2\x80\xa2 "); // •
        text << dot << tr ("Stems: {0} audio file(s), one per track (WAV, 24-bit, all the same length)", { juce::String ((int) plan.stems.size()) }) << "\n";
        text << dot << (plan.midi.empty() && plan.allMidi.empty() ? tr ("MIDI: none (no notes in the song)")
                                                                  : tr ("MIDI: {0} file(s) with the tempo and the key", { juce::String ((int) plan.midi.size() + (plan.allMidi.empty() ? 0 : 1)) }))
             << "\n";
        text << dot << tr ("Ableton Live set (.als), with its samples") << "\n";
        text << dot << tr ("FL Studio project (.flp)") << "\n";
        if (lastMixdown)
            text << dot << tr ("Mixdown (WAV)") << "\n";
        text << dot << tr ("README.txt: tempo, key and how to bring the files in") << "\n";
        if (lastZip)
            text << tr ("Everything zipped into one .zip file.");
        contents.setText (text, juce::dontSendNotification);
        resized();
        repaint();
    }

    void chooseFolder()
    {
        const auto start = lastFolder.isDirectory() ? lastFolder : suggestedFolder;
        chooser = lynx::ui::makeFileChooser (tr ("Choose where to put the folder"), start, {});
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [safe = juce::Component::SafePointer<OtherAppsPanel> (this)] (const juce::FileChooser& c) {
                                     const auto folder = c.getResult();
                                     if (safe == nullptr || ! folder.isDirectory())
                                         return;
                                     lastFolder = folder;
                                     auto& engine = safe->engine;
                                     const auto name = safe->projectName;
                                     exportbundle::Options o;
                                     o.mixdown = lastMixdown;
                                     o.zip = lastZip;
                                     safe->closeWindow();
                                     engine.capturePluginStates();
                                     (new BundleThread (engine, name, o, folder))->launchThread();
                                 });
    }

    void closeWindow()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    AudioEngine& engine;
    juce::File suggestedFolder;
    juce::String projectName;
    bool canExport;
    exportbundle::Plan plan;
    juce::Rectangle<int> card;
    juce::Label title, intro, facts, heading, contents;
    juce::ToggleButton mixdownButton { tr ("Include the mixdown") }, zipButton { tr ("Zip the folder") };
    juce::TextButton exportButton { tr ("Export...") }, cancelButton { tr ("Cancel") };
    std::unique_ptr<juce::FileChooser> chooser;

    static inline juce::File lastFolder;
    static inline bool lastMixdown = true, lastZip = false;
};
} // namespace

void exportOneTrack (AudioEngine& engine, int track, bool asMidi, const juce::File& suggestedFolder, const juce::String& projectName)
{
    const auto& song = engine.getSong();
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return;
    const auto trackName = song.playlistTracks[(std::size_t) track].name.empty() ? tr ("Track {0}", { juce::String (track + 1) })
                                                                                  : fromUtf8 (song.playlistTracks[(std::size_t) track].name);
    // the original's names: "<song> - <track>.wav", "<track>.mid"
    const auto name = juce::File::createLegalFileName (asMidi ? trackName + ".mid" : (projectName.isNotEmpty() ? projectName : juce::String ("LYNX SHARE")) + " - " + trackName + ".wav");
    static std::unique_ptr<juce::FileChooser> chooser;
    chooser = lynx::ui::makeFileChooser (asMidi ? tr ("Export the track as MIDI") : tr ("Export the track as audio"), suggestedFolder.getChildFile (name),
                                         asMidi ? "*.mid" : "*.wav");
    lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                             [&engine, track, asMidi] (const juce::FileChooser& fc) {
                                 auto file = fc.getResult();
                                 if (file == juce::File())
                                     return;
                                 file = file.withFileExtension (asMidi ? "mid" : "wav");
                                 if (asMidi)
                                 {
                                     juce::String error;
                                     const auto song = exportbundle::prepared (engine.getSong());
                                     exportbundle::TrackFile t;
                                     t.track = track;
                                     for (int i = 0; i < (int) song.playlist.size(); ++i)
                                         if (const auto& c = song.playlist[(std::size_t) i]; c.track == track && ! c.muted)
                                             t.clips.push_back (i);
                                     const auto notes = exportbundle::trackNotes (song, t);
                                     if (notes.empty() || ! otherapps::writeMidi (song, notes, file, error))
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"),
                                                                                 notes.empty() ? tr ("The track has no notes.") : error);
                                     return;
                                 }
                                 engine.capturePluginStates();
                                 (new TrackStemThread (engine, track, file))->launchThread();
                             });
}

void showOtherAppsExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr ("Export for other apps");
    options.content.setOwned (fitContentToScreen (std::make_unique<OtherAppsPanel> (engine, suggestedFolder, projectName.isNotEmpty() ? projectName : "LYNX SHARE", true)).release());
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

std::unique_ptr<juce::Component> makeOtherAppsExportPanel (AudioEngine& engine, const juce::String& projectName)
{
    return std::make_unique<OtherAppsPanel> (engine, juce::File(), projectName.isNotEmpty() ? projectName : "LYNX SHARE", false);
}

std::unique_ptr<juce::Component> makeOtherAppsReportPanel (const juce::File& folder, const juce::StringArray& written, const juce::StringArray& failed)
{
    return std::make_unique<BundleReportPanel> (folder, written, failed);
}
} // namespace lynx
