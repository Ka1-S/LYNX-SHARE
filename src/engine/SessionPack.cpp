#include "engine/SessionPack.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <map>
#include <set>

#include "core/MissingFiles.h"
#include "core/PatternBlocks.h"
#include "core/ProjectSafety.h"
#include "core/SessionIds.h"
#include "core/Sha256.h"
#include "core/StandIns.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "engine/ProjectFile.h"
#include "platform/Paths.h"

namespace lynx::sessionpack
{
namespace
{
juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
std::string utf8 (const juce::String& s) { return s.toStdString(); }

juce::String colourHex (std::uint32_t c) { return juce::String::toHexString ((juce::int64) (c & 0xFFFFFF)).paddedLeft ('0', 6); }
std::uint32_t colourFrom (const juce::var& v) { return 0xFF000000u | (std::uint32_t) v.toString().getHexValue32(); }

juce::var participantToVar (const SessionParticipant& p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("tag", (int) p.tag);
    o->setProperty ("name", str (p.name));
    o->setProperty ("colour", colourHex (p.colour));
    if (p.insertCount > 0)
        o->setProperty ("inserts", juce::Array<juce::var> { p.insertFirst, p.insertCount });
    if (p.rowCount > 0)
        o->setProperty ("rows", juce::Array<juce::var> { p.rowFirst, p.rowCount });
    o->setProperty ("keepFull", p.keepFull);
    return juce::var (o.release());
}

SessionParticipant participantFromVar (const juce::var& o)
{
    SessionParticipant p;
    p.tag = (std::uint32_t) juce::jlimit (0, 0xFFFFFF, (int) o.getProperty ("tag", 0));
    p.name = utf8 (o["name"].toString());
    p.colour = colourFrom (o["colour"]);
    if (const auto& r = o["inserts"]; r.isArray() && r.size() == 2)
        p.insertFirst = (int) r[0], p.insertCount = (int) r[1];
    if (const auto& r = o["rows"]; r.isArray() && r.size() == 2)
        p.rowFirst = (int) r[0], p.rowCount = (int) r[1];
    p.keepFull = (bool) o.getProperty ("keepFull", false);
    return p;
}

// a file's SHA-256, read in 1 MiB pieces
juce::String hashFileImpl (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return {};
    Sha256Stream hash;
    juce::HeapBlock<std::uint8_t> buffer (1 << 20);
    for (;;)
    {
        const auto n = in.read (buffer.get(), 1 << 20);
        if (n <= 0)
            break;
        hash.update (buffer.get(), (std::size_t) n);
    }
    return str (hash.hex());
}

// the message thread runs `fn` (it is the caller in command-line modes, else it is asked and waited for)
void onMessageThread (const std::function<void()>& fn)
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread())
    {
        fn();
        return;
    }
    juce::WaitableEvent done;
    juce::MessageManager::callAsync ([&] {
        fn();
        done.signal();
    });
    done.wait();
}

double ticksToSeconds (const Song& song, std::int64_t ticks) { return (double) ticks * 60.0 / (std::max (song.bpm, 1.0) * ppq); }

exportopts::Options stemOptions (double sampleRate, double tailSeconds)
{
    exportopts::Options o;
    o.format = exportopts::Format::wav;
    o.wavBits = 32;          // float: an insert's output before its fader can pass 0 dBFS
    o.dither = exportopts::DitherMode::none;
    o.sampleRate = sampleRate;
    o.range = exportopts::Range::song;
    o.tail = exportopts::TailMode::leave;
    o.maxTailSeconds = tailSeconds;
    o.hqAllPlugins = false;  // the quality that is heard while playing
    o.disableMaxPolyphony = false; // (and the channels' Max polyphony as while playing: lifted, a stem differs, round 190ay)
    o.normalize = false;
    o.loopMarkers = false;   // (no extra chunks: the stem's bytes are only audio)
    o.playlistMarkers = false;
    return o;
}

// renders `song` into `file` with its own engine; false when cancelled / failed
bool renderSong (const Song& song, const exportopts::Options& options, const juce::File& file, const std::function<bool (double)>& progress, juce::String& error)
{
    std::unique_ptr<ExportJob> job;
    onMessageThread ([&] { job = std::make_unique<ExportJob> (song, options, file); });
    const auto ok = job->run (progress, error);
    onMessageThread ([&] { job.reset(); });
    return ok;
}

bool readWav (const juce::File& file, juce::AudioBuffer<float>& buffer, double& rate)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr)
        return false;
    rate = reader->sampleRate;
    buffer.setSize (2, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    return true;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& source, int from, int count, double rate)
{
    juce::AudioBuffer<float> slice (2, std::max (1, count));
    slice.clear();
    for (int ch = 0; ch < 2; ++ch)
        if (from < source.getNumSamples())
            slice.copyFrom (ch, 0, source, std::min (ch, source.getNumChannels() - 1), from, std::max (0, std::min (count, source.getNumSamples() - from)));
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    if (out == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), rate, 2, 32, {}, 0));
    if (writer == nullptr)
        return false;
    out.release();
    return writer->writeFromAudioSampleBuffer (slice, 0, slice.getNumSamples());
}

// the last sample above −100 dB at or after `from` within [from, to)
int soundEnd (const juce::AudioBuffer<float>& b, int from, int to)
{
    for (int i = std::min (to, b.getNumSamples()); --i >= from;)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            if (std::abs (b.getSample (ch, i)) > 1.0e-5f)
                return i + 1;
    return from;
}

std::string pluginNames (const std::vector<const PluginRef*>& refs)
{
    std::string out;
    for (const auto* r : refs)
        out += (out.empty() ? "" : ", ") + r->name;
    return out;
}

