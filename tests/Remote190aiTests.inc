// Round 190ai: Online Sessions P5-A1, remote plugins (docs/design/ONLINE_SESSIONS.md "P5-A1 as built",
// core/RemotePlugins.h): a guest uses a plugin only the host has. What the host tells guests about a shared plugin, the
// parameter values on a PluginRef, what the host accepts from a guest (never a state), who renders the part, the
// rate limit, the editor's outbox, permissions and what is left when the session ends. No network: the in-process
// network of round 169b (Live169bNet); its codec here is what the host does to every proposal (remoteplug::sanitize).
#include "core/LiveSession.h"
#include "core/RemotePlugins.h"
#include "core/SessionIds.h"
#include "core/SessionParts.h"
#include "core/StandIns.h"

namespace
{
lynx::remoteplug::Shared remote190aiSynth()
{
    lynx::remoteplug::Shared s;
    s.id = "VST3-HostSynth-1a2b3c-4d5e6f";
    s.name = "HostSynth";
    s.vendor = "Somebody";
    s.format = "VST3";
    s.instrument = true;
    s.params = { { 0, "Cutoff", "Hz", "20 Hz", "20 kHz", 0.5f, 0, {} }, { 1, "Wave", "", "Sine", "Square", 0.0f, 3, { "Sine", "Saw", "Square" } } };
    return s;
}

// what the host does to a guest's proposal before its authority judges it
lynx::live::Codec remote190aiHostRules()
{
    return [] (const lynx::live::Patch& p) {
        auto q = p;
        lynx::remoteplug::sanitize (q.carrier, lynx::session::hostTag);
        return q;
    };
}

// Ana (the first guest) puts the host's shared plugin on a new channel of hers; returns its uid
std::uint64_t remote190aiAddChannel (lynx::Song& working, const lynx::remoteplug::Shared& plugin, const std::string& name)
{
    lynx::ChannelDef c;
    c.name = name;
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin = lynx::remoteplug::makeRef (plugin, lynx::session::hostTag);
    const auto* me = working.session.find (working.session.me);
    c.mixerTrack = me != nullptr ? me->insertFirst : 0;
    working.channels.push_back (c);
    live169bPublish (working);
    return working.channels.back().uid;
}

const lynx::PluginRef* remote190aiRef (const lynx::Song& s, std::uint64_t uid)
{
    return lynx::remoteplug::refAt (s, { { lynx::live::Kind::channel, uid, 0 }, -1 });
}
} // namespace

TEST (remote190ai_values_and_what_arrives_are_bounded)
{
    using namespace lynx;
    PluginRef ref;
    CHECK (remoteplug::setValue (ref, 7, 0.25f) && remoteplug::setValue (ref, 2, 2.0f) && remoteplug::setValue (ref, 5, -1.0f));
    CHECK (! remoteplug::setValue (ref, 7, 0.25f)); // (nothing changed)
    CHECK (! remoteplug::setValue (ref, -1, 0.5f) && ! remoteplug::setValue (ref, 1 << 20, 0.5f));
    CHECK (ref.values == (std::vector<std::pair<int, float>> { { 2, 1.0f }, { 5, 0.0f }, { 7, 0.25f } })); // sorted, 0..1
    CHECK (remoteplug::valueOf (ref, 7, 0.9f) == 0.25f && remoteplug::valueOf (ref, 3, 0.9f) == 0.9f);
    CHECK (remoteplug::setValue (ref, 7, std::nanf ("")) && remoteplug::valueOf (ref, 7, 0.9f) == 0.0f); // (never a NaN)

    ref.values = { { 9, 0.5f }, { 3, 7.0f }, { 9, 0.75f }, { -4, 0.1f }, { 1 << 21, 0.1f }, { 1, std::nanf ("") } };
    remoteplug::cleanValues (ref);
    CHECK (ref.values == (std::vector<std::pair<int, float>> { { 1, 0.0f }, { 3, 1.0f }, { 9, 0.75f } }));
    for (int i = 0; i < 5000; ++i)
        ref.values.push_back ({ i + 10, 0.5f });
    remoteplug::cleanValues (ref);
    CHECK (ref.values.size() == remoteplug::maxParams);

    // a shared plugin's description as it arrives: sizes, counts, duplicate indices, control characters
    auto s = remote190aiSynth();
    s.name = std::string (300, 'n') + "\n\t";
    s.params[0].name = "Cut\x01off";
    s.params[0].def = 9.0f;
    s.params.push_back (s.params[0]);          // the same index twice
    s.params.push_back ({ -3, "bad", "", "", "", 0.0f, 0, {} });
    s.params.push_back ({ 40, "Steps without names", "", "", "", 0.0f, 4, { "a", "b" } });
    remoteplug::clean (s);
    CHECK (s.name.size() == remoteplug::maxText && s.params.size() == 3);
    CHECK (s.params[0].name == "Cutoff" && s.params[0].def == 1.0f && s.params[2].steps == 4 && s.params[2].stepNames.empty());
    std::vector<remoteplug::Shared> list (40, remote190aiSynth());
    remoteplug::clean (list);
    CHECK (list.size() == 1); // (one per plugin id)
    for (int i = 0; i < 40; ++i)
    {
        list.push_back (remote190aiSynth());
        list.back().id += std::to_string (i);
    }
    remoteplug::clean (list);
    CHECK (list.size() == remoteplug::maxShared && remoteplug::find (list, remote190aiSynth().id) != nullptr && remoteplug::find (list, "nope") == nullptr);

    const auto made = remoteplug::makeRef (remote190aiSynth(), session::hostTag);
    CHECK (made.id == remote190aiSynth().id && made.name == "HostSynth" && made.playedBy == session::hostTag && made.state.empty() && made.values.empty());
}

