#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/ExportOptions.h"
#include "core/Song.h"

namespace juce
{
class AudioPluginInstance;
}

namespace lynx
{
class AudioEngine;
class Instrument;
class Effect;
class PluginInstrument;
class PluginEffect;

// The short form used by the command line, the self-test and "Render to audio" (the Export window uses
// exportopts::Options, core/ExportOptions.h).
struct ExportSettings
{
    enum class Format
    {
        wav16,
        wav24,
        wav32Float,
        flac24,
        ogg
    };

    Format format = Format::wav24;
    double sampleRate = 44100.0;
    bool wholeSong = true;     // false = current pattern only
    double tailSeconds = 6.0;  // maximum tail; stops earlier when the sound ends

    exportopts::Options toOptions() const;
};

// File extension (with dot) for the format.
juce::String exportExtension (ExportSettings::Format format);

// Round 190jb (owner decision 2026-11-16: FL-exact; docs/analise/REA_rodada190jb.md): FL renders with the plugin
// instances that play, playback stopped. While a hold is open, an ExportJob made on the message thread (the Export
// window's mix, its stems and its marker sections; round 190jx: Render to audio and Freeze too) renders with the live engine's hosted plugins and the Patchers that
// host plugins: the same instances, so a plugin with internal state, a random seed, a licence check or a long boot
// renders what was heard. LYNX's own units are made new (their render is the same either way). Opening the hold stops
// playback; when it and the last job that used it are gone (message thread), every plugin it lent gets its sample rate
// and its state back and is reset as FL resets it, and the live engine plays again. The export makes its own instances,
// as before, without a hold (the command line, the remote renderer, the self-tests' own engines), when the live engine
// cannot be held (recording, a real-time export), when another job uses the hold, and for a plugin it cannot lend (a
// crashed one, one that refuses the export's sample rate, a Patcher at another rate).
class LiveExportHold
{
public:
    // nullptr when the live engine can't be held (message thread)
    static std::shared_ptr<LiveExportHold> open (AudioEngine& live);
    // the hold open now, if any
    static std::shared_ptr<LiveExportHold> current();
    ~LiveExportHold();
    LiveExportHold (const LiveExportHold&) = delete;
    LiveExportHold& operator= (const LiveExportHold&) = delete;

    AudioEngine& getLive() noexcept { return live; }
    // the plugins lent so far (hosted plugins, counting the ones inside Patchers)
    int lentPlugins() const noexcept { return (int) units.size(); }
    // one job at a time renders with the lent units (false: another one does; this one makes its own)
    bool claim() noexcept;
    void unclaim() noexcept;
    // what the claiming job's engine builds with (AudioEngine::setLiveUnits)
    std::shared_ptr<Instrument> lendInstrument (const ChannelDef& channel, double sampleRate);
    std::shared_ptr<Effect> lendEffect (const EffectSlot& slot, double sampleRate);

    // tests: why a unit was not lent (the last refusal)
    juce::String lastRefusal() const { return refusal; }

private:
    explicit LiveExportHold (AudioEngine& e) : live (e) {}
    struct Unit
    {
        std::shared_ptr<void> keep; // (the adapter alive until it is restored)
        juce::AudioPluginInstance* plugin = nullptr;
        PluginInstrument* instrument = nullptr;
        PluginEffect* effect = nullptr;
        std::optional<std::string> state; // as it was before the export
        double rate = 0.0;
    };
    void note (const std::shared_ptr<void>& keep, PluginInstrument* instrument, PluginEffect* effect);
    bool fitRate (double sampleRate, PluginInstrument* instrument, PluginEffect* effect);
    AudioEngine& live;
    std::vector<Unit> units;
    std::atomic<bool> claimed { false };
    juce::String refusal;
};

// Exports a copy of the song with its own engine, without touching what is playing (round 190jb: with a LiveExportHold
// open, with the live engine's plugin instances).
// The constructor creates the instruments and plugins (call on the UI thread); run() may
// run on a background thread.
class ExportJob
{
public:
    ExportJob (const Song& song, const ExportSettings& settings, const juce::File& output);
    ExportJob (const Song& song, const exportopts::Options& options, const juce::File& output);
    // A real-time bounce through the audio device (round 165c, "Render through the hardware in real time"): the live
    // engine plays the song while its Master is recorded, so hardware inserts' returns are in the file. The file is
    // written at the device's rate; the range is the song or the Playlist selection, played once, with the Master's
    // effects and the playback quality (what you hear). Construct on the UI thread; run() on another thread (it asks
    // the message thread to start and stop playback).
    ExportJob (AudioEngine& live, const exportopts::Options& options, const juce::File& output);
    ~ExportJob();

