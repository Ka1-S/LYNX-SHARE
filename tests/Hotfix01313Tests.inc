// Hotfix 0.1.31.3 (round 190rj, regression sweep 2 of 0.1.32): Online Sessions and the PipeWire channel order. Kept in a
// file of its own so the same commits apply to main (0.1.31.x) and to the 0.1.32 rollup. Needs Live169bTests.inc above.
#include "core/PipeWireOrder.h"

// Online Sessions: each copy dates itself (Project info › Created) when it is saved. The date was part of the shared
// project settings, so a guest whose copy had another date than the host's kept his own settings against the host's: a
// tempo change of the host never arrived. exportMarkers (0.1.31) was compared and never copied, with the same effect.
TEST (hotfix01313_session_created_date_keeps_no_settings_back)
{
    using namespace lynx;
    auto host = live169bSong();
    host.info.created = 0.0; // (a Live set just imported)
    live::Authority authority (host, {});
    live::Replica hostCopy (host, 0, session::hostTag, {}), guestCopy (host, 0, 2, {});
    auto guest = host;
    guest.info.created = 1.7e9; // (the guest saved his copy)
    host.info.created = 1.6e9;
    host.bpm = 151.0;
    const auto proposal = hostCopy.poll (host);
    CHECK (proposal.has_value());
    if (! proposal)
        return;
    const auto commit = authority.propose (session::hostTag, *proposal);
    const auto update = guestCopy.onCommit (commit, guest);
    CHECK (update.working.has_value());
    if (update.working)
    {
        CHECK (update.working->bpm == 151.0);
        CHECK (update.working->info.created == 1.7e9);
    }
    // dating a copy is no edit of the project
    auto dated = host;
    dated.info.created = 1.0;
    CHECK (live::diff (host, dated).empty());
    // a fresh copy takes the host's date
    Song fresh;
    live::apply (fresh, live::diff (Song(), host));
    CHECK (fresh.bpm == 151.0);
}

TEST (hotfix01313_session_settings_that_are_compared_arrive)
{
    using namespace lynx;
    const auto a = live169bSong();
    auto b = a;
    b.exportMarkers = ! b.exportMarkers;
    auto c = a;
    live::apply (c, live::diff (a, b));
    CHECK (c.exportMarkers == b.exportMarkers);
    CHECK (live::diff (c, b).empty());
    // every switch of the shared settings at once: what differs arrives, and nothing differs afterwards
    b.slideNoteProps = ! b.slideNoteProps;
    b.playTruncatedNotes = ! b.playTruncatedNotes;
    b.muteRunsEffects = ! b.muteRunsEffects;
    b.patternClipTakeover = ! b.patternClipTakeover;
    b.mutedInsertsSkip = ! b.mutedInsertsSkip;
    b.fastDeclick = ! b.fastDeclick;
    b.panTriangular = ! b.panTriangular;
    b.flPluginLevels = ! b.flPluginLevels;
    b.autoPdc = ! b.autoPdc;
    b.compensateAutomations = ! b.compensateAutomations;
    b.compensateMixerLevels = ! b.compensateMixerLevels;
    b.performanceMode = ! b.performanceMode;
    b.substitutesBypassed = ! b.substitutesBypassed;
    b.cleanMixer = ! b.cleanMixer;
    b.autoCrossfades = ! b.autoCrossfades;
    b.linkTracksToMixer = ! b.linkTracksToMixer;
    b.patternBlocksOn = ! b.patternBlocksOn;
    b.compCrossfadeMs += 5.0f;
    b.masterPitch += 1.0f;
    b.swing = 0.3f;
    b.info.title = "T";
    b.dataFolder = "/x";
    c = a;
    live::apply (c, live::diff (a, b));
    CHECK (live::diff (c, b).empty());
}

