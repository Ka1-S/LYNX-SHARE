#include "engine/LiveLink.h"

#include <chrono>
#include <cstring>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <winhttp.h>
#else
 #include <dlfcn.h>
 #include <poll.h>
#endif

namespace lynx::livelink
{
void Connection::push (Event e)
{
    const std::lock_guard<std::mutex> g (lock);
    incoming.push_back (std::move (e));
}

bool Connection::next (std::pair<bool, std::vector<std::uint8_t>>& out, int waitMs)
{
    std::unique_lock<std::mutex> g (lock);
    if (outgoing.empty() && waitMs > 0)
        wake.wait_for (g, std::chrono::milliseconds (waitMs), [this] { return ! outgoing.empty() || stopping.load(); });
    if (outgoing.empty())
        return false;
    out = std::move (outgoing.front());
    outgoing.pop_front();
    outgoingBytes -= out.second.size();
    return true;
}

void Connection::send (std::vector<std::uint8_t> message)
{
    {
        const std::lock_guard<std::mutex> g (lock);
        outgoingBytes += message.size();
        outgoing.push_back ({ true, std::move (message) });
    }
    wake.notify_all();
}

void Connection::sendText (const std::string& text)
{
    {
        const std::lock_guard<std::mutex> g (lock);
        outgoingBytes += text.size();
        outgoing.push_back ({ false, std::vector<std::uint8_t> (text.begin(), text.end()) });
    }
    wake.notify_all();
}

std::size_t Connection::queuedBytes() const
{
    const std::lock_guard<std::mutex> g (lock);
    return outgoingBytes;
}

std::vector<Event> Connection::events()
{
    const std::lock_guard<std::mutex> g (lock);
    std::vector<Event> out;
    out.swap (incoming);
    return out;
}

void Connection::open (const std::string& url, const std::string& token, const std::string& userAgent)
{
    close();
    {
        // what the previous connection still had to say (its own "closed" above all) is not about this one: a session
        // started right after another one ended would otherwise end at once with "connection lost" (round 190ah)
        const std::lock_guard<std::mutex> g (lock);
        incoming.clear();
    }
    stopping = false;
    state = 1;
    reader = std::thread ([this, url, token, userAgent] { run (url, token, userAgent); });
}

#if defined(_WIN32)
// ---------------------------------------------------------------------------------------------------------------------
// WinHTTP (Windows 8 and later): the system's TLS, proxy settings and certificate store

struct Connection::Impl
{
    HINTERNET session = nullptr, connection = nullptr, socket = nullptr;
    std::mutex handles;
    std::atomic<bool> readerDone { false };
};

namespace
{
std::wstring widen (const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w ((std::size_t) n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
    return w;
}
} // namespace

Connection::Connection() : impl (std::make_unique<Impl>()) {}

bool Connection::available (std::string*) { return true; }

void Connection::run (std::string url, std::string token, std::string userAgent)
{
    auto fail = [this] (int code, const std::string& why) {
        state = 3;
        Event e;
        e.type = Event::Type::closed;
        e.code = code;
        e.text = why;
        push (std::move (e));
        impl->readerDone = true;
    };
    bool secure = url.rfind ("wss://", 0) == 0;
    if (url.rfind ("wss://", 0) == 0)
        url = "https://" + url.substr (6);
    else if (url.rfind ("ws://", 0) == 0)
        url = "http://" + url.substr (5);
    auto wurl = widen (url);
    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof parts;
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (! WinHttpCrackUrl (wurl.c_str(), 0, 0, &parts))
        return fail (0, "bad relay address");

    auto ua = widen (userAgent);
    HINTERNET session = WinHttpOpen (ua.c_str(), 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (8.1+) */, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        session = WinHttpOpen (ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return fail (0, "WinHTTP is not available");
    WinHttpSetTimeouts (session, 10000, 15000, 30000, 0); // (receive: no limit, the relay pings every 25 s)
    HINTERNET connection = WinHttpConnect (session, host, parts.nPort, 0);
    HINTERNET request = connection == nullptr ? nullptr
                                              : WinHttpOpenRequest (connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                                    secure ? WINHTTP_FLAG_SECURE : 0);
    auto cleanup = [&] {
        if (request != nullptr)
            WinHttpCloseHandle (request);
        if (connection != nullptr)
            WinHttpCloseHandle (connection);
        WinHttpCloseHandle (session);
    };
    if (request == nullptr || ! WinHttpSetOption (request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
    {
        cleanup();
        return fail (0, "could not open the connection");
    }
    const auto headers = widen ("Authorization: Bearer " + token + "\r\nSec-WebSocket-Protocol: lynx-relay.v1\r\n");
    if (! WinHttpSendRequest (request, headers.c_str(), (DWORD) -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || ! WinHttpReceiveResponse (request, nullptr))
    {
        const auto err = GetLastError();
        cleanup();
        return fail (0, "no connection to the relay (" + std::to_string (err) + ")");
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders (request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 101)
    {
        cleanup();
        return fail ((int) status, "the relay refused the connection");
    }
    HINTERNET socket = WinHttpWebSocketCompleteUpgrade (request, 0);
    WinHttpCloseHandle (request);
    request = nullptr;
    if (socket == nullptr)
    {
        cleanup();
        return fail (0, "the WebSocket upgrade failed");
    }
    {
        const std::lock_guard<std::mutex> g (impl->handles);
        impl->session = session;
        impl->connection = connection;
        impl->socket = socket;
    }
    state = 2;
    push ({ Event::Type::opened, {}, {}, 0 });

    writer = std::thread ([this, socket] {
        std::pair<bool, std::vector<std::uint8_t>> m;
        while (! stopping.load())
            if (next (m, 200))
            {
                const auto type = m.first ? WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
                if (WinHttpWebSocketSend (socket, type, m.second.empty() ? nullptr : m.second.data(), (DWORD) m.second.size()) != NO_ERROR)
                    break;
            }
    });

    std::vector<std::uint8_t> message, buffer (65536);
    Event closed;
    closed.type = Event::Type::closed;
    for (;;)
    {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type {};
        const auto err = WinHttpWebSocketReceive (socket, buffer.data(), (DWORD) buffer.size(), &read, &type);
        if (err != NO_ERROR)
        {
            closed.text = stopping ? "closed" : "connection lost (" + std::to_string (err) + ")";
            break;
        }
        message.insert (message.end(), buffer.begin(), buffer.begin() + read);
        if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
        {
            push ({ Event::Type::binary, std::move (message), {}, 0 });
            message.clear();
        }
        else if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
        {
            push ({ Event::Type::text, {}, std::string (message.begin(), message.end()), 0 });
            message.clear();
        }
        else if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        {
            USHORT code = 0;
            char reason[128] = {};
            DWORD length = 0;
            WinHttpWebSocketQueryCloseStatus (socket, &code, reason, sizeof reason, &length);
            closed.code = code;
            closed.text.assign (reason, length);
            break;
        }
        // (fragments: keep reading)
    }
    state = 3;
    stopping = true;
    wake.notify_all();
    if (writer.joinable())
        writer.join();
    push (std::move (closed));
    impl->readerDone = true;
}

void Connection::close()
{
    if (! reader.joinable())
        return;
    stopping = true;
    wake.notify_all();
    {
        const std::lock_guard<std::mutex> g (impl->handles);
        if (impl->socket != nullptr)
            WinHttpWebSocketShutdown (impl->socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    }
    // the relay answers the close at once; if it does not, closing the handle ends the pending receive
    for (int i = 0; i < 40 && ! impl->readerDone.load(); ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    {
        const std::lock_guard<std::mutex> g (impl->handles);
        if (impl->socket != nullptr)
            WinHttpCloseHandle (impl->socket);
        impl->socket = nullptr;
    }
    reader.join();
    const std::lock_guard<std::mutex> g (impl->handles);
    if (impl->connection != nullptr)
        WinHttpCloseHandle (impl->connection);
    if (impl->session != nullptr)
        WinHttpCloseHandle (impl->session);
    impl->connection = impl->session = nullptr;
    impl->readerDone = false;
    state = 0;
}

#else
// ---------------------------------------------------------------------------------------------------------------------
// libcurl's WebSocket API (7.86+, on by default since 8.11), loaded at run time: the distribution's TLS and CA store,
// and LYNX SHARE still starts on a system without it (the Session menu then says why).

namespace
{
struct CurlWsFrame
{
    int age, flags;
    long long offset, bytesleft;
    std::size_t len;
};
constexpr int wsText = 1 << 0, wsBinary = 1 << 1, wsCont = 1 << 2, wsClose = 1 << 3;
constexpr int optUrl = 10002, optUserAgent = 10018, optHttpHeader = 10023, optConnectTimeout = 78, optNoSignal = 99, optConnectOnly = 141;
constexpr int optWriteData = 10001, optWriteFunction = 20011, optPostFields = 10015, optPostFieldSize = 60, optCustomRequest = 10036, optTimeout = 13;
constexpr int infoResponseCode = 0x200000 + 2, infoActiveSocket = 0x500000 + 44;
constexpr int curlOk = 0, curlAgain = 81;

struct Curl
{
    void* lib = nullptr;
    void* (*easyInit)() = nullptr;
    int (*setopt) (void*, int, ...) = nullptr;
    int (*perform) (void*) = nullptr;
    void (*cleanup) (void*) = nullptr;
    int (*getinfo) (void*, int, ...) = nullptr;
    void* (*slistAppend) (void*, const char*) = nullptr;
    void (*slistFree) (void*) = nullptr;
    int (*wsRecv) (void*, void*, std::size_t, std::size_t*, const CurlWsFrame**) = nullptr;
    int (*wsSend) (void*, const void*, std::size_t, std::size_t*, long long, unsigned int) = nullptr;
    const char* (*strerror) (int) = nullptr;
    std::string why;

    Curl()
    {
        for (const char* name : { "libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4", "libcurl.4.dylib", "/usr/lib/libcurl.4.dylib" })
            if ((lib = dlopen (name, RTLD_NOW | RTLD_LOCAL)) != nullptr)
                break;
        if (lib == nullptr)
        {
            why = "libcurl is not installed";
            return;
        }
        auto sym = [this] (auto& f, const char* name) { f = reinterpret_cast<std::remove_reference_t<decltype (f)>> (dlsym (lib, name)); };
        sym (easyInit, "curl_easy_init");
        sym (setopt, "curl_easy_setopt");
        sym (perform, "curl_easy_perform");
        sym (cleanup, "curl_easy_cleanup");
        sym (getinfo, "curl_easy_getinfo");
        sym (slistAppend, "curl_slist_append");
        sym (slistFree, "curl_slist_free_all");
        sym (wsRecv, "curl_ws_recv");
        sym (wsSend, "curl_ws_send");
        sym (strerror, "curl_easy_strerror");
        if (wsRecv == nullptr || wsSend == nullptr)
            why = "this system's libcurl is too old for WebSockets (7.86 or later is needed)";
    }
    bool ok() const { return lib != nullptr && easyInit != nullptr && wsRecv != nullptr && wsSend != nullptr && setopt != nullptr; }
};

Curl& curl()
{
    static Curl c;
    return c;
}
} // namespace

struct Connection::Impl
{
};

Connection::Connection() : impl (std::make_unique<Impl>()) {}

bool Connection::available (std::string* why)
{
    if (why != nullptr)
        *why = curl().why;
    return curl().ok();
}

void Connection::run (std::string url, std::string token, std::string userAgent)
{
    auto& c = curl();
    auto closedEvent = [this] (int code, const std::string& why) {
        state = 3;
        Event e;
        e.type = Event::Type::closed;
        e.code = code;
        e.text = why;
        push (std::move (e));
    };
    if (! c.ok())
        return closedEvent (0, c.why);
    void* h = c.easyInit();
    if (h == nullptr)
        return closedEvent (0, "curl_easy_init failed");
    void* headers = nullptr;
    headers = c.slistAppend (headers, ("Authorization: Bearer " + token).c_str());
    headers = c.slistAppend (headers, "Sec-WebSocket-Protocol: lynx-relay.v1");
    c.setopt (h, optUrl, url.c_str());
    c.setopt (h, optUserAgent, userAgent.c_str());
    c.setopt (h, optHttpHeader, headers);
    c.setopt (h, optConnectTimeout, 15L);
    c.setopt (h, optNoSignal, 1L);
    c.setopt (h, optConnectOnly, 2L); // (2 = WebSocket: the upgrade, then curl_ws_recv / curl_ws_send)
    const auto rc = c.perform (h);
    if (rc != curlOk)
    {
        long status = 0;
        c.getinfo (h, infoResponseCode, &status);
        const std::string why = c.strerror != nullptr ? c.strerror (rc) : "error " + std::to_string (rc);
        c.cleanup (h);
        c.slistFree (headers);
        return closedEvent (status > 0 && status != 101 ? (int) status : 0, why);
    }
    int fd = -1;
    c.getinfo (h, infoActiveSocket, &fd);
    state = 2;
    push ({ Event::Type::opened, {}, {}, 0 });

    std::vector<std::uint8_t> message, buffer (65536);
    int messageFlags = 0;
    Event closed;
    closed.type = Event::Type::closed;
    bool done = false;
    while (! done && ! stopping.load())
    {
        // writes first (everything queued), then whatever arrived
        std::pair<bool, std::vector<std::uint8_t>> m;
        while (! done && next (m, 0))
        {
            std::size_t off = 0;
            const unsigned flags = m.first ? wsBinary : wsText;
            while (off < m.second.size() || (m.second.empty() && off == 0))
            {
                std::size_t sent = 0;
                const auto r = c.wsSend (h, m.second.data() + off, m.second.size() - off, &sent, 0, flags);
                off += sent;
                if (r == curlAgain)
                {
                    pollfd p { fd, POLLOUT, 0 };
                    ::poll (&p, 1, 50);
                    continue;
                }
                if (r != curlOk)
                {
                    closed.text = "send failed";
                    done = true;
                }
                break;
            }
        }
        pollfd p { fd, POLLIN, 0 };
        // (the poll is only the wait: curl is asked every time, because what arrived together with the upgrade's answer
        // or inside an already-read TLS record sits in curl's buffer and never makes the socket readable — the relay's
        // "welcome" was lost that way when the relay was near, round 190ah)
        ::poll (&p, 1, 10);
        for (;;)
        {
            std::size_t got = 0;
            const CurlWsFrame* meta = nullptr;
            const auto r = c.wsRecv (h, buffer.data(), buffer.size(), &got, &meta);
            if (r == curlAgain)
                break;
            if (r != curlOk || meta == nullptr)
            {
                closed.text = "connection lost";
                done = true;
                break;
            }
            if ((meta->flags & wsClose) != 0)
            {
                closed.code = got >= 2 ? (buffer[0] << 8 | buffer[1]) : 1000;
                if (got > 2)
                    closed.text.assign ((const char*) buffer.data() + 2, got - 2);
                done = true;
                break;
            }
            if ((meta->flags & (wsText | wsBinary)) == 0)
                continue; // (pings: curl answers them)
            messageFlags = meta->flags;
            message.insert (message.end(), buffer.begin(), buffer.begin() + (long) got);
            if (meta->bytesleft == 0 && (meta->flags & wsCont) == 0)
            {
                if ((messageFlags & wsBinary) != 0)
                    push ({ Event::Type::binary, std::move (message), {}, 0 });
                else
                    push ({ Event::Type::text, {}, std::string (message.begin(), message.end()), 0 });
                message.clear();
            }
        }
    }
    if (stopping.load())
    {
        std::size_t sent = 0;
        const std::uint8_t normal[2] = { 0x03, 0xE8 }; // 1000
        c.wsSend (h, normal, 2, &sent, 0, wsClose);
        closed.text = "closed";
    }
    c.cleanup (h);
    c.slistFree (headers);
    state = 3;
    push (std::move (closed));
}

void Connection::close()
{
    if (! reader.joinable())
        return;
    stopping = true;
    wake.notify_all();
    reader.join();
    state = 0;
}
#endif

Connection::~Connection()
{
    close();
}

#if defined(__linux__)
namespace
{
std::size_t collectBody (char* data, std::size_t size, std::size_t count, void* user)
{
    auto& out = *static_cast<std::string*> (user);
    if (out.size() + size * count > (1u << 20))
        return 0; // (the relay's answers are a few hundred bytes)
    out.append (data, size * count);
    return size * count;
}
} // namespace

bool httpAvailable()
{
    const auto& c = curl();
    return c.lib != nullptr && c.easyInit != nullptr && c.setopt != nullptr && c.perform != nullptr && c.getinfo != nullptr && c.slistAppend != nullptr;
}

HttpAnswer http (const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string& body,
                 const std::string& userAgent, int timeoutSeconds)
{
    HttpAnswer answer;
    if (! httpAvailable())
        return answer;
    auto& c = curl();
    void* h = c.easyInit();
    if (h == nullptr)
        return answer;
    void* list = nullptr;
    for (const auto& line : headers)
        list = c.slistAppend (list, line.c_str());
    c.setopt (h, optUrl, url.c_str());
    c.setopt (h, optUserAgent, userAgent.c_str());
    c.setopt (h, optCustomRequest, method.c_str());
    if (list != nullptr)
        c.setopt (h, optHttpHeader, list);
    if (! body.empty() || method == "POST")
    {
        c.setopt (h, optPostFields, body.c_str());
        c.setopt (h, optPostFieldSize, (long) body.size());
    }
    c.setopt (h, optConnectTimeout, (long) timeoutSeconds);
    c.setopt (h, optTimeout, (long) timeoutSeconds * 2);
    c.setopt (h, optNoSignal, 1L);
    c.setopt (h, optWriteFunction, &collectBody);
    c.setopt (h, optWriteData, &answer.body);
    if (c.perform (h) == curlOk)
    {
        long status = 0;
        c.getinfo (h, infoResponseCode, &status);
        answer.status = (int) status;
    }
    c.cleanup (h);
    if (list != nullptr)
        c.slistFree (list);
    return answer;
}
#else
bool httpAvailable() { return false; }
HttpAnswer http (const std::string&, const std::string&, const std::vector<std::string>&, const std::string&, const std::string&, int) { return {}; }
#endif
} // namespace lynx::livelink
