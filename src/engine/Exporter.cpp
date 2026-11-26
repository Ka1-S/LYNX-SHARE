#include "engine/Exporter.h"
#include "core/AraRegions.h"
#include "engine/AraHost.h"
#include "core/MixerOps.h" // (round 190my: the Current track)

#include "core/Mastering.h"

#include "core/Dither.h"
#include "core/MessageThreadOwned.h"
#include "core/Mp3Encoder.h"
#include "engine/AacEncoder.h"
#include "engine/AudioEngine.h"
#include "core/Patcher.h"
#include "engine/PluginHost.h"
#include "engine/PowerHints.h"
#include "engine/SessionPack.h"
#include "engine/Text.h"
#include "ui/I18n.h"

namespace lynx
{
using namespace exportopts;

juce::String exportExtension (ExportSettings::Format format)
{
    switch (format)
    {
        case ExportSettings::Format::flac24: return ".flac";
        case ExportSettings::Format::ogg: return ".ogg";
        default: return ".wav";
    }
}

Options ExportSettings::toOptions() const
{
    Options o;
    switch (format)
    {
        case Format::wav16: o.format = exportopts::Format::wav; o.wavBits = 16; break;
        case Format::wav24: o.format = exportopts::Format::wav; o.wavBits = 24; break;
        case Format::wav32Float: o.format = exportopts::Format::wav; o.wavBits = 32; break;
        case Format::flac24: o.format = exportopts::Format::flac; o.flacBits = 24; o.flacLevel = 0; break;
        case Format::ogg: o.format = exportopts::Format::ogg; o.oggKbps = 256; break;
    }
    o.dither = format == Format::wav16 ? DitherMode::shaped : DitherMode::none; // as LYNX always did
    o.sampleRate = sampleRate;
    o.range = wholeSong ? Range::song : Range::pattern;
    o.tail = TailMode::leave;
    o.maxTailSeconds = tailSeconds;
    return o;
}

namespace
{
// M4A without a system AAC encoder (Linux; a Windows N edition without the Media Feature Pack): said before the render
bool formatsWritable (const Options& o, juce::String& error)
{
    for (const auto f : formatsOf (o))
        if (f == exportopts::Format::aac && ! aac::available())
        {
            error = juce::String::fromUTF8 (aac::unavailableReason().c_str());
            return false;
        }
    return true;
}

// tail length in ticks for `seconds` at the tempo where the music ends
std::int64_t tailTicksFor (const Song& song, const Options& o, std::int64_t& musicTicks, std::int64_t& loopTicks)
{
    makeRenderSong (song, o, 0, musicTicks, loopTicks);
    const auto seconds = o.tail == TailMode::cut ? 0.0 : o.maxTailSeconds;
    return (std::int64_t) (seconds * song.tempoAt ((double) musicTicks) * ppq / 60.0);
}

double tailSecondsFor (const Options& o)
{
    return o.tail == TailMode::cut ? 0.0 : o.maxTailSeconds;
}

// Round 162 (issue #9: exporting over a file of the same name failed): the file is written next to the target under a
// temporary name and replaces it only once complete, so a cancelled or failed export leaves the old file as it was. A
// target another program holds open (a media player, a preview) is reported before the render starts.
bool canReplace (const juce::File& target)
{
    if (! target.exists())
        return true;
    if (target.isDirectory())
        return false;
    juce::FileOutputStream probe (target); // opening for writing fails while another program holds it (Windows)
    return probe.openedOk();
}

juce::String inUseMessage (const juce::File& target)
{
    return tr ("\"{0}\" is in use by another program (a media player, or a preview). Close it, or export with another name.",
               { target.getFileName() });
}

bool replaceTarget (juce::TemporaryFile& temp, const juce::File& target, juce::String& error)
{
    if (temp.overwriteTargetFileWithTemporary())
        return true;
    error = canReplace (target) ? tr ("could not replace {0}", { target.getFullPathName() }) : inUseMessage (target);
    return false;
}

int oggQualityIndex (juce::AudioFormat& format, int kbps)
{
    const auto options = format.getQualityOptions();
    for (int i = 0; i < options.size(); ++i)
        if (options[i].getIntValue() == kbps)
            return i;
    return juce::jmax (0, options.size() - 3);
}
} // namespace

// --- Round 190jb: exports with the live plugin instances (LiveExportHold) ----------------------------------------------

namespace
{
std::weak_ptr<LiveExportHold>& openHold()
{
    static std::weak_ptr<LiveExportHold> hold; // (message thread)
    return hold;
}

bool songHostsPlugins (const Song& song)
{
    for (const auto& c : song.channels)
        if (c.instrument == InstrumentKind::plugin || c.instrument == InstrumentKind::patcher)
            return true;
    for (const auto& m : song.mixer)
        for (const auto& fx : m.effects)
            if (fx.kind == EffectKind::plugin || fx.kind == EffectKind::patcher)
                return true;
    return false;
}

// the hosted plugins inside a Patcher's graph (and its Patchers'), with their adapters' owners
void hostedInGraph (patcher::Graph* graph, std::vector<std::pair<std::shared_ptr<void>, std::pair<PluginInstrument*, PluginEffect*>>>& out)
{
    if (graph == nullptr)
        return;
    for (int n = 0; n < graph->nodeCount(); ++n)
    {
        auto effect = graph->effectAt (n);
        auto instrument = graph->instrumentAt (n);
        if (auto* p = dynamic_cast<PluginEffect*> (effect.get()))
            out.push_back ({ effect, { nullptr, p } });
        if (auto* p = dynamic_cast<PluginInstrument*> (instrument.get()))
            out.push_back ({ instrument, { p, nullptr } });
        hostedInGraph (patcher::graphOf (effect.get()), out);
        hostedInGraph (patcher::graphOf (instrument.get()), out);
    }
}
} // namespace

std::shared_ptr<LiveExportHold> LiveExportHold::current()
{
    return openHold().lock();
}

std::shared_ptr<LiveExportHold> LiveExportHold::open (AudioEngine& live)
{
    if (auto h = current(); h != nullptr)
        return &h->live == &live ? h : nullptr;
    // (nothing to lend: the export goes as before, the live engine's own silence during renders included)
    if (! songHostsPlugins (live.getSong()) || ! live.holdForExport())
        return nullptr;
    std::shared_ptr<LiveExportHold> h (new LiveExportHold (live));
    openHold() = h;
    return h;
}

bool LiveExportHold::claim() noexcept
{
    auto expected = false;
    return claimed.compare_exchange_strong (expected, true);
}

void LiveExportHold::unclaim() noexcept
{
    claimed.store (false);
}

void LiveExportHold::note (const std::shared_ptr<void>& keep, PluginInstrument* instrument, PluginEffect* effect)
{
    auto* plugin = instrument != nullptr ? &instrument->getInstance() : &effect->getInstance();
    for (const auto& u : units)
        if (u.plugin == plugin)
            return; // (lent before: its state before the first export is the one to restore)
    Unit u;
    u.keep = keep;
    u.plugin = plugin;
    u.instrument = instrument;
    u.effect = effect;
    u.rate = plugin->getSampleRate();
    u.state = instrument != nullptr ? instrument->captureState() : effect->captureState();
    units.push_back (std::move (u));
}

bool LiveExportHold::fitRate (double sampleRate, PluginInstrument* instrument, PluginEffect* effect)
{
    auto& plugin = instrument != nullptr ? instrument->getInstance() : effect->getInstance();
    if (plugin.getSampleRate() == sampleRate)
        return true;
    // (LYNX's own extra: an export at another rate than the device's; FL renders at the device's. The running instance
    // is prepared at the export's rate and back afterwards; one that does not take it is not lent)
    const auto before = plugin.getSampleRate();
    if (instrument != nullptr ? instrument->changeRate (sampleRate) : effect->changeRate (sampleRate))
        return true;
    if (instrument != nullptr)
        instrument->changeRate (before);
    else
        effect->changeRate (before);
    return false;
}

std::shared_ptr<Instrument> LiveExportHold::lendInstrument (const ChannelDef& channel, double sampleRate)
{
    const auto& song = live.getSong();
    auto at = song.channels.size();
    for (std::size_t i = 0; i < song.channels.size() && at == song.channels.size(); ++i)
        if (song.channels[i].instance.id == channel.instance.id)
            at = i;
    if (at == song.channels.size() || song.channels[at].instrument != channel.instrument)
    {
        refusal = "channel " + juce::String::fromUTF8 (channel.name.c_str()) + ": not in the live project";
        return nullptr;
    }
    const auto& mine = song.channels[at];
    auto unit = live.getChannelInstrument ((int) at);
    // round 190kx: an audio clip channel edited in an ARA plugin renders with the live instance (its edits as they are now)
    if (channel.instrument == InstrumentKind::audioClip && channel.ara.active())
    {
        if (! AraHost::isAraInstrument (unit.get()) || ara::effectiveLink (song, (int) at) != channel.ara // (round 190oq: or its ARA insert's)
            || live.getTransport().sampleRate.load() != sampleRate)
        {
            refusal = "channel " + juce::String::fromUTF8 (channel.name.c_str()) + ": ARA plugin not lent";
            return nullptr;
        }
        return unit;
    }
    if (channel.instrument == InstrumentKind::plugin)
    {
        auto* p = dynamic_cast<PluginInstrument*> (unit.get());
        if (p == nullptr || p->hasCrashed() || mine.plugin.id != channel.plugin.id)
        {
            refusal = "channel " + juce::String::fromUTF8 (channel.name.c_str()) + ": no running plugin";
            return nullptr;
        }
        note (unit, p, nullptr); // (its state and rate before the export, before fitRate changes the rate)
        if (! fitRate (sampleRate, p, nullptr))
        {
            refusal = "channel " + juce::String::fromUTF8 (channel.name.c_str()) + ": the plugin does not take " + juce::String (sampleRate) + " Hz";
            return nullptr;
        }
        return unit;
    }
    // a Patcher: lent whole when it hosts plugins, has the same structure and runs at the export's rate
    std::vector<std::pair<std::shared_ptr<void>, std::pair<PluginInstrument*, PluginEffect*>>> inside;
    hostedInGraph (patcher::graphOf (unit.get()), inside);
    if (inside.empty() || ! patcher::sameStructure (mine.patcher, channel.patcher) || live.getTransport().sampleRate.load() != sampleRate)
    {
        refusal = "channel " + juce::String::fromUTF8 (channel.name.c_str()) + ": Patcher not lent";
        return nullptr;
    }
    for (const auto& [keep, adapter] : inside)
        note (keep, adapter.first, adapter.second);
    return unit;
}

std::shared_ptr<Effect> LiveExportHold::lendEffect (const EffectSlot& slot, double sampleRate)
{
    const auto& song = live.getSong();
    int track = -1, index = -1;
    for (std::size_t t = 0; t < song.mixer.size() && track < 0; ++t)
        for (std::size_t i = 0; i < song.mixer[t].effects.size(); ++i)
            if (song.mixer[t].effects[i].instance.id == slot.instance.id)
            {
                track = (int) t;
                index = (int) i;
                break;
            }
    if (track < 0 || song.mixer[(std::size_t) track].effects[(std::size_t) index].kind != slot.kind)
    {
        refusal = "effect " + juce::String::fromUTF8 (slot.plugin.name.c_str()) + ": not in the live project";
        return nullptr;
    }
    const auto& mine = song.mixer[(std::size_t) track].effects[(std::size_t) index];
    auto unit = live.getEffect (track, index);
    if (slot.kind == EffectKind::plugin)
    {
        auto* p = dynamic_cast<PluginEffect*> (unit.get());
        if (p == nullptr || p->hasCrashed() || mine.plugin.id != slot.plugin.id)
        {
            refusal = "effect " + juce::String::fromUTF8 (slot.plugin.name.c_str()) + ": no running plugin";
            return nullptr;
        }
        note (unit, nullptr, p);
        if (! fitRate (sampleRate, nullptr, p))
        {
            refusal = "effect " + juce::String::fromUTF8 (slot.plugin.name.c_str()) + ": the plugin does not take " + juce::String (sampleRate) + " Hz";
            return nullptr;
        }
        return unit;
    }
    std::vector<std::pair<std::shared_ptr<void>, std::pair<PluginInstrument*, PluginEffect*>>> inside;
    hostedInGraph (patcher::graphOf (unit.get()), inside);
    if (inside.empty() || ! patcher::sameStructure (mine.patcher, slot.patcher) || live.getTransport().sampleRate.load() != sampleRate)
    {
        refusal = "effect slot: Patcher not lent";
        return nullptr;
    }
    for (const auto& [keep, adapter] : inside)
        note (keep, adapter.first, adapter.second);
    return unit;
}

LiveExportHold::~LiveExportHold()
{
    // every plugin lent goes back as it was: its rate, its real-time mode, its state (the export's automation and its
    // start values moved its parameters), reset as FL resets a plugin ("Reset plugin when FL Studio resets"), and the
    // live engine plays again. Message thread (the jobs and their holders are deleted there).
    auto restore = [units = std::move (units), engine = &live]() mutable {
        for (auto& u : units)
        {
            auto* in = u.instrument;
            auto* fx = u.effect;
            if (in != nullptr ? in->hasCrashed() : fx->hasCrashed())
                continue;
            if (u.plugin->getSampleRate() != u.rate)
                in != nullptr ? (void) in->changeRate (u.rate) : (void) fx->changeRate (u.rate);
            if (u.plugin->isNonRealtime())
                in != nullptr ? in->setRenderMode (false) : fx->setRenderMode (false);
            if (u.state.has_value())
                if (const auto now = in != nullptr ? in->captureState() : fx->captureState(); now.has_value() && *now != *u.state)
                    in != nullptr ? (void) in->restoreState (*u.state) : (void) fx->restoreState (*u.state);
            in != nullptr ? in->resetForExport() : fx->resetForExport();
        }
        units.clear();
        engine->releaseAfterExport();
    };
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread())
        restore();
    else
        juce::MessageManager::callAsync (std::move (restore));
}

