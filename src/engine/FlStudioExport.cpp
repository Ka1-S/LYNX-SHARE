#include "engine/FlStudioExport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <set>
#include <tuple>

#include "engine/PluginHost.h"

namespace lynx::flstudio
{
namespace
{
juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
} // namespace

flpexport::Options optionsFor (PluginHost* host, flpexport::LynxOnly mode)
{
    flpexport::Options o;
    o.lynxOnly = mode;
    if (host != nullptr)
        o.pluginRecord = [host] (const PluginRef& ref, bool instrument) { return host->flWrapperRecord (ref, instrument); };
    o.sampleSeconds = [] (const std::string& path) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const juce::File file (str (path));
        if (! juce::File::isAbsolutePath (str (path)) || ! file.existsAsFile())
            return 0.0;
        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        return reader != nullptr && reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
    };
    return o;
}

flpexport::Options optionsFor (PluginHost* host, flpexport::LynxOnly mode, const Song& song)
{
    auto o = optionsFor (host, mode);
    if (host == nullptr)
        return o;
    // (id, state, instrument) → the record, looked up now
    using Key = std::tuple<std::string, std::string, bool>;
    auto cache = std::make_shared<std::map<Key, std::optional<ForeignPlugin>>>();
    auto look = [&] (const PluginRef& ref, bool instrument) {
        const Key key { ref.id, ref.state, instrument };
        if (cache->count (key) == 0)
            (*cache)[key] = host->flWrapperRecord (ref, instrument);
    };
    for (const auto& c : song.channels)
        if (c.instrument == InstrumentKind::plugin)
            look (c.plugin, true);
    for (const auto& t : song.mixer)
        for (const auto& fx : t.effects)
            if (fx.kind == EffectKind::plugin)
                look (fx.plugin, false);
    o.pluginRecord = [cache] (const PluginRef& ref, bool instrument) -> std::optional<ForeignPlugin> {
        const auto found = cache->find (Key { ref.id, ref.state, instrument });
        return found != cache->end() ? found->second : std::nullopt;
    };
    return o;
}

bool exportProject (const Song& song, const flpexport::Options& options, const juce::File& target, bool zip,
                    const juce::File& renderDir, double sampleRate, const RenderFn& render,
                    const std::function<bool (double, const juce::String&)>& progress, Written& written, juce::String& error)
{
    const auto plan = flpexport::planRenders (song, options);
    Song out = song;
    flpexport::Report rendered;
    if (! plan.empty())
    {
        if (! render)
        {
            error = "no renderer";
            return false;
        }
        if (! renderDir.createDirectory())
        {
            error = "could not create " + renderDir.getFullPathName();
            return false;
        }
        ExportSettings settings;
        settings.format = ExportSettings::Format::wav32Float;
        settings.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        settings.tailSeconds = 4.0;
        // the clip length: the song + the tail, at the ending tempo (as "Render to audio")
        std::int64_t musicTicks = 0;
        makeExportSong (song, true, 0, musicTicks);
        const auto length = musicTicks + (std::int64_t) (settings.tailSeconds * song.tempoAt ((double) musicTicks) * ppq / 60.0);

        struct Job
        {
            bool channel;
            int index;
            juce::String name;
            juce::File wav;
        };
        std::vector<Job> jobs;
        auto wavFor = [&] (const std::string& name) {
            auto base = juce::File::createLegalFileName (str (name)).trim();
            if (base.isEmpty())
                base = "render";
            return renderDir.getNonexistentChildFile (base, ".wav", false);
        };
        for (const auto c : plan.channels)
            jobs.push_back ({ true, c, str (song.channels[(std::size_t) c].name), {} });
        for (const auto t : plan.inserts)
            jobs.push_back ({ false, t, str (song.mixer[(std::size_t) t].name), {} });

        for (std::size_t i = 0; i < jobs.size(); ++i)
        {
            auto& job = jobs[i];
            const auto label = (job.channel ? "Channel " + juce::String (job.index + 1) : "Insert " + juce::String (job.index))
                               + (job.name.isEmpty() ? juce::String() : " '" + job.name + "'");
            if (progress && ! progress ((double) i / (double) (jobs.size() + 1), label))
                return false;
            job.wav = wavFor ((job.channel ? "Channel " : "Insert ") + std::to_string (job.channel ? job.index + 1 : job.index)
                              + (job.name.isEmpty() ? std::string() : " " + job.name.toStdString()));
            // (every render from the song as it is: the renders don't hear each other's changes)
            const auto renderSong = job.channel ? flpexport::makeFreezeSong (song, job.index) : makeBounceSong (song, job.index);
            const auto base = (double) i / (double) (jobs.size() + 1);
            const auto ok = render (renderSong, settings, job.wav, [&] (double p) {
                return ! progress || progress (base + p / (double) (jobs.size() + 1), label);
            }, error);
            if (! ok)
            {
                if (error.isEmpty())
                    error = "cancelled";
                return false;
            }
        }
        for (const auto& job : jobs)
            if (job.channel)
                flpexport::applyChannelRender (out, job.index, job.wav.getFullPathName().toStdString(), length, rendered);
            else if (flpexport::applyInsertRender (out, job.index, job.wav.getFullPathName().toStdString(), length, options, rendered) < 0)
                rendered.notes.push_back ("Insert " + std::to_string (job.index) + ": the mixer is full, its render was not added");
    }
    if (progress && ! progress (plan.empty() ? 0.5 : (double) (plan.channels.size() + plan.inserts.size()) / (double) (plan.channels.size() + plan.inserts.size() + 1),
                                "FL Studio project"))
        return false;
    if (! write (out, options, target, zip, written, error))
        return false;
    written.report.rendered.insert (written.report.rendered.begin(), rendered.rendered.begin(), rendered.rendered.end());
    written.report.notes.insert (written.report.notes.end(), rendered.notes.begin(), rendered.notes.end());
    return true;
}

