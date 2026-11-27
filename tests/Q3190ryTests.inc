// Round 190ry (0.1.32 stage Q3, fluidity): what a native engine needs to be offered is answered without listing FL's
// folders when a Default preset is there (natives::hasPresetCandidate); the answer is presetCandidates' own.

#include "core/NativeEngines.h"
#include "core/PerfProjects.h"

namespace q3190ry
{
// a pretend FL installation: the files it has (FL-style paths after resolveFlPath, '/' or '\\'), and how often it was listed
struct FakeFl
{
    std::set<std::string> files;
    std::shared_ptr<int> listings = std::make_shared<int> (0);

    static std::string norm (std::string p)
    {
        std::replace (p.begin(), p.end(), '\\', '/');
        return p;
    }

    lynx::natives::FlAccess access() const
    {
        lynx::natives::FlAccess fl;
        fl.roots.data = "/fl/Data";
        fl.roots.factoryData = "/fl";
        auto all = files;
        fl.fileExists = [all] (const std::string& p) { return all.count (norm (p)) > 0; };
        fl.listPresets = [all, n = listings] (const std::string& folder) {
            ++*n;
            std::vector<std::string> out;
            const auto prefix = norm (folder) + "/";
            for (const auto& f : all)
                if (f.rfind (prefix, 0) == 0)
                    out.push_back (f);
            return out;
        };
        return fl;
    }
};
} // namespace q3190ry

TEST (q3190ry_has_preset_candidate_matches_the_candidates)
{
    using namespace lynx;
    using q3190ry::FakeFl;
    const std::string presets = "/fl/Data/Patches/Plugin presets/";
    // Default.fst there: yes, and nothing is listed
    {
        FakeFl f;
        f.files = { presets + "Effects/Fruity Delay 3/Default.fst", presets + "Generators/Sytrus/Default.fst" };
        const auto fl = f.access();
        CHECK (natives::hasPresetCandidate ("Fruity Delay 3", false, fl));
        CHECK (natives::hasPresetCandidate ("Sytrus", true, fl));
        CHECK (*f.listings == 0);
        CHECK (! natives::presetCandidates ("Fruity Delay 3", false, fl).empty());
    }
    // no Default: the folder's other presets, the Plugin database entry (generators only), the extensions FL reads
    {
        FakeFl f;
        f.files = { presets + "Effects/Fruity Reeverb 2/Big hall.fst", presets + "Effects/Odd/readme.txt",
                    "/fl/Data/Patches/Plugin database/Generators/Synth classic/Harmor.fst", presets + "Generators/Toxic Biohazard/Default/Default.tbio" };
        const auto fl = f.access();
        for (const auto& [folder, generator] : std::vector<std::pair<std::string, bool>> {
                 { "Fruity Reeverb 2", false }, { "Odd", false }, { "Harmor", true }, { "Harmor", false }, { "Toxic Biohazard", true }, { "Nothing", true }, { "", true } })
            CHECK (natives::hasPresetCandidate (folder, generator, fl) == ! natives::presetCandidates (folder, generator, fl).empty());
        CHECK (natives::hasPresetCandidate ("Fruity Reeverb 2", false, fl));
        CHECK (! natives::hasPresetCandidate ("Odd", false, fl));
        CHECK (natives::hasPresetCandidate ("Harmor", true, fl));
        CHECK (! natives::hasPresetCandidate ("Harmor", false, fl));
        CHECK (natives::hasPresetCandidate ("Toxic Biohazard", true, fl));
        CHECK (! natives::hasPresetCandidate ("Nothing", true, fl));
    }
    // no FL at all
    {
        natives::FlAccess none;
        CHECK (! natives::hasPresetCandidate ("Sytrus", true, none));
    }
}

TEST (q3190ry_available_is_unchanged_on_the_real_install)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    auto fl = natives142::flAccess();
    auto listings = std::make_shared<int> (0);
    auto list = fl.listPresets;
    fl.listPresets = [list, listings] (const std::string& folder) { ++*listings; return list (folder); };
    int same = 0, offered = 0;
    for (const auto& e : natives::instrumentEngines())
    {
        const auto fast = natives::hasPresetCandidate (e.presetFolder, true, fl);
        CHECK (fast == ! natives::presetCandidates (e.presetFolder, true, fl).empty());
        same += 1;
        offered += natives::available (e, fl) ? 1 : 0;
    }
    for (const auto& e : natives::effectEngines())
    {
        const auto fast = natives::hasPresetCandidate (e.presetFolder, false, fl);
        CHECK (fast == ! natives::presetCandidates (e.presetFolder, false, fl).empty());
        same += 1;
        offered += natives::available (e, fl) ? 1 : 0;
    }
    // every engine's availability again, alone: Default presets answer nearly all of them without a listing
    *listings = 0;
    for (const auto& e : natives::instrumentEngines())
        (void) natives::available (e, fl);
    for (const auto& e : natives::effectEngines())
        (void) natives::available (e, fl);
    std::printf ("  q3190ry: %d engines compared, %d offered, %d folder listings for every engine's availability\n", same, offered, *listings);
    CHECK (*listings < 20);
}

TEST (q3190ry_big_project)
{
    using namespace lynx;
    Song song;
    perfprojects::Samples samples;
    CHECK (perfprojects::make ("q3big", song, samples));
    CHECK (song.channels.size() == 120);
    CHECK (song.mixer.size() == 129);
    CHECK (song.patterns.size() == 120 && song.patterns[0].notes.size() == 10000);
    std::size_t points = 0;
    for (const auto& a : song.automations)
        points = std::max (points, a.points.size());
    CHECK (song.automations.size() == 24 && points >= 500);
    // the gate's list stays as it was
    CHECK (std::find (perfprojects::names().begin(), perfprojects::names().end(), "q3big") == perfprojects::names().end());
}

TEST (q3190ry_availability_memo)
{
    using namespace lynx;
    using q3190ry::FakeFl;
    const natives::EffectEngine* engine = nullptr;
    for (const auto& e : natives::effectEngines())
        if (engine == nullptr && e.needs != natives::Needs::nothing && std::string (e.presetFolder).size() > 0)
            engine = &e;
    CHECK (engine != nullptr);
    if (engine == nullptr)
        return;
    FakeFl f;
    f.files = { "/fl/Data/Patches/Plugin presets/Effects/" + std::string (engine->presetFolder) + "/Big hall.fst" };
    auto fl = f.access();
    auto looks = std::make_shared<int> (0);
    auto exists = fl.fileExists;
    fl.fileExists = [exists, looks] (const std::string& p) { ++*looks; return exists (p); };
    // no memo: every call looks again (as before)
    const auto first = natives::available (*engine, fl);
    CHECK (first);
    const auto looked = *looks;
    CHECK (natives::available (*engine, fl) == first && *looks == 2 * looked && looked > 0);
    // with the memo: looked once, then the kept answer (the copies share it)
    fl.availability = std::make_shared<natives::FlAccess::AvailabilityMemo>();
    *looks = 0;
    CHECK (natives::available (*engine, fl) == first);
    const auto once = *looks;
    const auto copy = fl;
    CHECK (natives::available (*engine, copy) == first && *looks == once);
}
