// Round 169b: Online Sessions phase 2 — live sessions (docs/design/ONLINE_SESSIONS.md §1.2–§1.5, §2, §6.4): entity
// diff / apply across computers whose indices differ, the host's authority (permissions, conflicts, ripples), replicas
// that converge over an in-process network with delays and concurrent edits, held commits while recording, undo per
// user, and the invite code / end-to-end encryption.
#include <deque>
#include <random>

#include "core/LiveCrypto.h"
#include "core/LiveSession.h"
#include "core/SessionIds.h"
#include "core/SessionParts.h"

namespace
{
lynx::Song live169bSong()
{
    auto s = lynx::Song::makeDefault();
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    for (const auto* name : { "Kick", "Snare", "Bass" })
    {
        lynx::ChannelDef c;
        c.name = name;
        c.instrument = lynx::InstrumentKind::synth;
        c.mixerTrack = (int) s.channels.size() + 1;
        s.channels.push_back (c);
    }
    for (const auto* name : { "Beat", "Fill" })
        s.patterns.emplace_back().name = name;
    for (int i = 0; i < 4; ++i)
    {
        lynx::Note n;
        n.channel = i % 3;
        n.start = i * 240;
        n.key = 36 + i;
        s.patterns[0].notes.push_back (n);
    }
    lynx::Clip clip;
    clip.pattern = 0;
    clip.track = 0;
    clip.length = 3840;
    s.playlist.push_back (clip);
    clip.pattern = 1;
    clip.start = 3840;
    s.playlist.push_back (clip);
    lynx::uids::normalize (s);
    return s;
}

int live169bChannel (const lynx::Song& s, std::uint64_t uid)
{
    for (int i = 0; i < (int) s.channels.size(); ++i)
        if (s.channels[(std::size_t) i].uid == uid)
            return i;
    return -1;
}

bool live169bSame (const lynx::Song& a, const lynx::Song& b) { return lynx::live::diff (a, b).empty(); }

// what LYNX's publishSong does after every edit
void live169bPublish (lynx::Song& s) { lynx::uids::normalize (s); }
} // namespace

TEST (live169b_diff_apply_round_trip)
{
    using namespace lynx;
    const auto a = live169bSong();
    auto b = a;
    b.bpm = 141.0;
    b.channels[1].volume = 0.3f;
    b.channels.push_back (b.channels[0]); // a duplicate: normalize renames it
    b.channels.back().name = "Kick 2";
    b.patterns[0].notes[1].key = 70;
    b.patterns[1].name = "Fill B";
    b.patterns.emplace_back().name = "New";
    Note n;
    n.channel = 3;
    n.start = 960;
    b.patterns[2].notes.push_back (n);
    Clip c;
    c.pattern = 2;
    c.track = 2;
    c.start = 7680;
    c.length = 3840;
    b.playlist.push_back (c);
    b.mixer[3].volume = 0.5f;
    b.mixer[3].name = "Bass bus";
    b.playlistTracks.resize (3);
    b.playlistTracks[2].name = "Drums";
    live169bPublish (b);

    const auto patch = live::diff (a, b);
    CHECK (! patch.empty());
    auto x = a;
    live::apply (x, patch);
    CHECK (live169bSame (x, b));
    CHECK (x.channels.size() == 4 && x.channels[3].name == "Kick 2" && x.patterns.size() == 3 && x.playlist.size() == 3);
    CHECK (x.playlist[2].pattern == 2 && x.patterns[2].notes.size() == 1 && x.patterns[2].notes[0].channel == 3);
    CHECK (x.bpm == 141.0 && x.mixer[3].name == "Bass bus" && x.playlistTracks[2].name == "Drums");
    // nothing left to do afterwards, both ways
    CHECK (live::diff (x, b).empty() && live::diff (b, x).empty());

    // deletions go through Song::removeChannel (its notes and audio clips go with it)
    auto d = b;
    d.removeChannel (0);
    live169bPublish (d);
    auto y = b;
    live::apply (y, live::diff (b, d));
    CHECK (live169bSame (y, d) && y.channels.size() == 3 && y.channels[0].name == "Snare");
    CHECK (std::none_of (y.patterns[0].notes.begin(), y.patterns[0].notes.end(), [&] (const Note& k) { return y.channels[(std::size_t) k.channel].name == "Kick"; }));

    // the carrier of a one-note change is small: the channel stub and the pattern, nothing else
    auto e = b;
    e.patterns[0].notes[0].velocity = 0.25f;
    const auto small = live::diff (b, e);
    CHECK (small.changed.size() == 1 && small.removed.empty() && small.changed[0].kind == live::Kind::notes);
    CHECK (small.carrier.channels.size() == 1 && small.carrier.patterns.size() == 1 && small.carrier.playlist.empty());

    // keys survive their wire form
    for (const auto& k : patch.keys())
        CHECK (live::keyFromString (live::toString (k)) == k);
    CHECK (! live::keyFromString ("n:12") && ! live::keyFromString ("m:99999999") && ! live::keyFromString ("x:1"));
}

