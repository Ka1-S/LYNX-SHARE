// Round 169j: Online Sessions phase 2, next slice (docs/design/ONLINE_SESSIONS.md §1.5, §2.3, §4.2, §9 "P2 as built"):
// the host's replay log, reconnect / resume without losing pending edits (replay or a fresh snapshot, a host that
// restarted from its journal behind its guests), permissions changed during the session, the host's identity when it
// comes back, chat text, following the host's transport and which parts need live stand-ins. No network: the
// in-process network of round 169b (Live169bNet) with links that drop.
#include "core/LiveCrypto.h"
#include "core/LiveSession.h"
#include "core/SessionIds.h"
#include "core/SessionParts.h"

namespace
{
// Live169bNet with links that drop: what is in flight on a dropped link is lost; a dropped guest keeps editing (its
// edits wait, like LiveSessionManager's, which does not poll while it has no host) and resumes like the manager does
// — the host's replay when it still has the commits, else a fresh snapshot.
struct Live169jNet : Live169bNet
{
    std::vector<bool> online;
    int replays = 0, snapshots = 0;
    std::uint64_t epochs = 0;

    Live169jNet (lynx::Song song, int guestCount, lynx::live::Codec c = {}) : Live169bNet (std::move (song), guestCount, std::move (c))
    {
        online.assign (guests.size(), true);
    }

    void drop (int g)
    {
        online[(std::size_t) g] = false;
        guests[(std::size_t) g].inbox.clear();
        guests[(std::size_t) g].outbox.clear();
    }

    void reconnect (int g)
    {
        auto& p = guests[(std::size_t) g];
        online[(std::size_t) g] = true;
        if (auto missed = authority->since (p.replica->revision(), p.replica->epoch))
        {
            ++replays;
            for (const auto& c : *missed)
                deliver (p, c);
            p.replica->resend();
        }
        else
        {
            ++snapshots;
            const bool lost = authority->lostCommitsOf (p.replica->epoch, p.replica->revision());
            p.working = p.replica->resume (authority->confirmed(), authority->revision(), p.working, lost || p.replica->revision() > authority->revision());
            p.replica->epoch = authority->epoch();
        }
    }

    // the host's LYNX restarts from its journal (`journal`: the confirmed song, revision and conflict record it last
    // wrote): every link drops, the host's own unconfirmed edits are gone, the replay log too
    void restartHost (const lynx::Song& journal, std::uint64_t rev, const std::map<lynx::live::Key, lynx::live::Authority::Change>& changes)
    {
        for (int g = 0; g < (int) guests.size(); ++g)
            drop (g);
        authority = std::make_unique<lynx::live::Authority> (journal, codec, rev, ++epochs);
        authority->restoreChanges (changes);
        host.replica = std::make_unique<lynx::live::Replica> (journal, rev, 1, codec);
        host.working = journal;
    }

    void pollOnline (int who)
    {
        if (who == 0 || online[(std::size_t) who - 1])
            poll (who);
    }

    void settleAll (std::mt19937& rng)
    {
        for (int g = 0; g < (int) guests.size(); ++g)
            if (! online[(std::size_t) g])
                reconnect (g);
        settle (rng);
    }
};

std::uint64_t live169jNewPattern (lynx::Song& s, const std::string& name)
{
    s.patterns.emplace_back().name = name;
    live169bPublish (s);
    return s.patterns.back().uid;
}

bool live169jHasPattern (const lynx::Song& s, std::uint64_t uid)
{
    return std::any_of (s.patterns.begin(), s.patterns.end(), [&] (const lynx::Pattern& p) { return p.uid == uid; });
}
} // namespace

