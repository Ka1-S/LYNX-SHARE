// Round 190rh (QUEUE-0.1.33 item 78): FL's native engines without FL Studio installed (core/NativeData.h) — LYNX's own
// data sets, the presets of the new banks, which set a channel plays and that it never changes by itself.

#include "core/FlInstall.h"
#include "core/LynxBanks.h"
#include "core/NativeData.h"
#include "core/NativeEngines.h"
#include "core/Toxic.h"
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
    static const std::vector<lynx::InstrumentKind> list = { lynx::InstrumentKind::transistorbass, lynx::InstrumentKind::minisynth, lynx::InstrumentKind::sawer, lynx::InstrumentKind::bassdrum, lynx::InstrumentKind::gms, lynx::InstrumentKind::sytrus, lynx::InstrumentKind::ogun, lynx::InstrumentKind::morphine, lynx::InstrumentKind::kepler, lynx::InstrumentKind::keplerExo, lynx::InstrumentKind::slayer, lynx::InstrumentKind::harmor, lynx::InstrumentKind::autogun };
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
            CHECK (c->samplePath == nativedata::ownPath (kind) || (kind == InstrumentKind::morphine && c->samplePath.empty())); // (Morphine: the presets with noise)
            CHECK (noflnatives190rh::render (*c) == a);
        }
        // ... also with FL Studio on the computer: the project's choice, never the computer's
        if (const auto fl = noflnatives190rh::realInstallation(); ! fl.empty())
        {
            const auto without = noflnatives190rh::render (*made);
            noflnatives190rh::Installation here (fl);
            const auto with = noflnatives190rh::render (*made);
            if (kind == InstrumentKind::kepler || kind == InstrumentKind::keplerExo) // (last bits: FL's engine file brings its library's power tables)
            {
                double worst = 0.0;
                for (std::size_t i = 0; i < with.size(); ++i)
                    worst = std::max (worst, (double) std::fabs (with[i] - without[i]));
                CHECK (worst < 1.0e-4);
            }
            else
                CHECK (with == without);
            if (nativedata::hasOwnSet (kind) && kind != InstrumentKind::morphine)
                CHECK (natives::dataSource (*made) == natives::DataSource::own);
        }
        // a preset picked on a channel that plays FL's data keeps FL's data
        if (nativedata::hasOwnSet (kind) && kind != InstrumentKind::morphine)
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
        if (! nativedata::hasOwnSet (kind) || kind == InstrumentKind::morphine)
            continue;
        noflnatives190rh::Installation none ("");
        auto c = *natives::makeChannel (kind, {});
        const auto own = noflnatives190rh::render (c);
        const char* file = kind == InstrumentKind::minisynth ? "Minisynth\\oscdata.dat" : kind == InstrumentKind::sawer ? "Sawer\\data.saw" : kind == InstrumentKind::bassdrum ? "BassDrum" : kind == InstrumentKind::gms ? "GMS\\Synth Waves" : kind == InstrumentKind::kepler ? "Kepler\\engine_x64.dll" : kind == InstrumentKind::slayer ? "FL Slayer\\Slayer_Engine.dll" : kind == InstrumentKind::keplerExo ? "Kepler Exo\\engine_x64.dll" : "Transistor Bass\\engine_x64.dll";
        c.samplePath = std::string ("Z:\\no such place\\Plugins\\Fruity\\Generators\\") + file;
        CHECK (natives::dataSource (c) == natives::DataSource::ownForFl);
        if (kind != InstrumentKind::kepler && kind != InstrumentKind::keplerExo)
            CHECK (noflnatives190rh::render (c) == own);
        else
            CHECK (noflnatives190rh::rms (noflnatives190rh::render (c)) > 2.0e-3);
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