TEST (remote190ai_a_guest_uses_the_hosts_plugin_and_the_host_renders_it)
{
    using namespace lynx;
    std::mt19937 rng (190);
    Live169bNet net (live169bSong(), 2, remote190aiHostRules());
    auto& ana = net.guests[0];
    const auto plugin = remote190aiSynth();
    const auto uid = remote190aiAddChannel (ana.working, plugin, "Ana lead");
    // her notes for it, in the host's pattern (the notes of her channel are hers)
    Note n;
    n.channel = (int) ana.working.channels.size() - 1;
    n.key = 60;
    ana.working.patterns[0].notes.push_back (n);
    net.poll (1);
    net.settle (rng);
    const auto& confirmed = net.authority->confirmed();
    const auto* ref = remote190aiRef (confirmed, uid);
    CHECK (ref != nullptr && uids::tagOf (uid) == 2 && net.rejected == 0);
    if (ref == nullptr)
        return;
    CHECK (ref->id == plugin.id && ref->playedBy == session::hostTag && ref->state.empty());
    CHECK (live169bSame (confirmed, net.host.working) && live169bSame (confirmed, ana.working) && live169bSame (confirmed, net.guests[1].working));

    // who plays it: the host has the plugin, the guests do not (and would not load it if they had: it is the host's)
    auto hostMissing = [] (const PluginRef&) { return false; };
    auto guestMissing = [] (const PluginRef&) { return true; };
    const live::Key key { live::Kind::channel, uid, 0 };
    const auto needs = live::unplayable (confirmed, guestMissing);
    CHECK (needs == std::set<live::Key> { key } && live::unplayable (confirmed, hostMissing).empty());
    auto isShared = [&] (const std::string& id) { return id == plugin.id; };
    auto notShared = [] (const std::string&) { return false; };
    // round 169j's rule (the owner renders) finds nobody: it is Ana's part and Ana cannot play it
    CHECK (live::toRender (confirmed, needs, 1, hostMissing).empty() && live::toRender (confirmed, needs, 2, guestMissing).empty());
    // P5-A1: the host renders it for her, while it shares the plugin; nobody else does
    CHECK (remoteplug::toRender (confirmed, needs, 1, hostMissing, isShared) == std::vector<live::Key> { key });
    CHECK (remoteplug::toRender (confirmed, needs, 1, hostMissing, notShared).empty());       // un-shared: the last stems stay
    CHECK (remoteplug::toRender (confirmed, needs, 1, guestMissing, isShared).empty());       // (the host lost the plugin)
    CHECK (remoteplug::toRender (confirmed, needs, 2, guestMissing, isShared).empty() && remoteplug::toRender (confirmed, needs, 3, guestMissing, isShared).empty());
    CHECK (remoteplug::toRender (confirmed, {}, 1, hostMissing, isShared).empty());           // nobody needs it: nothing
    // a plugin of Ana's own that the host happens to have too is not the host's to render (playedBy 0)
    {
        auto other = confirmed;
        remoteplug::refAt (other, { key, -1 })->playedBy = 0;
        CHECK (remoteplug::toRender (other, needs, 1, hostMissing, isShared).empty());
    }
    // the host's "used by" list
    const auto uses = remoteplug::uses (confirmed, session::hostTag);
    CHECK (uses.size() == 1 && uses[0].owner == 2 && uses[0].id == plugin.id && uses[0].place.part == key && uses[0].place.slot == -1);

    // she turns a knob: a value on her channel, to everyone; the host's stems (its edit of her channel) reach her
    remoteplug::setValue (*remoteplug::refAt (ana.working, { key, -1 }), 0, 0.8f);
    net.poll (1);
    net.settle (rng);
    CHECK (remoteplug::valueOf (*remote190aiRef (net.authority->confirmed(), uid), 0, -1.0f) == 0.8f);
    StandIn stem;
    stem.pattern = net.host.working.patterns[0].uid;
    stem.path = "/host/stems/ana lead.wav";
    stem.sourceHash = "abc";
    stem.by = "Host";
    net.host.working.channels[(std::size_t) live169bChannel (net.host.working, uid)].standIns = { stem };
    net.poll (0);
    net.settle (rng);
    const auto c = live169bChannel (ana.working, uid);
    CHECK (c >= 0 && ana.working.channels[(std::size_t) c].standIns.size() == 1 && ana.working.channels[(std::size_t) c].standIns[0].by == "Host");
    CHECK (remoteplug::valueOf (ana.working.channels[(std::size_t) c].plugin, 0, -1.0f) == 0.8f && net.rejected == 0);

    // an effect of the host's on an insert of hers works the same way
    const auto anaInsert = ana.working.session.find (2)->insertFirst;
    auto fx = plugin;
    fx.id = "VST3-HostVerb-9";
    fx.instrument = false;
    EffectSlot slot;
    slot.kind = EffectKind::plugin;
    slot.plugin = remoteplug::makeRef (fx, session::hostTag);
    if ((int) ana.working.mixer.size() <= anaInsert)
        ana.working.mixer.resize ((std::size_t) anaInsert + 1);
    ana.working.mixer[(std::size_t) anaInsert].effects.push_back (slot);
    live169bPublish (ana.working);
    net.poll (1);
    net.settle (rng);
    const live::Key insertKey { live::Kind::insert, (std::uint64_t) anaInsert, 0 };
    const auto& after = net.authority->confirmed();
    const auto needs2 = live::unplayable (after, guestMissing);
    CHECK (needs2.count (insertKey) == 1 && net.rejected == 0);
    auto bothShared = [&] (const std::string& id) { return id == plugin.id || id == fx.id; };
    const auto both = remoteplug::toRender (after, needs2, 1, hostMissing, bothShared);
    CHECK (both.size() == 2 && std::count (both.begin(), both.end(), insertKey) == 1);
    CHECK (remoteplug::toRender (after, needs2, 1, hostMissing, isShared) == std::vector<live::Key> { key }); // (the effect is not shared)
    const auto* fxRef = remoteplug::refAt (after, { insertKey, 0 });
    CHECK (fxRef != nullptr && fxRef->playedBy == session::hostTag && remoteplug::uses (after, session::hostTag).size() == 2);
}