TEST (live169b_patch_applies_by_uid_not_index)
{
    using namespace lynx;
    // two copies whose channel order differs (the guest has an unconfirmed channel of his own in front)
    const auto host = live169bSong();
    auto guest = host;
    guest.session.me = 2;
    ChannelDef mine;
    mine.name = "Guest synth";
    guest.channels.insert (guest.channels.begin(), mine);
    for (auto& p : guest.patterns)
        for (auto& n : p.notes)
            ++n.channel;
    for (auto& c : guest.playlist)
        (void) c;
    live169bPublish (guest);

    auto edited = host;
    edited.patterns[0].notes.push_back ([] {
        Note n;
        n.channel = 2; // Bass in the host's order
        n.start = 1920;
        n.key = 50;
        return n;
    }());
    edited.channels[2].pan = -0.5f;
    live::apply (guest, live::diff (host, edited));
    const auto bass = live169bChannel (guest, host.channels[2].uid);
    CHECK (bass == 3 && guest.channels[3].pan == -0.5f && guest.channels[0].name == "Guest synth");
    CHECK (std::any_of (guest.patterns[0].notes.begin(), guest.patterns[0].notes.end(), [&] (const Note& n) { return n.channel == bass && n.key == 50; }));
    // the local view stays: the current pattern by uid, the play mode
    auto view = host;
    view.currentPattern = 1;
    view.mode = PlayMode::song;
    auto moved = host;
    moved.patterns.insert (moved.patterns.begin(), Pattern {});
    moved.patterns[0].name = "Intro";
    for (auto& c : moved.playlist)
        ++c.pattern;
    live169bPublish (moved);
    live::apply (view, live::diff (host, moved));
    CHECK (view.mode == PlayMode::song && view.patterns[(std::size_t) view.currentPattern].name == "Fill");
}

