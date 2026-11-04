#pragma once

// Online Sessions, phase 2 (docs/design/ONLINE_SESSIONS.md, round 169b): a live session of this LYNX SHARE — as the
// host (the authority: core/LiveSession.h's Authority, and the room on the relay) or as a guest — over the LYNX SHARE
// relay (engine/LiveLink.h, end-to-end encrypted with the invite code's key, core/LiveCrypto.h).
//
// The wire (inside the relay's [peer] frames): [1][sealed frame] or [2][u32 id][u16 index][u16 count][part of a
// sealed frame] for the ones over 512 KiB. A sealed frame's plain text is ['J'][JSON] / ['Z'][gzip JSON] / ['M'][media
// chunk]. The JSON messages: hello / welcome / deny / snapshot / propose / commit / people / presence / want / bye /
// end / resync; round 169j adds seek / host / back / resume / resumed (reconnect, §1.5), perms, chat, transport, need /
// needs (live stand-ins) and 'F' frames (a medium as a lossless FLAC pack, engine/MediaPack.h). Sample paths travel as "lynx-media:<sha256>/<name>"; a received change may only point at this
// session's media folder (sessionpack::confine), and media are only sent when the sender's own song uses them.

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "core/LiveCrypto.h"
#include "core/LiveSession.h"
#include "engine/LiveLink.h"

namespace lynx
{
class AudioEngine;

class LiveSessionManager final : private juce::Timer
{
public:
    explicit LiveSessionManager (AudioEngine& engine);
    ~LiveSessionManager() override;

    enum class State
    {
        idle,
        starting,   // host: opening the room
        connecting, // the WebSocket
        knocking,   // guest: waiting for the host to accept
        loading,    // guest: the project is arriving
        live,
        reconnecting, // this computer's connection dropped: it connects again by itself (edits wait, nothing is lost)
        waitingHost,  // guest: the host's connection dropped (or its LYNX SHARE restarts): waiting for it to come back
        ended
    };

    struct Options
    {
        juce::String relay = "https://relay.lynx-share.com"; // (tests: a local relay, http://127.0.0.1:port)
        juce::String name;                    // this person's display name
        bool guestsEditEverything = false;    // host: every guest may edit everything (else his own area)
        bool autoAccept = false;              // host: people with the code join without a question (scripted runs)
        bool guestsKeepFull = false;          // host: a guest keeps a full copy at the end (the user's decision 1)
        juce::File projectFile;               // host: the open project (where guests' media go)
        juce::File folder;                    // where this session's received media go (default: the profile's cache)
        // host: the journal a restarted LYNX SHARE resumes the session from (default: the profile's
        // "Live session/journal.json"; tests: their own)
        juce::File journal;
        int reconnectForMs = 15 * 60 * 1000;  // how long a dropped computer keeps trying (and guests wait for the host)
        int standInDelayMs = 2000;            // live stand-ins render this long after the part's last change
        bool inlineWork = false;              // command-line runs without a message loop: stand-ins render in pump()
        // tests: whether a plugin is missing on this computer (default: the plugin host's own list)
        std::function<bool (const PluginRef&)> missing;
    };

    // What a host's journal says (the start-up question "resume the session?").
    struct JournalInfo
    {
        juce::String name, code;
        juce::Time saved;
        int guests = 0;
        std::uint64_t revision = 0;
        juce::File projectFile;
    };
    static juce::File defaultJournal();
    static std::optional<JournalInfo> readJournal (const juce::File& journal);
    static void discardJournal (const juce::File& journal, const juce::String& relay); // (and closes its room)

    struct Person
    {
        std::uint32_t tag = 0;
        juce::String name;
        std::uint32_t colour = 0;
        bool host = false, me = false, online = false, playing = false, recording = false;
        double position = 0.0; // the playhead (ticks)
        live::Permissions permissions; // (host: what this guest may do; guests: their own)
        bool keepFull = false;
    };

