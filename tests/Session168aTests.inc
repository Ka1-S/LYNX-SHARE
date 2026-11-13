// Round 168a: Online Sessions phase 1 (docs/design/ONLINE_SESSIONS.md) — stable ids, the index-reference visitor, a
// guest's part cut out and merged back, and stand-ins (renders of third-party plugin parts) that play like the part.
#include "core/SessionIds.h"
#include "core/SessionParts.h"
#include "core/StandIns.h"
#include "core/Sha256.h"

namespace
{
lynx::ChannelDef session168aChannel (const std::string& name, int insert)
{
    lynx::ChannelDef c;
    c.name = name;
    c.instrument = lynx::InstrumentKind::synth;
    c.synth = {};
    c.synth.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    c.synth.cutoff = 20000.0f;
    c.mixerTrack = insert;
    return c;
}

lynx::Note session168aNote (int channel, std::int64_t start, std::int64_t length, int key)
{
    lynx::Note n;
    n.channel = channel;
    n.start = start;
    n.length = length;
    n.key = key;
    return n;
}

// the host's song: two channels, two patterns, three clips, insert 1 used
lynx::Song session168aHostSong()
{
    auto s = lynx::Song::makeDefault();
    s.channels.clear();
    s.patterns.clear();
    s.playlist.clear();
    s.channels.push_back (session168aChannel ("Kick", 1));
    s.channels.push_back (session168aChannel ("Bass", 2));
    s.patterns.emplace_back().name = "Beat";
    s.patterns[0].notes.push_back (session168aNote (0, 0, 240, 36));
    s.patterns[0].notes.push_back (session168aNote (1, 960, 480, 40));
    s.patterns.emplace_back().name = "Fill";
    s.patterns[1].notes.push_back (session168aNote (0, 0, 240, 38));
    for (int i = 0; i < 2; ++i)
    {
        lynx::Clip c;
        c.pattern = i;
        c.track = 0;
        c.start = i * 3840;
        c.length = 3840;
        s.playlist.push_back (c);
    }
    s.mixer[1].name = "Kick bus";
    lynx::uids::normalize (s);
    return s;
}
} // namespace

TEST (session168a_uids_normalize)
{
    auto s = session168aHostSong();
    CHECK (s.projectId.size() == 32);
    for (const auto& c : s.channels)
        CHECK (c.uid != 0 && lynx::uids::tagOf (c.uid) == lynx::session::hostTag);
    for (const auto& c : s.playlist)
        CHECK (c.uid != 0);
    CHECK (s.patterns[0].uid != s.patterns[1].uid && s.arrangements[0].uid != 0);
    // idempotent
    const auto before = s;
    CHECK (! lynx::uids::normalize (s));
    CHECK (s.channels == before.channels && s.projectId == before.projectId && s.nextUid == before.nextUid);

    // a duplicated channel (a copy keeps the uid): the LATER one is renamed, the original keeps its own
    const auto kick = s.channels[0].uid;
    s.channels.push_back (s.channels[0]);
    CHECK (lynx::uids::normalize (s));
    CHECK (s.channels[0].uid == kick && s.channels[2].uid != kick && s.channels[2].uid != 0);
    // the same for clips across arrangements (a copied arrangement)
    s.addArrangement ("Copy", true);
    CHECK (lynx::uids::normalize (s));
    std::vector<std::uint64_t> clipUids;
    s.forEachClipList ([&] (const std::vector<lynx::Clip>& list) {
        for (const auto& c : list)
            clipUids.push_back (c.uid);
    });
    std::sort (clipUids.begin(), clipUids.end());
    CHECK (std::adjacent_find (clipUids.begin(), clipUids.end()) == clipUids.end());
    CHECK (std::find (clipUids.begin(), clipUids.end(), 0) == clipUids.end());

    // a guest's new things carry his tag; the counter never hands out a uid that exists (a lost nextUid)
    auto g = lynx::parts::guestCopy (s, 2);
    g.nextUid = 0;
    g.channels.push_back (session168aChannel ("Guitar", 0));
    lynx::uids::normalize (g);
    CHECK (lynx::uids::tagOf (g.channels.back().uid) == 2);
    auto h = s;
    h.nextUid = 0;
    h.channels.push_back (session168aChannel ("Pad", 0));
    lynx::uids::normalize (h);
    std::vector<std::uint64_t> ids;
    for (const auto& c : h.channels)
        ids.push_back (c.uid);
    std::sort (ids.begin(), ids.end());
    CHECK (std::adjacent_find (ids.begin(), ids.end()) == ids.end());

    // hex form round trip
    CHECK (lynx::uids::fromHex (lynx::uids::toHex (0x0000020000000abcull)) == 0x0000020000000abcull);
    CHECK (lynx::uids::fromHex ("xyz") == 0 && lynx::uids::fromHex ("") == 0);
}

