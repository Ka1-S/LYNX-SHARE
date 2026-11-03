#include "engine/LiveSessions.h"

#include <algorithm>
#include <cstring>

#include "core/MissingFiles.h"
#include "core/ProjectSafety.h"
#include "core/SessionParts.h"
#include "core/StandIns.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectFile.h"
#include "engine/SessionPack.h"
#include "platform/Paths.h"
#include "ui/I18n.h"

namespace lynx
{
namespace
{
constexpr std::size_t fragmentSize = 512 * 1024; // (the relay takes frames up to 1 MiB)
constexpr int maxFragments = 128;                // 64 MiB: the largest message (a snapshot) accepted
constexpr int chunkSize = 256 * 1024;            // media pieces
constexpr juce::int64 maxMediaBytes = (juce::int64) 4 << 30;
constexpr std::size_t maxJsonBytes = 64u << 20;
constexpr std::size_t uploadBacklog = 1u << 20; // media wait while this much is queued (the song's changes go first)
const juce::String tokenPrefix = "lynx-media:";

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
std::string utf8 (const juce::String& s) { return s.toStdString(); }

std::uint32_t colourOf (std::uint32_t tag)
{
    static constexpr std::uint32_t guests[] = { 0xFF3D9BE9, 0xFF46C27A, 0xFFE0A030 };
    return tag <= session::hostTag ? 0xFFE5484D : guests[(tag - 2) % 3];
}

bool isSha (const juce::String& s) { return s.length() == 64 && s.containsOnly ("0123456789abcdef"); }

// the extension a received medium is stored with (only plain letters / digits: nothing a name could abuse)
juce::String safeExtension (const juce::String& name)
{
    auto ext = name.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (ext == name.toLowerCase() || ext.isEmpty() || ext.length() > 5 || ! ext.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789"))
        return ".wav";
    return "." + ext;
}

void put32 (std::vector<std::uint8_t>& v, std::uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back ((std::uint8_t) (x >> (8 * i)));
}
std::uint32_t get32 (const std::uint8_t* p) { return (std::uint32_t) p[0] | (std::uint32_t) p[1] << 8 | (std::uint32_t) p[2] << 16 | (std::uint32_t) p[3] << 24; }
std::uint64_t get64 (const std::uint8_t* p) { return (std::uint64_t) get32 (p) | (std::uint64_t) get32 (p + 4) << 32; }

juce::var object() { return juce::var (new juce::DynamicObject()); }
juce::var& set (juce::var& o, const char* name, const juce::var& value)
{
    o.getDynamicObject()->setProperty (name, value);
    return o;
}
juce::var message (const char* type)
{
    auto o = object();
    return set (o, "t", type);
}

juce::String wsUrlFor (juce::String relay, const juce::String& room)
{
    relay = relay.trimCharactersAtEnd ("/");
    if (relay.startsWithIgnoreCase ("https://"))
        relay = "wss://" + relay.substring (8);
    else if (relay.startsWithIgnoreCase ("http://"))
        relay = "ws://" + relay.substring (7);
    return relay + "/v1/rooms/" + room + "/ws";
}

std::string userAgent() { return utf8 ("LYNX-SHARE/" + juce::String (JUCE_APPLICATION_VERSION_STRING)); }

juce::File engineFiles()
{
    const auto fl = findFlStudioRoots().factoryData;
    return fl.empty() ? juce::File() : juce::File (str (fl));
}
} // namespace

LiveSessionManager::LiveSessionManager (AudioEngine& e) : engine (e) {}

LiveSessionManager::~LiveSessionManager()
{
    *alive = false;
    if (isActive())
        leave();
    stopTimer();
    link.close();
    if (httpThread.joinable())
        httpThread.join();
}

// --- the patch on the wire ------------------------------------------------------------------------------------------

juce::var LiveSessionManager::patchToVar (const live::Patch& patch)
{
    juce::Array<juce::var> changed, removed;
    for (const auto& k : patch.changed)
        changed.add (str (live::toString (k)));
    for (const auto& k : patch.removed)
        removed.add (str (live::toString (k)));
    auto o = object();
    set (o, "set", changed);
    set (o, "del", removed);
    if (! patch.changed.empty())
        set (o, "song", ProjectFile::toJson (patch.carrier));
    return o;
}

live::Patch LiveSessionManager::patchFromVar (const juce::var& v)
{
    live::Patch p;
    auto keys = [] (const juce::var& list, std::vector<live::Key>& out) {
        if (const auto* a = list.getArray())
            for (const auto& item : *a)
                if (auto k = live::keyFromString (utf8 (item.toString())))
                    out.push_back (*k);
    };
    keys (v["set"], p.changed);
    keys (v["del"], p.removed);
    if (! p.changed.empty())
    {
        juce::String error;
        auto song = ProjectFile::fromJson (v["song"].toString(), error);
        if (! song)
            p.changed.clear(); // (a damaged carrier changes nothing)
        else
            p.carrier = std::move (*song);
    }
    return p;
}

live::Patch LiveSessionManager::roundTrip (const live::Patch& patch)
{
    return patchFromVar (patchToVar (patch));
}

std::uint64_t LiveSessionManager::revision() const
{
    return authority ? authority->revision() : replica ? replica->revision() : 0;
}

int LiveSessionManager::waitingChanges() const
{
    return replica ? replica->waiting() : 0;
}

// --- starting / joining / leaving -----------------------------------------------------------------------------------

void LiveSessionManager::setState (State s, const juce::String& text)
{
    state = s;
    status = text;
    if (onChange)
        onChange();
}

juce::String LiveSessionManager::hostName() const
{
    return options.name.isNotEmpty() ? options.name : juce::String ("Host");
}

void LiveSessionManager::start (const Options& o)
{
    if (isActive())
        return;
    options = o;
    host = true;
    me = session::hostTag;
    peers.clear();
    stats = {};
    std::string why;
    if (! livelink::Connection::available (&why))
        return setState (State::ended, tr ("Live sessions need a newer libcurl on this computer ({0})", { str (why) }));

    // this computer is the host: its own record first (the area colour), then the authority over the song as it is
    engine.editSong (
        [&] (Song& s) {
            s.session.me = session::hostTag;
            auto* mine = s.session.find (session::hostTag);
            if (mine == nullptr)
            {
                SessionParticipant p;
                p.tag = session::hostTag;
                s.session.participants.insert (s.session.participants.begin(), p);
                mine = &s.session.participants.front();
            }
            mine->name = utf8 (hostName());
            if (mine->colour == 0)
                mine->colour = colourOf (session::hostTag);
        },
        false);
    {
        // the song in the form the guests receive it (the .lynx JSON rounds a few values, e.g. floats a computation
        // made): from now on every copy, this one included, holds exactly the same values
        Song rounded = engine.getSong();
        live::apply (rounded, roundTrip (live::diff (Song(), rounded)));
        engine.applyRemote (std::move (rounded));
    }
    const auto& song = engine.getSong();
    authority = std::make_unique<live::Authority> (song, &roundTrip);
    replica = std::make_unique<live::Replica> (song, 0, session::hostTag, &roundTrip);
    keepFull = options.guestsKeepFull;
    editEverything = options.guestsEditEverything;
    mediaFolder = options.folder != juce::File() ? options.folder : sessionpack::sessionMediaFolder (options.projectFile, song, "Live session");
    lastStamp = engine.getSongStamp();
    engine.setSessionHistory (true);

    invite = {};
    invite.secret = livecrypto::newSecret();
    keys = livecrypto::derive (invite); // (the relay token depends on the secret only)
    setState (State::starting, tr ("Opening the session..."));
    const auto relay = options.relay.trimCharactersAtEnd ("/");
    const auto token = str (keys.relayToken);
    if (httpThread.joinable())
        httpThread.join();
    roomAnswer = {};
    startTimer (40);
    httpThread = std::thread ([this, relay, token] {
        int status = 0;
        juce::String room, hostTok, error;
        auto request = object();
        set (request, "token", token);
        const auto body = juce::JSON::toString (request, true);
        juce::var parsed;
        if (auto in = juce::URL (relay + "/v1/rooms")
                          .withPOSTData (body)
                          .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                  .withExtraHeaders ("Content-Type: application/json\r\nUser-Agent: " + str (userAgent()))
                                                  .withConnectionTimeoutMs (15000)
                                                  .withStatusCode (&status)))
            parsed = juce::JSON::parse (in->readEntireStreamAsString());
        if (status == 201)
        {
            room = parsed["room"].toString();
            hostTok = parsed["hostToken"].toString();
        }
        else if (status == 429)
            error = tr ("Too many sessions were opened from this connection. Try again in a while.");
        else if (status == 503)
            error = tr ("The LYNX SHARE relay is full. Try again in a few minutes.");
        else
            error = tr ("Could not reach the LYNX SHARE relay ({0}).", { status == 0 ? tr ("no connection") : juce::String (status) });
        const std::lock_guard<std::mutex> g (roomLock);
        roomAnswer = { true, room, hostTok, error };
    });
}

void LiveSessionManager::onRoomCreated (const juce::String& room, const juce::String& hostTok)
{
    roomId = room;
    hostToken = hostTok;
    invite.room = utf8 (room);
    keys = livecrypto::derive (invite);
    inviteCode = str (livecrypto::formatCode (invite));
    connect();
}

void LiveSessionManager::join (const juce::String& code, const Options& o)
{
    if (isActive())
        return;
    options = o;
    host = false;
    peers.clear();
    people.clear();
    stats = {};
    std::string why;
    if (! livelink::Connection::available (&why))
        return setState (State::ended, tr ("Live sessions need a newer libcurl on this computer ({0})", { str (why) }));
    const auto parsed = livecrypto::parseCode (utf8 (code));
    if (! parsed)
        return setState (State::ended, tr ("That is not a LYNX SHARE invite code."));
    invite = *parsed;
    keys = livecrypto::derive (invite);
    roomId = str (invite.room);
    inviteCode = str (livecrypto::formatCode (invite));
    mediaFolder = options.folder != juce::File() ? options.folder : sessionpack::receivedRoot().getChildFile ("Live sessions").getChildFile (roomId);
    connect();
}

void LiveSessionManager::connect()
{
    sealer = std::make_unique<livecrypto::Sealer> (keys.key);
    opener = std::make_unique<livecrypto::Opener> (keys.key);
    wsUrl = wsUrlFor (options.relay, roomId);
    link.close();
    link.open (utf8 (wsUrl), keys.relayToken, userAgent());
    setState (State::connecting, tr ("Connecting to the session..."));
    startTimer (40);
}

void LiveSessionManager::leave()
{
    if (! isActive())
        return;
    if (host)
    {
        auto m = message ("end");
        sendToGuests (m);
    }
    else if (hostPeer != 0)
        sendJson (hostPeer, message ("bye"));
    // (what is queued goes out first: a second at most)
    for (int i = 0; i < 40 && link.isOpen() && link.queuedBytes() > 0; ++i)
        juce::Thread::sleep (25);
    if (host)
        end (tr ("You ended the live session."), false);
    else
        end (keepFull ? tr ("You left the live session. The project stays as it is.") : tr ("You left the live session. Your part of the project stays here."),
             ! keepFull);
}

void LiveSessionManager::end (const juce::String& text, bool keepPart)
{
    if (state == State::idle || state == State::ended)
        return;
    const bool wasLive = state == State::live;
    stopTimer();
    link.close();
    if (host && roomId.isNotEmpty() && hostToken.isNotEmpty())
    {
        // the room goes at once (else when the relay's timer finds it empty)
        const auto url = options.relay.trimCharactersAtEnd ("/") + "/v1/rooms/" + roomId, token = hostToken;
        std::thread ([url, token] {
            int status = 0;
            juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                   .withHttpRequestCmd ("DELETE")
                                                   .withExtraHeaders ("Authorization: Bearer " + token)
                                                   .withConnectionTimeoutMs (8000)
                                                   .withStatusCode (&status));
        }).detach();
    }
    if (! host && wasLive && keepPart)
        engine.editSong ([] (Song& s) { s = parts::keepOnlyPart (s); }, false);
    engine.setSessionHistory (false);
    authority.reset();
    replica.reset();
    uploads.clear();
    wanted.clear();
    forward.clear();
    fragments.clear();
    for (auto& p : peers)
        p.second.accepted = false;
    people.clear();
    hostToken.clear();
    setState (State::ended, text);
    if (onEnded)
        onEnded (text);
}