    // What a real-time bounce needs: the song has an active hardware insert. The options it changes (and why) for the
    // Export window, and the length it takes (the music and the longest tail).
    static exportopts::Options realtimeOptions (exportopts::Options o, double deviceRate);
    // A message for the user after run() (dropouts during a real-time bounce); empty when nothing to say
    juce::String getNotice() const { return notice; }
    // what the export did besides writing the file (the mastering's loudness, round 168e); empty when nothing to say
    juce::String getInfo() const { return info; }

    // progress(0..1) returns false to cancel. On error or cancellation the file is deleted. With several formats
    // (Options::moreFormats) every file is written from the one render: `output`, then the same name per extension.
    // The frozen channels this render plays live because their audio is out of date (sessionpack::freshForExport)
    const std::vector<int>& channelsRenderedLive() const noexcept { return thawedForExport; }
    bool run (const std::function<bool (double)>& progress, juce::String& error);

    double getMusicSeconds() const noexcept { return musicSeconds; }
    float getPeak() const noexcept { return peak; }
    // FL's "Extra data" (markers, loop, tempo: Options::saveTempo / loopMarkers / playlistMarkers) in WAV files: on for
    // the user's exports (the export window, --export), off for LYNX's own renders (bounces, consolidation, the
    // auto-sampler), whose files become samples again and must not carry a loop
    void setExtraData (bool on) noexcept { extraData = on; }
    AudioEngine& getEngine() noexcept { return *engine; }
    // Round 190gh (B7): the file gets exactly `frames` samples (music, then the tail, padded with silence when it
    // ends sooner): every stem of one export as long as the mix. 0 = the tail ends at its silence (as before).
    void setExactFrames (std::int64_t frames) noexcept { exactFrames = frames; }
    // the frames the last run() wrote (after the PDC trim and the tail)
    std::int64_t getWrittenFrames() const noexcept { return writtenFrames; }
    // Round 190jb: the live engine's plugins its hold has lent so far, when this job renders with them (0: its own
    // instances only)
    int livePlugins() const noexcept;

private:
    friend class StemPass;
    // Round 190gj: a job that only writes `file` from samples another job's render makes (StemPass): no engine
    struct NoEngine
    {
    };
    ExportJob (const Song& song, const exportopts::Options& options, const juce::File& output, NoEngine);

    // what run() renders into: the render's options with this file's writer (streamed) or capture (buffered), then the
    // file finished from what came out (`renderedPeak` < 0: cancelled)
    struct Sink; // (Exporter.cpp)
    bool openSink (Sink& sink, juce::String& error);
    bool closeSink (Sink& sink, float renderedPeak, juce::String& error);
    bool writeBuffered (std::vector<float>& left, std::vector<float>& right, juce::String& error);
    bool writeFile (const exportopts::Options& o, const juce::File& file, float* left, float* right, int frames, int channels, juce::String& error);

    bool runRealtime (const std::function<bool (double)>& progress, juce::String& error);

    std::shared_ptr<LiveExportHold> hold; // (round 190jb: declared before the engine, which holds its units: released after it)
    std::unique_ptr<AudioEngine> engine;
    std::vector<int> thawedForExport; // frozen channels whose audio was out of date: they render live (round 190ay)
    AudioEngine* live = nullptr; // a real-time bounce
    double startTick = 0.0;
    juce::String notice, info;
    exportopts::Options options;
    juce::File output;
    std::string title, artist;
    double musicSeconds = 0.0, loopSeconds = 0.0;
    exportopts::WavExtras extras; // FL's extra data for WAV files
    bool extraData = false;
    float peak = 0.0f;
    std::int64_t exactFrames = 0, writtenFrames = 0;
};

// Seconds the export of `song` with these options lasts (the music, all loops; no tail).
double exportMusicSeconds (const Song& song, const exportopts::Options& options);

// Round 190gj (P9): FL renders "Split mixer tracks" in one pass (TRENDERFORM; REA_rodada84 §1). LYNX's stem of an insert
// is the render of makeBounceSong (song, insert); here one engine plays the song once, the output of every insert a stem
// needs is tapped, and each file is made from what that stem's own render would have put out (exportopts::planStemTap,
// StemMixer) by the same code (RenderStream, ExportJob's sink): the same file, bit for bit. A target the one render
// cannot make exactly is left to its own render (result() == separate): the caller renders it with an ExportJob.
class StemPass
{
public:
    struct Target
    {
        int track = 0; // an insert (its stem), or 0: the song's own mix (the Master file)
        juce::File file;
    };
    enum class Result
    {
        separate, // not made here: needs its own render
        written,
        failed
    };