TEST (session168a_every_index_reference_is_visited)
{
    // every field that refers to a channel is set to 5: the visitor must reach each one (a field added later and not
    // visited makes this fail when it is added here)
    lynx::Song s;
    s.channels.resize (6);
    s.patterns.resize (1);
    s.patterns[0].notes.push_back (session168aNote (5, 0, 10, 60));
    s.patterns[0].channelLoops.push_back ({ 5, -1 });
    s.patterns[0].events.push_back ({ { lynx::AutomationTargetKind::channelPan, 5, 0, 0 }, {} });
    lynx::Clip audio;
    audio.kind = lynx::ClipKind::audio;
    audio.channel = 5;
    s.playlist.push_back (audio);
    s.playlistTracks.resize (1);
    s.playlistTracks[0].channel = 5;
    s.channels[0].layer.children = { 5 };
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::channelVolume, 5, 0, 0 };
    a.source.channel = 5;
    s.automations.push_back (a);
    s.midiLinks.push_back ({ 1, -1, { lynx::AutomationTargetKind::channelParam, 5, 0, 3 } });
    s.initialValues.push_back ({ { lynx::AutomationTargetKind::channelVolume, 5, 0, 0 }, 0.5f });
    int seen = 0;
    lynx::refs::forEachChannelRef (s, [&] (int& i) {
        if (i == 5)
            ++seen, i = 4;
    });
    std::printf ("  channel refs: %d\n", seen);
    CHECK (seen == 10);
    int left = 0;
    lynx::refs::forEachChannelRef (s, [&] (int& i) { left += i == 5; });
    CHECK (left == 0);

    // inserts: channel routing, sends, side-chains, Transmitter outputs, Patcher outputs, rows, targets, live sources
    lynx::Song m;
    m.channels.resize (1);
    m.channels[0].mixerTrack = 7;
    m.channels[0].patcher.outputTracks = { 7 };
    m.mixer.resize (2);
    m.mixer[1].sends.push_back ({ 7, 1.0f });
    lynx::EffectSlot slot;
    slot.sidechain = 7;
    slot.sidechain2 = 7;
    slot.sendTracks = { 7, 7 };
    lynx::EffectSlot inner;
    inner.sidechain = 7;
    slot.branches.push_back ({ { inner }, 1.0f });
    m.mixer[1].effects.push_back (slot);
    m.playlistTracks.resize (1);
    m.playlistTracks[0].mixerTrack = 7;
    lynx::Automation send;
    send.target = { lynx::AutomationTargetKind::mixerSend, 7, 7, 0 };
    send.source.track = 7;
    m.automations.push_back (send);
    m.midiLinks.push_back ({ 1, -1, { lynx::AutomationTargetKind::mixerVolume, 7, 0, 0 } });
    int inserts = 0;
    lynx::refs::forEachInsertRef (m, [&] (int& i) { inserts += i == 7; });
    std::printf ("  insert refs: %d\n", inserts);
    CHECK (inserts == 13);
}