// --- the wire -------------------------------------------------------------------------------------------------------

void LiveSessionManager::sendRaw (int dest, const std::vector<std::uint8_t>& plain)
{
    if (! sealer || ! link.isOpen())
        return;
    const auto sealed = sealer->seal (plain);
    ++stats.sent;
    stats.bytesSent += (juce::int64) sealed.size();
    if (sealed.size() <= fragmentSize)
    {
        std::vector<std::uint8_t> out;
        out.reserve (sealed.size() + 2);
        out.push_back ((std::uint8_t) dest);
        out.push_back (1);
        out.insert (out.end(), sealed.begin(), sealed.end());
        link.send (std::move (out));
        return;
    }
    const auto id = ++fragmentId;
    const auto count = (std::uint32_t) ((sealed.size() + fragmentSize - 1) / fragmentSize);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        std::vector<std::uint8_t> out;
        const auto from = (std::size_t) i * fragmentSize, size = std::min (fragmentSize, sealed.size() - from);
        out.reserve (size + 10);
        out.push_back ((std::uint8_t) dest);
        out.push_back (2);
        put32 (out, id);
        out.push_back ((std::uint8_t) (i & 255));
        out.push_back ((std::uint8_t) (i >> 8));
        out.push_back ((std::uint8_t) (count & 255));
        out.push_back ((std::uint8_t) (count >> 8));
        out.insert (out.end(), sealed.begin() + (std::ptrdiff_t) from, sealed.begin() + (std::ptrdiff_t) (from + size));
        link.send (std::move (out));
    }
}

