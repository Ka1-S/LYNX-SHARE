#include "engine/RemoteInstrumentEdits.h"
#include "engine/RemoteJsonLimits.h"
#include "engine/RemoteAuthentication.h"
#include <cstdio>
#include <stdexcept>

using namespace lynx;
namespace
{
void check (bool condition, const char* message)
{
    if (! condition) throw std::runtime_error (message);
}
template <typename Fn> void refuses (Fn&& fn)
{
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    check (rejected, "invalid instrument transaction was accepted");
}
Song fixture()
{
    Song song;
    song.channels.resize (3);
    song.channels[0].instrument = InstrumentKind::layer;
    song.channels[0].layer.children = { 1, 2 };
    auto& channel = song.channels[1];
    channel.name = "Lead";
    channel.instrument = InstrumentKind::plugin;
    channel.plugin.id = "existing synth";
    channel.plugin.state = "existing state";
    channel.uid = 1234;
    channel.mixerTrack = 2;
    channel.volume = 0.23f;
    channel.pan = -0.4f;
    channel.muted = true;
    channel.group = 4;
    channel.swingMix = 0.13f;
    channel.colour = 0xff112233;
    channel.maxPolyphony = 4;
    channel.cutGroup = 2;
    channel.cutBy = 3;
    channel.keyLabels[60] = "C custom";
    channel.ghostHidden = true;
    channel.soloLock = true;
    channel.keyLow = 30;
    channel.keyHigh = 90;
    channel.receiveFrom = "Controller";
    channel.receiveMidiChannel = 3;
    channel.icon = 42;
    song.channels[2].instrument = InstrumentKind::audioClip;
    song.mixer.resize (4);
    song.patterns.resize (1);
    Note note;
    note.channel = 1;
    note.velocity = 0.44f;
    note.slide = true;
    note.modX = 0.21f;
    song.patterns[0].notes.push_back (note);
    song.patterns[0].channelLoops.push_back ({ 1, 8 });
    PatternEvents lane;
    lane.target = { AutomationTargetKind::channelParam, 1, 0, 3 };
    lane.events.push_back ({ 100, 0.8f });
    song.patterns[0].events.push_back (lane);
    Automation automation;
    automation.target = { AutomationTargetKind::channelVolume, 1 };
    automation.source.channel = 1;
    automation.points.push_back ({ 0, 0.25f });
    song.automations.push_back (automation);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 2;
    clip.length = 960;
    song.playlist.push_back (clip);
    song.arrangements.push_back ({ "Other", { clip } });
    return song;
}
void instrumentEdits()
{
    const auto original = fixture();
    ChannelDef generator;
    generator.instrument = InstrumentKind::snare;
    generator.name = "New Snare";
    auto inserted = remote_instruments::create (original, generator, 1, 3);
    const auto& song = inserted.song;
    check (inserted.channelIndex == 1 && song.channels.size() == 4, "middle insertion failed");
    check (song.channels[1].mixerTrack == 3 && song.channels[1].instrument == InstrumentKind::snare, "selected generator or route lost");
    check (song.channels[2] == original.channels[1] && song.channels[2].instance.id == original.channels[1].instance.id,
           "middle insertion changed an existing hosted channel/instance");
    auto expectedNote = original.patterns[0].notes[0];
    expectedNote.channel = 2;
    check (song.patterns[0].notes[0] == expectedNote, "note insertion remap lost note expression");
    check (song.patterns[0].channelLoops[0].channel == 2 && song.patterns[0].channelLoops[0].setting == 8, "loop reference was not remapped");
    check (song.automations[0].target.index == 2 && song.automations[0].source.channel == 2, "automation references were not remapped");
    check (song.patterns[0].events[0].target.index == 2 && song.patterns[0].events[0].events == original.patterns[0].events[0].events,
           "pattern automation remap lost events");
    check (song.channels[0].layer.children == std::vector<int> ({ 2, 3 }), "Layer children were not remapped");
    check (song.playlist[0].channel == 3 && song.arrangements[1].clips[0].channel == 3, "audio sources were not remapped in every arrangement");
    check (original.channels.size() == 3 && original.patterns[0].notes[0].channel == 1, "staged insertion changed original");

    auto appended = remote_instruments::create (original, generator);
    check (appended.channelIndex == 3 && appended.song.channels.size() == 4, "default insertion did not append");
    check (appended.song.channels.back().mixerTrack != 2, "default insertion reused the occupied insert");
    check (appended.song.patterns == original.patterns && appended.song.automations == original.automations, "append remapped existing musical references");

    auto replaced = remote_instruments::replace (original, 1, generator);
    auto expected = original.channels[1];
    expected.instrument = generator.instrument;
    expected.name = generator.name;
    expected.plugin = generator.plugin;
    check (replaced.song.channels[1] == expected, "replacement lost channel identity/preferences or retained old plugin");
    check (replaced.song.channels[1].instance.id != original.channels[1].instance.id, "replacement reused the old runtime instrument instance");
    check (replaced.song.patterns == original.patterns && replaced.song.automations == original.automations
           && replaced.song.playlist == original.playlist, "replacement changed musical references");

    refuses ([&] { remote_instruments::create (original, generator, -1); });
    refuses ([&] { remote_instruments::create (original, generator, 4); });
    refuses ([&] { remote_instruments::create (original, generator, 1, 4); });
    refuses ([&] { remote_instruments::replace (original, 3, generator); });
    refuses ([&] { remote_instruments::replace (original, -1, generator); });
    check (original.channels[1].plugin.state == "existing state" && original.patterns[0].notes[0].channel == 1,
           "rejected transaction changed original model");
}
void authentication()
{
    using namespace remote_auth;
    // RFC 4231 cases 1, 2, 3 and 6 include binary keys/data and a key
    // longer than SHA-256's block, independently checking the HMAC construction.
    check (hmacSha256 (std::string (20, '\x0b'), "Hi There") ==
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "RFC 4231 HMAC case 1 failed");
    check (hmacSha256 ("Jefe", "what do ya want for nothing?") ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "RFC 4231 HMAC case 2 failed");
    check (hmacSha256 (std::string (20, '\xaa'), std::string (50, '\xdd')) ==
        "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", "RFC 4231 HMAC case 3 failed");
    check (hmacSha256 (std::string (131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First") ==
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", "RFC 4231 HMAC long-key case failed");
    const auto key = std::string (64, 'a');
    const auto client = std::string ("0123456789abcdef0123456789abcdef");
    const auto server = std::string ("fedcba9876543210fedcba9876543210");
    const auto serverProof = proof (key, "server", client, server);
    const auto clientProof = proof (key, "client", client, server);
    // Independent Python hashlib/hmac vectors pin the ASCII token key and transcript.
    check (serverProof == "8dc31853f158358575e3b58b554d2bd73d555325a891b29c7bc5de90c3b4077a", "server proof transcript changed");
    check (clientProof == "2626f47afd5c951ff3c7147d1aed38a9443f7aedf728b2471db8acde57a2b12c", "client proof transcript changed");
    check (! constantTimeEqual (serverProof, clientProof), "server proof could be reflected as client authentication");
    check (! constantTimeEqual (clientProof, proof (key, "client", client, std::string (32, '0'))), "proof could be replayed for a fresh server nonce");
    check (! constantTimeEqual (clientProof, proof (std::string (64, 'b'), "client", client, server)), "wrong key proof authenticated");
    auto changed = clientProof;
    changed.back() = changed.back() == '0' ? '1' : '0';
    check (! constantTimeEqual (changed, clientProof), "changed proof authenticated");
    check (constantTimeEqual (clientProof, clientProof), "correct proof rejected");
    check (isLowerHex (client, 32) && ! isLowerHex (std::string (32, 'A'), 32)
           && ! isLowerHex (client + "0", 32), "nonce format validation failed");
}

void jsonNesting()
{
    using remote_json::withinNestingLimit;
    check (withinNestingLimit (std::string (64, '[') + "0" + std::string (64, ']')), "nesting boundary was rejected");
    check (! withinNestingLimit (std::string (65, '[') + "0" + std::string (65, ']')), "over-nested unauthenticated array was accepted");
    check (! withinNestingLimit (std::string (100000, '['), 8), "pre-auth crash payload was accepted");
    check (! withinNestingLimit (std::string (100000, '[')), "authenticated crash payload was accepted");
    check (withinNestingLimit (std::string (8, '[') + "0" + std::string (8, ']'), 8), "handshake nesting boundary was rejected");
    check (! withinNestingLimit (std::string (9, '[') + "0" + std::string (9, ']'), 8), "over-nested handshake was accepted");
    std::string objects;
    for (int i = 0; i < 65; ++i) objects += "{\"x\":";
    check (! withinNestingLimit (objects + "0" + std::string (65, '}')), "over-nested objects were accepted");
    check (withinNestingLimit ("{\"name\":\"" + std::string (1000, '[') + "\"}"), "string data counted toward nesting");
    check (withinNestingLimit (R"json({"escaped":"\"[[[[\\","x":[0]})json", 2), "escaped quote or slash corrupted nesting count");
    check (! withinNestingLimit (R"json({"escaped":"\\","x":[[0]]})json", 2), "escaped slash hid actual nesting");
}
}
int main()
{
    try
    {
        instrumentEdits();
        jsonNesting();
        authentication();
        std::puts ("Remote instrument edits, JSON nesting and authentication tests passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf (stderr, "%s\n", error.what());
        return 1;
    }
}
