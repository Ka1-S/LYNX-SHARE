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
#include "engine/MediaPack.h"
#include "engine/PluginHost.h"
#include "engine/ProjectFile.h"
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

// noise does not pack (FLAC saves nothing on it): the file travels at its full size (the media limits' checks)
void writeNoise (const juce::File& file, int samples, int seed)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    juce::AudioBuffer<float> b (1, samples);
    juce::Random random (seed);
    for (int i = 0; i < samples; ++i)
        b.setSample (0, i, random.nextFloat() * 1.6f - 0.8f);
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

    // 5. FLAC packs (round 169j): 16- and 24-bit PCM come back byte for byte (with chunks around the samples);
    // 32-bit float and tiny files are sent as they are
    juce::String packReport;
    bool packOk = true;
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-livepack");
        tmp.deleteRecursively();
        tmp.createDirectory();
        auto make = [&] (const juce::String& name, int bits, int channels, int frames, bool floating) {
            const auto f = tmp.getChildFile (name);
            juce::WavAudioFormat wav;
            juce::AudioBuffer<float> b (channels, frames);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < frames; ++i)
                    b.setSample (c, i, 0.5f * std::sin ((110.0f + 55.0f * (float) c) * (float) i / 48000.0f * 6.2831853f) * (1.0f - (float) i / (float) frames));
            juce::StringPairArray meta;
            meta.set ("bwav description", "LYNX pack test"); // (a bext chunk before the samples)
            std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());
            if (auto* w = wav.createWriterFor (out.get(), 48000.0, (unsigned) channels, floating ? 32 : bits, meta, 0))
            {
                out.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                writer->writeFromAudioSampleBuffer (b, 0, frames);
            }
            return f;
        };
        struct Case
        {
            const char* name;
            int bits, channels, frames;
            bool floating, packs;
        };
        for (const auto& c : { Case { "s16.wav", 16, 1, 300000, false, true }, Case { "s24.wav", 24, 2, 200000, false, true },
                               Case { "f32.wav", 32, 2, 100000, true, false }, Case { "tiny.wav", 16, 1, 1000, false, false } })
        {
            const auto wavFile = make (c.name, c.bits, c.channels, c.frames, c.floating);
            const auto packFile = tmp.getChildFile (juce::String (c.name) + ".lxf"), back = tmp.getChildFile (juce::String (c.name) + ".back");
            const bool packed = mediapack::pack (wavFile, packFile);
            bool same = true;
            if (packed)
            {
                same = mediapack::isPack (packFile) && mediapack::unpack (packFile, back) && sessionpack::hashFile (back) == sessionpack::hashFile (wavFile);
                packReport << c.name << " " << (int) (100 * packFile.getSize() / std::max<juce::int64> (1, wavFile.getSize())) << "% ";
            }
            else
                packReport << c.name << " raw ";
            packOk = packOk && packed == c.packs && same;
        }
        // a damaged pack is refused
        if (tmp.getChildFile ("s16.wav.lxf").existsAsFile())
        {
            juce::MemoryBlock m;
            tmp.getChildFile ("s16.wav.lxf").loadFileAsData (m);
            ((char*) m.getData())[20] ^= 0x55;
            tmp.getChildFile ("bad.lxf").replaceWithData (m.getData(), m.getSize());
            packOk = packOk && ! mediapack::unpack (tmp.getChildFile ("bad.lxf"), tmp.getChildFile ("bad.wav"));
        }
        tmp.deleteRecursively();
    }

    // P5-A1 (round 190ai): a plugin played by the host and its parameter values go through the wire form unchanged
    bool hostedOk = false;
    {
        Song a = Song::makeDefault(), b = a;
        ChannelDef c;
        c.name = "Hosted";
        c.instrument = InstrumentKind::plugin;
        c.plugin.id = "VST3-Somebody-1";
        c.plugin.name = "Somebody";
        c.plugin.playedBy = session::hostTag;
        c.plugin.values = { { 0, 0.1f }, { 7, 1.0f / 3.0f }, { 1023, 1.0f } };
        b.channels.push_back (c);
        uids::normalize (a);
        uids::normalize (b);
        const auto wire = LiveSessionManager::roundTrip (live::diff (a, b));
        Song got = a;
        live::apply (got, wire);
        hostedOk = ! got.channels.empty() && got.channels.back().plugin == b.channels.back().plugin && got.channels.back().plugin.playedBy == session::hostTag
                   && got.channels.back().plugin.values.size() == 3;
    }

    const bool ok = unstable.isEmpty() && diverged.isEmpty() && editsOk && cryptoOk && packOk && hostedOk;
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
    out << "; flac packs " << packReport.trim() << (packOk ? "" : " WRONG");
    out << "; hosted plugin " << (hostedOk ? "ok" : "WRONG");
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
    // round 169j: --plugin <a .vst3>: Ana plays that instrument, Rui does not have it (live stand-ins)
    juce::String pluginFile;
    if (const auto r = args.indexOf ("--plugin"); r >= 0)
        pluginFile = args[r + 1].unquoted();
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
                if (p->live != nullptr) // (the host's, while it "restarts")
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
        o.journal = dir.getChildFile ("host journal.json");
        o.guestJournal = dir.getChildFile (p.name + " guest journal.json");
        o.guestMediaLimit = 3 << 20; // (round 190ah: a small limit per guest, so the checks below reach it)
        o.inlineWork = true;
        o.standInDelayMs = 300;
        if (&p == peers[3].get())
            o.missing = [] (const PluginRef&) { return true; }; // (Rui has none of the third-party plugins)
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
            {
                line ("     " + p->name + " differs on " + d.substring (0, 400));
                // the first differing line of the JSON of an insert that differs (what the format and the replica disagree on)
                for (const auto& key : juce::StringArray::fromTokens (d, ",", ""))
                    if (key.startsWith ("m:"))
                    {
                        const auto i = (std::size_t) key.substring (2).getLargeIntValue();
                        const auto& hs = host.engine->getSong();
                        const auto& ps = p->engine->getSong();
                        if (i >= hs.mixer.size() || i >= ps.mixer.size())
                        {
                            line ("       the host has " + juce::String ((int) hs.mixer.size()) + " mixer tracks, " + p->name + " " + juce::String ((int) ps.mixer.size()));
                            break;
                        }
                        const auto &x = hs.mixer[i], &y = ps.mixer[i];
                        juce::StringArray f;
#define LYNX_FIELD(n) if (! (x.n == y.n)) f.add (#n);
                        LYNX_FIELD (name) LYNX_FIELD (volume) LYNX_FIELD (pan) LYNX_FIELD (muted) LYNX_FIELD (solo) LYNX_FIELD (soloSources) LYNX_FIELD (effects)
                        LYNX_FIELD (toMaster) LYNX_FIELD (masterLevel) LYNX_FIELD (stereo) LYNX_FIELD (colour) LYNX_FIELD (invertPhase) LYNX_FIELD (swapChannels)
                        LYNX_FIELD (sends) LYNX_FIELD (eq) LYNX_FIELD (inputChannel) LYNX_FIELD (inputStereo) LYNX_FIELD (monitor) LYNX_FIELD (recordSafe)
                        LYNX_FIELD (recordTo) LYNX_FIELD (recordSource) LYNX_FIELD (recordDryToo) LYNX_FIELD (flInput) LYNX_FIELD (outputChannel) LYNX_FIELD (outputMode)
                        LYNX_FIELD (threaded) LYNX_FIELD (separator) LYNX_FIELD (dock) LYNX_FIELD (latencyMs) LYNX_FIELD (latencyBypass)
                        LYNX_FIELD (compensateMasterLatency) LYNX_FIELD (standIns) LYNX_FIELD (icon)
#undef LYNX_FIELD
                        for (std::size_t e = 0; e < x.effects.size() && e < y.effects.size(); ++e)
                        {
                            const auto &ex = x.effects[e], &ey = y.effects[e];
#define LYNX_FIELD(n) if (! (ex.n == ey.n)) f.add ("fx" + juce::String ((int) e) + "." #n);
                            LYNX_FIELD (kind) LYNX_FIELD (enabled) LYNX_FIELD (params) LYNX_FIELD (mix) LYNX_FIELD (plugin) LYNX_FIELD (sidechain) LYNX_FIELD (sidechain2)
                            LYNX_FIELD (sendTracks) LYNX_FIELD (state) LYNX_FIELD (samplePath) LYNX_FIELD (substituteFor) LYNX_FIELD (presetName) LYNX_FIELD (abOther)
                            LYNX_FIELD (abShowingB) LYNX_FIELD (patcher)
#undef LYNX_FIELD
                            if (! (ex.params == ey.params))
                                for (std::size_t q = 0; q < ex.params.size(); ++q)
                                    if (! (ex.params[q] == ey.params[q]))
                                    {
                                        f.add ("p" + juce::String ((int) q) + "=" + juce::String (ex.params[q], 9) + "/" + juce::String (ey.params[q], 9));
                                        break;
                                    }
                            if (ex.state != ey.state)
                                f.add ("state " + juce::String ((int) ex.state.size()) + "/" + juce::String ((int) ey.state.size()));
                            if (ex.samplePath != ey.samplePath)
                                f.add ("path " + juce::String (ex.samplePath) + " / " + juce::String (ey.samplePath));
                        }
                        line ("       fields: " + f.joinIntoString (",") + " host recordTo=" + juce::String (x.recordTo) + " / " + juce::String (y.recordTo)
                              + " out=" + juce::String (x.outputChannel) + "/" + juce::String (y.outputChannel) + " in=" + juce::String (x.inputChannel) + "/" + juce::String (y.inputChannel));
                    }
            }
    };

    // the host's project: a sampler with its own sample
    {
        auto song = Song::makeDefault();
        song.channels.push_back (sampler ("Host kick", hostWav));
        host.engine->setSong (song);
    }
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    host.live->start (options (host));
    {
        // (the answer first, then the line about it: the order the arguments of one call are evaluated in is not fixed)
        const bool opened = pumpUntil ([&] { return host.live->getState() == LiveSessionManager::State::live || ! host.live->isActive(); }, 20000)
                            && host.live->getState() == LiveSessionManager::State::live;
        if (! check (opened, "host opened the room (" + host.live->getStatus() + ") in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t0)) + " ms"))
            return 1;
    }
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
    // (they knock at the same moment: who is let in first, and so gets tag 2, is the network's choice)
    check (anaTag + liaTag == 5 && (anaTag == 2 || anaTag == 3) && host.engine->getSong().session.participants.size() == 3, "participants: host 1, Ana " + juce::String ((int) anaTag) + ", Lia " + juce::String ((int) liaTag));
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
        // (as the app does for a new channel: the first free insert of her own range; the mixer grows to reach it when
        // she was let in second and her range lies past its end)
        s.channels.back().mixerTrack = 0;
        s.channels.back().mixerTrack = s.claimFreeInsert();
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
        if (anaRecord != nullptr)
            line ("     Ana's area: inserts " + juce::String (anaRecord->insertFirst) + "+" + juce::String (anaRecord->insertCount) + ", rows " + juce::String (anaRecord->rowFirst) + "+"
                  + juce::String (anaRecord->rowCount) + "; the host has " + juce::String ((int) s.mixer.size()) + " mixer tracks, Ana " + juce::String ((int) ana.engine->getSong().mixer.size())
                  + "; her insert's volume on the host "
                  + juce::String (anaRecord->insertFirst < (int) s.mixer.size() ? s.mixer[(std::size_t) anaRecord->insertFirst].volume : -1.0f, 3) + ", " + juce::String (bass >= 0 ? s.channels[(std::size_t) bass].mixerTrack : -1)
                  + " is her channel's insert, " + juce::String ((int) host.live->getStats().rejected) + " refused");
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

    // --- round 169j ------------------------------------------------------------------------------------------------
    using S = LiveSessionManager::State;
    // big samples are hashed off the message thread; PCM WAVs travel as lossless FLAC packs
    check (ana.live->getStats().hashedOffThread >= 1, "Ana's 1.2 MB sample was hashed off the message thread (" + juce::String (ana.live->getStats().hashedOffThread) + ")");
    check (ana.live->getStats().packedSent >= 1 && ana.live->getStats().packedSaved > 0,
           "Ana's WAV went as a FLAC pack: " + juce::String (ana.live->getStats().packedSent) + " sent, "
               + juce::String (ana.live->getStats().packedSaved / 1024) + " KiB saved; the host forwarded " + juce::String (host.live->getStats().packedSent) + " pack(s)");

    // chat: a guest's line reaches everyone through the host, in one order; the host's too
    ana.live->sendChat ("  hello from Ana \x01 ");
    host.live->sendChat ("hi Ana");
    {
        const auto t = juce::Time::getMillisecondCounterHiRes();
        auto has = [] (Peer& p, const juce::String& text) {
            for (const auto& l : p.live->getChat())
                if (l.text == text)
                    return true;
            return false;
        };
        const bool ok = pumpUntil ([&] { return has (rui, "hello from Ana") && has (host, "hello from Ana") && has (ana, "hello from Ana") && has (ana, "hi Ana") && has (rui, "hi Ana"); }, 10000);
        auto order = [] (Peer& p) {
            juce::StringArray lines;
            for (const auto& l : p.live->getChat())
                lines.add (l.name + ": " + l.text);
            return lines.joinIntoString (" | ");
        };
        check (ok && rui.live->unreadChat() >= 2 && order (rui) == order (host) && order (ana) == order (host),
               "chat order everywhere: " + order (host));
        check (ok,
               "chat: Ana's and the host's lines reached everyone in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t)) + " ms (Rui has "
                   + juce::String (rui.live->unreadChat()) + " unread)");
    }

    // follow the host's transport: Rui plays, jumps and stops with the host
    rui.live->setFollowTransport (true);
    host.engine->seek ((double) ppq);
    host.engine->getTransport().positionTicks.store ((double) ppq); // (no audio device here: the playhead is set by hand)
    host.engine->play();
    const auto hostAt = host.engine->songPlayheadTicks();
    pumpUntil ([&] { return rui.engine->getTransport().playing.load(); }, 8000);
    const auto ruiSeek = rui.engine->getTransport().seekRequest.load();
    check (rui.engine->getTransport().playing.load() && hostAt > 0.0 && ruiSeek >= hostAt && ruiSeek < hostAt + 4 * ppq,
           "Rui follows the host: playing from " + juce::String (ruiSeek, 0) + " ticks (the host's " + juce::String (hostAt, 0) + " plus the time on the way)");
    host.engine->stop();
    check (pumpUntil ([&] { return ! rui.engine->getTransport().playing.load(); }, 8000) && ! ana.engine->getTransport().playing.load(),
           "the host stopped: Rui stopped too, Ana (not following) never played");
    rui.live->setFollowTransport (false);

    // permissions during the session: tempo for Ana, listen-only for Rui
    host.live->setPermissions (anaTag, [] { live::Permissions p; p.settings = true; return p; }(), false);
    pumpUntil ([&] { return ana.live->myPermissions().settings; }, 8000);
    ana.engine->editSong ([] (Song& s) { s.bpm = 97.0; });
    const auto ruiTag = rui.live->myTag();
    host.live->setPermissions (ruiTag, [] { live::Permissions p; p.listener = true; return p; }(), false);
    pumpUntil ([&] { return rui.live->myPermissions().listener; }, 8000);
    rui.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "Rui listens"; });
    pumpUntil ([&] { return converged ({ &ana, &rui }) && host.engine->getSong().bpm == 97.0 && patternIndex (rui.engine->getSong(), "Rui listens") < 0; }, 15000);
    check (host.engine->getSong().bpm == 97.0 && rui.engine->getSong().bpm == 97.0 && patternIndex (host.engine->getSong(), "Rui listens") < 0
               && patternIndex (rui.engine->getSong(), "Rui listens") < 0,
           "the host gave Ana the tempo (97 BPM everywhere) and made Rui a listener (his new pattern was undone)");
    host.live->setPermissions (anaTag, {}, false);
    host.live->setPermissions (ruiTag, {}, false);
    pumpUntil ([&] { return ! rui.live->myPermissions().listener && ! ana.live->myPermissions().settings; }, 8000);

    // a guest's connection drops: she keeps working; back by herself, nothing lost on either side
    {
        const auto t = juce::Time::getMillisecondCounterHiRes();
        ana.live->dropConnection();
        check (ana.live->getState() == S::reconnecting, "Ana's connection dropped: " + ana.live->getStatus());
        ana.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "Ana offline"; });
        auto anaOffline = [&] {
            for (const auto& person : host.live->getPeople())
                if (person.tag == anaTag)
                    return ! person.online;
            return false;
        };
        pumpUntil (anaOffline, 8000);
        check (anaOffline(), "the host shows Ana offline (her place is kept)");
        host.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "Host while Ana is away"; });
        const bool back = pumpUntil ([&] { return converged ({ &ana, &rui }) && patternIndex (host.engine->getSong(), "Ana offline") >= 0
                                                  && patternIndex (ana.engine->getSong(), "Host while Ana is away") >= 0; },
                                     30000);
        check (back && ana.live->getStats().reconnects >= 1 && host.live->getStats().replayed >= 1,
               "Ana reconnected and resumed in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t)) + " ms: her offline pattern reached everyone, "
                   + "the host replayed what she missed (" + juce::String (host.live->getStats().replayed) + " replay)");
        explain ({ &ana, &rui });
    }

    // the host's connection drops: the guests wait, edit on, and resume when it is back
    {
        const auto t = juce::Time::getMillisecondCounterHiRes();
        host.live->dropConnection();
        pumpUntil ([&] { return ana.live->getState() == S::waitingHost || host.live->getState() == S::live; }, 8000);
        rui.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "Rui while the host is away"; });
        const bool back = pumpUntil ([&] { return converged ({ &ana, &rui }) && patternIndex (host.engine->getSong(), "Rui while the host is away") >= 0; }, 40000);
        check (back && host.live->getStats().reconnects >= 1,
               "the host's connection dropped and came back: everyone resumed in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t)) + " ms, Rui's edit made meanwhile arrived");
        explain ({ &ana, &rui });
    }

    // live stand-ins: Ana plays a third-party instrument Rui does not have; she renders it, Rui gets the render, and a
    // new one when her part changes
    if (pluginFile.isEmpty())
        line ("skip live stand-ins (no --plugin <file.vst3>)");
    else
    {
        auto& anaHost = ana.engine->getPluginHost();
        const auto added = anaHost.addPluginFile (juce::File (pluginFile));
        juce::PluginDescription instrument;
        bool found = false;
        for (const auto& type : anaHost.getKnownPlugins().getTypes())
            if (type.isInstrument && juce::File (type.fileOrIdentifier).getFileName() == juce::File (pluginFile).getFileName())
                instrument = type, found = true;
        if (check (found, "Ana's computer has the plugin (" + juce::String (added) + " in the file): " + instrument.name))
        {
            const auto t = juce::Time::getMillisecondCounterHiRes();
            ana.engine->editSong ([&] (Song& s) {
                ChannelDef c;
                c.name = "Ana plugin";
                c.instrument = InstrumentKind::plugin;
                c.plugin.id = instrument.createIdentifierString().toStdString();
                c.plugin.name = instrument.name.toStdString();
                const auto* me = s.session.find (s.session.me);
                c.mixerTrack = me != nullptr ? me->insertFirst : 0;
                s.channels.push_back (c);
                Pattern p;
                p.name = "Ana plugin riff";
                for (int n = 0; n < 4; ++n)
                    p.notes.push_back ({ (std::int64_t) n * ppq, ppq / 2, (int) s.channels.size() - 1, 60 + n, 0.9f });
                s.patterns.push_back (p);
            });
            auto standInOf = [&] (Peer& p) -> juce::String {
                const auto& song = p.engine->getSong();
                const auto c = channelIndex (song, "Ana plugin");
                if (c < 0 || song.channels[(std::size_t) c].standIns.empty())
                    return {};
                juce::String all;
                for (const auto& st : song.channels[(std::size_t) c].standIns)
                {
                    const juce::File f (str (st.path));
                    if (! f.existsAsFile())
                        return {};
                    all << sessionpack::hashFile (f) << ";";
                }
                return all;
            };
            const bool arrived = pumpUntil ([&] { return standInOf (rui).isNotEmpty() && converged ({ &ana, &rui }); }, 60000);
            const auto first = standInOf (rui);
            check (arrived && first == standInOf (ana) && ana.live->getStats().standInsRendered >= 1,
                   "Ana rendered her plugin part for Rui (" + juce::String (ana.live->getStats().standInsRendered) + " part(s)), Rui has the same stems in "
                       + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t)) + " ms");
            explain ({ &ana, &rui });
            // her part changes: a new render follows; Lia's… Rui's copy gets it
            const auto t2 = juce::Time::getMillisecondCounterHiRes();
            ana.engine->editSong ([&] (Song& s) {
                const auto p = patternIndex (s, "Ana plugin riff");
                if (p >= 0)
                    s.patterns[(std::size_t) p].notes.front().key = 48;
            });
            const bool updated = pumpUntil ([&] { const auto now = standInOf (rui); return now.isNotEmpty() && now != first && converged ({ &ana, &rui }); }, 60000);
            check (updated && standInOf (rui) == standInOf (ana),
                   "her notes changed: the stand-in was rendered again and reached Rui in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t2)) + " ms");
            // the host has the plugin list of its own (not Ana's): whether it needed the render or not, it has the same song
            explain ({ &ana, &rui });
        }
    }

    // P5-A1, remote plugins (round 190ai): the HOST shares the plugin; Rui, who has no plugins, puts it on a channel of
    // his own; the host's computer renders his part and he hears the render; he turns a parameter; the state never
    // crosses; edits are rate-limited; un-sharing leaves the last stems
    std::uint64_t ruiRemoteUid = 0;
    if (pluginFile.isEmpty())
        line ("skip remote plugins (no --plugin <file.vst3>)");
    else
    {
        auto& plugins = host.engine->getPluginHost();
        plugins.addPluginFile (juce::File (pluginFile));
        juce::PluginDescription instrument;
        bool found = false;
        for (const auto& type : plugins.getKnownPlugins().getTypes())
            if (type.isInstrument && juce::File (type.fileOrIdentifier).getFileName() == juce::File (pluginFile).getFileName())
                instrument = type, found = true;
        const auto id = instrument.createIdentifierString().toStdString();
        juce::String why;
        check (rui.live->sharedPlugins().empty() && ! rui.live->mayUseSharedPlugins(), "nothing is shared until the host shares it");
        check (! rui.live->sharePlugin (id, true), "a guest cannot share plugins");
        const bool sharedOk = found && host.live->sharePlugin (id, true, &why);
        if (check (sharedOk && host.live->sharedPlugins().size() == 1,
                   "the host shares " + instrument.name + " with the session (" + juce::String (sharedOk ? (int) host.live->sharedPlugins().front().params.size() : 0)
                       + " parameters) " + why))
        {
            const auto described = host.live->sharedPlugins().front();
            const bool told = pumpUntil ([&] { return rui.live->sharedPlugins().size() == 1 && ana.live->sharedPlugins().size() == 1; }, 15000);
            check (told && rui.live->sharedPlugins().front() == described && rui.live->mayUseSharedPlugins(),
                   "Rui and Ana got its name and parameter list (" + (described.params.empty() ? juce::String ("none") : str (described.params.front().name) + ", ...") + ")");
            // Rui uses it: a channel of his own, played by the host, and a riff
            const auto t = juce::Time::getMillisecondCounterHiRes();
            rui.engine->editSong ([&] (Song& s) {
                ChannelDef c;
                c.name = "Rui remote";
                c.instrument = InstrumentKind::plugin;
                c.plugin = rui.live->sharedPluginRef (rui.live->sharedPlugins().front());
                const auto* me = s.session.find (s.session.me);
                c.mixerTrack = me != nullptr ? me->insertFirst : 0;
                s.channels.push_back (c);
                Pattern p;
                p.name = "Rui remote riff";
                for (int n = 0; n < 4; ++n)
                    p.notes.push_back ({ (std::int64_t) n * ppq, ppq / 2, (int) s.channels.size() - 1, 57 + 2 * n, 0.9f });
                s.patterns.push_back (p);
            });
            auto place = [&] (Peer& p) {
                const auto c = channelIndex (p.engine->getSong(), "Rui remote");
                return remoteplug::Place { { live::Kind::channel, c >= 0 ? p.engine->getSong().channels[(std::size_t) c].uid : 0, 0 }, -1 };
            };
            auto refOf = [&] (Peer& p) -> PluginRef {
                const auto* r = remoteplug::refAt (p.engine->getSong(), place (p));
                return r != nullptr ? *r : PluginRef();
            };
            // the stems of the channel on a computer: their files' hashes ("" when one is missing) and their source hashes
            auto stemsOf = [&] (Peer& p, bool sources) -> juce::String {
                const auto& song = p.engine->getSong();
                const auto c = channelIndex (song, "Rui remote");
                if (c < 0 || song.channels[(std::size_t) c].standIns.empty())
                    return {};
                juce::String all;
                for (const auto& st : song.channels[(std::size_t) c].standIns)
                {
                    const juce::File f (str (st.path));
                    if (! f.existsAsFile())
                        return {};
                    all << (sources ? str (st.sourceHash) : sessionpack::hashFile (f)) << ";";
                }
                return all;
            };
            auto peakOf = [&] (Peer& p) {
                const auto& song = p.engine->getSong();
                const auto c = channelIndex (song, "Rui remote");
                float peak = 0.0f;
                if (c >= 0)
                    for (const auto& st : song.channels[(std::size_t) c].standIns)
                    {
                        juce::WavAudioFormat wav;
                        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (juce::File (str (st.path)).createInputStream().release(), true));
                        if (reader == nullptr)
                            continue;
                        juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
                        reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                        peak = std::max (peak, b.getMagnitude (0, b.getNumSamples()));
                    }
                return peak;
            };
            using HS = LiveSessionManager::HostedState;
            bool sawUpdating = false;
            const bool arrived = pumpUntil ([&] {
                sawUpdating = sawUpdating || rui.live->hostedState (place (rui)) == HS::updating;
                return stemsOf (rui, false).isNotEmpty() && converged ({ &ana, &rui }) && rui.live->hostedState (place (rui)) == HS::upToDate;
            }, 90000);
            ruiRemoteUid = place (rui).part.a;
            const auto first = stemsOf (rui, true);
            check (arrived && stemsOf (rui, false) == stemsOf (host, false) && host.live->getStats().hostedRendered >= 1 && rui.live->getStats().standInsRendered == 0,
                   "the host rendered Rui's part with its plugin (" + juce::String (host.live->getStats().hostedRendered) + " part(s)); Rui has the stems in "
                       + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t)) + " ms");
            explain ({ &ana, &rui });
            check (sawUpdating && rui.live->hostedState (place (rui)) == HS::upToDate, "Rui's editor said \"updating...\" while the host rendered, then \"up to date\"");
            check (peakOf (rui) > 0.001f, "the render Rui plays is the plugin's sound (peak " + juce::String (peakOf (rui), 3) + ")");
            check (refOf (rui).playedBy == session::hostTag && refOf (host).playedBy == session::hostTag && refOf (rui).state.empty() && refOf (host).state.empty()
                       && refOf (ana).state.empty(),
                   "the channel says \"played by " + rui.live->playedByName (refOf (rui)) + "\" and carries no plugin state on any computer");
            check (host.live->sharedPluginUsers (id).size() == 1, "the host sees who uses it: " + host.live->sharedPluginUsers (id).joinIntoString (", "));
            // (the host itself plays the part live with its own instance)
            auto hostValue = [&] (int index) {
                const auto c = channelIndex (host.engine->getSong(), "Rui remote");
                auto instrument = host.engine->getChannelInstrument (c);
                auto* p = dynamic_cast<PluginInstrument*> (instrument.get());
                return p != nullptr && index < p->getInstance().getParameters().size() ? p->getInstance().getParameters()[index]->getValue() : -1.0f;
            };
            // Rui turns a parameter: a value on his channel; the host applies it and renders again
            if (! described.params.empty())
            {
                const auto& param = described.params.front();
                const auto value = param.def < 0.5f ? 0.9f : 0.1f;
                const auto rendered = host.live->getStats().hostedRendered;
                const auto t2 = juce::Time::getMillisecondCounterHiRes();
                rui.engine->editSong ([&] (Song& s) {
                    if (auto* r = remoteplug::refAt (s, place (rui)))
                        remoteplug::setValue (*r, param.index, value);
                }, false);
                const bool again = pumpUntil ([&] {
                    const auto now = stemsOf (rui, true);
                    return now.isNotEmpty() && now != first && converged ({ &ana, &rui }) && rui.live->hostedState (place (rui)) == HS::upToDate;
                }, 90000);
                check (again && host.live->getStats().hostedRendered > rendered && remoteplug::valueOf (refOf (host), param.index, -1.0f) == value,
                       "Rui set \"" + str (param.name) + "\" to " + juce::String (value) + ": the host rendered again and the new stems reached him in "
                           + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t2)) + " ms");
                check (std::abs (hostValue (param.index) - value) < 0.26f, "the host's running plugin has the value (" + juce::String (hostValue (param.index), 3) + ")");
                // the host changes it in the plugin's own window: its values reach Rui (never a state)
                {
                    const auto c = channelIndex (host.engine->getSong(), "Rui remote");
                    auto instrument = host.engine->getChannelInstrument (c);
                    if (auto* p = dynamic_cast<PluginInstrument*> (instrument.get()))
                        p->setParameterFromUi (param.index, param.def < 0.5f ? 0.6f : 0.4f);
                    settle (300);
                    host.live->captureHostedValues();
                    const auto want = hostValue (param.index);
                    const bool reached = pumpUntil ([&] { return converged ({ &ana, &rui }) && std::abs (remoteplug::valueOf (refOf (rui), param.index, param.def) - want) < 0.003f; }, 20000);
                    check (reached && refOf (rui).state.empty() && std::abs (want - value) > 0.05f,
                           "the host changed it on its side (" + juce::String (want, 3) + "): Rui's editor shows the host's value, still no state");
                }
            }
            // a guest's computer that sends a state for the host's plugin anyway: dropped by the host
            rui.engine->editSong ([&] (Song& s) {
                if (auto* r = remoteplug::refAt (s, place (rui)))
                    r->state = "QUJDRA==";
            }, false);
            settle (1500);
            const bool clean = pumpUntil ([&] { return converged ({ &ana, &rui }) && refOf (rui).state.empty(); }, 15000);
            check (clean && refOf (host).state.empty() && refOf (ana).state.empty(), "a state sent by a guest for the host's plugin is dropped (nobody has it)");
            // too many parameter changes at once: refused
            {
                const auto before = refOf (rui).values.size();
                rui.hints.clear();
                rui.engine->editSong ([&] (Song& s) {
                    if (auto* r = remoteplug::refAt (s, place (rui)))
                        for (int i = 0; i < 400; ++i)
                            remoteplug::setValue (*r, 100 + i, 0.5f);
                }, false);
                const bool refused = pumpUntil ([&] { return host.live->getStats().valueEditsRefused >= 1 && converged ({ &ana, &rui }) && refOf (rui).values.size() == before; }, 15000);
                check (refused && refOf (host).values.size() == before && rui.hints.joinIntoString ("|").contains ("Too many"),
                       "400 parameter changes in one go were refused by the host (rate limit): " + rui.hints.joinIntoString (" | ").substring (0, 120));
            }
            // "listen only" guests are not offered shared plugins
            host.live->setPermissions (anaTag, [] { live::Permissions p; p.listener = true; return p; }(), false);
            const bool hidden = pumpUntil ([&] { return ana.live->sharedPlugins().empty(); }, 10000);
            host.live->setPermissions (anaTag, {}, false);
            const bool shown = pumpUntil ([&] { return ana.live->sharedPlugins().size() == 1; }, 10000);
            check (hidden && shown, "a \"listen only\" guest gets no shared plugins; an editor again does");
            // the host stops sharing: Rui's editor says so, his notes change, the last stems stay as they are
            pumpUntil ([&] { return converged ({ &ana, &rui }) && rui.live->hostedState (place (rui)) == HS::upToDate && host.live->standInsRendering() == 0; }, 90000);
            const auto kept = stemsOf (rui, false);
            const auto renderedBefore = host.live->getStats().hostedRendered;
            host.live->sharePlugin (id, false);
            const bool gone = pumpUntil ([&] { return rui.live->sharedPlugins().empty() && rui.live->hostedState (place (rui)) == HS::notShared; }, 10000);
            rui.engine->editSong ([&] (Song& s) {
                const auto p = patternIndex (s, "Rui remote riff");
                if (p >= 0)
                    s.patterns[(std::size_t) p].notes.front().key = 45;
            });
            pumpUntil ([&] { return converged ({ &ana, &rui }); }, 15000);
            settle (2500);
            check (gone && ! rui.live->mayUseSharedPlugins() && stemsOf (rui, false) == kept && host.live->getStats().hostedRendered == renderedBefore,
                   "the host stopped sharing: Rui is told, his last stems keep playing and nothing new is rendered");
            // shared again: the part is rendered with the notes as they are now
            host.live->sharePlugin (id, true);
            const bool backAgain = pumpUntil ([&] { return stemsOf (rui, false).isNotEmpty() && stemsOf (rui, false) != kept && converged ({ &ana, &rui })
                                                           && rui.live->hostedState (place (rui)) == HS::upToDate; }, 90000);
            check (backAgain, "shared again: the host rendered Rui's changed notes");
            explain ({ &ana, &rui });
        }
    }

    // the host's LYNX SHARE "crashes" and restarts: it resumes from its journal, the guests come back by themselves
    {
        pumpUntil ([] { return false; }, 2600); // (the journal is written at most every 2 s)
        const auto journal = dir.getChildFile ("host journal.json");
        const auto info = LiveSessionManager::readJournal (journal);
        check (info.has_value() && info->guests == 2 && info->code == code, "the host's journal: " + juce::String (info ? info->guests : 0) + " guests, revision "
                                                                              + juce::String ((juce::int64) (info ? info->revision : 0)));
        const auto t = juce::Time::getMillisecondCounterHiRes();
        host.live->abandon();
        host.live.reset();
        pumpUntil ([&] { return ana.live->getState() == S::waitingHost && rui.live->getState() == S::waitingHost; }, 10000);
        check (ana.live->getState() == S::waitingHost, "the guests wait for the host: " + ana.live->getStatus());
        ana.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "Ana while the host restarts"; });
        host.live = std::make_unique<LiveSessionManager> (*host.engine);
        host.live->onHint = [raw = &host] (const juce::String& h) { raw->hints.add (h); };
        const bool resumed = host.live->resumeHost (journal, options (host));
        const bool back = resumed && pumpUntil ([&] { return host.live->getState() == S::live && converged ({ &ana, &rui })
                                                             && patternIndex (host.engine->getSong(), "Ana while the host restarts") >= 0
                                                             && host.live->getPeople().size() == 3 && host.live->getPeople().back().online; },
                                                40000);
        check (back, "the host restarted from its journal; both guests resumed and Ana's edit made meanwhile arrived in "
                         + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t)) + " ms (" + host.live->getStatus() + ")");
        explain ({ &ana, &rui });
        host.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "After the restart"; });
        check (pumpUntil ([&] { return converged ({ &ana, &rui }) && patternIndex (rui.engine->getSong(), "After the restart") >= 0; }, 15000),
               "the session goes on after the host's restart");
    }

    // --- round 190ah ------------------------------------------------------------------------------------------------
    // every computer computes the same digest of the confirmed song (what two real computers compare, --livepeer)
    {
        pumpUntil ([&] { return converged ({ &ana, &rui }); }, 10000);
        const auto h = host.live->digest(), a = ana.live->digest(), r = rui.live->digest();
        check (h.isNotEmpty() && h == a && h == r, "the song's digest is the same on the three computers: " + h + " / " + a + " / " + r);
    }

    // a guest reaches the limit for audio files (3 MiB per guest here): the host is asked, says no, then yes
    {
        std::vector<LiveSessionManager::MediaLimit> asked;
        host.live->onMediaLimit = [&] (const LiveSessionManager::MediaLimit& l) { asked.push_back (l); };
        const auto bigWav = dir.getChildFile ("ana/noise take.wav");
        writeNoise (bigWav, 2400000, 7); // 4.8 MB that FLAC cannot shrink
        ana.hints.clear();
        ana.engine->editSong ([&] (Song& s) { s.channels.push_back (sampler ("Ana take", bigWav)); });
        // (not converged(): that compares the files' contents, and this file is meant to be on Ana's computer only)
        const bool refused = pumpUntil ([&] { return ! asked.empty() && channelIndex (host.engine->getSong(), "Ana take") >= 0 && channelIndex (rui.engine->getSong(), "Ana take") >= 0
                                                     && ana.live->waitingChanges() == 0 && rui.live->waitingChanges() == 0 && host.live->mediaDownloading() == 0
                                                     && rui.live->mediaDownloading() == 0 && ana.hints.joinIntoString ("|").contains ("noise take.wav"); },
                                        30000);
        auto fileOn = [&] (Peer& p) {
            const auto k = channelIndex (p.engine->getSong(), "Ana take");
            return k >= 0 && juce::File (str (p.engine->getSong().channels[(std::size_t) k].samplePath)).existsAsFile();
        };
        check (refused && asked.size() == 1 && ! asked[0].session && asked[0].name == "Ana" && ! fileOn (host) && ! fileOn (rui) && host.live->getStats().mediaRefused >= 1,
               "Ana's 4.8 MB take is over her limit: the host is asked (" + (asked.empty() ? juce::String ("no question") : asked[0].name + ", " + str (live::mediaSize (asked[0].used)) + " of "
                                                                                                                              + str (live::mediaSize (asked[0].limit)))
                   + "), the edit itself arrived, the file stays with Ana: " + ana.hints.joinIntoString (" | ").substring (0, 200)
                   + (refused && asked.size() == 1 && ! fileOn (host) && ! fileOn (rui) ? juce::String()
                          : " [settled " + juce::String ((int) refused) + ", questions " + juce::String ((int) asked.size()) + ", file on host " + juce::String ((int) fileOn (host))
                                + ", on Rui " + juce::String ((int) fileOn (rui)) + ", fetching host/Ana/Rui " + juce::String (host.live->mediaDownloading()) + "/"
                                + juce::String (ana.live->mediaDownloading()) + "/" + juce::String (rui.live->mediaDownloading()) + ", waiting Ana/Rui "
                                + juce::String (ana.live->waitingChanges()) + "/" + juce::String (rui.live->waitingChanges()) + ", refused " + juce::String (host.live->getStats().mediaRefused)
                                + ", differs " + differences (host.engine->getSong(), ana.engine->getSong()).substring (0, 80) + " / "
                                + differences (host.engine->getSong(), rui.engine->getSong()).substring (0, 80) + "]"));
        if (! asked.empty() && asked[0].answer)
            asked[0].answer (true);
        const bool arrived = pumpUntil ([&] { return fileOn (host) && fileOn (rui) && converged ({ &ana, &rui }); }, 40000);
        check (arrived && host.live->mediaBudget().limitOf (anaTag) > (3 << 20),
               "the host raised Ana's limit to " + str (live::mediaSize (host.live->mediaBudget().limitOf (anaTag))) + ": the take reached the host and Rui ("
                   + str (live::mediaSize (host.live->mediaBudget().usedBy (anaTag))) + " from Ana, " + str (live::mediaSize (host.live->mediaBudget().session)) + " in the session)");
        host.live->onMediaLimit = nullptr;
    }

    // Ana's LYNX SHARE "crashes" with an edit the host has not confirmed, and restarts: she goes back in from her
    // journal as herself (same tag, permissions, no question to the host), nothing lost
    {
        host.live->setPermissions (anaTag, [] { live::Permissions p; p.settings = true; return p; }(), false);
        pumpUntil ([&] { return ana.live->myPermissions().settings; }, 8000);
        ana.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "Ana before her restart"; });
        pumpUntil ([&] { return converged ({ &ana, &rui }) && patternIndex (host.engine->getSong(), "Ana before her restart") >= 0; }, 10000);
        // (only Ana's computer runs for a while: her next edit goes out but no answer comes back before the crash)
        ana.engine->editSong ([] (Song& s) { s.patterns.emplace_back().name = "Ana unconfirmed"; });
        {
            const auto until = juce::Time::getMillisecondCounter() + 2700u; // (her journal is written at most every 2 s)
            while (juce::Time::getMillisecondCounter() < until)
            {
                ana.live->pump();
                juce::Thread::sleep (5);
            }
        }
        const auto journal = dir.getChildFile ("Ana guest journal.json");
        const auto info = LiveSessionManager::readGuestJournal (journal);
        check (info.has_value() && info->hostName == "Host" && info->name == "Ana",
               "Ana's journal: the session of " + (info ? info->hostName : juce::String ("?")) + ", revision " + juce::String ((juce::int64) (info ? info->revision : 0)));
        const auto peopleBefore = host.engine->getSong().session.participants.size();
        const auto t = juce::Time::getMillisecondCounterHiRes();
        ana.live->abandon();
        ana.live.reset();
        ana.engine = std::make_unique<AudioEngine> (false); // (a new process: nothing of the old one is left)
        ana.live = std::make_unique<LiveSessionManager> (*ana.engine);
        ana.live->onHint = [raw = &ana] (const juce::String& h) { raw->hints.add (h); };
        const bool resumed = ana.live->resumeGuest (journal, options (ana));
        auto anaOnline = [&] {
            for (const auto& person : host.live->getPeople())
                if (person.tag == anaTag)
                    return person.online;
            return false;
        };
        const bool back = resumed && pumpUntil ([&] { return converged ({ &ana, &rui }) && anaOnline() && patternIndex (host.engine->getSong(), "Ana unconfirmed") >= 0; }, 40000);
        check (back && ana.live->myTag() == anaTag && ana.live->getStats().rejoined == 1 && host.engine->getSong().session.participants.size() == peopleBefore
                   && host.live->getPeople().size() == 3 && ana.live->myPermissions().settings && patternIndex (ana.engine->getSong(), "Ana before her restart") >= 0,
               "Ana's LYNX SHARE restarted: back as herself (tag " + juce::String ((int) ana.live->myTag()) + ", " + juce::String ((int) host.live->getPeople().size())
                   + " people, her permission kept) in " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t)) + " ms; her unconfirmed pattern reached everyone ("
                   + ana.live->getStatus() + ")");
        explain ({ &ana, &rui });
        // the session goes on for her: an edit her permission allows, a new file of hers, a new file of the host's
        const auto afterWav = dir.getChildFile ("ana/after.wav"), hostAfter = dir.getChildFile ("host/after.wav");
        writeWav (afterWav, 220.0f, 30000);
        writeWav (hostAfter, 330.0f, 30000);
        ana.engine->editSong ([&] (Song& s) { s.bpm = 101.0; s.channels.push_back (sampler ("Ana after", afterWav)); });
        host.engine->editSong ([&] (Song& s) { s.channels.push_back (sampler ("Host after", hostAfter)); });
        auto has = [&] (Peer& p, const char* name) {
            const auto k = channelIndex (p.engine->getSong(), name);
            return k >= 0 && juce::File (str (p.engine->getSong().channels[(std::size_t) k].samplePath)).existsAsFile();
        };
        check (pumpUntil ([&] { return converged ({ &ana, &rui }) && host.engine->getSong().bpm == 101.0 && has (host, "Ana after") && has (rui, "Ana after") && has (ana, "Host after"); }, 20000),
               "after her restart: her tempo change (101 BPM) and new sample reached everyone, the host's new sample reached her");
        explain ({ &ana, &rui });
        host.live->setPermissions (anaTag, {}, false);
        // her LYNX SHARE closes by itself (the window is closed): the journal stays, and the host keeps her place
        ana.live.reset();
        const auto closed = LiveSessionManager::readGuestJournal (journal);
        pumpUntil ([&] { return ! anaOnline(); }, 10000);
        check (closed.has_value() && ! anaOnline() && host.live->getPeople().size() == 3, "Ana closed LYNX SHARE: her journal stays and the host keeps her place (offline)");
        ana.engine = std::make_unique<AudioEngine> (false);
        ana.live = std::make_unique<LiveSessionManager> (*ana.engine);
        ana.live->onHint = [raw = &ana] (const juce::String& h) { raw->hints.add (h); };
        const bool again = ana.live->resumeGuest (journal, options (ana)) && pumpUntil ([&] { return converged ({ &ana, &rui }) && anaOnline(); }, 40000);
        check (again && ana.live->myTag() == anaTag, "she opens it again and goes back in a second time (" + ana.live->getStatus() + ")");
    }

    // Rui's LYNX SHARE restarts and he chooses not to go back: the host is told, his place is free again
    {
        pumpUntil ([] { return false; }, 2600);
        const auto journal = dir.getChildFile ("Rui guest journal.json");
        const bool written = LiveSessionManager::readGuestJournal (journal).has_value();
        rui.live->abandon();
        rui.live.reset();
        pumpUntil ([&] { return host.live->getPeople().size() == 3 && ! host.live->getPeople().back().online; }, 10000);
        LiveSessionManager::discardGuestJournal (journal, relay);
        const bool freed = pumpUntil ([&] { return host.live->getPeople().size() == 2; }, 20000);
        check (written && freed && ! journal.existsAsFile(), "Rui did not go back in: his journal is gone and the host shows " + juce::String ((int) host.live->getPeople().size()) + " people");
        rui.engine = std::make_unique<AudioEngine> (false);
        rui.live = std::make_unique<LiveSessionManager> (*rui.engine);
        rui.live->onHint = [raw = &rui] (const juce::String& h) { raw->hints.add (h); };
    }

    // the host ends it: the guests keep their parts
    const auto stats = host.live->getStats();
    host.live->leave();
    check (! dir.getChildFile ("host journal.json").existsAsFile(), "the host ended the session: its journal is gone");
    pumpUntil ([&] { return ! ana.live->isActive() && ! rui.live->isActive(); }, 10000);
    {
        const auto& a = ana.engine->getSong();
        bool onlyHers = ! a.channels.empty();
        for (const auto& c : a.channels)
            onlyHers = onlyHers && uids::tagOf (c.uid) == anaTag;
        check (! ana.live->isActive() && ! rui.live->isActive() && onlyHers && patternIndex (a, "Ana riff") >= 0,
               "the host ended the session: Ana keeps her part (" + ana.live->getStatus() + ")");
        check (! dir.getChildFile ("Ana guest journal.json").existsAsFile(), "the session is over: Ana's journal is gone");
    }

    // round 190ah: the session's own limit (1 MiB here): the host's 1.9 MB sample is not sent, both sides are told
    {
        const auto bigWav = dir.getChildFile ("host/noise bed.wav");
        writeNoise (bigWav, 960000, 11);
        auto song = Song::makeDefault();
        song.channels.push_back (sampler ("Host bed", bigWav));
        host.engine->setSong (song);
        auto o = options (host);
        o.sessionMediaLimit = 1 << 20;
        std::vector<LiveSessionManager::MediaLimit> told;
        host.live->onMediaLimit = [&] (const LiveSessionManager::MediaLimit& l) { told.push_back (l); };
        host.live->start (o);
        const bool open = pumpUntil ([&] { return host.live->getState() == S::live || ! host.live->isActive(); }, 20000) && host.live->getState() == S::live;
        lia.hints.clear();
        if (check (open, "a second session with a 1 MiB limit for the whole session (" + host.live->getStatus() + ")"))
        {
            lia.live->join (host.live->getInviteCode(), options (lia));
            const bool limited = pumpUntil ([&] { return lia.live->getState() == S::live && ! told.empty() && lia.live->mediaDownloading() == 0
                                                         && lia.hints.joinIntoString ("|").contains ("limit"); },
                                            30000);
            check (limited && told.size() == 1 && told[0].session && channelIndex (lia.engine->getSong(), "Host bed") >= 0 && host.live->mediaBudget().session == 0,
                   "the session's limit: the host's sample is not sent, the host is told once and Lia too: " + lia.hints.joinIntoString (" | ").substring (0, 160));
            host.live->leave();
            pumpUntil ([&] { return ! lia.live->isActive(); }, 10000);
        }
        host.live->onMediaLimit = nullptr;
    }
    if (ruiRemoteUid != 0)
    {
        // what the guest keeps follows the keep policy: his own part, with the audio stand-ins only
        const auto* kept = remoteplug::refAt (rui.engine->getSong(), { { live::Kind::channel, ruiRemoteUid, 0 }, -1 });
        const auto c = channelIndex (rui.engine->getSong(), "Rui remote");
        const auto* hostRef = remoteplug::refAt (host.engine->getSong(), { { live::Kind::channel, ruiRemoteUid, 0 }, -1 });
        check (kept != nullptr && c >= 0 && ! rui.engine->getSong().channels[(std::size_t) c].standIns.empty() && kept->playedBy == 0 && kept->state.empty()
                   && kept->values.empty(),
               "Rui keeps his channel with the audio stand-ins only (no plugin state, no values)");
        check (hostRef != nullptr && hostRef->playedBy == 0 && hostRef->values.empty() && ! hostRef->state.empty(),
               "the host's project keeps the plugin as it sounded (its real state, " + juce::String (hostRef != nullptr ? (int) hostRef->state.size() : 0) + " bytes)");
    }
    line ("     host: " + juce::String (stats.sent) + " frames sent (" + juce::String (stats.bytesSent / 1024) + " KiB), " + juce::String (stats.received) + " received, "
          + juce::String (stats.commits) + " commits, " + juce::String (stats.rejected) + " refused, " + juce::String (stats.mediaSent) + " media sent");
    line ("     ana: " + juce::String (ana.live->getStats().mediaSent) + " media sent, " + juce::String (ana.live->getStats().mediaReceived) + " received, "
          + juce::String (ana.live->getStats().reconnects) + " reconnects, " + juce::String (ana.live->getStats().standInsRendered) + " stand-ins rendered");
    line ("     total " + juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t0)) + " ms");
    for (auto& p : peers)
        p->live.reset();
    line (ok ? "result=OK" : "result=FAILED");
    dir.deleteRecursively();
    return ok ? 0 : 1;
}