TEST (remote190ai_the_state_never_crosses_and_only_the_host_plays)
{
    using namespace lynx;
    // the rule itself
    Song carrier = live169bSong();
    ChannelDef hosted;
    hosted.instrument = InstrumentKind::plugin;
    hosted.plugin = remoteplug::makeRef (remote190aiSynth(), 3); // "played by guest 3": only the host can
    hosted.plugin.state = "QUJDRA==";                              // a state blob from a guest
    hosted.plugin.foreign.format = ForeignPlugin::Format::vst3;
    hosted.plugin.values = { { 4, 0.5f }, { 4, 0.75f }, { 2, 9.0f } };
    ChannelDef own;
    own.instrument = InstrumentKind::plugin;
    own.plugin.id = "VST3-Mine-1";
    own.plugin.state = "c3RhdGU=";       // an ordinary plugin's state travels as before (P2)
    own.plugin.values = { { 1, 0.5f } }; // (values mean nothing there)
    carrier.channels.push_back (hosted);
    carrier.channels.push_back (own);
    live169bPublish (carrier);
    remoteplug::sanitize (carrier, session::hostTag);
    const auto& h = carrier.channels[carrier.channels.size() - 2].plugin;
    const auto& o = carrier.channels.back().plugin;
    CHECK (h.playedBy == session::hostTag && h.state.empty() && h.foreign.format == ForeignPlugin::Format::none);
    CHECK (h.values == (std::vector<std::pair<int, float>> { { 2, 1.0f }, { 4, 0.75f } }));
    CHECK (o.playedBy == 0 && o.state == "c3RhdGU=" && o.values.empty());

    // through a session: a guest's computer that sends a state for the host's plugin anyway
    std::mt19937 rng (7);
    Live169bNet net (live169bSong(), 1, remote190aiHostRules());
    auto& ana = net.guests[0];
    const auto uid = remote190aiAddChannel (ana.working, remote190aiSynth(), "Ana lead");
    auto* ref = remoteplug::refAt (ana.working, { { live::Kind::channel, uid, 0 }, -1 });
    ref->state = "QUJDRA==";
    ref->playedBy = 2;
    net.poll (1);
    net.settle (rng);
    const auto* confirmed = remote190aiRef (net.authority->confirmed(), uid);
    CHECK (confirmed != nullptr && confirmed->state.empty() && confirmed->playedBy == session::hostTag);
    // (her own copy adopts the host's form, like any rounding)
    CHECK (remote190aiRef (ana.working, uid)->state.empty() && remote190aiRef (ana.working, uid)->playedBy == session::hostTag);
    CHECK (live169bSame (net.authority->confirmed(), ana.working));
}

