#include "engine/CrossClipboard.h"

#include "engine/ProjectFile.h"

namespace lynx::crossclipboard
{
namespace
{
constexpr const char* names[] = { "notes", "clips", "steps", "channels", "mixerTracks", "points" };

// notes and steps travel in a Song of their own (the project's note format, MPE curves and all): one channel, one
// pattern per slot
Song carrier()
{
    Song s;
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    ChannelDef c;
    c.name = "clipboard";
    s.channels.push_back (c);
    return s;
}

juce::var songVar (const Song& song)
{
    return juce::JSON::parse (ProjectFile::toJson (song, false));
}

std::optional<Song> songOf (const juce::var& v)
{
    if (! v.isObject())
        return std::nullopt;
    juce::String error;
    return ProjectFile::fromJson (juce::JSON::toString (v, true), error);
}

juce::String firstLine (const juce::String& text, int& next)
{
    const auto end = text.indexOfChar ('\n');
    next = end < 0 ? text.length() : end + 1;
    return (end < 0 ? text : text.substring (0, end)).trimEnd();
}
} // namespace

const char* kindName (Kind kind) noexcept
{
    return names[(int) kind];
}

std::optional<Kind> kindFromName (const juce::String& name) noexcept
{
    for (int i = 0; i < (int) std::size (names); ++i)
        if (name == names[i])
            return (Kind) i;
    return std::nullopt;
}

juce::String toJson (const Payload& payload)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("lynxClipboard", formatVersion);
    root->setProperty ("kind", kindName (payload.kind));
    root->setProperty ("stamp", payload.stamp);
    root->setProperty ("pid", payload.pid);
    root->setProperty ("from", payload.from);
    switch (payload.kind)
    {
        case Kind::notes:
        {
            auto s = carrier();
            auto& p = s.patterns.emplace_back();
            p.notes = payload.notes;
            for (auto& n : p.notes)
                n.channel = 0;
            p.sortNotes();
            root->setProperty ("song", songVar (s));
            break;
        }
        case Kind::steps:
        {
            auto s = carrier();
            juce::Array<juce::var> copied;
            for (const auto& slot : payload.steps.slots)
            {
                auto& p = s.patterns.emplace_back();
                p.notes = slot.notes;
                for (auto& n : p.notes)
                    n.channel = 0;
                p.sortNotes();
                if (slot.loops)
                    p.channelLoops.push_back ({ 0, slot.loopSetting });
                copied.add (slot.copied);
            }
            if (s.patterns.empty())
                s.patterns.emplace_back();
            root->setProperty ("song", songVar (s));
            root->setProperty ("copied", copied);
            break;
        }
        case Kind::clips:
        case Kind::channels:
            root->setProperty ("song", songVar (payload.fragment));
            break;
        case Kind::mixerTracks:
        {
            juce::Array<juce::var> tracks;
            for (const auto& t : payload.mixerTracks)
                tracks.add (juce::JSON::parse (ProjectFile::mixerTrackStateToJson (t)));
            root->setProperty ("mixerTracks", tracks);
            break;
        }
        case Kind::points:
        {
            juce::Array<juce::var> points;
            for (const auto& p : payload.points.points)
                points.add (juce::Array<juce::var> { (juce::int64) p.tick, (double) p.value, (double) p.tension, (int) p.mode });
            root->setProperty ("points", points);
            root->setProperty ("span", (juce::int64) payload.points.span);
            break;
        }
    }
    return juce::JSON::toString (juce::var (root.release()), true);
}