    struct ChatLine
    {
        std::uint32_t tag = 0;
        juce::String name, text;
        std::uint32_t colour = 0;
        juce::int64 at = 0; // ms since 1970 (the host's clock)
        bool mine = false;
    };

    // The host: opens a room on the relay (background) and waits for guests; the invite code is ready when the state is
    // live. Needs nothing saved: guests' media then go under Documents/LYNX/Session media.
    void start (const Options& options);
    // A guest: joins with the invite code (the project replaces the open one when it has arrived — the caller asked
    // about unsaved changes first).
    void join (const juce::String& code, const Options& options);
    // The guest leaves (keeps his part, or a copy if the host allowed it); the host ends the session for everybody.
    void leave();
    // The host's LYNX SHARE restarted: the session goes on from its journal (`onResumed` gets the song first); the
    // guests, waiting, reconnect by themselves.
    bool resumeHost (const juce::File& journal, const Options& options);

    // Session chat (round 169j): to everyone in the session.
    void sendChat (const juce::String& text);
    const std::deque<ChatLine>& getChat() const noexcept { return chat; }
    int unreadChat() const noexcept { return unread; }
    void markChatRead();
    // Follow the host's transport (guests): play, stop and jump with the host.
    void setFollowTransport (bool on);
    bool isFollowingTransport() const noexcept { return following; }
    // The host changes what a guest may do (and what the guest keeps at the end) during the session.
    void setPermissions (std::uint32_t tag, live::Permissions permissions, bool keepFull);
    live::Permissions myPermissions() const noexcept { return myPerms; }
    int standInsRendering() const noexcept { return rendering ? 1 : 0; }

    // tests: the connection drops as if the network went away (the manager reconnects by itself); abandon() stops
    // everything without a word to anyone, keeping the journal (what a crash of LYNX SHARE does)
    void dropConnection();
    void abandon();

    State getState() const noexcept { return state; }
    bool isActive() const noexcept { return state != State::idle && state != State::ended; }
    bool isHost() const noexcept { return host; }
    juce::String getInviteCode() const { return inviteCode; }
    juce::String getStatus() const { return status; } // one line for the presence bar / dialogs
    std::vector<Person> getPeople() const;
    int waitingChanges() const;  // commits held while this computer records
    int mediaDownloading() const noexcept { return (int) wanted.size(); }
    std::uint32_t myTag() const noexcept { return me; }

    // the UI's hooks (message thread)
    std::function<void()> onChange;                            // state, people or status changed
    std::function<void (const juce::String&)> onHint;          // a short message for the hint bar
    std::function<void (const juce::String& name, std::function<void (bool accept)> answer)> onKnock; // host: ask
    std::function<void (const juce::String& message)> onEnded; // the session is over (why, what is kept)
    // A guest's project arrived: the caller makes it the open project (no file yet, unsaved; default: engine.setSong).
    std::function<void (Song song)> onJoined;
    // The host resumed from its journal: the caller opens this song (default: engine.setSong).
    std::function<void (Song song)> onResumed;
    std::function<void (const ChatLine& line)> onChat; // a chat line arrived (or was sent)

    // tests / scripted runs: a message to every guest (as the host) is counted here
    struct Stats
    {
        int sent = 0, received = 0, commits = 0, rejected = 0, mediaSent = 0, mediaReceived = 0;
        juce::int64 bytesSent = 0, bytesReceived = 0;
        int reconnects = 0, replayed = 0, resumedBySnapshot = 0, standInsRendered = 0, packedSent = 0, followed = 0;
        juce::int64 packedSaved = 0; // bytes FLAC saved on the media sent
        int hashedOffThread = 0;
    };
    Stats getStats() const noexcept { return stats; }
    // Does what the timer does (network events, local edits, presence, media): command-line runs without a message
    // loop call it themselves.
    void pump();
    // the confirmed revision (host: the authority's)
    std::uint64_t revision() const;