// Settings › Files › FL Studio folder (issue #291): what a folder is, on drives mounted with odd names and permissions
TEST (noflnatives190rh_fl_folder)
{
    using namespace lynx;
    namespace fs = std::filesystem;
    using flinstall::Status;
    const auto root = fs::temp_directory_path() / ("lynx-flfolder-" + processTempTag());
    fs::create_directories (root);
    const auto maker = root / "windows" / "program files" / "IMAGE-LINE"; // (a mount that shows other capitals)
    const auto fl25 = maker / "FL Studio 2025", fl26 = maker / "FL Studio 2026";
    fs::create_directories (fl25 / "data");
    fs::create_directories (fl25 / "PLUGINS" / "fruity");
    std::ofstream (fl25 / "data" / "x.txt") << "x";
    const auto is = [] (const fs::path& p, const fs::path& install) {
        const auto c = flinstall::check (p.string());
        return c.status == Status::ok && fs::equivalent (c.folder, install);
    };
    // the folder, its program, one inside, "Image-Line", the drive; a trailing slash
    CHECK (is (fl25, fl25) && is (fl25 / "data", fl25) && is (fl25 / "PLUGINS" / "fruity", fl25) && is (maker, fl25) && is (root / "windows", fl25));
    CHECK (flinstall::check (fl25.string() + "/").status == Status::ok);
    std::ofstream (fl25 / "FL64.exe") << "not a program";
    CHECK (is (fl25 / "FL64.exe", fl25) && flinstall::check (fl25.string()).version.empty());
    // two installations: the user chooses
    fs::create_directories (fl26 / "Data");
    fs::create_directories (fl26 / "Plugins" / "Fruity");
    const auto two = flinstall::check (maker.string());
    CHECK (two.status == Status::several && two.candidates.size() == 2 && two.folder.empty());
    CHECK (is (fl26, fl26));
    // found on mounted drives (/media/<user>/<drive> too), nothing outside Program Files
    const auto media = root / "media" / "me";
    fs::create_directories (media / "Drive" / "Program Files" / "Image-Line" / "FL Studio 21" / "Data");
    fs::create_directories (media / "Drive" / "Program Files" / "Image-Line" / "FL Studio 21" / "Plugins" / "Fruity");
    CHECK (flinstall::onMountedDrives ({ root.string() }).size() == 2);
    CHECK (flinstall::onMountedDrives ({ (root / "media").string() }).size() == 1);
    CHECK (flinstall::onMountedDrives ({ (root / "nothing").string() }).empty());
    // not an installation
    fs::create_directories (root / "Documents" / "Presets");
    fs::create_directories (root / "Documents" / "Settings");
    CHECK (flinstall::check ((root / "Documents").string()).status == Status::userData);
    CHECK (flinstall::check ((root / "media" / "me").string()).status == Status::notFl);
    CHECK (flinstall::check ((root / "gone").string()).status == Status::missing && flinstall::check ("").status == Status::missing);
    // the program's version, as Windows' resource holds it
    std::vector<unsigned char> exe (64, 0);
    const unsigned char info[] = { 0xBD, 0x04, 0xEF, 0xFE, 0, 0, 1, 0, 1, 0, 26, 0, 0x16, 0x16, 7, 0 };
    std::copy (std::begin (info), std::end (info), exe.begin() + 16);
    CHECK (flinstall::versionOf (exe) == "26.1.7.5654" && flinstall::versionOf ({}).empty());

   #if ! defined(_WIN32)
    const auto mode = [] (const fs::path& p, int bits) {
        std::error_code ec;
        fs::permissions (p, (fs::perms) bits, ec);
        return ! ec;
    };
    // a read-only drive: an installation like any other, and the check writes nothing
    mode (fl25 / "data", 0555), mode (fl25, 0555);
    const auto before = fs::last_write_time (fl25);
    CHECK (is (fl25, fl25) && fs::last_write_time (fl25) == before);
    mode (fl25, 0755);
    // (root reads everything: permissions say nothing there)
    mode (fl25 / "data", 0);
    std::error_code ec;
    fs::directory_iterator probe (fl25 / "data", ec);
    if (ec)
    {
        const auto partly = flinstall::check (fl25.string());
        CHECK (partly.status == Status::ok && partly.missing == "Data"); // its Data can't be listed
        mode (fl25 / "data", 0755);
        for (const auto bits : { 0, 0111, 0444 }) // nothing; no listing; a listing of names nobody may look at
        {
            mode (fl25, bits);
            CHECK (flinstall::check (fl25.string()).status == Status::notReadable);
            mode (fl25, 0755);
        }
        mode (fl25, 0);
        CHECK (flinstall::check ((fl25 / "data").string()).status == Status::notReadable);
        mode (fl25, 0755);
        mode (maker, 0); // (a folder on the way)
        CHECK (flinstall::check (fl25.string()).status == Status::notReadable);
        CHECK (flinstall::onMountedDrives ({ root.string() }).empty());
        mode (maker, 0755);
    }
    mode (fl25 / "data", 0755), mode (fl25, 0755), mode (maker, 0755);
   #endif
    fs::remove_all (root, ec);
}

