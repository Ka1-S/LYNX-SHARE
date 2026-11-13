// --- Round 190fn: FL Studio Mobile content without FL (decision O1: the user's own "Resources" folders), the "FL Studio
// Mobile Rack" generator of an .flp on the .flm importer's engines (FLM coverage instruments-0022), a short SEND
// (format-0049) and the song's META credits (format-0020…0023). Projects are written by the tests (flm182c's helpers).

#include "core/PocketSuperSaw.h"

namespace flm190fn
{
using namespace flm182c;
using Bytes = flm182c::Bytes; // (other test files have a Bytes of their own)
namespace flm = lynx::flm;

inline Bytes meta (const std::string& created, double editSeconds, const std::string& author, const std::string& notes, const std::string& saved)
{
    Bytes m;
    flm182c::fixed (m, created, 256);
    flm182c::f64 (m, editSeconds);
    flm182c::fixed (m, author, 256);
    flm182c::fixed (m, "New Song (7)", 256);
    flm182c::u8 (m, 0);
    flm182c::fixed (m, notes, 2048);
    flm182c::fixed (m, saved, 256);
    return flm182c::chunk ("META", m);
}

// a one-rack song: the master and one channel whose drum kit plays "@R@" samples
inline Bytes kitSong()
{
    Bytes f;
    flm182c::tag (f, "10LF");
    return flm182c::cat ({ f, flm182c::head ("Kit", 120.0), flm182c::rack (flm::kindMaster, {}),
                  flm182c::rack (flm::kindNormal, {}, { flm182c::sample ("Kick", "@R@", "Drum Samples\\Kick\\Kick 1.wav"), flm182c::sample ("Clap", "@R@", "Drum Samples\\Clap\\Clap 2.wav") }),
                  flm182c::channel (flm::kindMaster, "MASTER", 0, {}),
                  flm182c::channel (flm::kindNormal, "Drums", 1, { flm182c::lane (3, "Drums", { flm182c::clip (0, 4, 4, 0, 1, { { 0, 0.1249, 0 }, { 1, 0.1249, 1 } }) }) }) });
}

// the FL Studio Mobile Rack generator's state: "SSLF" + "10LF" + a short HEAD + one RACK (FLM_FORMAT §8)
inline Bytes rackState (const Bytes& rackChunk)
{
    Bytes body;
    flm182c::tag (body, "10LF");
    Bytes h (8, 0);
    body = flm182c::cat ({ body, flm182c::chunk ("HEAD", h), rackChunk });
    return flm182c::chunk ("SSLF", body);
}

inline Bytes withReserved (Bytes rmod, float postGain, float mix, float wetPan)
{
    // append an SMPR chunk inside the RMOd (Post Gain, Mix, Wet Pan; FLM_FORMAT §3.1)
    Bytes smpr;
    flm182c::f32 (smpr, postGain);
    flm182c::f32 (smpr, mix);
    flm182c::f32 (smpr, wetPan);
    const auto c = flm182c::chunk ("SMPR", smpr);
    flm182c::put (rmod, c.data(), c.size());
    const auto size = (std::uint32_t) (rmod.size() - 8);
    std::memcpy (rmod.data() + 4, &size, 4);
    return rmod;
}

inline ::flp::Project flpWithRack (const Bytes& state, int mixerTrack = 3)
{
    ::flp::Project p;
    p.tempo = 128.0;
    ::flp::Channel c;
    c.index = 0;
    c.type = ::flp::ChannelType::instrument;
    c.name = "Lead";
    c.pluginName = "FL Studio Mobile Rack";
    c.pluginState = state;
    c.mixerTrack = mixerTrack;
    p.channels.push_back (c);
    return p;
}
} // namespace flm190fn