// --- one peer of a session between two real computers (round 190ah) ---------------------------------------------------
//
// LYNX --livepeer host <folder> [--relay URL]             opens a session, writes <folder>/code.txt, then works through
//                                                         the script below with whoever joins; <folder>/host.txt
// LYNX --livepeer guest <folder> --code <code|@file>      joins, edits, then "crashes" (exit code 7, journal kept)
// LYNX --livepeer rejoin <folder>                         the same guest after its restart: goes back in from its journal
//
// The two sides talk through the session's own chat ("digest <step> <hash>@<rev>"): the host compares each digest the
// guest reports with its own at that revision, so "ok" means both computers really hold the same song.
int runPeer (const juce::StringArray& args)
{
    const auto i = args.indexOf ("--livepeer");
    const auto role = args[i + 1];
    const juce::File dir (args[i + 2].unquoted());
    dir.createDirectory();
    juce::String relay = "https://relay.lynx-share.com";
    if (const auto r = args.indexOf ("--relay"); r >= 0)
        relay = args[r + 1].unquoted();
    juce::String code;
    if (const auto r = args.indexOf ("--code"); r >= 0)
        code = args[r + 1].unquoted();
    if (code.startsWith ("@"))
        code = juce::File (code.substring (1)).loadFileAsString().trim();
    const bool isHost = role == "host";
    const auto report = dir.getChildFile (isHost ? "host.txt" : role == "rejoin" ? "rejoin.txt" : "guest.txt");
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
    using S = LiveSessionManager::State;
    AudioEngine engine (false);
    LiveSessionManager live (engine);
    juce::StringArray hints;
    live.onHint = [&] (const juce::String& h) { hints.add (h); };
    LiveSessionManager::Options o;
    o.relay = relay;
    o.name = isHost ? "Host PC" : "Guest PC";
    o.autoAccept = true;
    o.folder = dir.getChildFile ("media");
    o.journal = dir.getChildFile ("host journal.json");
    o.guestJournal = dir.getChildFile ("guest journal.json");
    o.inlineWork = true;
    auto pumpUntil = [&] (const std::function<bool()>& done, int timeoutMs) {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < until)
        {
            live.pump();
            if (done())
                return true;
            juce::Thread::sleep (5);
        }
        return false;
    };
    auto said = [&] (const juce::String& prefix) -> juce::String {
        juce::String found;
        for (const auto& l : live.getChat())
            if (! l.mine && l.text.startsWith (prefix))
                found = l.text.substring (prefix.length()).trim();
        return found;
    };
    // nothing waiting here, every file here, and the confirmed song's digest
    auto settled = [&] { return live.getState() == S::live && live.mediaDownloading() == 0 && live.waitingChanges() == 0; };
    auto filesHere = [&] {
        for (const auto& path : safety::referencedSamples (engine.getSong()))
            if (! juce::File (str (path)).existsAsFile())
                return false;
        return true;
    };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto ms = [&] { return juce::String ((int) (juce::Time::getMillisecondCounterHiRes() - t0)) + " ms"; };

    if (isHost)
    {
        const auto wav = dir.getChildFile ("own/host kick.wav");
        writeWav (wav, 60.0f, 600000); // 1.2 MB: a FLAC pack, several pieces
        auto song = Song::makeDefault();
        song.channels.push_back (sampler ("Host kick", wav));
        engine.setSong (song);
        live.start (o);
        const bool opened = pumpUntil ([&] { return live.getState() == S::live || ! live.isActive(); }, 30000) && live.getState() == S::live;
        if (! check (opened, "the room is open on " + relay + " (" + live.getStatus() + ") after " + ms()))
            return 1;
        dir.getChildFile ("code.txt").replaceWithText (live.getInviteCode());
        // a step: the guest reports its digest; ours at the same revision must be the same
        auto step = [&] (const juce::String& name, int timeoutMs, const std::function<bool()>& also) {
            juce::String theirs, mine;
            const bool same = pumpUntil ([&] {
                theirs = said ("digest " + name + " ");
                mine = settled() ? live.digest() : juce::String();
                return theirs.isNotEmpty() && theirs == mine && also();
            }, timeoutMs);
            return check (same, name + ": the guest's digest " + (theirs.isEmpty() ? juce::String ("(none)") : theirs) + ", this computer's " + mine + " after " + ms());
        };
        auto guestOnline = [&] {
            const auto people = live.getPeople();
            return people.size() == 2 && people.back().online;
        };
        {
            const bool good = pumpUntil (guestOnline, 600000); // (the wait first, then the text: the order of arguments is the compiler's)
            check (good, "a guest joined: " + (live.getPeople().size() > 1 ? live.getPeople().back().name : juce::String ("nobody")) + " after " + ms());
        }
        const auto guestTag = live.getPeople().size() > 1 ? live.getPeople().back().tag : 0u;
        step ("joined", 120000, [&] { return channelIndex (engine.getSong(), "Guest bass") >= 0 && filesHere(); });
        check (live.getStats().mediaSent >= 1 && live.getStats().mediaReceived >= 1,
               "files went both ways: " + juce::String (live.getStats().mediaSent) + " sent, " + juce::String (live.getStats().mediaReceived) + " received ("
                   + str (live::mediaSize (live.mediaBudget().session)) + " of the session's " + str (live::mediaSize (live.mediaBudget().sessionLimit)) + ")");
        // the guest "crashes": its place stays; meanwhile the host edits on
        {
            const bool good = pumpUntil ([&] { return live.getPeople().size() == 2 && ! live.getPeople().back().online; }, 120000); // (the wait first, then the text: the order of arguments is the compiler's)
            check (good, "the guest's LYNX SHARE went away: its place is kept (offline) after " + ms());
        }
        engine.editSong ([] (Song& s) { s.bpm = 123.0; s.patterns.emplace_back().name = "Host while the guest restarts"; });
        // it comes back as itself
        const bool cameBack = pumpUntil (guestOnline, 300000);
        check (cameBack && live.getPeople().back().tag == guestTag && engine.getSong().session.participants.size() == 2,
               "the guest is back as itself: tag " + juce::String ((int) live.getPeople().back().tag) + ", " + juce::String ((int) engine.getSong().session.participants.size())
                   + " participants in the song, after " + ms());
        step ("rejoined", 120000, [&] { return patternIndex (engine.getSong(), "Guest after its restart") >= 0 && patternIndex (engine.getSong(), "Guest unconfirmed") >= 0 && filesHere(); });
        live.sendChat ("done");
        pumpUntil ([] { return false; }, 1500);
        const auto stats = live.getStats();
        line ("     host: " + juce::String (stats.sent) + " frames sent (" + juce::String (stats.bytesSent / 1024) + " KiB), " + juce::String (stats.received) + " received ("
              + juce::String (stats.bytesReceived / 1024) + " KiB), " + juce::String (stats.commits) + " commits, " + juce::String (stats.replayed) + " replays, "
              + juce::String (stats.resumedBySnapshot) + " snapshot resumes");
        live.leave();
    }
    else if (role == "guest")
    {
        live.join (code, o);
        const bool in = pumpUntil ([&] { return (settled() && filesHere()) || ! live.isActive(); }, 120000) && live.getState() == S::live;
        if (! check (in, "joined and has the project with its files (" + live.getStatus() + ") after " + ms()))
            return 1;
        check (channelIndex (engine.getSong(), "Host kick") >= 0, "the host's channel and sample are here; this guest is tag " + juce::String ((int) live.myTag()));
        const auto wav = dir.getChildFile ("own/guest bass.wav");
        writeWav (wav, 110.0f, 600000);
        engine.editSong ([&] (Song& s) {
            s.channels.push_back (sampler ("Guest bass", wav));
            s.channels.back().mixerTrack = 0;
            s.channels.back().mixerTrack = s.claimFreeInsert(); // (as the app does: the first free insert of this guest's range)
            s.patterns.emplace_back().name = "Guest riff";
        });
        {
            const bool good = pumpUntil ([&] { return settled() && live.getStats().mediaSent >= 1 && channelIndex (engine.getSong(), "Guest bass") >= 0 && engine.hasSessionHistory(); }, 120000); // (the wait first, then the text: the order of arguments is the compiler's)
            check (good,
               "this guest's channel, pattern and 1.2 MB sample went to the host after " + ms());
        }
        pumpUntil ([] { return false; }, 1500);
        pumpUntil ([&] { return settled() && live.pendingEdits() == 0; }, 30000);
        live.sendChat ("digest joined " + live.digest());
        pumpUntil ([] { return false; }, 2600); // (the journal: at most every 2 s)
        // an edit that never left this computer before it closes: it is in the journal, and goes out after the restart
        engine.editSong ([] (Song& s) { s.patterns.emplace_back().name = "Guest unconfirmed"; });
        line ("     tag " + juce::String ((int) live.myTag()) + ", digest " + live.digest());
        dir.getChildFile ("tag.txt").replaceWithText (juce::String ((int) live.myTag()));
        // the window is closed with the session running: the journal is written, no goodbye is said
        line (ok ? "result=OK (closing with the session running)" : "result=FAILED");
        return ok ? 7 : 1;
    }
    else
    {
        const auto journal = dir.getChildFile ("guest journal.json");
        const auto info = LiveSessionManager::readGuestJournal (journal);
        if (! check (info.has_value(), "the journal of the session with " + (info ? info->hostName : juce::String ("?")) + " is here"))
            return 1;
        const auto tagBefore = dir.getChildFile ("tag.txt").loadFileAsString().getIntValue();
        const bool resumed = live.resumeGuest (journal, o);
        // before the host is even reached: this guest goes on working
        engine.editSong ([] (Song& s) { s.patterns.emplace_back().name = "Guest after its restart"; });
        const bool back = resumed && pumpUntil ([&] { return (settled() && filesHere() && patternIndex (engine.getSong(), "Host while the guest restarts") >= 0) || ! live.isActive(); }, 180000)
                          && live.getState() == S::live;
        check (back && (int) live.myTag() == tagBefore && live.getStats().rejoined == 1,
               "back in the session as the same guest (tag " + juce::String ((int) live.myTag()) + ", was " + juce::String (tagBefore) + "; " + live.getStatus() + ") after " + ms());
        check (patternIndex (engine.getSong(), "Guest unconfirmed") >= 0 && patternIndex (engine.getSong(), "Guest riff") >= 0 && engine.getSong().bpm == 123.0,
               "its own patterns are here (the unconfirmed one too) and the host's changes made meanwhile arrived (123 BPM)");
        pumpUntil ([] { return false; }, 2500);
        pumpUntil ([&] { return settled() && live.pendingEdits() == 0; }, 30000);
        live.sendChat ("digest rejoined " + live.digest());
        line ("     digest " + live.digest());
        const bool confirmed = pumpUntil ([&] { return said ("done").isNotEmpty() || ! live.isActive(); }, 180000);
        check (confirmed, "the host confirmed the same song and ended the session (" + live.getStatus() + ")");
        pumpUntil ([&] { return ! live.isActive(); }, 15000);
        check (! live.isActive() && ! journal.existsAsFile(), "the session is over: this guest's journal is gone (" + live.getStatus() + ")");
    }
    line ("     hints: " + hints.joinIntoString (" | ").substring (0, 600));
    line (ok ? "result=OK" : "result=FAILED");
    return ok ? 0 : 1;
}
} // namespace lynx::livetest
