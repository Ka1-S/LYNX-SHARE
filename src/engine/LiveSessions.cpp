#include "engine/LiveSessions.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>

#include "core/MissingFiles.h"
#include "core/ProjectSafety.h"
#include "core/SessionParts.h"
#include "core/StandIns.h"
#include "engine/AudioEngine.h"
#include "engine/MediaPack.h"
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
constexpr juce::int64 inlineHashBytes = 1 << 20; // files above this are hashed off the message thread
constexpr int chatShown = 60;                    // chat lines a late guest gets
constexpr juce::int64 silenceMs = 35000;         // nothing at all from the relay for this long: the connection is dead
constexpr double standInRate = 44100.0;          // (P1's stand-ins: 32-bit float WAV at 44.1 kHz)

juce::String randomHex (int bytes)
{
    std::vector<std::uint8_t> b ((std::size_t) bytes);
    livecrypto::randomBytes (b.data(), b.size());
    return juce::String (livecrypto::toHex (b.data(), b.size()));
}

juce::var keysToVar (const std::set<live::Key>& keys)
{
    juce::Array<juce::var> out;
    for (const auto& k : keys)
        out.add (juce::String (live::toString (k)));
    return out;
}

std::set<live::Key> keysFromVar (const juce::var& v)
{
    std::set<live::Key> out;
    if (const auto* a = v.getArray())
        for (const auto& item : *a)
            if (auto k = live::keyFromString (item.toString().toStdString()); k && (k->kind == live::Kind::channel || k->kind == live::Kind::insert) && out.size() < 4096)
                out.insert (*k);
    return out;
}

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

// Work off the message thread (round 169j): a job runs on the worker's thread and returns what runs afterwards on the
// message thread (pump() collects it). Jobs only touch copies; a renderer is left to finish on its own when the
// session goes (an ExportJob waits for the message thread).
struct LiveSessionManager::Worker
{
    using Done = std::function<void()>;
    using Job = std::function<Done()>;
    struct Shared
    {
        std::mutex lock;
        std::condition_variable wake;
        std::deque<Job> jobs;
        std::vector<Done> done;
        bool stop = false;
    };
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::thread thread;
    bool inlineJobs = false, detachAtEnd = false;

    Worker (bool runInline, bool detach) : inlineJobs (runInline), detachAtEnd (detach)
    {
        if (inlineJobs)
            return;
        thread = std::thread ([s = shared] {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> g (s->lock);
                    s->wake.wait (g, [&] { return s->stop || ! s->jobs.empty(); });
                    if (s->stop)
                        return;
                    job = std::move (s->jobs.front());
                    s->jobs.pop_front();
                }
                auto done = job();
                const std::lock_guard<std::mutex> g (s->lock);
                if (done)
                    s->done.push_back (std::move (done));
            }
        });
    }

    ~Worker()
    {
        {
            const std::lock_guard<std::mutex> g (shared->lock);
            shared->stop = true;
            shared->jobs.clear();
        }
        shared->wake.notify_all();
        if (thread.joinable())
        {
            if (detachAtEnd)
                thread.detach();
            else
                thread.join();
        }
    }

    void post (Job job)
    {
        if (inlineJobs)
        {
            if (auto done = job())
                shared->done.push_back (std::move (done));
            return;
        }
        {
            const std::lock_guard<std::mutex> g (shared->lock);
            shared->jobs.push_back (std::move (job));
        }
        shared->wake.notify_one();
    }

    std::vector<Done> finished()
    {
        std::vector<Done> out;
        const std::lock_guard<std::mutex> g (shared->lock);
        out.swap (shared->done);
        return out;
    }
};

LiveSessionManager::LiveSessionManager (AudioEngine& e) : engine (e) {}

LiveSessionManager::~LiveSessionManager()
{
    *alive = false;
    if (isActive() && ! abandoned)
        leave();
    stopTimer();
    link.close();
    if (httpThread.joinable())
        httpThread.join();
    io.reset();
    renderer.reset();
}

juce::File LiveSessionManager::defaultJournal()
{
    return platform::settingsFile ("Live session").getChildFile ("journal.json");
}

void LiveSessionManager::runCompletions()
{
    for (auto* w : { io.get(), renderer.get() })
        if (w != nullptr)
            for (auto& done : w->finished())
                done();
}

bool LiveSessionManager::missingHere (const PluginRef& ref)
{
    return options.missing ? options.missing (ref) : ! engine.getPluginHost().isInstalled (ref);
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
    away.clear();
    stats = {};
    chat.clear();
    unread = 0;
    following = false;
    abandoned = resuming = false;
    myPerms = {};
    allNeed.clear();
    myNeed.clear();
    renderedSig.clear();
    hostSeed = livecrypto::newSeed();
    journalFile = options.journal != juce::File() ? options.journal : defaultJournal();
    io = std::make_unique<Worker> (false, false);
    renderer = std::make_unique<Worker> (options.inlineWork, true);
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
    ensureHashed (song); // (the big samples start hashing now, before anyone asks for the snapshot)
    updateNeeds();

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
    journalDirty = true;
    connect();
}