TEST (flm190fn_resources_folder_from_a_pick)
{
    using namespace lynx;
    const std::set<std::string> dirs { "D:/FLM/Resources/Drum Samples", "E:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed/oscdata.dat",
                                       "F:/Copy/Synth Presets2" };
    const auto exists = [&dirs] (const std::string& p) { return dirs.count (p) > 0; };
    // the app's folder itself, the folder above it, an FL install folder, a partial copy, nothing
    CHECK (flmResourceRootFrom ("D:/FLM/Resources", exists) == "D:/FLM/Resources");
    CHECK (flmResourceRootFrom ("D:/FLM", exists) == "D:/FLM/Resources");
    CHECK (flmResourceRootFrom ("E:/FL", exists) == "E:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed");
    CHECK (flmResourceRootFrom ("F:/Copy", exists) == "F:/Copy");
    CHECK (flmResourceRootFrom ("G:/Other", exists) == "G:/Other"); // (kept as picked)
    CHECK (looksLikeFlmResources ("D:/FLM/Resources", exists) && ! looksLikeFlmResources ("D:/FLM", exists));
    CHECK (flmResourceRootFrom ("", exists).empty());
}

TEST (flm190fn_factory_samples_from_the_users_resources_first)
{
    using namespace lynx;
    const auto bytes = flm190fn::kitSong();
    const std::string fl = "C:/FL/Plugins/Fruity/Generators/FL Studio Mobile/Installed", mine = "D:/FLM/Resources";
    std::set<std::string> files { mine + "/Drum Samples/Kick/Kick 1.wav", mine + "/Drum Samples/Clap/Clap 2.wav", fl + "/Drum Samples/Kick/Kick 1.wav" };
    const auto run = [&] (bool withMine, bool withFl, FlpImportReport& report) {
        FlmImportOptions o;
        o.common.projectFolder = "E:/flm/Song";
        o.common.fileExists = [&files] (const std::string& p) { return files.count (p) > 0; };
        o.common.report = &report;
        if (withFl)
            o.resourceRoot = fl;
        if (withMine)
            o.userResourceRoots = { mine };
        return importFlm (bytes, o);
    };
    const auto pathOf = [] (const Song& s, const std::string& name) {
        for (const auto& c : s.channels)
            if (c.name == name)
                return c.samplePath;
        return std::string ("?");
    };
    {
        // FL's copy only: the kick is there, the clap is not (FL ships part of the content) → listed, with the hint
        FlpImportReport report;
        const auto s = run (false, true, report);
        CHECK (s.has_value());
        CHECK (s && pathOf (*s, "Kick") == fl + "/Drum Samples/Kick/Kick 1.wav");
        CHECK (s && pathOf (*s, "Clap").empty());
        CHECK (report.missingSamples == 1);
        CHECK (report.unsupported.count ("FL Studio Mobile factory content not found: add the app's Resources folder in Settings > Files") == 1);
    }
    {
        // the user's own Resources folder first: both found there, nothing listed
        FlpImportReport report;
        const auto s = run (true, true, report);
        CHECK (s && pathOf (*s, "Kick") == mine + "/Drum Samples/Kick/Kick 1.wav");
        CHECK (s && pathOf (*s, "Clap") == mine + "/Drum Samples/Clap/Clap 2.wav");
        CHECK (report.missingSamples == 0);
        CHECK (report.unsupported.empty() || report.unsupported.count ("FL Studio Mobile factory content not found: add the app's Resources folder in Settings > Files") == 0);
    }
    {
        // no FL at all: the user's folder alone does it
        FlpImportReport report;
        const auto s = run (true, false, report);
        CHECK (s && pathOf (*s, "Clap") == mine + "/Drum Samples/Clap/Clap 2.wav");
    }
    {
        // a user folder without the file: listed with the other wording
        files.erase (mine + "/Drum Samples/Clap/Clap 2.wav");
        FlpImportReport report;
        const auto s = run (true, true, report);
        CHECK (s && pathOf (*s, "Clap").empty());
        CHECK (report.unsupported.count ("FL Studio Mobile factory content not found in the Resources folders of Settings > Files") == 1);
    }
}

