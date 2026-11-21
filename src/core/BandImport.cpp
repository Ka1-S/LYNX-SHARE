#include "core/BandImport.h"
#include "core/SectionOps.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

#include "core/KeyFollow.h"
#include "core/MissingFiles.h"
#include "core/ProjectKey.h"
#include "core/SimpleTracks.h"
#include "core/SynthPresets.h"

namespace lynx
{
namespace
{
std::string lowercase (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

bool has (const std::string& name, std::initializer_list<const char*> words)
{
    return std::any_of (words.begin(), words.end(), [&name] (const char* w) { return name.find (w) != std::string::npos; });
}

std::string joinPath (const std::string& folder, const std::string& rel)
{
    if (folder.empty())
        return rel;
    const auto last = folder.back();
    return last == '/' || last == '\\' ? folder + rel : folder + "/" + rel;
}

std::string parentOf (const std::string& path)
{
    auto p = path;
    while (! p.empty() && (p.back() == '/' || p.back() == '\\'))
        p.pop_back();
    const auto slash = p.find_last_of ("/\\");
    return slash == std::string::npos ? std::string() : p.substr (0, slash);
}

std::string stemOf (std::string file)
{
    while (! file.empty() && (file.back() == '/' || file.back() == '\\'))
        file.pop_back();
    const auto slash = file.find_last_of ("/\\");
    auto s = file.substr (slash == std::string::npos ? 0 : slash + 1);
    if (const auto dot = s.find_last_of ('.'); dot != std::string::npos && dot > 0)
        s.resize (dot);
    return s;
}

// Logic's fader byte: 90 = 0 dB, 127 = +6 dB (Logic's manual); between them and below, the square law that meets both
// points (40·log10(v / 90) dB: +5.99 dB at 127). Approximate — the curve between the points is not published.
float faderGain (int v) noexcept
{
    if (v <= 0)
        return 0.0f;
    const auto g = (double) v / 90.0;
    return (float) (g * g);
}

void note (FlpImportReport* report, std::map<std::string, int> FlpImportReport::*list, const std::string& what, int n = 1)
{
    if (report != nullptr && n > 0)
        (report->*list)[what] += n;
}
} // namespace

std::string bandPresetFor (const std::string& patchName)
{
    const auto n = lowercase (patchName);
    if (has (n, { "sub bass", "808" }))
        return "808 Bass";
    if (has (n, { "bass" }))
        return "Reese Bass";
    if (has (n, { "guitar", "harp", "pizz", "pluck", "koto", "pipa", "guzheng", "banjo", "ukulele", "mandolin", "arp" }))
        return "Pluck";
    if (has (n, { "string", "violin", "viola", "cello", "erhu", "orchestra", "choir", "pad", "ambient", "atmos", "voices", "ensemble" }))
        return "Pad";
    if (has (n, { "piano", "grand", "keys", "rhodes", "wurli", "clav", "organ", "electric piano", "bell", "glock", "celest",
                  "marimba", "vibra", "xylo", "music box", "chime" }))
        return "Bell";
    if (has (n, { "saw", "trance", "supersaw" }))
        return "Supersaw";
    if (has (n, { "riser", "sweep", "noise", "fx", "uplifter" }))
        return "Noise (risers)";
    if (has (n, { "lead", "synth", "melody", "flute", "brass", "horn", "trumpet", "trombone", "sax", "clarinet", "oboe",
                  "bassoon", "woodwind", "whistle", "square", "sine" }))
        return "Lead";
    return "Default";
}

bool bandDrumKitName (const std::string& patchName)
{
    const auto n = lowercase (patchName);
    return has (n, { "drum", " kit", "drummer", "percussion", "beat", "808 kit", "trap kit" }) || n == "kit";
}

bool bandDrumTrack (const band::Track& track)
{
    if (track.audio)
        return false;
    // the patch's category / the instrument plugin when the project names them (GarageBand for iOS 2.3+: "808 Flex" and
    // "Latin" are Drums patches), else the track's name
    if (track.category == "Drums" || track.category == "Smart Drums" || track.category == "Drummer" || track.unit == "Drum Kit"
        || track.unit == "Drum Kits")
        return true;
    return bandDrumKitName (track.name);
}

InstrumentKind bandDrumFor (int key) noexcept
{
    switch (key)
    {
        case 35:
        case 36: return InstrumentKind::kick;
        case 37: // side stick
        case 38:
        case 40:
        case 41: // toms (no tom among the built-in drums: the snare's body is the nearest)
        case 43:
        case 45:
        case 47:
        case 48:
        case 50: return InstrumentKind::snare;
        case 39: return InstrumentKind::clap;
        default: return InstrumentKind::hat; // hats, cymbals, shakers and the rest
    }
}

Song importBand (const band::Project& project, const BandImportOptions& options)
{
    auto* report = options.common.report;
    Song song;
    song.channels.clear();
    song.patterns.clear();
    song.playlist.clear();
    song.mixer = Song::defaultMixer (0);
    song.mode = PlayMode::song;
    song.info.title = project.title;
    song.bpm = std::clamp (project.bpm > 0.0 ? project.bpm : 120.0, minTempo, maxTempo);
    song.beatsPerBar = std::clamp (project.numerator, 1, 32);
    const auto unit = project.denominator;
    song.beatUnit = unit == 2 || unit == 4 || unit == 8 || unit == 16 ? unit : 4;
    if (project.keyRoot >= 0)
        projectkey::set (song, keyfollow::projectKeyFor ({ project.keyRoot, project.keyMinor }));
    if (! project.tempoChanges.empty())
        note (report, &FlpImportReport::notReproduced, "GarageBand: tempo changes (the song plays at its first tempo)", (int) project.tempoChanges.size());

    // song sections (GarageBand for iOS) and markers; signature changes
    int sections = 0;
    for (const auto& [tick, name] : project.markers)
    {
        Marker m;
        m.tick = tick;
        m.name = name.empty() ? "Section " + std::to_string (++sections) : name;
        song.addMarker (m);
    }
    if (sections > 0)
        note (report, &FlpImportReport::converted, "GarageBand: song sections (as markers; their letters are not read)", sections);
    for (const auto& [tick, meter] : project.meters)
    {
        Marker m;
        m.tick = tick;
        m.numerator = meter.first;
        m.denominator = meter.second;
        m.action = Marker::timeSignature;
        song.addMarker (m);
    }

    const auto secondsToTicks = song.bpm / 60.0 * ppq;
    const auto sampleRate = project.sampleRate > 0.0 ? project.sampleRate : 44100.0;
    const auto resolve = [&] (const std::string& file) -> std::string {
        const auto& folder = options.packageFolder;
        for (const auto& candidate : { joinPath (folder, "Media/" + file), joinPath (folder, "Media/Audio Files/" + file),
                                       joinPath (folder, "Audio Files/" + file), joinPath (folder, file), joinPath (parentOf (folder), file) })
        {
            if (const auto r = options.common.relocations.find (candidate); r != options.common.relocations.end() && ! r->second.empty())
                return r->second;
            if (options.common.fileExists && options.common.fileExists (candidate))
                return candidate;
        }
        return {};
    };

    std::map<std::string, int> missingSeen;
    // a Sampler instrument the user made (GarageBand for iOS's Sampler: Media/Sampler/Sampler Instruments/<name>.exs and
    // its recordings in Media/Sampler/Sampler Files): the zones whose samples are in the package
    const auto samplerZones = [&] (const band::Track& track, const std::string& trackName) {
        std::vector<SampleZone> out;
        if (track.samplerInstrument.empty() || ! options.common.readFile)
            return out;
        const auto exs = band::readExs (options.common.readFile (joinPath (options.packageFolder, "Media/Sampler/Sampler Instruments/" + track.samplerInstrument)));
        if (! exs)
            return out; // (a library instrument: not in the package)
        for (const auto& z : *exs)
        {
            const auto rel = "Media/Sampler/Sampler Files/" + z.sample;
            auto path = joinPath (options.packageFolder, rel);
            if (const auto r = options.common.relocations.find (path); r != options.common.relocations.end() && ! r->second.empty())
                path = r->second;
            else if (! options.common.fileExists || ! options.common.fileExists (path))
            {
                if (report != nullptr)
                {
                    if (missingSeen[path]++ == 0)
                        ++report->missingSamples;
                    addMissingFile (report->missingFiles, path, "Sampler: " + trackName);
                }
                continue;
            }
            SampleZone zone;
            zone.path = path;
            zone.rootKey = z.rootKey;
            zone.lowKey = std::min (z.lowKey, z.highKey);
            zone.highKey = std::max (z.lowKey, z.highKey);
            zone.lowVelocity = std::min (z.lowVelocity, z.highVelocity);
            zone.highVelocity = std::max (z.lowVelocity, z.highVelocity);
            zone.tune = (float) z.fineTune / 100.0f;
            out.push_back (std::move (zone));
        }
        return out;
    };

    for (const auto& track : project.tracks)
    {
        auto zones = samplerZones (track, track.name);
        const auto drums = zones.empty() && bandDrumTrack (track);
        simpletracks::Request q;
        q.name = track.name;
        if (track.audio)
            q.kind = simpletracks::Kind::audio;
        else if (! zones.empty())
        {
            q.kind = simpletracks::Kind::sampler;
            ChannelDef sampler;
            sampler.instrument = InstrumentKind::sampler;
            sampler.samplePath = zones.front().path;
            sampler.zones = std::move (zones);
            sampler.sampler.release = 0.1f; // (held notes, not one-shots: GarageBand's Sampler plays while a key is down)
            q.channel = std::move (sampler);
            note (report, &FlpImportReport::approximated,
                  "GarageBand Sampler instrument \"" + track.name + "\" (its own samples, keys and tuning; the Sampler's envelope, loop and filter are not read)");
        }
        else if (drums)
        {
            q.kind = simpletracks::Kind::drums;
            for (const auto kind : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::clap, InstrumentKind::hat })
            {
                ChannelDef c;
                c.instrument = kind;
                c.name = kind == InstrumentKind::kick ? "Kick" : kind == InstrumentKind::snare ? "Snare" : kind == InstrumentKind::clap ? "Clap" : "Hat";
                q.kit.push_back (std::move (c));
            }
            note (report, &FlpImportReport::approximated, "GarageBand drum kit \"" + track.name + "\" (LYNX's built-in kick, snare, clap and hat)");
        }
        else
        {
            q.kind = simpletracks::Kind::instrument;
            ChannelDef synth;
            synth.instrument = InstrumentKind::synth;
            const auto preset = bandPresetFor (track.name);
            for (const auto& [n, s] : synthPresets())
                if (n == preset)
                    synth.synth = s;
            q.channel = std::move (synth);
            note (report, &FlpImportReport::approximated, "GarageBand instrument \"" + track.name + "\" (LYNX's built-in synth, " + preset + " preset)");
        }
        const auto made = simpletracks::addTrack (song, q);
        if (! made.ok())
        {
            note (report, &FlpImportReport::notReproduced, "GarageBand: tracks past LYNX's track or insert count");
            continue;
        }
        const auto colour = simpletracks::defaultColour (q.kind);
        auto reuse = drums ? made.pattern : -1; // (the Drums track's own pattern takes its first region)
        if (drums) // (a Drums track starts with an empty one-bar pattern at the song's start: its regions take its place)
            song.playlist.erase (std::remove_if (song.playlist.begin(), song.playlist.end(),
                                                 [&made] (const Clip& c) { return c.track == made.track && c.kind == ClipKind::pattern; }),
                                 song.playlist.end());

        // fader, pan, mute
        if (made.insert > 0 && made.insert < (int) song.mixer.size())
        {
            auto& m = song.mixer[(std::size_t) made.insert];
            if (track.volumeKnown)
            {
                const auto gain = faderGain (track.volume);
                m.volume = faderPosition (std::min (gain, maxFaderGain));
                m.pan = std::clamp ((float) (track.pan - 64) / 63.0f, -1.0f, 1.0f);
                if (gain > maxFaderGain * 1.0001f)
                    note (report, &FlpImportReport::partialEffects, "GarageBand: track level above +5.6 dB (capped)");
            }
        }
        if (track.muted)
        {
            song.playlistTracks[(std::size_t) made.track].muted = true;
            note (report, &FlpImportReport::converted, "GarageBand: track mute");
        }

        std::map<std::string, int> audioChannels; // file → channel
        for (const auto& region : track.regions)
        {
            if (region.audio)
            {
                const auto path = resolve (region.file);
                if (path.empty())
                {
                    const auto shown = joinPath (options.packageFolder, "Media/" + region.file);
                    if (report != nullptr)
                    {
                        if (missingSeen[shown]++ == 0)
                            ++report->missingSamples;
                        addMissingFile (report->missingFiles, shown, "Audio clip: " + track.name);
                    }
                    continue;
                }
                auto channel = audioChannels.find (path);
                if (channel == audioChannels.end())
                {
                    ChannelDef c;
                    c.name = stemOf (region.file);
                    c.instrument = InstrumentKind::audioClip;
                    c.samplePath = path;
                    c.mixerTrack = made.insert;
                    c.colour = colour;
                    song.channels.push_back (std::move (c));
                    channel = audioChannels.emplace (path, (int) song.channels.size() - 1).first;
                }
                Clip clip;
                clip.kind = ClipKind::audio;
                clip.channel = channel->second;
                clip.track = made.track;
                clip.start = region.start;
                const auto offset = (double) region.trimSamples / sampleRate * secondsToTicks;
                clip.offset = (std::int64_t) std::floor (offset);
                clip.offsetFraction = (float) (offset - (double) clip.offset);
                clip.length = std::max<std::int64_t> (1, std::llround ((double) region.lengthSamples / sampleRate * secondsToTicks));
                song.playlist.push_back (clip);
                note (report, &FlpImportReport::converted, "GarageBand: audio region");
                if (region.fades)
                    note (report, &FlpImportReport::notReproduced, "GarageBand: audio region fades");
                continue;
            }
            if (region.folder)
            {
                note (report, &FlpImportReport::notReproduced, "GarageBand: folder / take folder regions (their inner regions)");
                continue;
            }
            if (region.patternContent)
            {
                note (report, &FlpImportReport::notReproduced, "GarageBand: Smart Drums, Drummer and pattern regions (\"" + region.name + "\")");
                if (region.notes.empty())
                    continue;
            }
            Pattern p;
            p.name = region.name.empty() ? track.name : region.name;
            p.colour = colour;
            p.lengthTicks = std::max<std::int64_t> (1, region.length);
            for (const auto& n : region.notes)
            {
                Note out;
                out.start = n.tick;
                out.length = std::clamp<std::int64_t> (n.length, 1, std::max<std::int64_t> (1, p.lengthTicks - n.tick));
                out.key = n.key;
                out.velocity = std::clamp ((float) n.velocity / 127.0f, 0.0f, 1.0f);
                if (drums)
                {
                    const auto kind = bandDrumFor (n.key);
                    const auto slot = kind == InstrumentKind::kick ? 0 : kind == InstrumentKind::snare ? 1 : kind == InstrumentKind::clap ? 2 : 3;
                    out.channel = slot < (int) made.channels.size() ? made.channels[(std::size_t) slot] : made.channels.front();
                    out.key = 60;
                }
                else
                    out.channel = made.channels.empty() ? 0 : made.channels.front();
                p.notes.push_back (out);
            }
            p.sortNotes();
            Clip clip;
            clip.kind = ClipKind::pattern;
            if (reuse >= 0 && reuse < (int) song.patterns.size())
            {
                song.patterns[(std::size_t) reuse] = std::move (p);
                clip.pattern = reuse;
                reuse = -1;
            }
            else
            {
                song.patterns.push_back (std::move (p));
                clip.pattern = (int) song.patterns.size() - 1;
            }
            clip.track = made.track;
            clip.start = region.start;
            clip.length = std::max<std::int64_t> (1, region.length);
            song.playlist.push_back (clip);
            note (report, &FlpImportReport::converted, "GarageBand: MIDI region");
            if (region.controllerEvents > 0)
                note (report, &FlpImportReport::notReproduced, "GarageBand: MIDI controllers (sustain pedal, pitch bend, modulation...)", region.controllerEvents);
        }
    }
    if (! project.tracks.empty())
        note (report, &FlpImportReport::notReproduced, "GarageBand: the tracks' effects and instrument settings (not converted)");
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    if (song.patterns.empty())
        song.patterns.push_back (Pattern {}); // (a song keeps at least one pattern)
    // round 190ol: GarageBand's song sections / arrangement markers also fill the Sections lane (each to the next one; the
    // markers stay, as before)
    if (sections > 0 || ! project.markers.empty())
        if (const auto made = sections::fromMarkers (song); made > 0)
            note (report, &FlpImportReport::converted, "GarageBand: song sections (also in the Sections lane)", made);
    return song;
}

std::optional<Song> importBandPackage (const BandImportOptions& options, std::string* error)
{
    auto fail = [error] (const std::string& why) -> std::optional<Song> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (! options.common.readFile)
        return fail ("cannot read the GarageBand project");
    const auto& folder = options.packageFolder;
    std::optional<std::vector<std::uint8_t>> song;
    std::string why;
    for (const auto* name : { "Alternatives/000/ProjectData", "projectData" })
    {
        const auto bytes = options.common.readFile (joinPath (folder, name));
        if (bytes.empty())
            continue;
        song = band::songFromProjectData (bytes, &why);
        if (song)
            break;
    }
    if (! song)
        return fail (why.empty() ? "no GarageBand song in this folder" : why);
    auto project = band::parseSong (*song, &why);
    if (! project)
        return fail (why);
    for (const auto* name : { "Output/metadata.plist", "Alternatives/000/MetaData.plist" })
        if (const auto bytes = options.common.readFile (joinPath (folder, name)); ! bytes.empty())
            band::applyMetadata (bytes, *project);
    // the name the user gave the file (GarageBand for iOS keeps "My Song N" as the summary's title)
    if (auto name = stemOf (folder); ! name.empty())
        project->title = name;
    return importBand (*project, options);
}
} // namespace lynx