TEST (remote190ai_permissions_are_the_sessions)
{
    using namespace lynx;
    std::mt19937 rng (11);
    Live169bNet net (live169bSong(), 2, remote190aiHostRules());
    auto& ana = net.guests[0];
    auto& pedro = net.guests[1];
    const auto plugin = remote190aiSynth();
    // a guest cannot turn the host's channel into a hosted plugin
    ana.working.channels[0].instrument = InstrumentKind::plugin;
    ana.working.channels[0].plugin = remoteplug::makeRef (plugin, session::hostTag);
    net.poll (1);
    net.settle (rng);
    CHECK (net.authority->confirmed().channels[0].instrument == InstrumentKind::synth && ana.working.channels[0].instrument == InstrumentKind::synth);
    // her own channel: fine
    const auto uid = remote190aiAddChannel (ana.working, plugin, "Ana lead");
    net.poll (1);
    net.settle (rng);
    CHECK (remote190aiRef (net.authority->confirmed(), uid) != nullptr);
    // Pedro cannot turn Ana's knobs …
    const remoteplug::Place place { { live::Kind::channel, uid, 0 }, -1 };
    remoteplug::setValue (*remoteplug::refAt (pedro.working, place), 0, 0.9f);
    net.poll (2);
    net.settle (rng);
    CHECK (remote190aiRef (net.authority->confirmed(), uid)->values.empty() && remote190aiRef (pedro.working, uid)->values.empty());
    // … unless the host lets him edit everything
    live::Permissions all;
    all.editEverything = true;
    net.authority->setPermissions (3, all);
    remoteplug::setValue (*remoteplug::refAt (pedro.working, place), 0, 0.9f);
    net.poll (2);
    net.settle (rng);
    CHECK (remoteplug::valueOf (*remote190aiRef (net.authority->confirmed(), uid), 0, -1.0f) == 0.9f);
    CHECK (remoteplug::valueOf (*remote190aiRef (ana.working, uid), 0, -1.0f) == 0.9f);
    // "listen only": no hosted plugin at all (nothing of theirs is accepted)
    live::Permissions listen;
    listen.listener = true;
    net.authority->setPermissions (2, listen);
    const auto before = net.authority->confirmed().channels.size();
    remote190aiAddChannel (ana.working, plugin, "Ana second");
    remoteplug::setValue (*remoteplug::refAt (ana.working, place), 1, 0.5f);
    net.poll (1);
    net.settle (rng);
    CHECK (net.authority->confirmed().channels.size() == before && remoteplug::valueOf (*remote190aiRef (net.authority->confirmed(), uid), 1, -1.0f) == -1.0f);
    // the host is the authority: what it sets (its plugin's own window) reaches everyone
    remoteplug::refAt (net.host.working, place)->values = { { 0, 0.1f }, { 1, 1.0f } };
    net.poll (0);
    net.settle (rng);
    CHECK (remote190aiRef (pedro.working, uid)->values == (std::vector<std::pair<int, float>> { { 0, 0.1f }, { 1, 1.0f } }));
}