int ExportJob::livePlugins() const noexcept
{
    return hold != nullptr && engine != nullptr ? hold->lentPlugins() : 0;
}

double exportMusicSeconds (const Song& song, const Options& options)
{
    std::int64_t musicTicks = 0, loopTicks = 0;
    const auto copy = makeRenderSong (song, options, 0, musicTicks, loopTicks);
    return copy.secondsForTicks (musicTicks);
}

ExportJob::ExportJob (const Song& song, const ExportSettings& s, const juce::File& file)
    : ExportJob (song, s.toOptions(), file)
{
}

ExportJob::ExportJob (const Song& project, const Options& o, const juce::File& file)
    : engine (std::make_unique<AudioEngine> (false)),
      options (clamped (o)),
      output (file),
      title (project.info.title),
      artist (project.info.author)
{
    // Freeze (round 190ay): a frozen channel whose audio is out of date renders live — never the old audio in a bounce
    // (and where the render does not play the song's timeline as it is, every frozen channel does)
    const auto timelineKept = options.range == Range::song && options.loops <= 1 && ! project.playlist.empty();
    const auto song = sessionpack::freshForExport (project, timelineKept, &thawedForExport);
    // duration integrating tempo automation; the tail at the tempo of the song's end
    std::int64_t musicTicks = 0, loopTicks = 0;
    const auto tailTicks = tailTicksFor (song, options, musicTicks, loopTicks);
    auto copy = makeRenderSong (song, options, tailTicks, musicTicks, loopTicks);
    musicSeconds = copy.secondsForTicks (musicTicks);
    loopSeconds = copy.secondsForTicks (loopTicks);
    extras = wavExtras (song, options, options.sampleRate); // FL's extra data (markers, loop, tempo) for WAV files

    // rate before setSong: instruments and plugins are created already prepared for it
    engine->getTransport().sampleRate.store (options.sampleRate);
    // round 190jb: with the Export window's hold open, the render plays the live engine's running plugins (FL)
    if (juce::MessageManager::existsAndIsCurrentThread())
        if (auto h = LiveExportHold::current(); h != nullptr && h->claim())
        {
            hold = std::move (h);
            auto* lender = hold.get();
            engine->setLiveUnits ([lender] (const ChannelDef& c, double rate) { return lender->lendInstrument (c, rate); },
                                  [lender] (const EffectSlot& e, double rate) { return lender->lendEffect (e, rate); });
        }
    engine->setParallelSampleDecode (false); // (round 190la, QA-10: the song's samples are decoded already)
    // round 190my (DF-82; REA 190my R1, live check 190my A2): FL's render runs the same per-track process as playback,
    // with the Current track on the Mixer's selection — and its render window selects the Master when it opens (FormShow
    // FUN_00c51750 → FUN_00c4d0b0; the Mixer title turned "Mixer - Master", Fruity Balance at -inf on C silenced every
    // export whatever was selected before). So a render plays C on the Master's output.
    if (mixerops::currentTrack (copy) > 0)
        engine->setCurrentTrackSource (0);
    engine->setSong (std::move (copy));
}

