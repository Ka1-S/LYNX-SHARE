// Round 190rh (QUEUE-0.1.33 item 78): FL's native engines without FL Studio installed (core/NativeData.h) — LYNX's own
// data sets, the presets of the new banks, which set a channel plays and that it never changes by itself.

#include "core/LynxBanks.h"
#include "core/NativeData.h"
#include "core/NativeEngines.h"
#include "core/TransistorBass.h"

namespace noflnatives190rh
{
// this computer's FL installation (the tests' data folder is <installation>/Data) for the time of a test; "" = none
struct Installation
{
    std::string before = lynx::nativedata::factoryData();
    explicit Installation (const std::string& folder) { lynx::nativedata::setFactoryData (folder); }
    ~Installation() { lynx::nativedata::setFactoryData (before); }
};
std::string realInstallation() { return realFlpDir.empty() ? std::string() : std::filesystem::path (realFlpDir).parent_path().string(); }

// the engines this round's banks and sets are for (they grow with its steps)
const std::vector<lynx::InstrumentKind>& kinds()
{
    static const std::vector<lynx::InstrumentKind> list = { lynx::InstrumentKind::transistorbass, lynx::InstrumentKind::minisynth, lynx::InstrumentKind::sawer };
    return list;
}

// two notes (the second overlaps nothing) through the rack's instrument, interleaved
std::vector<float> render (const lynx::ChannelDef& c, int key = 60, double seconds = 1.5)
{
    auto inst = lynx::makeInstrument (c.instrument);
    std::vector<float> out;
    if (inst == nullptr)
        return out;
    inst->prepare (48000.0);
    inst->configure (c, 130.0);
    std::vector<float> l (512), r (512);
    const auto blocks = (int) (seconds * 48000.0 / 512.0);
    for (int b = 0; b < blocks; ++b)
    {
        if (b == 0)
            inst->noteOn (key, 0.8f);
        if (b == blocks / 3)
            inst->noteOff (key);
        if (b == blocks / 2)
            inst->noteOn (key + 7, 1.0f);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        inst->render (l.data(), r.data(), 512, 1.0f, 1.0f);
        for (int i = 0; i < 512; ++i)
            out.push_back (l[(std::size_t) i]), out.push_back (r[(std::size_t) i]);
    }
    return out;
}
double rms (const std::vector<float>& x)
{
    double s = 0.0;
    for (const auto v : x)
        s += (double) v * v;
    return x.empty() ? 0.0 : std::sqrt (s / (double) x.size());
}
double peak (const std::vector<float>& x)
{
    double p = 0.0;
    for (const auto v : x)
        p = std::max (p, (double) std::fabs (v));
    return p;
}
} // namespace noflnatives190rh

