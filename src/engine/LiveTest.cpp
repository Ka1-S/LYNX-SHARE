#include "engine/LiveTest.h"

#include <cmath>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/LiveCrypto.h"
#include "core/LiveSession.h"
#include "core/MissingFiles.h"
#include "core/ProjectSafety.h"
#include "core/SessionIds.h"
#include "core/SessionParts.h"
#include "engine/AudioEngine.h"
#include "engine/LiveSessions.h"
#include "engine/SessionPack.h"

namespace lynx::livetest
{
namespace
{
juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

void writeWav (const juce::File& file, float frequency, int samples)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    juce::AudioBuffer<float> b (1, samples);
    for (int i = 0; i < samples; ++i)
        b.setSample (0, i, 0.4f * std::sin (frequency * (float) i / 48000.0f * 6.2831853f) * (1.0f - (float) i / (float) samples));
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    if (auto* w = format.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0))
    {
        out.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, samples);
    }
}

// the song as every computer should have it: its sample paths (each computer's own) become the bytes' hashes
Song canonical (const Song& song)
{
    Song s = song;
    std::map<std::string, std::string> byHash;
    for (const auto& path : safety::referencedSamples (s))
    {
        const auto hash = sessionpack::hashFile (juce::File (str (path)));
        byHash[path] = "sha:" + (hash.isEmpty() ? std::string ("missing") : hash.toStdString());
    }
    applyRelocations (s, byHash);
    return s;
}

// the entities on which two songs differ ("" = none)
juce::String differences (const Song& a, const Song& b)
{
    auto ca = canonical (a), cb = canonical (b);
    auto keys = live::keysOf (ca);
    for (const auto& k : live::keysOf (cb))
        keys.insert (k);
    juce::StringArray out;
    for (const auto& k : keys)
        if (! live::same (ca, cb, k))
            out.add (str (live::toString (k)));
    return out.joinIntoString (",");
}

ChannelDef sampler (const std::string& name, const juce::File& wav)
{
    ChannelDef c;
    c.name = name;
    c.instrument = InstrumentKind::sampler;
    c.samplePath = wav.getFullPathName().toStdString();
    return c;
}

int channelIndex (const Song& s, const std::string& name)
{
    for (int i = 0; i < (int) s.channels.size(); ++i)
        if (s.channels[(std::size_t) i].name == name)
            return i;
    return -1;
}
int patternIndex (const Song& s, const std::string& name)
{
    for (int i = 0; i < (int) s.patterns.size(); ++i)
        if (s.patterns[(std::size_t) i].name == name)
            return i;
    return -1;
}
} // namespace

// --- without a network ----------------------------------------------------------------------------------------------

