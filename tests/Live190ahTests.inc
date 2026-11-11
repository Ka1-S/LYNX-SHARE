// Round 190ah: Online Sessions (docs/design/ONLINE_SESSIONS.md "As built (190ah)"): a guest whose own LYNX SHARE
// restarts comes back as itself from its journal (the confirmed state its working song stood on, the working song,
// the revision, the host's epoch and its proposal counter), with what it had changed and the host never confirmed;
// the limits on a session's audio files; more participant records than people online. No network: the in-process
// network of rounds 169b / 169j (Live169jNet) with guests that restart.
#include "core/LiveSession.h"
#include "core/SessionIds.h"
#include "core/SessionParts.h"

namespace
{
struct Live190ahNet : Live169jNet
{
    // what LiveSessionManager::writeGuestJournal keeps
    struct Journal
    {
        lynx::Song base, working;
        std::uint64_t rev = 0, epoch = 0;
        std::uint32_t seq = 0;
    };
    std::vector<Journal> journals;
    int guestRestarts = 0;

    Live190ahNet (lynx::Song song, int guestCount, lynx::live::Codec c = {}) : Live169jNet (std::move (song), guestCount, std::move (c))
    {
        journals.resize (guests.size());
        for (int g = 0; g < (int) guests.size(); ++g)
            writeJournal (g);
    }

    void writeJournal (int g)
    {
        const auto& p = guests[(std::size_t) g];
        journals[(std::size_t) g] = { p.replica->base(), p.working, p.replica->baseRevision(), p.replica->epoch, p.replica->sequence() };
    }

    // The guest's LYNX SHARE closes or crashes and starts again from its journal: everything since the journal is gone
    // from that computer. `sentArrives`: what it had sent still reaches the host (it was on the wire) — or is lost too.
    void restartGuest (int g, bool sentArrives)
    {
        auto& p = guests[(std::size_t) g];
        if (sentArrives && online[(std::size_t) g])
            while (! p.outbox.empty())
            {
                auto sent = codec ? lynx::live::Proposal { p.outbox.front().seq, p.outbox.front().baseRev, codec (p.outbox.front().patch) } : p.outbox.front();
                p.outbox.pop_front();
                broadcast (authority->propose ((std::uint32_t) g + 2, sent));
            }
        drop (g);
        const auto& j = journals[(std::size_t) g];
        p.replica = std::make_unique<lynx::live::Replica> (j.base, j.rev, (std::uint32_t) g + 2, codec);
        p.replica->epoch = j.epoch;
        p.replica->restored (j.seq + 1000u);
        p.working = j.working;
        ++guestRestarts;
    }

    // nothing reaches a computer that is not connected (and it sends nothing: LiveSessionManager does not poll then)
    void loseOffline()
    {
        for (std::size_t g = 0; g < guests.size(); ++g)
            if (! online[g])
                guests[g].inbox.clear();
    }

    void settleOnline (std::mt19937& rng)
    {
        for (int round = 0; round < 50; ++round)
        {
            for (int who = 0; who <= (int) guests.size(); ++who)
                pollOnline (who);
            bool any = false;
            for (;;)
            {
                loseOffline();
                if (! step (rng))
                    break;
                any = true;
            }
            if (! any)
                break;
        }
    }
};
} // namespace

