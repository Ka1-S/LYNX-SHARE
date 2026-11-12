// Round 190as: "Find similar samples" (core/SampleSimilarity.h). Synthetic sounds only: kicks (a falling sine), hats
// (short bright noise), pads (long chords) and loops (a kick/hat pattern); the analysis must put like with like, not
// care about level, leading silence or sample rate, and the worker must index in the background, answer a query at
// once (analysing the clicked file first when the pass has not reached it), wait while LYNX plays and never be killed.

#include "core/SampleSimilarity.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace s190as
{
using lynx::similarity::Features;

struct Rng
{
    std::uint32_t s;
    float next() // −1..1
    {
        s = s * 1664525u + 1013904223u;
        return (float) ((s >> 8) & 0xffff) / 32768.0f - 1.0f;
    }
};

// kind: 'k' kick, 'h' hat, 'p' pad, 'l' loop; `v` varies it a little
std::vector<float> sound (char kind, int v, double rate = 44100.0, float gain = 0.8f, double leadSilence = 0.0)
{
    Rng rng { (std::uint32_t) (1234 + v * 77 + kind) };
    const auto lead = (std::size_t) (leadSilence * rate);
    std::vector<float> out (lead, 0.0f);
    const auto add = [&] (double at, char what, double length) {
        const auto first = lead + (std::size_t) (at * rate), count = (std::size_t) (length * rate);
        if (out.size() < first + count)
            out.resize (first + count, 0.0f);
        double phase = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto t = (double) i / rate;
            float x = 0.0f;
            if (what == 'k')
            {
                phase += 2.0 * 3.14159265358979 * ((48.0 + v) + 140.0 * std::exp (-t * 35.0)) / rate;
                x = (float) (std::sin (phase) * std::exp (-t * (9.0 + 0.3 * v)));
            }
            else if (what == 'h')
            {
                static thread_local float last = 0.0f;
                const auto n = rng.next();
                x = (n - last) * 0.5f * (float) std::exp (-t * (60.0 + 4.0 * v)); // (differenced noise: bright)
                last = n;
            }
            else // pad: a slow chord
            {
                const auto base = 220.0 * std::pow (2.0, v / 12.0);
                const auto env = std::min (1.0, t / 0.4) * std::min (1.0, (length - t) / 0.5);
                x = (float) (env * 0.3 * (std::sin (2.0 * 3.14159265358979 * base * t) + std::sin (2.0 * 3.14159265358979 * base * 1.25 * t)
                                          + std::sin (2.0 * 3.14159265358979 * base * 1.5 * t)));
            }
            out[first + i] += gain * x;
        }
    };
    if (kind == 'k')
        add (0.0, 'k', 0.35 + 0.01 * v);
    else if (kind == 'h')
        add (0.0, 'h', 0.08 + 0.004 * v);
    else if (kind == 'p')
        add (0.0, 'p', 3.0 + 0.1 * v);
    else // a 2-bar loop at 120..130 BPM: kick on the beats, hats on the off-beats
    {
        const auto beat = 60.0 / (120.0 + v);
        for (int b = 0; b < 8; ++b)
        {
            add (b * beat, 'k', 0.25);
            add ((b + 0.5) * beat, 'h', 0.06);
        }
    }
    return out;
}

Features featuresOf (char kind, int v, double rate = 44100.0, float gain = 0.8f, double lead = 0.0)
{
    const auto audio = sound (kind, v, rate, gain, lead);
    Features f {};
    CHECK (lynx::similarity::analyse (audio.data(), audio.size(), rate, f));
    return f;
}

// "/lib/<folder>/<kind><v>.wav": the name says what the file sounds like
char kindOf (const std::string& path)
{
    const auto slash = path.find_last_of ('/');
    return path[slash + 1];
}
int variantOf (const std::string& path)
{
    const auto slash = path.find_last_of ('/');
    return std::atoi (path.c_str() + slash + 2);
}