TEST (flm190fn_short_send_plays_at_its_linear_gain)
{
    using namespace lynx;
    using namespace flm182c;
    using Bytes = flm182c::Bytes;
    // a channel whose SEND to the bus has only words [0]…[2] (FLM rebuilds the level from the linear gain [2])
    Bytes f;
    flm182c::tag (f, "10LF");
    auto ch = flm182c::channel (flm::kindNormal, "Lead", 1, {});
    {
        Bytes w;
        flm182c::u32 (w, 2);         // the bus's channel id
        flm182c::f32 (w, 1.0f);
        flm182c::f32 (w, 0.25f);     // linear gain
        const auto c = flm182c::chunk ("SEND", w);
        flm182c::put (ch, c.data(), c.size());
        const auto size = (std::uint32_t) (ch.size() - 8);
        std::memcpy (ch.data() + 4, &size, 4);
    }
    const auto bytes = flm182c::cat ({ f, flm182c::head ("Send", 120.0), flm182c::rack (flm::kindMaster, {}), flm182c::rack (flm::kindNormal, {}), flm182c::rack (flm::kindBus, {}),
                              flm182c::channel (flm::kindMaster, "MASTER", 0, {}), ch, flm182c::channel (flm::kindBus, "Bus", 2, {}) });
    FlpImportReport report;
    auto o = flm182c::options (&report);
    const auto s = importFlm (bytes, o);
    CHECK (s.has_value());
    if (! s)
        return;
    int lead = -1, bus = -1;
    for (int i = 0; i < (int) s->mixer.size(); ++i)
    {
        if (s->mixer[(std::size_t) i].name == "Lead")
            lead = i;
        else if (s->mixer[(std::size_t) i].name == "Bus")
            bus = i;
    }
    CHECK (lead > 0 && bus > 0);
    bool found = false;
    if (lead > 0)
        for (const auto& snd : s->mixer[(std::size_t) lead].sends)
            if (snd.target == bus)
            {
                found = true;
                CHECK (std::abs (snd.level - 0.25f) < 1.0e-6f);
            }
    CHECK (found);
}

TEST (flm190fn_meta_credits_reach_project_info)
{
    using namespace lynx;
    using namespace flm182c;
    using Bytes = flm182c::Bytes;
    Bytes f;
    flm182c::tag (f, "10LF");
    const auto bytes = flm182c::cat ({ f, flm182c::head ("Credits", 120.0), flm190fn::meta ("2025-07-06 11-39-10", 5400.0, "Kai", "Line one\nLine two", "2026-11-08 02:09:36"),
                              flm182c::rack (flm::kindMaster, {}), flm182c::channel (flm::kindMaster, "MASTER", 0, {}) });
    const auto p = flm::parse ({ bytes.data(), bytes.size() });
    CHECK (p && p->author == "Kai" && p->notes == "Line one\nLine two" && std::abs (p->editSeconds - 5400.0) < 1e-9);
    FlpImportReport report;
    const auto s = importFlm (bytes, flm182c::options (&report));
    CHECK (s && s->info.author == "Kai");
    CHECK (s && s->info.comments == "Line one\nLine two");
    // the creation date as written on the device ("YYYY-MM-DD HH-MM-SS"; no time zone in the file: read as UTC)
    CHECK (s && std::abs (s->info.created - 1751801950.0) < 0.5);
    CHECK (s && std::abs (s->info.workSeconds - 5400.0) < 1e-6);
    // no META, or empty fields: nothing set
    const auto bare = flm182c::cat ({ f, flm182c::head ("Bare", 120.0), flm182c::rack (flm::kindMaster, {}), flm182c::channel (flm::kindMaster, "MASTER", 0, {}) });
    const auto t = importFlm (bare, flm182c::options (&report));
    CHECK (t && t->info.author.empty() && t->info.comments.empty() && t->info.created == 0.0 && t->info.workSeconds == 0.0);
}