TEST (live190ah_guest_restart_comes_back_as_itself)
{
    using namespace lynx;
    std::mt19937 rng (5);
    Live190ahNet net (live169bSong(), 2);
    auto& ana = net.guests[0];
    // Ana works; her journal is written with one pattern confirmed and one still on its way to the host
    const auto confirmed = live169jNewPattern (ana.working, "Ana confirmed");
    net.poll (1);
    net.settle (rng);
    const auto onTheWire = live169jNewPattern (ana.working, "Ana on the wire");
    net.poll (1);
    const auto unsent = live169jNewPattern (ana.working, "Ana unsent");
    CHECK (ana.replica->pending() == 1);
    net.writeJournal (0);
    // after the journal: one more pattern (never written down: lost with the crash unless the host got it)
    const auto afterJournal = live169jNewPattern (ana.working, "Ana after the journal");
    net.poll (1);
    // her LYNX SHARE closes; what was on the wire reaches the host. Meanwhile the others go on
    net.restartGuest (0, true);
    net.host.working.bpm = 141.0;
    net.poll (0);
    const auto pedro = live169jNewPattern (net.guests[1].working, "Pedro meanwhile");
    net.poll (2);
    net.settleOnline (rng);
    CHECK (ana.replica->me() == 2 && ana.replica->pending() == 0 && live169jHasPattern (ana.working, unsent) && ! live169jHasPattern (ana.working, afterJournal));
    // she edits before she is even connected again
    const auto offline = live169jNewPattern (ana.working, "Ana before reconnecting");
    // back in: the host replays what she missed — her own earlier commits among them — and her unsent edits go out
    net.reconnect (0);
    CHECK (net.replays == 1 && net.snapshots == 0);
    net.settle (rng);
    const auto& truth = net.authority->confirmed();
    CHECK (live169jHasPattern (truth, confirmed) && live169jHasPattern (truth, onTheWire) && live169jHasPattern (truth, unsent) && live169jHasPattern (truth, offline)
           && live169jHasPattern (truth, afterJournal) && live169jHasPattern (truth, pedro) && truth.bpm == 141.0);
    CHECK (live169bSame (ana.working, truth) && live169bSame (net.guests[1].working, truth) && live169bSame (net.host.working, truth) && ana.replica->pending() == 0);
    // nothing was made twice: each of her patterns exists once
    int copies = 0;
    for (const auto& p : truth.patterns)
        copies += p.name == "Ana on the wire" || p.name == "Ana unsent" ? 1 : 0;
    CHECK (copies == 2);
    // still the same participant: no new record, the same tag owns her patterns
    CHECK (truth.session.participants.size() == 3 && uids::tagOf (unsent) == 2 && uids::tagOf (offline) == 2);

    // the same when what was on the wire is lost with the crash: it is in the journal, so it goes out again
    const auto second = live169jNewPattern (ana.working, "Ana second run");
    net.poll (1);
    net.writeJournal (0);
    net.restartGuest (0, false);
    CHECK (! live169jHasPattern (net.authority->confirmed(), second));
    net.reconnect (0);
    net.settle (rng);
    CHECK (live169jHasPattern (net.authority->confirmed(), second) && live169bSame (ana.working, net.authority->confirmed()) && ana.replica->pending() == 0);
}

TEST (live190ah_guest_restart_judged_on_the_journal_base)
{
    using namespace lynx;
    std::mt19937 rng (9);
    Live190ahNet net (live169bSong(), 1);
    live::Permissions all;
    all.editEverything = true;
    net.authority->setPermissions (2, all);
    auto& ana = net.guests[0];
    // Ana renames the host's pattern; the journal is written; the proposal is lost with her crash; the host renames it
    ana.working.patterns[0].name = "Ana's name";
    live169bPublish (ana.working);
    net.writeJournal (0);
    net.poll (1);
    net.restartGuest (0, false);
    net.host.working.patterns[0].name = "Host's name";
    live169bPublish (net.host.working);
    net.poll (0);
    net.settleOnline (rng);
    // back: her rename is judged against the revision her journal stood on — the host's came after it and stays
    net.reconnect (0);
    net.settle (rng);
    CHECK (net.authority->confirmed().patterns[0].name == "Host's name" && ana.working.patterns[0].name == "Host's name" && net.rejected >= 1);
    CHECK (live169bSame (ana.working, net.authority->confirmed()));
    // and a proposal counter above the earlier run's: an old answer is never taken for a new proposal's
    CHECK (ana.replica->sequence() > 1000u);
}