ExportJob::ExportJob (const Song& project, const Options& o, const juce::File& file, NoEngine)
    : options (clamped (o)),
      output (file),
      title (project.info.title),
      artist (project.info.author)
{
    // what the constructor above works out, without the engine (round 190gj: a file of a StemPass)
    const auto timelineKept = options.range == Range::song && options.loops <= 1 && ! project.playlist.empty();
    const auto song = sessionpack::freshForExport (project, timelineKept, &thawedForExport);
    std::int64_t musicTicks = 0, loopTicks = 0;
    const auto tailTicks = tailTicksFor (song, options, musicTicks, loopTicks);
    const auto copy = makeRenderSong (song, options, tailTicks, musicTicks, loopTicks);
    musicSeconds = copy.secondsForTicks (musicTicks);
    loopSeconds = copy.secondsForTicks (loopTicks);
    extras = wavExtras (song, options, options.sampleRate);
}

ExportJob::ExportJob (AudioEngine& liveEngine, const Options& o, const juce::File& file)
    : options (clamped (realtimeOptions (o, liveEngine.getTransport().sampleRate.load()))),
      output (file),
      title (liveEngine.getSong().info.title),
      artist (liveEngine.getSong().info.author)
{
    live = &liveEngine;
    const auto& song = liveEngine.getSong();
    std::int64_t musicTicks = 0, loopTicks = 0;
    const auto copy = makeRenderSong (song, options, 0, musicTicks, loopTicks);
    musicSeconds = copy.secondsForTicks (musicTicks);
    loopSeconds = copy.secondsForTicks (loopTicks);
    extras = wavExtras (song, options, options.sampleRate);
    // where the range starts in the song (the selection: the loop region, as makeRenderSong cuts it)
    startTick = options.range == Range::selection && song.hasLoop() ? (double) song.loopStart : 0.0;
}

Options ExportJob::realtimeOptions (Options o, double deviceRate)
{
    if (deviceRate > 0.0)
        o.sampleRate = deviceRate; // (the device plays at its own rate)
    if (o.range == Range::pattern)
        o.range = Range::song;
    o.loops = 1;
    o.stems = Stems::none;
    o.masterEffects = true;
    o.insertEffects = true;
    o.hqAllPlugins = false;  // (the playback quality: what you hear)
    o.pluginWarmUp = false;  // (the plugins are already running)
    o.disableMaxPolyphony = false;
    return o;
}

bool ExportJob::runRealtime (const std::function<bool (double)>& progress, juce::String& error)
{
    if (! formatsWritable (options, error))
        return false;
    for (const auto f : formatsOf (options))
        if (const auto file = f == options.format ? output : output.withFileExtension (extension (f)); ! canReplace (file))
        {
            error = inUseMessage (file);
            return false;
        }

    auto onMessageThread = [] (std::function<void()> f) {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        if (mm == nullptr || mm->isThisTheMessageThread())
            f();
        else
            mm->callFunctionOnMessageThread ([] (void* p) -> void* { (*static_cast<std::function<void()>*> (p))(); return nullptr; }, &f);
    };

    juce::String started;
    onMessageThread ([this, &started] { started = live->startRealtimeBounce (startTick); });
    if (started.isNotEmpty())
    {
        error = started;
        return false;
    }

    const auto rate = options.sampleRate;
    const auto latency = (std::int64_t) live->bounceLatency();
    const auto trim = options.trimPdc ? latency : 0;
    const auto music = (std::int64_t) std::llround (musicSeconds * rate);
    const auto maxTail = options.tail == TailMode::cut ? 0 : (std::int64_t) (options.maxTailSeconds * rate);
    const auto expected = latency + music + maxTail;
    std::vector<float> left, right;
    left.reserve ((std::size_t) expected + 65536);
    right.reserve ((std::size_t) expected + 65536);

    constexpr float silence = 3.0e-5f; // ~ -90 dB, as the offline render's tail
    std::int64_t quiet = 0, checked = 0;
    auto cancelled = false, stopped = false;
    for (;;)
    {
        juce::Thread::sleep (40);
        live->pullRealtimeBounce (left, right);
        const auto frames = (std::int64_t) left.size();
        // the tail: half a second of silence after the music ends it (as the offline render)
        for (; checked < frames; ++checked)
            if (checked >= latency + music)
                quiet = std::abs (left[(std::size_t) checked]) < silence && std::abs (right[(std::size_t) checked]) < silence ? quiet + 1 : 0;
        const auto tailDone = frames >= latency + music && (maxTail == 0 || quiet > (std::int64_t) (rate * 0.5) || frames >= expected);
        if (tailDone)
            break;
        if (progress && ! progress (juce::jlimit (0.0, 1.0, (double) frames / (double) juce::jmax<std::int64_t> (1, expected))))
        {
            cancelled = true;
            break;
        }
        if (! live->bounceStillPlaying())
        {
            stopped = true;
            break;
        }
    }
    const auto dropouts = live->bounceDropouts();
    onMessageThread ([this] { live->finishRealtimeBounce(); });

    if (cancelled)
    {
        error = "cancelled";
        return false;
    }
    if (stopped)
    {
        error = tr ("Playback was stopped during the real-time export");
        return false;
    }

    // keep: the latency's silence trimmed, the music, and the tail as far as it went
    auto end = std::min<std::int64_t> ((std::int64_t) left.size(), maxTail == 0 ? latency + music : expected);
    left.resize ((std::size_t) end);
    right.resize ((std::size_t) end);
    left.erase (left.begin(), left.begin() + (std::ptrdiff_t) std::min<std::int64_t> (trim, end));
    right.erase (right.begin(), right.begin() + (std::ptrdiff_t) std::min<std::int64_t> (trim, end));
    if (dropouts > 0)
        notice = tr ("{0} audio dropout(s) happened during the real-time export: listen to the file (a larger buffer size helps)",
                     { juce::String (dropouts) });
    if (progress)
        progress (1.0);
    return writeBuffered (left, right, error);
}