TEST (session168a_invite_ranges_and_limit)
{
    auto s = session168aHostSong();
    s.mixer[5].name = "FX"; // insert 5 in use
    auto* ana = lynx::parts::invite (s, "Carlos", "Ana", 0xFF3366FFu, false);
    CHECK (ana != nullptr && ana->tag == 2 && ana->insertFirst == 6 && ana->insertCount == lynx::session::insertsPerGuest);
    CHECK (ana->rowFirst == 1 && ana->rowCount == lynx::session::rowsPerGuest && ! ana->keepFull);
    CHECK (s.session.participants.size() == 2 && s.session.participants[0].name == "Carlos");
    // the same friend again: his record (and ranges) are reused, the keep choice updated
    auto* again = lynx::parts::invite (s, "Carlos", "ana", 0, true);
    CHECK (again != nullptr && again->tag == 2 && again->insertFirst == 6 && again->keepFull);
    auto* pedro = lynx::parts::invite (s, "Carlos", "Pedro", 0, false);
    CHECK (pedro != nullptr && pedro->tag == 3 && pedro->insertFirst == 26 && pedro->rowFirst == 17);
    CHECK (lynx::parts::invite (s, "Carlos", "Bia", 0, false) != nullptr);
    CHECK (lynx::parts::invite (s, "Carlos", "Duda", 0, false) == nullptr); // 4 at most (the user's decision 3)

    // the host's own new channels skip the inserts reserved for his guests; a guest's take his range first
    auto host = s;
    for (int t = 1; t < (int) host.mixer.size(); ++t)
        if (! host.insertInUse (t))
            host.mixer[(std::size_t) t].name = "taken " + std::to_string (t); // every existing insert used
    const auto free = host.claimFreeInsert();
    CHECK (free > 0 && ! host.session.reservedForOther (true, free));
    auto guest = lynx::parts::guestCopy (s, 3);
    CHECK (guest.claimFreeInsert() == 26);
}