struct Disk
{
    std::mutex m;
    std::map<std::string, std::vector<std::string>> folders { { "/lib", {} } }; // folder -> files
    std::vector<std::string> subs;                                             // of /lib
    std::atomic<int> decodes { 0 };
    std::atomic<bool> playing { false };
    std::string saved;
    int saves = 0;
    int decodeMs = 0;
    // Decoding this file waits until release(): the test knows the pass is in the middle of it (round 190dc: no
    // sleeps that guess how far a lowest-priority thread got on a busy machine)
    std::string hold;
    std::mutex gateLock;
    std::condition_variable gate;
    bool holding = false, released = false;

    bool waitHeld (int timeoutMs)
    {
        std::unique_lock<std::mutex> g (gateLock);
        return gate.wait_for (g, std::chrono::milliseconds (timeoutMs), [this] { return holding; });
    }
    void release()
    {
        {
            const std::lock_guard<std::mutex> g (gateLock);
            released = true;
        }
        gate.notify_all();
    }

    Disk()
    {
        for (const char* folder : { "kicks", "hats", "pads", "loops" })
        {
            subs.push_back (folder);
            auto& files = folders[std::string ("/lib/") + folder];
            for (int v = 0; v < 6; ++v)
                files.push_back (std::string (1, folder[0]) + std::to_string (v) + ".wav");
            files.push_back ("notes.txt");    // not audio: never decoded
            files.push_back ("x-broken.wav"); // can't be decoded: remembered as unusable
        }
    }

    lynx::similarity::Service::Callbacks callbacks()
    {
        lynx::similarity::Service::Callbacks cb;
        cb.fs.list = [this] (const std::string& folder) {
            lynx::search::FolderListing l;
            const std::lock_guard<std::mutex> g (m);
            const auto it = folders.find (folder);
            if (it == folders.end())
                return l;
            l.exists = true;
            l.files = it->second;
            if (folder == "/lib")
                l.subfolders = subs;
            return l;
        };
        cb.fs.folderTime = [] (const std::string&) { return (std::int64_t) 1; };
        cb.stat = [this] (const std::string& path, std::int64_t& size, std::int64_t& time) {
            const std::lock_guard<std::mutex> g (m);
            const auto slash = path.find_last_of ('/');
            const auto it = folders.find (path.substr (0, slash));
            if (it == folders.end() || std::find (it->second.begin(), it->second.end(), path.substr (slash + 1)) == it->second.end())
                return false;
            size = 1000;
            time = 5;
            return true;
        };
        cb.decode = [this] (const std::string& path, double, std::vector<float>& mono, double& rate) {
            ++decodes;
            if (! hold.empty() && path == hold)
            {
                std::unique_lock<std::mutex> g (gateLock);
                holding = true;
                gate.notify_all();
                gate.wait (g, [this] { return released; });
            }
            if (decodeMs > 0)
                std::this_thread::sleep_for (std::chrono::milliseconds (decodeMs));
            if (kindOf (path) == 'x')
                return false;
            rate = 44100.0;
            mono = sound (kindOf (path), variantOf (path));
            return true;
        };
        cb.accepts = [] (const std::string& name) { return name.size() > 4 && name.compare (name.size() - 4, 4, ".wav") == 0; };
        cb.load = [this] {
            const std::lock_guard<std::mutex> g (m);
            return saved;
        };
        cb.save = [this] (const std::string& bytes) {
            const std::lock_guard<std::mutex> g (m);
            saved = bytes;
            ++saves;
        };
        cb.shouldPause = [this] { return playing.load(); };
        cb.throttle = false;
        return cb;
    }
};

struct Inbox
{
    std::mutex m;
    std::vector<lynx::similarity::Result> results;
    std::function<void (const lynx::similarity::Result&)> sink()
    {
        return [this] (const lynx::similarity::Result& r) {
            const std::lock_guard<std::mutex> g (m);
            results.push_back (r);
        };
    }
    std::vector<lynx::similarity::Result> take()
    {
        const std::lock_guard<std::mutex> g (m);
        return std::exchange (results, {});
    }
};
} // namespace s190as