TEST (noflnatives190rh_paths)
{
    using namespace lynx;
    namespace fs = std::filesystem;
    const auto own = nativedata::ownPath (InstrumentKind::transistorbass);
    CHECK (own == "lynxdata:transistorbass/1" && nativedata::isOwn (own) && nativedata::hasOwnSet (InstrumentKind::transistorbass));
    CHECK (nativedata::ownRevision (own, InstrumentKind::transistorbass) == 1);
    CHECK (nativedata::ownRevision ("lynxdata:transistorbass/9", InstrumentKind::transistorbass) == 1); // (a newer LYNX's)
    CHECK (nativedata::ownRevision ("lynxdata:transistorbass/", InstrumentKind::transistorbass) == 0);
    CHECK (nativedata::ownRevision ("lynxdata:transistorbass/1x", InstrumentKind::transistorbass) == 0);
    CHECK (nativedata::ownRevision (own, InstrumentKind::sytrus) == 0 && nativedata::ownPath (InstrumentKind::sytrus).empty());
    CHECK (! nativedata::isOwn ("C:\\lynxdata:x") && ! nativedata::isOwn (""));

    // a file of FL's named where FL was on another computer: found in this computer's installation, names ignoring case
    const auto root = fs::temp_directory_path() / ("lynx-noflnatives-" + processTempTag());
    fs::create_directories (root / "Plugins" / "Fruity" / "Generators" / "Transistor Bass");
    const auto file = root / "Plugins" / "Fruity" / "Generators" / "Transistor Bass" / "Engine_x64.dll";
    std::ofstream (file) << "x";
    std::ofstream (root.parent_path() / "lynx-noflnatives-outside.txt") << "x";
    {
        noflnatives190rh::Installation here (root.string());
        const std::string elsewhere = "D:\\Music\\FL Studio 2025\\Plugins\\Fruity\\Generators\\transistor bass\\engine_x64.dll";
        CHECK (fs::exists (nativedata::locate (elsewhere)) && fs::equivalent (nativedata::locate (elsewhere), file));
        CHECK (nativedata::locate (file.string()) == file.string()); // (where it is)
        CHECK (nativedata::locate (own) == own && nativedata::locate ("").empty());
        const std::string gone = "D:\\FL\\Plugins\\Fruity\\Generators\\Nope\\x.dll", other = "D:\\Samples\\kick.wav";
        CHECK (nativedata::locate (gone) == gone && nativedata::locate (other) == other);
        const std::string out = "D:\\FL\\Plugins\\Fruity\\..\\..\\..\\lynx-noflnatives-outside.txt"; // never out of the installation
        CHECK (nativedata::locate (out) == out);
    }
    {
        noflnatives190rh::Installation none ("");
        const std::string gone = "D:\\FL\\Plugins\\Fruity\\Generators\\Transistor Bass\\engine_x64.dll";
        CHECK (nativedata::locate (gone) == gone);
    }
    fs::remove (root.parent_path() / "lynx-noflnatives-outside.txt");
    fs::remove_all (root);
}

TEST (noflnatives190rh_transistor_bass_pulse_cycles)
{
    for (const auto& [rate, count] : { std::pair { 192000.0, 57 }, std::pair { 44100.0, 55 } })
    {
        const auto cycles = lynx::nativedata::bassPulseCycles (rate, count);
        CHECK ((int) cycles.size() == count);
        for (std::size_t k = 0; k < cycles.size(); ++k)
        {
            const auto& c = cycles[k];
            double sum = 0.0, energy = 0.0;
            for (const auto v : c)
                sum += v, energy += (double) v * v;
            CHECK (std::fabs (sum / (double) c.size()) < 1.0e-4);
            CHECK (std::sqrt (energy / (double) c.size()) > 0.4 && std::sqrt (energy / (double) c.size()) < 1.3);
            CHECK (k == 0 || c.size() < cycles[k - 1].size()); // longest first
        }
    }
}