TEST (live169b_authority_permissions_and_ripples)
{
    using namespace lynx;
    auto song = live169bSong();
    const auto* ana = parts::invite (song, "Host", "Ana", 0xFF3D9BE9, false);
    CHECK (ana != nullptr && ana->tag == 2);
    const auto anaInsert = ana->insertFirst, anaRow = ana->rowFirst;
    live::Authority authority (song, {});
    live::Replica guest (song, 0, 2, {});

    // Ana's own channel, notes, insert and row: accepted
    auto w = guest.confirmed();
    ChannelDef c;
    c.name = "Ana's keys";
    c.mixerTrack = anaInsert;
    w.channels.push_back (c);
    live169bPublish (w);
    const auto anaChannel = (int) w.channels.size() - 1;
    CHECK (uids::tagOf (w.channels.back().uid) == 2);
    Note n;
    n.channel = anaChannel;
    n.key = 64;
    w.patterns[1].notes.push_back (n);
    if ((int) w.mixer.size() <= anaInsert)
        w.mixer.resize ((std::size_t) anaInsert + 1);
    w.mixer[(std::size_t) anaInsert].name = "Ana";
    w.playlistTracks.resize ((std::size_t) anaRow + 1);
    w.playlistTracks[(std::size_t) anaRow].name = "Ana's row";
    // … and two things that are not hers: the host's Kick and the tempo
    w.channels[0].volume = 0.1f;
    w.bpm = 99.0;
    auto prop = guest.poll (w);
    CHECK (prop.has_value());
    auto commit = authority.propose (2, *prop);
    CHECK (commit.rev == 1 && commit.rejected.size() == 2 && commit.reason == "owned");
    const auto& hostSide = authority.confirmed();
    CHECK (hostSide.bpm == song.bpm && hostSide.channels[0].volume == song.channels[0].volume);
    CHECK (hostSide.channels.size() == 4 && hostSide.channels[3].name == "Ana's keys" && hostSide.mixer[(std::size_t) anaInsert].name == "Ana");
    CHECK ((int) hostSide.playlistTracks.size() > anaRow && hostSide.playlistTracks[(std::size_t) anaRow].name == "Ana's row");
    // Ana's copy: her edits stay, the two others are undone and reported
    auto u = guest.onCommit (commit, w);
    CHECK (u.working.has_value() && u.rejected.size() == 2);
    w = *u.working;
    CHECK (w.bpm == song.bpm && w.channels[0].volume == song.channels[0].volume && w.channels[3].name == "Ana's keys");
    CHECK (live169bSame (w, authority.confirmed()) && ! guest.poll (w).has_value() && guest.pending() == 0);

    // a ripple: Ana deletes her channel; its notes go with it (her slice) — and a host-owned clip that used the
    // channel would go too, the same way on the host, so nothing is rejected
    auto w2 = w;
    w2.removeChannel (3);
    live169bPublish (w2);
    auto commit2 = authority.propose (2, *guest.poll (w2));
    CHECK (commit2.rejected.empty() && authority.confirmed().channels.size() == 3);
    auto u2 = guest.onCommit (commit2, w2);
    CHECK (! u2.working.has_value() || live169bSame (*u2.working, w2));

    // listeners edit nothing; "Edit everything" edits the host's things (not the participant list)
    live::Permissions everything;
    everything.editEverything = true;
    const live::Key kick { live::Kind::channel, song.channels[0].uid };
    CHECK (! live::mayEdit (2, {}, kick, song, song) && live::mayEdit (2, everything, kick, song, song));
    live::Permissions listener;
    listener.listener = true;
    CHECK (! live::mayEdit (2, listener, { live::Kind::channel, uids::make (2, 5) }, song, song));
    CHECK (! live::mayEdit (2, everything, { live::Kind::session }, song, song) && live::mayEdit (1, {}, { live::Kind::session }, song, song));
    CHECK (live::ownerOf ({ live::Kind::insert, (std::uint64_t) anaInsert }, authority.confirmed()) == 2
           && live::ownerOf ({ live::Kind::insert, 1 }, authority.confirmed()) == 1);
}

TEST (live169b_conflicts_the_earlier_edit_wins)
{
    using namespace lynx;
    auto song = live169bSong();
    parts::invite (song, "Host", "Ana", 0xFF3D9BE9, false);
    parts::invite (song, "Host", "Pedro", 0xFF5CC85C, false);
    live::Authority authority (song, {});
    live::Permissions everything;
    everything.editEverything = true;
    authority.setPermissions (2, everything);
    authority.setPermissions (3, everything);
    live::Replica ana (song, 0, 2, {}), pedro (song, 0, 3, {});
    auto wa = song, wp = song;
    wa.session.me = 2, wp.session.me = 3;
    wa.patterns[0].name = "Ana's name";
    wp.patterns[0].name = "Pedro's name";
    wp.patterns[1].name = "Pedro's fill"; // (not in conflict: stays)
    const auto pa = *ana.poll (wa);
    const auto pp = *pedro.poll (wp);
    auto ca = authority.propose (2, pa);
    auto cp = authority.propose (3, pp); // made on revision 0, after Ana's commit 1 changed that pattern
    CHECK (ca.rejected.empty() && cp.rejected.size() == 1 && cp.reason == "conflict");
    CHECK (authority.confirmed().patterns[0].name == "Ana's name" && authority.confirmed().patterns[1].name == "Pedro's fill");
    // both replicas end where the host is
    auto ua = ana.onCommit (ca, wa);
    if (ua.working)
        wa = *ua.working;
    ua = ana.onCommit (cp, wa);
    if (ua.working)
        wa = *ua.working;
    auto up = pedro.onCommit (ca, wp);
    if (up.working)
        wp = *up.working;
    up = pedro.onCommit (cp, wp);
    CHECK (up.rejected.size() == 1);
    if (up.working)
        wp = *up.working;
    CHECK (live169bSame (wa, authority.confirmed()) && live169bSame (wp, authority.confirmed()) && wp.patterns[0].name == "Ana's name");
}