TEST (live169j_replay_log_since)
{
    using namespace lynx;
    auto song = live169bSong();
    live::Authority a (song, {});
    CHECK (a.since (0).has_value() && a.since (0)->empty());
    CHECK (! a.since (1).has_value()); // (a revision this host never made)
    auto w = song;
    for (int i = 0; i < 10; ++i)
    {
        w.bpm = 100.0 + i;
        live::Proposal p;
        p.seq = (std::uint32_t) i + 1;
        p.baseRev = a.revision();
        p.patch = live::diff (a.confirmed(), w);
        a.propose (1, p);
    }
    CHECK (a.revision() == 10);
    const auto from4 = a.since (4);
    CHECK (from4.has_value() && from4->size() == 6 && from4->front().rev == 5 && from4->back().rev == 10);
    // an empty commit (everything refused) is not a revision and is not kept
    live::Proposal refused;
    refused.seq = 1;
    auto other = a.confirmed();
    other.bpm = 50.0;
    refused.patch = live::diff (a.confirmed(), other);
    const auto c = a.propose (2, refused);
    CHECK (c.patch.empty() && a.revision() == 10 && a.since (9)->size() == 1);
    // only the last replayLimit are kept: older peers get a snapshot
    for (std::size_t i = 0; i < live::replayLimit + 5; ++i)
    {
        w.bpm = 60.0 + (double) (i % 100) + 0.5 * (double) (i / 100);
        live::Proposal p;
        p.seq = (std::uint32_t) i + 20;
        p.baseRev = a.revision();
        p.patch = live::diff (a.confirmed(), w);
        a.propose (1, p);
    }
    CHECK (! a.since (4).has_value() && a.since (a.revision() - live::replayLimit).has_value()
           && ! a.since (a.revision() - live::replayLimit - 1).has_value());
    // a host restarted at revision 30 goes on from there
    live::Authority again (a.confirmed(), {}, 30);
    CHECK (again.revision() == 30 && again.since (30)->empty() && ! again.since (29).has_value());
}

TEST (live169j_resume_by_replay_keeps_pending_edits)
{
    using namespace lynx;
    std::mt19937 rng (7);
    Live169jNet net (live169bSong(), 2);
    auto& ana = net.guests[0];
    // Ana makes a pattern; the proposal is lost with the connection, and so is the host's commit of another one
    const auto first = live169jNewPattern (ana.working, "Ana A");
    net.poll (1);
    net.drop (0);
    CHECK (ana.replica->pending() == 1);
    const auto second = live169jNewPattern (ana.working, "Ana B");
    net.poll (1); // (lost too: the link is down)
    net.guests[0].outbox.clear();
    // meanwhile the host and Pedro go on
    net.host.working.bpm = 133.0;
    net.poll (0);
    const auto pedro = live169jNewPattern (net.guests[1].working, "Pedro");
    net.poll (2);
    net.settle (rng);
    CHECK (live169jHasPattern (net.authority->confirmed(), pedro) && ! live169jHasPattern (net.authority->confirmed(), first));
    // offline, she edits on
    const auto third = live169jNewPattern (ana.working, "Ana C");
    // back: the host replays what she missed, her unconfirmed edits go again
    net.reconnect (0);
    CHECK (net.replays == 1 && net.snapshots == 0 && ana.replica->revision() == net.authority->revision());
    net.settle (rng);
    const auto& truth = net.authority->confirmed();
    CHECK (live169jHasPattern (truth, first) && live169jHasPattern (truth, second) && live169jHasPattern (truth, third) && live169jHasPattern (truth, pedro));
    CHECK (truth.bpm == 133.0 && live169bSame (ana.working, truth) && live169bSame (net.guests[1].working, truth) && ana.replica->pending() == 0);
}

TEST (live169j_resume_conflict_judged_on_the_old_base)
{
    using namespace lynx;
    std::mt19937 rng (3);
    Live169jNet net (live169bSong(), 1);
    // "Edit everything" for Ana: she renames the host's pattern; the proposal is lost; the host renames it too
    live::Permissions all;
    all.editEverything = true;
    net.authority->setPermissions (2, all);
    auto& ana = net.guests[0];
    ana.working.patterns[0].name = "Ana's name";
    live169bPublish (ana.working);
    net.poll (1);
    net.drop (0);
    net.host.working.patterns[0].name = "Host's name";
    live169bPublish (net.host.working);
    net.poll (0);
    // she comes back: her rename is judged against the revision she had made it on — the host's came first and stays
    net.reconnect (0);
    net.settle (rng);
    CHECK (net.authority->confirmed().patterns[0].name == "Host's name" && ana.working.patterns[0].name == "Host's name");
    CHECK (net.rejected >= 1);
}