juce::String selfTest (const Song* rich)
{
    Song song = rich != nullptr ? *rich : Song::makeDefault();
    if (rich == nullptr)
    {
        song.bpm = 128.5;
        song.channels.push_back (sampler ("Hit", juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hit.wav")));
        Pattern p;
        p.name = "Beat";
        for (int i = 0; i < 16; ++i)
        {
            Note n;
            n.start = i * 96;
            n.channel = (int) song.channels.size() - 1;
            n.key = 48 + i;
            n.velocity = 0.1f + 0.05f * (float) i;
            n.finePitch = 0.3f * (float) i;
            p.notes.push_back (n);
        }
        song.patterns.push_back (p);
        Clip c;
        c.pattern = (int) song.patterns.size() - 1;
        c.track = 2;
        c.start = 384;
        c.length = 1536;
        song.playlist.push_back (c);
        if (song.mixer.size() > 3)
        {
            song.mixer[3].volume = 0.337f;
            song.mixer[3].pan = -0.21f;
            song.mixer[3].name = "Drums";
        }
    }
    uids::normalize (song);

    // 1. every entity through the .lynx JSON: what does not come back the same (fields the format rounds or does not
    // keep) — reported, not an error: the replicas adopt the host's form (core/LiveSession.h)
    const Song empty;
    const auto all = live::diff (empty, song);
    const auto once = LiveSessionManager::roundTrip (all);
    Song direct = empty, sent = empty;
    live::apply (direct, all);
    live::apply (sent, once);
    juce::StringArray lost;
    for (const auto& k : live::keysOf (direct))
        if (! live::same (direct, sent, k))
            lost.add (str (live::describe (k, song)) + " [" + str (live::toString (k)) + "]");
    // 2. stable: what came through once comes through again unchanged (else the copies would drift apart)
    const auto twice = LiveSessionManager::roundTrip (once);
    Song again = empty;
    live::apply (again, twice);
    juce::StringArray unstable;
    for (const auto& k : live::keysOf (sent))
        if (! live::same (sent, again, k))
            unstable.add (str (live::toString (k)));
    // 3. a host and two guests through the real codec: everyone's edits, then the same song everywhere
    live::Codec codec = &LiveSessionManager::roundTrip;
    Song start = song;
    parts::invite (start, "Host", "Ana", 0xFF3D9BE9, false);
    parts::invite (start, "Host", "Lia", 0xFF46C27A, false);
    live::Authority authority (start, codec);
    struct Peer
    {
        std::unique_ptr<live::Replica> replica;
        Song working;
    };
    std::vector<Peer> peers;
    for (std::uint32_t tag = 1; tag <= 3; ++tag)
    {
        Peer p;
        p.replica = std::make_unique<live::Replica> (start, 0, tag, codec);
        p.working = start;
        p.working.session.me = tag;
        peers.push_back (std::move (p));
    }
    auto deliver = [&] (const live::Commit& c) {
        for (auto& p : peers)
            if (auto u = p.replica->onCommit (c, p.working); u.working)
                p.working = *u.working;
    };
    auto edit = [&] (int who, const std::function<void (Song&)>& fn) {
        auto& p = peers[(std::size_t) who];
        fn (p.working);
        uids::normalize (p.working);
        if (auto proposal = p.replica->poll (p.working))
            deliver (authority.propose ((std::uint32_t) who + 1, { proposal->seq, proposal->baseRev, LiveSessionManager::roundTrip (proposal->patch) }));
    };
    edit (0, [] (Song& s) { s.bpm = 141.25; });
    edit (1, [] (Song& s) {
        s.channels.push_back (sampler ("Ana bass", juce::File()));
        Pattern p;
        p.name = "Ana riff";
        Note n;
        n.channel = (int) s.channels.size() - 1;
        n.velocity = 0.333f;
        p.notes.push_back (n);
        s.patterns.push_back (p);
    });
    edit (2, [] (Song& s) { s.patterns.front().name = "not mine"; }); // (refused: the host's pattern header)
    edit (1, [] (Song& s) {
        if (auto i = patternIndex (s, "Ana riff"); i >= 0)
            s.patterns[(std::size_t) i].notes.front().key = 50;
    });
    juce::StringArray diverged;
    for (std::size_t i = 1; i < peers.size(); ++i)
        if (const auto d = differences (peers[0].working, peers[i].working); d.isNotEmpty())
            diverged.add ("peer " + juce::String ((int) i) + ": " + d);
    const bool editsOk = peers[0].working.bpm == 141.25 && patternIndex (peers[2].working, "Ana riff") >= 0
                         && patternIndex (peers[0].working, "not mine") < 0;

    // 4. the invite code's key: the token the relay sees does not depend on the room, the key does
    livecrypto::Invite invite { "abcdefghij", livecrypto::newSecret() };
    const auto keys = livecrypto::derive (invite);
    auto moved = invite;
    moved.room = "abcdefghik";
    const bool cryptoOk = livecrypto::derive (moved).relayToken == keys.relayToken && livecrypto::derive (moved).key != keys.key;

    const bool ok = unstable.isEmpty() && diverged.isEmpty() && editsOk && cryptoOk;
    juce::String out = ok ? "OK" : "ERROR";
    out << " (" << (int) live::keysOf (song).size() << " entities, " << lost.size() << " not kept by the format";
    if (! lost.isEmpty())
        out << ": " << lost.joinIntoString ("; ").substring (0, 600);
    if (! unstable.isEmpty())
        out << "; unstable: " << unstable.joinIntoString (",");
    if (! diverged.isEmpty())
        out << "; diverged: " << diverged.joinIntoString (" | ");
    if (! editsOk)
        out << "; edits wrong";
    if (! cryptoOk)
        out << "; keys wrong";
    return out + ")";
}

// --- over the relay -------------------------------------------------------------------------------------------------

int run (const juce::StringArray& args)
{
    const auto i = args.indexOf ("--livetest");
    const juce::File report (args[i + 1].unquoted());
    juce::String relay = "https://relay.lynx-share.com";
    if (const auto r = args.indexOf ("--relay"); r >= 0)
        relay = args[r + 1].unquoted();
    juce::String log;
    bool ok = true;
    auto line = [&] (const juce::String& s) {
        log << s << "\n";
        report.replaceWithText (log);
    };
    auto check = [&] (bool good, const juce::String& what) {
        ok = ok && good;
        line ((good ? "ok   " : "FAIL ") + what);
        return good;
    };

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-livetest");
    dir.deleteRecursively();
    const auto hostWav = dir.getChildFile ("host/kick.wav"), anaWav = dir.getChildFile ("ana/bass line.wav");
    writeWav (hostWav, 60.0f, 24000);
    writeWav (anaWav, 110.0f, 600000); // 1.2 MB: several media pieces and a fragmented frame

    struct Peer
    {
        juce::String name;
        std::unique_ptr<AudioEngine> engine;
        std::unique_ptr<LiveSessionManager> live;
        juce::StringArray hints;
    };
    std::vector<std::unique_ptr<Peer>> peers;
    for (const auto* name : { "Host", "Ana", "Lia", "Rui" })
    {
        auto p = std::make_unique<Peer>();
        p->name = name;
        p->engine = std::make_unique<AudioEngine> (false);
        p->live = std::make_unique<LiveSessionManager> (*p->engine);
        auto* raw = p.get();
        p->live->onHint = [raw] (const juce::String& h) { raw->hints.add (h); };
        peers.push_back (std::move (p));
    }
    auto& host = *peers[0];
    auto& ana = *peers[1];
    auto& lia = *peers[2];
    auto& rui = *peers[3];
    auto pumpUntil = [&] (const std::function<bool()>& done, int timeoutMs) {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < until)
        {
            for (auto& p : peers)
                p->live->pump();
            if (done())
                return true;
            juce::Thread::sleep (5);
        }
        return false;
    };
    auto settle = [&] (int ms) { pumpUntil ([] { return false; }, ms); };
    auto options = [&] (Peer& p) {
        LiveSessionManager::Options o;
        o.relay = relay;
        o.name = p.name;
        o.autoAccept = true;
        o.folder = dir.getChildFile (p.name + " media");
        return o;
    };
    auto converged = [&] (std::initializer_list<Peer*> list) {
        for (auto* p : list)
            if (p->live->getState() != LiveSessionManager::State::live || p->live->mediaDownloading() > 0 || p->live->waitingChanges() > 0)
                return false;
        for (auto* p : list)
            if (differences (host.engine->getSong(), p->engine->getSong()).isNotEmpty())
                return false;
        return true;
    };
    auto explain = [&] (std::initializer_list<Peer*> list) {
        for (auto* p : list)
            if (const auto d = differences (host.engine->getSong(), p->engine->getSong()); d.isNotEmpty())
                line ("     " + p->name + " differs on " + d.substring (0, 400));
    };

    // the host's project: a sampler with its own sample
    {
        auto song = Song::makeDefault();
        song.channels.push_back (sampler ("Host kick", hostWav));
        host.engine->setSong (song);
    }
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    host.live->start (options (host));
    if (! check (pumpUntil ([&] { return host.live->getState() == LiveSessionManager::State::live || ! host.live->isActive(); }, 20000)
                     && host.live->getState() == LiveSessionManager::State::live,
                 "host opened the room (" + host.live->getStatus() + ") in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t0)) + " ms"))
        return 1;
    const auto code = host.live->getInviteCode();
    line ("     invite code " + code.substring (0, 16) + "…");

    // a wrong code: refused by the relay (another token)
    {
        auto wrong = livecrypto::parseCode (code.toStdString());
        wrong->secret[0] = wrong->secret[0] == 'a' ? 'b' : 'a';
        rui.live->join (str (livecrypto::formatCode (*wrong)), options (rui));
        pumpUntil ([&] { return ! rui.live->isActive(); }, 15000);
        check (rui.live->getState() == LiveSessionManager::State::ended && ! rui.engine->hasSessionHistory(), "a wrong code is refused: " + rui.live->getStatus());
    }

    // two guests join
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    ana.live->join (code, options (ana));
    lia.live->join (code.toLowerCase().replace ("-", " "), options (lia));
    check (pumpUntil ([&] { return converged ({ &ana, &lia }); }, 30000),
           "two guests joined and have the project with its sample in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t1)) + " ms");
    explain ({ &ana, &lia });
    const auto anaTag = ana.live->myTag(), liaTag = lia.live->myTag();
    check (anaTag == 2 && liaTag == 3 && host.engine->getSong().session.participants.size() == 3, "participants: host 1, Ana " + juce::String ((int) anaTag) + ", Lia " + juce::String ((int) liaTag));
    check (host.live->getPeople().size() == 3 && ana.live->getPeople().size() == 3, "presence: " + juce::String ((int) ana.live->getPeople().size()) + " people online");
    {
        const auto& s = ana.engine->getSong();
        const auto k = channelIndex (s, "Host kick");
        const juce::File f (k >= 0 ? str (s.channels[(std::size_t) k].samplePath) : juce::String());
        check (f.existsAsFile() && f.isAChildOf (dir.getChildFile ("Ana media")) && sessionpack::hashFile (f) == sessionpack::hashFile (hostWav),
               "the host's sample arrived on Ana's computer: " + f.getFileName());
    }

    // everyone edits
    host.engine->editSong ([] (Song& s) { s.bpm = 142.0; s.patterns.front().name = "Verse"; });
    ana.engine->editSong ([&] (Song& s) {
        s.channels.push_back (sampler ("Ana bass", anaWav));
        const auto* me = s.session.find (s.session.me);
        s.channels.back().mixerTrack = me != nullptr ? me->insertFirst : 0;
        Pattern p;
        p.name = "Ana riff";
        for (int n = 0; n < 8; ++n)
        {
            Note note;
            note.start = n * 192;
            note.channel = (int) s.channels.size() - 1;
            note.key = 36 + n;
            p.notes.push_back (note);
        }
        s.patterns.push_back (p);
    });
    settle (50);
    ana.engine->editSong ([&] (Song& s) {
        const auto* me = s.session.find (s.session.me);
        Clip c;
        c.pattern = patternIndex (s, "Ana riff");
        c.track = me != nullptr ? me->rowFirst : 0;
        c.start = 0;
        c.length = 1536;
        s.playlist.push_back (c);
    });
    ana.engine->editSong ([&] (Song& s) {
        const auto* me = s.session.find (s.session.me);
        if (me != nullptr && me->insertFirst < (int) s.mixer.size())
            s.mixer[(std::size_t) me->insertFirst].volume = 0.5f;
    });
    lia.engine->editSong ([&] (Song& s) {
        s.channels.push_back (sampler ("Lia keys", juce::File()));
        s.channels.back().instrument = InstrumentKind::synth;
        Note note;
        note.channel = (int) s.channels.size() - 1;
        note.key = 72;
        s.patterns.front().notes.push_back (note); // her notes in the host's pattern: hers
    });
    settle (50);
    lia.engine->editSong ([&] (Song& s) { s.channels[(std::size_t) channelIndex (s, "Host kick")].name = "Lia was here"; }); // not hers
    const auto t2 = juce::Time::getMillisecondCounterHiRes();
    const bool editsConverged = pumpUntil ([&] { return converged ({ &ana, &lia }) && channelIndex (lia.engine->getSong(), "Host kick") >= 0
                                                        && channelIndex (lia.engine->getSong(), "Ana bass") >= 0; },
                                           30000);
    check (editsConverged, "everyone's edits are everywhere in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t2)) + " ms");
    explain ({ &ana, &lia });
    {
        const auto& s = host.engine->getSong();
        const auto riff = patternIndex (s, "Ana riff");
        const auto bass = channelIndex (s, "Ana bass");
        const auto* anaRecord = s.session.find (anaTag);
        check (s.bpm == 142.0 && patternIndex (s, "Verse") == 0 && riff >= 0 && bass >= 0 && s.patterns[(std::size_t) riff].notes.size() == 8
                   && channelIndex (s, "Lia keys") >= 0 && s.patterns.front().notes.size() >= 1 && anaRecord != nullptr
                   && std::abs (s.mixer[(std::size_t) anaRecord->insertFirst].volume - 0.5f) < 1e-6f
                   && std::any_of (s.playlist.begin(), s.playlist.end(), [&] (const Clip& c) { return c.pattern == riff && c.track == anaRecord->rowFirst; }),
               "tempo, pattern rename, a new channel + pattern + notes + clip + insert volume, notes in another's pattern");
        check (channelIndex (s, "Lia was here") < 0 && channelIndex (lia.engine->getSong(), "Lia was here") < 0 && host.live->getStats().rejected >= 1,
               "an edit of someone else's channel is refused and undone (" + (lia.hints.isEmpty() ? juce::String ("no hint") : lia.hints[lia.hints.size() - 1]) + ")");
        const auto& l = lia.engine->getSong();
        const auto b = channelIndex (l, "Ana bass");
        const juce::File f (b >= 0 ? str (l.channels[(std::size_t) b].samplePath) : juce::String());
        check (f.existsAsFile() && sessionpack::hashFile (f) == sessionpack::hashFile (anaWav), "Ana's 1.2 MB sample went through the host to Lia");
    }

    // undo per user: Ana's last step (the insert volume) goes back everywhere, the host's tempo stays
    check (ana.engine->canUndo(), "Ana has her own undo steps");
    ana.engine->undo();
    pumpUntil ([&] {
        const auto& s = lia.engine->getSong();
        const auto* r = s.session.find (anaTag);
        return r != nullptr && converged ({ &ana, &lia }) && std::abs (s.mixer[(std::size_t) r->insertFirst].volume - unityFader) < 1e-6f;
    },
               15000);
    {
        const auto& s = lia.engine->getSong();
        const auto* r = s.session.find (anaTag);
        check (r != nullptr && std::abs (s.mixer[(std::size_t) r->insertFirst].volume - unityFader) < 1e-6f && s.bpm == 142.0 && patternIndex (s, "Ana riff") >= 0,
               "Ana's undo took back only her insert volume (tempo and her pattern stay)");
    }
    host.engine->undo(); // the host's own last step: tempo + rename
    pumpUntil ([&] { return converged ({ &ana, &lia }) && ana.engine->getSong().bpm != 142.0; }, 15000);
    check (ana.engine->getSong().bpm != 142.0 && patternIndex (ana.engine->getSong(), "Ana riff") >= 0 && channelIndex (ana.engine->getSong(), "Lia keys") >= 0,
           "the host's undo took back only the host's step");

    // a late guest: the whole project and both samples
    const auto t3 = juce::Time::getMillisecondCounterHiRes();
    rui.hints.clear();
    rui.live->join (code, options (rui));
    check (pumpUntil ([&] { return converged ({ &ana, &lia, &rui }); }, 30000),
           "a late guest got the project and its two samples in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t3)) + " ms");
    explain ({ &rui });
    check (host.live->getPeople().size() == 4, "4 people in the session");

    // Lia leaves: the host sees it; her copy keeps only her part
    lia.live->leave();
    pumpUntil ([&] { return host.live->getPeople().size() == 3; }, 10000);
    {
        const auto& l = lia.engine->getSong();
        bool onlyHers = ! l.channels.empty();
        for (const auto& c : l.channels)
            onlyHers = onlyHers && uids::tagOf (c.uid) == liaTag;
        check (host.live->getPeople().size() == 3 && lia.live->getState() == LiveSessionManager::State::ended && onlyHers && ! lia.engine->hasSessionHistory(),
               "Lia left: the host shows 3 people, her copy has only her " + juce::String ((int) l.channels.size()) + " channel(s)");
    }
    // the session goes on without her
    rui.engine->editSong ([] (Song& s) {
        Pattern p;
        p.name = "Rui fill";
        s.patterns.push_back (p);
    });
    check (pumpUntil ([&] { return converged ({ &ana, &rui }) && patternIndex (ana.engine->getSong(), "Rui fill") >= 0; }, 15000), "the session goes on after a guest left");

    // the host ends it: the guests keep their parts
    const auto stats = host.live->getStats();
    host.live->leave();
    pumpUntil ([&] { return ! ana.live->isActive() && ! rui.live->isActive(); }, 10000);
    {
        const auto& a = ana.engine->getSong();
        bool onlyHers = ! a.channels.empty();
        for (const auto& c : a.channels)
            onlyHers = onlyHers && uids::tagOf (c.uid) == anaTag;
        check (! ana.live->isActive() && ! rui.live->isActive() && onlyHers && patternIndex (a, "Ana riff") >= 0,
               "the host ended the session: Ana keeps her part (" + ana.live->getStatus() + ")");
    }
    line ("     host: " + juce::String (stats.sent) + " frames sent (" + juce::String (stats.bytesSent / 1024) + " KiB), " + juce::String (stats.received) + " received, "
          + juce::String (stats.commits) + " commits, " + juce::String (stats.rejected) + " refused, " + juce::String (stats.mediaSent) + " media sent");
    line ("     ana: " + juce::String (ana.live->getStats().mediaSent) + " media sent, " + juce::String (ana.live->getStats().mediaReceived) + " received");
    line ("     total " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t0)) + " ms");
    for (auto& p : peers)
        p->live.reset();
    line (ok ? "result=OK" : "result=FAILED");
    dir.deleteRecursively();
    return ok ? 0 : 1;
}
} // namespace lynx::livetest
