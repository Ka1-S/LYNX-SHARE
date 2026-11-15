#include "core/ExportBundle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/ExportOptions.h"
#include "core/PatternBlocks.h"
#include "core/QuickRender.h"

namespace lynx::exportbundle
{
namespace
{
std::string twoDigits (int n)
{
    char buf[16];
    std::snprintf (buf, sizeof buf, "%02d", n);
    return buf;
}

std::string trackName (const Song& song, int track)
{
    if (track >= 0 && track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty())
        return song.playlistTracks[(std::size_t) track].name;
    return "Track " + std::to_string (track + 1);
}

bool hasPatternClip (const Song& song, const std::vector<int>& clips)
{
    return std::any_of (clips.begin(), clips.end(), [&song] (int i) {
        return i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::pattern;
    });
}
} // namespace

Song prepared (const Song& song)
{
    Song out = song;
    // Pattern Blocks (round 170f) play on no Playlist track: as clips on tracks of their own they become stems too
    if (patternblocks::active (out))
        patternblocks::toClips (out);
    out.mode = PlayMode::song;
    return out;
}

Plan plan (const Song& song, const std::string& projectName, const Options& options)
{
    Plan p;
    p.name = quickrender::safeFileName (projectName, "LYNX SHARE");
    p.folder = p.name + " (for other apps)";
    p.zip = options.zip;
    p.endTick = song.songLength();

    int number = 0;
    for (auto t : quickrender::exportTracks (song, false))
    {
        quickrender::Plan render;
        if (quickrender::planTrack (song, t, false, quickrender::Start::songStart, render) != quickrender::Result::ok || render.clips.empty())
            continue;
        TrackFile f;
        f.track = t;
        f.number = ++number;
        f.name = trackName (song, t);
        f.clips = render.clips;
        const auto stem = twoDigits (f.number) + " " + quickrender::safeFileName (f.name, "Track " + std::to_string (t + 1));
        f.path = "Stems/" + stem + ".wav";
        p.stems.push_back (f);
        if (hasPatternClip (song, f.clips) && ! trackNotes (song, f).empty())
        {
            auto m = f;
            m.path = "MIDI/" + stem + ".mid";
            p.midi.push_back (std::move (m));
        }
    }
    if (! songNotes (song).empty())
        p.allMidi = "MIDI/" + p.name + " (all tracks).mid";
    p.alsFolder = p.name + " Project";
    p.als = p.alsFolder + "/" + p.name + ".als";
    p.flp = p.name + ".flp";
    if (options.mixdown)
        p.mixdown = p.name + " - mixdown.wav";
    return p;
}

std::vector<Note> trackNotes (const Song& song, const TrackFile& track)
{
    if (! hasPatternClip (song, track.clips))
        return {};
    Song only = song;
    only.playlist.clear();
    for (auto i : track.clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::pattern)
            only.playlist.push_back (song.playlist[(std::size_t) i]);
    only.mode = PlayMode::song;
    return exportopts::midiExportNotes (only);
}

std::vector<Note> songNotes (const Song& song)
{
    // (FL's export falls back to the current pattern without pattern clips; a bundle of audio tracks has no MIDI)
    if (std::none_of (song.playlist.begin(), song.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::pattern && ! c.muted; }))
        return {};
    Song s = song;
    s.mode = PlayMode::song;
    return exportopts::midiExportNotes (s);
}

std::optional<KeySignature> keySignature (const Song& song)
{
    const auto key = projectkey::get (song);
    if (! key)
        return std::nullopt;
    const auto& d = projectkey::intervals (*key);
    const auto has = [&d] (int i) { return std::find (d.begin(), d.end(), i) != d.end(); };
    KeySignature k;
    k.minor = has (3) && ! has (4);
    // the major key with the same signature: the root itself, or a minor scale's relative major three semitones up
    const auto major = (key->root + (k.minor ? 3 : 0)) % 12;
    auto fifths = (major * 7) % 12; // C 0, G 1, D 2 … on the circle of fifths
    if (fifths > 6)
        fifths -= 12; // F −1, B♭ −2 … (F♯ / G♭ is written with sharps)
    k.sharps = fifths;
    return k;
}

std::string tempoText (const Song& song)
{
    char buf[32];
    const auto bpm = song.bpm;
    if (std::abs (bpm - std::round (bpm)) < 0.005)
        std::snprintf (buf, sizeof buf, "%d BPM", (int) std::lround (bpm));
    else
        std::snprintf (buf, sizeof buf, "%.2f BPM", bpm);
    return buf;
}

std::string keyText (const Song& song)
{
    const auto key = projectkey::get (song);
    return key ? projectkey::name (*key) : std::string ("none");
}

std::string readme (const Song& song, const Plan& plan, double seconds, const std::vector<std::string>& written,
                    const std::vector<std::string>& failed)
{
    std::string r;
    const auto line = [&r] (const std::string& s = {}) { r += s + "\r\n"; };
    line (plan.name + " - exported for other apps by LYNX SHARE");
    line();
    line ("Tempo:          " + tempoText (song));
    line ("Time signature: " + std::to_string (std::max (1, song.beatsPerBar)) + "/" + std::to_string (meterUnit (song.beatUnit)));
    line ("Key:            " + keyText (song));
    line ("Length:         " + exportopts::formatDuration (seconds) + " ("
          + std::to_string ((int) std::ceil (exportopts::lengthInBars (seconds, song.bpm, song.meter()) - 1e-9)) + " bars)");
    line();
    line ("Files:");
    for (const auto& w : written)
        line ("  " + w);
    if (! failed.empty())
    {
        line();
        line ("Not written:");
        for (const auto& f : failed)
            line ("  " + f);
    }
    line();
    line ("How to use them:");
    line ("- Any app: start a new project at " + tempoText (song) + " and drop every file of Stems at the very start");
    line ("  (bar 1). They all start at the song's start and have the same length, so they line up. The stems have no");
    line ("  Master effects: together they are the mix before the Master. The mixdown is the finished sound.");
    line ("- MIDI: each .mid file holds the tempo, the time signature and the key. Load it on an instrument track, or");
    line ("  load the \"all tracks\" file to get every part at once.");
    if (! plan.als.empty())
        line ("- Ableton Live 12: open \"" + plan.als + "\" (its samples are in that folder).");
    if (! plan.flp.empty())
        line ("- FL Studio: open \"" + plan.flp + "\". Sounds only LYNX SHARE has are in it as audio.");
    line ("- Online studios and other apps: import the stems as audio tracks and the .mid files as MIDI tracks.");
    return r;
}
} // namespace lynx::exportbundle