TEST (session168a_part_round_trip_merges_without_clobbering)
{
    auto host = session168aHostSong();
    auto* ana = lynx::parts::invite (host, "Carlos", "Ana", 0xFF3366FFu, false);
    CHECK (ana != nullptr);
    const auto anaInserts = ana->insertFirst;
    const auto anaRows = ana->rowFirst;
    auto guest = lynx::parts::guestCopy (host, ana->tag);

    // Ana's work: a channel on her insert, notes in the host's "Beat" pattern, a pattern of her own on her row, an
    // automation of her channel's volume with its clip, and a change she must not be able to push: the host's Kick
    guest.channels.push_back (session168aChannel ("Guitar", 0));
    guest.channels.back().mixerTrack = guest.claimFreeInsert();
    lynx::uids::normalize (guest);
    const int guitar = (int) guest.channels.size() - 1;
    CHECK (guest.channels[(std::size_t) guitar].mixerTrack == anaInserts);
    guest.mixer[(std::size_t) anaInserts].name = "Guitar FX";
    guest.mixer[(std::size_t) anaInserts].volume = 0.5f;
    guest.patterns[0].notes.push_back (session168aNote (guitar, 1920, 960, 64));
    guest.patterns[0].sortNotes();
    guest.patterns.emplace_back().name = "Solo";
    guest.patterns.back().notes.push_back (session168aNote (guitar, 0, 3840, 67));
    lynx::uids::normalize (guest);
    lynx::Clip solo;
    solo.pattern = 2;
    solo.track = anaRows;
    solo.start = 3840;
    solo.length = 3840;
    guest.playlist.push_back (solo);
    lynx::Automation fade;
    fade.name = "Guitar volume";
    fade.target = { lynx::AutomationTargetKind::channelVolume, guitar, 0, 0 };
    fade.points = { { 0, 0.2f, 0.0f }, { 3840, 0.9f, 0.0f } };
    guest.automations.push_back (fade);
    lynx::Clip fadeClip;
    fadeClip.kind = lynx::ClipKind::automation;
    fadeClip.automation = 0;
    fadeClip.track = anaRows + 1;
    fadeClip.length = 3840;
    guest.playlist.push_back (fadeClip);
    guest.channels[0].volume = 0.1f; // the host's Kick: not hers
    guest.patterns[0].notes.erase (guest.patterns[0].notes.begin()); // a host note deleted by her: not hers either
    lynx::uids::normalize (guest);

    // meanwhile the host keeps working: a new channel and a note in "Beat"
    host.channels.push_back (session168aChannel ("Hat", 0));
    host.channels.back().mixerTrack = host.claimFreeInsert(); // (skips Ana's reserved inserts)
    CHECK (! host.session.reservedForOther (true, host.channels.back().mixerTrack));
    host.patterns[0].notes.push_back (session168aNote (2, 480, 120, 42));
    host.patterns[0].sortNotes();
    lynx::uids::normalize (host);

    const auto part = lynx::parts::extract (guest, ana->tag);
    // the part holds only her things (+ stubs of the host's patterns her clips/notes refer to)
    int hers = 0;
    for (const auto& c : part.channels)
        hers += lynx::uids::tagOf (c.uid) == ana->tag;
    CHECK (hers == 1);
    CHECK (part.mixer[(std::size_t) anaInserts].name == "Guitar FX" && part.mixer[1].name != "Kick bus");

    auto merged = host;
    const auto report = lynx::parts::merge (merged, part, ana->tag);
    CHECK (report.ok);
    CHECK (report.channels == 1 && report.notes == 2 && report.clips == 2 && report.automations == 1);
    CHECK (merged.channels.size() == 4);
    const auto& g = merged.channels.back();
    CHECK (g.name == "Guitar" && lynx::uids::tagOf (g.uid) == ana->tag && g.colour == 0xFF3366FFu && g.mixerTrack == anaInserts);
    CHECK (merged.mixer[(std::size_t) anaInserts].name == "Guitar FX" && merged.mixer[(std::size_t) anaInserts].volume == 0.5f);
    // the host's things as the HOST left them (Kick's volume, the note she deleted, his Hat note)
    CHECK (merged.channels[0].volume == host.channels[0].volume);
    CHECK (merged.patterns[0].notes.size() == host.patterns[0].notes.size() + 1);
    CHECK (std::any_of (merged.patterns[0].notes.begin(), merged.patterns[0].notes.end(), [] (const lynx::Note& n) { return n.channel == 3 && n.key == 64; }));
    CHECK (std::any_of (merged.patterns[0].notes.begin(), merged.patterns[0].notes.end(), [] (const lynx::Note& n) { return n.channel == 2 && n.key == 42; }));
    CHECK (merged.patterns.size() == 3 && merged.patterns[2].name == "Solo" && merged.patterns[2].notes.size() == 1 && merged.patterns[2].notes[0].channel == 3);
    CHECK (merged.automations.size() == 1 && merged.automations[0].target.index == 3);
    int onHerRows = 0;
    for (const auto& c : merged.playlist)
        if (c.track >= anaRows)
        {
            ++onHerRows;
            CHECK ((c.kind == lynx::ClipKind::pattern && c.pattern == 2) || (c.kind == lynx::ClipKind::automation && c.automation == 0));
        }
    CHECK (onHerRows == 2);
    CHECK ((int) merged.playlistTracks.size() > anaRows && merged.playlistTracks[(std::size_t) anaRows].name == "Ana"
           && merged.playlistTracks[(std::size_t) anaRows].colour == 0xFF3366FFu);

    // merging the same part again changes nothing (her area is replaced, not added twice)
    auto twice = merged;
    lynx::parts::merge (twice, part, ana->tag);
    CHECK (twice.channels == merged.channels && twice.playlist.size() == merged.playlist.size() && twice.automations.size() == merged.automations.size()
           && twice.patterns[0].notes.size() == merged.patterns[0].notes.size() && twice.patterns.size() == merged.patterns.size());

    // she deletes her automation and changes her solo; the next part replaces what the first left
    auto guest2 = guest;
    guest2.removeAutomation (0);
    guest2.patterns[2].notes[0].key = 69;
    const auto r2 = lynx::parts::merge (merged, lynx::parts::extract (guest2, ana->tag), ana->tag);
    CHECK (r2.ok && merged.automations.empty() && merged.patterns[2].notes[0].key == 69 && merged.channels.size() == 4);

    // a part of another project is refused
    auto stranger = part;
    stranger.projectId = lynx::uids::newProjectId();
    auto copy = merged;
    CHECK (! lynx::parts::merge (copy, stranger, ana->tag).ok);

    // the host routed one of his own channels into her reserved insert by hand: hers moves to a free insert
    auto busy = host;
    busy.channels[1].mixerTrack = anaInserts;
    const auto r3 = lynx::parts::merge (busy, part, ana->tag);
    CHECK (r3.ok && r3.movedInserts.size() == 1 && r3.movedInserts[0].first == anaInserts);
    CHECK (busy.channels.back().mixerTrack == r3.movedInserts[0].second && busy.channels[1].mixerTrack == anaInserts);
    CHECK (busy.mixer[(std::size_t) r3.movedInserts[0].second].name == "Guitar FX");
    // ... and clips the host put on her rows push hers down
    auto rows = host;
    lynx::Clip intruder;
    intruder.pattern = 0;
    intruder.track = anaRows;
    intruder.length = 960;
    rows.playlist.push_back (intruder);
    lynx::uids::normalize (rows);
    const auto r4 = lynx::parts::merge (rows, part, ana->tag);
    CHECK (r4.ok && r4.rowShift > 0);
    for (const auto& c : rows.playlist)
        if (lynx::uids::tagOf (c.uid) == ana->tag)
            CHECK (c.track >= anaRows + r4.rowShift);

    // the guest keeps only her part (decision 1): her channel, her pattern and her notes, nothing of the host's
    const auto own = lynx::parts::keepOnlyPart (guest);
    CHECK (own.channels.size() == 1 && own.channels[0].name == "Guitar");
    for (const auto& p : own.patterns)
        for (const auto& n : p.notes)
            CHECK (n.channel == 0);
    for (const auto& c : own.playlist)
        CHECK (lynx::uids::tagOf (c.uid) == ana->tag);
    CHECK (own.mixer[1].name == "Insert 1" && own.mixer[(std::size_t) anaInserts].name == "Guitar FX");
}

