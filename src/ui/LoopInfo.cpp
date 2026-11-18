#include "ui/LoopInfo.h"

#include "core/SampleFiles.h"
#include "engine/AudioEngine.h"
#include "engine/SampleLibrary.h"
#include "platform/Paths.h"
#include "ui/I18n.h"

namespace lynx::loopinfo
{
namespace
{
juce::File cacheFile() { return platform::settingsFile ("loop-analysis.txt"); }
}

struct Shared::Reader
{
    SampleLibrary library; // its own readers: nothing goes into the engine's sample cache
};

std::shared_ptr<Shared> Shared::get (AudioEngine& engine)
{
    static std::weak_ptr<Shared> current; // (message thread)
    if (auto s = current.lock())
        return s;
    auto s = std::make_shared<Shared> (engine);
    current = s;
    return s;
}

Shared::Shared (AudioEngine& engine) : reader (std::make_unique<Reader>())
{
    // the results of earlier sessions (checked against each file's size and time before they are used)
    if (const auto f = cacheFile(); f.existsAsFile() && f.getSize() < 32 * 1024 * 1024)
    {
        juce::StringArray lines;
        lines.addLines (f.loadFileAsString());
        for (const auto& line : lines)
        {
            loopanalysis::Stamp stamp;
            loopanalysis::Result r;
            if (loopanalysis::fromText (line.toStdString(), stamp, r) && ! loopanalysis::Cache::shared().find (stamp))
                loopanalysis::Cache::shared().put (stamp, r);
        }
    }
    loopanalysis::Service::Callbacks cb;
    cb.stat = [] (const std::string& path, std::int64_t& size, std::int64_t& modified) {
        const juce::File f (juce::String::fromUTF8 (path.c_str()));
        if (! f.existsAsFile())
            return false;
        size = f.getSize();
        modified = f.getLastModificationTime().toMilliseconds();
        return true;
    };
    cb.decode = [raw = reader.get()] (const std::string& path, double seconds, SampleData& out, double& total) {
        return raw->library.readForAnalysis (juce::File (juce::String::fromUTF8 (path.c_str())), seconds, out, total);
    };
    cb.analysed = [this] (const std::string& path) { answered (path); };
    // (the engine outlives every holder; these are atomics)
    cb.shouldPause = [&engine] { return engine.isRecording() || AudioEngine::offlineRendersRunning().load() > 0; };
    worker = std::make_unique<loopanalysis::Service> (std::move (cb));
}

Shared::~Shared()
{
    worker.reset(); // (joins: nothing is answered after this)
    removeAllChangeListeners();
    // keep what is known for the next session
    juce::String text;
    for (const auto& [stamp, r] : loopanalysis::Cache::shared().all())
        if (const auto line = loopanalysis::toText (stamp, r); ! line.empty())
            text << juce::String::fromUTF8 (line.c_str()) << "\n";
    if (text.isNotEmpty())
    {
        const auto f = cacheFile();
        const auto temp = f.getSiblingFile (f.getFileName() + ".tmp");
        if (temp.replaceWithText (text))
            temp.moveFileTo (f);
    }
}

void Shared::request (const std::vector<std::string>& paths)
{
    std::vector<std::string> wanted;
    {
        const std::lock_guard<std::mutex> hold (lock);
        for (const auto& p : paths)
            if (results.find (p) == results.end())
            {
                const auto dot = p.find_last_of ('.');
                if (dot != std::string::npos && isSampleExtension (p.substr (dot + 1)) && p.size() - dot != 7) // (not .speech)
                    wanted.push_back (p);
            }
    }
    if (! wanted.empty())
        worker->request (wanted);
}

std::optional<loopanalysis::Result> Shared::known (const std::string& path) const
{
    const std::lock_guard<std::mutex> hold (lock);
    if (const auto it = results.find (path); it != results.end())
        return it->second;
    return std::nullopt;
}

std::size_t Shared::knownCount() const
{
    const std::lock_guard<std::mutex> hold (lock);
    return results.size();
}

void Shared::answered (const std::string& path)
{
    if (const auto r = loopanalysis::Cache::shared().findPath (path))
    {
        {
            const std::lock_guard<std::mutex> hold (lock);
            results[path] = *r;
        }
        sendChangeMessage(); // (coalesced: the listeners repaint once for many answers)
    }
}

juce::String rowText (const loopanalysis::Result& r, Meter meter)
{
    juce::StringArray parts;
    if (r.isLoop())
    {
        const auto barBeats = (double) meter.bar() / (double) ppq;
        const auto bars = r.beats / barBeats;
        if (std::abs (bars - std::round (bars)) < 0.02 && std::round (bars) >= 1.0)
            parts.add (std::round (bars) == 1.0 ? tr ("1 bar") : tr ("{0} bars", { juce::String ((int) std::round (bars)) }));
    }
    if (r.bpm > 0.0 && (r.isLoop() || r.tempoSource != loopanalysis::TempoSource::detected))
        parts.add (tr ("{0} BPM", { juce::String (r.bpm, std::abs (r.bpm - std::round (r.bpm)) < 0.05 ? 0 : 1) }));
    if (r.key)
        parts.add (juce::String (projectkey::rootName (r.key->root)) + " " + (r.key->minor ? tr ("minor") : tr ("major")));
    return parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
}
} // namespace lynx::loopinfo