bool allInstalled (const std::vector<const PluginRef*>& refs, PluginHost& host)
{
    return ! refs.empty() && std::all_of (refs.begin(), refs.end(), [&] (const PluginRef* r) { return host.isInstalled (*r); });
}

juce::File fileOf (const std::string& path)
{
    const auto p = str (path);
    return juce::File::isAbsolutePath (p) ? juce::File (p) : juce::File();
}

bool samePath (const juce::File& a, const juce::File& b) { return a.getFullPathName().equalsIgnoreCase (b.getFullPathName()); }

// `path` (absolute) names a file inside `root`. No "..", and no folder named only with dots and spaces either:
// Windows drops a name's trailing dots and spaces, so ". ." or "... " can climb like "..". Compared as text (case
// as the file system: JUCE's File ==), so a path spelled any other way than the folder (another Unicode form, an
// 8.3 short name, \\?\ prefix) counts as outside: a missing file, never a file elsewhere.
bool insideFolder (const std::string& path, const juce::File& root)
{
    if (root == juce::File() || path.empty())
        return false;
    for (const auto& part : juce::StringArray::fromTokens (str (path), "/\\", ""))
        if (part.isNotEmpty() && part.trimCharactersAtEnd (". ").isEmpty())
            return false;
    return fileOf (path).isAChildOf (root);
}
} // namespace

juce::String manifestToJson (const Manifest& m)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "lynxpack");
    root->setProperty ("version", formatVersion);
    root->setProperty ("kind", m.part ? "part" : "project");
    root->setProperty ("lynxVersion", m.lynxVersion);
    root->setProperty ("projectName", m.projectName);
    root->setProperty ("projectId", m.projectId);
    root->setProperty ("created", m.created);
    root->setProperty ("from", participantToVar (m.from));
    if (! m.part)
        root->setProperty ("to", participantToVar (m.to));
    juce::Array<juce::var> media;
    for (const auto& e : m.media)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", e.id);
        o->setProperty ("name", e.name);
        o->setProperty ("size", e.size);
        o->setProperty ("entry", e.entry);
        media.add (juce::var (o.release()));
    }
    root->setProperty ("media", media);
    juce::Array<juce::var> plugins;
    for (const auto& p : m.plugins)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("ref", p.ref);
        o->setProperty ("name", p.name);
        o->setProperty ("format", p.format);
        o->setProperty ("manufacturer", p.manufacturer);
        o->setProperty ("version", p.version);
        o->setProperty ("file", p.file);
        o->setProperty ("uniqueId", p.uniqueId);
        o->setProperty ("deprecatedUid", p.deprecatedUid);
        o->setProperty ("instrument", p.instrument);
        plugins.add (juce::var (o.release()));
    }
    root->setProperty ("plugins", plugins);
    juce::Array<juce::var> missing;
    for (const auto& s : m.missing)
        missing.add (s);
    if (! missing.isEmpty())
        root->setProperty ("missing", missing);
    root->setProperty ("standIns", m.standIns);
    return juce::JSON::toString (juce::var (root.release()), false);
}

std::optional<Manifest> manifestFromJson (const juce::String& json, juce::String& error)
{
    juce::var root;
    if (juce::JSON::parse (json, root).failed() || root["format"].toString() != "lynxpack")
    {
        error = "not a LYNX SHARE package";
        return std::nullopt;
    }
    if ((int) root["version"] > formatVersion)
    {
        error = "the package was made by a newer LYNX SHARE";
        return std::nullopt;
    }
    Manifest m;
    m.part = root["kind"].toString() == "part";
    m.lynxVersion = root["lynxVersion"].toString();
    m.projectName = root["projectName"].toString();
    m.projectId = root["projectId"].toString();
    m.created = root["created"].toString();
    m.from = participantFromVar (root["from"]);
    if (root.hasProperty ("to"))
        m.to = participantFromVar (root["to"]);
    if (auto* media = root["media"].getArray())
        for (const auto& o : *media)
            m.media.push_back ({ o["id"].toString(), o["name"].toString(), (juce::int64) o["size"], o["entry"].toString() });
    if (auto* plugins = root["plugins"].getArray())
        for (const auto& o : *plugins)
        {
            PluginHost::Identity p;
            p.ref = o["ref"].toString();
            p.name = o["name"].toString();
            p.format = o["format"].toString();
            p.manufacturer = o["manufacturer"].toString();
            p.version = o["version"].toString();
            p.file = o["file"].toString();
            p.uniqueId = (int) o["uniqueId"];
            p.deprecatedUid = (int) o["deprecatedUid"];
            p.instrument = (bool) o["instrument"];
            m.plugins.push_back (p);
        }
    if (auto* missing = root["missing"].getArray())
        for (const auto& s : *missing)
            m.missing.add (s.toString());
    m.standIns = (int) root.getProperty ("standIns", 0);
    return m;
}

std::vector<RenderItem> planRenders (const Song& song, PluginHost& host, std::uint32_t onlyTag)
{
    std::vector<RenderItem> items;
    const auto* who = song.session.find (onlyTag);
    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        const auto& ch = song.channels[(std::size_t) c];
        if (! standins::channelUsesPlugin (ch) || (onlyTag != 0 && uids::tagOf (ch.uid) != onlyTag))
            continue;
        std::vector<const PluginRef*> refs;
        standins::pluginsOf (ch, refs);
        if (! allInstalled (refs, host))
            continue; // (not ours to render: whoever has it did, its stand-ins stay)
        items.push_back ({ false, c, standins::needsTimeline (song, c), str (ch.name), str (pluginNames (refs)) });
    }
    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        const auto& track = song.mixer[(std::size_t) t];
        if (! standins::insertUsesPlugin (track) || (onlyTag != 0 && (who == nullptr || ! who->ownsInsert (t))))
            continue;
        std::vector<const PluginRef*> refs;
        standins::pluginsOf (track, refs);
        if (! allInstalled (refs, host))
            continue;
        items.push_back ({ true, t, true, track.name.empty() ? "Insert " + juce::String (t) : str (track.name), str (pluginNames (refs)) });
    }
    return items;
}

