#include "engine/RemotePlaylistControl.h"
#include "engine/AudioEngine.h"
#include "engine/RemotePlaylistEdits.h"
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace lynx
{
namespace
{
using Field = std::pair<const char*, juce::var>;
juce::var object (std::initializer_list<Field> fields)
{
    auto* result = new juce::DynamicObject();
    for (const auto& [name, value] : fields) result->setProperty (name, value);
    return result;
}
juce::String text (const std::string& value) { return juce::String::fromUTF8 (value.c_str()); }
[[noreturn]] void fail (const juce::String& why) { throw std::runtime_error (why.toStdString()); }

void fields (const juce::var& value, std::initializer_list<std::string_view> allowed)
{
    const auto* obj = value.getDynamicObject();
    if (obj == nullptr) fail ("Playlist arguments must be an object");
    for (const auto& prop : obj->getProperties())
    {
        const auto key = prop.name.toString().toStdString();
        if (std::find (allowed.begin(), allowed.end(), key) == allowed.end())
            fail ("Unsupported Playlist field: " + prop.name.toString());
    }
}
double number (const juce::var& args, const char* key, double low, double high)
{
    const auto& value = args[key];
    if (! (value.isInt() || value.isInt64() || value.isDouble())) fail (juce::String (key) + " must be a number");
    const auto n = (double) value;
    if (! std::isfinite (n) || n < low || n > high) fail (juce::String (key) + " is outside its allowed range");
    return n;
}
std::int64_t integer (const juce::var& args, const char* key, std::int64_t low, std::int64_t high)
{
    const auto value = number (args, key, (double) low, (double) high);
    if (std::floor (value) != value) fail (juce::String (key) + " must be an integer");
    return (std::int64_t) value;
}
bool boolean (const juce::var& args, const char* key)
{
    if (! args[key].isBool()) fail (juce::String (key) + " must be true or false");
    return (bool) args[key];
}
juce::String string (const juce::var& args, const char* key)
{
    if (! args[key].isString()) fail (juce::String (key) + " must be a string");
    return args[key].toString();
}
std::uint64_t clipIdentity (const juce::var& edit)
{
    const auto value = string (edit, "clipId");
    if (value.length() != 16) fail ("clipId must be 16 hexadecimal digits");
    const auto id = uids::fromHex (value.toStdString());
    if (id == 0) fail ("clipId must be a nonzero Playlist clip identity");
    return id;
}

const char* kindName (ClipKind kind)
{
    switch (kind)
    {
        case ClipKind::pattern: return "pattern";
        case ClipKind::audio: return "audio";
        case ClipKind::automation: return "automation";
    }
    return "unknown";
}
juce::var clipValue (const Clip& clip, int index)
{
    return object ({ { "index", index }, { "clipId", text (uids::toHex (clip.uid)) }, { "kind", kindName (clip.kind) },
                    { "pattern", clip.pattern }, { "channel", clip.channel }, { "automation", clip.automation },
                    { "track", clip.track }, { "start", (juce::int64) clip.start }, { "length", (juce::int64) clip.length },
                    { "offset", (juce::int64) clip.offset }, { "offsetFraction", clip.offsetFraction },
                    { "fadeIn", (juce::int64) clip.fadeIn }, { "fadeOut", (juce::int64) clip.fadeOut },
                    { "gain", clip.gain }, { "pan", clip.pan }, { "muted", clip.muted }, { "pitch", clip.pitch },
                    { "reversed", clip.reversed }, { "stretch", clip.stretch },
                    { "crossfadeIn", (juce::int64) clip.crossfadeIn }, { "crossfadeOut", (juce::int64) clip.crossfadeOut } });
}

remote_playlist::Patch parsePatch (const juce::var& value)
{
    remote_playlist::Patch patch;
    constexpr auto max = remote_playlist::maxTick;
    if (value.hasProperty ("track")) patch.track = (int) integer (value, "track", 0, remote_playlist::maxTrack);
    if (value.hasProperty ("start")) patch.start = integer (value, "start", 0, max);
    if (value.hasProperty ("length")) patch.length = integer (value, "length", 1, max);
    if (value.hasProperty ("offset")) patch.offset = integer (value, "offset", 0, max);
    if (value.hasProperty ("fadeIn")) patch.fadeIn = integer (value, "fadeIn", 0, max);
    if (value.hasProperty ("fadeOut")) patch.fadeOut = integer (value, "fadeOut", 0, max);
    if (value.hasProperty ("offsetFraction")) patch.offsetFraction = (float) number (value, "offsetFraction", -0.5, 0.5);
    if (value.hasProperty ("gain")) patch.gain = (float) number (value, "gain", 0, 64);
    if (value.hasProperty ("pan")) patch.pan = (float) number (value, "pan", -1, 1);
    if (value.hasProperty ("pitch")) patch.pitch = (float) number (value, "pitch", -12, 12);
    if (value.hasProperty ("stretch")) patch.stretch = number (value, "stretch", 0.01, 100);
    if (value.hasProperty ("muted")) patch.muted = boolean (value, "muted");
    if (value.hasProperty ("reversed")) patch.reversed = boolean (value, "reversed");
    return patch;
}

std::vector<remote_playlist::Edit> parseEdits (const juce::var& args)
{
    fields (args, { "edits", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    const auto* input = args["edits"].getArray();
    if (input == nullptr || input->isEmpty() || input->size() > 128) fail ("edits must contain between 1 and 128 Playlist edits");
    std::vector<remote_playlist::Edit> edits;
    for (const auto& value : *input)
    {
        fields (value, { "action", "clipId", "kind", "pattern", "channel", "automation", "track", "start", "length", "offset",
                        "fadeIn", "fadeOut", "offsetFraction", "gain", "pan", "pitch", "stretch", "muted", "reversed" });
        remote_playlist::Edit edit;
        const auto action = string (value, "action");
        if (action == "add") edit.action = remote_playlist::Edit::Action::add;
        else if (action == "update") edit.action = remote_playlist::Edit::Action::update;
        else if (action == "duplicate") edit.action = remote_playlist::Edit::Action::duplicate;
        else if (action == "remove") edit.action = remote_playlist::Edit::Action::remove;
        else fail ("Playlist action must be add, update, duplicate or remove");
        edit.patch = parsePatch (value);
        if (edit.action == remote_playlist::Edit::Action::add)
        {
            if (value.hasProperty ("clipId")) fail ("Add allocates its own clipId");
            const auto kind = string (value, "kind");
            const char* source = nullptr;
            if (kind == "pattern") { edit.kind = ClipKind::pattern; source = "pattern"; }
            else if (kind == "audio") { edit.kind = ClipKind::audio; source = "channel"; }
            else if (kind == "automation") { edit.kind = ClipKind::automation; source = "automation"; }
            else fail ("Playlist kind must be pattern, audio or automation");
            edit.source = (int) integer (value, source, 0, 1000000);
            for (const auto* key : { "pattern", "channel", "automation" })
                if (std::string_view (key) != source && value.hasProperty (key)) fail ("A clip may name only the source matching its kind");
        }
        else
        {
            edit.clipId = clipIdentity (value);
            for (const auto* key : { "kind", "pattern", "channel", "automation" })
                if (value.hasProperty (key)) fail ("Existing clip kind/source cannot be reassigned; remove and add instead");
        }
        edits.push_back (std::move (edit));
    }
    return edits;
}

juce::var inspect (const Song& song, const juce::var& args)
{
    fields (args, { "offset", "limit", "expectedProjectId", "expectedRevision", "expectedEpoch" });
    const auto offset = args.hasProperty ("offset") ? (std::size_t) integer (args, "offset", 0, 100000) : 0;
    const auto limit = args.hasProperty ("limit") ? (std::size_t) integer (args, "limit", 1, 1000) : 250;
    juce::Array<juce::var> clips, tracks, arrangements, patterns, audio, automations;
    for (std::size_t i = offset; i < std::min (song.playlist.size(), offset + limit); ++i)
        clips.add (clipValue (song.playlist[i], (int) i));
    auto trackCount = std::min ((std::size_t) remote_playlist::maxTrack + 1, song.playlistTracks.size());
    for (const auto& clip : song.playlist)
        if (clip.track >= 0 && clip.track <= remote_playlist::maxTrack) trackCount = std::max (trackCount, (std::size_t) clip.track + 1);
    for (std::size_t i = 0; i < trackCount; ++i)
    {
        const auto track = i < song.playlistTracks.size() ? song.playlistTracks[i] : PlaylistTrack {};
        const auto mode = track.mode == PlaylistTrackMode::audio ? "audio" : track.mode == PlaylistTrackMode::instrument ? "instrument" : "unassigned";
        tracks.add (object ({ { "index", (int) i }, { "name", text (track.name) }, { "muted", track.muted }, { "mode", mode },
                             { "mixer", track.mixerTrack }, { "channel", track.channel }, { "lockedToContent", track.lockedToContent } }));
    }
    constexpr std::size_t maxSources = 4096;
    for (std::size_t i = 0; i < std::min (song.arrangements.size(), maxSources); ++i)
    {
        const auto& arrangement = song.arrangements[i];
        const auto current = (int) i == song.currentArrangement;
        arrangements.add (object ({ { "index", (int) i }, { "arrangementId", text (uids::toHex (arrangement.uid)) },
                                   { "name", text (arrangement.name) }, { "current", current },
                                   { "clipCount", (juce::int64) (current ? song.playlist.size() : arrangement.clips.size()) } }));
    }
    for (std::size_t i = 0; i < std::min (song.patterns.size(), maxSources); ++i)
        patterns.add (object ({ { "index", (int) i }, { "name", text (song.patterns[i].name) },
                               { "length", (juce::int64) song.patterns[i].effectiveLength (song.meter()) } }));
    for (std::size_t i = 0; i < song.channels.size() && (std::size_t) audio.size() < maxSources; ++i)
        if (song.channels[i].instrument == InstrumentKind::audioClip)
            audio.add (object ({ { "index", (int) i }, { "name", text (song.channels[i].name) }, { "mixer", song.channels[i].mixerTrack } }));
    for (std::size_t i = 0; i < std::min (song.automations.size(), maxSources); ++i)
        automations.add (object ({ { "index", (int) i }, { "name", text (song.automations[i].name) },
                                  { "length", (juce::int64) song.automations[i].length() } }));
    const auto audioTotal = (std::size_t) std::count_if (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::audioClip; });
    return object ({ { "currentArrangement", song.currentArrangement }, { "arrangements", arrangements },
                     { "arrangementsTotal", (juce::int64) song.arrangements.size() }, { "tracks", tracks }, { "clips", clips },
                     { "totalClips", (juce::int64) song.playlist.size() }, { "offset", (juce::int64) offset }, { "limit", (juce::int64) limit },
                     { "hasMore", offset + (std::size_t) clips.size() < song.playlist.size() }, { "patterns", patterns },
                     { "audioChannels", audio }, { "automations", automations }, { "ppq", ppq },
                     { "patternsTotal", (juce::int64) song.patterns.size() }, { "audioChannelsTotal", (juce::int64) audioTotal },
                     { "automationsTotal", (juce::int64) song.automations.size() },
                     { "sourceListsTruncated", song.patterns.size() > maxSources || audioTotal > maxSources || song.automations.size() > maxSources || song.arrangements.size() > maxSources } });
}
} // namespace

bool isRemotePlaylistOperation (const juce::String& op) { return op == "playlist.inspect" || op == "playlist.edit"; }

juce::var dispatchRemotePlaylist (AudioEngine& engine, const juce::String& op, const juce::var& args, const std::function<bool()>& cancelled)
{
    if (op == "playlist.inspect") return inspect (engine.getSong(), args);
    if (op != "playlist.edit") fail ("Unsupported Playlist operation");
    if (engine.getTransport().playing.load() || engine.isRecording()) fail ("Stop playback and recording before editing the Playlist");
    auto transaction = remote_playlist::apply (engine.getSong(), parseEdits (args));
    const auto changed = transaction.song.playlist != engine.getSong().playlist;
    if (changed)
    {
        engine.editSongRemoteTransaction ([&] (Song& song) {
            song.playlist = std::move (transaction.song.playlist);
            song.nextUid = transaction.song.nextUid;
        }, cancelled);
    }
    juce::Array<juce::var> created;
    for (const auto id : transaction.created) created.add (text (uids::toHex (id)));
    return object ({ { "changed", changed }, { "createdClipIds", created }, { "updated", (juce::int64) transaction.updated },
                     { "removed", (juce::int64) transaction.removed }, { "totalClips", (juce::int64) engine.getSong().playlist.size() } });
}
} // namespace lynx