TEST (similar190as_analysis_puts_like_with_like)
{
    using namespace s190as;
    lynx::similarity::Index index;
    for (const char kind : { 'k', 'h', 'p', 'l' })
        for (int v = 0; v < 8; ++v)
        {
            lynx::similarity::Entry e;
            e.path = std::string ("/lib/") + kind + std::to_string (v) + ".wav";
            e.valid = true;
            e.features = featuresOf (kind, v);
            CHECK (index.put (e));
        }
    CHECK (index.size() == 32 && index.validCount() == 32);
    // every file's 7 nearest are the 7 other files of its kind, the file itself is never listed
    for (const auto& e : index.all())
    {
        const auto matches = index.rank (e.features, e.path, {}, 7);
        CHECK (matches.size() == 7);
        for (std::size_t i = 0; i < matches.size(); ++i)
        {
            CHECK (kindOf (matches[i].path) == kindOf (e.path));
            CHECK (matches[i].path != e.path);
            CHECK (i == 0 || matches[i].distance >= matches[i - 1].distance); // nearest first
        }
    }
    // the nearest variant of a kick is a neighbouring variant (its tuning and decay move with v)
    const auto* k3 = index.find ("/lib/k3.wav");
    CHECK (k3 != nullptr);
    const auto nearest = index.rank (k3->features, k3->path, {}, 1);
    CHECK (nearest.size() == 1 && std::abs (variantOf (nearest[0].path) - 3) == 1);
    // only under the given folders
    CHECK (index.rank (k3->features, k3->path, { "/elsewhere" }, 7).empty());
    CHECK (index.rank (k3->features, k3->path, { "/lib" }, 100).size() == 31);
    // the percentage: 100 for the same sound, falling with the distance
    CHECK (lynx::similarity::percent (0.0f) == 100);
    CHECK (lynx::similarity::percent (1.0f) < 100 && lynx::similarity::percent (1.0f) > lynx::similarity::percent (4.0f));
    CHECK (lynx::similarity::percent (1000.0f) == 0);
}

TEST (similar190as_level_silence_and_sample_rate_do_not_matter)
{
    using namespace s190as;
    lynx::similarity::Index index;
    for (const char kind : { 'k', 'h', 'p', 'l' })
        for (int v = 0; v < 6; ++v)
        {
            lynx::similarity::Entry e;
            e.path = std::string ("/lib/") + kind + std::to_string (v) + ".wav";
            e.valid = true;
            e.features = featuresOf (kind, v);
            index.put (e);
        }
    // the same kick 20 dB quieter, after 300 ms of silence, and at 48 kHz: each is nearest to the original
    const auto quiet = featuresOf ('k', 2, 44100.0, 0.08f);
    const auto late = featuresOf ('k', 2, 44100.0, 0.8f, 0.3);
    const auto rate48 = featuresOf ('k', 2, 48000.0);
    for (const auto* f : { &quiet, &late, &rate48 })
    {
        const auto m = index.rank (*f, "", {}, 1);
        CHECK (m.size() == 1 && m[0].path == "/lib/k2.wav");
    }
    const auto same = index.rank (quiet, "", {}, 1);
    CHECK (! same.empty() && same[0].distance < 0.01f); // level changes nothing

    // nothing to analyse: silence, almost nothing, nonsense rates, NaN-only audio
    Features f {};
    const std::vector<float> silence (44100, 0.0f);
    CHECK (! lynx::similarity::analyse (silence.data(), silence.size(), 44100.0, f));
    CHECK (! lynx::similarity::analyse (silence.data(), 4, 44100.0, f));
    CHECK (! lynx::similarity::analyse (nullptr, 0, 44100.0, f));
    const auto kick = sound ('k', 0);
    CHECK (! lynx::similarity::analyse (kick.data(), kick.size(), 0.0, f));
    CHECK (! lynx::similarity::analyse (kick.data(), kick.size(), std::nan (""), f));
    const std::vector<float> bad (4096, std::nanf (""));
    CHECK (! lynx::similarity::analyse (bad.data(), bad.size(), 44100.0, f));
    // a click of a few samples and a file with NaNs in it still give finite numbers
    std::vector<float> click (64, 0.0f);
    click[3] = 1.0f;
    click[30] = -0.5f;
    CHECK (lynx::similarity::analyse (click.data(), click.size(), 44100.0, f));
    auto holes = kick;
    holes[100] = std::nanf ("");
    holes[200] = std::numeric_limits<float>::infinity();
    CHECK (lynx::similarity::analyse (holes.data(), holes.size(), 44100.0, f));
    for (const auto v : f)
        CHECK (std::isfinite (v));
}