ExportJob::~ExportJob()
{
    // (round 190jb: the engine, which holds the lent units, goes first; then the hold may give them back)
    engine.reset();
    if (hold != nullptr)
        hold->unclaim();
}

struct ExportJob::Sink
{
    AudioEngine::RenderOptions render;
    bool streaming = false;
    std::unique_ptr<juce::TemporaryFile> temp;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::vector<float> left, right;
};

bool ExportJob::run (const std::function<bool (double)>& progress, juce::String& error)
{
    // rollup 0.1.28 (REA 190hb action 3, FL's rule): the PC does not sleep during an export, and an offline render's
    // thread runs below normal priority so the desktop stays usable (its workers do too: AudioEngine's offline pool)
    const powerhints::KeepAwake awake;
    if (live != nullptr)
        return runRealtime (progress, error);
    const powerhints::BelowNormalPriority background;

    Sink sink;
    if (! openSink (sink, error))
        return false;
    sink.render.progress = progress;
    const auto rendered = engine->renderOffline (sink.render);
    return closeSink (sink, rendered, error);
}

bool ExportJob::openSink (Sink& sink, juce::String& error)
{
    auto& render = sink.render;
    render.seconds = musicSeconds;
    render.sampleRate = options.sampleRate;
    render.tailSeconds = tailSecondsFor (options);
    render.trimPdc = options.trimPdc; // the latency as reported after the plugins' warm-up
    render.hqAllPlugins = options.hqAllPlugins;
    render.pluginWarmUp = options.pluginWarmUp;
    render.exactSamples = exactFrames; // (round 190gh: every stem as long as the mix)
    writtenFrames = 0;

    if (! formatsWritable (options, error))
        return false;
    for (const auto f : formatsOf (options))
        if (const auto file = f == options.format ? output : output.withFileExtension (extension (f)); ! canReplace (file))
        {
            error = inUseMessage (file);
            return false;
        }

    // streaming straight into the file when nothing needs the whole render first (the classic WAV/FLAC/OGG export)
    // (MP3 and M4A encode the whole render at the end: their encoders' rates may differ from the render's)
    sink.streaming = options.format != exportopts::Format::mp3 && options.format != exportopts::Format::aac && options.channels == Channels::stereo && ! options.normalize && ! options.master
                     && options.tail == TailMode::leave && formatsOf (options).size() == 1;

    if (! sink.streaming)
    {
        const auto expected = (std::size_t) ((musicSeconds + render.tailSeconds) * options.sampleRate) + 4096;
        sink.left.reserve (expected);
        sink.right.reserve (expected);
        render.keepOvers = true; // normalising comes after; each file's writer / dither clamps what still goes over
        render.capture = [&sink] (const float* l, const float* r, int n) {
            sink.left.insert (sink.left.end(), l, l + n);
            sink.right.insert (sink.right.end(), r, r + n);
        };
        return true;
    }

    std::unique_ptr<juce::AudioFormat> format;
    int quality = 0;
    const auto bits = fileBits (options);

    if (options.format == exportopts::Format::flac)
    {
        format = std::make_unique<juce::FlacAudioFormat>();
        quality = options.flacLevel;
    }
    else if (options.format == exportopts::Format::ogg)
    {
        format = std::make_unique<juce::OggVorbisAudioFormat>();
        quality = oggQualityIndex (*format, options.oggKbps);
    }
    else if (options.format == exportopts::Format::aiff)
        format = std::make_unique<juce::AiffAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    sink.temp = std::make_unique<juce::TemporaryFile> (output);
    auto stream = sink.temp->getFile().createOutputStream();

    if (stream == nullptr)
    {
        error = tr ("could not create {0}", { output.getFullPathName() });
        return false;
    }

    sink.writer.reset (format->createWriterFor (stream.get(), options.sampleRate, 2, bits, {}, quality));

    if (sink.writer == nullptr)
    {
        error = utf8 ("the format does not accept these settings");
        return false;
    }

    stream.release(); // now owned by the writer
    render.output = sink.writer.get();
    render.capture = [this] (const float*, const float*, int n) { writtenFrames += n; };
    render.ditherBits = usesDither (options) ? bits : 0;
    render.keepOvers = options.format == exportopts::Format::wav && bits == 32; // 32-bit float WAV keeps overs, as FL
    render.ditherShaping = options.dither == DitherMode::shaped;
    return true;
}

bool ExportJob::closeSink (Sink& sink, float renderedPeak, juce::String& error)
{
    if (! sink.streaming)
    {
        if (renderedPeak < 0.0f)
        {
            error = "cancelled";
            return false;
        }
        return writeBuffered (sink.left, sink.right, error);
    }

    peak = renderedPeak;
    sink.writer.reset(); // closes the file (the WAV header is finalized here)

    if (renderedPeak < 0.0f)
    {
        error = "cancelled"; // (the temporary file goes away; an older file of that name stays)
        return false;
    }
    if (extraData && options.format == exportopts::Format::wav && ! appendWavExtras (sink.temp->getFile().getFullPathName().toStdString(), extras, options.sampleRate))
    {
        error = tr ("could not write {0}", { output.getFullPathName() });
        return false;
    }

    return replaceTarget (*sink.temp, output, error);
}

bool ExportJob::writeBuffered (std::vector<float>& left, std::vector<float>& right, juce::String& error)
{
    // tail, then normalising (the peak of what is kept), then mono; dither per file (each format has its own bits)
    applyTail (left, right, options.tail == TailMode::leave ? 0 : (std::int64_t) std::llround (musicSeconds * options.sampleRate), options.tail);
    if (exactFrames > 0 && options.tail == TailMode::leave) // (round 190gh: a stem as long as the mix)
    {
        left.resize ((std::size_t) exactFrames, 0.0f);
        right.resize ((std::size_t) exactFrames, 0.0f);
    }
    writtenFrames = (std::int64_t) left.size();

    if (options.master) // round 168e: File › Export › Master... (LYNX's own mastering, core/Mastering.h)
    {
        mastering::Settings m;
        m.targetLufs = options.masterLufs;
        const auto r = mastering::process (left, right, options.sampleRate, m);
        info = tr ("Mastered: {0} LUFS → {1} LUFS, true peak {2} dBTP",
                     { juce::String (r.inputLufs, 1), juce::String (r.outputLufs, 1), juce::String (r.outputTruePeakDb, 1) });
    }

    const auto frames = (int) left.size();
    if (options.normalize && ! options.master)
    {
        float p = 0.0f;
        for (int i = 0; i < frames; ++i)
            p = juce::jmax (p, std::abs (left[(std::size_t) i]), std::abs (right[(std::size_t) i]));
        const auto gain = normalizeGain (p);
        juce::FloatVectorOperations::multiply (left.data(), gain, frames);
        juce::FloatVectorOperations::multiply (right.data(), gain, frames);
    }

    const auto channels = channelCount (options.channels);
    toChannels (left.data(), right.data(), frames, options.channels);

    // FL writes every ticked format from the one render (REA round 139): the first file is `output`, the others the
    // same name with their extension
    peak = 0.0f;
    for (const auto f : formatsOf (options))
    {
        auto o = options;
        o.format = f;
        const auto file = f == options.format ? output : output.withFileExtension (extension (f));
        std::vector<float> dl, dr;
        float* l = left.data();
        float* r = right.data();
        if (usesDither (o))
        {
            dl = left;
            dr = right;
            l = dl.data();
            r = dr.data();
            Dither dither (fileBits (o), o.dither == DitherMode::shaped);
            constexpr int block = 512; // the same block order as a streamed render
            for (int done = 0; done < frames; done += block)
            {
                const auto n = juce::jmin (block, frames - done);
                dither.process (l + done, n, 0);
                if (channels == 2)
                    dither.process (r + done, n, 1);
            }
        }

        for (int i = 0; i < frames; ++i)
            peak = juce::jmax (peak, std::abs (l[i]), channels == 2 ? std::abs (r[i]) : 0.0f);

        if (! writeFile (o, file, l, r, frames, channels, error))
            return false;
    }
    return true;
}