namespace
{
// The in-process network: the host (authority + its own replica, no latency) and up to 3 guests, each link a FIFO
// whose messages are delivered at random moments (delay, interleaving).
struct Live169bNet
{
    struct Peer
    {
        std::unique_ptr<lynx::live::Replica> replica;
        lynx::Song working;
        std::deque<lynx::live::Commit> inbox;    // host -> this peer
        std::deque<lynx::live::Proposal> outbox; // this peer -> host
        bool holding = false;
    };
    std::unique_ptr<lynx::live::Authority> authority;
    Peer host;
    std::vector<Peer> guests;
    lynx::live::Codec codec;
    int rejected = 0;

    Live169bNet (lynx::Song song, int guestCount, lynx::live::Codec c = {}) : codec (std::move (c))
    {
        static const char* names[] = { "Ana", "Pedro", "Lia" };
        for (int g = 0; g < guestCount; ++g)
            lynx::parts::invite (song, "Host", names[g], 0xFF3D9BE9u + (std::uint32_t) g, false);
        authority = std::make_unique<lynx::live::Authority> (song, codec);
        host.replica = std::make_unique<lynx::live::Replica> (song, 0, 1, codec);
        host.working = song;
        for (int g = 0; g < guestCount; ++g)
        {
            Peer p;
            p.replica = std::make_unique<lynx::live::Replica> (song, 0, (std::uint32_t) g + 2, codec);
            p.working = song;
            p.working.session.me = (std::uint32_t) g + 2;
            guests.push_back (std::move (p));
        }
    }

    void deliver (Peer& p, const lynx::live::Commit& c)
    {
        auto u = p.replica->onCommit (c, p.working, p.holding);
        rejected += (int) u.rejected.size();
        if (u.working)
            p.working = *u.working;
    }

    void broadcast (const lynx::live::Commit& c)
    {
        deliver (host, c);
        for (std::size_t g = 0; g < guests.size(); ++g)
            if (! c.patch.empty() || c.origin == g + 2)
                guests[g].inbox.push_back (c);
    }

    // a peer sends what changed locally
    void poll (int who)
    {
        if (who == 0)
        {
            if (auto p = host.replica->poll (host.working))
                broadcast (authority->propose (1, *p));
        }
        else if (auto p = guests[(std::size_t) who - 1].replica->poll (guests[(std::size_t) who - 1].working))
            guests[(std::size_t) who - 1].outbox.push_back (*p);
    }

    // one message travels (false: nothing in flight)
    bool step (std::mt19937& rng)
    {
        std::vector<std::pair<int, bool>> ready; // guest, to host?
        for (int g = 0; g < (int) guests.size(); ++g)
        {
            if (! guests[(std::size_t) g].outbox.empty())
                ready.push_back ({ g, true });
            if (! guests[(std::size_t) g].inbox.empty())
                ready.push_back ({ g, false });
        }
        if (ready.empty())
            return false;
        const auto [g, up] = ready[rng() % ready.size()];
        auto& peer = guests[(std::size_t) g];
        if (up)
        {
            auto p = codec ? lynx::live::Proposal { peer.outbox.front().seq, peer.outbox.front().baseRev, codec (peer.outbox.front().patch) } : peer.outbox.front();
            peer.outbox.pop_front();
            broadcast (authority->propose ((std::uint32_t) g + 2, p));
        }
        else
        {
            auto c = peer.inbox.front();
            peer.inbox.pop_front();
            deliver (peer, c);
        }
        return true;
    }

