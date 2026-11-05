#include "engine/KumiControl.h"
#include "engine/AudioEngine.h"
#include "engine/KumiPluginControl.h"
#include "engine/KumiPlaylistControl.h"
#include "engine/KumiInstrumentControl.h"
#include "engine/KumiJsonLimits.h"
#include "engine/KumiAuthentication.h"
#include "core/SessionIds.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>
#include <juce_events/juce_events.h>

#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <cerrno>
 #include <fcntl.h>
 #include <sys/socket.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace lynx
{
namespace
{
constexpr std::size_t maxMessage = 1024 * 1024;
constexpr std::size_t maxHandshake = 1024;
constexpr const char* protocol = "lynx-kumi/2";
constexpr juce::int64 maxTick = 1000000000;
using Field = std::pair<const char*, juce::var>;

juce::var object (std::initializer_list<Field> fields)
{
    auto* result = new juce::DynamicObject();
    for (const auto& [name, value] : fields)
        result->setProperty (name, value);
    return result;
}

juce::String text (const std::string& value) { return juce::String::fromUTF8 (value.c_str()); }
juce::var success (juce::var result) { return object ({ { "ok", true }, { "result", std::move (result) } }); }
juce::var failure (const juce::String& error) { return object ({ { "ok", false }, { "error", error } }); }

[[noreturn]] void invalid (const juce::String& why) { throw std::runtime_error (why.toStdString()); }

double number (const juce::var& args, const char* field, double low, double high)
{
    const auto& value = args[field];
    if (! (value.isInt() || value.isInt64() || value.isDouble()))
        invalid (juce::String (field) + " must be a number");
    const auto n = (double) value;
    if (! std::isfinite (n) || n < low || n > high)
        invalid (juce::String (field) + " is outside its allowed range");
    return n;
}

juce::int64 integer (const juce::var& args, const char* field, juce::int64 low, juce::int64 high)
{
    const auto n = number (args, field, (double) low, (double) high);
    if (std::floor (n) != n)
        invalid (juce::String (field) + " must be an integer");
    return (juce::int64) n;
}

bool boolean (const juce::var& args, const char* field)
{
    if (! args[field].isBool())
        invalid (juce::String (field) + " must be true or false");
    return (bool) args[field];
}

int indexFor (const juce::var& args, std::size_t size)
{
    if (size == 0)
        invalid ("The requested collection is empty");
    return (int) integer (args, "index", 0, (juce::int64) size - 1);
}

juce::String randomToken()
{
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    unsigned char bytes[32];
    const auto fd = ::open ("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        invalid ("Cannot open the operating system random source");
    std::size_t done = 0;
    while (done < sizeof (bytes))
    {
        const auto count = ::read (fd, bytes + done, sizeof (bytes) - done);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            ::close (fd);
            invalid ("Cannot read the operating system random source");
        }
        done += (std::size_t) count;
    }
    ::close (fd);
    return juce::String::toHexString (bytes, sizeof (bytes), 0);
#else
    invalid ("Kumi control is currently available on macOS and Linux");
#endif
}


void writeDiscovery (const juce::File& file, const juce::String& json)
{
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    auto pattern = (file.getFullPathName() + ".XXXXXX").toStdString();
    const auto fd = ::mkstemp (pattern.data());
    if (fd < 0)
        invalid ("Cannot create Kumi discovery file in its parent folder");
    bool ok = ::fchmod (fd, S_IRUSR | S_IWUSR) == 0;
    const auto bytes = json.toStdString();
    std::size_t done = 0;
    while (ok && done < bytes.size())
    {
        const auto count = ::write (fd, bytes.data() + done, bytes.size() - done);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            ok = false;
        else
            done += (std::size_t) count;
    }
    if (ok)
        ok = ::fsync (fd) == 0;
    ::close (fd);
    if (ok)
        ok = ::rename (pattern.c_str(), file.getFullPathName().toRawUTF8()) == 0;
    if (! ok)
    {
        ::unlink (pattern.c_str());
        invalid ("Cannot publish owner-only Kumi discovery file");
    }
#else
    juce::ignoreUnused (file, json);
    invalid ("Owner-only Kumi discovery is unavailable on this platform");
#endif
}

juce::var noteValue (const Note& n)
{
    return object ({ { "start", (juce::int64) n.start }, { "length", (juce::int64) n.length },
                     { "channel", n.channel }, { "key", n.key }, { "velocity", n.velocity },
                     { "pan", n.pan }, { "muted", n.muted }, { "finePitch", n.finePitch },
                     { "slide", n.slide }, { "porta", n.porta }, { "release", n.release },
                     { "modX", n.modX }, { "modY", n.modY }, { "colour", n.colour }, { "group", n.group } });
}

Note parseNote (const juce::var& value, std::size_t channels)
{
    if (value.getDynamicObject() == nullptr)
        invalid ("Every note must be an object");
    for (const auto* field : { "start", "length", "channel", "key", "velocity", "pan", "muted", "finePitch",
                              "slide", "porta", "release", "modX", "modY", "colour", "group" })
        if (! value.hasProperty (field))
            invalid (juce::String ("Every replacement note must include ") + field + "; read the complete notes first");
    if (channels == 0)
        invalid ("Cannot add a note without a channel");
    Note n;
    n.start = integer (value, "start", 0, maxTick);
    n.length = integer (value, "length", 1, maxTick);
    if (n.start > maxTick - n.length)
        invalid ("Note end is outside its allowed range");
    n.channel = (int) integer (value, "channel", 0, (juce::int64) channels - 1);
    n.key = (int) integer (value, "key", 0, 127);
    n.velocity = (float) number (value, "velocity", 0, 1);
    if (value.hasProperty ("pan")) n.pan = (float) number (value, "pan", -1, 1);
    if (value.hasProperty ("muted")) n.muted = boolean (value, "muted");
    if (value.hasProperty ("finePitch")) n.finePitch = (float) number (value, "finePitch", -120, 120);
    if (value.hasProperty ("slide")) n.slide = boolean (value, "slide");
    if (value.hasProperty ("porta")) n.porta = boolean (value, "porta");
    if (value.hasProperty ("release")) n.release = (float) number (value, "release", 0, 1);
    if (value.hasProperty ("modX")) n.modX = (float) number (value, "modX", 0, 1);
    if (value.hasProperty ("modY")) n.modY = (float) number (value, "modY", 0, 1);
    if (value.hasProperty ("colour")) n.colour = (int) integer (value, "colour", 0, 15);
    if (value.hasProperty ("group")) n.group = (int) integer (value, "group", 0, 2147483647);
    return n;
}
} // namespace

struct KumiControl::Impl final : juce::Thread
{
    struct State
    {
        State (AudioEngine& e, std::function<juce::String()> name, std::function<juce::String()> path)
            : engine (e), projectName (std::move (name)), projectPath (std::move (path)), controlEpoch (randomToken()) {}
        AudioEngine& engine;
        std::function<juce::String()> projectName, projectPath;
        // Public boot identity, generated independently of the private authentication token.
        const juce::String controlEpoch;
        std::atomic<bool> active { true };

        juce::var inspect()
        {
            const auto& s = engine.getSong();
            const auto& t = engine.getTransport();
            juce::Array<juce::var> channels, mixer, patterns, playlist;
            for (std::size_t i = 0; i < s.channels.size(); ++i)
            {
                const auto& c = s.channels[i];
                const auto instrument = c.instrument == InstrumentKind::plugin ? text (c.plugin.name)
                    : c.instrument == InstrumentKind::sampler ? juce::String ("sampler")
                    : c.instrument == InstrumentKind::audioClip ? juce::String ("audioClip")
                    : juce::String ("native:") + juce::String ((int) c.instrument);
                channels.add (object ({ { "index", (int) i }, { "channelId", text (uids::toHex (c.uid)) }, { "name", text (c.name) }, { "instrument", instrument },
                                       { "volume", c.volume }, { "pan", c.pan }, { "muted", c.muted }, { "mixer", c.mixerTrack } }));
            }
            for (std::size_t i = 0; i < s.mixer.size(); ++i)
            {
                const auto& m = s.mixer[i];
                mixer.add (object ({ { "index", (int) i }, { "name", text (m.name) }, { "volume", m.volume },
                                    { "pan", m.pan }, { "muted", m.muted }, { "solo", m.solo } }));
            }
            for (std::size_t i = 0; i < s.patterns.size(); ++i)
                patterns.add (object ({ { "index", (int) i }, { "patternId", text (uids::toHex (s.patterns[i].uid)) }, { "name", text (s.patterns[i].name) },
                                       { "noteCount", (int) s.patterns[i].notes.size() } }));
            for (std::size_t i = 0; i < s.playlist.size(); ++i)
            {
                const auto& c = s.playlist[i];
                playlist.add (object ({ { "index", (int) i }, { "kind", (int) c.kind }, { "pattern", c.pattern },
                                       { "track", c.track }, { "channel", c.channel }, { "automation", c.automation },
                                       { "start", (juce::int64) c.start }, { "length", (juce::int64) c.length },
                                       { "offset", (juce::int64) c.offset }, { "muted", c.muted } }));
            }
            return object ({ { "project", object ({ { "name", projectName() }, { "id", text (s.projectId) },
                                                              { "path", projectPath ? projectPath() : juce::String() }, { "modified", engine.isModified() } }) },
                            { "controlEpoch", controlEpoch }, { "appVersion", JUCE_APPLICATION_VERSION_STRING },
                            { "bpm", s.bpm }, { "beatsPerBar", s.beatsPerBar }, { "playing", t.playing.load() }, { "recording", engine.isRecording() },
                            { "songMode", s.mode == PlayMode::song }, { "positionTicks", t.positionTicks.load() },
                            { "ppq", ppq }, { "channels", channels }, { "mixer", mixer }, { "patterns", patterns },
                            { "currentPattern", s.currentPattern }, { "playlist", playlist },
                            { "capabilities", object ({ { "pluginParameters", true }, { "effects", true }, { "playlist", true }, { "instruments", true } }) },
                            { "canUndo", engine.canUndo() }, { "canRedo", engine.canRedo() },
                            { "revision", (juce::int64) engine.getSongStamp() } });
        }

        juce::var dispatch (const juce::String& op, const juce::var& args, const std::function<bool()>& cancelled)
        {
            jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
            const auto& s = engine.getSong();
            if (op == "session.inspect")
                return inspect();
            if (op == "pattern.notes")
            {
                const auto index = indexFor (args, s.patterns.size());
                const auto& source = s.patterns[(std::size_t) index].notes;
                const auto total = (juce::int64) source.size();
                const auto offset = args.hasProperty ("offset") ? integer (args, "offset", 0, 1000000000) : 0;
                const auto paged = args.hasProperty ("offset") || args.hasProperty ("limit");
                const auto limit = args.hasProperty ("limit") ? integer (args, "limit", 1, 256) : (paged ? 256 : total);
                juce::Array<juce::var> notes;
                for (auto i = offset; i < total && i - offset < limit; ++i)
                    notes.add (noteValue (source[(std::size_t) i]));
                auto result = object ({ { "index", index }, { "notes", notes }, { "offset", offset }, { "totalNotes", total },
                                        { "revision", (juce::int64) engine.getSongStamp() } });
                if (offset < total && total - offset > limit) result.getDynamicObject()->setProperty ("nextOffset", offset + limit);
                return result;
            }
            if (op == "plugin.inspect" || op == "effect.catalog")
                return dispatchKumiPlugin (engine, op, args, cancelled);
            if (op == "instrument.catalog")
                return dispatchKumiInstrument (engine, op, args, cancelled);
            if (op == "playlist.inspect")
                return dispatchKumiPlaylist (engine, op, args);

            // An authenticated emergency stop must work even when the project or
            // revision has changed since Kumi's last observation.
            if (op == "transport.stop")
            {
                engine.stop();
                return object ({ { "playing", false } });
            }

            if (op != "transport.play" && op != "transport.stop" && op != "tempo.set" && op != "mixer.set"
                && op != "channel.set" && op != "pattern.replace_notes" && op != "history.undo" && op != "history.redo"
                && ! isKumiPluginOperation (op) && ! isKumiPlaylistOperation (op) && ! isKumiInstrumentOperation (op))
                invalid ("Unsupported operation: " + op);

            // Every mutation is bound to the project and revision Kumi actually observed. This also protects shared
            // history: an intervening producer edit must never become a delayed agent undo's target.
            if (! args["expectedProjectId"].isString() || s.projectId.empty()
                || args["expectedProjectId"].toString() != text (s.projectId))
                invalid ("Project changed or expectedProjectId is missing; inspect the session again");
            if (! args["expectedEpoch"].isString() || args["expectedEpoch"].toString() != controlEpoch)
                invalid ("LYNX control restarted or expectedEpoch is missing; inspect the session again");
            const auto& revision = args["expectedRevision"];
            if (! (revision.isInt() || revision.isInt64()) || (juce::int64) revision < 0
                || (std::uint64_t) (juce::int64) revision != engine.getSongStamp())
                invalid ("Project changed since it was read or expectedRevision is missing; inspect the session again");
            if (engine.isRecording() && op != "transport.stop")
                invalid ("Stop recording before changing the project through Kumi");
            if (isKumiPluginOperation (op))
                return dispatchKumiPlugin (engine, op, args, cancelled);
            if (isKumiInstrumentOperation (op))
                return dispatchKumiInstrument (engine, op, args, cancelled);
            if (isKumiPlaylistOperation (op))
                return dispatchKumiPlaylist (engine, op, args);

            if (op == "transport.play")
            {
                engine.startPlayback();
                return object ({ { "playing", engine.getTransport().playing.load() } });
            }
            if (op == "tempo.set")
            {
                const auto bpm = number (args, "bpm", minTempo, maxTempo);
                if (bpm != s.bpm)
                    engine.editSong ([bpm] (Song& song) { song.bpm = bpm; });
                return object ({ { "bpm", engine.getSong().bpm } });
            }
            if (op == "mixer.set" || op == "channel.set")
            {
                const auto isMixer = op == "mixer.set";
                const auto index = indexFor (args, isMixer ? s.mixer.size() : s.channels.size());
                const auto volume = args.hasProperty ("volume");
                const auto pan = args.hasProperty ("pan");
                const auto muted = args.hasProperty ("muted");
                const auto solo = args.hasProperty ("solo");
                if (! volume && ! pan && ! muted && ! solo)
                    invalid ("Supply at least one control to change");
                if (solo && ! isMixer)
                    invalid ("channel.set does not support solo");
                const auto v = volume ? (float) number (args, "volume", 0, 1) : 0.0f;
                const auto p = pan ? (float) number (args, "pan", -1, 1) : 0.0f;
                const auto m = muted ? boolean (args, "muted") : false;
                const auto so = solo ? boolean (args, "solo") : false;
                const auto change = [=] (Song& song) {
                    if (isMixer)
                    {
                        auto& track = song.mixer[(std::size_t) index];
                        if (volume) track.volume = v;
                        if (pan) track.pan = p;
                        if (muted) track.muted = m;
                        if (solo) track.solo = so;
                    }
                    else
                    {
                        auto& channel = song.channels[(std::size_t) index];
                        if (volume) channel.volume = v;
                        if (pan) channel.pan = p;
                        if (muted) channel.muted = m;
                    }
                };
                if (muted || solo)
                    engine.editSong (change);
                else
                {
                    engine.editLevels (change);
                    engine.flushLevels();
                }
                return object ({ { "index", index }, { "changed", true } });
            }
            if (op == "pattern.replace_notes")
            {
                if (engine.getTransport().playing.load())
                    invalid ("Stop playback before replacing pattern notes");
                const auto index = indexFor (args, s.patterns.size());
                const auto* input = args["notes"].getArray();
                if (input == nullptr || input->size() > 10000)
                    invalid ("notes must be an array of at most 10000 notes");
                std::vector<Note> notes;
                notes.reserve ((std::size_t) input->size());
                for (const auto& value : *input)
                    notes.push_back (parseNote (value, s.channels.size()));
                const auto count = (int) notes.size();
                engine.editSong ([index, notes = std::move (notes)] (Song& song) {
                    auto& pattern = song.patterns[(std::size_t) index];
                    pattern.notes = notes;
                    pattern.sortNotes();
                });
                return object ({ { "index", index }, { "noteCount", count } });
            }
            if (op == "history.undo" || op == "history.redo")
            {
                if (engine.getTransport().playing.load())
                    invalid ("Stop playback before changing history");
                const auto undo = op == "history.undo";
                const auto available = undo ? engine.canUndo() : engine.canRedo();
                if (available)
                {
                    if (undo) engine.undo(); else engine.redo();
                }
                return object ({ { "changed", available }, { "canUndo", engine.canUndo() }, { "canRedo", engine.canRedo() } });
            }
            invalid ("Unsupported operation: " + op);
        }
    };

    struct Work
    {
        juce::String op;
        juce::var args, response;
        juce::WaitableEvent finished;
        std::atomic<bool> cancelled { false };
    };

    Impl (AudioEngine& engine, std::function<juce::String()> name, std::function<juce::String()> path, juce::File file)
        : juce::Thread ("Kumi control"), state (std::make_shared<State> (engine, std::move (name), std::move (path))),
          discovery (std::move (file)), token (randomToken())
    {
        if (! listener.createListener (0, "127.0.0.1"))
            invalid ("Cannot create the Kumi loopback listener");
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        const auto listenerFd = listener.getRawSocketHandle();
        const auto listenerFlags = ::fcntl (listenerFd, F_GETFL, 0);
        if (listenerFlags < 0 || ::fcntl (listenerFd, F_SETFL, listenerFlags | O_NONBLOCK) != 0)
            invalid ("Cannot make the Kumi listener nonblocking");
#endif
        juce::int64 pid = 0;
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        pid = (juce::int64) ::getpid();
#endif
        writeDiscovery (discovery, juce::JSON::toString (object ({
            { "protocol", protocol }, { "host", "127.0.0.1" }, { "port", listener.getBoundPort() },
            { "token", token }, { "pid", pid }, { "appVersion", JUCE_APPLICATION_VERSION_STRING } }), true));
        startThread();
    }

    ~Impl() override
    {
        state->active = false;
        signalThreadShouldExit();
        stopThread (3500);
        listener.close();
        // A second custom LYNX may have replaced this file; never remove its record.
        const auto record = juce::JSON::parse (discovery.loadFileAsString());
        if (record["token"].toString() == token)
            discovery.deleteFile();
    }

    juce::var request (const juce::String& json)
    {
        const auto utf8 = json.toRawUTF8();
        if (! kumi_json::withinNestingLimit (std::string_view (utf8, json.getNumBytesAsUTF8())))
            return failure ("JSON nesting exceeds the control request limit");
        juce::var input;
        if (juce::JSON::parse (json, input).failed() || input.getDynamicObject() == nullptr)
            return failure ("Invalid JSON request");
        if (input.getDynamicObject()->getProperties().size() != 2 || input.hasProperty ("token"))
            return failure ("Authenticated requests require only op and args");
        if (! input["op"].isString() || input["op"].toString().length() > 100
            || input["args"].getDynamicObject() == nullptr)
            return failure ("Request needs an operation string and an args object");
        auto work = std::make_shared<Work>();
        work->op = input["op"].toString();
        work->args = input["args"];
        const std::weak_ptr<State> weak = state;
        if (! juce::MessageManager::callAsync ([weak, work] {
            const auto shared = weak.lock();
            if (shared == nullptr || ! shared->active || work->cancelled)
                return;
            try
            {
                auto result = shared->dispatch (work->op, work->args, [weak, work] {
                    const auto owner = weak.lock();
                    return owner == nullptr || ! owner->active || work->cancelled.load();
                });
                if (auto* properties = result.getDynamicObject())
                {
                    properties->setProperty ("revision", (juce::int64) shared->engine.getSongStamp());
                    properties->setProperty ("projectId", text (shared->engine.getSong().projectId));
                    properties->setProperty ("controlEpoch", shared->controlEpoch);
                }
                work->response = success (std::move (result));
            }
            catch (const std::exception& e) { work->response = failure (e.what()); }
            catch (...) { work->response = failure ("Control operation failed"); }
            work->finished.signal();
        }))
            return failure ("The LYNX message loop is unavailable");
        const auto longOperation = work->op == "plugin.parameters_set" || work->op == "effect.load"
                                || work->op == "history.undo" || work->op == "history.redo"
                                || work->op == "channel.create" || work->op == "instrument.replace";
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + (longOperation ? 60000.0 : 5000.0);
        while (state->active && ! threadShouldExit() && juce::Time::getMillisecondCounterHiRes() < deadline)
            if (work->finished.wait (50))
                return work->response;
        work->cancelled = true;
        return failure ("LYNX did not answer in time; the change may have run. Inspect the session before retrying");
    }

    bool readLine (juce::StreamingSocket& socket, std::string& pending, std::string& line,
                   std::size_t limit, double deadline)
    {
        while (state->active && ! threadShouldExit() && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            if (const auto newline = pending.find ('\n'); newline != std::string::npos)
            {
                if (newline > limit) return false;
                line = pending.substr (0, newline);
                pending.erase (0, newline + 1);
                return true;
            }
            if (pending.size() > limit) return false;
            if (socket.waitUntilReady (true, 50) <= 0) continue;
            char chunk[4096];
            const auto amount = (int) std::min<std::size_t> (sizeof (chunk), limit + 1 - pending.size());
            const auto count = socket.read (chunk, amount, false);
            if (count <= 0) return false;
            pending.append (chunk, (std::size_t) count);
        }
        return false;
    }

    bool writeFrame (juce::StreamingSocket& socket, const juce::var& value, std::size_t limit, double deadline)
    {
        auto output = juce::JSON::toString (value, true).toStdString();
        if (output.size() > limit) return false;
        output += '\n';
        std::size_t sent = 0;
        while (sent < output.size() && state->active && ! threadShouldExit()
               && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            if (socket.waitUntilReady (false, 50) <= 0) continue;
            const auto amount = (int) std::min<std::size_t> (16384, output.size() - sent);
#if JUCE_LINUX
            const auto count = ::send (socket.getRawSocketHandle(), output.data() + sent, (std::size_t) amount, MSG_NOSIGNAL);
#else
            const auto count = socket.write (output.data() + sent, amount);
#endif
            if (count <= 0) return false;
            sent += (std::size_t) count;
        }
        return sent == output.size();
    }

    bool authenticate (juce::StreamingSocket& socket, std::string& pending)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        std::string line;
        if (! readLine (socket, pending, line, maxHandshake, deadline)
            || ! kumi_json::withinNestingLimit (line, 8)) return false;
        juce::var hello;
        if (juce::JSON::parse (juce::String::fromUTF8 (line.data(), (int) line.size()), hello).failed()
            || hello.getDynamicObject() == nullptr || hello.getDynamicObject()->getProperties().size() != 3
            || hello["type"].toString() != "hello" || hello["protocol"].toString() != protocol
            || ! hello["nonce"].isString() || ! kumi_auth::isLowerHex (hello["nonce"].toString().toStdString(), 32))
            return false;
        const auto clientNonce = hello["nonce"].toString().toStdString();
        const auto serverNonce = randomToken().substring (0, 32).toStdString();
        const auto key = token.toStdString();
        const auto challenge = object ({ { "type", "challenge" }, { "protocol", protocol },
                                        { "nonce", text (serverNonce) }, { "proof", text (kumi_auth::proof (key, "server", clientNonce, serverNonce)) } });
        if (! writeFrame (socket, challenge, maxHandshake, deadline)
            || ! readLine (socket, pending, line, maxHandshake, deadline)
            || ! kumi_json::withinNestingLimit (line, 8)) return false;
        juce::var auth;
        if (juce::JSON::parse (juce::String::fromUTF8 (line.data(), (int) line.size()), auth).failed()
            || auth.getDynamicObject() == nullptr || auth.getDynamicObject()->getProperties().size() != 2
            || auth["type"].toString() != "auth" || ! auth["proof"].isString()) return false;
        const auto supplied = auth["proof"].toString().toStdString();
        if (! kumi_auth::isLowerHex (supplied, 64)
            || ! kumi_auth::constantTimeEqual (supplied, kumi_auth::proof (key, "client", clientNonce, serverNonce))) return false;
        return writeFrame (socket, object ({ { "type", "ready" }, { "protocol", protocol } }), maxHandshake, deadline);
    }

    void connection (juce::StreamingSocket& socket)
    {
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        const auto fd = socket.getRawSocketHandle();
        const auto flags = ::fcntl (fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl (fd, F_SETFL, flags | O_NONBLOCK) != 0) return;
#if JUCE_MAC || JUCE_BSD
        const int noSigPipe = 1;
        if (::setsockopt (fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof (noSigPipe)) != 0) return;
#endif
#endif
        std::string pending;
        if (! authenticate (socket, pending))
        {
            writeFrame (socket, failure ("Authentication failed; lynx-kumi/2 mutual authentication is required"),
                        maxHandshake, juce::Time::getMillisecondCounterHiRes() + 500.0);
            return;
        }
        std::string line;
        auto response = readLine (socket, pending, line, maxMessage, juce::Time::getMillisecondCounterHiRes() + 2000.0)
            ? request (juce::String::fromUTF8 (line.data(), (int) line.size()))
            : failure ("Authenticated request exceeded its byte or time limit");
        if (juce::JSON::toString (response, true).getNumBytesAsUTF8() > maxMessage)
            response = failure ("Response exceeds 1 MiB; request a smaller collection");
        writeFrame (socket, response, maxMessage, juce::Time::getMillisecondCounterHiRes() + 2000.0);
    }

    void run() override
    {
        while (state->active && ! threadShouldExit())
        {
            if (listener.waitUntilReady (true, 100) <= 0)
                continue;
            std::unique_ptr<juce::StreamingSocket> socket (listener.waitForNextConnection());
            if (socket != nullptr)
                try { connection (*socket); }
                catch (...) { /* A malformed connection must not stop the DAW. */ }
        }
    }

    std::shared_ptr<State> state;
    juce::StreamingSocket listener;
    juce::File discovery;
    juce::String token;
};

KumiControl::KumiControl (std::unique_ptr<Impl> value) : impl (std::move (value)) {}
KumiControl::~KumiControl() = default;

std::unique_ptr<KumiControl> KumiControl::create (AudioEngine& engine, std::function<juce::String()> name,
                                                  std::function<juce::String()> projectPath)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("LYNX_KUMI_DISCOVERY", {});
    if (path.isEmpty())
        return {};
    try
    {
        if (! juce::File::isAbsolutePath (path))
            invalid ("LYNX_KUMI_DISCOVERY must be an absolute path");
        auto control = std::unique_ptr<KumiControl> (new KumiControl (std::make_unique<Impl> (engine, std::move (name), std::move (projectPath), juce::File (path))));
        engine.enableExternalPluginStateRestore();
        return control;
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("Kumi control did not start: " + juce::String (e.what()));
        return {};
    }
}
} // namespace lynx