bool ExportJob::writeFile (const Options& o, const juce::File& file, float* left, float* right, int frames, int channels, juce::String& error)
{
    juce::TemporaryFile temp (file);

    if (o.format == exportopts::Format::mp3)
    {
        Mp3Encoder encoder;
        std::string message;
        if (! encoder.open ((int) o.sampleRate, (int) mp3SampleRate (o.sampleRate), channels, o.mp3Kbps, message, title, artist))
        {
            error = utf8 (message.c_str());
            return false;
        }

        std::vector<std::uint8_t> bytes;
        bytes.reserve ((std::size_t) (frames / o.sampleRate * o.mp3Kbps * 125.0) + 65536);
        constexpr int block = 8192;
        for (int done = 0; done < frames; done += block)
            encoder.encode (left + done, right + done, juce::jmin (block, frames - done), bytes);
        encoder.finish (bytes);

        if (! temp.getFile().replaceWithData (bytes.data(), bytes.size()))
        {
            error = tr ("could not create {0}", { file.getFullPathName() });
            return false;
        }
        return replaceTarget (temp, file, error);
    }

    if (o.format == exportopts::Format::aac)
    {
        // M4A (owner decision D5): the system's AAC encoder (engine/AacEncoder.h) at 44.1 / 48 kHz — a render at
        // 88.2 / 96 / 176.4 / 192 kHz is brought down by halving (exportopts::decimate)
        if (! aac::available())
        {
            error = utf8 (aac::unavailableReason().c_str());
            return false;
        }
        const auto factor = aacDecimation (o.sampleRate);
        if (factor <= 0)
        {
            error = tr ("M4A files take 44100 or 48000 Hz (or a multiple of it); this export is at {0} Hz", { juce::String ((int) o.sampleRate) });
            return false;
        }
        std::vector<float> dl, dr;
        const float* l = left;
        const float* r = channels == 2 ? right : left;
        auto n = (long long) frames;
        if (factor > 1)
        {
            dl = decimate (std::vector<float> (left, left + frames), o.sampleRate, factor);
            if (channels == 2)
                dr = decimate (std::vector<float> (right, right + frames), o.sampleRate, factor);
            l = dl.data();
            r = channels == 2 ? dr.data() : l;
            n = (long long) dl.size();
        }
        std::string message;
        if (! aac::encode (temp.getFile().getFullPathName().toStdString(), l, r, n, channels, (int) aacSampleRate (o.sampleRate), o.aacKbps, message))
        {
            error = utf8 (message.c_str());
            return false;
        }
        return replaceTarget (temp, file, error);
    }

    std::unique_ptr<juce::AudioFormat> format;
    int quality = 0;
    if (o.format == exportopts::Format::flac)
    {
        format = std::make_unique<juce::FlacAudioFormat>();
        quality = o.flacLevel;
    }
    else if (o.format == exportopts::Format::ogg)
    {
        format = std::make_unique<juce::OggVorbisAudioFormat>();
        quality = oggQualityIndex (*format, o.oggKbps);
    }
    else if (o.format == exportopts::Format::aiff)
        format = std::make_unique<juce::AiffAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    auto stream = temp.getFile().createOutputStream();
    if (stream == nullptr)
    {
        error = tr ("could not create {0}", { file.getFullPathName() });
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        format->createWriterFor (stream.get(), o.sampleRate, (unsigned int) channels, fileBits (o), {}, quality));
    if (writer == nullptr)
    {
        error = utf8 ("the format does not accept these settings");
        return false;
    }
    stream.release();

    float* data[2] = { left, right };
    juce::AudioBuffer<float> view (data, channels, frames);
    const auto ok = writer->writeFromAudioSampleBuffer (view, 0, frames);
    writer.reset();

    if (! ok || (extraData && o.format == exportopts::Format::wav && ! appendWavExtras (temp.getFile().getFullPathName().toStdString(), extras, o.sampleRate)))
    {
        error = tr ("could not write {0}", { file.getFullPathName() });
        return false;
    }
    return replaceTarget (temp, file, error);
}
// --- "Split mixer tracks" in one render (round 190gj) ----------------------------------------------------------------

struct StemPass::Out
{
    Target target;
    std::unique_ptr<ExportJob> job; // the file's writing (no engine of its own)
    exportopts::StemTap plan;       // (a stem)
    std::unique_ptr<exportopts::StemMixer> mixer;
    std::unique_ptr<ExportJob::Sink> sink;
    std::unique_ptr<RenderStream> stream;
    Result result = Result::separate;
    juce::String error, why;
    bool inPass = false; // made by this render (until it turns out it cannot be)
    bool pending = false; // its length is the mix's, found during the render (round 190gh)
    float peak = 0.0f;

    // this file leaves the pass: its own render makes it
    void leave (const juce::String& reason)
    {
        inPass = false;
        result = Result::separate;
        why = reason;
        if (sink != nullptr)
        {
            sink->writer.reset();
            sink.reset(); // (the temporary file goes)
        }
        stream.reset();
    }
};

struct StemPass::Shared final : AudioEngine::RenderOptions::Shared
{
    Shared (StemPass& p, AudioEngine& e, std::function<bool (double)> progressFn) : pass (p), engine (e), progress (std::move (progressFn)) {}

