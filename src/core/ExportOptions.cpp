#include "core/ExportOptions.h"
#include "core/Scheduler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

#include "core/PatternBlocks.h"
#include "core/Preferences.h"

namespace lynx::exportopts
{
const std::vector<int>& mp3Bitrates()
{
    static const std::vector<int> list { 96, 128, 160, 192, 224, 256, 320 };
    return list;
}

const std::vector<int>& oggBitrates()
{
    static const std::vector<int> list { 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 500 };
    return list;
}

const std::vector<double>& sampleRates()
{
    static const std::vector<double> list { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    return list;
}

const char* extension (Format format)
{
    switch (format)
    {
        case Format::mp3: return ".mp3";
        case Format::ogg: return ".ogg";
        case Format::flac: return ".flac";
        case Format::wav: break;
    }
    return ".wav";
}

int fileBits (const Options& o) noexcept
{
    switch (o.format)
    {
        case Format::wav: return o.wavBits;
        case Format::flac: return o.flacBits;
        case Format::mp3:
        case Format::ogg: break;
    }
    return 16;
}

bool usesDither (const Options& o) noexcept
{
    if (o.dither == DitherMode::none)
        return false;
    if (o.format == Format::wav)
        return o.wavBits == 16 || o.wavBits == 24;
    return o.format == Format::flac;
}

double mp3SampleRate (double rate) noexcept
{
    if (rate == 32000.0 || rate == 44100.0 || rate == 48000.0)
        return rate;
    return std::fmod (rate, 11025.0) == 0.0 ? 44100.0 : 48000.0;
}

namespace
{
template <typename T>
T nearest (const std::vector<T>& list, T value)
{
    auto best = list.front();
    for (auto v : list)
        if (std::abs ((double) v - (double) value) < std::abs ((double) best - (double) value))
            best = v;
    return best;
}

int toInt (const std::map<std::string, std::string>& kv, const char* key, int fallback)
{
    const auto it = kv.find (key);
    if (it == kv.end() || it->second.empty())
        return fallback;
    try
    {
        return std::stoi (it->second);
    }
    catch (...)
    {
        return fallback;
    }
}

double toDouble (const std::map<std::string, std::string>& kv, const char* key, double fallback)
{
    const auto it = kv.find (key);
    if (it == kv.end() || it->second.empty())
        return fallback;
    try
    {
        return std::stod (it->second);
    }
    catch (...)
    {
        return fallback;
    }
}
} // namespace

Options clamped (Options o)
{
    const Options d;
    if ((int) o.format < 0 || (int) o.format > 3) o.format = d.format;
    if (o.wavBits != 16 && o.wavBits != 24 && o.wavBits != 32) o.wavBits = d.wavBits;
    if (o.flacBits != 16 && o.flacBits != 24) o.flacBits = d.flacBits;
    o.flacLevel = std::clamp (o.flacLevel, 0, 8);
    o.mp3Kbps = nearest (mp3Bitrates(), o.mp3Kbps);
    o.oggKbps = nearest (oggBitrates(), o.oggKbps);
    o.sampleRate = nearest (sampleRates(), o.sampleRate);
    if ((int) o.dither < 0 || (int) o.dither > 2) o.dither = d.dither;
    if ((int) o.tail < 0 || (int) o.tail > 2) o.tail = d.tail;
    if (! (o.maxTailSeconds >= 0.0)) o.maxTailSeconds = d.maxTailSeconds;
    o.maxTailSeconds = std::min (o.maxTailSeconds, 60.0);
    if ((int) o.range < 0 || (int) o.range > 2) o.range = d.range;
    o.loops = std::clamp (o.loops, 1, 16);
    if ((int) o.stems < 0 || (int) o.stems > (int) Stems::channelsDry) o.stems = d.stems;
    if ((int) o.channels < 0 || (int) o.channels > 3) o.channels = d.channels;
    o.moreFormats &= 0xFu & ~(1u << (int) o.format);
    if (! (o.masterLufs >= -30.0 && o.masterLufs <= -5.0)) o.masterLufs = d.masterLufs;
    return o;
}

double lengthInBars (double seconds, double bpm, Meter meter) noexcept
{
    return seconds <= 0.0 || bpm <= 0.0 ? 0.0 : seconds * bpm / 60.0 / ((double) meter.bar() / ppq);
}

std::int64_t estimatedBytes (const Options& o, double seconds) noexcept
{
    if (seconds <= 0.0)
        return 0;
    const auto channels = o.channels == Channels::stereo ? 2.0 : 1.0;
    std::int64_t total = 0; // every format the export writes (FL's disk space counts all ticked boxes)
    for (const auto f : { Format::wav, Format::mp3, Format::ogg, Format::flac })
        if (f == o.format || (o.moreFormats & (1u << (int) f)) != 0)
        switch (f)
        {
            case Format::wav: total += (std::int64_t) (seconds * o.sampleRate * channels * (o.wavBits / 8.0)) + 44; break;
            case Format::flac: total += (std::int64_t) (seconds * o.sampleRate * channels * (o.flacBits / 8.0) * 0.6); break; // ~60 % of PCM
            case Format::mp3: total += (std::int64_t) (seconds * o.mp3Kbps * 1000.0 / 8.0); break;
            case Format::ogg: total += (std::int64_t) (seconds * o.oggKbps * 1000.0 / 8.0); break;
        }
    return total;
}

std::vector<Format> formatsOf (const Options& o)
{
    std::vector<Format> out { o.format };
    for (const auto f : { Format::wav, Format::mp3, Format::ogg, Format::flac })
        if (f != o.format && (o.moreFormats & (1u << (int) f)) != 0)
            out.push_back (f);
    return out;
}

std::uint32_t flLastExportedMask (const Options& o) noexcept
{
    std::uint32_t mask = flFormatBit (o.format);
    for (const auto f : { Format::wav, Format::mp3, Format::ogg, Format::flac })
        if ((o.moreFormats & (1u << (int) f)) != 0)
            mask |= flFormatBit (f);
    return mask;
}

Options withFlLastExported (Options o, std::uint32_t mask)
{
    if (mask < 8 || mask > 0x3FF) // FL: anything else falls back to WAV
        mask = 8;
    std::vector<Format> chosen;
    for (const auto f : { Format::wav, Format::mp3, Format::ogg, Format::flac })
        if ((mask & flFormatBit (f)) != 0)
            chosen.push_back (f);
    if (chosen.empty()) // only formats LYNX writes elsewhere (MIDI, M4A): WAV
        chosen.push_back (Format::wav);
    o.format = chosen.front();
    o.moreFormats = 0;
    for (std::size_t i = 1; i < chosen.size(); ++i)
        o.moreFormats |= 1u << (int) chosen[i];
    return o;
}

Options withSingleFormat (Options o, Format f)
{
    o.format = f;
    o.moreFormats = 0;
    return o;
}

std::vector<Note> midiExportNotes (const Song& original)
{
    std::optional<Song> expanded; // Pattern Blocks (round 170f) export as their clips
    if (patternblocks::active (original))
        patternblocks::expandSong (expanded.emplace (original));
    const Song& song = expanded ? *expanded : original;
    std::vector<Note> out;
    const auto trackMuted = [&song] (int track) {
        return track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].muted;
    };
    if (song.mode == PlayMode::song)
        for (std::size_t clipIndex = 0; clipIndex < song.playlist.size(); ++clipIndex)
        {
            const auto& c = song.playlist[clipIndex];
            if (c.kind != ClipKind::pattern || c.muted || c.length <= 0 || trackMuted (c.track) || c.pattern < 0
                || c.pattern >= (int) song.patterns.size())
                continue;
        {
            // the pattern repeats along a clip longer than it (LYNX extra stretchedClipsLoop, issue #25)
            const auto& pattern = song.patterns[(std::size_t) c.pattern];
            const auto period = std::max<std::int64_t> (1, pattern.effectiveLength (song.meter()));
            const auto repeats = stretchedClipsLoop() && c.offset + c.length > period;
            // FL's pattern clip takeover (issue #158): what plays is what is written
            const auto stopsAt = patternClipStopsAt (song, clipIndex);
            for (auto pass = repeats ? c.offset / period : 0; pass <= (repeats ? (c.offset + c.length - 1) / period : 0); ++pass)
                for (auto n : pattern.notes)
                {
                    if (repeats && n.start >= period)
                        continue;
                    const auto at = n.start + (repeats ? pass * period : 0);
                    if (n.muted || at < c.offset || at >= c.offset + c.length)
                        continue;
                    n.start = c.start + (at - c.offset);
                    if (n.start >= stopsAt)
                        continue;
                    n.length = std::max<std::int64_t> (1, std::min (n.length, c.end() - n.start));
                    out.push_back (n);
                }
        }
        }

    if (out.empty() && (song.mode != PlayMode::song || std::none_of (song.playlist.begin(), song.playlist.end(),
                                                                       [] (const Clip& c) { return c.kind == ClipKind::pattern; })))
        if (song.currentPattern >= 0 && song.currentPattern < (int) song.patterns.size())
            for (const auto& n : song.patterns[(std::size_t) song.currentPattern].notes)
                if (! n.muted)
                    out.push_back (n);

    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::string toText (const Options& o)
{
    std::ostringstream s;
    s << "format=" << (int) o.format << "\n"
      << "wavBits=" << o.wavBits << "\n"
      << "flacBits=" << o.flacBits << "\n"
      << "flacLevel=" << o.flacLevel << "\n"
      << "mp3Kbps=" << o.mp3Kbps << "\n"
      << "oggKbps=" << o.oggKbps << "\n"
      << "sampleRate=" << (int) o.sampleRate << "\n"
      << "dither=" << (int) o.dither << "\n"
      << "tail=" << (int) o.tail << "\n"
      << "maxTail=" << o.maxTailSeconds << "\n"
      << "range=" << (int) o.range << "\n"
      << "loops=" << o.loops << "\n"
      << "stems=" << (int) o.stems << "\n"
      << "stemsMaster=" << (o.stemsIncludeMaster ? 1 : 0) << "\n"
      << "normalize=" << (o.normalize ? 1 : 0) << "\n"
      << "masterEffects=" << (o.masterEffects ? 1 : 0) << "\n"
      << "insertEffects=" << (o.insertEffects ? 1 : 0) << "\n"
      << "trimPdc=" << (o.trimPdc ? 1 : 0) << "\n"
      << "hqAllPlugins=" << (o.hqAllPlugins ? 1 : 0) << "\n"
      << "pluginWarmUp=" << (o.pluginWarmUp ? 1 : 0) << "\n"
      << "showWhenDone=" << (o.showWhenDone ? 1 : 0) << "\n"
      << "disableMaxPolyphony=" << (o.disableMaxPolyphony ? 1 : 0) << "\n"
      << "moreFormats=" << o.moreFormats << "\n"
      << "channels=" << (int) o.channels << "\n"
      << "saveTempo=" << (o.saveTempo ? 1 : 0) << "\n"
      << "loopMarkers=" << (o.loopMarkers ? 1 : 0) << "\n"
      << "playlistMarkers=" << (o.playlistMarkers ? 1 : 0) << "\n"
      << "masterLufs=" << o.masterLufs << "\n"
      << "sections=" << (o.sections ? 1 : 0) << "\n";
    return s.str();
}

Options fromText (const std::string& text)
{
    const auto kv = prefs::parseKeyValues (text);
    const Options d;
    Options o;
    o.format = (Format) toInt (kv, "format", (int) d.format);
    o.wavBits = toInt (kv, "wavBits", d.wavBits);
    o.flacBits = toInt (kv, "flacBits", d.flacBits);
    o.flacLevel = toInt (kv, "flacLevel", d.flacLevel);
    o.mp3Kbps = toInt (kv, "mp3Kbps", d.mp3Kbps);
    o.oggKbps = toInt (kv, "oggKbps", d.oggKbps);
    o.sampleRate = toDouble (kv, "sampleRate", d.sampleRate);
    o.dither = (DitherMode) toInt (kv, "dither", (int) d.dither);
    o.tail = (TailMode) toInt (kv, "tail", (int) d.tail);
    o.maxTailSeconds = toDouble (kv, "maxTail", d.maxTailSeconds);
    o.range = (Range) toInt (kv, "range", (int) d.range);
    o.loops = toInt (kv, "loops", d.loops);
    o.stems = (Stems) toInt (kv, "stems", (int) d.stems);
    o.stemsIncludeMaster = toInt (kv, "stemsMaster", d.stemsIncludeMaster ? 1 : 0) != 0;
    o.normalize = toInt (kv, "normalize", d.normalize ? 1 : 0) != 0;
    o.masterEffects = toInt (kv, "masterEffects", d.masterEffects ? 1 : 0) != 0;
    o.insertEffects = toInt (kv, "insertEffects", d.insertEffects ? 1 : 0) != 0;
    o.trimPdc = toInt (kv, "trimPdc", d.trimPdc ? 1 : 0) != 0;
    o.hqAllPlugins = toInt (kv, "hqAllPlugins", d.hqAllPlugins ? 1 : 0) != 0;
    o.pluginWarmUp = toInt (kv, "pluginWarmUp", d.pluginWarmUp ? 1 : 0) != 0;
    o.showWhenDone = toInt (kv, "showWhenDone", d.showWhenDone ? 1 : 0) != 0;
    o.disableMaxPolyphony = toInt (kv, "disableMaxPolyphony", d.disableMaxPolyphony ? 1 : 0) != 0;
    o.moreFormats = (std::uint32_t) std::clamp (toInt (kv, "moreFormats", 0), 0, 15);
    o.channels = (Channels) toInt (kv, "channels", (int) d.channels);
    o.saveTempo = toInt (kv, "saveTempo", d.saveTempo ? 1 : 0) != 0;
    o.loopMarkers = toInt (kv, "loopMarkers", d.loopMarkers ? 1 : 0) != 0;
    o.playlistMarkers = toInt (kv, "playlistMarkers", d.playlistMarkers ? 1 : 0) != 0;
    o.masterLufs = toDouble (kv, "masterLufs", d.masterLufs);
    o.sections = toInt (kv, "sections", d.sections ? 1 : 0) != 0;
    return clamped (o);
}

bool hasSelection (const Song& song) noexcept
{
    return song.loopEnd > song.loopStart && song.loopStart >= 0;
}

Song makeRenderSong (const Song& original, const Options& o, std::int64_t tailTicks, std::int64_t& musicTicks, std::int64_t& loopTicks)
{
    // FL's legacy Pattern Blocks (round 170f) are rendered as the clips they play as
    std::optional<Song> expanded;
    if (patternblocks::active (original))
        patternblocks::expandSong (expanded.emplace (original));
    const Song& song = expanded ? *expanded : original;
    Song base = song;
    base.auditionLane = base.auditionComp = -1; // an audition is for listening: an export renders what the song plays (round 177c)
    std::int64_t length = 0;

    if (o.range == Range::selection && hasSelection (song))
    {
        const auto from = song.loopStart, to = song.loopEnd;
        std::vector<Clip> cropped;
        // by start (the same start: the list's order): clips of a pattern cut at `from` all start there, and the one that
        // started last must stay the later one (FL's pattern clip takeover, issue #158)
        auto ordered = song.playlist;
        std::stable_sort (ordered.begin(), ordered.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });

        for (auto clip : ordered)
        {
            if (clip.end() <= from || clip.start >= to || clip.length <= 0)
                continue;
            if (clip.start < from)
                clip = splitClip (clip, from);
            if (clip.end() > to)
                splitClip (clip, to); // keeps the head
            clip.start -= from;
            cropped.push_back (clip);
        }

        base.playlist = std::move (cropped);
        base.mode = PlayMode::song;
        std::int64_t ignored = 0;
        base = makeExportSong (base, true, 0, ignored);
        length = to - from;

        if (base.playlist.empty()) // nothing in the selection: a silent pass of the right length
        {
            base.patterns.emplace_back().name = "(silence)";
            Clip silent;
            silent.pattern = (int) base.patterns.size() - 1;
            silent.length = length;
            base.playlist.push_back (silent);
        }
    }
    else
        base = makeExportSong (song, o.range == Range::song || (o.range == Range::selection), 0, length);
        base.auditionLane = base.auditionComp = -1; // (made from the song again: the audition stays out)

    length = std::max<std::int64_t> (1, length);
    loopTicks = length;

    // the range repeated: copies of the clips shifted by one pass each
    const auto loops = std::clamp (o.loops, 1, 16);
    if (loops > 1)
    {
        const auto once = base.playlist;
        for (int k = 1; k < loops; ++k)
            for (auto clip : once)
            {
                clip.start += length * k;
                base.playlist.push_back (clip);
            }
    }

    musicTicks = length * loops;

    // the tail: an empty pattern after the music (plus a bar of slack, like makeExportSong)
    if (tailTicks > 0)
    {
        base.patterns.emplace_back().name = "(tail)";
        Clip tail;
        tail.pattern = (int) base.patterns.size() - 1;
        tail.start = musicTicks;
        tail.length = tailTicks + base.barTicks();
        base.playlist.push_back (tail);
    }

    if (! o.masterEffects && ! base.mixer.empty())
        base.mixer[0].effects.clear();
    if (! o.insertEffects)
        for (std::size_t t = 1; t < base.mixer.size(); ++t)
            base.mixer[t].effects.clear();

    if (o.disableMaxPolyphony) // FL: every channel's Max polyphony becomes 0 (no limit) for the render
        for (auto& c : base.channels)
            c.maxPolyphony = 0;

    // FL's export plays the song straight through: marker loops / skips / pauses and the Start / Loop markers act only
    // while playing live (REA_rodada165f-markers §1.3: FL's renders of every marker action match the plain song)
    for (auto& a : base.arrangements)
        for (auto& m : a.markers)
            if (m.action == Marker::markerLoop || m.action == Marker::markerSkip || m.action == Marker::markerPause
                || m.action == Marker::songLoop || m.action == Marker::start)
                m.action = Marker::none;

    base.mode = PlayMode::song;
    base.loopStart = base.loopEnd = 0;
    return base;
}

std::vector<int> stemTracks (const Song& song)
{
    std::vector<int> out;
    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        auto used = std::any_of (song.channels.begin(), song.channels.end(), [t] (const ChannelDef& c) { return c.mixerTrack == t; });
        for (int src = 1; src < (int) song.mixer.size() && ! used; ++src)
            used = src != t && song.routes (src, t);
        if (used)
            out.push_back (t);
    }
    return out;
}

