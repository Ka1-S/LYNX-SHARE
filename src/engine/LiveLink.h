#pragma once

// Online Sessions, phase 2 (round 169b): one WebSocket to the LYNX SHARE relay (docs/design/SHARE_API.md §3) with the
// operating system's own TLS, no new library: WinHTTP's WebSocket API on Windows, the system libcurl (loaded at run
// time, 7.86 or later) on Linux and macOS. A thread per connection reads; sends are queued and written by another
// (Windows) or the same (curl) thread, so the message thread never waits for the network.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lynx::livelink
{
struct Event
{
    enum class Type
    {
        opened, // the WebSocket is up (the relay's "welcome" text follows)
        text,   // a control message from the relay (JSON)
        binary, // [source peer][payload]
        closed  // the connection ended (code: the WebSocket close code, 0 = it could not connect / dropped)
    };
    Type type = Type::opened;
    std::vector<std::uint8_t> bytes;
    std::string text; // text frames; for closed: why
    int code = 0;     // closed: close code; or the HTTP status that refused the upgrade (401, 404, 409, 429)
};

class Connection
{
public:
    Connection();
    ~Connection();

    // Connects in the background (events() tells how it went). url: ws:// or wss://…/v1/rooms/<room>/ws
    void open (const std::string& url, const std::string& token, const std::string& userAgent);
    // Queues one binary message (thread-safe). Messages are sent in order.
    void send (std::vector<std::uint8_t> message);
    void sendText (const std::string& text);
    // What happened since the last call (message thread).
    std::vector<Event> events();
    // Closes (normal close 1000) and waits for the threads.
    void close();
    bool isOpen() const noexcept { return state.load() == 2; }
    // bytes queued and not yet written (the media upload backs off on this)
    std::size_t queuedBytes() const;

    // Whether this computer can open WebSockets at all (curl too old or missing on Linux: false), and why not.
    static bool available (std::string* why = nullptr);

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
    std::atomic<int> state { 0 }; // 0 idle, 1 connecting, 2 open, 3 closed
    mutable std::mutex lock;
    std::condition_variable wake;
    std::deque<std::pair<bool, std::vector<std::uint8_t>>> outgoing; // (binary?, bytes)
    std::size_t outgoingBytes = 0;
    std::vector<Event> incoming;
    std::thread reader, writer;
    std::atomic<bool> stopping { false };
    void push (Event e);
    bool next (std::pair<bool, std::vector<std::uint8_t>>& out, int waitMs);
    void run (std::string url, std::string token, std::string userAgent);
    friend struct Impl;
};
} // namespace lynx::livelink