std::string sourceHash (const Song& song, bool insert, int index, int pattern, double sampleRate)
{
    Song mini;
    if (! insert && pattern >= 0 && index >= 0 && index < (int) song.channels.size() && pattern < (int) song.patterns.size())
    {
        // a pattern stem: the channel (without what applies live), its notes in that pattern, the tempo and swing
        mini.bpm = song.bpm;
        mini.beatsPerBar = song.beatsPerBar;
        mini.beatUnit = song.beatUnit;
        mini.swing = song.swing;
        mini.masterPitch = song.masterPitch;
        auto c = song.channels[(std::size_t) index];
        c.uid = 0;
        c.standIns.clear();
        c.name.clear();
        c.colour = 0;
        c.volume = 1.0f;
        c.pan = 0.0f;
        c.mixerTrack = 0;
        c.muted = false;
        c.group = -1;
        mini.channels = { c };
        const auto& source = song.patterns[(std::size_t) pattern];
        Pattern p;
        p.lengthTicks = source.effectiveLength (song.meter());
        p.loopOn = source.loopOn;
        p.loopMode = source.loopMode;
        for (auto n : source.notes)
            if (n.channel == index)
                n.channel = 0, p.notes.push_back (n);
        for (auto l : source.channelLoops)
            if (l.channel == index)
                l.channel = 0, p.channelLoops.push_back (l);
        for (auto lane : source.events)
            if (lane.target.kind == AutomationTargetKind::channelParam && lane.target.index == index)
                lane.target.index = 0, p.events.push_back (lane);
        mini.patterns = { p };
        mini.mixer = Song::defaultMixer (0);
    }
    else
    {
        // a timeline stem depends on the whole song (what plays into the insert, tempo automation...)
        mini = song;
        for (auto& c : mini.channels)
            c.standIns.clear(), c.name.clear(), c.colour = 0;
        for (auto& t : mini.mixer)
            t.standIns.clear(), t.colour = 0;
        mini.session = {};
        mini.info = {};
        mini.projectId.clear();
        mini.nextUid = 0;
        mini.currentPattern = 0;
        mini.mode = PlayMode::song;
    }
    const auto json = ProjectFile::toJson (mini, false) + "|" + (insert ? "insert " : "channel ") + juce::String (index) + "|"
                      + juce::String (pattern) + "|" + juce::String (sampleRate, 1);
    const auto bytes = json.toStdString();
    return sha256Hex ((const std::uint8_t*) bytes.data(), bytes.size());
}