std::vector<int> stemTracks (const Song& song, const std::vector<int>& only)
{
    if (only.empty())
        return stemTracks (song);
    std::vector<int> out;
    for (auto t : only)
        if (t > 0 && t < (int) song.mixer.size())
            out.push_back (t);
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

std::vector<int> stemChannels (const Song& original, Range range)
{
    std::optional<Song> expanded; // Pattern Blocks (round 170f) count as their clips
    if (patternblocks::active (original))
        patternblocks::expandSong (expanded.emplace (original));
    const Song& song = expanded ? *expanded : original;
    std::set<int> used;
    auto addPattern = [&] (int p) {
        if (p < 0 || p >= (int) song.patterns.size())
            return;
        for (const auto& n : song.patterns[(std::size_t) p].notes)
            used.insert (n.channel);
    };

    if (range == Range::pattern || song.playlist.empty())
        addPattern (song.currentPattern);
    else
        for (const auto& c : song.playlist)
        {
            if (c.muted || song.isTrackMuted (c.track))
                continue;
            if (c.kind == ClipKind::pattern)
                addPattern (c.pattern);
            else if (c.kind == ClipKind::audio)
                used.insert (c.channel);
        }

    std::vector<int> out;
    for (auto c : used)
        if (c >= 0 && c < (int) song.channels.size() && ! song.channels[(std::size_t) c].muted)
            out.push_back (c);
    return out;
}

Song makeChannelStemSong (const Song& song, int channel, bool dry)
{
    Song out = song;
    for (int c = 0; c < (int) out.channels.size(); ++c)
        if (c != channel)
            out.channels[(std::size_t) c].muted = true;
    if (dry && channel >= 0 && channel < (int) out.channels.size())
    {
        const auto t = out.channels[(std::size_t) channel].mixerTrack;
        if (t > 0 && t < (int) out.mixer.size())
        {
            auto& track = out.mixer[(std::size_t) t];
            track.sends.clear();
            if (! track.toMaster)
            {
                track.toMaster = true;
                track.masterLevel = 1.0f;
            }
        }
    }
    return out;
}

std::string stemFileName (const std::string& base, int number, const std::string& name, const char* ext)
{
    std::string clean;
    for (auto ch : name)
        clean += std::string ("<>:\"/\\|?*").find (ch) != std::string::npos || (unsigned char) ch < 32 ? '_' : ch;
    while (! clean.empty() && (clean.back() == ' ' || clean.back() == '.'))
        clean.pop_back();
    return base + " - " + std::to_string (number) + (clean.empty() ? "" : " " + clean) + ext;
}

// --- one file per marker section (round 190ap) ----------------------------------------------------------------------
namespace
{
std::string legalName (const std::string& name)
{
    std::string clean;
    for (auto ch : name)
        clean += std::string ("<>:\"/\\|?*").find (ch) != std::string::npos || (unsigned char) ch < 32 ? '_' : ch;
    while (! clean.empty() && (clean.back() == ' ' || clean.back() == '.'))
        clean.pop_back();
    const auto first = clean.find_first_not_of (' ');
    return first == std::string::npos ? std::string() : clean.substr (first);
}

std::string lowerAscii (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}
} // namespace

std::vector<Section> markerSections (const Song& song)
{
    std::vector<Section> out;
    const auto end = song.songLength();
    if (end <= 0)
        return out;
    // the plain markers inside the song, in order, one per place (the first of several at one tick names it)
    std::vector<std::pair<std::int64_t, std::string>> cuts;
    int index = 0;
    for (const auto& m : song.markers())
    {
        ++index;
        if (m.action != Marker::none || m.isSignature() || m.tick < 0 || m.tick >= end)
            continue;
        cuts.push_back ({ m.tick, m.name.empty() ? "Marker " + std::to_string (index) : m.name });
    }
    std::stable_sort (cuts.begin(), cuts.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    cuts.erase (std::unique (cuts.begin(), cuts.end(), [] (const auto& a, const auto& b) { return a.first == b.first; }), cuts.end());
    if (cuts.empty())
        return out;
    if (cuts.front().first > 0)
        out.push_back ({ "Start", 0, cuts.front().first });
    for (std::size_t i = 0; i < cuts.size(); ++i)
        out.push_back ({ cuts[i].second, cuts[i].first, i + 1 < cuts.size() ? cuts[i + 1].first : end });
    return out;
}

Song makeSectionSong (const Song& song, const Section& section)
{
    auto copy = song;
    copy.loopStart = section.start;
    copy.loopEnd = section.end;
    return copy;
}

Options sectionOptions (Options o)
{
    o.range = Range::selection;
    o.loops = 1;
    o.sections = false;
    return o;
}

bool exportsSections (const Song& song, const Options& o)
{
    return o.sections && o.range == Range::song && ! o.master && ! markerSections (song).empty();
}

std::string sectionBaseName (const std::string& base, int number, int count, const std::string& name)
{
    char digits[16];
    std::snprintf (digits, sizeof (digits), count >= 100 ? "%03d" : "%02d", number);
    const auto clean = legalName (name);
    return base + " - " + digits + (clean.empty() ? "" : " " + clean);
}

int fileCount (const Song& song, const Options& o, const std::vector<int>& onlyTracks)
{
    const auto perRender = (int) formatsOf (o).size();
    const auto parts = [&] (const Song& s, const Options& with) {
        if (with.stems == Stems::none)
            return 1;
        const auto stems = with.stems == Stems::mixerTracks ? (int) stemTracks (s, onlyTracks).size() : (int) stemChannels (s, with.range).size();
        return stems + (with.stemsIncludeMaster ? 1 : 0);
    };
    if (! exportsSections (song, o))
        return parts (song, o) * perRender;
    int files = 0;
    for (const auto& section : markerSections (song))
        files += parts (makeSectionSong (song, section), sectionOptions (o));
    return files * perRender;
}

// --- export presets (round 190ap) -------------------------------------------------------------------------------------
std::string cleanPresetName (const std::string& name)
{
    std::string s;
    for (const auto c : name)
        s += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    const auto trim = [] (std::string& t) {
        const auto first = t.find_first_not_of (' ');
        t = first == std::string::npos ? std::string() : t.substr (first, t.find_last_not_of (' ') - first + 1);
    };
    trim (s);
    // ("[x]" is a block's header in the file)
    while (! s.empty() && s.front() == '[')
        s.erase (s.begin());
    while (! s.empty() && s.back() == ']')
        s.pop_back();
    trim (s);
    if (s.size() > 60)
    {
        auto cut = (std::size_t) 60;
        while (cut > 0 && ((unsigned char) s[cut] & 0xC0) == 0x80)
            --cut;
        s = s.substr (0, cut);
        trim (s);
    }
    return s;
}

std::string presetsToText (const std::vector<Preset>& presets)
{
    std::string text;
    for (const auto& p : presets)
        if (const auto name = cleanPresetName (p.name); ! name.empty())
            text += "[" + name + "]\n" + toText (p.options) + "\n";
    return text;
}

std::vector<Preset> presetsFromText (const std::string& text)
{
    std::vector<Preset> out;
    std::string name, block;
    const auto flush = [&] {
        if (! name.empty())
            out = withPreset (std::move (out), { name, fromText (block) });
        name.clear();
        block.clear();
    };
    std::size_t at = 0;
    while (at <= text.size())
    {
        auto end = text.find ('\n', at);
        if (end == std::string::npos)
            end = text.size();
        auto line = text.substr (at, end - at);
        at = end + 1;
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            flush();
            name = cleanPresetName (line.substr (1, line.size() - 2));
        }
        else if (! name.empty())
            block += line + "\n";
    }
    flush();
    return out;
}

std::vector<Preset> withPreset (std::vector<Preset> presets, Preset preset)
{
    preset.name = cleanPresetName (preset.name);
    if (preset.name.empty())
        return presets;
    // (a preset is a set of choices for any project: the one-off parts of an export do not belong to it)
    preset.options.master = false;
    preset.options = clamped (preset.options);
    presets = withoutPreset (std::move (presets), preset.name);
    presets.push_back (std::move (preset));
    std::stable_sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b) {
        const auto la = lowerAscii (a.name), lb = lowerAscii (b.name);
        return la != lb ? la < lb : a.name < b.name;
    });
    return presets;
}