TEST (noflnatives190rh_banks_play_without_fl)
{
    using namespace lynx;
    noflnatives190rh::Installation none ("");
    const natives::FlAccess noFl {};
    for (const auto kind : noflnatives190rh::kinds())
    {
        const auto* engine = natives::instrumentEngine (kind);
        CHECK (engine != nullptr && engine->needs == natives::Needs::nothing && natives::available (*engine, noFl));
        const auto made = natives::makeChannel (kind, noFl);
        CHECK (made.has_value());
        if (! made)
            continue;
        const auto& bank = banks::bankOf (kind);
        CHECK (bank.size() >= 16);
        std::set<std::string> names;
        ChannelDef blank;
        blank.instrument = kind;
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            CHECK (names.insert (bank[(std::size_t) i].name).second && ! bank[(std::size_t) i].category.empty());
            const auto c = banks::withPreset (blank, i);
            CHECK (c.has_value());
            if (! c)
                continue;
            const auto a = noflnatives190rh::render (*c);
            const auto level = noflnatives190rh::rms (a), top = noflnatives190rh::peak (a);
            CHECK (level > 2.0e-3 && std::isfinite (level) && top < 2.0); // plays, and not far over full scale
            if (! (level > 2.0e-3 && top < 2.0))
                std::printf ("  %s / %s: rms %g peak %g\n", engine->name, bank[(std::size_t) i].name.c_str(), level, top);
            // the data set is in the channel where the engine has one, and the sound is the same every time
            CHECK (c->samplePath == nativedata::ownPath (kind));
            CHECK (noflnatives190rh::render (*c) == a);
        }
        // ... also with FL Studio on the computer: the project's choice, never the computer's
        if (const auto fl = noflnatives190rh::realInstallation(); ! fl.empty())
        {
            const auto without = noflnatives190rh::render (*made);
            noflnatives190rh::Installation here (fl);
            CHECK (noflnatives190rh::render (*made) == without);
            if (nativedata::hasOwnSet (kind))
                CHECK (natives::dataSource (*made) == natives::DataSource::own);
        }
        // a preset picked on a channel that plays FL's data keeps FL's data
        if (nativedata::hasOwnSet (kind))
        {
            auto withFl = blank;
            withFl.samplePath = "C:\\FL\\Plugins\\Fruity\\Generators\\x.dll";
            CHECK (banks::withPreset (withFl, 0)->samplePath == withFl.samplePath);
        }
    }
}

// FL's file is not on this computer: LYNX's set plays, the channel keeps FL's path (D2); with the file back, FL's data
TEST (noflnatives190rh_missing_fl_data)
{
    using namespace lynx;
    for (const auto kind : noflnatives190rh::kinds())
    {
        if (! nativedata::hasOwnSet (kind))
            continue;
        noflnatives190rh::Installation none ("");
        auto c = *natives::makeChannel (kind, {});
        const auto own = noflnatives190rh::render (c);
        const char* file = kind == InstrumentKind::minisynth ? "Minisynth\\oscdata.dat" : kind == InstrumentKind::sawer ? "Sawer\\data.saw" : "Transistor Bass\\engine_x64.dll";
        c.samplePath = std::string ("Z:\\no such place\\Plugins\\Fruity\\Generators\\") + file;
        CHECK (natives::dataSource (c) == natives::DataSource::ownForFl);
        CHECK (noflnatives190rh::render (c) == own);
        if (const auto fl = noflnatives190rh::realInstallation(); ! fl.empty())
        {
            noflnatives190rh::Installation here (fl);
            CHECK (natives::dataSource (c) == natives::DataSource::fl);
            CHECK (! natives::flDataPath (kind, natives142::flAccess()).empty());
        }
    }
}

// an .flp's Transistor Bass opened without FL: the engine on FL's state with LYNX's set, and a line in the report (D1)
TEST (noflnatives190rh_flp_import_without_fl)
{
    using namespace lynx;
    noflnatives190rh::Installation none ("");
    flp::Project project;
    flp::Channel c;
    c.type = flp::ChannelType::instrument;
    c.name = "Bass";
    c.pluginName = "Transistor Bass";
    c.pluginState = natives::makeChannel (InstrumentKind::transistorbass, {})->pluginState;
    project.channels.push_back (c);
    FlpImportReport report;
    FlpImportOptions options;
    options.report = &report;
    const auto song = importFlp (project, options);
    CHECK (song.channels.size() == 1);
    if (song.channels.empty())
        return;
    const auto& d = song.channels.front();
    CHECK (d.instrument == InstrumentKind::transistorbass && d.pluginState == c.pluginState);
    CHECK (d.samplePath == nativedata::ownPath (InstrumentKind::transistorbass));
    CHECK (noflnatives190rh::rms (noflnatives190rh::render (d)) > 2.0e-3);
    bool said = false;
    for (const auto& [line, n] : report.notReproduced)
        said = said || line.find ("Transistor Bass") != std::string::npos;
    CHECK (said && report.unsupported.empty());
}