TEST (flm190fn_flp_mobile_rack_plays_on_the_flm_engine)
{
    using namespace lynx;
    using namespace flm182c;
    using Bytes = flm182c::Bytes;
    // a SuperSaw (type 23) at Post Gain −6 dB-ish and Wet Pan right, then a Filter (type 9): FLM's rack in an .flp
    auto saw = flm190fn::withReserved (flm182c::module (23, 2, {}, "Default/Default", 0, 127, pocketsaw::flmParamCount), 0.35f, 1.0f, 0.75f);
    const auto state = flm190fn::rackState (flm182c::rack (flm::kindNormal, { saw, flm182c::module (9, 3, {}, "Default/Default", 0, 127, 5) }));
    FlpImportReport report;
    FlpImportOptions o;
    o.report = &report;
    o.fileExists = [] (const std::string&) { return false; };
    const auto s = importFlp (flm190fn::flpWithRack (state), o);
    CHECK (s.channels.size() == 1);
    if (s.channels.empty())
        return;
    const auto& c = s.channels.front();
    CHECK (c.instrument == InstrumentKind::pocketSuperSaw);
    CHECK (c.name == "Lead");
    CHECK (report.converted.count ("FL Studio Mobile Rack") == 1 && report.converted.count ("FL Studio Mobile: SuperSaw") == 1);
    // the wrapper: Post Gain 0.35 → 40·0.35 − 20 = −6 dB on FL's default channel volume; Wet Pan 0.75 → +0.5 pan
    const auto expected = channelGain (0.78f) * std::pow (10.0f, -6.0f / 20.0f);
    CHECK (std::abs (channelGain (c.volume) - expected) < 1.0e-4f);
    CHECK (std::abs (c.pan - 0.5f) < 1.0e-6f);
    // the rack's level (0.9 → +1.3 dB on FLM's fader law) and its Filter on an insert of their own, sending to FL's insert 3
    CHECK (c.mixerTrack > 3 && c.mixerTrack < (int) s.mixer.size());
    if (c.mixerTrack <= 3 || c.mixerTrack >= (int) s.mixer.size())
        return;
    const auto& m = s.mixer[(std::size_t) c.mixerTrack];
    CHECK (m.name == "Lead (FL Studio Mobile Rack)");
    CHECK (! m.toMaster && s.routes (c.mixerTrack, 3));
    CHECK (! m.effects.empty());
    const double gain = faderGain (m.volume);
    const double law = std::pow (10.0, 3.3 * std::pow (0.9, 0.6) - 3.0);
    CHECK (std::abs (gain - law) < 1.0e-4);
}

TEST (flm190fn_flp_mobile_rack_left_as_stand_in_when_not_rebuilt)
{
    using namespace lynx;
    using namespace flm182c;
    using Bytes = flm182c::Bytes;
    const auto check = [] (const Bytes& state, const std::string& line) {
        FlpImportReport report;
        FlpImportOptions o;
        o.report = &report;
        o.fileExists = [] (const std::string&) { return false; };
        const auto s = importFlp (flm190fn::flpWithRack (state), o);
        CHECK (s.channels.size() == 1 && s.channels.front().instrument == InstrumentKind::synth);
        CHECK (s.channels.size() == 1 && s.channels.front().mixerTrack == 3);
        CHECK (report.unsupported.count (line + "; played with a LYNX SHARE synth preset") == 1);
        CHECK (report.converted.empty());
    };
    // a drum kit, two instruments, an effect first, an active note effect, a state that is not a rack
    check (flm190fn::rackState (flm182c::rack (flm::kindNormal, {}, { flm182c::sample ("Kick", "@R@", "Drum Samples\\Kick\\Kick 1.wav") })), "FL Studio Mobile Rack (drum kit: not reproduced yet)");
    check (flm190fn::rackState (flm182c::rack (flm::kindNormal, { flm182c::module (23, 2, {}, "Default/Default", 0, 127, pocketsaw::flmParamCount),
                                                         flm182c::module (23, 3, {}, "Default/Default", 0, 127, pocketsaw::flmParamCount) })),
           "FL Studio Mobile Rack (several instruments: not reproduced yet)");
    check (flm190fn::rackState (flm182c::rack (flm::kindNormal, { flm182c::module (9, 3, {}, "Default/Default", 0, 127, 5),
                                                         flm182c::module (23, 2, {}, "Default/Default", 0, 127, pocketsaw::flmParamCount) })),
           "FL Studio Mobile Rack (an effect before the instrument: not reproduced yet)");
    check (flm190fn::rackState (flm182c::rack (flm::kindNormal, { flm182c::module (48, 4, {}, "Default/Default", 0, 127, 3),
                                                         flm182c::module (23, 2, {}, "Default/Default", 0, 127, pocketsaw::flmParamCount) })),
           "FL Studio Mobile Rack (note effects: not reproduced yet)");
    check (Bytes { 'n', 'o', 'p', 'e', 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 }, "FL Studio Mobile Rack (state not readable)");
}