TEST (similar190as_index_file_and_limits)
{
    using namespace s190as;
    lynx::similarity::Index index;
    lynx::similarity::Entry a, b, c;
    a.path = "/lib/a/k0.wav";
    a.size = 10;
    a.time = 20;
    a.valid = true;
    a.features = featuresOf ('k', 0);
    b.path = "/lib/a/h0.wav";
    b.valid = true;
    b.features = featuresOf ('h', 0);
    c.path = "/lib/a/sub/x.wav"; // unusable: kept, never ranked
    CHECK (index.put (a) && index.put (b) && index.put (c));
    CHECK (index.size() == 3 && index.validCount() == 2);

    const auto bytes = index.toBytes();
    lynx::similarity::Index read;
    CHECK (read.fromBytes (bytes));
    CHECK (read.size() == 3 && read.validCount() == 2);
    const auto* ra = read.find ("/lib/a/k0.wav");
    CHECK (ra != nullptr && ra->valid && ra->size == 10 && ra->time == 20 && ra->features == a.features);
    CHECK (read.find ("/lib/a/sub/x.wav") != nullptr && ! read.find ("/lib/a/sub/x.wav")->valid);
    CHECK (read.rank (a.features, a.path, {}, 10).size() == 1);

    // a cut or foreign file never crashes: what was whole is kept, the rest dropped
    for (std::size_t cut = 0; cut < bytes.size(); cut += 7)
    {
        lynx::similarity::Index partial;
        partial.fromBytes (bytes.substr (0, cut));
        CHECK (partial.size() <= 3);
    }
    lynx::similarity::Index other;
    CHECK (! other.fromBytes ("not an index file at all"));
    CHECK (! other.fromBytes (""));
    auto wrong = bytes;
    wrong[8] = 7; // another feature count (an older or newer LYNX): not read
    CHECK (! other.fromBytes (wrong) && other.size() == 0);

    // replace, remove, prune (the folder was listed again: files gone from it are dropped, subfolders stay)
    a.valid = false;
    CHECK (index.put (a) && index.size() == 3 && index.validCount() == 1);
    CHECK (index.prune ("/lib/a", { "h0.wav" }, '/') == 1);
    CHECK (index.find ("/lib/a/k0.wav") == nullptr && index.find ("/lib/a/h0.wav") != nullptr && index.find ("/lib/a/sub/x.wav") != nullptr);
    CHECK (index.remove ("/lib/a/h0.wav") && ! index.remove ("/lib/a/h0.wav"));
    CHECK (index.size() == 1 && index.validCount() == 0);
    CHECK (index.rank (b.features, "", {}, 10).empty());
    // bounded: about 300 bytes per file, so the largest index is about 45 MB
    CHECK (bytes.size() < 3 * 330);
    CHECK (lynx::similarity::maxIndexedFiles * 330 < 64u * 1024 * 1024);
}