namespace
{
std::vector<float> session168aRender (const lynx::Song& s, std::int64_t ticks, const lynx::RackState::SampleProvider& provider, bool songMode)
{
    auto song = s;
    song.mode = songMode ? lynx::PlayMode::song : lynx::PlayMode::pattern;
    auto state = lynx::RackState::build (song, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 480);
    const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
    const auto frames = (int) std::llround ((double) ticks / tps);
    std::vector<float> out ((std::size_t) frames * 2), l (480), rr (480);
    for (int pos = 0; pos < frames; pos += 480)
    {
        const auto n = std::min (480, frames - pos);
        r.process (state.get(), true, pos * tps, n, l.data(), rr.data());
        for (int i = 0; i < n; ++i)
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i], out[(std::size_t) (pos + i) * 2 + 1] = rr[(std::size_t) i];
    }
    return out;
}
} // namespace

TEST (session168a_stand_in_plays_like_the_part)
{
    // A channel's stems rendered the way SessionPack renders them (one render, cut in segments) and played in place of
    // the channel where its plugin is missing must sound like the channel itself — in Song mode (clips, a clip that
    // starts inside its pattern, the tail into the next clip) and in Pattern mode (the loop stem).
    auto song = session168aHostSong();
    song.bpm = 120.0; // 25 samples per tick at 48 kHz
    song.mode = lynx::PlayMode::song;
    song.channels[0].volume = 0.6f;
    song.channels[0].pan = 0.3f;
    song.channels[1].muted = true;
    song.patterns[0].notes.push_back (session168aNote (0, 1440, 240, 43));
    song.patterns[0].notes.push_back (session168aNote (0, 2880, 480, 48));
    song.patterns[0].sortNotes();
    lynx::Clip late; // starts one beat into "Beat", up to its end
    late.pattern = 0;
    late.start = 7680;
    late.offset = 960;
    late.length = 2880;
    song.playlist.push_back (late);
    lynx::uids::normalize (song);
    const auto end = song.songLength();

    auto none = lynx::RackState::SampleProvider {};
    const auto render = lynx::standins::channelPatternRender (song, 0);
    CHECK (render.segments.size() == 2 && render.tail == 4 * 3840);
    CHECK (! lynx::standins::needsTimeline (song, 0));
    const auto all = session168aRender (render.song, render.end, none, true);
    const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
    std::map<std::string, std::shared_ptr<const lynx::SampleData>> stems;
    auto cut = [&] (std::int64_t from, std::int64_t length) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        const auto a = (std::size_t) std::llround ((double) from / tps), n = (std::size_t) std::llround ((double) length / tps);
        for (std::size_t i = 0; i < n && (a + i) * 2 + 1 < all.size(); ++i)
            d->left.push_back (all[(a + i) * 2]), d->right.push_back (all[(a + i) * 2 + 1]);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto plugged = song;
    auto& ch = plugged.channels[0];
    ch.instrument = lynx::InstrumentKind::plugin;
    ch.plugin.id = "VST3-Missing-0-0";
    ch.plugin.name = "Missing Synth";
    for (const auto& seg : render.segments)
    {
        const auto once = "once" + std::to_string (seg.pattern), loop = "loop" + std::to_string (seg.pattern);
        stems[once] = cut (seg.once, seg.length + render.tail);
        stems[loop] = cut (seg.loopFrom, seg.length);
        ch.standIns.push_back ({ seg.patternUid, false, once, "", seg.length + render.tail, "Missing Synth", "Carlos" });
        ch.standIns.push_back ({ seg.patternUid, true, loop, "", seg.length, "Missing Synth", "Carlos" });
    }
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto it = stems.find (path);
        return it != stems.end() ? it->second : nullptr;
    };
    CHECK (lynx::standins::channelUsesPlugin (ch) && lynx::standins::hasAny (plugged));
    lynx::standins::Projected projected;
    const auto played = lynx::standins::project (plugged, [] (const lynx::PluginRef&) { return true; }, &projected);
    CHECK (projected.channels == std::vector<int> { 0 } && projected.inserts.empty());
    CHECK (played.songLength() == song.songLength());
    // installed: nothing changes
    lynx::standins::Projected nothing;
    const auto same = lynx::standins::project (plugged, [] (const lynx::PluginRef&) { return false; }, &nothing);
    CHECK (nothing.channels.empty() && same.channels[0].instrument == lynx::InstrumentKind::plugin);

    auto diff = [] (const std::vector<float>& a, const std::vector<float>& b, std::size_t from) {
        float worst = 0.0f, peak = 0.0f;
        for (std::size_t i = from; i < std::min (a.size(), b.size()); ++i)
            worst = std::max (worst, std::abs (a[i] - b[i])), peak = std::max (peak, std::abs (a[i]));
        return std::pair<float, float> { worst, peak };
    };
    const auto original = session168aRender (song, end, none, true);
    const auto standIn = session168aRender (played, end, provider, true);
    const auto [worst, peak] = diff (original, standIn, 0);
    std::printf ("  stand-in song mode: peak %.4f, worst difference %.2e\n", peak, worst);
    CHECK (peak > 0.05f && worst < 1.0e-4f);

    // Pattern mode: the loop stem from the second pass on (the first pass of the stem already carries a tail)
    song.currentPattern = plugged.currentPattern = 0;
    plugged.mode = lynx::PlayMode::pattern;
    const auto loopPlayed = lynx::standins::project (plugged, [] (const lynx::PluginRef&) { return true; });
    const auto length = song.patterns[0].effectiveLength (song.meter());
    const auto o2 = session168aRender (song, 3 * length, none, false);
    const auto s2 = session168aRender (loopPlayed, 3 * length, provider, false);
    const auto [worstLoop, peakLoop] = diff (o2, s2, (std::size_t) std::llround ((double) length / tps) * 2);
    std::printf ("  stand-in pattern mode: peak %.4f, worst difference %.2e\n", peakLoop, worstLoop);
    CHECK (peakLoop > 0.05f && worstLoop < 1.0e-4f);
}