void LiveSessionManager::join (const juce::String& code, const Options& o)
{
    if (isActive())
        return;
    options = o;
    host = false;
    peers.clear();
    away.clear();
    people.clear();
    stats = {};
    chat.clear();
    unread = 0;
    following = false;
    abandoned = resuming = false;
    myPerms = {};
    allNeed.clear();
    myNeed.clear();
    renderedSig.clear();
    hostKey = {};
    resumeToken.clear();
    seekNonce.clear();
    hostPeer = 0;
    io = std::make_unique<Worker> (false, false);
    renderer = std::make_unique<Worker> (options.inlineWork, true);
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
    lastEventMs = juce::Time::currentTimeMillis();
    reconnectAtMs = 0; // (an attempt is under way)
    if (state != State::reconnecting)
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
    else if (hostPeer != 0 && state == State::live)
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
    if (! host && (wasLive || state == State::waitingHost || state == State::reconnecting) && replica && keepPart)
        engine.editSong ([] (Song& s) { s = parts::keepOnlyPart (s); }, false);
    engine.setSessionHistory (false);
    if (host && ! abandoned && journalFile != juce::File())
        journalFile.deleteFile(); // (ended on purpose: nothing to resume)
    authority.reset();
    replica.reset();
    uploads.clear();
    wanted.clear();
    forward.clear();
    fragments.clear();
    away.clear();
    pendingSnapshots.clear();
    hashing.clear();
    packing.clear();
    if (mediaFolder != juce::File())
        mediaFolder.getChildFile (".packs").deleteRecursively();
    packs.clear();
    rendering = false;
    resuming = false;
    haveHostTransport = false;
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
    runCompletions();
    if (! isActive())
        return;
    for (auto& e : link.events())
    {
        lastEventMs = juce::Time::currentTimeMillis();
        handleEvent (e);
        if (! isActive())
            return;
    }
    const auto now = juce::Time::currentTimeMillis();
    // a dropped connection: connect again (1 s, 2 s, 4 s … 30 s apart); give up after options.reconnectForMs
    if (state == State::reconnecting || state == State::waitingHost)
    {
        if (now - droppedAtMs > options.reconnectForMs)
            return end (host ? tr ("The live session could not be resumed: the connection did not come back.")
                             : (keepFull ? tr ("The host did not come back. The project stays as it is.")
                                         : tr ("The host did not come back. Your part of the project stays here.")),
                        ! host && ! keepFull);
        if (state == State::reconnecting && reconnectAtMs != 0 && now >= reconnectAtMs)
            connect();
        if (state == State::waitingHost && link.isOpen() && now - lastSeekMs >= 5000)
            sendSeek (0);
    }
    // a connection that went silent (no frame, no pong) is dead even when the system has not noticed yet
    if (link.isOpen() && (state == State::live || state == State::waitingHost))
    {
        if (now - lastPingMs >= 10000)
        {
            lastPingMs = now;
            link.sendText ("{\"type\":\"ping\",\"t\":" + std::to_string (now) + "}");
        }
        if (now - lastEventMs > silenceMs)
            return lostConnection (tr ("The connection to the session went silent."));
    }
    const bool editing = state == State::live || (host && state == State::reconnecting);
    if (editing && now - lastPollMs >= 100)
    {
        lastPollMs = now;
        pollLocal();
        releaseHeld();
        if (state == State::live && now - lastPresenceMs >= 250)
        {
            lastPresenceMs = now;
            sendPresence();
            if (host)
                sendTransport();
        }
        if (! host)
            followHost();
        updateNeeds();
        maybeRenderStandIns();
    }
    if (host && ! pendingSnapshots.empty() && state == State::live)
    {
        auto waiting = pendingSnapshots;
        pendingSnapshots.clear();
        for (const auto& [peer, resumed] : waiting)
            sendSnapshot (peer, resumed);
    }
    if (host && journalDirty && now >= journalDueMs)
        writeJournal (false);
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
            if (state == State::reconnecting)
            {
                ++stats.reconnects;
                reconnectDelayMs = 1000;
                if (host)
                {
                    // back: every guest that waits checks it is us and resumes
                    setState (State::live, tr ("Live"));
                    sendJson (0, message ("back"));
                    if (onHint)
                        onHint (tr ("Connected again: the people in the session are coming back."));
                    sendPeople (true);
                }
                else
                {
                    setState (State::waitingHost, tr ("Connected again: finding the host..."));
                    sendSeek (0);
                }
                return;
            }
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
        else if (type == "join")
        {
            if (! host && state == State::waitingHost)
                sendSeek ((int) ctl["peer"]); // (perhaps the host, back)
        }
        else if (type == "leave")
        {
            const int peer = (int) ctl["peer"];
            if (! host && peer == hostPeer && hostPeer != 0 && state == State::live)
            {
                // the host's connection dropped (or its LYNX SHARE closed without ending): wait for it, edits wait too
                hostPeer = 0;
                droppedAtMs = juce::Time::currentTimeMillis();
                uploads.clear();
                setState (State::waitingHost, tr ("The host's connection dropped. Waiting for them to come back..."));
                if (onHint)
                    onHint (tr ("The host's connection dropped. Keep working: your edits go out when they are back."));
                return;
            }
            if (host)
                if (auto it = peers.find (peer); it != peers.end())
                {
                    const auto name = it->second.name;
                    const bool was = it->second.accepted;
                    if (was)
                    {
                        // a dropped guest may come back (resume): keep their place
                        auto keep = it->second;
                        keep.accepted = false;
                        keep.relayPeer = 0;
                        away[keep.tag] = keep;
                        journalDirty = true;
                    }
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
                        broadcastNeeds();
                        if (onHint)
                            onHint (tr ("{0}'s connection dropped", { name }));
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
        // a session that was running reconnects by itself (unless the room is gone or the code no longer works)
        const bool running = state == State::live || state == State::waitingHost || state == State::reconnecting;
        if (running && e.code != 401 && e.code != 404 && e.code != 4000)
            return lostConnection (tr ("The connection to the session was lost."));
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
        else if (e.code == 404 && running)
            why = tr ("The live session is over: the relay no longer has it.");
        else
            why = tr ("The connection to the session was lost.");
        const bool keep = ! host && ! keepFull;
        if (! host && running)
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
    if (type == 'M' || type == 'F')
        return handleMediaChunk (from, plain->data() + 1, plain->size() - 1, type == 'F');
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
        if (t == "seek")
            return onSeek (from, m);
        if (t == "resume")
            return onResume (from, m);
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
        else if (t == "chat")
            onChatMessage (from, m);
        else if (t == "need")
        {
            peer.need = keysFromVar (m["keys"]);
            broadcastNeeds();
        }
        else if (t == "bye")
        {
            const auto name = peer.name;
            peers.erase (it);
            journalDirty = true;
            sendPeople (true);
            broadcastNeeds();
            if (onHint)
                onHint (tr ("{0} left the live session", { name }));
        }
        return;
    }
    // a guest: only the host speaks to us (the first one who lets us in or turns us away; after a drop, the one who
    // proves it is the same host)
    if (t == "host")
        return onHostAnswer (from, m);
    if (t == "back")
    {
        if (from != hostPeer || state == State::waitingHost)
            sendSeek (from);
        return;
    }
    if (hostPeer == 0 && state == State::knocking && (t == "snapshot" || t == "deny"))
        hostPeer = from;
    if (from != hostPeer || hostPeer == 0)
        return;
    if (t == "deny" && m["reason"].toString() == "resume")
        return end (keepFull ? tr ("The host no longer knows this computer in the session. The project stays as it is.")
                             : tr ("The host no longer knows this computer in the session. Your part of the project stays here."),
                    ! keepFull);
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
    else if (t == "resumed")
    {
        // everything we missed has arrived: our own edits that never got through go out again
        if (replica)
            replica->resend();
        resuming = false;
        ++stats.replayed;
        setState (State::live, tr ("Live"));
        lastStamp = 0; // (poll now)
        pollLocal();
        sendPresence();
        myNeed.clear();
        needStamp = 0;
        updateNeeds();
        if (onHint)
            onHint (tr ("You are back in the live session."));
    }
    else if (t == "perms")
    {
        myPerms.editEverything = (bool) m["editEverything"];
        myPerms.listener = (bool) m["listener"];
        myPerms.settings = (bool) m["settings"];
        editEverything = myPerms.editEverything;
        keepFull = (bool) m["keepFull"];
        if (onHint)
            onHint (myPerms.listener         ? tr ("The host made you a listener: you hear everything and edit nothing")
                    : myPerms.editEverything ? tr ("The host lets you edit everything in the project")
                    : myPerms.settings       ? tr ("The host lets you edit your own parts and the tempo and project settings")
                                             : tr ("The host lets you edit your own parts"));
        if (onChange)
            onChange();
    }
    else if (t == "chat")
    {
        ChatLine line;
        line.tag = (std::uint32_t) (int) m["tag"];
        line.name = m["name"].toString().substring (0, 64);
        line.text = str (live::cleanChat (utf8 (m["text"].toString())));
        line.colour = (std::uint32_t) (juce::int64) m["colour"];
        line.at = (juce::int64) m["at"];
        line.mine = line.tag == me;
        if (line.text.isNotEmpty())
            addChat (line);
    }
    else if (t == "transport")
    {
        hostTransport.playing = (bool) m["playing"];
        hostTransport.songMode = (bool) m["song"];
        hostTransport.ticks = (double) m["pos"];
        hostTransport.bpm = juce::jlimit (10.0, 999.0, (double) m["bpm"]);
        hostTransport.atMs = (double) juce::Time::currentTimeMillis();
        haveHostTransport = true;
        lastFollowMs = 0; // (follow at once)
        followHost();
    }
    else if (t == "needs")
        allNeed = keysFromVar (m["keys"]);
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
                if (p.me)
                    p.permissions = myPerms, p.keepFull = keepFull;
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
    int accepted = (int) away.size();
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
    peer.token = randomHex (16);
    peer.perms = {};
    peer.perms.editEverything = editEverything;
    peer.keepFull = keepFull;
    away.erase (peer.tag); // (a dropped guest who joined again from scratch)
    authority->setPermissions (peer.tag, peer.perms);
    journalDirty = true;
    broadcastCommit (authority->setParticipants (song.session.participants));
    peer.accepted = true;
    sendSnapshot (from);
    sendPeople (true);
    if (onHint)
        onHint (tr ("{0} joined the live session", { name }));
    setState (State::live, tr ("Live"));
}

void LiveSessionManager::sendSnapshot (int to, bool resumed)
{
    auto it = peers.find (to);
    if (it == peers.end())
        return;
    Song song = authority->confirmed();
    if (! ensureHashed (song))
    {
        pendingSnapshots[to] = resumed; // (a big sample is still being hashed: in a moment)
        return;
    }
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
    set (m, "keepFull", it->second.keepFull);
    set (m, "editEverything", it->second.perms.editEverything);
    set (m, "perms", permsToVar (it->second.perms, it->second.keepFull));
    set (m, "song", ProjectFile::toJson (song));
    set (m, "manifest", sessionpack::manifestToJson (manifest));
    set (m, "hostKey", juce::String (livecrypto::toHex (livecrypto::publicKeyOf (hostSeed))));
    set (m, "resume", it->second.token);
    set (m, "resumed", resumed);
    set (m, "needs", keysToVar (allNeed));
    juce::Array<juce::var> lines;
    for (std::size_t i = chat.size() > (std::size_t) chatShown ? chat.size() - (std::size_t) chatShown : 0; i < chat.size(); ++i)
    {
        auto o = object();
        set (o, "tag", (int) chat[i].tag);
        set (o, "name", chat[i].name);
        set (o, "text", chat[i].text);
        set (o, "colour", (juce::int64) chat[i].colour);
        set (o, "at", chat[i].at);
        lines.add (o);
    }
    set (m, "chat", lines);
    sendJson (to, m);
    if (haveHostTransport || engine.getTransport().playing.load())
        lastTransportMs = 0; // (the newcomer gets the transport at the next presence tick)
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
    journalDirty = true;
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
            q.permissions = p.perms;
            q.keepFull = p.keepFull;
            list.push_back (q);
        }
    for (const auto& [tag, p] : away)
    {
        Person q;
        q.tag = p.tag;
        q.name = p.name;
        q.colour = p.colour;
        q.permissions = p.perms;
        q.keepFull = p.keepFull;
        list.push_back (q);
    }
    std::sort (list.begin() + 1, list.end(), [] (const Person& a, const Person& b) { return a.tag < b.tag; });
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
        digest << (int) p.tag << p.name << (p.online ? "o" : "") << (p.playing ? "p" : "") << (p.recording ? "r" : "") << (p.playing ? juce::String ((int) (p.position / 96.0)) : "")
               << (p.permissions.editEverything ? "E" : "") << (p.permissions.listener ? "L" : "") << (p.permissions.settings ? "S" : "") << (p.keepFull ? "K" : "") << ";";
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
    myPerms = {};
    myPerms.editEverything = editEverything;
    myPerms.listener = (bool) m["perms"]["listener"];
    myPerms.settings = (bool) m["perms"]["settings"];
    livecrypto::fromHex (utf8 (m["hostKey"].toString()), hostKey);
    resumeToken = m["resume"].toString().substring (0, 64);
    allNeed = keysFromVar (m["needs"]);
    const auto rev = (std::uint64_t) (juce::int64) m["rev"];
    detokenize (*song, hostPeer);
    if (auto manifest = sessionpack::manifestFromJson (m["manifest"].toString(), error))
        sessionpack::resolvePlugins (*song, *manifest, engine.getPluginHost(), true);
    song->session.me = me;
    const bool resumed = (bool) m["resumed"] && replica != nullptr;
    const bool first = state != State::live && ! resumed;
    if (first)
    {
        chat.clear();
        if (const auto* lines = m["chat"].getArray())
            for (const auto& v : *lines)
            {
                ChatLine line;
                line.tag = (std::uint32_t) (int) v["tag"];
                line.name = v["name"].toString().substring (0, 64);
                line.text = str (live::cleanChat (utf8 (v["text"].toString())));
                line.colour = (std::uint32_t) (juce::int64) v["colour"];
                line.at = (juce::int64) v["at"];
                if (line.text.isNotEmpty())
                    chat.push_back (line);
            }
    }
    if (resumed)
    {
        // back after a drop, with a fresh snapshot: what we did meanwhile goes on top of it
        auto working = replica->resume (*song, rev, engine.getSong());
        engine.applyRemote (std::move (working));
        ++stats.resumedBySnapshot;
        resuming = false;
    }
    else
    {
        replica = std::make_unique<live::Replica> (*song, rev, me, &roundTrip);
        if (onJoined)
            onJoined (*song);
        else
            engine.setSong (*song);
    }
    engine.setSessionHistory (true);
    lastStamp = resumed ? 0 : engine.getSongStamp();
    setState (State::live, tr ("Live"));
    myNeed.clear();
    needStamp = 0;
    updateNeeds();
    if (resumed && onHint)
        onHint (tr ("You are back in the live session."));
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
    if (! host && (hostPeer == 0 || state != State::live))
        return; // (no host to send it to: it waits, nothing is lost)
    // every sample the change points at must have its hash first (a big one: in a moment, off the message thread)
    if (! ensureHashed (engine.getSong()))
        return;
    lastStamp = engine.getSongStamp();
    lastLocalChangeMs = juce::Time::currentTimeMillis();
    auto p = replica->poll (engine.getSong());
    if (! p)
        return;
    if (host)
    {
        const auto c = authority->propose (session::hostTag, *p);
        broadcastCommit (c);
        journalDirty = true;
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
        cached = { size, modified, sessionpack::hashFile (f) }; // (ensureHashed did the big ones off the message thread)
    return cached.sha;
}

bool LiveSessionManager::ensureHashed (const Song& song)
{
    bool ready = true;
    for (const auto& path : safety::referencedSamples (song))
    {
        const juce::File f (str (path));
        if ((f.isAChildOf (mediaFolder) && isSha (f.getFileNameWithoutExtension())) || ! f.existsAsFile())
            continue;
        const auto key = f.getFullPathName();
        const auto size = f.getSize();
        const auto modified = f.getLastModificationTime().toMilliseconds();
        if (auto it = hashCache.find (key); it != hashCache.end() && it->second.sha.isNotEmpty() && it->second.size == size && it->second.modified == modified)
            continue;
        if (size <= inlineHashBytes)
        {
            hashCache[key] = { size, modified, sessionpack::hashFile (f) };
            continue;
        }
        ready = false;
        if (io && hashing.insert (key).second)
            io->post ([this, f, key, size, modified]() -> Worker::Done {
                const auto sha = sessionpack::hashFile (f);
                return [this, key, size, modified, sha] {
                    hashing.erase (key);
                    hashCache[key] = { size, modified, sha };
                    ++stats.hashedOffThread;
                };
            });
    }
    return ready;
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
    // stand-ins of the parts this computer plays itself are not fetched (the host notes who has them, for whoever asks)
    std::set<std::string> optional;
    {
        const auto cannot = live::unplayable (song, [this] (const PluginRef& r) { return missingHere (r); });
        for (const auto& c : song.channels)
            if (cannot.count ({ live::Kind::channel, c.uid, 0 }) == 0)
                for (const auto& st : c.standIns)
                    optional.insert (st.path);
        for (int t = 0; t < (int) song.mixer.size(); ++t)
            if (cannot.count ({ live::Kind::insert, (std::uint64_t) t, 0 }) == 0)
                for (const auto& st : song.mixer[(std::size_t) t].standIns)
                    optional.insert (st.path);
    }
    std::map<std::string, std::string> local;
    for (const auto& path : safety::referencedSamples (song))
    {
        const juce::File f (str (path));
        const auto sha = f.getFileNameWithoutExtension();
        if (! f.isAChildOf (mediaFolder) || ! isSha (sha))
            continue;
        const auto name = names[sha].isNotEmpty() ? names[sha] : f.getFileName();
        if (auto it = served.find (sha); it != served.end() && it->second != f && it->second.existsAsFile())
            local[path] = utf8 (it->second.getFullPathName()); // our own file (or one we have already)
        else if (! f.existsAsFile() && optional.count (path) > 0)
        {
            if (host && fromPeer != 0)
                mediaOrigin[sha] = { fromPeer, name };
        }
        else if (! f.existsAsFile())
            request (sha, name, fromPeer);
    }
    applyRelocations (song, local);
}

void LiveSessionManager::request (const juce::String& sha, const juce::String& name, int fromPeer)
{
    if (wanted.count (sha) != 0 || fromPeer == 0)
        return;
    Want w;
    w.fromPeer = fromPeer;
    w.fromTag = host ? (peers.count (fromPeer) != 0 ? peers[fromPeer].tag : 0) : session::hostTag;
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
        // a PCM WAV goes as a lossless FLAC pack (made once, off the message thread)
        const auto file = it->second;
        if (file.getFileExtension().toLowerCase() == ".wav" && file.getSize() >= mediapack::minimumSize && unpackable.count (sha) == 0
            && packs.count (sha) == 0 && io && packing.insert (sha).second)
        {
            const auto packFile = mediaFolder.getChildFile (".packs").getChildFile (sha + ".lxf");
            io->post ([this, file, packFile, sha]() -> Worker::Done {
                const bool ok = mediapack::pack (file, packFile);
                return [this, ok, packFile, sha] {
                    packing.erase (sha);
                    if (ok)
                        packs[sha] = packFile;
                    else
                        unpackable.insert (sha);
                };
            });
        }
        uploads.push_back ({ to, sha, file, 0, false });
        return;
    }
    if (! host)
        return;
    if (wanted.count (sha) != 0)
        forward[sha].push_back (to); // (a guest's file that is still on its way here)
    else if (auto o = mediaOrigin.find (sha); o != mediaOrigin.end() && peers.count (o->second.first) != 0)
    {
        // a stand-in this computer did not need: fetched from whoever made it, then passed on
        request (sha, o->second.second, o->second.first);
        forward[sha].push_back (to);
    }
}