bool render (const Song& song, const std::vector<RenderItem>& items, double sampleRate, const juce::File& folder, const juce::String& by,
             std::vector<Rendered>& out, const std::function<bool (double, const juce::String&)>& progress, juce::String& error)
{
    out.clear();
    if (items.empty())
        return true;
    if (! folder.createDirectory())
    {
        error = "could not create " + folder.getFullPathName();
        return false;
    }
    const auto tail = standins::tailTicks (song);
    const auto tailSeconds = ticksToSeconds (song, tail);
    const auto stamp = juce::String (uids::newProjectId()).substring (0, 8);

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        const auto base = (double) i / (double) items.size(), share = 1.0 / (double) items.size();
        auto step = [&] (double p) { return ! progress || progress (base + share * p, item.name); };
        if (! step (0.0))
            return false;

        Rendered r;
        r.insert = item.insert;
        r.index = item.index;
        const auto& existing = item.insert ? song.mixer[(std::size_t) item.index].standIns : song.channels[(std::size_t) item.index].standIns;
        if (! item.insert)
            r.uid = song.channels[(std::size_t) item.index].uid;
        auto upToDate = [&] (std::uint64_t patternUid, bool loop, const std::string& hash) -> const StandIn* {
            const auto* s = standins::find (existing, patternUid, loop);
            return s != nullptr && s->sourceHash == hash && fileOf (s->path).existsAsFile() ? s : nullptr;
        };
        const auto stem = juce::File::createLegalFileName ((item.insert ? "insert " + juce::String (item.index) : juce::String ("channel ")
                                                            + juce::String (item.index + 1)) + " " + item.name);
        const auto plugins = utf8 (item.plugins);

        if (item.timeline)
        {
            const auto hash = sourceHash (song, item.insert, item.index, -1, sampleRate);
            if (const auto* kept = upToDate (0, false, hash))
            {
                r.standIns.push_back (*kept);
                out.push_back (std::move (r));
                continue;
            }
            const auto file = folder.getChildFile (stem + " " + stamp + ".wav");
            const auto renderSongCopy = item.insert ? standins::insertTimelineRender (song, item.index) : standins::channelTimelineRender (song, item.index);
            if (! renderSong (renderSongCopy, stemOptions (sampleRate, tailSeconds), file, step, error))
                return false;
            StandIn s;
            s.path = utf8 (file.getFullPathName());
            s.sourceHash = hash;
            s.lengthTicks = song.songLength() + tail;
            s.plugin = plugins;
            s.by = utf8 (by);
            r.standIns.push_back (s);
            out.push_back (std::move (r));
            continue;
        }

        // pattern stems: one render of the channel, cut into each pattern's single pass (+ tail) and loop pass
        const auto plan = standins::channelPatternRender (song, item.index);
        bool allKept = ! plan.segments.empty();
        std::vector<std::string> hashes;
        for (const auto& seg : plan.segments)
        {
            hashes.push_back (sourceHash (song, false, item.index, seg.pattern, sampleRate));
            allKept = allKept && upToDate (seg.patternUid, false, hashes.back()) != nullptr && upToDate (seg.patternUid, true, hashes.back()) != nullptr;
        }
        if (plan.segments.empty())
        {
            out.push_back (std::move (r)); // plays in no pattern: nothing to render (its old stand-ins go)
            continue;
        }
        if (allKept)
        {
            for (std::size_t k = 0; k < plan.segments.size(); ++k)
            {
                r.standIns.push_back (*upToDate (plan.segments[k].patternUid, false, hashes[k]));
                r.standIns.push_back (*upToDate (plan.segments[k].patternUid, true, hashes[k]));
            }
            out.push_back (std::move (r));
            continue;
        }
        const auto whole = folder.getChildFile (stem + " " + stamp + " (render).wav");
        if (! renderSong (plan.song, stemOptions (sampleRate, tailSeconds), whole, step, error))
            return false;
        juce::AudioBuffer<float> audio;
        double rate = sampleRate;
        if (! readWav (whole, audio, rate))
        {
            error = "could not read " + whole.getFullPathName();
            return false;
        }
        whole.deleteFile();
        const auto samplesOf = [&] (std::int64_t ticks) { return (int) std::llround (ticksToSeconds (song, ticks) * rate); };
        for (std::size_t k = 0; k < plan.segments.size(); ++k)
        {
            const auto& seg = plan.segments[k];
            const auto& name = song.patterns[(std::size_t) seg.pattern].name;
            const auto label = juce::File::createLegalFileName (stem + " - " + str (name.empty() ? "Pattern " + std::to_string (seg.pattern + 1) : name));
            // the single pass, up to where its tail goes silent (at least the pattern)
            const auto from = samplesOf (seg.once), patternEnd = samplesOf (seg.once + seg.length);
            const auto end = std::max (patternEnd, soundEnd (audio, from, samplesOf (seg.once + seg.length + plan.tail)));
            const auto onceFile = folder.getChildFile (label + " " + stamp + ".wav");
            const auto loopFile = folder.getChildFile (label + " " + stamp + " (loop).wav");
            if (! writeWav (onceFile, audio, from, end - from, rate)
                || ! writeWav (loopFile, audio, samplesOf (seg.loopFrom), samplesOf (seg.loopFrom + seg.length) - samplesOf (seg.loopFrom), rate))
            {
                error = "could not write " + onceFile.getFullPathName();
                return false;
            }
            StandIn once;
            once.pattern = seg.patternUid;
            once.path = utf8 (onceFile.getFullPathName());
            once.sourceHash = hashes[k];
            once.lengthTicks = seg.length + (std::int64_t) std::llround ((double) (end - patternEnd) / rate * song.bpm * ppq / 60.0);
            once.plugin = plugins;
            once.by = utf8 (by);
            auto loop = once;
            loop.loop = true;
            loop.path = utf8 (loopFile.getFullPathName());
            loop.lengthTicks = seg.length;
            r.standIns.push_back (once);
            r.standIns.push_back (loop);
        }
        out.push_back (std::move (r));
    }
    return ! progress || progress (1.0, {});
}

void apply (Song& song, const std::vector<Rendered>& rendered)
{
    for (const auto& r : rendered)
    {
        if (r.insert)
        {
            if (r.index > 0 && r.index < (int) song.mixer.size())
                song.mixer[(std::size_t) r.index].standIns = r.standIns;
            continue;
        }
        for (auto& c : song.channels)
            if (c.uid == r.uid && r.uid != 0)
                c.standIns = r.standIns;
    }
}