    void settle (std::mt19937& rng)
    {
        for (int round = 0; round < 50; ++round)
        {
            for (int who = 0; who <= (int) guests.size(); ++who)
                poll (who);
            bool any = false;
            while (step (rng))
                any = true;
            if (! any)
                break;
        }
    }
};

// One random edit by participant `who` (0 = host) on his working song: mostly in his own area, sometimes not
void live169bRandomEdit (lynx::Song& s, int who, std::mt19937& rng)
{
    using namespace lynx;
    const auto tag = (std::uint32_t) who + 1;
    auto pick = [&] (int n) { return n <= 0 ? 0 : (int) (rng() % (std::uint32_t) n); };
    std::vector<int> mine;
    for (int i = 0; i < (int) s.channels.size(); ++i)
        if (uids::tagOf (s.channels[(std::size_t) i].uid) == tag)
            mine.push_back (i);
    const auto* me = s.session.find (tag);
    switch (pick (11))
    {
        case 0: // a channel of his own
        {
            ChannelDef c;
            c.name = "Ch " + std::to_string (rng() % 1000);
            if (me != nullptr && me->insertCount > 0 && tag != 1)
                c.mixerTrack = me->insertFirst;
            s.channels.push_back (c);
            break;
        }
        case 1:
        case 2: // a note of one of his channels
            if (! mine.empty() && ! s.patterns.empty())
            {
                Note n;
                n.channel = mine[(std::size_t) pick ((int) mine.size())];
                n.start = 120 * pick (32);
                n.key = 40 + pick (40);
                s.patterns[(std::size_t) pick ((int) s.patterns.size())].notes.push_back (n);
            }
            break;
        case 3: // moves or deletes one of his notes
            if (! s.patterns.empty())
            {
                auto& p = s.patterns[(std::size_t) pick ((int) s.patterns.size())];
                for (std::size_t i = 0; i < p.notes.size(); ++i)
                    if (uids::tagOf (s.channels[(std::size_t) p.notes[i].channel].uid) == tag)
                    {
                        if (rng() % 2)
                            p.notes.erase (p.notes.begin() + (long) i);
                        else
                            p.notes[i].start += 120;
                        break;
                    }
            }
            break;
        case 4: // a pattern of his own
            s.patterns.emplace_back().name = "P" + std::to_string (rng() % 1000);
            break;
        case 5: // renames a pattern (his own, or anyone's: the host's would be refused)
            if (! s.patterns.empty())
                s.patterns[(std::size_t) pick ((int) s.patterns.size())].name = "R" + std::to_string (rng() % 1000);
            break;
        case 6: // a clip of one of his patterns on his row
        {
            for (int i = (int) s.patterns.size() - 1; i >= 0; --i)
                if (uids::tagOf (s.patterns[(std::size_t) i].uid) == tag)
                {
                    Clip c;
                    c.pattern = i;
                    c.track = tag == 1 ? pick (3) : (me != nullptr ? me->rowFirst : 0);
                    c.start = 3840 * pick (8);
                    c.length = 3840;
                    s.playlist.push_back (c);
                    break;
                }
            break;
        }
        case 7: // his insert's fader
        {
            const int t = tag == 1 ? 1 + pick (3) : (me != nullptr ? me->insertFirst : 1);
            if (t >= (int) s.mixer.size())
                s.mixer.resize ((std::size_t) t + 1);
            s.mixer[(std::size_t) t].volume = (float) pick (100) / 100.0f;
            break;
        }
        case 8: // his channel's volume (or the host's Kick: refused for guests)
            if (! mine.empty() && rng() % 3)
                s.channels[(std::size_t) mine[0]].volume = (float) pick (100) / 100.0f;
            else if (! s.channels.empty())
                s.channels[0].pan = (float) pick (100) / 100.0f - 0.5f;
            break;
        case 9: // the tempo (the host's)
            s.bpm = 100.0 + pick (60);
            break;
        case 10: // deletes one of his channels
            if (! mine.empty() && rng() % 3 == 0)
                s.removeChannel (mine.back());
            break;
    }
    live169bPublish (s);
}
} // namespace

TEST (live169b_replicas_converge_over_a_delayed_network)
{
    using namespace lynx;
    int seeds = 0, converged = 0, rejectedTotal = 0, commits = 0;
    for (unsigned seed = 1; seed <= 24; ++seed)
    {
        std::mt19937 rng (seed);
        Live169bNet net (live169bSong(), 1 + (int) (seed % 3)); // 2 to 4 people
        for (int i = 0; i < 160; ++i)
        {
            const int who = (int) (rng() % (net.guests.size() + 1));
            auto& w = who == 0 ? net.host.working : net.guests[(std::size_t) who - 1].working;
            live169bRandomEdit (w, who, rng);
            if (rng() % 2)
                net.poll (who);
            for (int k = (int) (rng() % 4); k > 0; --k)
                net.step (rng);
        }
        net.settle (rng);
        ++seeds;
        const auto& truth = net.authority->confirmed();
        bool ok = live169bSame (net.host.working, truth) && live169bSame (net.host.replica->confirmed(), truth);
        for (auto& g : net.guests)
            ok = ok && live169bSame (g.working, truth) && live169bSame (g.replica->confirmed(), truth) && g.replica->pending() == 0
                 && g.replica->revision() == net.authority->revision();
        converged += ok ? 1 : 0;
        rejectedTotal += net.rejected;
        commits += (int) net.authority->revision();
        // every guest still owns what he made (no edit of one guest landed in another's area)
        for (const auto& c : truth.channels)
            CHECK (uids::tagOf (c.uid) >= 1 && uids::tagOf (c.uid) <= 4);
    }
    CHECK (converged == seeds);
    CHECK (commits > 100 && rejectedTotal > 0); // (the scripts did edit, and some edits were refused)
    std::printf ("  live169b: %d/%d sessions converged, %d commits, %d refused edits\n", converged, seeds, commits, rejectedTotal);
}