std::vector<Preset> withoutPreset (std::vector<Preset> presets, const std::string& name)
{
    const auto wanted = lowerAscii (cleanPresetName (name));
    presets.erase (std::remove_if (presets.begin(), presets.end(), [&] (const Preset& p) { return lowerAscii (p.name) == wanted; }), presets.end());
    return presets;
}

std::string lastExportToText (const LastExport& last)
{
    std::string file;
    for (const auto c : last.file)
        file += (c == '\n' || c == '\r') ? ' ' : c;
    return "file=" + file + "\n" + toText (last.options);
}

LastExport lastExportFromText (const std::string& text)
{
    LastExport last;
    // (the path is read by hand: it may hold "=" and anything else)
    std::size_t at = 0;
    while (at < text.size())
    {
        auto end = text.find ('\n', at);
        if (end == std::string::npos)
            end = text.size();
        auto line = text.substr (at, end - at);
        at = end + 1;
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare (0, 5, "file=") == 0)
        {
            last.file = line.substr (5);
            break;
        }
    }
    last.options = fromText (text);
    last.options.master = false;
    return last;
}

std::string sameAsLastFile (const LastExport& last, const std::string& projectName)
{
    const auto name = legalName (projectName);
    if (last.file.empty() || name.empty())
        return {};
    const auto slash = last.file.find_last_of ("/\\");
    if (slash == std::string::npos)
        return {};
    return last.file.substr (0, slash + 1) + name + extension (last.options.format);
}