std::string freezeSource (const Song& song, int channel, double sampleRate, int insert)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return {};
    if (insert < 0)
        insert = song.channels[(std::size_t) channel].freeze.insert;
    // a track freeze (round 190ff): the volume / pan and the insert's effects are in the stems, and so is what drives them
    const bool track = insert > 0 && insert < (int) song.mixer.size();
    auto drivesTrack = [&] (const AutomationTarget& t) {
        if (! track)
            return false;
        const bool level = (t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                            || (t.kind == AutomationTargetKind::channelAdjust && (t.param == leveladjust::pan || t.param == leveladjust::volume)))
                           && t.index == channel;
        const bool effects = (t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == insert;
        return level || effects;
    };
    auto remapped = [&] (AutomationTarget t) {
        t.index = t.index == insert && (t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) ? 1 : 0;
        return t;
    };
    Song mini;
    mini.bpm = song.bpm;
    mini.beatsPerBar = song.beatsPerBar;
    mini.beatUnit = song.beatUnit;
    mini.swing = song.swing;
    mini.masterPitch = song.masterPitch;
    auto c = song.channels[(std::size_t) channel];
    c.uid = 0;
    c.standIns.clear();
    c.freeze = {};
    c.name.clear();
    c.colour = 0;
    c.icon = 0;
    if (! track)
    {
        c.volume = 1.0f; // (volume, pan, mute and the insert apply live)
        c.pan = 0.0f;
    }
    c.mixerTrack = track ? 1 : 0;
    c.muted = false;
    c.group = -1;
    const auto stateBytes = c.plugin.state.size();
    c.plugin.state.clear();
    mini.channels = { c };
    // every pattern keeps its number (the clips refer to it), holding only what this channel plays in it
    std::vector<char> plays (song.patterns.size(), 0);
    for (std::size_t p = 0; p < song.patterns.size(); ++p)
    {
        const auto& source = song.patterns[p];
        Pattern q;
        for (auto n : source.notes)
            if (n.channel == channel)
                n.channel = 0, q.notes.push_back (n);
        plays[p] = ! q.notes.empty();
        if (plays[p])
        {
            q.lengthTicks = source.effectiveLength (song.meter());
            q.loopOn = source.loopOn;
            q.loopMode = source.loopMode;
            for (auto l : source.channelLoops)
                if (l.channel == channel)
                    l.channel = 0, q.channelLoops.push_back (l);
            for (auto lane : source.events)
                if (lane.target.kind == AutomationTargetKind::tempo
                    || (lane.target.kind == AutomationTargetKind::channelParam && lane.target.index == channel) || drivesTrack (lane.target))
                {
                    lane.target = remapped (lane.target);
                    q.events.push_back (lane);
                }
        }
        mini.patterns.push_back (std::move (q));
    }
    // the automation that reaches the stem: the tempo and the instrument's own parameters
    std::vector<int> kept (song.automations.size(), -1);
    for (std::size_t a = 0; a < song.automations.size(); ++a)
    {
        const auto& t = song.automations[a].target;
        if (t.kind != AutomationTargetKind::tempo && ! (t.kind == AutomationTargetKind::channelParam && t.index == channel) && ! drivesTrack (t))
            continue;
        auto copy = song.automations[a];
        copy.uid = 0;
        copy.target = remapped (copy.target);
        kept[a] = (int) mini.automations.size();
        mini.automations.push_back (std::move (copy));
    }
    auto clips = song.playlist;
    for (const auto& block : patternblocks::asClips (song)) // (FL's legacy Pattern Blocks play as clips)
        clips.push_back (block);
    for (auto clip : clips)
    {
        if (clip.kind == ClipKind::pattern)
        {
            if (clip.pattern < 0 || clip.pattern >= (int) plays.size() || ! plays[(std::size_t) clip.pattern])
                continue;
            clip.muted = clip.muted || song.isTrackMuted (clip.track);
        }
        else if (clip.kind == ClipKind::automation)
        {
            if (clip.automation < 0 || clip.automation >= (int) kept.size() || kept[(std::size_t) clip.automation] < 0)
                continue;
            clip.automation = kept[(std::size_t) clip.automation];
        }
        else
            continue;
        clip.track = 0;
        clip.uid = 0;
        clip.gridSlot = -1;
        mini.playlist.push_back (clip);
    }
    std::stable_sort (mini.playlist.begin(), mini.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    mini.mixer = Song::defaultMixer (0);
    juce::String extra;
    if (track)
    {
        // the insert's effects (plugin states by their size, as the channel's: an unloaded plugin cannot change), and
        // whether the freeze still holds (the channel on that insert, nothing else playing into it)
        MixerTrack t;
        t.effects = song.mixer[(std::size_t) insert].effects;
        for (auto& e : t.effects)
            if (e.kind == EffectKind::plugin)
            {
                extra << (juce::int64) e.plugin.state.size() << ",";
                e.plugin.state.clear();
            }
        t.chainBlend = song.mixer[(std::size_t) insert].chainBlend;
        mini.mixer.resize (2);
        mini.mixer[1] = t;
        const auto* taken = song.channels[(std::size_t) channel].mixerTrack != insert ? "moved" : freeze::whyNotTrack (song, channel);
        extra << "|track " << insert << "|" << (taken != nullptr ? taken : "holds");
    }
    const auto json = ProjectFile::toJson (mini, false) + "|freeze|" + juce::String ((juce::int64) stateBytes) + "|" + juce::String (song.songLength())
                      + "|" + juce::String (sampleRate, 1) + extra;
    const auto bytes = json.toStdString();
    return sha256Hex ((const std::uint8_t*) bytes.data(), bytes.size());
}

Song freshForExport (const Song& song, bool timelineKept, std::vector<int>* thawed)
{
    std::vector<int> stale;
    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        const auto& f = song.channels[(std::size_t) c].freeze;
        if (! f.on())
            continue;
        if (! timelineKept)
        {
            stale.push_back (c);
            continue;
        }
        // the rate the channel was frozen at is its audio's (the source counts it); audio that is not there is the
        // engine's case (it plays the instrument)
        const auto* whole = standins::find (f.stems, 0, false);
        if (whole == nullptr)
            continue;
        juce::WavAudioFormat wav;
        const auto file = fileOf (whole->path);
        if (! file.existsAsFile())
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        if (reader == nullptr || freezeSource (song, c, reader->sampleRate) != f.source)
            stale.push_back (c);
    }
    if (thawed != nullptr)
        *thawed = stale;
    if (stale.empty())
        return song;
    auto copy = song;
    for (const auto c : stale)
        copy.channels[(std::size_t) c].freeze = {};
    return copy;
}

bool renderSelection (const Song& song, double sampleRate, const juce::File& file, const std::function<bool (double)>& progress, juce::String& error)
{
    if (! file.getParentDirectory().createDirectory())
    {
        error = "could not create " + file.getParentDirectory().getFullPathName();
        return false;
    }
    auto o = stemOptions (sampleRate, 0.0);
    o.range = exportopts::Range::selection;
    o.tail = exportopts::TailMode::cut;
    return renderSong (song, o, file, progress, error);
}