TEST (flm190fn_newer_app_version_is_named)
{
    using namespace lynx;
    using namespace flm182c;
    using Bytes = flm182c::Bytes;
    const auto song = [] (const std::string& version) {
        Bytes h;
        flm182c::u32 (h, 0);
        flm182c::u32 (h, 0);
        flm182c::fixed (h, "V", 256);
        flm182c::f64 (h, 120.0);
        for (int k = 0; k < 7; ++k)
            flm182c::f64 (h, 0.0);
        h.resize (h.size() + 16, 0);
        flm182c::u32 (h, 0);
        flm182c::u32 (h, 0);
        flm182c::u8 (h, 0);
        flm182c::u8 (h, 0);
        flm182c::u32 (h, 2);
        flm182c::lp32 (h, version);
        Bytes f;
        flm182c::tag (f, "10LF");
        return flm182c::cat ({ f, flm182c::chunk ("HEAD", h), flm182c::rack (flm::kindMaster, {}), flm182c::channel (flm::kindMaster, "MASTER", 0, {}) });
    };
    const auto lines = [&song] (const std::string& version) {
        FlpImportReport report;
        const auto s = importFlm (song (version), flm182c::options (&report));
        CHECK (s.has_value());
        return report.unsupported.count ("FL Studio Mobile " + version + " (newer than this import knows: new settings may be missing)");
    };
    CHECK (lines ("5.1.0") == 1);
    CHECK (lines ("6.0") == 1);
    CHECK (lines ("5.0.4.2") == 1);
    CHECK (lines ("5.0.4") == 0);
    CHECK (lines ("4.8.3") == 0);
    CHECK (lines ("beta") == 0);
}