void applyTail (std::vector<float>& left, std::vector<float>& right, std::int64_t loopFrames, TailMode mode)
{
    const auto frames = (std::int64_t) left.size();
    if (mode == TailMode::leave || loopFrames <= 0 || frames <= loopFrames)
    {
        if (mode != TailMode::leave && loopFrames > 0 && frames < loopFrames) // render ended early: pad to the length
        {
            left.resize ((std::size_t) loopFrames, 0.0f);
            right.resize ((std::size_t) loopFrames, 0.0f);
        }
        return;
    }

    if (mode == TailMode::wrap)
        for (auto i = loopFrames; i < frames; ++i)
        {
            const auto j = (std::size_t) (i % loopFrames);
            left[j] += left[(std::size_t) i];
            if ((std::size_t) i < right.size())
                right[j] += right[(std::size_t) i];
        }

    left.resize ((std::size_t) loopFrames);
    right.resize ((std::size_t) loopFrames);
}

float normalizeGain (float peak) noexcept
{
    return peak > 1.0e-9f ? 1.0f / peak : 1.0f;
}

void toChannels (float* left, const float* right, int n, Channels mode) noexcept
{
    switch (mode)
    {
        case Channels::monoMerged:
            for (int i = 0; i < n; ++i)
                left[i] = 0.5f * (left[i] + right[i]);
            break;
        case Channels::monoRight:
            for (int i = 0; i < n; ++i)
                left[i] = right[i];
            break;
        case Channels::stereo:
        case Channels::monoLeft: break;
    }
}