// The bug class of the two tests above: a field the shared settings compare (sameGlobals) but do not copy (copyGlobals),
// or the other way round, never converges. Both lists must name the same fields.
TEST (hotfix01313_session_settings_compare_and_copy_the_same_fields)
{
    const auto src = i18ntest::readFile ("src/core/LiveSession.cpp");
    const auto same = src.find ("bool sameGlobals (const Song& a, const Song& b)");
    const auto copy = src.find ("void copyGlobals (const Song& from, Song& to)");
    CHECK (same != std::string::npos && copy != std::string::npos && same < copy);
    if (same == std::string::npos || copy == std::string::npos)
        return;
    const auto sameBody = src.substr (same, copy - same);
    const auto copyBody = src.substr (copy, src.find ("\n}\n", copy) - copy);
    std::set<std::string> compared, copied;
    for (std::size_t at = 0; (at = sameBody.find ("a.", at)) != std::string::npos; ++at)
    {
        auto end = sameBody.find_first_not_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", at + 2);
        if (sameBody.compare (end, 6, " == b.") == 0)
            compared.insert (sameBody.substr (at + 2, end - at - 2));
    }
    for (std::size_t at = 0; (at = copyBody.find ("to.", at)) != std::string::npos; ++at)
    {
        auto end = copyBody.find_first_not_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", at + 3);
        if (copyBody.compare (end, 8, " = from.") == 0)
            copied.insert (copyBody.substr (at + 3, end - at - 3));
    }
    CHECK (compared.size() >= 30);
    CHECK (compared == copied);
    for (const auto& f : compared)
        if (copied.count (f) == 0)
            std::printf ("  sameGlobals compares %s, copyGlobals does not copy it\n", f.c_str());
    for (const auto& f : copied)
        if (compared.count (f) == 0)
            std::printf ("  copyGlobals copies %s, sameGlobals does not compare it\n", f.c_str());
}

// The song-level things a session does not share yet were rebuilt from the shared state on every commit: a change of
// any of them was lost at the next edit anyone made (round 190rj). One check per item: a guest's own change survives a
// host's commit; and what the guest got at join stays after the host's edits.
namespace
{
void hotfix01313KeptLocal (const char* what, const std::function<void (lynx::Song&)>& change,
                          const std::function<bool (const lynx::Song&, const lynx::Song&)>& same)
{
    using namespace lynx;
    auto base = live169bSong();
    live::Authority authority (base, {});
    live::Replica hostCopy (base, 0, session::hostTag, {}), guestCopy (base, 0, 2, {});
    auto host = base;
    host.patterns[0].name = "Renamed by the host";
    const auto proposal = hostCopy.poll (host);
    CHECK (proposal.has_value());
    if (! proposal)
        return;
    const auto commit = authority.propose (session::hostTag, *proposal);
    auto guest = base;
    change (guest);
    const auto update = guestCopy.onCommit (commit, guest);
    CHECK (update.working.has_value());
    if (! update.working)
        return;
    const auto& now = *update.working;
    if (! same (now, guest))
        std::printf ("  %s: the guest's own change was put back by the host's commit\n", what);
    CHECK (same (now, guest));
    CHECK (now.patterns[0].name == "Renamed by the host");
    CHECK (live::keptLocalChanges (now, guest).empty());
}
} // namespace

TEST (hotfix01313_session_keeps_local_vcas)
{
    hotfix01313KeptLocal ("VCAs", [] (lynx::Song& s) { s.vcas.emplace_back().name = "VCA 1"; s.vcas.back().level = 0.5f; },
                         [] (const lynx::Song& a, const lynx::Song& b) { return a.vcas == b.vcas; });
}
TEST (hotfix01313_session_keeps_local_main_volume)
{
    hotfix01313KeptLocal ("main volume", [] (lynx::Song& s) { s.mainVolume = 0.5f; },
                         [] (const lynx::Song& a, const lynx::Song& b) { return a.mainVolume == b.mainVolume; });
}
TEST (hotfix01313_session_keeps_local_modulators)
{
    hotfix01313KeptLocal ("modulators", [] (lynx::Song& s) { s.modulators.emplace_back().name = "LFO 1"; },
                         [] (const lynx::Song& a, const lynx::Song& b) { return a.modulators == b.modulators; });
}
TEST (hotfix01313_session_keeps_local_clip_grid)
{
    hotfix01313KeptLocal ("clip grid", [] (lynx::Song& s) { s.clipGrid.emplace(); s.clipGrid->scenes.emplace_back().name = "S1"; },
                         [] (const lynx::Song& a, const lynx::Song& b) { return a.clipGrid == b.clipGrid; });
}
TEST (hotfix01313_session_keeps_local_grooves)
{
    hotfix01313KeptLocal ("grooves", [] (lynx::Song& s) { s.grooves.amount = 0.5f; },
                         [] (const lynx::Song& a, const lynx::Song& b) { return a.grooves == b.grooves; });
}
TEST (hotfix01313_session_keeps_local_mix_snapshots)
{
    hotfix01313KeptLocal ("mix snapshots", [] (lynx::Song& s) { s.mixSnapshots.emplace_back().name = "Mix A"; s.mixSnapshotCurrent = 0; },
                         [] (const lynx::Song& a, const lynx::Song& b) { return a.mixSnapshots == b.mixSnapshots && a.mixSnapshotCurrent == b.mixSnapshotCurrent; });
}
TEST (hotfix01313_session_keeps_local_master_look)
{
    hotfix01313KeptLocal ("Master look", [] (lynx::Song& s) { s.masterLook.intensity = 0.25f; },
                         [] (const lynx::Song& a, const lynx::Song& b) { return a.masterLook == b.masterLook; });
}