TEST (live169j_resume_by_snapshot_and_host_restart)
{
    using namespace lynx;
    std::mt19937 rng (11);
    Live169jNet net (live169bSong(), 2);
    auto& ana = net.guests[0];
    // the host keeps only 4096 commits: Ana, away for longer, gets a snapshot; her offline edits stay
    net.drop (0);
    const auto offline = live169jNewPattern (ana.working, "Ana offline");
    for (std::size_t i = 0; i < live::replayLimit + 2; ++i)
    {
        net.host.working.bpm = 90.0 + (double) (i % 50) + 0.25 * (double) (i / 50);
        live169bPublish (net.host.working);
        net.poll (0);
    }
    net.reconnect (0);
    CHECK (net.snapshots == 1 && ana.replica->revision() == net.authority->revision() && live169jHasPattern (ana.working, offline));
    net.settle (rng);
    CHECK (live169jHasPattern (net.authority->confirmed(), offline) && live169bSame (ana.working, net.authority->confirmed()));

    // the host's journal is written; then a few more commits — Ana's pattern among them — and the host crashes
    const auto journal = net.authority->confirmed();
    const auto journalRev = net.authority->revision();
    const auto journalChanges = net.authority->changes();
    const auto lost = live169jNewPattern (ana.working, "Ana after the journal");
    net.poll (1);
    const auto pedroLost = live169jNewPattern (net.guests[1].working, "Pedro after the journal");
    net.poll (2);
    net.settle (rng);
    CHECK (net.authority->revision() > journalRev && live169jHasPattern (net.guests[1].working, lost));
    net.restartHost (journal, journalRev, journalChanges);
    // while the host is away Ana edits on
    const auto waiting = live169jNewPattern (ana.working, "Ana while the host restarts");
    net.settleAll (rng);
    const auto& truth = net.authority->confirmed();
    // both guests were ahead of the restarted host: snapshots; each one's own area comes back from their copies
    CHECK (net.snapshots == 3);
    CHECK (live169jHasPattern (truth, lost) && live169jHasPattern (truth, waiting) && live169jHasPattern (truth, pedroLost));
    CHECK (live169bSame (ana.working, truth) && live169bSame (net.guests[1].working, truth) && live169bSame (net.host.working, truth));
}

TEST (live169j_replicas_converge_over_links_that_drop)
{
    using namespace lynx;
    int sessions = 0, converged = 0, replays = 0, snapshots = 0, restarts = 0, markersKept = 0, markers = 0;
    for (unsigned seed = 1; seed <= 24; ++seed)
    {
        std::mt19937 rng (seed * 7919u);
        Live169jNet net (live169bSong(), 1 + (int) (seed % 3));
        std::vector<std::uint64_t> marks; // patterns guests made while their link was down: none may be lost
        lynx::Song journal = net.authority->confirmed();
        std::uint64_t journalRev = 0;
        auto journalChanges = net.authority->changes();
        for (int i = 0; i < 160; ++i)
        {
            const int who = (int) (rng() % (net.guests.size() + 1));
            auto& w = who == 0 ? net.host.working : net.guests[(std::size_t) who - 1].working;
            live169bRandomEdit (w, who, rng);
            if (who > 0 && ! net.online[(std::size_t) who - 1] && rng() % 3 == 0)
                marks.push_back (live169jNewPattern (w, "off" + std::to_string (i)));
            if (rng() % 2)
                net.pollOnline (who);
            for (int k = (int) (rng() % 4); k > 0; --k)
                net.step (rng);
            const auto r = rng() % 40;
            const int g = (int) (rng() % net.guests.size());
            if (r == 0 && net.online[(std::size_t) g])
                net.drop (g);
            else if (r <= 3 && ! net.online[(std::size_t) g])
                net.reconnect (g);
            else if (r == 4)
                journal = net.authority->confirmed(), journalRev = net.authority->revision(), journalChanges = net.authority->changes();
            else if (r == 5 && i % 3 == 0)
            {
                ++restarts;
                net.restartHost (journal, journalRev, journalChanges);
            }
        }
        net.settleAll (rng);
        net.settleAll (rng);
        ++sessions;
        const auto& truth = net.authority->confirmed();
        bool ok = live169bSame (net.host.working, truth);
        for (auto& g : net.guests)
            ok = ok && live169bSame (g.working, truth) && g.replica->pending() == 0 && g.replica->revision() == net.authority->revision();
        converged += ok ? 1 : 0;
        if (! ok)
        {
            std::string why = "host:" + std::to_string (live169bSame (net.host.working, truth));
            for (auto& g : net.guests)
            {
                why += " g:" + std::to_string (live169bSame (g.working, truth)) + "/" + std::to_string (g.replica->pending()) + "/"
                       + std::to_string (g.replica->revision()) + "/" + std::to_string (net.authority->revision());
                for (const auto& k : live::diff (truth, g.working).keys())
                    why += " " + live::toString (k);
            }
            std::printf ("  seed %u: %s\n", seed, why.c_str());
        }
        replays += net.replays;
        snapshots += net.snapshots;
        for (auto uid : marks)
            markersKept += live169jHasPattern (truth, uid) ? 1 : 0;
        markers += (int) marks.size();
    }
    CHECK (converged == sessions);
    CHECK (markers > 10 && markersKept == markers);
    CHECK (replays > 10 && snapshots > 5 && restarts > 0);
    std::printf ("  live169j: %d/%d sessions converged over dropping links (%d replays, %d snapshots, %d host restarts), %d/%d offline edits kept\n",
                 converged, sessions, replays, snapshots, restarts, markersKept, markers);
}