TEST (similar190as_worker_indexes_and_answers)
{
    using namespace s190as;
    Disk disk;
    Inbox inbox;
    {
        lynx::similarity::Service service (disk.callbacks());
        service.refresh ({ "/lib" });
        CHECK (service.waitIdle (60000));
        auto st = service.status();
        CHECK (st.loaded && ! st.busy && st.files == 24); // 4 folders x 6 sounds; the broken ones are not usable
        CHECK (disk.decodes == 28);                       // ... but were tried once; notes.txt never
        CHECK (disk.saves >= 1);

        const auto id = service.findSimilar ("/lib/hats/h2.wav", {}, 5, inbox.sink());
        CHECK (service.waitIdle (20000));
        auto got = inbox.take();
        CHECK (got.size() == 1 && got[0].id == id && ! got[0].failed && ! got[0].partial && got[0].searched == 24);
        CHECK (got[0].matches.size() == 5);
        for (const auto& m : got[0].matches)
            CHECK (kindOf (m.path) == 'h' && m.path != "/lib/hats/h2.wav");
        CHECK (disk.decodes == 28); // answered from the index: nothing decoded again

        // only one folder; a file that can't be analysed; a file that does not exist
        service.findSimilar ("/lib/hats/h2.wav", { "/lib/kicks" }, 50, inbox.sink());
        CHECK (service.waitIdle (20000));
        got = inbox.take();
        CHECK (got.size() == 1 && got[0].matches.size() == 6);
        service.findSimilar ("/lib/hats/x-broken.wav", {}, 5, inbox.sink());
        CHECK (service.waitIdle (20000));
        got = inbox.take();
        CHECK (got.size() == 1 && got[0].failed && got[0].matches.empty());
        CHECK (disk.decodes == 28); // (known to be unusable and unchanged: not tried again)
        service.findSimilar ("/lib/hats/gone.wav", {}, 5, inbox.sink());
        CHECK (service.waitIdle (20000));
        got = inbox.take();
        CHECK (got.size() == 1 && got[0].failed);

        // a second pass decodes nothing; a file deleted from its folder leaves the index, a new one comes in
        service.refresh ({ "/lib" });
        CHECK (service.waitIdle (60000));
        CHECK (disk.decodes == 28);
        {
            const std::lock_guard<std::mutex> g (disk.m);
            auto& kicks = disk.folders["/lib/kicks"];
            kicks.erase (std::find (kicks.begin(), kicks.end(), "k0.wav"));
            kicks.push_back ("k9.wav");
        }
        service.refresh ({ "/lib" });
        CHECK (service.waitIdle (60000));
        CHECK (service.status().files == 24 && disk.decodes == 29);
        service.findSimilar ("/lib/kicks/k1.wav", {}, 50, inbox.sink());
        CHECK (service.waitIdle (20000));
        got = inbox.take();
        bool hasGone = false, hasNew = false;
        for (const auto& m : got.empty() ? std::vector<lynx::similarity::Match>() : got[0].matches)
        {
            hasGone = hasGone || m.path == "/lib/kicks/k0.wav";
            hasNew = hasNew || m.path == "/lib/kicks/k9.wav";
        }
        CHECK (! hasGone && hasNew);
    }
    // the index was saved when the service closed: the next session starts with it and decodes nothing
    CHECK (! disk.saved.empty());
    const auto before = disk.decodes.load();
    lynx::similarity::Service again (disk.callbacks());
    again.refresh ({ "/lib" });
    CHECK (again.waitIdle (60000));
    CHECK (again.status().files == 24 && disk.decodes == before);
}

