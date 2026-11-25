// Round 190rj (0.1.32 regression sweep 2): one test per defect found and fixed.

// Online Sessions: each copy dates itself (Project info › Created) when it is saved. The date was part of the shared
// project settings, so a guest whose copy had another date than the host's kept his own settings against the host's: a
// tempo change of the host never arrived. exportMarkers (0.1.31) was compared and never copied, with the same effect.
TEST (sweep190rj_session_created_date_keeps_no_settings_back)
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

TEST (sweep190rj_session_settings_that_are_compared_arrive)
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
    b.flTrialSaved = ! b.flTrialSaved;
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
TEST (sweep190rj_session_settings_compare_and_copy_the_same_fields)
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