    // On the message thread (the engine's plugins are made there). `sameLength` (round 190gh: a stems export whose tail is
    // left to ring): every stem gets the mix's length — `mixLength` frames after the trim, or, when that is not known
    // yet (< 0), the length the mix has in this render.
    StemPass (const Song& song, const exportopts::Options& options, const std::vector<Target>& targets, bool sameLength = false,
              std::int64_t mixLength = -1);
    ~StemPass();
    void setExtraData (bool on) noexcept;

    // The one render (any thread). False: cancelled, or the render could not start (`error`).
    bool run (const std::function<bool (double)>& progress, juce::String& error);

    std::size_t size() const noexcept { return outs.size(); }
    Result result (std::size_t i) const noexcept;
    juce::String getError (std::size_t i) const;
    juce::String whySeparate (std::size_t i) const; // (a target that needs its own render: why)
    float getPeak (std::size_t i) const noexcept;
    AudioEngine& getEngine() noexcept;
    // the mix's length after the trim (frames) once run() found or was given it; −1 = not known
    std::int64_t getMixLength() const noexcept { return mixLength; }
    // the song renders as it plays without the solos the song has (when it has none, its own mix can be a target)
    bool rendersTheMix() const noexcept { return masterInPass; }

private:
    struct Out;
    struct Shared;
    std::unique_ptr<ExportJob> main; // owns the engine that plays the song
    std::vector<std::unique_ptr<Out>> outs;
    exportopts::Options options;
    bool masterInPass = false;
    bool sameLength = false;
    std::int64_t mixLength = -1;
};

// The files of an export with stems or sections (the Export window, the Mixer's "Render armed tracks", --stemexport),
// in the order they are written. A set is one song with its options (the song, or one of its marker sections).
struct StemPlan
{
    struct Set
    {
        Song song;
        exportopts::Options options;
        // round 190gh (B7): every stem of the set has the mix's length, as FL's split render (its tail is left to ring)
        bool sameLength = false;
    };
    struct Item
    {
        juce::String name;
        juce::File file;
        int set = 0;
        int track = -1;        // the set's insert whose stem this is; 0: the set's song itself; −1: a channel stem
        Song song;             // what the item's own render plays (as before round 190gj)
        exportopts::Options options;
    };
    std::vector<Set> sets;
    std::vector<Item> items;
    bool sectioned = false;
};
// `onlyTracks`: the Mixer's armed / selected inserts (FL's "Render armed tracks"); empty = every insert with sound.
StemPlan planStemExport (const Song& song, const exportopts::Options& options, const juce::File& base, const std::vector<int>& onlyTracks = {});

// Renders a plan: every set's inserts (and its song, when asked) in one pass each (StemPass), the rest one render per
// file, as before; `separate` = every file by its own render (the old way, for comparisons). `post` runs a call on the
// message thread (where engines are made and deleted); `stop` asks the work to end (cancel). `progress` (0..1) and
// `status` (the item's name) report; `done (item, ok)` after each file. Returns the files written.
struct StemRunStats
{
    int passes = 0, inPass = 0, alone = 0; // renders shared, files made by them, files rendered alone
    double makeMs = 0.0, renderMs = 0.0;     // making the engines (plugins), and rendering + writing
    juce::StringArray why;                  // why files were rendered alone
};
int runStemExport (const StemPlan& plan, std::function<void (std::function<void()>)> post, std::function<bool()> stop,
                   const std::function<void (double)>& progress, const std::function<void (const juce::String&)>& status,
                   const std::function<void (std::size_t, bool, float)>& done, bool separate = false, bool extraData = true,
                   StemRunStats* stats = nullptr);
} // namespace lynx