TEST (live169b_codec_rounding_is_adopted)
{
    using namespace lynx;
    // a serializer that rounds (like the .lynx JSON does for clip gains): every copy ends with the rounded value and
    // the sender does not keep re-sending its own unrounded one
    live::Codec rounding = [] (const live::Patch& p) {
        auto q = p;
        for (auto& c : q.carrier.channels)
            c.volume = std::round (c.volume * 1000.0f) / 1000.0f;
        return q;
    };
    std::mt19937 rng (7);
    Live169bNet net (live169bSong(), 1, rounding);
    auto& w = net.guests[0].working;
    ChannelDef c;
    c.name = "Ana";
    c.volume = 0.123456f;
    w.channels.push_back (c);
    live169bPublish (w);
    net.settle (rng);
    const auto ana = (std::size_t) live169bChannel (net.authority->confirmed(), w.channels.back().uid);
    CHECK (net.authority->confirmed().channels[ana].volume == std::round (0.123456f * 1000.0f) / 1000.0f);
    CHECK (w.channels.back().volume == net.authority->confirmed().channels[ana].volume);
    CHECK (! net.guests[0].replica->poll (w).has_value());
    CHECK (live169bSame (net.host.working, w));
}

TEST (live169b_commits_wait_while_recording)
{
    using namespace lynx;
    std::mt19937 rng (3);
    Live169bNet net (live169bSong(), 1);
    auto& guest = net.guests[0];
    guest.holding = true; // Ana records: her playback must not change under her
    const auto before = guest.working;
    net.host.working.bpm = 150.0;
    net.host.working.patterns[0].name = "Verse";
    live169bPublish (net.host.working);
    net.poll (0);
    net.settle (rng);
    CHECK (guest.replica->waiting() == 1 && live169bSame (guest.working, before));
    // her own edit during the take still goes out (and its answer waits too)
    guest.working.channels.push_back (ChannelDef {});
    live169bPublish (guest.working);
    net.settle (rng);
    CHECK (guest.replica->pending() == 1 && net.authority->confirmed().channels.size() == 4);
    guest.holding = false;
    auto u = guest.replica->release (guest.working);
    CHECK (u.working.has_value());
    guest.working = *u.working;
    CHECK (guest.working.bpm == 150.0 && guest.working.patterns[0].name == "Verse" && guest.working.channels.size() == 4);
    CHECK (guest.replica->waiting() == 0 && guest.replica->pending() == 0 && live169bSame (guest.working, net.authority->confirmed()));
}

TEST (live169b_undo_per_user)
{
    using namespace lynx;
    std::mt19937 rng (5);
    Live169bNet net (live169bSong(), 1);
    auto& host = net.host.working;
    auto& ana = net.guests[0].working;
    ChannelDef c;
    c.name = "Ana's pad";
    ana.channels.push_back (c);
    live169bPublish (ana);
    net.settle (rng);

    // the host's step: a note of his Kick and the pattern's name
    const auto before = host;
    Note n;
    n.channel = 0;
    n.start = 1920;
    host.patterns[0].notes.push_back (n);
    host.patterns[1].name = "Host's fill";
    live169bPublish (host);
    const auto after = host;
    net.settle (rng);
    // then Ana adds notes of her channel to the same pattern, and renames... nothing of the host's (she can't)
    Note m;
    m.channel = live169bChannel (ana, ana.channels.back().uid);
    m.start = 480;
    ana.patterns[0].notes.push_back (m);
    live169bPublish (ana);
    net.settle (rng);
    const auto anaNotes = [&] (const Song& s) {
        const auto ch = live169bChannel (s, ana.channels.back().uid);
        return std::count_if (s.patterns[0].notes.begin(), s.patterns[0].notes.end(), [&] (const Note& k) { return k.channel == ch; });
    };
    CHECK (anaNotes (host) == 1);

    // the host undoes his step: his note and his name go, Ana's note stays
    std::vector<live::Key> skipped;
    host = live::selectiveRevert (host, before, after, &skipped);
    CHECK (skipped.empty() && host.patterns[1].name == before.patterns[1].name && anaNotes (host) == 1);
    CHECK (std::none_of (host.patterns[0].notes.begin(), host.patterns[0].notes.end(), [] (const Note& k) { return k.channel == 0 && k.start == 1920; }));
    net.settle (rng);
    CHECK (live169bSame (ana, host) && anaNotes (ana) == 1);
    // redo puts them back
    const auto undone = host;
    host = live::selectiveRevert (host, after, before, &skipped);
    CHECK (skipped.empty() && host.patterns[1].name == "Host's fill");
    net.settle (rng);
    CHECK (live169bSame (ana, host));

    // an entity someone else changed since is kept, and named
    const auto b2 = host;
    host.patterns[1].name = "Mine again";
    live169bPublish (host);
    const auto a2 = host;
    net.settle (rng);
    auto other = host;
    other.patterns[1].name = "Changed by someone";
    std::vector<live::Key> skipped2;
    const auto r = live::selectiveRevert (other, b2, a2, &skipped2);
    CHECK (skipped2.size() == 1 && skipped2[0].kind == live::Kind::pattern && r.patterns[1].name == "Changed by someone");
    (void) undone;
}