TEST (live169j_permissions_change_during_the_session)
{
    using namespace lynx;
    std::mt19937 rng (5);
    Live169jNet net (live169bSong(), 1);
    auto& ana = net.guests[0];
    // the tempo is the host's …
    ana.working.bpm = 150.0;
    net.poll (1);
    net.settle (rng);
    CHECK (net.authority->confirmed().bpm != 150.0 && ana.working.bpm != 150.0);
    // … until the host gives her "Tempo & project settings" (not the host's channels: still refused)
    live::Permissions settings;
    settings.settings = true;
    net.authority->setPermissions (2, settings);
    ana.working.bpm = 150.0;
    ana.working.channels[0].name = "Not hers";
    live169bPublish (ana.working);
    net.poll (1);
    net.settle (rng);
    CHECK (net.authority->confirmed().bpm == 150.0 && ana.working.bpm == 150.0 && net.authority->confirmed().channels[0].name == "Kick");
    // listen only: even her own new channel is refused
    live::Permissions listen;
    listen.listener = true;
    net.authority->setPermissions (2, listen);
    ChannelDef c;
    c.name = "Ana's";
    ana.working.channels.push_back (c);
    live169bPublish (ana.working);
    net.poll (1);
    net.settle (rng);
    CHECK (net.authority->confirmed().channels.size() == 3 && ana.working.channels.size() == 3);
    // edit everything: the host's channel now
    live::Permissions all;
    all.editEverything = true;
    net.authority->setPermissions (2, all);
    ana.working.channels[0].name = "Ana's kick";
    live169bPublish (ana.working);
    net.poll (1);
    net.settle (rng);
    CHECK (net.authority->confirmed().channels[0].name == "Ana's kick" && live169bSame (ana.working, net.authority->confirmed()));
    CHECK (live::mayEdit (2, settings, { live::Kind::globals }, net.authority->confirmed(), net.authority->confirmed())
           && ! live::mayEdit (2, settings, { live::Kind::session }, net.authority->confirmed(), net.authority->confirmed())
           && ! live::mayEdit (2, settings, { live::Kind::order }, net.authority->confirmed(), net.authority->confirmed()));
}

TEST (live169j_host_identity)
{
    using namespace lynx;
    const auto seed = livecrypto::newSeed();
    const auto pub = livecrypto::publicKeyOf (seed);
    const auto sig = livecrypto::signHost (seed, "abcdefghij", "nonce-1");
    CHECK (livecrypto::checkHost (pub, "abcdefghij", "nonce-1", sig));
    CHECK (! livecrypto::checkHost (pub, "abcdefghij", "nonce-2", sig)); // another guest's nonce: refused (no replay)
    CHECK (! livecrypto::checkHost (pub, "abcdefghik", "nonce-1", sig));
    auto bent = sig;
    bent[5] ^= 1;
    CHECK (! livecrypto::checkHost (pub, "abcdefghij", "nonce-1", bent));
    CHECK (! livecrypto::checkHost (livecrypto::publicKeyOf (livecrypto::newSeed()), "abcdefghij", "nonce-1", sig)); // a guest pretending
    CHECK (livecrypto::publicKeyOf (seed) == pub); // (the seed is not wiped)
    livecrypto::PublicKey back {};
    CHECK (livecrypto::fromHex (livecrypto::toHex (pub), back) && back == pub && ! livecrypto::fromHex ("12", back) && ! livecrypto::fromHex (std::string (64, 'x'), back));
}