void LiveSessionManager::sendJson (int dest, const juce::var& m)
{
    const auto text = juce::JSON::toString (m, true);
    const auto size = text.getNumBytesAsUTF8();
    std::vector<std::uint8_t> plain;
    if (size < 4096)
    {
        plain.reserve (size + 1);
        plain.push_back ('J');
        plain.insert (plain.end(), text.toRawUTF8(), text.toRawUTF8() + size);
    }
    else
    {
        juce::MemoryOutputStream packed;
        {
            juce::GZIPCompressorOutputStream gz (packed, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            gz.write (text.toRawUTF8(), size);
        }
        plain.reserve (packed.getDataSize() + 1);
        plain.push_back ('Z');
        const auto* d = (const std::uint8_t*) packed.getData();
        plain.insert (plain.end(), d, d + packed.getDataSize());
    }
    sendRaw (dest, plain);
}

void LiveSessionManager::sendToGuests (const juce::var& m)
{
    for (const auto& [n, p] : peers)
        if (p.accepted)
            sendJson (n, m);
}

void LiveSessionManager::timerCallback()
{
    pump();
}

void LiveSessionManager::pump()
{
    if (state == State::starting)
    {
        RoomAnswer answer;
        {
            const std::lock_guard<std::mutex> g (roomLock);
            answer = roomAnswer;
        }
        if (! answer.done)
            return;
        if (httpThread.joinable())
            httpThread.join();
        if (answer.room.length() != 10 || ! answer.room.containsOnly ("abcdefghijklmnopqrstuvwxyz234567"))
            return end (answer.error.isNotEmpty() ? answer.error : tr ("The relay gave an unexpected answer."), false);
        return onRoomCreated (answer.room, answer.hostToken);
    }
    for (auto& e : link.events())
    {
        handleEvent (e);
        if (! isActive())
            return;
    }
    const auto now = juce::Time::currentTimeMillis();
    if (state == State::live && now - lastPollMs >= 100)
    {
        lastPollMs = now;
        pollLocal();
        releaseHeld();
        if (now - lastPresenceMs >= 250)
        {
            lastPresenceMs = now;
            sendPresence();
        }
    }
    pumpUploads();
}

void LiveSessionManager::handleEvent (const livelink::Event& e)
{
    using T = livelink::Event::Type;
    if (e.type == T::text)
    {
        const auto ctl = juce::JSON::parse (str (e.text));
        const auto type = ctl["type"].toString();
        if (type == "welcome")
        {
            myPeer = (int) ctl["peer"];
            if (host)
            {
                setState (State::live, tr ("Live: waiting for guests"));
                if (onHint)
                    onHint (tr ("Live session open. Send the invite code to up to 3 people."));
            }
            else
            {
                auto hello = message ("hello");
                set (hello, "name", options.name.isNotEmpty() ? options.name : juce::String ("Guest"));
                set (hello, "version", JUCE_APPLICATION_VERSION_STRING);
                set (hello, "protocol", live::protocolVersion);
                sendJson (0, hello);
                setState (State::knocking, tr ("Waiting for the host to let you in..."));
            }
        }
        else if (type == "leave")
        {
            const int peer = (int) ctl["peer"];
            if (! host && peer == hostPeer && hostPeer != 0)
                return end (keepFull ? tr ("The host left the live session. The project stays as it is.")
                                     : tr ("The host left the live session. Your part of the project stays here."),
                            ! keepFull);
            if (host)
                if (auto it = peers.find (peer); it != peers.end())
                {
                    const auto name = it->second.name;
                    const bool was = it->second.accepted;
                    peers.erase (it);
                    for (auto& u : uploads)
                        if (u.peer == peer)
                            u.next = -1;
                    std::erase_if (uploads, [] (const Upload& u) { return u.next < 0; });
                    for (auto& [sha, list] : forward)
                        std::erase (list, peer);
                    fragments.erase (peer);
                    if (was)
                    {
                        sendPeople (true);
                        if (onHint)
                            onHint (tr ("{0} left the live session", { name }));
                    }
                }
        }
        return;
    }
    if (e.type == T::binary)
    {
        if (e.bytes.size() < 2)
            return;
        stats.bytesReceived += (juce::int64) e.bytes.size();
        handleFrame (e.bytes[0], std::vector<std::uint8_t> (e.bytes.begin() + 1, e.bytes.end()));
        return;
    }
    if (e.type == T::closed)
    {
        juce::String why;
        if (e.code == 401)
            why = tr ("The invite code is wrong.");
        else if (e.code == 404)
            why = tr ("There is no such session: it has ended, or the code is wrong.");
        else if (e.code == 409)
            why = tr ("The session is full.");
        else if (e.code == 429)
            why = tr ("Too many attempts. Try again in a minute.");
        else if (e.code == 4000)
            why = host ? tr ("The relay closed the session.") : tr ("The host ended the live session.");
        else
            why = tr ("The connection to the session was lost.");
        const bool keep = ! host && ! keepFull;
        if (! host && state == State::live)
            why += " " + (keepFull ? tr ("The project stays as it is.") : tr ("Your part of the project stays here."));
        end (why, keep);
    }
}

void LiveSessionManager::handleFrame (int from, std::vector<std::uint8_t> payload)
{
    if (from <= 0 || from == myPeer || payload.empty())
        return;
    std::vector<std::uint8_t> sealed;
    if (payload[0] == 1)
        sealed.assign (payload.begin() + 1, payload.end());
    else if (payload[0] == 2 && payload.size() > 9)
    {
        const auto id = get32 (payload.data() + 1);
        const int index = payload[5] | payload[6] << 8, count = payload[7] | payload[8] << 8;
        if (count < 2 || count > maxFragments || index >= count)
            return;
        auto& byId = fragments[from];
        if (byId.size() > 4 && byId.find (id) == byId.end())
            byId.erase (byId.begin()); // (a sender that never finishes cannot fill this computer's memory)
        auto& parts = byId[id];
        if (parts.empty())
            parts.resize ((std::size_t) count);
        if ((int) parts.size() != count)
            return;
        parts[(std::size_t) index].assign (payload.begin() + 9, payload.end());
        if (std::any_of (parts.begin(), parts.end(), [] (const auto& p) { return p.empty(); }))
            return;
        for (auto& p : parts)
            sealed.insert (sealed.end(), p.begin(), p.end());
        byId.erase (id);
    }
    else
        return;
    auto plain = opener->open (sealed);
    if (! plain || plain->empty())
        return; // another key (a wrong code), changed on the way or replayed
    ++stats.received;
    const auto type = (*plain)[0];
    if (type == 'M')
        return handleMediaChunk (from, plain->data() + 1, plain->size() - 1);
    juce::String text;
    if (type == 'J')
        text = juce::String::fromUTF8 ((const char*) plain->data() + 1, (int) plain->size() - 1);
    else if (type == 'Z')
    {
        juce::GZIPDecompressorInputStream gz (new juce::MemoryInputStream (plain->data() + 1, plain->size() - 1, false), true,
                                              juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryOutputStream out;
        char buffer[65536];
        for (;;)
        {
            const auto n = gz.read (buffer, sizeof buffer);
            if (n <= 0)
                break;
            out.write (buffer, (size_t) n);
            if (out.getDataSize() > maxJsonBytes)
                return;
        }
        text = out.toUTF8();
    }
    else
        return;
    const auto m = juce::JSON::parse (text);
    if (m.isObject())
        handleMessage (from, m);
}

void LiveSessionManager::handleMessage (int from, const juce::var& m)
{
    const auto t = m["t"].toString();
    if (host)
    {
        if (t == "hello")
            return onHello (from, m);
        auto it = peers.find (from);
        if (it == peers.end() || ! it->second.accepted)
            return;
        auto& peer = it->second;
        if (t == "propose")
            onPropose (from, m);
        else if (t == "presence")
        {
            peer.playing = (bool) m["playing"];
            peer.recording = (bool) m["recording"];
            peer.position = (double) m["pos"];
        }
        else if (t == "want")
            serve (from, m["sha"].toString());
        else if (t == "resync")
            sendSnapshot (from);
        else if (t == "bye")
        {
            const auto name = peer.name;
            peers.erase (it);
            sendPeople (true);
            if (onHint)
                onHint (tr ("{0} left the live session", { name }));
        }
        return;
    }
    // a guest: only the host speaks to us (the first one who lets us in or turns us away)
    if (hostPeer == 0 && (t == "snapshot" || t == "deny"))
        hostPeer = from;
    if (from != hostPeer)
        return;
    if (t == "deny")
    {
        const auto reason = m["reason"].toString();
        juce::String why = reason == "version" ? tr ("The host uses another version of LYNX SHARE ({0}). Update both to the same version.", { m["version"].toString() })
                         : reason == "full"    ? tr ("The session is full.")
                                               : tr ("The host did not let you in.");
        return end (why, false);
    }
    if (t == "snapshot")
        onSnapshot (m);
    else if (t == "commit")
        onCommit (m);
    else if (t == "people")
    {
        people.clear();
        if (const auto* list = m["list"].getArray())
            for (const auto& v : *list)
            {
                Person p;
                p.tag = (std::uint32_t) (int) v["tag"];
                p.name = v["name"].toString().substring (0, 64);
                p.colour = (std::uint32_t) (juce::int64) v["colour"];
                p.host = (bool) v["host"];
                p.online = (bool) v["online"];
                p.playing = (bool) v["playing"];
                p.recording = (bool) v["recording"];
                p.position = (double) v["pos"];
                p.me = p.tag == me;
                people.push_back (p);
            }
        if (onChange)
            onChange();
    }
    else if (t == "want")
        serve (from, m["sha"].toString());
    else if (t == "end")
    {
        keepFull = (bool) m["keepFull"] || keepFull;
        end (keepFull ? tr ("The host ended the live session. The project stays as it is.")
                      : tr ("The host ended the live session. Your part of the project stays here."),
             ! keepFull);
    }
}

// --- the host -------------------------------------------------------------------------------------------------------

void LiveSessionManager::deny (int to, const juce::String& reason)
{
    auto m = message ("deny");
    set (m, "reason", reason);
    set (m, "version", JUCE_APPLICATION_VERSION_STRING);
    sendJson (to, m);
}

void LiveSessionManager::onHello (int from, const juce::var& m)
{
    if (peers.count (from) != 0)
        return;
    if ((int) m["protocol"] != live::protocolVersion)
        return deny (from, "version");
    int accepted = 0;
    for (const auto& [n, p] : peers)
        accepted += p.accepted || p.asking ? 1 : 0;
    if (accepted >= session::maxParticipants - 1)
        return deny (from, "full");
    Peer p;
    p.relayPeer = from;
    p.name = m["name"].toString().trim().substring (0, 40);
    if (p.name.isEmpty())
        p.name = "Guest";
    p.version = m["version"].toString().substring (0, 32);
    p.asking = true;
    peers[from] = p;
    const auto name = p.name;
    if (options.autoAccept || ! onKnock)
        return accept (from);
    auto flag = alive;
    onKnock (name, [this, flag, from] (bool yes) {
        if (! *flag || state != State::live)
            return;
        auto it = peers.find (from);
        if (it == peers.end() || ! it->second.asking)
            return;
        if (yes)
            return accept (from);
        deny (from, "declined");
        peers.erase (it);
    });
}

void LiveSessionManager::accept (int from)
{
    auto it = peers.find (from);
    if (it == peers.end())
        return;
    auto& peer = it->second;
    peer.asking = false;
    // a name nobody else in the session has (the record of an earlier visit with it is his again)
    Song song = authority->confirmed();
    auto name = peer.name;
    for (int n = 2; std::any_of (peers.begin(), peers.end(), [&] (const auto& q) { return q.first != from && q.second.accepted && q.second.name.equalsIgnoreCase (name); })
                    || name.equalsIgnoreCase (hostName());
         ++n)
        name = peer.name + " " + juce::String (n);
    peer.name = name;
    // (colour 0: the tag's own, set below)
    auto* p = parts::invite (song, utf8 (hostName()), utf8 (name), 0, keepFull);
    if (p == nullptr)
    {
        deny (from, "full");
        peers.erase (it);
        return;
    }
    p->colour = colourOf (p->tag);
    peer.tag = p->tag;
    peer.colour = p->colour;
    authority->setPermissions (peer.tag, { editEverything, false });
    broadcastCommit (authority->setParticipants (song.session.participants));
    peer.accepted = true;
    sendSnapshot (from);
    sendPeople (true);
    if (onHint)
        onHint (tr ("{0} joined the live session", { name }));
    setState (State::live, tr ("Live"));
}

void LiveSessionManager::sendSnapshot (int to)
{
    auto it = peers.find (to);
    if (it == peers.end())
        return;
    Song song = authority->confirmed();
    tokenize (song);
    sessionpack::Manifest manifest;
    std::vector<const PluginRef*> refs;
    for (const auto& c : song.channels)
        standins::pluginsOf (c, refs);
    for (const auto& t : song.mixer)
        standins::pluginsOf (t, refs);
    std::set<std::string> seen;
    for (const auto* r : refs)
        if (seen.insert (r->id).second)
            if (auto id = engine.getPluginHost().portableIdentity (*r))
                manifest.plugins.push_back (*id);
    auto m = message ("snapshot");
    set (m, "rev", (juce::int64) authority->revision());
    set (m, "tag", (int) it->second.tag);
    set (m, "keepFull", keepFull);
    set (m, "editEverything", editEverything);
    set (m, "song", ProjectFile::toJson (song));
    set (m, "manifest", sessionpack::manifestToJson (manifest));
    sendJson (to, m);
}

void LiveSessionManager::onPropose (int from, const juce::var& m)
{
    auto& peer = peers[from];
    live::Proposal p;
    p.seq = (std::uint32_t) (juce::int64) m["seq"];
    p.baseRev = (std::uint64_t) (juce::int64) m["base"];
    p.patch = patchFromVar (m["patch"]);
    detokenize (p.patch.carrier, from);
    const auto c = authority->propose (peer.tag, p);
    stats.rejected += (int) c.rejected.size();
    broadcastCommit (c);
}

void LiveSessionManager::broadcastCommit (const live::Commit& c)
{
    // this computer first (its replica, like everyone's), then every guest in the session
    applyUpdate (replica->onCommit (c, engine.getSong(), holding()));
    ++stats.commits;
    juce::var wire;
    for (const auto& [n, p] : peers)
        if (p.accepted && (! c.patch.empty() || c.origin == p.tag))
        {
            if (wire.isVoid())
                wire = commitToVar (c);
            sendJson (n, wire);
        }
}

void LiveSessionManager::sendPeople (bool force)
{
    if (! host || ! authority)
        return;
    std::vector<Person> list;
    Person mine;
    mine.tag = session::hostTag;
    mine.name = hostName();
    mine.colour = colourOf (session::hostTag);
    mine.host = mine.online = mine.me = true;
    mine.playing = engine.getTransport().playing.load();
    mine.recording = mine.playing && engine.isRecording();
    mine.position = engine.songPlayheadTicks();
    list.push_back (mine);
    for (const auto& [n, p] : peers)
        if (p.accepted)
        {
            Person q;
            q.tag = p.tag;
            q.name = p.name;
            q.colour = p.colour;
            q.online = true;
            q.playing = p.playing;
            q.recording = p.recording;
            q.position = p.position;
            list.push_back (q);
        }
    juce::Array<juce::var> out;
    juce::String digest;
    for (const auto& p : list)
    {
        auto o = object();
        set (o, "tag", (int) p.tag);
        set (o, "name", p.name);
        set (o, "colour", (juce::int64) p.colour);
        set (o, "host", p.host);
        set (o, "online", p.online);
        set (o, "playing", p.playing);
        set (o, "recording", p.recording);
        set (o, "pos", std::round (p.position));
        digest << (int) p.tag << p.name << (p.playing ? "p" : "") << (p.recording ? "r" : "") << (p.playing ? juce::String ((int) (p.position / 96.0)) : "") << ";";
        out.add (o);
    }
    people = list;
    const auto now = juce::Time::currentTimeMillis();
    if (! force && digest == lastPeople && now - lastPeopleMs < 5000)
        return;
    const bool changedList = digest != lastPeople;
    lastPeople = digest;
    lastPeopleMs = now;
    auto m = message ("people");
    set (m, "list", out);
    sendToGuests (m);
    if (changedList && onChange)
        onChange();
}

// --- the guest ------------------------------------------------------------------------------------------------------

void LiveSessionManager::onSnapshot (const juce::var& m)
{
    juce::String error;
    auto song = ProjectFile::fromJson (m["song"].toString(), error);
    if (! song)
        return end (tr ("The project could not be read ({0}).", { error }), false);
    me = (std::uint32_t) (int) m["tag"];
    keepFull = (bool) m["keepFull"];
    editEverything = (bool) m["editEverything"];
    const auto rev = (std::uint64_t) (juce::int64) m["rev"];
    detokenize (*song, hostPeer);
    if (auto manifest = sessionpack::manifestFromJson (m["manifest"].toString(), error))
        sessionpack::resolvePlugins (*song, *manifest, engine.getPluginHost(), true);
    song->session.me = me;
    replica = std::make_unique<live::Replica> (*song, rev, me, &roundTrip);
    const bool first = state != State::live;
    if (onJoined)
        onJoined (*song);
    else
        engine.setSong (*song);
    engine.setSessionHistory (true);
    lastStamp = engine.getSongStamp();
    setState (State::live, tr ("Live"));
    if (first && onHint)
        onHint (tr ("You joined the live session. Your edits go to everyone."));
}

void LiveSessionManager::onCommit (const juce::var& m)
{
    if (! replica)
        return;
    const auto c = commitFromVar (m, hostPeer);
    auto u = replica->onCommit (c, engine.getSong(), holding());
    ++stats.commits;
    if (u.gap)
    {
        sendJson (hostPeer, message ("resync"));
        return;
    }
    applyUpdate (u);
}

void LiveSessionManager::applyUpdate (const live::Replica::Update& u)
{
    if (u.working)
    {
        engine.applyRemote (*u.working);
        lastStamp = engine.getSongStamp();
    }
    if (! u.rejected.empty() && onHint)
    {
        const auto& song = engine.getSong();
        const auto what = str (live::describe (u.rejected.front(), song));
        const auto owner = live::ownerOf (u.rejected.front(), song);
        if (u.reason == "conflict")
            onHint (tr ("{0} was changed by someone else at the same time: their change was kept", { what }));
        else if (owner != 0 && owner != me)
            onHint (tr ("{0} belongs to {1}: your change was undone", { what, nameOf (owner) }));
        else
            onHint (tr ("{0} can only be changed by the host: your change was undone", { what }));
    }
}

// --- both -----------------------------------------------------------------------------------------------------------

bool LiveSessionManager::holding() const
{
    // a take being recorded is never changed under the musician's hands (§1.3): what arrives waits for the stop
    return engine.isRecording() && engine.getTransport().playing.load();
}

void LiveSessionManager::releaseHeld()
{
    if (replica && replica->waiting() > 0 && ! holding())
        applyUpdate (replica->release (engine.getSong()));
}

void LiveSessionManager::pollLocal()
{
    if (! replica || engine.getSongStamp() == lastStamp)
        return;
    lastStamp = engine.getSongStamp();
    auto p = replica->poll (engine.getSong());
    if (! p)
        return;
    if (host)
    {
        const auto c = authority->propose (session::hostTag, *p);
        broadcastCommit (c);
        return;
    }
    tokenize (p->patch.carrier);
    auto m = message ("propose");
    set (m, "seq", (juce::int64) p->seq);
    set (m, "base", (juce::int64) p->baseRev);
    set (m, "patch", patchToVar (p->patch));
    sendJson (hostPeer, m);
}

void LiveSessionManager::sendPresence()
{
    const bool playing = engine.getTransport().playing.load();
    const bool recording = playing && engine.isRecording();
    const auto position = engine.songPlayheadTicks();
    if (host)
        return sendPeople (false);
    const auto digest = juce::String (playing ? "p" : "") + (recording ? "r" : "") + (playing ? juce::String ((int) (position / 96.0)) : "");
    const auto now = juce::Time::currentTimeMillis();
    if (digest == lastPresence && now - lastPeopleMs < 5000)
        return;
    lastPresence = digest;
    lastPeopleMs = now;
    auto m = message ("presence");
    set (m, "playing", playing);
    set (m, "recording", recording);
    set (m, "pos", std::round (position));
    sendJson (hostPeer, m);
}

juce::var LiveSessionManager::commitToVar (const live::Commit& c)
{
    auto patch = c.patch;
    tokenize (patch.carrier);
    auto m = message ("commit");
    set (m, "rev", (juce::int64) c.rev);
    set (m, "origin", (int) c.origin);
    set (m, "seq", (juce::int64) c.seq);
    set (m, "patch", patchToVar (patch));
    juce::Array<juce::var> rejected;
    for (const auto& k : c.rejected)
        rejected.add (str (live::toString (k)));
    set (m, "rejected", rejected);
    set (m, "reason", str (c.reason));
    return m;
}

live::Commit LiveSessionManager::commitFromVar (const juce::var& v, int from)
{
    live::Commit c;
    c.rev = (std::uint64_t) (juce::int64) v["rev"];
    c.origin = (std::uint32_t) (int) v["origin"];
    c.seq = (std::uint32_t) (juce::int64) v["seq"];
    c.patch = patchFromVar (v["patch"]);
    detokenize (c.patch.carrier, from);
    if (const auto* a = v["rejected"].getArray())
        for (const auto& k : *a)
            if (auto key = live::keyFromString (utf8 (k.toString())))
                c.rejected.push_back (*key);
    c.reason = utf8 (v["reason"].toString());
    return c;
}

std::vector<LiveSessionManager::Person> LiveSessionManager::getPeople() const
{
    return people;
}

juce::String LiveSessionManager::nameOf (std::uint32_t tag) const
{
    for (const auto& p : people)
        if (p.tag == tag)
            return p.name;
    if (const auto* p = engine.getSong().session.find (tag))
        return str (p->name);
    return tr ("someone else");
}

// --- media ----------------------------------------------------------------------------------------------------------

juce::String LiveSessionManager::shaOf (const juce::File& f)
{
    const auto path = f.getFullPathName();
    const auto size = f.getSize();
    const auto modified = f.getLastModificationTime().toMilliseconds();
    auto& cached = hashCache[path];
    if (cached.sha.isEmpty() || cached.size != size || cached.modified != modified)
        cached = { size, modified, sessionpack::hashFile (f) };
    return cached.sha;
}

void LiveSessionManager::tokenize (Song& song)
{
    // a path of this computer means nothing elsewhere: the bytes' hash (and the name) travel instead
    std::map<std::string, std::string> tokens;
    for (const auto& path : safety::referencedSamples (song))
    {
        const juce::File f (str (path));
        juce::String sha;
        if (f.isAChildOf (mediaFolder) && isSha (f.getFileNameWithoutExtension()))
            sha = f.getFileNameWithoutExtension(); // (received, or still arriving: it was checked against this hash)
        else if (f.existsAsFile())
            sha = shaOf (f);
        if (sha.isEmpty())
            continue;
        if (f.existsAsFile())
            served[sha] = f;
        tokens[path] = utf8 (tokenPrefix + sha + "/" + f.getFileName());
    }
    applyRelocations (song, tokens);
}

void LiveSessionManager::detokenize (Song& song, int fromPeer)
{
    std::map<std::string, std::string> places;
    std::map<juce::String, juce::String> names;
    for (const auto& path : safety::referencedSamples (song))
    {
        const auto p = str (path);
        if (! p.startsWith (tokenPrefix))
            continue;
        const auto sha = p.substring (tokenPrefix.length(), tokenPrefix.length() + 64);
        if (! isSha (sha))
            continue;
        const auto name = p.substring (tokenPrefix.length() + 65);
        names[sha] = name;
        places[path] = utf8 (mediaFolder.getChildFile (sha + safeExtension (name)).getFullPathName());
    }
    applyRelocations (song, places);
    // whatever else it points at (a path of the sender's computer, a token that was not one) cannot be read here
    sessionpack::confine (song, mediaFolder, engineFiles());
    std::map<std::string, std::string> local;
    for (const auto& path : safety::referencedSamples (song))
    {
        const juce::File f (str (path));
        const auto sha = f.getFileNameWithoutExtension();
        if (! f.isAChildOf (mediaFolder) || ! isSha (sha))
            continue;
        if (auto it = served.find (sha); it != served.end() && it->second != f && it->second.existsAsFile())
            local[path] = utf8 (it->second.getFullPathName()); // our own file (or one we have already)
        else if (! f.existsAsFile())
            request (sha, names[sha].isNotEmpty() ? names[sha] : f.getFileName(), fromPeer);
    }
    applyRelocations (song, local);
}

void LiveSessionManager::request (const juce::String& sha, const juce::String& name, int fromPeer)
{
    if (wanted.count (sha) != 0 || fromPeer == 0)
        return;
    Want w;
    w.fromPeer = fromPeer;
    w.name = name;
    w.file = mediaFolder.getChildFile (sha + safeExtension (name));
    wanted[sha] = w;
    mediaFolder.createDirectory();
    auto m = message ("want");
    set (m, "sha", sha);
    sendJson (fromPeer, m);
    if (onChange)
        onChange();
}

void LiveSessionManager::serve (int to, const juce::String& sha)
{
    if (! isSha (sha))
        return;
    // only a file of this session's song (one we turned into this hash ourselves) is ever sent
    if (auto it = served.find (sha); it != served.end() && it->second.existsAsFile())
    {
        for (const auto& u : uploads)
            if (u.peer == to && u.sha == sha)
                return;
        uploads.push_back ({ to, sha, it->second, 0 });
        return;
    }
    if (host && wanted.count (sha) != 0)
        forward[sha].push_back (to); // (a guest's file that is still on its way here)
}

void LiveSessionManager::pumpUploads()
{
    while (! uploads.empty() && link.isOpen() && link.queuedBytes() < uploadBacklog)
    {
        auto& u = uploads.front();
        const auto size = u.file.getSize();
        const int count = std::max (1, (int) ((size + chunkSize - 1) / chunkSize));
        std::vector<std::uint8_t> plain;
        plain.push_back ('M');
        plain.insert (plain.end(), u.sha.toRawUTF8(), u.sha.toRawUTF8() + 64);
        put32 (plain, (std::uint32_t) u.next);
        put32 (plain, (std::uint32_t) count);
        put32 (plain, (std::uint32_t) (size & 0xFFFFFFFF));
        put32 (plain, (std::uint32_t) (size >> 32));
        const auto from = (juce::int64) u.next * chunkSize;
        const auto length = (int) std::min<juce::int64> (chunkSize, std::max<juce::int64> (0, size - from));
        juce::FileInputStream in (u.file);
        if (! in.openedOk() || ! in.setPosition (from))
        {
            uploads.pop_front();
            continue;
        }
        const auto at = plain.size();
        plain.resize (at + (std::size_t) length);
        if (length > 0 && in.read (plain.data() + at, length) != length)
        {
            uploads.pop_front();
            continue;
        }
        sendRaw (u.peer, plain);
        if (++u.next >= count)
        {
            ++stats.mediaSent;
            uploads.pop_front();
        }
    }
}

void LiveSessionManager::handleMediaChunk (int from, const std::uint8_t* data, std::size_t size)
{
    if (size < 64 + 16)
        return;
    const auto sha = juce::String::fromUTF8 ((const char*) data, 64);
    auto it = wanted.find (sha);
    if (it == wanted.end() || it->second.fromPeer != from)
        return; // nothing we asked that peer for
    auto& w = it->second;
    const int index = (int) get32 (data + 64), count = (int) get32 (data + 68);
    const auto total = (juce::int64) get64 (data + 72);
    const auto bytes = data + 80;
    const auto length = (juce::int64) size - 80;
    const int expected = std::max (1, (int) ((total + chunkSize - 1) / chunkSize));
    const auto expectedLength = std::min<juce::int64> (chunkSize, std::max<juce::int64> (0, total - (juce::int64) index * chunkSize));
    auto part = w.file.withFileExtension (w.file.getFileExtension() + ".part");
    if (total < 0 || total > maxMediaBytes || count != expected || index != w.got || length != expectedLength || (w.got > 0 && total != w.size))
    {
        part.deleteFile();
        wanted.erase (it);
        return;
    }
    if (index == 0)
    {
        part.deleteFile();
        w.size = total;
        w.count = count;
    }
    {
        juce::FileOutputStream out (part);
        if (! out.openedOk() || (length > 0 && ! out.write (bytes, (size_t) length)))
        {
            wanted.erase (it);
            return;
        }
    }
    if (++w.got == w.count)
        finishMedia (sha);
}

void LiveSessionManager::finishMedia (const juce::String& sha)
{
    auto it = wanted.find (sha);
    if (it == wanted.end())
        return;
    const auto w = it->second;
    wanted.erase (it);
    auto part = w.file.withFileExtension (w.file.getFileExtension() + ".part");
    if (sessionpack::hashFile (part) != sha || ! part.moveFileTo (w.file))
    {
        part.deleteFile();
        if (onHint)
            onHint (tr ("A file of the session arrived damaged: {0}", { w.name }));
        forward.erase (sha);
        return;
    }
    ++stats.mediaReceived;
    served[sha] = w.file;
    hashCache[w.file.getFullPathName()] = { w.file.getSize(), w.file.getLastModificationTime().toMilliseconds(), sha };
    for (int peer : forward[sha])
        serve (peer, sha);
    forward.erase (sha);
    // the song already points at it: load it now
    engine.getSampleLibrary().forget (w.file.getFullPathName());
    engine.applyRemote (engine.getSong());
    lastStamp = engine.getSongStamp();
    if (onChange)
        onChange();
}
} // namespace lynx