TEST (similar190as_query_first_pause_and_close)
{
    using namespace s190as;
    Disk disk;
    disk.decodeMs = 40; // a slow disk: the whole pass takes more than a second
    disk.hold = "/lib/kicks/k1.wav";
    Inbox inbox;
    {
        lynx::similarity::Service service (disk.callbacks());
        service.refresh ({ "/lib" });
        CHECK (service.status().busy); // (at once, before the worker starts)
        // (the pass is lowest priority: on a busy machine it may start late; no answer below depends on when)
        CHECK (disk.waitHeld (60000)); // the pass is in the middle of its second file
        // a query while the pass runs is served first, for a file the pass has not reached (loops come last), and
        // at once: not after the file being analysed (still held here) nor after a slow lowest-priority thread
        const auto id = service.findSimilar ("/lib/loops/l3.wav", {}, 3, inbox.sink());
        const auto began = std::chrono::steady_clock::now();
        std::vector<lynx::similarity::Result> got;
        while (got.empty() && std::chrono::steady_clock::now() - began < std::chrono::seconds (10))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
            got = inbox.take();
        }
        CHECK (got.size() == 1 && got[0].id == id && ! got[0].failed && got[0].partial); // partial: the pass goes on
        CHECK (service.status().busy && disk.decodes == 3); // k0, k1 (still in progress) and the clicked file
        disk.release();

        // a newer query replaces one still waiting: only the newest is answered
        service.findSimilar ("/lib/kicks/k1.wav", {}, 3, inbox.sink());
        const auto newest = service.findSimilar ("/lib/kicks/k2.wav", {}, 3, inbox.sink());
        CHECK (service.waitIdle (60000));
        got = inbox.take();
        CHECK (! got.empty() && got.back().id == newest && got.back().query == "/lib/kicks/k2.wav");
        CHECK (service.status().files == 24);
        // (an answer given during the pass stays "partial": the Browser asks again when the pass ends)
        service.findSimilar ("/lib/kicks/k2.wav", {}, 3, inbox.sink());
        CHECK (service.waitIdle (20000));
        got = inbox.take();
        CHECK (got.size() == 1 && ! got[0].partial && got[0].matches.size() == 3 && got[0].searched == 24);
    }

    // while LYNX plays, the pass waits; it goes on when playback stops; a query is still answered
    {
        Disk second;
        second.playing = true;
        lynx::similarity::Service service (second.callbacks());
        service.refresh ({ "/lib" });
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        CHECK (second.decodes == 0);
        CHECK (service.status().busy);
        service.findSimilar ("/lib/kicks/k1.wav", {}, 3, inbox.sink());
        const auto began = std::chrono::steady_clock::now();
        std::vector<lynx::similarity::Result> got;
        while (got.empty() && std::chrono::steady_clock::now() - began < std::chrono::seconds (10))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
            got = inbox.take();
        }
        CHECK (got.size() == 1 && ! got[0].failed && got[0].matches.empty()); // (only the clicked file is known yet)
        CHECK (second.decodes == 1);
        second.playing = false;
        CHECK (service.waitIdle (60000));
        CHECK (service.status().files == 24);
    }

    // closing in the middle of a pass waits for the file being analysed (kills nothing), saves what was analysed and
    // does not go on with the pass
    {
        Disk third;
        third.decodeMs = 40;
        third.hold = "/lib/kicks/k2.wav";
        auto service = std::make_unique<lynx::similarity::Service> (third.callbacks());
        service->refresh ({ "/lib" });
        CHECK (third.waitHeld (60000)); // the pass is in the middle of its third file
        std::atomic<bool> closed { false };
        const auto began = std::chrono::steady_clock::now();
        std::thread closer ([&] {
            service.reset();
            closed = true;
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        CHECK (! closed); // (still analysing k2: closing never abandons a file)
        third.release();
        closer.join();
        const auto took = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - began).count();
        CHECK (took < 5000.0); // (the whole pass would take over a second even on an idle machine)
        CHECK (third.decodes >= 3 && third.decodes < 28);
        lynx::similarity::Index saved;
        CHECK (saved.fromBytes (third.saved) && saved.size() >= 3 && saved.size() < 28);
        const auto* k2 = saved.find ("/lib/kicks/k2.wav");
        CHECK (k2 != nullptr && k2->valid); // the file being analysed when closing was asked was finished and saved
    }
}