std::string formatDuration (double seconds)
{
    const auto total = (long long) std::llround (std::max (0.0, seconds));
    const auto h = total / 3600, m = (total / 60) % 60, s = total % 60;
    char text[32];
    if (h > 0)
        std::snprintf (text, sizeof (text), "%lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf (text, sizeof (text), "%lld:%02lld", m, s);
    return text;
}

namespace
{
void put32 (std::vector<std::uint8_t>& b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back ((std::uint8_t) (v >> (8 * i)));
}
void put16 (std::vector<std::uint8_t>& b, std::uint16_t v)
{
    b.push_back ((std::uint8_t) v);
    b.push_back ((std::uint8_t) (v >> 8));
}
void putTag (std::vector<std::uint8_t>& b, const char* tag)
{
    b.insert (b.end(), tag, tag + 4);
}
void putFloat (std::vector<std::uint8_t>& b, float f)
{
    std::uint32_t v = 0;
    std::memcpy (&v, &f, 4);
    put32 (b, v);
}
// a chunk: tag, size, body, pad byte
void chunk (std::vector<std::uint8_t>& out, const char* tag, const std::vector<std::uint8_t>& body)
{
    putTag (out, tag);
    put32 (out, (std::uint32_t) body.size());
    out.insert (out.end(), body.begin(), body.end());
    if ((body.size() & 1) != 0)
        out.push_back (0);
}
} // namespace

WavExtras wavExtras (const Song& song, const Options& o, double sampleRate)
{
    WavExtras x;
    std::int64_t musicTicks = 0, loopTicks = 0;
    const auto copy = makeRenderSong (song, o, 0, musicTicks, loopTicks);
    x.musicFrames = (std::int64_t) std::llround (copy.secondsForTicks (musicTicks) * sampleRate);
    x.loop = o.loopMarkers;
    x.acid = o.saveTempo;
    x.tempo = (float) song.bpm;
    x.numerator = std::max (1, song.beatsPerBar);
    x.denominator = meterUnit (song.beatUnit); // FL: 16 / steps per beat (round 190fg: the song meter's denominator)
    if (o.playlistMarkers)
    {
        // the range: the selection starts at the loop region (as makeRenderSong cuts it), one pass
        const auto selection = o.range == Range::selection && hasSelection (song);
        const auto start = selection ? song.loopStart : (std::int64_t) 0;
        const auto end = selection ? song.loopEnd : start + loopTicks;
        const auto startSeconds = song.secondsForTicks (start);
        for (const auto& m : song.markers())
            if (m.action == Marker::none && ! m.isSignature() && m.tick >= start && m.tick < end)
                x.markers.push_back ({ (std::int64_t) std::llround ((song.secondsForTicks (m.tick) - startSeconds) * sampleRate), m.name });
        std::stable_sort (x.markers.begin(), x.markers.end(), [] (const auto& a, const auto& b) { return a.frame < b.frame; });
    }
    return x;
}

std::vector<std::uint8_t> wavExtraChunks (const WavExtras& x, double sampleRate, std::int64_t dataFrames)
{
    std::vector<std::uint8_t> out;
    if (x.loop)
    {
        // smpl: manufacturer 0, product 0, sample period (ns), MIDI unity note 60, pitch fraction 0, SMPTE 0 / 0,
        // one loop, no sampler data; the loop: id 0x20000, type 0x400, start 0, end = last frame of the music
        std::vector<std::uint8_t> b;
        put32 (b, 0);
        put32 (b, 0);
        put32 (b, (std::uint32_t) std::llround (1.0e9 / std::max (1.0, sampleRate)));
        put32 (b, 60);
        put32 (b, 0);
        put32 (b, 0);
        put32 (b, 0);
        put32 (b, 1);
        put32 (b, 0);
        put32 (b, 0x20000);
        put32 (b, 0x400);
        put32 (b, 0);
        put32 (b, (std::uint32_t) std::max<std::int64_t> (0, x.musicFrames - 1));
        put32 (b, 0);
        put32 (b, 0);
        chunk (out, "smpl", b);
    }
    if (x.acid)
    {
        // acid: flags 4, root note 60, 0x80, 0.0, beats, meter (FL writes the numerator first), tempo
        std::vector<std::uint8_t> b;
        put32 (b, 4);
        put16 (b, 60);
        put16 (b, 0x80);
        putFloat (b, 0.0f);
        put32 (b, (std::uint32_t) std::llround ((double) dataFrames * x.tempo / (std::max (1.0, sampleRate) * 60.0)));
        put16 (b, (std::uint16_t) x.numerator);
        put16 (b, (std::uint16_t) x.denominator);
        putFloat (b, x.tempo);
        chunk (out, "acid", b);
    }
    if (! x.markers.empty())
    {
        // cue: ids 0x10000 + index, each at its frame in the "data" chunk
        std::vector<std::uint8_t> cue, adtl, tlst;
        put32 (cue, (std::uint32_t) x.markers.size());
        putTag (adtl, "adtl");
        for (std::size_t i = 0; i < x.markers.size(); ++i)
        {
            const auto id = (std::uint32_t) (0x10000 + i);
            const auto frame = (std::uint32_t) std::max<std::int64_t> (0, x.markers[i].frame);
            put32 (cue, id);
            put32 (cue, frame);
            putTag (cue, "data");
            put32 (cue, 0);
            put32 (cue, 0);
            put32 (cue, frame);
            // labl: the name, or "Marker <index>"
            auto name = x.markers[i].name.empty() ? "Marker " + std::to_string (i) : x.markers[i].name;
            std::vector<std::uint8_t> l;
            put32 (l, id);
            l.insert (l.end(), name.begin(), name.end());
            l.push_back (0);
            chunk (adtl, "labl", l);
        }
        chunk (out, "cue ", cue);
        chunk (out, "LIST", adtl);
        // tlst (FL's trigger list, up to 125 markers): 'cue ', the id − 1 (sic), 1, 0xff, note 60 + index (≤ 131),
        // 0x90, then zeros
        if (x.markers.size() < 0x7e)
        {
            put32 (tlst, (std::uint32_t) x.markers.size());
            for (std::size_t i = 0; i < x.markers.size(); ++i)
            {
                putTag (tlst, "cue ");
                put32 (tlst, (std::uint32_t) (0x10000 + i - 1));
                put32 (tlst, 1);
                tlst.push_back (0xff);
                tlst.push_back ((std::uint8_t) std::min<std::size_t> (0x3c + i, 0x83));
                tlst.push_back (0x90);
                tlst.push_back (0);
                put32 (tlst, 0);
                put32 (tlst, 0);
            }
            chunk (out, "tlst", tlst);
        }
    }
    return out;
}

bool appendWavChunks (const std::string& path, const std::vector<std::uint8_t>& chunks)
{
    if (chunks.empty())
        return true;
    std::fstream f (path, std::ios::in | std::ios::out | std::ios::binary);
    if (! f)
        return false;
    char head[12] {};
    f.read (head, 12);
    if (! f || std::memcmp (head, "RIFF", 4) != 0 || std::memcmp (head + 8, "WAVE", 4) != 0)
        return false;
    f.seekg (0, std::ios::end);
    auto size = (std::uint64_t) f.tellg();
    std::vector<std::uint8_t> tail;
    if ((size & 1) != 0) // (the last chunk's pad byte)
        tail.push_back (0);
    tail.insert (tail.end(), chunks.begin(), chunks.end());
    if (size + tail.size() - 8 > 0xffffffffull)
        return false;
    f.seekp ((std::streamoff) size);
    f.write ((const char*) tail.data(), (std::streamsize) tail.size());
    size += tail.size();
    std::uint8_t riff[4];
    for (int i = 0; i < 4; ++i)
        riff[i] = (std::uint8_t) ((size - 8) >> (8 * i));
    f.seekp (4);
    f.write ((const char*) riff, 4);
    return (bool) f;
}

std::int64_t wavDataFrames (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    char head[12] {};
    f.read (head, 12);
    if (! f || std::memcmp (head, "RIFF", 4) != 0 || std::memcmp (head + 8, "WAVE", 4) != 0)
        return -1;
    std::uint32_t align = 0;
    for (;;)
    {
        std::uint8_t h[8] {};
        f.read ((char*) h, 8);
        if (! f)
            return -1;
        const auto size = (std::uint32_t) h[4] | (std::uint32_t) h[5] << 8 | (std::uint32_t) h[6] << 16 | (std::uint32_t) h[7] << 24;
        if (std::memcmp (h, "fmt ", 4) == 0 && size >= 16)
        {
            std::uint8_t fmt[16] {};
            f.read ((char*) fmt, 16);
            align = (std::uint32_t) fmt[12] | (std::uint32_t) fmt[13] << 8;
            f.seekg ((std::streamoff) (size - 16 + (size & 1)), std::ios::cur);
            continue;
        }
        if (std::memcmp (h, "data", 4) == 0)
            return align > 0 ? (std::int64_t) (size / align) : -1;
        f.seekg ((std::streamoff) (size + (size & 1)), std::ios::cur);
    }
}

bool appendWavExtras (const std::string& path, const WavExtras& x, double sampleRate)
{
    if (! x.loop && ! x.acid && x.markers.empty())
        return true;
    const auto frames = wavDataFrames (path);
    if (frames < 0)
        return false;
    return appendWavChunks (path, wavExtraChunks (x, sampleRate, frames));
}
} // namespace lynx::exportopts