TEST (live190ah_guest_restart_while_the_host_restarted_too)
{
    using namespace lynx;
    std::mt19937 rng (13);
    Live190ahNet net (live169bSong(), 2);
    auto& ana = net.guests[0];
    const auto kept = live169jNewPattern (ana.working, "Ana kept");
    net.poll (1);
    net.settle (rng);
    // the host's journal, then more commits (Ana's among them) the host loses in its crash
    const auto journal = net.authority->confirmed();
    const auto journalRev = net.authority->revision();
    const auto journalChanges = net.authority->changes();
    const auto lostByHost = live169jNewPattern (ana.working, "Ana, lost by the host");
    net.poll (1);
    net.settle (rng);
    net.writeJournal (0); // (Ana's journal is ahead of the host's)
    const auto pending = live169jNewPattern (ana.working, "Ana pending");
    net.writeJournal (0);
    net.restartHost (journal, journalRev, journalChanges);
    net.restartGuest (0, false);
    net.settleAll (rng);
    const auto& truth = net.authority->confirmed();
    // another epoch: a snapshot; her own area comes back from her journal's copy, the pending pattern too
    CHECK (net.snapshots >= 2 && live169jHasPattern (truth, kept) && live169jHasPattern (truth, lostByHost) && live169jHasPattern (truth, pending));
    CHECK (live169bSame (ana.working, truth) && live169bSame (net.guests[1].working, truth) && live169bSame (net.host.working, truth));
}

TEST (live190ah_replicas_converge_with_guest_restarts)
{
    using namespace lynx;
    int sessions = 0, converged = 0, guestRestarts = 0, hostRestarts = 0, kept = 0, marked = 0, replays = 0, snapshots = 0;
    for (unsigned seed = 1; seed <= 24; ++seed)
    {
        std::mt19937 rng (seed * 104729u);
        Live190ahNet net (live169bSong(), 1 + (int) (seed % 3));
        // patterns a guest made that its journal knows (or that it made after its last restart): none may be lost
        std::vector<std::uint64_t> marks;
        std::vector<std::vector<std::uint64_t>> notWritten (net.guests.size());
        lynx::Song journal = net.authority->confirmed();
        std::uint64_t journalRev = 0;
        auto journalChanges = net.authority->changes();
        auto writeGuest = [&] (int g) {
            net.writeJournal (g);
            auto& fresh = notWritten[(std::size_t) g];
            marks.insert (marks.end(), fresh.begin(), fresh.end());
            fresh.clear();
        };
        for (int i = 0; i < 200; ++i)
        {
            const int who = (int) (rng() % (net.guests.size() + 1));
            auto& w = who == 0 ? net.host.working : net.guests[(std::size_t) who - 1].working;
            live169bRandomEdit (w, who, rng);
            if (who > 0 && rng() % 4 == 0)
                notWritten[(std::size_t) who - 1].push_back (live169jNewPattern (w, "mark" + std::to_string (i)));
            if (rng() % 2)
                net.pollOnline (who);
            for (int k = (int) (rng() % 4); k > 0; --k)
            {
                net.loseOffline();
                net.step (rng);
            }
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
                ++hostRestarts;
                net.restartHost (journal, journalRev, journalChanges);
            }
            else if (r <= 12)
                writeGuest (g); // (the journal is written every 2 s: often)
            else if (r <= 14)
            {
                // a guest's LYNX SHARE restarts: what it made since its journal is gone from that computer
                notWritten[(std::size_t) g].clear();
                net.restartGuest (g, rng() % 2 == 0);
            }
        }
        for (auto& fresh : notWritten)
            marks.insert (marks.end(), fresh.begin(), fresh.end());
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
                why += " g:" + std::to_string (live169bSame (g.working, truth)) + "/" + std::to_string (g.replica->pending()) + "/" + std::to_string (g.replica->revision()) + "/"
                       + std::to_string (net.authority->revision());
                for (const auto& k : live::diff (truth, g.working).keys())
                    why += " " + live::toString (k);
            }
            std::printf ("  seed %u: %s\n", seed, why.c_str());
        }
        guestRestarts += net.guestRestarts;
        replays += net.replays;
        snapshots += net.snapshots;
        for (auto uid : marks)
            kept += live169jHasPattern (truth, uid) ? 1 : 0;
        marked += (int) marks.size();
        // nobody became someone else: the participants are the ones the session started with
        CHECK (truth.session.participants.size() == net.guests.size() + 1);
    }
    CHECK (converged == sessions);
    CHECK (marked > 40 && kept == marked);
    CHECK (guestRestarts > 40 && hostRestarts > 0 && replays > 10 && snapshots > 5);
    std::printf ("  live190ah: %d/%d sessions converged with %d guest restarts and %d host restarts (%d replays, %d snapshots), %d/%d journaled edits kept\n",
                 converged, sessions, guestRestarts, hostRestarts, replays, snapshots, kept, marked);
}

