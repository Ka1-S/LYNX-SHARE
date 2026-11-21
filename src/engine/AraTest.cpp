// LYNX --aratest <plugin.vst3> <audio.wav> <out folder> [project.lynx] (round 190kx): ARA 2 hosting end to end, no window.
//  1. the plugin file is scanned (in a child process, as every scan) and must list an ARA main factory;
//  2. a song with one audio clip channel playing the whole file is exported twice: as a plain audio clip ("plain.wav") and
//     edited in the ARA plugin without any edit ("ara.wav"): the plugin must play the clip where it is (an untouched
//     Melodyne region plays the audio back as it was: level and timing match, the difference is reported);
//  3. the ARA document is archived as a save does, given to a new engine as an open does, and exported again
//     ("restored.wav"): the archive must be read back and play the same;
//  4. with a project (a .lynx whose audio clips were edited in the plugin's own window), that project is exported
//     ("project.wav"): what the edits made, for the round's comparisons.
//  5. (round 190oq) the plugin as an ARA insert on the channel's mixer track: "insert.wav" = "ara.wav"; bypassed =
//     "plain.wav"; the insert's ARA mode saved and read back;
//  6. (round 190oq) under tempo automation (90 → 150 BPM): "tempo-ara.wav" lines up with "tempo-plain.wav" (the regions,
//     the tempo entries and the play head follow the tempo map).
// Writes <out>/aratest.txt; result=OK when 1-3, 5 and 6 hold.

#include "engine/AraHost.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "engine/ProjectFile.h"
#include "core/AraRegions.h"

#if JUCE_WINDOWS || JUCE_LINUX
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); } // (juce_Messaging_*.cpp)
#endif

namespace lynx
{
namespace
{
struct Wave
{
    std::vector<float> l, r;
    double rate = 0.0;
};

Wave readWave (const juce::File& file)
{
    Wave w;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return w;
    juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
    w.rate = reader->sampleRate;
    w.l.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
    const auto* r = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
    w.r.assign (r, r + b.getNumSamples());
    return w;
}

double rms (const std::vector<float>& x, std::size_t from = 0, std::size_t n = ~std::size_t { 0 })
{
    double s = 0.0;
    std::size_t c = 0;
    for (auto i = from; i < x.size() && c < n; ++i, ++c)
        s += (double) x[i] * x[i];
    return c > 0 ? std::sqrt (s / (double) c) : 0.0;
}

// the lag (samples, −maxLag…maxLag) at which `b` best matches `a` (b[i + lag] ~ a[i]) over their first 8 seconds at
// 48 kHz, and the normalised correlation there: a coarse search every 8 samples, then every sample around the best
double correlationAt (const std::vector<float>& a, const std::vector<float>& b, int lag, int step)
{
    const auto n = (long) std::min<std::size_t> (std::min (a.size(), b.size()), 384000);
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (long i = std::max (0L, (long) -lag); i < n && i + lag < (long) b.size(); i += step)
    {
        const double x = a[(std::size_t) i], y = b[(std::size_t) (i + lag)];
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
}

std::pair<int, double> bestLag (const std::vector<float>& a, const std::vector<float>& b, int maxLag)
{
    auto coarse = std::pair<int, double> { 0, -2.0 };
    for (int lag = -maxLag; lag <= maxLag; lag += 8)
        if (const auto c = correlationAt (a, b, lag, 5); c > coarse.second)
            coarse = { lag, c };
    auto best = std::pair<int, double> { coarse.first, correlationAt (a, b, coarse.first, 1) };
    for (int lag = coarse.first - 8; lag <= coarse.first + 8; ++lag)
        if (const auto c = correlationAt (a, b, lag, 1); c > best.second)
            best = { lag, c };
    return best;
}

juce::String db (double v) { return juce::String (juce::Decibels::gainToDecibels ((float) v, -200.0f), 2) + " dB"; }
} // namespace

juce::String araSelfTest (const juce::File& pluginFile, const juce::File& audio, const juce::File& out, const juce::File& project,
                          const std::function<void (const juce::String&)>& log)
{
    auto say = [&log] (const juce::String& line) {
        if (log)
            log (line);
    };
    if (! AraHost::available())
        return "ERROR this build has no ARA hosting";
    out.createDirectory();
    juce::StringArray errors;

    // 1. the plugin
    juce::PluginDescription plugin;
    {
        AudioEngine scanner (false);
        scanner.getPluginHost().addPluginFile (pluginFile);
        for (const auto& t : scanner.getPluginHost().getKnownPlugins().getTypes())
            if (juce::File (t.fileOrIdentifier).getFileName() == pluginFile.getFileName())
            {
                say ("scanned: " + t.name + " (" + t.pluginFormatName + ", " + t.manufacturerName + " " + t.version + ") ara=" + juce::String ((int) AraHost::isAra (t)));
                if (AraHost::isAra (t))
                    plugin = t;
            }
        // (the scanned list, for a test profile's plugins.xml: the app then finds the plugin by its id)
        if (auto xml = scanner.getPluginHost().getKnownPlugins().createXml())
            xml->writeTo (out.getChildFile ("plugins.xml"));
    }
    if (plugin.name.isEmpty())
        return "ERROR no ARA plugin in " + pluginFile.getFileName();

    // 2. the song: the whole file as one clip at bar 2 (so a plugin's lead-in is inside the render)
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    double seconds = 0.0;
    if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (audio)); r != nullptr)
        seconds = (double) r->lengthInSamples / r->sampleRate;
    if (seconds <= 0.0)
        return "ERROR can't read " + audio.getFullPathName();
    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer();
    ChannelDef ch;
    ch.name = "Vocal";
    ch.instrument = InstrumentKind::audioClip;
    ch.samplePath = audio.getFullPathName().toStdString();
    ch.volume = audioClipUnityVolume;
    ch.uid = 4242;
    song.channels.push_back (ch);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.start = 4 * ppq; // bar 2 (2 s at 120 BPM)
    clip.length = (std::int64_t) std::ceil (seconds / ara::secondsPerTick (song));
    clip.uid = 99;
    song.playlist.push_back (clip);
    song.mode = PlayMode::song;