// the FL effects whose settings live in their state: made without FL, they change or pass a signal, never silence or garbage
TEST (noflnatives190rh_effects_without_fl)
{
    using namespace lynx;
    for (const auto& e : natives::effectEngines())
    {
        const auto slot = natives::makeSlot (e.kind, {});
        CHECK (e.needs == natives::Needs::nothing && slot.has_value() && slot->kind == e.kind);
        if (! slot)
            continue;
        auto fx = makeEffect (*slot);
        fx->prepare (48000.0);
        double in = 0.0, out = 0.0;
        bool finite = true;
        for (int b = 0; b < 94; ++b)
        {
            std::vector<float> l (512), r (512);
            for (int i = 0; i < 512; ++i)
            {
                const auto t = (b * 512 + i) / 48000.0;
                l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.25 * std::sin (2.0 * M_PI * 220.0 * t) + 0.05 * std::sin (2.0 * M_PI * 3100.0 * t));
                in += 2.0 * l[(std::size_t) i] * l[(std::size_t) i];
            }
            fx->process (l.data(), r.data(), 512, slot->params, 120.0);
            for (int i = 0; i < 512; ++i)
            {
                finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
                out += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
            }
        }
        const auto change = 10.0 * std::log10 (out / in);
        CHECK (finite && change < 12.0 && (change > -20.0 || e.kind == EffectKind::vocodex)); // (Vocodex: its modulator is another track's)
        if (! (finite && change < 12.0 && (change > -20.0 || e.kind == EffectKind::vocodex)))
            std::printf ("  %s: %g dB\n", e.presetFolder, change);
    }
}

// Toxic Biohazard's built-in waves, DrumSynth Live's waves and BooBass's multisample: LYNX's own without FL; a channel that
// names FL's file plays them too while the file is away, and keeps its path
TEST (noflnatives190rh_toxic_drumsynth_boobass)
{
    using namespace lynx;
    const std::pair<InstrumentKind, const char*> files[] = { { InstrumentKind::toxic, "Toxic Biohazard\\Toxic Biohazard_x64.dll" },
                                                             { InstrumentKind::drumsynth, "Fruity DrumSynth Live\\drumsynth_eng_x64.dll" },
                                                             { InstrumentKind::boobass, "BooBass\\BooBass_Eng_x64.dll" } };
    for (const auto& [kind, file] : files)
    {
        noflnatives190rh::Installation none ("");
        auto c = natives::makeChannel (kind, {});
        CHECK (c.has_value() && c->samplePath == nativedata::ownPath (kind) && natives::dataSource (*c) == natives::DataSource::own);
        if (! c)
            continue;
        if (kind == InstrumentKind::toxic) // oscillator 1 on wave 9 (index 306: its wave, 25: its switch)
        {
            const float wave = 9.0f, on = 1.0f;
            std::memcpy (c->pluginState.data() + toxicParamOffset (c->pluginState, 306), &wave, 4);
            std::memcpy (c->pluginState.data() + toxicParamOffset (c->pluginState, 25), &on, 4);
        }
        // the kit's keys (DrumSynth Live: one patch per key)
        std::vector<float> own;
        int key = 60;
        for (const int k : { 60, 36, 48, 38 })
            if (own = noflnatives190rh::render (*c, k); noflnatives190rh::rms (own) > 2.0e-3)
            {
                key = k;
                break;
            }
        CHECK (noflnatives190rh::rms (own) > 2.0e-3 && noflnatives190rh::peak (own) < 2.5);
        auto away = *c;
        away.samplePath = std::string ("Z:\\no such place\\Plugins\\Fruity\\Generators\\") + file;
        CHECK (natives::dataSource (away) == natives::DataSource::ownForFl);
        CHECK (noflnatives190rh::render (away, key) == own);
        if (const auto fl = noflnatives190rh::realInstallation(); ! fl.empty())
        {
            noflnatives190rh::Installation here (fl);
            CHECK (natives::dataSource (away) == natives::DataSource::fl);
            CHECK (noflnatives190rh::render (*c, key) == own);
        }
    }
    // a channel without a path plays what it always did (LYNX's bank before round 190rh: the drawn wave and the sine)
    ChannelDef old;
    old.instrument = InstrumentKind::toxic;
    old.pluginState = toxicDefaultState();
    CHECK (natives::dataSource (old) == natives::DataSource::notNeeded);
}