std::optional<Payload> fromJson (const juce::String& json)
{
    const auto root = juce::JSON::parse (json);
    if (! root.isObject() || (int) root.getProperty ("lynxClipboard", 0) < 1 || (int) root.getProperty ("lynxClipboard", 0) > formatVersion)
        return std::nullopt;
    const auto kind = kindFromName (root["kind"].toString());
    if (! kind)
        return std::nullopt;
    Payload out;
    out.kind = *kind;
    out.stamp = root["stamp"].toString();
    out.pid = (int) root.getProperty ("pid", 0);
    out.from = root["from"].toString();
    switch (*kind)
    {
        case Kind::notes:
        {
            const auto s = songOf (root["song"]);
            if (! s || s->patterns.empty())
                return std::nullopt;
            out.notes = s->patterns.front().notes;
            break;
        }
        case Kind::steps:
        {
            const auto s = songOf (root["song"]);
            const auto* copied = root["copied"].getArray();
            if (! s || copied == nullptr || (int) s->patterns.size() < copied->size())
                return std::nullopt;
            for (int i = 0; i < copied->size(); ++i)
            {
                const auto& p = s->patterns[(std::size_t) i];
                ChannelStepsClipboard::Slot slot;
                slot.copied = (bool) (*copied)[i];
                slot.notes = p.notes;
                slot.loops = ! p.channelLoops.empty();
                slot.loopSetting = slot.loops ? p.channelLoops.front().setting : 0;
                out.steps.slots.push_back (std::move (slot));
            }
            break;
        }
        case Kind::clips:
        case Kind::channels:
        {
            auto s = songOf (root["song"]);
            if (! s)
                return std::nullopt;
            out.fragment = std::move (*s);
            break;
        }
        case Kind::mixerTracks:
        {
            const auto* tracks = root["mixerTracks"].getArray();
            if (tracks == nullptr)
                return std::nullopt;
            for (const auto& t : *tracks)
            {
                juce::String error;
                if (auto state = ProjectFile::mixerTrackStateFromJson (juce::JSON::toString (t, true), error))
                    out.mixerTracks.push_back (std::move (*state));
            }
            if (out.mixerTracks.empty())
                return std::nullopt;
            break;
        }
        case Kind::points:
        {
            const auto* points = root["points"].getArray();
            if (points == nullptr)
                return std::nullopt;
            for (const auto& p : *points)
                if (p.isArray() && p.size() >= 4)
                {
                    AutomationPoint point;
                    point.tick = juce::jmax<juce::int64> (0, (juce::int64) p[0]);
                    point.value = juce::jlimit (0.0f, 1.0f, (float) (double) p[1]);
                    point.tension = juce::jlimit (-1.0f, 1.0f, (float) (double) p[2]);
                    point.mode = (std::uint8_t) juce::jlimit (0, 255, (int) p[3]);
                    out.points.points.push_back (point);
                }
            out.points.span = juce::jmax<juce::int64> (0, (juce::int64) root.getProperty ("span", 0));
            break;
        }
    }
    return out;
}

juce::String clipboardText (const Payload& payload, const juce::File& sideFolder, std::size_t limit)
{
    const auto document = toJson (payload);
    juce::String text;
    text << magic << " " << formatVersion << "\n" << kindName (payload.kind) << " " << payload.stamp << " " << payload.pid << "\n";
    if ((std::size_t) document.getNumBytesAsUTF8() <= limit)
        return text + document;
    if (! sideFolder.createDirectory())
        return {};
    const auto file = sideFolder.getChildFile (juce::File::createLegalFileName (payload.stamp) + ".json");
    const auto temporary = file.withFileExtension (".part");
    if (! temporary.replaceWithText (document, false, false, "\n") || ! temporary.moveFileTo (file))
    {
        temporary.deleteFile();
        return {};
    }
    prune (sideFolder);
    return text + "file " + file.getFullPathName();
}

std::optional<Envelope> envelopeOf (const juce::String& text)
{
    int at = 0;
    const auto first = firstLine (text, at);
    if (! first.startsWith (magic))
        return std::nullopt;
    if (const auto version = first.fromFirstOccurrenceOf (magic, false, false).trim().getIntValue(); version < 1 || version > formatVersion)
        return std::nullopt;
    auto rest = text.substring (at);
    const auto second = firstLine (rest, at);
    const auto words = juce::StringArray::fromTokens (second, " ", {});
    if (words.size() < 3)
        return std::nullopt;
    const auto kind = kindFromName (words[0]);
    if (! kind)
        return std::nullopt;
    Envelope e;
    e.kind = *kind;
    e.stamp = words[1];
    e.pid = words[2].getIntValue();
    rest = rest.substring (at);
    if (rest.startsWith ("file "))
        e.file = juce::File (firstLine (rest.substring (5), at));
    else
        e.document = rest;
    return e;
}

std::optional<Payload> open (const Envelope& envelope)
{
    const auto document = envelope.file != juce::File() ? (envelope.file.existsAsFile() ? envelope.file.loadFileAsString() : juce::String())
                                                         : envelope.document;
    auto payload = fromJson (document);
    if (! payload || payload->kind != envelope.kind || payload->stamp != envelope.stamp)
        return std::nullopt;
    return payload;
}

void prune (const juce::File& sideFolder, int keepNewest)
{
    auto files = sideFolder.findChildFiles (juce::File::findFiles, false, "*.json");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
    for (int i = keepNewest; i < files.size(); ++i)
        files.getReference (i).deleteFile();
}
} // namespace lynx::crossclipboard