TEST (live169j_chat_text)
{
    using namespace lynx;
    CHECK (live::cleanChat ("  hello  ") == "hello");
    CHECK (live::cleanChat ("a\tb\x01" "c\r\nd") == "a b c\nd");
    CHECK (live::cleanChat (" \n \t ").empty());
    std::string longText;
    for (int i = 0; i < 400; ++i)
        longText += "\xC3\xA9"; // é: 2 bytes each
    const auto cut = live::cleanChat (longText);
    CHECK (cut.size() <= live::chatMaxBytes && cut.size() % 2 == 0 && cut.size() >= live::chatMaxBytes - 1);
}

TEST (live169j_follow_the_host_transport)
{
    using namespace lynx;
    using T = live::FollowAction::Type;
    live::HostTransport h;
    h.playing = true;
    h.ticks = 3840.0;
    h.bpm = 120.0;
    h.atMs = 1000.0;
    // 500 ms later at 120 BPM the host is one beat on
    CHECK (std::abs (live::hostTicksAt (h, 1500.0) - (3840.0 + ppq)) < 1e-6);
    auto a = live::follow (h, 1500.0, false, true, 0.0);
    CHECK (a.type == T::play && std::abs (a.ticks - (3840.0 + ppq)) < 1e-6);
    CHECK (live::follow (h, 1500.0, true, true, 3840.0 + ppq - 100.0).type == T::none); // close enough
    CHECK (live::follow (h, 1500.0, true, true, 0.0).type == T::seek);
    CHECK (live::follow (h, 1500.0, true, false, 3840.0 + ppq).type == T::play); // (the host plays the song, this one a pattern)
    h.playing = false;
    CHECK (live::follow (h, 9000.0, true, true, 5000.0).type == T::stop && live::follow (h, 9000.0, false, true, 0.0).type == T::none);
    CHECK (live::hostTicksAt (h, 9000.0) == 3840.0);
}

TEST (live169j_who_renders_live_stand_ins)
{
    using namespace lynx;
    auto song = live169bSong();
    auto* ana = parts::invite (song, "Host", "Ana", 0xFF3D9BE9, false);
    const auto anaInsert = ana->insertFirst;
    PluginRef serum;
    serum.id = "VST3-Serum-1";
    serum.name = "Serum";
    PluginRef valhalla;
    valhalla.id = "VST3-Valhalla-2";
    valhalla.name = "ValhallaRoom";
    // Ana's channel plays Serum; her insert has ValhallaRoom; the host's Kick is LYNX's own synth
    ChannelDef c;
    c.name = "Ana lead";
    c.instrument = InstrumentKind::plugin;
    c.plugin = serum;
    c.mixerTrack = anaInsert;
    song.channels.push_back (c);
    song.session.me = 2;
    live169bPublish (song);
    if ((int) song.mixer.size() <= anaInsert)
        song.mixer.resize ((std::size_t) anaInsert + 1);
    EffectSlot slot;
    slot.kind = EffectKind::plugin;
    slot.plugin = valhalla;
    song.mixer[(std::size_t) anaInsert].effects.push_back (slot);
    const auto anaLead = song.channels.back().uid;
    CHECK (uids::tagOf (anaLead) == 2);

    // the host has neither plugin; Ana has both; Pedro (3) has Serum only
    auto hostMissing = [] (const PluginRef&) { return true; };
    auto anaMissing = [] (const PluginRef&) { return false; };
    auto pedroMissing = [] (const PluginRef& r) { return r.name != "Serum"; };
    const auto hostNeeds = live::unplayable (song, hostMissing);
    const live::Key leadKey { live::Kind::channel, anaLead, 0 }, insertKey { live::Kind::insert, (std::uint64_t) anaInsert, 0 };
    CHECK (hostNeeds.size() == 2 && hostNeeds.count (leadKey) == 1 && hostNeeds.count (insertKey) == 1);
    CHECK (live::unplayable (song, anaMissing).empty() && live::unplayable (song, pedroMissing) == std::set<live::Key> { insertKey });
    // Ana renders both (hers, plugins here); the host renders nothing (not his, and missing here); Pedro neither
    CHECK (live::toRender (song, hostNeeds, 2, anaMissing).size() == 2);
    CHECK (live::toRender (song, hostNeeds, 1, hostMissing).empty() && live::toRender (song, hostNeeds, 3, pedroMissing).empty());
    // nothing needed: nothing rendered; a channel that is gone: nothing
    CHECK (live::toRender (song, {}, 2, anaMissing).empty());
    CHECK (live::toRender (song, { { live::Kind::channel, uids::make (2, 999), 0 } }, 2, anaMissing).empty());
}