TEST (flm190fn_effect_enable_lane_switches_the_slots)
{
    using namespace lynx;
    using namespace flm182c;
    using Bytes = flm182c::Bytes;
    // a Filter (type 9) with an Enable lane: on (65535) at beat 0, off (0) at beat 2, on at beat 4
    Bytes f;
    flm182c::tag (f, "10LF");
    auto filter = flm182c::module (9, 3, {}, "Default/Default", 0, 127, 5);
    const auto bytes = flm182c::cat ({ f, flm182c::head ("Enable", 120.0), flm182c::rack (flm::kindMaster, {}),
                              flm182c::rack (flm::kindNormal, { flm182c::module (23, 2, {}, "Default/Default", 0, 127, pocketsaw::flmParamCount), filter }),
                              flm182c::channel (flm::kindMaster, "MASTER", 0, {}),
                              flm182c::channel (flm::kindNormal, "Pad", 1,
                                       { flm182c::lane (1, "Enable : FX Filter",
                                               { flm182c::clip (0, 8, 8, 0, 1, { { 0, 0, -1, 200, 127, 0xFFFF }, { 2, 0, -1, 200, 127, 0 }, { 4, 0, -1, 200, 127, 0xFFFF } }) },
                                               3, 0) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm182c::options (&report));
    CHECK (s.has_value());
    if (! s || s->mixer.size() < 2)
        return;
    const auto& fx = s->mixer[1].effects;
    CHECK (fx.size() == 1 && fx[0].kind == EffectKind::pocketFilter && fx[0].enabled);
    int lanes = 0;
    for (const auto& a : s->automations)
        if (a.target.kind == AutomationTargetKind::effectMix && a.target.index == 1 && a.target.slot == 0)
        {
            ++lanes;
            CHECK (a.points.size() == 3);
            if (a.points.size() == 3)
                CHECK (std::abs (a.points[0].value - fx[0].mix) < 1e-6f && a.points[1].value == 0.0f && std::abs (a.points[2].value - fx[0].mix) < 1e-6f);
        }
    CHECK (lanes == 1);
    CHECK (report.converted.count ("FL Studio Mobile: effect automation") == 1);
    CHECK (report.unsupported.count ("FL Studio Mobile: automation of an effect switch or layout (not imported)") == 0);
}

TEST (flm190fn_flp_mobile_rack_fx_on_the_flm_engines)
{
    using namespace lynx;
    using namespace flm182c;
    using Bytes = flm182c::Bytes;
    // a Rack FX: Filter (9) → Equalizer (3) → a Tuner (24, sound-neutral) → a SuperSaw (an instrument: an effect rack
    // plays no notes); the rack at level 0.9 and pan 0.75 (RPRM as written by rack(): level 0.9, pan 0.5 → patched)
    auto r = flm182c::rack (flm::kindNormal, { flm182c::module (9, 3, {}, "Default/Default", 0, 127, 5), flm182c::module (3, 4, {}, "Default/Default", 0, 127, 12),
                                      flm182c::module (24, 5, {}, "Default/Default", 0, 127, 2),
                                      flm182c::module (23, 6, {}, "Default/Default", 0, 127, pocketsaw::flmParamCount) });
    {
        // RPRM sits after the RACK header (8), "RACK"+size (8) and RHED (8 + 16): its [1] (pan) at +8 + 8 + 24 + 8 + 4
        const float pan = 0.75f;
        std::memcpy (r.data() + 8 + 8 + 24 + 8 + 4, &pan, 4);
    }
    const auto state = flm190fn::rackState (r);
    flp::Project p;
    p.tempo = 120.0;
    flp::MixerInsert m;
    m.index = 2;
    m.slots.push_back ({ 0, "FL Studio Mobile Rack FX", state, -1 });
    p.mixer.push_back (m);
    FlpImportReport report;
    FlpImportOptions o;
    o.report = &report;
    const auto s = importFlp (p, o);
    CHECK (s.mixer.size() > 2);
    if (s.mixer.size() <= 2)
        return;
    const auto& fx = s.mixer[2].effects;
    CHECK (fx.size() == 1);
    if (fx.size() != 1)
        return;
    CHECK (fx[0].kind == EffectKind::parallel && fx[0].branches.size() == 1 && fx[0].enabled);
    if (fx[0].branches.size() != 1)
        return;
    const auto& b = fx[0].branches[0];
    CHECK (b.chain.size() == 3);
    if (b.chain.size() == 3)
    {
        CHECK (b.chain[0].kind == EffectKind::pocketFilter && b.chain[1].kind == EffectKind::pocketEq && b.chain[2].kind == EffectKind::utility);
        CHECK (std::abs (b.chain[2].params[1] - 0.75f) < 1e-6f && std::abs (b.chain[2].params[0] - 0.5f) < 1e-6f);
    }
    CHECK (std::abs (b.gain - (float) std::pow (10.0, 3.3 * std::pow (0.9, 0.6) - 3.0)) < 1e-5f);
    CHECK (report.converted.count ("FL Studio Mobile Rack FX") == 1 && report.converted.count ("FL Studio Mobile: Filter") == 1
           && report.converted.count ("FL Studio Mobile: Equalizer") == 1);
    CHECK (report.droppedEffects.count ("FL Studio Mobile Rack FX: SuperSaw (an effect rack plays no notes)") == 1);
    CHECK (report.droppedEffects.count ("FL Studio Mobile Rack FX (its modules are not reproduced; the slot is left out)") == 0);
    // FL's own default Rack FX state shape: an empty rack at 0 dB passes the sound, no slot
    FlpImportReport empty;
    const auto e = flmRackFxSlot (flm190fn::rackState (flm182c::rack (flm::kindNormal, {})), &empty);
    // (rack() writes level 0.9: not 0 dB, so a gain-only container)
    CHECK (e && e->kind == EffectKind::parallel && e->branches.size() == 1 && e->branches[0].chain.empty());
    CHECK (! flmRackFxSlot ({ 1, 2, 3 }, &empty));
    CHECK (empty.droppedEffects.count ("FL Studio Mobile Rack FX (state not readable)") == 1);
}