TEST (remote190ai_parameter_edits_are_rate_limited)
{
    using namespace lynx;
    remoteplug::Limiter limiter (10.0, 20.0); // 10 a second, 20 at once
    CHECK (limiter.allow (2, 0.0, 0) && limiter.allow (2, 0.0, 15) && limiter.allow (2, 0.0, 5));
    CHECK (! limiter.allow (2, 0.0, 1) && ! limiter.allow (2, 50.0, 1)); // empty; 50 ms later still under one
    CHECK (limiter.allow (2, 150.0, 1));                                 // refilled: one
    CHECK (limiter.allow (3, 150.0, 20) && ! limiter.allow (3, 150.0, 1)); // each guest has their own
    CHECK (limiter.allow (2, 60000.0, 20) && ! limiter.allow (2, 60000.0, 1)); // never more than the burst
    CHECK (! limiter.allow (2, 60500.0, 6) && limiter.allow (2, 60500.0, 5));  // (a refused one costs nothing)

    // what counts: values of hosted plugins that differ from the confirmed song
    Song confirmed = live169bSong();
    confirmed.session.me = 2;
    const auto uid = remote190aiAddChannel (confirmed, remote190aiSynth(), "Ana lead");
    const remoteplug::Place place { { live::Kind::channel, uid, 0 }, -1 };
    auto working = confirmed;
    auto& ref = *remoteplug::refAt (working, place);
    remoteplug::setValue (ref, 0, 0.3f);
    remoteplug::setValue (ref, 1, 0.6f);
    working.channels[0].name = "renamed"; // (not a value)
    CHECK (remoteplug::valueEdits (confirmed, live::diff (confirmed, working)) == 2);
    auto again = working;
    remoteplug::setValue (*remoteplug::refAt (again, place), 1, 0.7f);
    CHECK (remoteplug::valueEdits (working, live::diff (working, again)) == 1);
    again.channels[1].name = "also renamed";
    CHECK (remoteplug::valueEdits (again, live::diff (working, again)) == 0); // nothing new against what is confirmed
    // a new hosted channel counts what it starts with; an ordinary plugin never counts
    Song empty = live169bSong();
    CHECK (remoteplug::valueEdits (empty, live::diff (empty, working)) == 2);
    remoteplug::refAt (again, place)->playedBy = 0;
    CHECK (remoteplug::valueEdits (confirmed, live::diff (confirmed, again)) == 0);
}