TEST (hotfix01313_session_join_values_stay)
{
    using namespace lynx;
    auto base = live169bSong();
    base.vcas.emplace_back().name = "From the host";
    base.mainVolume = 0.7f;
    live::Authority authority (base, {});
    live::Replica hostCopy (base, 0, session::hostTag, {}), guestCopy (base, 0, 2, {}); // (the guest's copy: the join snapshot)
    auto host = base;
    host.bpm = 150.0;
    const auto proposal = hostCopy.poll (host);
    CHECK (proposal.has_value());
    if (! proposal)
        return;
    const auto update = guestCopy.onCommit (authority.propose (session::hostTag, *proposal), base);
    CHECK (update.working.has_value());
    if (update.working)
        CHECK (update.working->bpm == 150.0 && update.working->vcas == base.vcas && update.working->mainVolume == 0.7f);
    // the join snapshot carries the host's current values, not the session start's
    const auto src = i18ntest::readFile ("src/engine/LiveSessions.cpp");
    const auto snap = src.find ("void LiveSessionManager::sendSnapshot (int to, bool resumed)");
    CHECK (snap != std::string::npos && src.find ("live::copyKeptLocal (engine.getSong(), song);", snap) < src.find ("\n}\n", snap));
}
// PipeWire: a device's channels came in the order of the registry's ids, which PipeWire hands out as it likes. A stereo
// sink whose FR port had a lower id than FL played LYNX's left channel on the right (released 0.1.31.2, 2 / 2 runs); a
// 16-channel source recorded In 1 = AUX8... The node's own port order (port.id) decides.
TEST (hotfix01313_pipewire_channels_in_port_order)
{
    using namespace lynx;
    std::vector<pwchannels::Port> stereo { { 1, 139, "FR" }, { 0, 190, "FL" } };
    auto byId = stereo; // (the order before the fix)
    std::sort (byId.begin(), byId.end(), [] (const auto& a, const auto& b) { return a.id < b.id; });
    CHECK (byId.front().channel == "FR");
    CHECK ((pwchannels::inPortOrder (stereo) == std::vector<std::string> { "FL", "FR" }));
    // the 16-channel source of the sweep: ids out of order, ports 0..15
    const std::uint32_t ids[16] { 210, 220, 160, 150, 170, 200, 145, 190, 90, 95, 230, 215, 140, 205, 180, 100 };
    std::vector<pwchannels::Port> aux;
    for (int i = 0; i < 16; ++i)
        aux.push_back ({ i, ids[i], "AUX" + std::to_string (i) });
    std::reverse (aux.begin(), aux.end());
    const auto order = pwchannels::inPortOrder (aux);
    CHECK (order.size() == 16);
    for (int i = 0; i < 16 && i < (int) order.size(); ++i)
        CHECK (order[(std::size_t) i] == "AUX" + std::to_string (i));
    // a port without port.id keeps the registry's order among such ports
    CHECK ((pwchannels::inPortOrder ({ { -1, 9, "B" }, { -1, 3, "A" } }) == std::vector<std::string> { "A", "B" }));
    // the device uses it for both directions
    const auto src = i18ntest::readFile ("src/engine/PipeWireDevice.cpp");
    CHECK (src.find ("return pwchannels::inPortOrder (std::move (found));") != std::string::npos);
    CHECK (src.find ("lookup (props, \"port.id\")") != std::string::npos);
}