TEST (live190ah_media_budget)
{
    using namespace lynx;
    const std::int64_t mb = 1 << 20;
    live::MediaBudget b;
    CHECK (b.guestLimit == live::guestMediaBytes && b.sessionLimit == live::sessionMediaBytes && live::guestMediaBytes < live::sessionMediaBytes);
    b.guestLimit = 100 * mb;
    b.sessionLimit = 400 * mb;
    using V = live::MediaBudget::Verdict;
    // a guest's files count against that guest's limit and the session's; the host's only against the session's
    CHECK (b.admit (2, 60 * mb) == V::ok);
    b.received (2, 60 * mb);
    CHECK (b.usedBy (2) == 60 * mb && b.session == 60 * mb && b.usedBy (3) == 0);
    CHECK (b.admit (2, 41 * mb) == V::guestLimit && b.admit (2, 40 * mb) == V::ok && b.admit (3, 100 * mb) == V::ok && b.admit (3, 101 * mb) == V::guestLimit);
    CHECK (b.admit (session::hostTag, 300 * mb) == V::ok && b.admitOut (340 * mb) == V::ok && b.admitOut (341 * mb) == V::sessionLimit);
    b.sent (200 * mb);
    CHECK (b.session == 260 * mb && b.usedBy (2) == 60 * mb);
    // the host raises one guest's limit: it doubles until the file fits; the others keep theirs
    b.raise (2, 90 * mb);
    CHECK (b.limitOf (2) == 200 * mb && b.limitOf (3) == 100 * mb && b.admit (2, 90 * mb) == V::ok);
    b.raise (2, 0);
    CHECK (b.limitOf (2) == 400 * mb);
    b.raise (2, 10000 * mb); // (never above the session's)
    CHECK (b.limitOf (2) == 400 * mb);
    // the session's limit wins over a raised guest's
    b.received (2, 100 * mb);
    CHECK (b.session == 360 * mb && b.admit (2, 41 * mb) == V::sessionLimit && b.admit (3, 41 * mb) == V::sessionLimit && b.admit (3, 40 * mb) == V::ok);
    // nonsense sizes count as nothing
    b.received (3, -5);
    b.sent (-5);
    CHECK (b.session == 360 * mb && b.admit (3, -1) == V::ok);
    CHECK (live::mediaSize (live::guestMediaBytes) == "1 GB" && live::mediaSize (live::sessionMediaBytes) == "8 GB" && live::mediaSize (1536 * mb) == "1.5 GB"
           && live::mediaSize (320 * mb) == "320 MB" && live::mediaSize (10) == "1 MB" && live::mediaSize (-1) == "1 MB");
}

TEST (live190ah_more_participant_records_than_people_online)
{
    using namespace lynx;
    auto song = live169bSong();
    // packages (P1) keep the user's limit of 4 participants in all
    CHECK (parts::invite (song, "Host", "Ana", 0, false) != nullptr && parts::invite (song, "Host", "Pedro", 0, false) != nullptr
           && parts::invite (song, "Host", "Lia", 0, false) != nullptr);
    CHECK (parts::invite (song, "Host", "Rui", 0, false) == nullptr && song.session.participants.size() == 4);
    // a live session lets someone new in after someone else left for good: a new record, a new tag and area
    auto* rui = parts::invite (song, "Host", "Rui", 0, false, 16);
    CHECK (rui != nullptr && rui->tag == 5 && song.session.participants.size() == 5);
    const auto* lia = song.session.find (4);
    CHECK (lia != nullptr && rui != nullptr && rui->insertFirst >= lia->insertFirst + lia->insertCount && rui->rowFirst >= lia->rowFirst + lia->rowCount);
    // someone who was here before gets their own record back, however many there are
    auto* again = parts::invite (song, "Host", "pedro", 0, false, 16);
    CHECK (again != nullptr && again->tag == 3 && song.session.participants.size() == 5);
    for (int i = 0; i < 20; ++i)
        parts::invite (song, "Host", "Guest " + std::to_string (i), 0, false, 16);
    CHECK (song.session.participants.size() == 16);
    CHECK (live::protocolVersion == 4);
}