    void start (int latency) override
    {
        std::vector<int> channelLatency;
        std::vector<std::vector<int>> slotLatency;
        engine.getPublishedLatencies (channelLatency, slotLatency);
        const auto& song = engine.getSong(); // the song this render plays
        const auto tracks = std::max<int> (1, (int) song.mixer.size());
        taps.capacity = AudioEngine::renderBlockSize;
        taps.wanted.assign ((std::size_t) tracks, 0);
        taps.data.assign ((std::size_t) tracks, {});

        // hosted plugins see the size of the blocks they are given; LYNX's own units do not (round 190gh: a render is
        // the same in blocks of any size)
        for (const auto& c : song.channels)
            hosted = hosted || c.instrument == InstrumentKind::plugin || c.instrument == InstrumentKind::patcher;
        for (const auto& m : song.mixer)
            for (const auto& fx : m.effects)
                hosted = hosted || fx.kind == EffectKind::plugin || fx.kind == EffectKind::patcher;

        // round 190gh: the stems get the mix's length; not known yet, it is measured on this render's own mix (as the
        // mix file's own render would end), and the stems keep back what may lie past it until then
        AudioEngine::RenderOptions mix;
        mix.seconds = pass.main->musicSeconds;
        mix.sampleRate = pass.options.sampleRate;
        mix.tailSeconds = tailSecondsFor (pass.options);
        mix.trimPdc = pass.options.trimPdc;
        const auto measuring = pass.sameLength && pass.mixLength < 0;
        if (measuring)
        {
            measure = std::make_unique<RenderStream> (AudioEngine::streamSetup (mix, latency));
            longest = measure->getSetup().totalSamples;
        }
        const auto mixTrim = AudioEngine::streamSetup (mix, latency).trim;

        for (auto& o : pass.outs)
        {
            if (! o->inPass)
                continue;
            auto stemLatency = latency;
            if (o->target.track > 0)
            {
                o->plan = exportopts::planStemTap (song, o->target.track, channelLatency, slotLatency, pass.options.sampleRate);
                if (! o->plan.ok)
                {
                    o->leave (juce::String::fromUTF8 (o->plan.why.c_str()));
                    continue;
                }
                stemLatency = o->plan.latency;
            }
            o->sink = std::make_unique<ExportJob::Sink>();
            juce::String error;
            if (! o->job->openSink (*o->sink, error))
            {
                o->leave ({});
                o->result = Result::failed;
                o->error = error;
                continue;
            }
            auto setup = AudioEngine::streamSetup (o->sink->render, stemLatency);
            o->pending = measuring && o->target.track > 0;
            if (o->pending)
                setup.holdBack = std::max<std::int64_t> (0, mixTrim - setup.trim) + AudioEngine::renderBlockSize;
            o->stream = std::make_unique<RenderStream> (std::move (setup));
            longest = std::max (longest, o->stream->getSetup().totalSamples);
            if (o->target.track > 0)
            {
                o->mixer = std::make_unique<exportopts::StemMixer> (o->plan);
                taps.wanted[0] = 1; // the Master's own channels
                for (const auto& in : o->plan.inputs)
                    taps.wanted[(std::size_t) in.track] = 1;
            }
        }
        for (std::size_t t = 0; t < taps.wanted.size(); ++t)
            if (taps.wanted[t] != 0)
                taps.data[t].assign ((std::size_t) taps.capacity * 2, 0.0f);
        outL.assign ((std::size_t) taps.capacity, 0.0f);
        outR.assign ((std::size_t) taps.capacity, 0.0f);
        scratch.setSize (2, taps.capacity);
        engine.setRenderTaps (&taps);
    }

    int nextBlock (std::int64_t done) override
    {
        if (cancelled)
            return 0;
        int most = measure != nullptr ? measure->blockAt (done, AudioEngine::renderBlockSize) : 0, least = most;
        for (auto& o : pass.outs)
            if (o->inPass && o->stream != nullptr)
                if (const auto n = o->stream->blockAt (done, AudioEngine::renderBlockSize); n > 0)
                {
                    most = std::max (most, n);
                    least = least == 0 ? n : std::min (least, n);
                }
        // a file whose own render ends inside a block that the others still need whole: its render's last block is
        // shorter. LYNX's own units make the same samples in it (round 190gh); hosted plugins may not: with them, that
        // file gets its own render
        if (least != most && hosted)
            for (auto& o : pass.outs)
                if (o->inPass && o->stream != nullptr && o->stream->blockAt (done, AudioEngine::renderBlockSize) != most
                    && o->stream->blockAt (done, AudioEngine::renderBlockSize) > 0)
                    o->leave ("its render ends inside a block");
        return most;
    }

    void block (const juce::AudioBuffer<float>& raw, std::int64_t done, int n) override
    {
        std::vector<const float*> left, right;
        for (auto& o : pass.outs)
        {
            if (! o->inPass || o->stream == nullptr || o->stream->blockAt (done, AudioEngine::renderBlockSize) <= 0)
                continue;
            if (o->target.track == 0)
            {
                // the song's own mix: the graph's output as it is
                for (int ch = 0; ch < 2; ++ch)
                    scratch.copyFrom (ch, 0, raw, ch, 0, n);
                juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 2, n);
                o->stream->block (view, done, n);
                continue;
            }
            left.clear();
            right.clear();
            for (const auto& in : o->plan.inputs)
            {
                left.push_back (taps.left (in.track));
                right.push_back (taps.right (in.track));
            }
            o->mixer->mix (taps.left (0), taps.right (0), left.data(), right.data(), outL.data(), outR.data(), n);
            // what the stem render's neutral Master and the graph do after: a gain of exactly 1, and the preview's
            // silence added at the graph's output (which turns −0 into +0)
            for (int i = 0; i < n; ++i)
            {
                outL[(std::size_t) i] += 0.0f;
                outR[(std::size_t) i] += 0.0f;
            }
            float* channels[2] = { outL.data(), outR.data() };
            juce::AudioBuffer<float> view (channels, 2, n);
            o->stream->block (view, done, n);
        }
        if (measure != nullptr && measure->blockAt (done, AudioEngine::renderBlockSize) > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
                scratch.copyFrom (ch, 0, raw, ch, 0, n);
            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 2, n);
            measure->block (view, done, n);
            if (measure->hasEnded())
                found (measure->keptFrames());
        }
        for (std::size_t t = 0; t < taps.wanted.size(); ++t)
            if (taps.wanted[t] != 0)
                std::fill (taps.data[t].begin(), taps.data[t].end(), 0.0f);

        if (progress && (++blocks % 32) == 0 && ! progress ((double) done / (double) std::max<std::int64_t> (1, longest)))
            cancelled = true;
    }

    // the mix's length is known: the stems end there (a silent mix: as before round 190gh, its own render each)
    void found (std::int64_t frames)
    {
        pass.mixLength = frames;
        measure.reset();
        for (auto& o : pass.outs)
        {
            if (! o->inPass || ! o->pending)
                continue;
            o->pending = false;
            if (frames <= 0)
            {
                o->leave ("a silent mix");
                continue;
            }
            o->job->setExactFrames (frames);
            o->stream->setTotal (o->stream->getSetup().trim + frames);
        }
    }

    StemPass& pass;
    AudioEngine& engine;
    std::function<bool (double)> progress;
    std::unique_ptr<RenderStream> measure; // the mix, measured for its length (round 190gh)
    bool hosted = false;
    RackRenderer::RenderTaps taps;
    juce::AudioBuffer<float> scratch;
    std::vector<float> outL, outR;
    std::int64_t longest = 0;
    int blocks = 0;
    bool cancelled = false;
};

StemPass::StemPass (const Song& song, const Options& o, const std::vector<Target>& targets, bool sameLengthFiles, std::int64_t knownLength)
    : options (clamped (o)),
      sameLength (sameLengthFiles),
      mixLength (sameLengthFiles ? knownLength : -1)
{
    // the one render plays the song with its solos cleared (each stem render clears them); the song's own mix comes
    // from it only when the song has none
    const auto solo = std::any_of (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& m) { return m.solo; });
    masterInPass = ! solo;
    auto shared = song;
    for (auto& m : shared.mixer)
        m.solo = false;
    main = std::make_unique<ExportJob> (shared, options, targets.empty() ? juce::File() : targets.front().file);

    for (const auto& t : targets)
    {
        auto out = std::make_unique<Out>();
        out->target = t;
        if (t.track > 0 || (t.track == 0 && masterInPass))
        {
            out->job.reset (new ExportJob (t.track > 0 ? makeBounceSong (song, t.track) : song, options, t.file, ExportJob::NoEngine {}));
            out->inPass = true;
            // round 190gh: a stem as long as the mix (until it is measured, as long as the mix can be)
            if (sameLength && t.track > 0)
                out->job->setExactFrames (mixLength >= 0 ? mixLength
                                                         : (std::int64_t) (main->musicSeconds * options.sampleRate)
                                                               + (std::int64_t) (tailSecondsFor (options) * options.sampleRate) + 1);
        }
        else
            out->why = t.track == 0 ? "a solo" : "not an insert";
        outs.push_back (std::move (out));
    }
}