bool renderFreeze (const Song& song, int channel, double sampleRate, const juce::File& folder, std::vector<StandIn>& stems,
                   const std::function<bool (double)>& progress, juce::String& error, int insert, bool timelineOnly)
{
    stems.clear();
    if (channel < 0 || channel >= (int) song.channels.size())
    {
        error = "no such channel";
        return false;
    }
    if (! folder.createDirectory())
    {
        error = "could not create " + folder.getFullPathName();
        return false;
    }
    const auto& def = song.channels[(std::size_t) channel];
    const auto tail = standins::tailTicks (song);
    const auto tailSeconds = ticksToSeconds (song, tail);
    const auto stamp = juce::String (uids::newProjectId()).substring (0, 8);
    const auto base = juce::File::createLegalFileName ((def.name.empty() ? juce::String ("Channel ") + juce::String (channel + 1) : str (def.name)) + " (frozen " + stamp + ")");
    const bool track = insert > 0 && insert < (int) song.mixer.size();
    std::vector<const PluginRef*> refs;
    standins::pluginsOf (def, refs);
    if (track)
        standins::pluginsOf (song.mixer[(std::size_t) insert], refs);
    const auto plugins = pluginNames (refs);
    // the pattern stems share one render: it is the shorter part unless the song is very short
    auto plan = timelineOnly ? standins::ChannelRender() : standins::channelPatternRender (song, channel, track);
    const auto songTicks = (double) std::max<std::int64_t> (1, song.songLength()), loopTicks = (double) plan.end;
    const auto share = songTicks / (songTicks + loopTicks);
    auto fail = [&] {
        for (const auto& s : stems)
            fileOf (s.path).deleteFile();
        stems.clear();
        return false;
    };

    // Song mode: the whole song
    {
        auto renderSongCopy = standins::channelTimelineRender (song, channel, track);
        freeze::lighten (renderSongCopy, channel, track ? insert : -1);
        const auto file = folder.getChildFile (base + ".wav");
        if (! renderSong (renderSongCopy, stemOptions (sampleRate, tailSeconds), file, [&] (double p) { return ! progress || progress (share * p); }, error))
            return fail();
        StandIn s;
        s.path = utf8 (file.getFullPathName());
        s.lengthTicks = song.songLength() + tail;
        s.plugin = plugins;
        stems.push_back (s);
    }

    // Pattern mode: each pattern's loop pass
    if (! plan.segments.empty())
    {
        auto renderSongCopy = plan.song;
        freeze::lighten (renderSongCopy, channel, track ? insert : -1);
        const auto whole = folder.getChildFile (base + " (render).wav");
        if (! renderSong (renderSongCopy, stemOptions (sampleRate, tailSeconds), whole, [&] (double p) { return ! progress || progress (share + (1.0 - share) * p); }, error))
            return fail();
        juce::AudioBuffer<float> audio;
        double rate = sampleRate;
        const auto read = readWav (whole, audio, rate);
        whole.deleteFile();
        if (! read)
        {
            error = "could not read " + whole.getFullPathName();
            return fail();
        }
        const auto samplesOf = [&] (std::int64_t ticks) { return (int) std::llround (ticksToSeconds (song, ticks) * rate); };
        for (const auto& seg : plan.segments)
        {
            const auto& name = song.patterns[(std::size_t) seg.pattern].name;
            const auto file = folder.getChildFile (base + " - " + juce::File::createLegalFileName (str (name.empty() ? "Pattern " + std::to_string (seg.pattern + 1) : name))
                                                   + " " + juce::String (seg.pattern + 1) + " (loop).wav");
            if (! writeWav (file, audio, samplesOf (seg.loopFrom), samplesOf (seg.loopFrom + seg.length) - samplesOf (seg.loopFrom), rate))
            {
                error = "could not write " + file.getFullPathName();
                return fail();
            }
            StandIn s;
            s.pattern = seg.patternUid;
            s.loop = true;
            s.path = utf8 (file.getFullPathName());
            s.lengthTicks = seg.length;
            s.plugin = plugins;
            stems.push_back (s);
        }
    }
    return ! progress || progress (1.0);
}