TEST (session168a_insert_stand_in_replaces_the_chain)
{
    auto song = session168aHostSong();
    song.bpm = 120.0;
    song.mode = lynx::PlayMode::song;
    song.mixer[1].volume = 0.6f;
    song.mixer[1].pan = -0.4f;
    lynx::EffectSlot gain;
    gain.kind = lynx::EffectKind::utility;
    song.mixer[1].effects.push_back (gain);
    lynx::uids::normalize (song);
    const auto end = song.songLength();
    auto none = lynx::RackState::SampleProvider {};
    const auto renderSong = lynx::standins::insertTimelineRender (song, 1);
    const auto stem = session168aRender (renderSong, end, none, true);
    auto plugged = song;
    lynx::EffectSlot hosted;
    hosted.kind = lynx::EffectKind::plugin;
    hosted.plugin.id = "VST3-Missing FX-0-0";
    hosted.enabled = false; // (would play through here anyway: the test's "plugin" is the utility before it)
    plugged.mixer[1].effects.push_back (hosted);
    plugged.mixer[1].standIns.push_back ({ 0, false, "insert1", "", end, "Missing FX", "Carlos" });
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    for (std::size_t i = 0; i + 1 < stem.size(); i += 2)
        data->left.push_back (stem[i]), data->right.push_back (stem[i + 1]);
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> { return path == "insert1" ? data : nullptr; };
    lynx::standins::Projected projected;
    auto played = lynx::standins::project (plugged, [] (const lynx::PluginRef& r) { return r.id == "VST3-Missing FX-0-0"; }, &projected);
    CHECK (projected.inserts == std::vector<int> { 1 });
    played.mode = lynx::PlayMode::song;
    const auto original = session168aRender (song, end, none, true);
    const auto standIn = session168aRender (played, end, provider, true);
    float worst = 0.0f, peak = 0.0f;
    for (std::size_t i = 0; i < std::min (original.size(), standIn.size()); ++i)
        worst = std::max (worst, std::abs (original[i] - standIn[i])), peak = std::max (peak, std::abs (original[i]));
    std::printf ("  insert stand-in: peak %.4f, worst difference %.2e\n", peak, worst);
    CHECK (peak > 0.05f && worst < 1.0e-4f);
}

TEST (session168a_streaming_sha256)
{
    // the streaming hash equals the one-shot one for every split (packages hash media while reading them)
    std::vector<std::uint8_t> data (1000);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = (std::uint8_t) (i * 7 + 3);
    for (const auto size : { (std::size_t) 0, (std::size_t) 55, (std::size_t) 56, (std::size_t) 64, (std::size_t) 1000 })
        for (const auto piece : { (std::size_t) 1, (std::size_t) 63, (std::size_t) 64, (std::size_t) 200 })
        {
            lynx::Sha256Stream stream;
            for (std::size_t at = 0; at < size; at += piece)
                stream.update (data.data() + at, std::min (piece, size - at));
            CHECK (stream.hex() == lynx::sha256Hex (data.data(), size));
        }
    lynx::Sha256Stream abc;
    abc.update ((const std::uint8_t*) "abc", 3);
    CHECK (abc.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