TEST (live169b_invite_code_and_encryption)
{
    using namespace lynx::livecrypto;
    const Invite invite { "2syq75e66t", newSecret() };
    CHECK (invite.secret.size() == 16 && newSecret() != invite.secret);
    const auto code = formatCode (invite);
    CHECK (code.size() == 36 && code.rfind ("LYNX-2SYQ7-5E66T-", 0) == 0);
    auto parsed = parseCode (code);
    CHECK (parsed && parsed->room == invite.room && parsed->secret == invite.secret);
    // pasted with spaces, lower case, inside a link
    auto loose = code;
    for (auto& ch : loose)
        ch = ch == '-' ? ' ' : (char) std::tolower ((unsigned char) ch);
    CHECK (parseCode (loose) && parseCode (loose)->secret == invite.secret);
    CHECK (parseCode ("https://lynx-share.com/join#" + code) && parseCode ("https://lynx-share.com/join#" + code)->room == invite.room);
    CHECK (! parseCode ("LYNX-ABC") && ! parseCode (code + "X") && ! parseCode ("LYNX-2SYQ7-5E66T-!!!!-AAAA-AAAA-AAAA"));

    const auto keys = derive (invite);
    const auto again = derive (*parsed);
    CHECK (keys.key == again.key && keys.relayToken == again.relayToken && keys.relayToken.size() == 64);
    auto otherInvite = invite;
    otherInvite.secret[0] = otherInvite.secret[0] == 'a' ? 'b' : 'a';
    const auto other = derive (otherInvite);
    CHECK (other.key != keys.key && other.relayToken != keys.relayToken);
    // the relay token tells nothing about the key (different domains of the same Argon2 result)
    CHECK (std::string (keys.key.begin(), keys.key.end()).find (keys.relayToken.substr (0, 8)) == std::string::npos);

    Sealer host (keys.key), guest (keys.key);
    Opener rx (keys.key);
    const std::vector<std::uint8_t> message { 'h', 'e', 'l', 'l', 'o', 0, 1, 2 };
    const auto frame = host.seal (message);
    CHECK (frame.size() == message.size() + overhead);
    std::uint64_t sender = 0;
    auto opened = rx.open (frame, &sender);
    CHECK (opened && *opened == message && sender == host.id() && host.id() != guest.id());
    CHECK (! rx.open (frame)); // replayed
    auto tampered = host.seal (message);
    tampered[tampered.size() - 1] ^= 1;
    CHECK (! rx.open (tampered));
    Opener wrong (other.key);
    CHECK (! wrong.open (guest.seal (message)));
    // two senders are numbered apart; an empty message is fine
    CHECK (rx.open (guest.seal (message)) && rx.open (host.seal ({})) && rx.open (host.seal ({}))->empty());
    // nothing of the plain text is visible in the frame
    const auto big = std::vector<std::uint8_t> (200, 'A');
    const auto sealed = host.seal (big);
    CHECK (std::search (sealed.begin(), sealed.end(), big.begin(), big.begin() + 16) == sealed.end());
}