    exportopts::Options o;
    o.format = exportopts::Format::wav;
    o.wavBits = 32;
    o.sampleRate = 48000;
    o.tail = exportopts::TailMode::leave;
    o.pluginWarmUp = true;
    auto render = [&] (const Song& s, const juce::String& name) {
        const auto file = out.getChildFile (name + ".wav");
        const auto start = juce::Time::getMillisecondCounterHiRes();
        ExportJob job (s, o, file);
        juce::String error;
        if (! job.run ({}, error))
            errors.add (name + ": " + error);
        say (name + ".wav rendered in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, 2) + " s");
        return readWave (file);
    };

    const auto plain = render (song, "plain");
    auto araSong = song;
    araSong.channels[0].ara = { plugin.createIdentifierString().toStdString(), plugin.name.toStdString() };
    const auto araOut = render (araSong, "ara");

    const auto compare = [&] (const Wave& a, const Wave& b, const juce::String& what, double minCorr) {
        if (a.l.empty() || b.l.empty())
        {
            errors.add (what + ": empty render");
            return;
        }
        const auto [lag, corr] = bestLag (a.l, b.l, 4800);
        const auto ra = rms (a.l), rb = rms (b.l);
        say (what + ": correlation " + juce::String (corr, 4) + " at lag " + juce::String (lag) + " samples, level " + db (ra) + " vs " + db (rb)
             + " (" + juce::String (juce::Decibels::gainToDecibels ((float) (rb / juce::jmax (1.0e-9, ra))), 2) + " dB)");
        if (corr < minCorr)
            errors.add (what + ": correlation " + juce::String (corr, 3) + " < " + juce::String (minCorr, 2));
        if (rb < 0.25 * ra)
            errors.add (what + ": much quieter (" + db (rb) + " vs " + db (ra) + ")");
    };
    compare (plain, araOut, "plain vs ARA (no edit)", 0.9);

    // 3. archive and restore (a save and an open)
    {
        AudioEngine live (false);
        live.getTransport().sampleRate.store (48000.0);
        live.setSong (araSong);
        if (auto* host = live.getAraHost(); host == nullptr || host->instanceCount() != 1)
            errors.add ("live engine: no ARA instance (" + (host != nullptr ? host->lastError() : juce::String ("no host")) + ")");
        // the plugin analyses first (as a user waits for Melodyne's blobs)
        const auto start = juce::Time::getMillisecondCounterHiRes();
        while (live.getAraHost() != nullptr && live.getAraHost()->analysing() && juce::Time::getMillisecondCounterHiRes() - start < 120000.0)
            juce::Thread::sleep (50);
        say ("analysis took " + juce::String ((juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, 2) + " s");
        live.capturePluginStates();
        auto saved = live.getSong();
        say ("archive: " + juce::String ((int) saved.araDocuments.size()) + " document(s), "
             + juce::String (saved.araDocuments.empty() ? 0 : (int) saved.araDocuments[0].archive.size()) + " base64 bytes");
        if (saved.araDocuments.empty())
            errors.add ("no ARA document archived");
        // the session as a project (the round opens it in the app to edit the clip in the plugin's own window)
        juce::String saveError;
        if (! ProjectFile::save (saved, out.getChildFile ("ara-session.lynx"), saveError))
            errors.add ("can't save ara-session.lynx: " + saveError);
        else if (auto back = ProjectFile::load (out.getChildFile ("ara-session.lynx"), saveError);
                 ! back || back->channels.empty() || back->channels[0].ara != saved.channels[0].ara || back->araDocuments != saved.araDocuments)
            errors.add ("ara-session.lynx did not read back the ARA link and document");
        else
            say ("ara-session.lynx: the ARA link and the document read back");
        const auto restored = render (saved, "restored");
        compare (araOut, restored, "ARA vs restored", 0.98);
    }

    // 5. round 190oq (issue #243): the plugin on the channel's mixer insert in ARA mode plays the clip as the channel's own
    // link does; bypassed, the clip plays unedited; the slot's mode reads back from a saved project
    {
        auto ins = song;
        ins.channels[0].mixerTrack = 1;
        EffectSlot fx;
        fx.kind = EffectKind::plugin;
        fx.plugin.id = plugin.createIdentifierString().toStdString();
        fx.plugin.name = plugin.name.toStdString();
        fx.ara = true;
        ins.mixer[1].effects.push_back (fx);
        {
            AudioEngine live (false);
            live.getTransport().sampleRate.store (48000.0);
            live.setSong (ins);
            const auto n = live.getAraHost() != nullptr ? live.getAraHost()->instanceCount() : 0;
            say ("ARA insert: " + juce::String (n) + " instance(s) in the live engine");
            if (n != 1)
                errors.add ("ARA insert: " + juce::String (n) + " instances (1 expected)");
            juce::String saveError;
            if (! ProjectFile::save (ins, out.getChildFile ("ara-insert.lynx"), saveError))
                errors.add ("can't save ara-insert.lynx: " + saveError);
            else if (auto back = ProjectFile::load (out.getChildFile ("ara-insert.lynx"), saveError);
                     ! back || back->mixer.size() < 2 || back->mixer[1].effects.empty() || ! back->mixer[1].effects[0].ara)
                errors.add ("ara-insert.lynx did not read back the ARA insert");
            else
                say ("ara-insert.lynx: the ARA insert reads back");
        }
        const auto insOut = render (ins, "insert");
        compare (araOut, insOut, "ARA on the channel vs ARA insert", 0.98);
        auto bypassed = ins;
        bypassed.mixer[1].effects[0].enabled = false;
        const auto bypassedOut = render (bypassed, "insert-bypassed");
        compare (plain, bypassedOut, "plain vs bypassed ARA insert", 0.98);
    }

    // 6. round 190oq (issue #243): under tempo automation the regions, the musical context and the play head follow the
    // tempo map: the ARA render lines up with the plain one (before, the region sat at the project tempo's seconds)
    {
        auto tempoSong = song;
        Automation tempo;
        tempo.name = "Tempo";
        tempo.target = { AutomationTargetKind::tempo, 0, 0, 0 };
        tempo.points = { { 0, valueFromTempo (90.0), 0.0f }, { ppq * 4, valueFromTempo (150.0), 0.0f }, { ppq * 64, valueFromTempo (150.0), 0.0f } };
        tempoSong.automations.push_back (tempo);
        Clip at;
        at.kind = ClipKind::automation;
        at.automation = 0;
        at.track = 2;
        at.start = 0;
        at.length = ppq * 64;
        tempoSong.playlist.push_back (at);
        const auto map = ara::timeMap (tempoSong);
        say ("tempo map: " + juce::String ((int) map.ticks.size()) + " points; bar 2 at " + juce::String (map.secondsAt (4.0 * ppq), 3) + " s");
        const auto tPlain = render (tempoSong, "tempo-plain");
        auto tAra = tempoSong;
        tAra.channels[0].ara = { plugin.createIdentifierString().toStdString(), plugin.name.toStdString() };
        const auto tAraOut = render (tAra, "tempo-ara");
        compare (tPlain, tAraOut, "plain vs ARA under tempo automation", 0.9);
        if (! tPlain.l.empty() && ! tAraOut.l.empty())
        {
            const auto [lag, corr] = bestLag (tPlain.l, tAraOut.l, 4800);
            if (std::abs (lag) > 480)
                errors.add ("tempo map: the ARA render is " + juce::String (lag) + " samples off");
        }

        // (round 190oq) LYNX_ARATEST_SHOW=<seconds>: the plugin's editor of an ARA insert in this tempo-map song (and a 3/4
        // marker at bar 5) on screen for that long, for a screenshot of its bar ruler; no input is needed
        const auto show = juce::SystemStats::getEnvironmentVariable ("LYNX_ARATEST_SHOW", {}).getIntValue();
        if (show > 0)
        {
            auto shown = tempoSong;
            shown.channels[0].mixerTrack = 1;
            EffectSlot fx;
            fx.kind = EffectKind::plugin;
            fx.plugin.id = plugin.createIdentifierString().toStdString();
            fx.plugin.name = plugin.name.toStdString();
            fx.ara = true;
            shown.mixer[1].effects.push_back (fx);
            Marker three;
            three.tick = 16 * ppq;
            three.numerator = 3;
            three.denominator = 4;
            three.action = Marker::timeSignature;
            three.name = "3/4";
            shown.markers().push_back (three);
            juce::String saveError;
            ProjectFile::save (shown, out.getChildFile ("ara-insert-tempo.lynx"), saveError);
            AudioEngine live (false);
            live.getTransport().sampleRate.store (48000.0);
            live.setSong (shown);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            while (live.getAraHost() != nullptr && live.getAraHost()->analysing() && juce::Time::getMillisecondCounterHiRes() - start < 120000.0)
                juce::Thread::sleep (50);
            auto* host = live.getAraHost();
            auto* instance = host != nullptr ? host->pluginOf (shown.channels[0].uid) : nullptr;
            if (instance == nullptr)
                errors.add ("show: no ARA instance");
            else if (auto* editor = instance->createEditorIfNeeded())
            {
                juce::DocumentWindow window ("LYNX 190oq ARA insert - " + plugin.name, juce::Colours::black, juce::DocumentWindow::closeButton);
                window.setUsingNativeTitleBar (true);
                window.setContentNonOwned (editor, true);
                window.setTopLeftPosition (60, 60);
                window.setVisible (true);
                window.toFront (true);
                host->showChannelsInEditor (shown.channels[0].uid, { shown.channels[0].uid });
                say ("show: editor on screen (" + juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()) + ") for "
                     + juce::String (show) + " s");
                const auto until = juce::Time::getMillisecondCounterHiRes() + 1000.0 * show;
                while (juce::Time::getMillisecondCounterHiRes() < until)
                {
#if JUCE_WINDOWS || JUCE_LINUX
                    for (int k = 0; k < 64 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++k) {}
#endif
                    juce::Thread::sleep (15);
                }
                window.clearContentComponent();
                delete editor;
                say ("show: editor closed");
            }
        }
    }

    // 4. a project edited in the plugin's window
    if (project.existsAsFile())
    {
        juce::String error;
        if (auto loaded = ProjectFile::load (project, error))
        {
            const auto edited = render (*loaded, "project");
            say ("project.wav: " + juce::String ((int) edited.l.size()) + " samples, level " + db (rms (edited.l)));
        }
        else
            errors.add ("can't open " + project.getFileName() + ": " + error);
    }
    return errors.isEmpty() ? juce::String ("OK") : "ERROR " + errors.joinIntoString ("; ");
}
} // namespace lynx