StemPass::~StemPass() = default;

void StemPass::setExtraData (bool on) noexcept
{
    for (auto& o : outs)
        if (o->job != nullptr)
            o->job->setExtraData (on);
}

AudioEngine& StemPass::getEngine() noexcept { return main->getEngine(); }

bool StemPass::run (const std::function<bool (double)>& progress, juce::String& error)
{
    if (std::none_of (outs.begin(), outs.end(), [] (const std::unique_ptr<Out>& o) { return o->inPass; }))
        return true;
    // rollup 0.1.28: the pass is an offline render as ExportJob::run's (REA 190hb action 3): no sleep meanwhile, this
    // thread below normal priority (the pass engine's workers too: an engine without a device)
    const powerhints::KeepAwake awake;
    const powerhints::BelowNormalPriority background;

    for (auto& o : outs) // (as each file's own render checks first)
        if (o->inPass)
            for (const auto f : formatsOf (options))
                if (const auto file = f == options.format ? o->target.file : o->target.file.withFileExtension (extension (f)); ! canReplace (file))
                {
                    o->leave ({});
                    o->result = Result::failed;
                    o->error = inUseMessage (file);
                    break;
                }

    auto& engine = main->getEngine();
    Shared shared (*this, engine, progress);
    AudioEngine::RenderOptions render;
    render.seconds = main->musicSeconds;
    render.sampleRate = options.sampleRate;
    render.progress = progress; // (the plugin warm-up's)
    render.tailSeconds = tailSecondsFor (options);
    render.trimPdc = options.trimPdc;
    render.hqAllPlugins = options.hqAllPlugins;
    render.pluginWarmUp = options.pluginWarmUp;
    render.shared = &shared;
    const auto rendered = engine.renderOffline (render);
    engine.setRenderTaps (nullptr);

    if (rendered < 0.0f || shared.cancelled)
    {
        for (auto& o : outs)
            if (o->inPass)
                o->leave ({});
        error = "cancelled";
        return false;
    }

    for (auto& o : outs)
    {
        if (! o->inPass)
            continue;
        juce::String e;
        if (o->job->closeSink (*o->sink, o->stream->getPeak(), e))
        {
            o->result = Result::written;
            o->peak = o->job->getPeak();
        }
        else
        {
            o->result = Result::failed;
            o->error = e;
        }
        o->sink.reset();
        o->stream.reset();
        o->inPass = false;
    }
    return true;
}

StemPass::Result StemPass::result (std::size_t i) const noexcept { return i < outs.size() ? outs[i]->result : Result::failed; }
juce::String StemPass::getError (std::size_t i) const { return i < outs.size() ? outs[i]->error : juce::String(); }
juce::String StemPass::whySeparate (std::size_t i) const { return i < outs.size() ? outs[i]->why : juce::String(); }
float StemPass::getPeak (std::size_t i) const noexcept { return i < outs.size() ? outs[i]->peak : 0.0f; }

namespace
{
// FL's "Record <insert> to" file of an insert (round 167a): a track render writes there; none = File()
juce::File recordToFile (const Song& song, int t)
{
    if (t <= 0 || t >= (int) song.mixer.size())
        return {};
    const auto path = juce::String::fromUTF8 (song.mixer[(std::size_t) t].recordTo.c_str());
    return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

// the files of one render set: the full mix when asked, then one per channel or insert (ui/ExportDialog's StemsThread
// before round 190gj)
void addStemItems (StemPlan& plan, int set, const Song& from, const Options& o, const juce::File& base, const std::vector<int>& onlyTracks,
                   const juce::String& prefix)
{
    const auto ext = extension (o.format);
    const auto baseName = base.getFileNameWithoutExtension().toStdString();
    auto file = [&] (int number, const std::string& name) {
        return base.getSiblingFile (juce::String::fromUTF8 (stemFileName (baseName, number, name, ext).c_str()));
    };

    if (o.stemsIncludeMaster)
        plan.items.push_back ({ prefix + juce::String ("Master"), base, set, 0, from, o });

    if (o.stems == Stems::channels || o.stems == Stems::channelsDry)
        for (auto c : stemChannels (from, o.range))
            plan.items.push_back ({ prefix + juce::String::fromUTF8 (from.channels[(std::size_t) c].name.c_str()), file (c + 1, from.channels[(std::size_t) c].name), set,
                                    -1, makeChannelStemSong (from, c, o.stems == Stems::channelsDry), o });
    else
        for (auto t : stemTracks (from, onlyTracks))
            plan.items.push_back ({ prefix + juce::String::fromUTF8 (from.mixer[(std::size_t) t].name.c_str()),
                                    ! onlyTracks.empty() && recordToFile (from, t) != juce::File() ? recordToFile (from, t).withFileExtension (ext)
                                                                                                   : file (t, from.mixer[(std::size_t) t].name),
                                    set, t, makeBounceSong (from, t), o });
}
} // namespace

StemPlan planStemExport (const Song& song, const Options& options, const juce::File& base, const std::vector<int>& onlyTracks)
{
    StemPlan plan;
    // round 190ap: "One file per marker section" — each section is rendered as a time selection of the song, alone
    // ("<name> - 02 Verse.wav") or with its stems ("<name> - 02 Verse - 3 Bass.wav")
    if (exportsSections (song, options))
    {
        plan.sectioned = true;
        const auto ext = extension (options.format);
        const auto sections = exportSections (song, options); // (round 190ol: the Sections lane's or the markers')
        const auto each = sectionOptions (options);
        const auto baseName = base.getFileNameWithoutExtension().toStdString();
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const auto name = sectionBaseName (baseName, (int) i + 1, (int) sections.size(), sections[i].name);
            const auto file = base.getSiblingFile (juce::String::fromUTF8 (name.c_str()) + ext);
            const auto part = makeSectionSong (song, sections[i]);
            plan.sets.push_back ({ part, each, each.stems != Stems::none && each.tail == TailMode::leave });
            const auto set = (int) plan.sets.size() - 1;
            if (each.stems == Stems::none)
                plan.items.push_back ({ juce::String::fromUTF8 (sections[i].name.c_str()), file, set, 0, part, each });
            else
                addStemItems (plan, set, part, each, file, onlyTracks, juce::String::fromUTF8 (sections[i].name.c_str()) + ": ");
        }
        return plan;
    }
    plan.sets.push_back ({ song, options, options.tail == TailMode::leave }); // (cut / wrap: every file is the music's length)
    addStemItems (plan, 0, song, options, base, onlyTracks, {});
    return plan;
}