void LiveSessionManager::pumpUploads()
{
    while (! uploads.empty() && link.isOpen() && link.queuedBytes() < uploadBacklog)
    {
        // (one whose FLAC pack is still being made waits; the others go on)
        auto ready = std::find_if (uploads.begin(), uploads.end(), [this] (const Upload& x) { return x.next > 0 || packing.count (x.sha) == 0; });
        if (ready == uploads.end())
            return;
        if (ready != uploads.begin())
            std::iter_swap (ready, uploads.begin());
        auto& u = uploads.front();
        if (u.next == 0 && ! u.packed)
            if (auto pk = packs.find (u.sha); pk != packs.end() && pk->second.existsAsFile())
            {
                stats.packedSaved += u.file.getSize() - pk->second.getSize();
                ++stats.packedSent;
                u.file = pk->second;
                u.packed = true;
            }
        const auto size = u.file.getSize();
        const int count = std::max (1, (int) ((size + chunkSize - 1) / chunkSize));
        std::vector<std::uint8_t> plain;
        plain.push_back (u.packed ? 'F' : 'M');
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

void LiveSessionManager::handleMediaChunk (int from, const std::uint8_t* data, std::size_t size, bool packed)
{
    if (size < 64 + 16)
        return;
    const auto sha = juce::String::fromUTF8 ((const char*) data, 64);
    auto it = wanted.find (sha);
    if (it == wanted.end() || it->second.fromPeer != from || it->second.verifying)
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
        w.packed = packed;
    }
    else if (packed != w.packed)
    {
        part.deleteFile();
        wanted.erase (it);
        return;
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
        verifyArrived (sha);
}

void LiveSessionManager::verifyArrived (const juce::String& sha)
{
    auto it = wanted.find (sha);
    if (it == wanted.end())
        return;
    auto& w = it->second;
    w.verifying = true;
    // unpacking a FLAC pack and hashing the result happen off the message thread; the file is used only if it is
    // exactly what was announced
    const auto part = w.file.withFileExtension (w.file.getFileExtension() + ".part"), file = w.file;
    const bool packed = w.packed;
    auto check = [part, file, packed, sha]() -> bool {
        auto source = part;
        if (packed)
        {
            source = file.withFileExtension (file.getFileExtension() + ".unpacked");
            const bool unpacked = mediapack::unpack (part, source);
            part.deleteFile();
            if (! unpacked)
            {
                source.deleteFile();
                return false;
            }
        }
        if (sessionpack::hashFile (source) != sha || ! source.moveFileTo (file))
        {
            source.deleteFile();
            return false;
        }
        return true;
    };
    if (! io)
        return finishMedia (sha, check());
    io->post ([this, check, sha]() -> Worker::Done {
        const bool ok = check();
        return [this, sha, ok] { finishMedia (sha, ok); };
    });
}

void LiveSessionManager::finishMedia (const juce::String& sha, bool ok)
{
    auto it = wanted.find (sha);
    if (it == wanted.end())
        return;
    const auto w = it->second;
    wanted.erase (it);
    auto part = w.file.withFileExtension (w.file.getFileExtension() + ".part");
    if (! ok)
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
// --- reconnect / resume (round 169j, ONLINE_SESSIONS.md §1.5) -------------------------------------------------------

void LiveSessionManager::scheduleReconnect()
{
    reconnectAtMs = juce::Time::currentTimeMillis() + reconnectDelayMs;
    reconnectDelayMs = std::min (30000, reconnectDelayMs * 2);
}

void LiveSessionManager::lostConnection (const juce::String& why)
{
    link.close();
    (void) link.events(); // (what the closed connection still had to say)
    fragments.clear();
    uploads.clear();
    if (state != State::reconnecting)
    {
        droppedAtMs = juce::Time::currentTimeMillis();
        reconnectDelayMs = 1000;
        if (host)
        {
            // every guest will come back on their own (resume): keep their places
            for (auto& [n, p] : peers)
                if (p.accepted)
                {
                    auto keep = p;
                    keep.accepted = false;
                    keep.relayPeer = 0;
                    away[keep.tag] = keep;
                }
            peers.clear();
            forward.clear();
            sendPeople (true);
        }
        else
            hostPeer = 0;
        setState (State::reconnecting, why + " " + tr ("Connecting again..."));
        if (onHint)
            onHint (why + " " + tr ("Connecting again: keep working, nothing is lost."));
    }
    scheduleReconnect();
}

void LiveSessionManager::sendSeek (int to)
{
    if (seekNonce.isEmpty())
        seekNonce = randomHex (16);
    auto m = message ("seek");
    set (m, "nonce", seekNonce);
    sendJson (to, m);
    lastSeekMs = juce::Time::currentTimeMillis();
}

void LiveSessionManager::onSeek (int from, const juce::var& m)
{
    // someone (a guest that reconnected, or one that saw us come back) asks who the host is: we prove it
    const auto nonce = m["nonce"].toString();
    if (nonce.length() < 16 || nonce.length() > 64 || state != State::live)
        return;
    const auto sig = livecrypto::signHost (hostSeed, utf8 (roomId), utf8 (nonce));
    auto answer = message ("host");
    set (answer, "nonce", nonce);
    set (answer, "sig", juce::String (livecrypto::toHex (sig)));
    sendJson (from, answer);
}

void LiveSessionManager::onHostAnswer (int from, const juce::var& m)
{
    if (state != State::waitingHost && ! (state == State::live && from != hostPeer))
        return;
    livecrypto::Signature sig {};
    if (seekNonce.isEmpty() || m["nonce"].toString() != seekNonce || ! livecrypto::fromHex (utf8 (m["sig"].toString()), sig)
        || ! livecrypto::checkHost (hostKey, utf8 (roomId), utf8 (seekNonce), sig))
        return; // (not the host of this session)
    seekNonce.clear();
    hostPeer = from;
    reroute (session::hostTag, from);
    resuming = true;
    sendResume();
}

void LiveSessionManager::sendResume()
{
    auto m = message ("resume");
    set (m, "tag", (int) me);
    set (m, "token", resumeToken);
    set (m, "rev", (juce::int64) (replica ? replica->revision() : 0));
    sendJson (hostPeer, m);
    setState (State::waitingHost, tr ("Catching up with the session..."));
}

void LiveSessionManager::reroute (std::uint32_t tag, int peer)
{
    // media that were coming from that participant come again from where they are now (from the start)
    for (auto& [sha, w] : wanted)
        if (w.fromTag == tag && ! w.verifying)
        {
            w.file.withFileExtension (w.file.getFileExtension() + ".part").deleteFile();
            w.fromPeer = peer;
            w.got = 0;
            w.packed = false;
            auto m = message ("want");
            set (m, "sha", sha);
            sendJson (peer, m);
        }
}

void LiveSessionManager::onResume (int from, const juce::var& m)
{
    const auto tag = (std::uint32_t) (int) m["tag"];
    const auto token = m["token"].toString();
    std::optional<Peer> back;
    if (auto it = away.find (tag); it != away.end())
        back = it->second;
    for (auto it = peers.begin(); it != peers.end(); ++it)
        if (it->second.accepted && it->second.tag == tag)
        {
            back = it->second; // (its old connection is not gone yet on the relay)
            peers.erase (it);
            break;
        }
    if (! back || token.isEmpty() || back->token != token)
        return deny (from, "resume");
    away.erase (tag);
    back->relayPeer = from;
    back->accepted = true;
    back->asking = false;
    peers[from] = *back;
    authority->setPermissions (tag, back->perms);
    reroute (tag, from);
    const auto rev = (std::uint64_t) (juce::int64) m["rev"];
    if (auto missed = authority->since (rev))
    {
        for (const auto& c : *missed)
            sendJson (from, commitToVar (c));
        auto done = message ("resumed");
        set (done, "rev", (juce::int64) authority->revision());
        sendJson (from, done);
        ++stats.replayed;
    }
    else
    {
        sendSnapshot (from, true);
        ++stats.resumedBySnapshot;
    }
    sendPeople (true);
    broadcastNeeds();
    journalDirty = true;
    lastTransportMs = 0;
    if (onHint)
        onHint (tr ("{0} is back in the live session", { back->name }));
}

void LiveSessionManager::dropConnection()
{
    if (state == State::live || state == State::waitingHost)
        lostConnection (tr ("The connection to the session was lost."));
}

void LiveSessionManager::abandon()
{
    if (! isActive())
        return;
    abandoned = true;
    stopTimer();
    link.close();
    io.reset(); // (a journal being written is finished first)
    renderer.reset();
    authority.reset();
    replica.reset();
    peers.clear();
    away.clear();
    state = State::ended;
}

// --- the host's journal ---------------------------------------------------------------------------------------------

juce::var LiveSessionManager::permsToVar (const live::Permissions& p, bool full) const
{
    auto o = object();
    set (o, "editEverything", p.editEverything);
    set (o, "listener", p.listener);
    set (o, "settings", p.settings);
    set (o, "keepFull", full);
    return o;
}

void LiveSessionManager::writeJournal (bool now)
{
    (void) now;
    journalDirty = false;
    journalDueMs = juce::Time::currentTimeMillis() + 2000; // (at most every 2 s)
    if (! host || ! authority || journalFile == juce::File() || roomId.isEmpty() || hostToken.isEmpty())
        return;
    auto j = object();
    set (j, "version", 1);
    set (j, "saved", juce::Time::currentTimeMillis());
    set (j, "relay", options.relay);
    set (j, "room", roomId);
    set (j, "secret", str (invite.secret));
    set (j, "hostToken", hostToken);
    set (j, "seed", juce::String (livecrypto::toHex (hostSeed)));
    set (j, "name", hostName());
    set (j, "editEverything", editEverything);
    set (j, "keepFull", keepFull);
    set (j, "projectFile", options.projectFile.getFullPathName());
    set (j, "mediaFolder", mediaFolder.getFullPathName());
    set (j, "rev", (juce::int64) authority->revision());
    juce::Array<juce::var> guests;
    auto addGuest = [&] (const Peer& p) {
        auto g = permsToVar (p.perms, p.keepFull);
        set (g, "tag", (int) p.tag);
        set (g, "name", p.name);
        set (g, "token", p.token);
        set (g, "colour", (juce::int64) p.colour);
        guests.add (g);
    };
    for (const auto& [n, p] : peers)
        if (p.accepted)
            addGuest (p);
    for (const auto& [t, p] : away)
        addGuest (p);
    set (j, "guests", guests);
    juce::Array<juce::var> changes;
    for (const auto& [k, c] : authority->changes())
    {
        juce::Array<juce::var> row;
        row.add (str (live::toString (k)));
        row.add ((juce::int64) c.rev);
        row.add ((int) c.origin);
        changes.add (row);
    }
    set (j, "changes", changes);
    set (j, "song", ProjectFile::toJson (authority->confirmed()));
    const auto text = juce::JSON::toString (j, true);
    const auto file = journalFile;
    auto write = [file, text]() -> Worker::Done {
        file.getParentDirectory().createDirectory();
        juce::TemporaryFile temp (file);
        if (temp.getFile().replaceWithText (text))
            temp.overwriteTargetFileWithTemporary();
        return {};
    };
    if (io)
        io->post (write);
    else
        write();
}

std::optional<LiveSessionManager::JournalInfo> LiveSessionManager::readJournal (const juce::File& file)
{
    if (! file.existsAsFile() || file.getSize() > ((juce::int64) 256 << 20))
        return std::nullopt;
    const auto j = juce::JSON::parse (file.loadFileAsString());
    if (! j.isObject() || (int) j["version"] != 1)
        return std::nullopt;
    livecrypto::Invite invite { utf8 (j["room"].toString()), utf8 (j["secret"].toString()) };
    if (! livecrypto::parseCode (livecrypto::formatCode (invite)))
        return std::nullopt;
    JournalInfo info;
    info.name = j["name"].toString();
    info.code = str (livecrypto::formatCode (invite));
    info.saved = juce::Time ((juce::int64) j["saved"]);
    info.revision = (std::uint64_t) (juce::int64) j["rev"];
    info.projectFile = juce::File (j["projectFile"].toString());
    if (const auto* g = j["guests"].getArray())
        info.guests = g->size();
    // (the relay forgets a room 12 h after its last connection)
    if (juce::Time::getCurrentTime() - info.saved > juce::RelativeTime::hours (12))
        return std::nullopt;
    return info;
}

void LiveSessionManager::discardJournal (const juce::File& file, const juce::String& relayDefault)
{
    const auto j = juce::JSON::parse (file.loadFileAsString());
    file.deleteFile();
    const auto room = j["room"].toString(), token = j["hostToken"].toString();
    auto relay = j["relay"].toString();
    if (relay.isEmpty())
        relay = relayDefault;
    if (room.isEmpty() || token.isEmpty() || relay.isEmpty())
        return;
    const auto url = relay.trimCharactersAtEnd ("/") + "/v1/rooms/" + room;
    std::thread ([url, token] {
        int status = 0;
        juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                               .withHttpRequestCmd ("DELETE")
                                               .withExtraHeaders ("Authorization: Bearer " + token)
                                               .withConnectionTimeoutMs (8000)
                                               .withStatusCode (&status));
    }).detach();
}

bool LiveSessionManager::resumeHost (const juce::File& file, const Options& o)
{
    if (isActive())
        return false;
    const auto j = juce::JSON::parse (file.loadFileAsString());
    if (! j.isObject() || (int) j["version"] != 1)
        return false;
    livecrypto::Invite inv { utf8 (j["room"].toString()), utf8 (j["secret"].toString()) };
    const auto parsed = livecrypto::parseCode (livecrypto::formatCode (inv));
    livecrypto::Seed seed {};
    juce::String error;
    auto song = ProjectFile::fromJson (j["song"].toString(), error);
    if (! parsed || ! livecrypto::fromHex (utf8 (j["seed"].toString()), seed) || ! song || j["hostToken"].toString().isEmpty())
        return false;
    std::string why;
    if (! livelink::Connection::available (&why))
        return false;
    options = o;
    if (options.relay.isEmpty() || j["relay"].toString().isNotEmpty())
        options.relay = j["relay"].toString().isNotEmpty() ? j["relay"].toString() : options.relay;
    if (options.name.isEmpty())
        options.name = j["name"].toString();
    if (options.projectFile == juce::File())
        options.projectFile = juce::File (j["projectFile"].toString());
    options.guestsEditEverything = (bool) j["editEverything"];
    options.guestsKeepFull = (bool) j["keepFull"];
    host = true;
    me = session::hostTag;
    peers.clear();
    away.clear();
    stats = {};
    chat.clear();
    unread = 0;
    following = false;
    abandoned = resuming = false;
    allNeed.clear();
    myNeed.clear();
    renderedSig.clear();
    hostSeed = seed;
    journalFile = file;
    io = std::make_unique<Worker> (false, false);
    renderer = std::make_unique<Worker> (options.inlineWork, true);
    invite = *parsed;
    keys = livecrypto::derive (invite);
    roomId = str (invite.room);
    hostToken = j["hostToken"].toString();
    inviteCode = str (livecrypto::formatCode (invite));
    keepFull = options.guestsKeepFull;
    editEverything = options.guestsEditEverything;
    mediaFolder = juce::File (j["mediaFolder"].toString());
    if (mediaFolder == juce::File() || ! juce::File::isAbsolutePath (j["mediaFolder"].toString()))
        mediaFolder = sessionpack::sessionMediaFolder (options.projectFile, *song, "Live session");
    const auto rev = (std::uint64_t) (juce::int64) j["rev"];
    song->session.me = session::hostTag;
    if (onResumed)
        onResumed (*song);
    else
        engine.setSong (*song);
    authority = std::make_unique<live::Authority> (*song, &roundTrip, rev);
    std::map<live::Key, live::Authority::Change> changes;
    if (const auto* rows = j["changes"].getArray())
        for (const auto& row : *rows)
            if (auto k = live::keyFromString (utf8 (row[0].toString())))
                changes[*k] = { (std::uint64_t) (juce::int64) row[1], (std::uint32_t) (int) row[2] };
    authority->restoreChanges (std::move (changes));
    replica = std::make_unique<live::Replica> (*song, rev, session::hostTag, &roundTrip);
    if (const auto* g = j["guests"].getArray())
        for (const auto& v : *g)
        {
            Peer p;
            p.tag = (std::uint32_t) (int) v["tag"];
            p.name = v["name"].toString().substring (0, 40);
            p.token = v["token"].toString();
            p.colour = (std::uint32_t) (juce::int64) v["colour"];
            p.perms.editEverything = (bool) v["editEverything"];
            p.perms.listener = (bool) v["listener"];
            p.perms.settings = (bool) v["settings"];
            p.keepFull = (bool) v["keepFull"];
            if (p.tag > session::hostTag && p.token.isNotEmpty())
            {
                away[p.tag] = p;
                authority->setPermissions (p.tag, p.perms);
            }
        }
    lastStamp = engine.getSongStamp();
    engine.setSessionHistory (true);
    ensureHashed (*song);
    updateNeeds();
    sealer.reset();
    droppedAtMs = juce::Time::currentTimeMillis();
    reconnectDelayMs = 1000;
    setState (State::reconnecting, tr ("Resuming the live session..."));
    connect();
    return true;
}

// --- chat, transport, permissions ---------------------------------------------------------------------------------------

void LiveSessionManager::addChat (ChatLine line)
{
    chat.push_back (line);
    while (chat.size() > live::chatKeep)
        chat.pop_front();
    if (! line.mine)
        ++unread;
    if (onChat)
        onChat (line);
    if (onChange)
        onChange();
}

void LiveSessionManager::markChatRead()
{
    if (unread == 0)
        return;
    unread = 0;
    if (onChange)
        onChange();
}

void LiveSessionManager::sendChat (const juce::String& text)
{
    const auto clean = str (live::cleanChat (utf8 (text)));
    if (clean.isEmpty() || state != State::live)
        return;
    if (! host)
    {
        auto m = message ("chat");
        set (m, "text", clean);
        sendJson (hostPeer, m);
        return; // (it comes back from the host, in everyone's order)
    }
    ChatLine line;
    line.tag = session::hostTag;
    line.name = hostName();
    line.text = clean;
    line.colour = colourOf (session::hostTag);
    line.at = juce::Time::currentTimeMillis();
    line.mine = true;
    addChat (line);
    auto m = message ("chat");
    set (m, "tag", (int) line.tag);
    set (m, "name", line.name);
    set (m, "text", line.text);
    set (m, "colour", (juce::int64) line.colour);
    set (m, "at", line.at);
    sendToGuests (m);
}

void LiveSessionManager::onChatMessage (int from, const juce::var& m)
{
    auto it = peers.find (from);
    const auto clean = str (live::cleanChat (utf8 (m["text"].toString())));
    if (it == peers.end() || clean.isEmpty())
        return;
    ChatLine line;
    line.tag = it->second.tag;
    line.name = it->second.name;
    line.text = clean;
    line.colour = it->second.colour;
    line.at = juce::Time::currentTimeMillis();
    addChat (line);
    auto out = message ("chat");
    set (out, "tag", (int) line.tag);
    set (out, "name", line.name);
    set (out, "text", line.text);
    set (out, "colour", (juce::int64) line.colour);
    set (out, "at", line.at);
    sendToGuests (out);
}

void LiveSessionManager::sendTransport()
{
    const auto& t = engine.getTransport();
    const bool playing = t.playing.load();
    const auto ticks = engine.songPlayheadTicks();
    const auto bpm = t.bpm.load();
    const auto now = juce::Time::currentTimeMillis();
    const auto expected = sentPlaying ? sentTicks + (double) (now - lastTransportMs) * bpm * (double) ppq / 60000.0 : sentTicks;
    const bool due = lastTransportMs == 0 || playing != sentPlaying || (playing && (std::abs (ticks - expected) > ppq / 2 || now - lastTransportMs > 2000))
                     || (! playing && std::abs (ticks - sentTicks) > 1.0);
    if (! due)
        return;
    sentPlaying = playing;
    sentTicks = ticks;
    lastTransportMs = now;
    auto m = message ("transport");
    set (m, "playing", playing);
    set (m, "song", engine.getSong().mode == PlayMode::song);
    set (m, "pos", ticks);
    set (m, "bpm", bpm);
    sendToGuests (m);
}

void LiveSessionManager::setFollowTransport (bool on)
{
    following = on && ! host;
    lastFollowMs = 0;
    if (following)
        followHost();
    if (onChange)
        onChange();
}

void LiveSessionManager::followHost()
{
    if (! following || ! haveHostTransport || state != State::live || engine.isRecording())
        return;
    const auto now = juce::Time::currentTimeMillis();
    if (now - lastFollowMs < 700)
        return; // (the last jump is still settling)
    const auto a = live::follow (hostTransport, (double) now, engine.getTransport().playing.load(), engine.getSong().mode == PlayMode::song,
                                 engine.songPlayheadTicks());
    using T = live::FollowAction::Type;
    if (a.type == T::none)
        return;
    lastFollowMs = now;
    ++stats.followed;
    if (a.type == T::stop)
        return engine.stop();
    if ((engine.getSong().mode == PlayMode::song) != a.songMode)
        engine.editSong ([song = a.songMode] (Song& s) { s.mode = song ? PlayMode::song : PlayMode::pattern; }, false);
    engine.seek (a.ticks);
    if (a.type == T::play)
        engine.play();
}

void LiveSessionManager::setPermissions (std::uint32_t tag, live::Permissions p, bool full)
{
    if (! host || ! authority || tag <= session::hostTag)
        return;
    Peer* peer = nullptr;
    int relay = 0;
    for (auto& [n, q] : peers)
        if (q.accepted && q.tag == tag)
            peer = &q, relay = n;
    if (peer == nullptr)
        if (auto it = away.find (tag); it != away.end())
            peer = &it->second;
    if (peer == nullptr)
        return;
    peer->perms = p;
    peer->keepFull = full;
    authority->setPermissions (tag, p);
    if (relay != 0)
    {
        auto m = permsToVar (p, full);
        set (m, "t", "perms");
        sendJson (relay, m);
    }
    journalDirty = true;
    sendPeople (true);
    if (onChange)
        onChange();
}

// --- live stand-ins (ONLINE_SESSIONS.md §4.2) -------------------------------------------------------------------------

void LiveSessionManager::updateNeeds()
{
    const auto now = juce::Time::currentTimeMillis();
    if (engine.getSongStamp() == needStamp || now - lastNeedMs < 500)
        return;
    lastNeedMs = now;
    needStamp = engine.getSongStamp();
    auto need = live::unplayable (engine.getSong(), [this] (const PluginRef& r) { return missingHere (r); });
    if (need == myNeed)
        return;
    myNeed = std::move (need);
    if (host)
        return broadcastNeeds();
    if (hostPeer != 0 && state == State::live)
    {
        auto m = message ("need");
        set (m, "keys", keysToVar (myNeed));
        sendJson (hostPeer, m);
    }
}

void LiveSessionManager::broadcastNeeds()
{
    if (! host)
        return;
    auto all = myNeed;
    for (const auto& [n, p] : peers)
        if (p.accepted)
            all.insert (p.need.begin(), p.need.end());
    if (all == allNeed)
        return;
    allNeed = std::move (all);
    auto m = message ("needs");
    set (m, "keys", keysToVar (allNeed));
    sendToGuests (m);
}

void LiveSessionManager::maybeRenderStandIns()
{
    const auto now = juce::Time::currentTimeMillis();
    if (rendering || ! renderer || state != State::live || allNeed.empty() || now - lastRenderCheckMs < 500)
        return;
    lastRenderCheckMs = now;
    // (debounced after the last change; never while this computer plays or records)
    if (engine.getTransport().playing.load() || engine.isRecording() || now - lastLocalChangeMs < options.standInDelayMs)
        return;
    const auto& song = engine.getSong();
    const auto keys = live::toRender (song, allNeed, me, [this] (const PluginRef& r) { return missingHere (r); });
    std::vector<sessionpack::RenderItem> items;
    std::vector<std::pair<live::Key, std::string>> sigs;
    for (const auto& k : keys)
    {
        sessionpack::RenderItem item;
        std::string sig;
        std::vector<const PluginRef*> refs;
        if (k.kind == live::Kind::channel)
        {
            int index = -1;
            for (int i = 0; i < (int) song.channels.size(); ++i)
                if (song.channels[(std::size_t) i].uid == k.a)
                    index = i;
            if (index < 0)
                continue;
            const auto& c = song.channels[(std::size_t) index];
            item = { false, index, standins::needsTimeline (song, index), str (c.name), {} };
            standins::pluginsOf (c, refs);
            if (item.timeline)
                sig = sessionpack::sourceHash (song, false, index, -1, standInRate);
            else
                for (int p = 0; p < (int) song.patterns.size(); ++p)
                {
                    const auto& pat = song.patterns[(std::size_t) p];
                    if (std::any_of (pat.notes.begin(), pat.notes.end(), [&] (const Note& n) { return n.channel == index; }))
                        sig += sessionpack::sourceHash (song, false, index, p, standInRate) + ";";
                }
            // (what is already on the channel and still matches needs no render: e.g. after a restart)
            if (sig.empty() && c.standIns.empty())
                continue; // (no notes anywhere: nothing to play)
        }
        else
        {
            const auto t = (int) k.a;
            const auto& track = song.mixer[(std::size_t) t];
            item = { true, t, true, track.name.empty() ? "Insert " + juce::String (t) : str (track.name), {} };
            standins::pluginsOf (track, refs);
            sig = sessionpack::sourceHash (song, true, t, -1, standInRate);
        }
        if (renderedSig[k] == sig)
            continue;
        juce::StringArray names;
        for (const auto* r : refs)
            names.addIfNotAlreadyThere (str (r->name));
        item.plugins = names.joinIntoString (", ");
        items.push_back (item);
        sigs.push_back ({ k, sig });
    }
    if (items.empty())
        return;
    rendering = true;
    const auto folder = host ? sessionpack::standInFolder (options.projectFile, song).getChildFile ("Live session")
                             : mediaFolder.getChildFile ("Stand-ins");
    const auto by = host ? hostName() : (options.name.isNotEmpty() ? options.name : juce::String ("Guest"));
    if (onChange)
        onChange();
    renderer->post ([this, copy = song, items, folder, by, sigs]() -> Worker::Done {
        std::vector<sessionpack::Rendered> out;
        juce::String error;
        const bool ok = sessionpack::render (copy, items, standInRate, folder, by, out, {}, error);
        // their hashes now, here: the change that carries them goes out without waiting
        std::vector<std::pair<juce::File, juce::String>> hashed;
        for (const auto& r : out)
            for (const auto& st : r.standIns)
            {
                const juce::File f (str (st.path));
                hashed.push_back ({ f, sessionpack::hashFile (f) });
            }
        return [this, ok, out, error, sigs, hashed] {
            rendering = false;
            if (! isActive())
                return;
            for (const auto& [f, sha] : hashed)
                if (sha.isNotEmpty())
                    hashCache[f.getFullPathName()] = { f.getSize(), f.getLastModificationTime().toMilliseconds(), sha };
            if (! ok)
            {
                if (onHint)
                    onHint (tr ("A stand-in for the others could not be rendered: {0}", { error }));
                for (const auto& [k, sig] : sigs)
                    renderedSig[k] = sig; // (not again and again: at the next change)
                return;
            }
            engine.editSong ([&] (Song& s) { sessionpack::apply (s, out); }, false);
            for (const auto& [k, sig] : sigs)
                renderedSig[k] = sig;
            stats.standInsRendered += (int) out.size();
            if (onChange)
                onChange();
        };
    });
}
} // namespace lynx