bool write (const Song& song, Manifest manifest, PluginHost& host, const juce::File& file, WriteResult& result, juce::String& error)
{
    result = {};
    manifest.media.clear();
    manifest.plugins.clear();
    manifest.missing.clear();
    manifest.lynxVersion = JUCE_APPLICATION_VERSION_STRING;
    manifest.projectId = str (song.projectId);
    manifest.created = juce::Time::getCurrentTime().toISO8601 (true);

    Song packed = song;
    std::map<std::string, std::string> relink;
    std::map<juce::String, juce::String> entryOfHash; // the same bytes twice are stored once
    juce::ZipFile::Builder builder;
    for (const auto& original : safety::referencedSamples (song))
    {
        const auto source = fileOf (original);
        if (! source.existsAsFile())
        {
            result.missing.add (str (original));
            continue; // (the project keeps the old path)
        }
        const auto hash = hashFileImpl (source);
        if (hash.isEmpty())
        {
            result.missing.add (str (original));
            continue;
        }
        auto it = entryOfHash.find (hash);
        if (it == entryOfHash.end())
        {
            const auto entry = "media/" + hash + source.getFileExtension().toLowerCase();
            builder.addFile (source, 0, entry); // (audio barely compresses)
            it = entryOfHash.emplace (hash, entry).first;
            manifest.media.push_back ({ "sha256-" + hash, source.getFileName(), source.getSize(), entry });
            ++result.media;
            result.bytes += source.getSize();
        }
        else
            ++result.duplicates;
        relink[original] = utf8 (it->second);
    }
    applyRelocations (packed, relink);
    manifest.missing = result.missing;

    // the third-party plugins the song uses (their portable identities; never the whole plugin list)
    std::vector<const PluginRef*> refs;
    for (const auto& c : song.channels)
        standins::pluginsOf (c, refs);
    for (const auto& t : song.mixer)
        standins::pluginsOf (t, refs);
    std::set<std::string> seen;
    for (const auto* r : refs)
        if (seen.insert (r->id).second)
            if (auto id = host.portableIdentity (*r))
                manifest.plugins.push_back (*id);
    manifest.standIns = 0;
    for (const auto& c : song.channels)
        manifest.standIns += (int) c.standIns.size();
    for (const auto& t : song.mixer)
        manifest.standIns += (int) t.standIns.size();

    const auto json = ProjectFile::toJson (packed);
    builder.addEntry (new juce::MemoryInputStream (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 9, "project.lynx", juce::Time::getCurrentTime());
    const auto manifestJson = manifestToJson (manifest);
    builder.addEntry (new juce::MemoryInputStream (manifestJson.toRawUTF8(), manifestJson.getNumBytesAsUTF8(), true), 9, "manifest.json",
                      juce::Time::getCurrentTime());

    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
        {
            error = "could not write " + file.getFullPathName();
            return false;
        }
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

juce::String hashFile (const juce::File& file)
{
    return hashFileImpl (file);
}

void confine (Song& song, const juce::File& folder, const juce::File& engineFiles)
{
    // a path anywhere else on this computer would be copied into the project and sent on with the next package: it
    // becomes a missing file under its own name
    auto inside = [&] (const std::string& path) { return insideFolder (path, folder); };
    auto within = [&] (const std::string& path) { return inside (path) || (engineFiles != juce::File() && insideFolder (path, engineFiles)); };
    auto missing = [&] (const std::string& path) {
        const auto p = str (path);
        const auto name = juce::File::createLegalFileName (p.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false));
        return utf8 (folder.getChildFile ("missing").getChildFile (name).getFullPathName());
    };
    std::map<std::string, std::string> outside;
    for (const auto& path : safety::referencedSamples (song))
        if (! inside (path))
            outside[path] = missing (path);
    applyRelocations (song, outside);
    // every other path the engine reads, also inside Patcher graphs (not in the media list, never packed)
    auto engineOnly = [&] (std::string& path) {
        if (! path.empty() && ! within (path))
            path = missing (path);
    };
    std::function<void (EffectSlot&)> slot;
    std::function<void (PatcherDoc&)> doc;
    std::function<void (ChannelDef&)> channel;
    slot = [&] (EffectSlot& s) {
        engineOnly (s.samplePath);
        for (auto& b : s.branches)
            for (auto& child : b.chain)
                slot (child);
        doc (s.patcher);
    };
    doc = [&] (PatcherDoc& d) {
        for (auto& n : d.nodes)
        {
            if (n.generator)
                channel (n.channel);
            else
                slot (n.effect);
            for (auto& h : n.busHeads)
                if (h.head)
                    slot (*h.head);
        }
    };
    channel = [&] (ChannelDef& c) {
        engineOnly (c.samplePath);
        for (auto& z : c.zones)
            engineOnly (z.path);
        for (auto& s : c.standIns)
            engineOnly (s.path);
        doc (c.patcher);
    };
    for (auto& c : song.channels)
        channel (c);
    for (auto& t : song.mixer)
    {
        for (auto& s : t.effects)
            slot (s);
        for (auto& s : t.standIns)
            engineOnly (s.path);
        // a file the insert records and renders to: written, so only inside the folder (else the automatic name)
        if (! t.recordTo.empty() && ! inside (t.recordTo))
            t.recordTo.clear();
    }
    song.dataFolder.clear(); // the sender's folder: nothing to do with this computer
}

std::optional<Manifest> readManifest (const juce::File& file, juce::String& error)
{
    juce::ZipFile zip (file);
    const auto index = zip.getIndexOfFileName ("manifest.json");
    if (index < 0)
    {
        error = "not a LYNX SHARE package (no manifest)";
        return std::nullopt;
    }
    std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (index));
    if (in == nullptr)
    {
        error = "could not read the package";
        return std::nullopt;
    }
    return manifestFromJson (in->readEntireStreamAsString(), error);
}

std::optional<Opened> open (const juce::File& file, const juce::File& destinationRoot, juce::String& error)
{
    auto manifest = readManifest (file, error);
    if (! manifest)
        return std::nullopt;
    juce::ZipFile zip (file);
    if (zip.getIndexOfFileName ("project.lynx") < 0)
    {
        error = "the package has no project";
        return std::nullopt;
    }
    for (int i = 0; i < zip.getNumEntries(); ++i)
        if (zip.getEntry (i)->isSymbolicLink) // (JUCE would create it, and copying "media" through it reads any file here)
        {
            error = "the package contains a link";
            return std::nullopt;
        }
    destinationRoot.createDirectory();
    const auto folder = destinationRoot.getNonexistentChildFile (juce::File::createLegalFileName (file.getFileNameWithoutExtension()), {}, false);
    if (! folder.createDirectory())
    {
        error = "could not create " + folder.getFullPathName();
        return std::nullopt;
    }
    if (const auto r = zip.uncompressTo (folder, false); r.failed()) // (JUCE refuses entries outside the folder)
    {
        error = r.getErrorMessage();
        return std::nullopt;
    }
    Opened opened;
    opened.manifest = *manifest;
    opened.folder = folder;
    opened.project = folder.getChildFile ("project.lynx");
    // the project file takes the project's name (the window title and Recent projects show it)
    if (const auto named = folder.getChildFile (juce::File::createLegalFileName (manifest->projectName.isNotEmpty() ? manifest->projectName
                                                                                                                 : file.getFileNameWithoutExtension())
                                                  + ".lynx");
        ! named.exists() && opened.project.moveFileTo (named))
        opened.project = named;
    for (const auto& m : manifest->media)
    {
        const auto f = folder.getChildFile (m.entry); // (the manifest is the sender's too: "../" or "/dev/zero" is never read)
        if (! insideFolder (utf8 (f.getFullPathName()), folder) || ! f.existsAsFile() || "sha256-" + hashFileImpl (f) != m.id)
            opened.corrupt.add (m.name);
    }
    auto song = ProjectFile::load (opened.project, error);
    if (! song)
        return std::nullopt;
    // a received project may only use its own media (or FL Studio's own files: FL Keys' wavetables, the engines' DLLs)
    const auto flInstall = findFlStudioRoots().factoryData;
    confine (*song, folder, flInstall.empty() ? juce::File() : juce::File (str (flInstall)));
    opened.song = std::move (*song);
    return opened;
}