TEST (remote190ai_a_tweak_survives_the_stems_landing)
{
    using namespace lynx;
    std::mt19937 rng (3);
    Live169bNet net (live169bSong(), 1, remote190aiHostRules());
    auto& ana = net.guests[0];
    const auto uid = remote190aiAddChannel (ana.working, remote190aiSynth(), "Ana lead");
    net.poll (1);
    net.settle (rng);
    const remoteplug::Place place { { live::Kind::channel, uid, 0 }, -1 };

    // the editor's outbox: several moves of a slider become one write, a few times a second
    remoteplug::Outbox outbox (200.0, 700.0, 5);
    outbox.set (0, 0.2f, 0.0);
    outbox.set (0, 0.4f, 10.0);
    outbox.set (1, 1.0f, 20.0);
    CHECK (outbox.showing (0) == 0.4f && ! outbox.showing (5).has_value());
    auto write = [&] (double now) {
        const auto due = outbox.due (*remoteplug::refAt (ana.working, place), now);
        for (const auto& [i, v] : due)
            remoteplug::setValue (*remoteplug::refAt (ana.working, place), i, v);
        return (int) due.size();
    };
    CHECK (write (30.0) == 2 && write (40.0) == 0);
    outbox.set (0, 0.5f, 100.0);
    CHECK (write (120.0) == 0); // (too soon after the last write)

    // meanwhile the host's render of her channel lands: an edit of the same channel, confirmed before her write
    // arrives — her write is refused as a conflict and undone on her side
    StandIn stem;
    stem.pattern = net.host.working.patterns[0].uid;
    stem.path = "/host/stems/a.wav";
    net.host.working.channels[(std::size_t) live169bChannel (net.host.working, uid)].standIns = { stem };
    net.poll (0); // (the host's commit is made now; hers was based on the revision before it)
    net.poll (1);
    net.settle (rng);
    CHECK (net.rejected >= 1);
    CHECK (remote190aiRef (net.authority->confirmed(), uid)->values.empty() && remote190aiRef (ana.working, uid)->values.empty());
    CHECK (ana.working.channels[(std::size_t) live169bChannel (ana.working, uid)].standIns.size() == 1);
    // the outbox still wants them: it writes again, and this time they stay
    CHECK (outbox.showing (0) == 0.5f && write (400.0) == 1); // (the move not written yet goes; the undone one waits)
    CHECK (write (800.0) == 1);                               // (its retry time)
    net.poll (1);
    net.settle (rng);
    const auto* confirmed = remote190aiRef (net.authority->confirmed(), uid);
    CHECK (confirmed->values == (std::vector<std::pair<int, float>> { { 0, 0.5f }, { 1, 1.0f } }));
    CHECK (net.authority->confirmed().channels[(std::size_t) live169bChannel (net.authority->confirmed(), uid)].standIns.size() == 1);
    // arrived and stayed: forgotten, the slider shows the song's value again
    CHECK (write (1600.0) == 0 && write (2400.0) == 0 && outbox.empty() && ! outbox.showing (0).has_value());
    // a value the session keeps refusing is given up after a few tries (the session's value stands)
    remoteplug::Outbox stubborn (200.0, 700.0, 3);
    stubborn.set (0, 0.9f, 0.0);
    PluginRef refused; // (never takes the value)
    int writes = 0;
    for (double t = 0.0; t < 20000.0; t += 100.0)
        writes += (int) stubborn.due (refused, t).size();
    CHECK (writes == 3 && stubborn.empty());
}

TEST (remote190ai_the_end_of_the_session_leaves_the_stems_only)
{
    using namespace lynx;
    std::mt19937 rng (5);
    Live169bNet net (live169bSong(), 1, remote190aiHostRules());
    auto& ana = net.guests[0];
    const auto uid = remote190aiAddChannel (ana.working, remote190aiSynth(), "Ana lead");
    const remoteplug::Place place { { live::Kind::channel, uid, 0 }, -1 };
    remoteplug::setValue (*remoteplug::refAt (ana.working, place), 0, 0.8f);
    net.poll (1);
    net.settle (rng);
    StandIn stem;
    stem.pattern = net.host.working.patterns[0].uid;
    stem.path = "/media/ana lead.wav";
    stem.by = "Host";
    net.host.working.channels[(std::size_t) live169bChannel (net.host.working, uid)].standIns = { stem };
    net.poll (0);
    net.settle (rng);

    // the guest leaves with their own part (the keep policy): the channel, its notes and the audio stand-ins — no
    // state (there never was one here), and nothing that says "ask the host" any more
    auto kept = parts::keepOnlyPart (ana.working);
    remoteplug::endSession (kept);
    const auto c = live169bChannel (kept, uid);
    CHECK (c >= 0 && kept.channels.size() == 1);
    if (c >= 0)
    {
        const auto& channel = kept.channels[(std::size_t) c];
        CHECK (channel.standIns.size() == 1 && channel.standIns[0].path == "/media/ana lead.wav");
        CHECK (channel.plugin.playedBy == 0 && channel.plugin.values.empty() && channel.plugin.state.empty() && channel.plugin.id == remote190aiSynth().id);
        // (the plugin is not on their computer: the stems play)
        const auto projected = standins::project (kept, [] (const PluginRef&) { return true; });
        CHECK (standins::hasAny (kept) && ! (projected.channels == kept.channels));
    }
    // the host's project: the marker goes; the values stay until the plugin's real state is captured (then they go)
    auto hostSong = net.host.working;
    remoteplug::endSession (hostSong, false);
    CHECK (remote190aiRef (hostSong, uid)->playedBy == 0 && remoteplug::valueOf (*remote190aiRef (hostSong, uid), 0, -1.0f) == 0.8f);
    remoteplug::endSession (hostSong, true);
    CHECK (remote190aiRef (hostSong, uid)->values.empty() && hostSong.channels[(std::size_t) live169bChannel (hostSong, uid)].standIns.size() == 1);
    // a later session of the same song starts clean: the old host tag means nothing
    CHECK (remoteplug::uses (hostSong, session::hostTag).empty());
}
