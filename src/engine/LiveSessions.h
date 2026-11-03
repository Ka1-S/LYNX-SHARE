#pragma once

// Online Sessions, phase 2 (docs/design/ONLINE_SESSIONS.md, round 169b): a live session of this LYNX SHARE — as the
// host (the authority: core/LiveSession.h's Authority, and the room on the relay) or as a guest — over the LYNX SHARE
// relay (engine/LiveLink.h, end-to-end encrypted with the invite code's key, core/LiveCrypto.h).
//
// The wire (inside the relay's [peer] frames): [1][sealed frame] or [2][u32 id][u16 index][u16 count][part of a
// sealed frame] for the ones over 512 KiB. A sealed frame's plain text is ['J'][JSON] / ['Z'][gzip JSON] / ['M'][media
// chunk]. The JSON messages: hello / welcome / deny / snapshot / propose / commit / people / presence / want / bye /
// end / resync. Sample paths travel as "lynx-media:<sha256>/<name>"; a received change may only point at this
// session's media folder (sessionpack::confine), and media are only sent when the sender's own song uses them.

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

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
    };

    struct Person
    {
        std::uint32_t tag = 0;
        juce::String name;
        std::uint32_t colour = 0;
        bool host = false, me = false, online = false, playing = false, recording = false;
        double position = 0.0; // the playhead (ticks)
    };

    // The host: opens a room on the relay (background) and waits for guests; the invite code is ready when the state is
    // live. Needs nothing saved: guests' media then go under Documents/LYNX/Session media.
    void start (const Options& options);
    // A guest: joins with the invite code (the project replaces the open one when it has arrived — the caller asked
    // about unsaved changes first).
    void join (const juce::String& code, const Options& options);
    // The guest leaves (keeps his part, or a copy if the host allowed it); the host ends the session for everybody.
    void leave();

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

    // tests / scripted runs: a message to every guest (as the host) is counted here
    struct Stats
    {
        int sent = 0, received = 0, commits = 0, rejected = 0, mediaSent = 0, mediaReceived = 0;
        juce::int64 bytesSent = 0, bytesReceived = 0;
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
    };
    struct Want
    {
        int fromPeer = 0;
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
    };
    std::deque<Upload> uploads;
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
    void handleMediaChunk (int from, const std::uint8_t* data, std::size_t size);
    // sends
    void sendRaw (int dest, const std::vector<std::uint8_t>& plain);
    void sendJson (int dest, const juce::var& message);
    void sendToGuests (const juce::var& message);
    // host
    void onHello (int from, const juce::var& m);
    void accept (int from);
    void sendSnapshot (int to);
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
    void finishMedia (const juce::String& sha);
    juce::String nameOf (std::uint32_t tag) const;
    void releaseHeld();
    bool holding() const;
    void onRoomCreated (const juce::String& room, const juce::String& hostToken);
    void connect();
    void request (const juce::String& sha, const juce::String& name, int fromPeer);
    void pumpUploads();
    void deny (int to, const juce::String& reason);
    juce::String hostName() const;
};
} // namespace lynx