    // the serialiser's round trip (the .lynx JSON): what every peer receives (also used by the tests)
    static live::Patch roundTrip (const live::Patch& patch);
    static juce::var patchToVar (const live::Patch& patch);
    static live::Patch patchFromVar (const juce::var& v);

private:
    struct Peer
    {
        int relayPeer = 0;
        std::uint32_t tag = 0;
        juce::String name, version;
        std::uint32_t colour = 0;
        bool accepted = false, asking = false;
        bool playing = false, recording = false;
        double position = 0.0;
        juce::String token;           // what it resumes with after a drop
        live::Permissions perms;
        bool keepFull = false;
        std::set<live::Key> need;     // parts it cannot play
        bool lostRev = false;         // it saw commits this host lost (a restart from an older journal)
    };
    struct Want
    {
        int fromPeer = 0;
        std::uint32_t fromTag = 0;    // (the peer number changes when it reconnects)
        bool packed = false;          // FLAC pack pieces ('F')
        bool verifying = false;       // complete: being checked off the message thread
        juce::String name;
        juce::File file;
        int count = 0, got = 0;
        juce::int64 size = 0;
        std::vector<bool> have;
    };

    AudioEngine& engine;
    Options options;
    State state = State::idle;
    bool host = false;
    juce::String status, inviteCode;
    livecrypto::Invite invite;
    livecrypto::Keys keys;
    std::unique_ptr<livecrypto::Sealer> sealer;
    std::unique_ptr<livecrypto::Opener> opener;
    livelink::Connection link;
    juce::String roomId, hostToken, wsUrl;
    int myPeer = 0, hostPeer = 0;
    std::uint32_t me = session::hostTag;
    std::unique_ptr<live::Authority> authority;
    std::unique_ptr<live::Replica> replica;
    std::uint64_t lastStamp = 0;
    std::map<int, Peer> peers; // by relay peer number
    std::uint32_t fragmentId = 0;
    std::map<int, std::map<std::uint32_t, std::vector<std::vector<std::uint8_t>>>> fragments;
    // media
    juce::File mediaFolder;
    std::map<juce::String, juce::File> served;     // sha -> a file of ours the song uses (we may send it)
    struct Hashed
    {
        juce::int64 size = 0, modified = 0;
        juce::String sha;
    };
    std::map<juce::String, Hashed> hashCache;      // path -> its hash
    std::map<juce::String, Want> wanted;           // sha -> what is arriving
    std::map<juce::String, std::vector<int>> forward; // sha -> peers waiting for it (host: a guest's file not here yet)
    struct Upload
    {
        int peer = 0;
        juce::String sha;
        juce::File file;
        int next = 0;
        bool packed = false;
    };
    std::deque<Upload> uploads;
    // 169j: work off the message thread (hashing, FLAC packs, checking what arrived) and its answers
    struct Worker;
    std::unique_ptr<Worker> io, renderer;
    std::set<juce::String> hashing;             // paths being hashed
    std::map<juce::String, juce::File> packs;   // sha -> its FLAC pack (sent instead of the file)
    std::set<juce::String> packing, unpackable; // packs being made / files FLAC does not take
    std::map<int, bool> pendingSnapshots;       // snapshots waiting for hashes (peer -> a resume)
    // reconnect
    std::map<std::uint32_t, Peer> away;         // host: guests whose connection dropped (by tag)
    livecrypto::Seed hostSeed {};               // host: its identity
    livecrypto::PublicKey hostKey {};           // guest: the host's
    juce::String resumeToken, seekNonce;        // guest
    juce::int64 droppedAtMs = 0, reconnectAtMs = 0, lastSeekMs = 0;
    int reconnectDelayMs = 1000;
    bool resuming = false, abandoned = false;
    // journal (host)
    juce::File journalFile;
    bool journalDirty = false;
    juce::int64 journalDueMs = 0;
    // chat, transport, permissions
    std::deque<ChatLine> chat;
    int unread = 0;
    bool following = false;
    live::HostTransport hostTransport;
    bool haveHostTransport = false;
    juce::int64 lastFollowMs = 0, lastTransportMs = 0;
    bool sentPlaying = false;
    double sentTicks = 0.0;
    live::Permissions myPerms;
    // live stand-ins
    std::set<live::Key> myNeed, allNeed;
    std::map<juce::String, std::pair<int, juce::String>> mediaOrigin; // host: sha -> (peer, name) whose song brought it (fetched when asked)
    std::map<live::Key, std::string> renderedSig;
    juce::int64 lastNeedMs = 0, lastLocalChangeMs = 0, lastEventMs = 0, lastPingMs = 0, lastRenderCheckMs = 0;
    std::uint64_t needStamp = 0;
    bool rendering = false;
    std::vector<Person> people;   // as of the last "people" (guests) / recomputed (host)
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    bool keepFull = false, editEverything = false;
    juce::int64 lastPresenceMs = 0, lastPeopleMs = 0, lastPollMs = 0;
    juce::String lastPresence, lastPeople;
    std::thread httpThread;
    struct RoomAnswer
    {
        bool done = false;
        juce::String room, hostToken, error;
    };
    std::mutex roomLock;
    RoomAnswer roomAnswer;
    Stats stats;