juce::String reportText (const Written& written, const std::function<juce::String (const juce::String&)>& translate)
{
    auto t = [&] (const char* text) { return translate ? translate (text) : juce::String (text); };
    juce::String text;
    auto section = [&] (const char* heading, const std::vector<std::string>& lines) {
        if (lines.empty())
            return;
        text << t (heading) << " (" << (int) lines.size() << ")\n";
        for (const auto& line : lines)
            text << "  - " << str (line) << "\n";
        text << "\n";
    };
    section ("Exported exactly", written.report.exact);
    section ("Rendered to audio", written.report.rendered);
    section ("Replaced by an FL plugin", written.report.substituted);
    section ("Left out", written.report.dropped);
    section ("Worth knowing", written.report.notes);
    if (! written.missing.isEmpty())
    {
        text << t ("Samples not on this PC") << " (" << written.missing.size() << ")\n";
        for (const auto& m : written.missing)
            text << "  - " << m << "\n";
    }
    return text.trimEnd();
}

bool write (const Song& song, flpexport::Options options, const juce::File& target, bool zip, Written& written, juce::String& error)
{
    written = {};
    // the package: every sample by its file name (made unique), FL's own layout
    std::map<std::string, std::string> inside; // original path → name in the zip
    std::set<juce::String> taken;
    if (zip)
        options.samplePath = [&] (const std::string& path) -> std::string {
            if (const auto found = inside.find (path); found != inside.end())
                return found->second;
            const auto p = str (path);
            const juce::File file (p);
            if (! juce::File::isAbsolutePath (p) || ! file.existsAsFile())
            {
                written.missing.addIfNotAlreadyThere (p);
                return path;
            }
            auto name = file.getFileName();
            for (int k = 2; taken.count (name.toLowerCase()) > 0; ++k)
                name = file.getFileNameWithoutExtension() + " (" + juce::String (k) + ")" + file.getFileExtension();
            taken.insert (name.toLowerCase());
            return inside[path] = name.toStdString();
        };
#if JUCE_WINDOWS
    else
        options.samplePath = [] (const std::string& path) { return str (path).replaceCharacter ('/', '\\').toStdString(); };
#endif

    const auto bytes = flpexport::exportFlp (song, options, written.report);
    if (bytes.empty())
    {
        error = "nothing to write";
        return false;
    }

    juce::TemporaryFile temp (target);
    if (! zip)
    {
        if (! temp.getFile().replaceWithData (bytes.data(), bytes.size()))
        {
            error = "could not write " + target.getFullPathName();
            return false;
        }
    }
    else
    {
        juce::ZipFile::Builder builder;
        for (const auto& [original, name] : inside)
        {
            builder.addFile (juce::File (str (original)), 0, str (name)); // (samples barely compress)
            ++written.samples;
        }
        const auto flp = juce::File::createLegalFileName (target.getFileNameWithoutExtension()) + ".flp";
        builder.addEntry (new juce::MemoryInputStream (bytes.data(), bytes.size(), true), 9, flp, juce::Time::getCurrentTime());
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
        {
            error = "could not write " + target.getFullPathName();
            return false;
        }
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "could not write " + target.getFullPathName();
        return false;
    }
    return true;
}
} // namespace lynx::flstudio