PluginReport resolvePlugins (Song& song, const Manifest& manifest, PluginHost& host, bool useOtherVersions)
{
    PluginReport report;
    std::map<std::string, std::string> rewrite; // their id -> ours
    std::set<std::string> listed;
    for (const auto& identity : manifest.plugins)
    {
        const auto ref = utf8 (identity.ref);
        PluginRef probe;
        probe.id = ref;
        if (host.isInstalled (probe)) // the same id here (the same file path): nothing to do
        {
            report.found.addIfNotAlreadyThere (identity.name);
            continue;
        }
        bool sameFormat = false;
        const auto found = host.findByIdentity (identity, &sameFormat);
        if (! found || ! sameFormat)
        {
            report.missing.addIfNotAlreadyThere (identity.name + (found ? " (" + found->pluginFormatName + ")" : juce::String()));
            continue;
        }
        if (found->version != identity.version && ! useOtherVersions)
        {
            report.otherVersion.addIfNotAlreadyThere (identity.name + " " + identity.version + " / " + found->version);
            continue;
        }
        rewrite[ref] = utf8 (found->createIdentifierString());
        report.found.addIfNotAlreadyThere (identity.name);
    }
    std::function<void (EffectSlot&)> slot;
    std::function<void (PatcherDoc&)> doc;
    std::function<void (ChannelDef&)> channel;
    auto fix = [&] (PluginRef& r) {
        if (auto it = rewrite.find (r.id); it != rewrite.end())
            r.id = it->second;
    };
    slot = [&] (EffectSlot& s) {
        if (s.kind == EffectKind::plugin)
            fix (s.plugin);
        for (auto& b : s.branches)
            for (auto& child : b.chain)
                slot (child);
        doc (s.patcher);
    };
    doc = [&] (PatcherDoc& d) {
        for (auto& n : d.nodes)
            if (n.generator)
                channel (n.channel);
            else
                slot (n.effect);
    };
    channel = [&] (ChannelDef& c) {
        if (c.instrument == InstrumentKind::plugin)
            fix (c.plugin);
        doc (c.patcher);
    };
    for (auto& c : song.channels)
        channel (c);
    for (auto& t : song.mixer)
        for (auto& s : t.effects)
            slot (s);
    return report;
}

juce::File receivedRoot()
{
    return platform::documentsFolder().getChildFile ("LYNX/Projects/Received");
}

juce::File standInFolder (const juce::File& projectFile, const Song& song)
{
    if (projectFile != juce::File())
        return projectFile.getParentDirectory().getChildFile ("Stand-ins");
    return platform::documentsFolder().getChildFile ("LYNX/Stand-ins").getChildFile (str (song.projectId.empty() ? std::string ("project") : song.projectId));
}

juce::File sessionMediaFolder (const juce::File& projectFile, const Song& song, const juce::String& guest)
{
    const auto name = juce::File::createLegalFileName (guest.isNotEmpty() ? guest : juce::String ("Guest"));
    if (projectFile != juce::File())
        return projectFile.getParentDirectory().getChildFile ("Session media").getChildFile (name);
    return platform::documentsFolder().getChildFile ("LYNX/Session media").getChildFile (str (song.projectId)).getChildFile (name);
}

bool adoptMedia (Song& song, const juce::File& folder, juce::String& error)
{
    if (! folder.createDirectory())
    {
        error = "could not create " + folder.getFullPathName();
        return false;
    }
    std::map<std::string, std::string> relink;
    std::map<juce::String, juce::File> byHash;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false))
        byHash.emplace (hashFileImpl (f), f);
    for (const auto& original : safety::referencedSamples (song))
    {
        const auto source = fileOf (original);
        if (! source.existsAsFile() || source.isAChildOf (folder))
            continue;
        const auto hash = hashFileImpl (source);
        if (auto it = byHash.find (hash); it != byHash.end())
        {
            relink[original] = utf8 (it->second.getFullPathName());
            continue;
        }
        // a readable name (the media are stored by hash inside packages): the stand-in / take's own name if known
        auto name = source.getFileNameWithoutExtension();
        if (name.length() == 64 && name.containsOnly ("0123456789abcdef"))
            name = "media " + name.substring (0, 8);
        const auto target = folder.getNonexistentChildFile (juce::File::createLegalFileName (name), source.getFileExtension(), false);
        if (! source.copyFileTo (target))
        {
            error = "could not copy " + source.getFullPathName();
            return false;
        }
        byHash.emplace (hash, target);
        relink[original] = utf8 (target.getFullPathName());
    }
    applyRelocations (song, relink);
    return true;
}
} // namespace lynx::sessionpack
