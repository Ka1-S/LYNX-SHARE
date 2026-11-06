// Round 169g (discussion #56 + the crash reports of 2026-10-31…11-03): FL engine DLLs of other FL Studio builds,
// the export threads' Cancel, docs/analise/REA_rodada169g.md

#include <thread>

#include "core/DrumSynthLive.h"
#include "core/MessageThreadOwned.h"
#include "core/PhysicalModels.h"
#include "core/Sha256.h"
#include "core/Slayer.h"
#include "core/TransistorBass.h"

namespace crash169g
{
namespace fs = std::filesystem;

std::vector<std::uint8_t> readAll (const fs::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

void writeAll (const fs::path& p, const std::vector<std::uint8_t>& b)
{
    std::ofstream (p, std::ios::binary).write ((const char*) b.data(), (std::streamsize) b.size());
}

// FL Studio 2025.1.6's engine DLLs, taken from its installer (not part of the repository): LYNX_FL25_DLLS=<folder>,
// default /mnt/e/lynx-work/crash169g/fl25 (3392 = Transistor Bass engine_x64, 1542 = Slayer_Engine, 310 =
// BooBass_Eng_x64, 2741 = drumsynth_eng_x64); empty = not on this computer
std::string fl25 (const char* name)
{
    const char* env = std::getenv ("LYNX_FL25_DLLS");
    const auto p = fs::path (env != nullptr && *env != 0 ? env : "/mnt/e/lynx-work/crash169g/fl25") / name;
    return fs::is_regular_file (p) ? p.string() : std::string();
}

std::string flGenerator (const char* folder, const char* file)
{
    if (realFlpDir.empty())
        return {};
    const auto p = fs::path (realFlpDir).parent_path() / "Plugins" / "Fruity" / "Generators" / folder / file;
    return fs::is_regular_file (p) ? p.string() : std::string();
}

// a minimal PE32+ header: "MZ", e_lfanew, "PE\0\0", optional header magic 0x20B, checksum and certificate directory
std::vector<std::uint8_t> fakePe (std::uint32_t checksum, std::uint32_t certAt, std::size_t size)
{
    std::vector<std::uint8_t> b (size, 0x11);
    auto put32 = [&b] (std::size_t at, std::uint32_t v) { std::memcpy (b.data() + at, &v, 4); };
    b[0] = 'M', b[1] = 'Z';
    put32 (0x3c, 0x80);
    put32 (0x80, 0x00004550u);
    b[0x80 + 24] = 0x0b, b[0x80 + 25] = 0x02;
    put32 (0x80 + 24 + 64, checksum);
    put32 (0x80 + 24 + 112 + 32, certAt);
    put32 (0x80 + 24 + 112 + 36, certAt != 0 ? (std::uint32_t) (size - certAt) : 0u);
    return b;
}
struct Job
{
    explicit Job (std::atomic<int>& l) : live (l) { ++live; }
    ~Job() { --live; deletedOn = std::this_thread::get_id(); }
    std::atomic<int>& live;
    static inline std::thread::id deletedOn {};
};
} // namespace crash169g

TEST (crash169g_pe_image_hash_ignores_the_signature)
{
    using namespace crash169g;
    // unsigned, checksum 0: the whole-file hash (so the hashes LYNX already had stay valid)
    const auto plain = fakePe (0, 0, 0x2000);
    CHECK (lynx::peImageSha256Hex (plain) == lynx::sha256Hex (plain.data(), plain.size()));
    // the same image signed (a checksum, a certificate table appended): the same image hash
    auto signedCopy = fakePe (0x1234567, 0x2000, 0x2400);
    std::fill (signedCopy.begin() + 0x2000, signedCopy.end(), (std::uint8_t) 0x99);
    CHECK (lynx::peImageSha256Hex (signedCopy) == lynx::peImageSha256Hex (plain));
    CHECK (lynx::sha256Hex (signedCopy.data(), signedCopy.size()) != lynx::sha256Hex (plain.data(), plain.size()));
    // a different image: a different hash; not a PE: the plain hash
    auto other = plain;
    other[0x1800] ^= 1;
    CHECK (lynx::peImageSha256Hex (other) != lynx::peImageSha256Hex (plain));
    const std::vector<std::uint8_t> text { 'h', 'e', 'l', 'l', 'o' };
    CHECK (lynx::peImageSha256Hex (text) == lynx::sha256Hex (text.data(), text.size()));
}

// discussion #56 ("Transistor Bass is described as implemented but I really can't get it working"): only FL Studio
// 2026's engine_x64.dll was accepted (whole-file hash, fixed addresses); FL 2025's has the same 57 + 55 cycles at
// other offsets. Any build with them now plays, sample for sample the same.
TEST (crash169g_transistor_bass_plays_with_other_fl_builds)
{
    using namespace crash169g;
    const auto dll26 = flGenerator ("Transistor Bass", "engine_x64.dll");
    if (dll26.empty())
        return; // (no FL installation)
    CHECK (lynx::transistorBassDllUsable (dll26));
    const auto state = tbassPresetState ("Factory programs.fst");
    CHECK (! state.empty());
    std::vector<lynx::tbass::RefNote> notes (2);
    notes[0].key = 36, notes[0].release = 30000;
    notes[1].start = 33000, notes[1].key = 43;
    const auto reference = lynx::tbass::renderReference (state, dll26, notes, 44100.0, 66150);
    double energy = 0.0;
    for (const auto v : reference)
        energy += (double) v * v;
    CHECK (reference.size() == 2 * 66150 && energy > 1.0);

    // the same DLL with its data 0x400 bytes further on (another build's layout): found by shape, same sound
    auto bytes = readAll (dll26);
    bytes.insert (bytes.begin() + 0x1000, 0x400, (std::uint8_t) 0);
    const auto shifted = (fs::temp_directory_path() / ("lynx-crash169g-tbass-shifted-" + processTempTag() + ".dll")).string();
    writeAll (shifted, bytes);
    CHECK (lynx::transistorBassDllUsable (shifted));
    CHECK (lynx::tbass::renderReference (state, shifted, notes, 44100.0, 66150) == reference);
    // no cycles in it (another plugin's DLL, a truncated file): not usable, as before
    bytes.resize (0xF0000);
    writeAll (shifted, bytes);
    CHECK (! lynx::transistorBassDllUsable (shifted));
    fs::remove (shifted);

    // FL Studio 2025.1.6's own engine_x64.dll (when this computer has a copy)
    if (const auto dll25 = fl25 ("3392.dll"); ! dll25.empty())
    {
        CHECK (lynx::transistorBassDllUsable (dll25));
        CHECK (lynx::tbass::renderReference (state, dll25, notes, 44100.0, 66150) == reference);
    }
}

// the user's path: Transistor Bass added from the plugin list (natives::makeChannel) on a computer whose FL is 2025
TEST (crash169g_transistor_bass_from_the_plugin_list_with_fl_2025)
{
    using namespace crash169g;
    using namespace lynx;
    const auto dll25 = fl25 ("3392.dll");
    if (realFlpDir.empty() || dll25.empty())
        return;
    const auto root = fs::temp_directory_path() / ("lynx-crash169g-fl25-" + processTempTag());
    fs::create_directories (root / "Plugins" / "Fruity" / "Generators" / "Transistor Bass");
    fs::copy_file (dll25, root / "Plugins" / "Fruity" / "Generators" / "Transistor Bass" / "engine_x64.dll", fs::copy_options::overwrite_existing);
    auto fl = natives142::flAccess();
    const auto data = fl.roots.data;
    fl.roots.factoryData = root.string();
    const auto* engine = natives::instrumentEngine (InstrumentKind::transistorbass);
    CHECK (engine != nullptr && natives::available (*engine, fl));
    const auto c = natives::makeChannel (InstrumentKind::transistorbass, fl);
    CHECK (c.has_value());
    if (c)
    {
        CHECK (c->instrument == InstrumentKind::transistorbass && c->samplePath.find ("lynx-crash169g-fl25") != std::string::npos);
        CHECK (natives142::renderChannel (*c, fl, 60) > 1.0e-3);
    }
    (void) data;
    fs::remove_all (root);
}

// the other engines that read FL's DLLs, with FL 2025.1.6's builds: FL Slayer's engine is the same image signed
// again, BooBass's and DrumSynth Live's differ in time stamps only
TEST (crash169g_other_engine_dlls_of_fl_2025)
{
    using namespace crash169g;
    using namespace lynx;
    if (const auto slayer25 = fl25 ("1542.dll"); ! slayer25.empty())
    {
        CHECK (slayerDllUsable (slayer25));
        const auto c = realFlpDir.empty() ? std::nullopt : natives::makeChannel (InstrumentKind::slayer, natives142::flAccess());
        if (const auto slayer26 = flGenerator ("FL Slayer", "Slayer_Engine.dll"); ! slayer26.empty() && c)
        {
            const auto params = slayerStateParams (c->pluginState);
            CHECK (params.size() == 29);
            std::vector<slayer::RefNote> notes (1);
            notes[0].release = 20000;
            const auto a = slayer::renderReference (params, slayer26, notes, 44100.0, 44100);
            CHECK (! a.empty() && a == slayer::renderReference (params, slayer25, notes, 44100.0, 44100));
        }
    }
    if (const auto boo25 = fl25 ("310.dll"); ! boo25.empty())
    {
        CHECK (booBassDllUsable (boo25));
        if (const auto boo26 = flGenerator ("BooBass", "BooBass_Eng_x64.dll"); ! boo26.empty() && ! realFlpDir.empty())
        {
            const auto fl = natives142::flAccess();
            if (auto c = natives::makeChannel (InstrumentKind::boobass, fl))
            {
                const auto a = natives142::renderChannel (*c, fl, 40);
                c->samplePath = boo25;
                CHECK (a > 1.0e-3 && natives142::renderChannel (*c, fl, 40) == a);
            }
        }
    }
    if (const auto ds25 = fl25 ("2741.dll"); ! ds25.empty())
        CHECK (drumSynthDllUsable (ds25));
}

// the export threads' Cancel (ui/ExportDialog.cpp): the worker never waits for a message thread that is itself
// waiting for the worker; the object is still made and deleted by the posted calls only
TEST (crash169g_message_thread_owned_cancel)
{
    using Job = crash169g::Job;
    std::mutex lock;
    std::vector<std::function<void()>> queue; // the "message thread": runs only when the test says so
    auto post = [&] (std::function<void()> f) {
        const std::lock_guard<std::mutex> g (lock);
        queue.push_back (std::move (f));
    };
    auto runQueue = [&] {
        std::vector<std::function<void()>> now;
        {
            const std::lock_guard<std::mutex> g (lock);
            now.swap (queue);
        }
        for (auto& f : now)
            f();
    };
    std::atomic<int> live { 0 };
    std::atomic<bool> stop { false };

    // normal: created and deleted by the owning thread while the worker waits
    {
        std::atomic<bool> running { true };
        std::thread owner ([&] {
            while (running)
            {
                runQueue();
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
            runQueue();
        });
        std::thread worker ([&] {
            lynx::MessageThreadOwned<Job> slot (post, [&] { return stop.load(); });
            auto* j = slot.create ([&] { return std::make_unique<Job> (live); });
            CHECK (j != nullptr && live == 1);
            slot.release();
            CHECK (live == 0);
        });
        worker.join();
        running = false;
        const auto ownerId = owner.get_id();
        owner.join();
        CHECK (live == 0 && Job::deletedOn == ownerId);
    }

    // cancelled while the owning thread is blocked (Cancel waiting in stopThread): the worker returns at once
    stop = false;
    const auto started = std::chrono::steady_clock::now();
    std::thread worker ([&] {
        lynx::MessageThreadOwned<Job> slot (post, [&] { return stop.load(); });
        auto* j = slot.create ([&] { return std::make_unique<Job> (live); });
        CHECK (j == nullptr);
    }); // (its destructor queues the deletion and does not wait either)
    std::this_thread::sleep_for (std::chrono::milliseconds (60));
    stop = true;
    worker.join();
    CHECK (std::chrono::steady_clock::now() - started < std::chrono::seconds (5));
    CHECK (live == 0);
    runQueue(); // the message thread free again: the creation call sees the cancel and makes nothing
    CHECK (live == 0);

    // cancelled after the object exists: deleted later by the owning thread, never by the worker
    stop = false;
    std::atomic<bool> made { false };
    std::thread worker2 ([&] {
        lynx::MessageThreadOwned<Job> slot (post, [&] { return stop.load(); });
        std::thread helper ([&] {
            while (! made)
            {
                runQueue();
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        });
        auto* j = slot.create ([&] { auto p = std::make_unique<Job> (live); made = true; return p; });
        helper.join();
        CHECK (j != nullptr && live == 1);
        stop = true;
    }); // (release in the destructor: queued, not waited for)
    worker2.join();
    CHECK (live == 1);
    const auto here = std::this_thread::get_id();
    runQueue();
    CHECK (live == 0 && Job::deletedOn == here);
}