int runStemExport (const StemPlan& plan, std::function<void (std::function<void()>)> post, std::function<bool()> stop,
                   const std::function<void (double)>& progress, const std::function<void (const juce::String&)>& status,
                   const std::function<void (std::size_t, bool, float)>& done, bool separate, bool extraData, StemRunStats* stats)
{
    const auto count = plan.items.size();
    const std::function<bool()> stopFn = stop ? std::move (stop) : std::function<bool()> ([] { return false; });
    std::vector<char> handled (count, 0);
    std::size_t finished = 0;
    int written = 0;
    // round 190gh (B7): each set's mix length (frames after the trim; −1 = not known yet): every other file of a set
    // whose tail rings out gets it, as FL's split render
    std::vector<std::int64_t> lengths (plan.sets.size(), -1);
    // the length of a set's mix when no render made it: a render of the mix to a temporary file
    auto probe = [&] (std::size_t s) -> std::int64_t {
        if (lengths[s] >= 0)
            return lengths[s];
        const auto& set = plan.sets[s];
        if (status)
            status (tr ("Measuring the length of the mix..."));
        auto o = set.options;
        o.format = exportopts::Format::wav;
        o.moreFormats = 0;
        o.wavBits = 16;
        const juce::TemporaryFile temp (".wav");
        MessageThreadOwned<ExportJob> slot (post, stopFn);
        auto* job = slot.create ([&set, &o, &temp] { return std::make_unique<ExportJob> (set.song, o, temp.getFile()); });
        if (job == nullptr)
            return 0;
        juce::String error;
        const auto frames = job->run ([&] (double) { return ! stopFn(); }, error) ? job->getWrittenFrames() : 0;
        slot.release();
        if (stats != nullptr)
            ++stats->alone;
        return lengths[s] = frames;
    };
    auto report = [&] (double p) {
        if (progress)
            progress (juce::jlimit (0.0, 1.0, p));
    };

    // each set's inserts (and its own mix) from one render of it
    for (std::size_t s = 0; s < plan.sets.size() && ! separate; ++s)
    {
        std::vector<std::size_t> members;
        for (std::size_t i = 0; i < count; ++i)
            if (plan.items[i].set == (int) s && plan.items[i].track >= 0)
                members.push_back (i);
        if (std::none_of (members.begin(), members.end(), [&] (std::size_t i) { return plan.items[i].track > 0; }))
            continue; // (no stem: nothing to share)

        // files that need their whole render in memory (normalise, mastering, mono, more formats, tails cut or wrapped)
        // are made together only as far as about 2 GB of it goes; streamed files have no such limit
        const auto& set = plan.sets[s];
        const auto o = clamped (set.options);
        const auto streamed = o.format != exportopts::Format::mp3 && o.format != exportopts::Format::aac && o.channels == Channels::stereo && ! o.normalize && ! o.master
                              && o.tail == TailMode::leave && formatsOf (o).size() == 1;
        const auto seconds = exportMusicSeconds (set.song, o) + tailSecondsFor (o);
        const auto bytesEach = std::max (1.0, seconds * o.sampleRate * 2.0 * sizeof (float));
        const auto perPass = streamed ? members.size() : std::max<std::size_t> (1, (std::size_t) (2.0e9 / bytesEach));

        for (std::size_t first = 0; first < members.size(); first += perPass)
        {
            if (stopFn())
                return written;
            const auto last = std::min (members.size(), first + perPass);
            std::vector<StemPass::Target> targets;
            for (auto k = first; k < last; ++k)
                targets.push_back ({ plan.items[members[k]].track, plan.items[members[k]].file });
            if (status)
                status (plan.items[members[first]].name + (last - first > 1 ? " + " + juce::String ((int) (last - first - 1)) : juce::String()));

            // (a song with solos: this render is not its mix, which is measured on its own first)
            const auto solo = std::any_of (set.song.mixer.begin(), set.song.mixer.end(), [] (const MixerTrack& m) { return m.solo; });
            const auto known = set.sameLength && solo ? probe (s) : lengths[s];
            if (stopFn())
                return written;
            MessageThreadOwned<StemPass> slot (post, stopFn);
            const auto made = juce::Time::getMillisecondCounterHiRes();
            auto* pass = slot.create ([&set, &targets, extraData, known] {
                auto p = std::make_unique<StemPass> (set.song, set.options, targets, set.sameLength, known);
                p->setExtraData (extraData);
                return p;
            });
            const auto rendering = juce::Time::getMillisecondCounterHiRes();
            if (stats != nullptr)
                stats->makeMs += rendering - made;
            if (pass == nullptr)
                return written; // (cancelled)

            juce::String error;
            const auto base = (double) finished / (double) std::max<std::size_t> (1, count);
            const auto share = (double) (last - first) / (double) std::max<std::size_t> (1, count);
            const auto ok = pass->run ([&] (double p) {
                report (base + p * share);
                return ! stopFn();
            }, error);
            if (stats != nullptr)
            {
                ++stats->passes;
                stats->renderMs += juce::Time::getMillisecondCounterHiRes() - rendering;
            }
            if (! ok)
            {
                slot.release();
                return written; // (cancelled)
            }
            if (set.sameLength && pass->getMixLength() >= 0)
                lengths[s] = pass->getMixLength();
            for (auto k = first; k < last; ++k)
            {
                const auto i = members[k];
                const auto r = pass->result (k - first);
                if (r == StemPass::Result::separate)
                {
                    if (stats != nullptr)
                        stats->why.add (plan.items[i].name + ": " + pass->whySeparate (k - first));
                    continue;
                }
                handled[i] = 1;
                ++finished;
                if (r == StemPass::Result::written)
                {
                    ++written;
                    if (stats != nullptr)
                        ++stats->inPass;
                }
                if (done)
                    done (i, r == StemPass::Result::written, pass->getPeak (k - first));
            }
            slot.release();
        }
    }

    // the rest: one render per file, as before
    for (std::size_t i = 0; i < count; ++i)
    {
        if (handled[i])
            continue;
        if (stopFn())
            break;
        const auto& item = plan.items[i];
        const auto set = (std::size_t) std::clamp (item.set, 0, (int) plan.sets.size() - 1);
        const auto mixItem = item.track == 0;
        // round 190gh: the set's length, from its mix (exported first when asked, else rendered once to find it)
        const auto exact = plan.sets[set].sameLength && ! mixItem ? std::max<std::int64_t> (0, probe (set)) : 0;
        if (status)
            status (item.name);

        MessageThreadOwned<ExportJob> slot (post, stopFn);
        const auto made = juce::Time::getMillisecondCounterHiRes();
        auto* job = slot.create ([&item, extraData, exact] {
            auto j = std::make_unique<ExportJob> (item.song, item.options, item.file);
            j->setExtraData (extraData);
            j->setExactFrames (exact);
            return j;
        });
        const auto rendering = juce::Time::getMillisecondCounterHiRes();
        if (stats != nullptr)
            stats->makeMs += rendering - made;
        if (job == nullptr)
            break; // (cancelled)

        juce::String error;
        const auto base = (double) finished / (double) std::max<std::size_t> (1, count);
        const auto ok = job->run ([&] (double p) {
            report (base + p / (double) std::max<std::size_t> (1, count));
            return ! stopFn();
        }, error);
        ++finished;
        if (ok)
            ++written;
        if (ok && mixItem && plan.sets[set].sameLength && lengths[set] < 0)
            lengths[set] = job->getWrittenFrames();
        if (stats != nullptr)
        {
            ++stats->alone;
            stats->renderMs += juce::Time::getMillisecondCounterHiRes() - rendering;
        }
        if (done)
            done (i, ok, job->getPeak());
        slot.release();
    }
    return written;
}
} // namespace lynx