    void timerCallback() override;
    void setState (State s, const juce::String& text);
    void end (const juce::String& message, bool keepPart);
    void handleEvent (const livelink::Event& e);
    void handleFrame (int from, std::vector<std::uint8_t> payload);
    void handleMessage (int from, const juce::var& m);
    void handleMediaChunk (int from, const std::uint8_t* data, std::size_t size, bool packed);
    // sends
    void sendRaw (int dest, const std::vector<std::uint8_t>& plain);
    void sendJson (int dest, const juce::var& message);
    void sendToGuests (const juce::var& message);
    // host
    void onHello (int from, const juce::var& m);
    void accept (int from);
    void sendSnapshot (int to, bool resumed = false);
    void onPropose (int from, const juce::var& m);
    void broadcastCommit (const live::Commit& c);
    void sendPeople (bool force);
    // guest
    void onSnapshot (const juce::var& m);
    void onCommit (const juce::var& m);
    void applyUpdate (const live::Replica::Update& u);
    // both
    void pollLocal();
    void sendPresence();
    juce::var commitToVar (const live::Commit& c);
    live::Commit commitFromVar (const juce::var& v, int from);
    void tokenize (Song& song);
    void detokenize (Song& song, int fromPeer);
    juce::String shaOf (const juce::File& f);
    void serve (int to, const juce::String& sha);
    void finishMedia (const juce::String& sha, bool ok);
    juce::String nameOf (std::uint32_t tag) const;
    void releaseHeld();
    bool holding() const;
    void onRoomCreated (const juce::String& room, const juce::String& hostToken);
    void connect();
    void request (const juce::String& sha, const juce::String& name, int fromPeer);
    void pumpUploads();
    void deny (int to, const juce::String& reason);
    juce::String hostName() const;
    // 169j
    bool missingHere (const PluginRef& ref);
    bool ensureHashed (const Song& song);
    void runCompletions();
    void lostConnection (const juce::String& why);
    void scheduleReconnect();
    void sendSeek (int to);
    void onSeek (int from, const juce::var& m);
    void onHostAnswer (int from, const juce::var& m);
    void onResume (int from, const juce::var& m);
    void reroute (std::uint32_t tag, int peer);
    void sendResume();
    void onChatMessage (int from, const juce::var& m);
    void addChat (ChatLine line);
    void sendTransport();
    void followHost();
    void updateNeeds();
    void broadcastNeeds();
    void maybeRenderStandIns();
    void writeJournal (bool now);
    juce::var permsToVar (const live::Permissions& p, bool keepFull) const;
    void verifyArrived (const juce::String& sha);
};
} // namespace lynx
