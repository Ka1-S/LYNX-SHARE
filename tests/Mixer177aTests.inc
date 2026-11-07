// --- Round 177a: routing stays put when mixer inserts move, are inserted or deleted ------------------------------------
// Every reference to an insert follows its logical track (REA_rodada177a §1): sends, side-chains, slot sends
// (Transmitter, Fruity Send, Transporter, Patcher outputs, also inside a Patcher), a generator Patcher's relative
// outputs, Soundfont / Voltage Controller "send to insert" settings, automation, links, initial values, pattern
// events and controller sources. A reference to a deleted insert is dropped instead of landing on another track.

#include "core/FlpExport.h"
#include "core/FruityBasicFx.h"
#include "core/Transmitter.h"
#include "flp/Flp.h"
#include "core/MixerOps.h"
#include "core/Patcher.h"
#include "core/SoundFont.h"
#include "core/TransporterFl.h"
#include "core/VoltageController.h"

namespace
{
int insertNamed (const lynx::Song& song, const std::string& name)
{
    for (int t = 0; t < (int) song.mixer.size(); ++t)
        if (song.mixer[(std::size_t) t].name == name)
            return t;
    return -1;
}

int sendParamTrack (float value) { return (int) std::lround (value * (lynx::maxMixerTracks - 1)); }

// A project where every kind of reference points at a named insert
lynx::Song routedSong()
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (9);
    const char* names[] { "Master", "lead", "kick", "bass", "bus", "fx", "verb", "delay", "spare", "last" };
    for (int t = 0; t < 10; ++t)
        song.mixer[(std::size_t) t].name = names[t];

    song.setRoute (2, 4, true, 0.5f); // kick -> bus
    song.setRoute (3, 4, true, 0.7f); // bass -> bus
    EffectSlot comp;
    comp.kind = EffectKind::compressor;
    comp.sidechain = 2; // bass ducks under the kick
    song.mixer[3].effects = { comp };

    EffectSlot send;
    send.kind = EffectKind::fruitySend;
    send.params[3] = flbasic::sendTrackValue (6); // fx -> verb
    EffectSlot transporter;
    transporter.kind = EffectKind::flTransporter;
    transporter.params[(std::size_t) transporter::sendParam] = 7.0f / (float) (maxMixerTracks - 1); // -> delay
    song.mixer[5].effects = { send, transporter };

    EffectSlot transmitter;
    transmitter.kind = EffectKind::transmitter;
    transmitter.sendTracks = { 6, 7 };
    EffectSlot patcher;
    patcher.kind = EffectKind::patcher;
    patcher.patcher.outputTracks = { 6 };
    PatchNode inner; // a Fruity Send inside the Patcher
    inner.effect.kind = EffectKind::fruitySend;
    inner.effect.params[3] = flbasic::sendTrackValue (7);
    patcher.patcher.nodes = { inner };
    song.mixer[1].effects = { transmitter, patcher };

    song.channels.resize (3);
    song.channels[0].mixerTrack = 2;
    // a generator Patcher on "fx" with one relative extra output (FL's "the next insert": verb)
    song.channels[1].instrument = InstrumentKind::patcher;
    song.channels[1].mixerTrack = 5;
    PatchWire wire;
    wire.from = patchFromHost;
    wire.to = patchToHost;
    wire.toPort = 1;
    song.channels[1].patcher.wires = { wire };
    // a Soundfont whose reverb goes to "verb" and chorus to "delay"
    song.channels[2] = sf::makeChannel();
    song.channels[2].mixerTrack = 3;
    song.channels[2].modelParams[(std::size_t) sf::pReverbTrack] = 6.0f;
    song.channels[2].modelParams[(std::size_t) sf::pChorusTrack] = 7.0f;
    ChannelDef volt;
    volt.instrument = InstrumentKind::voltage;
    voltage::Settings vs;
    vs.tracks = { 6, 0, 9 };
    volt.pluginState = voltage::write (vs);
    song.channels.push_back (volt);

    song.playlistTracks.resize (2);
    song.playlistTracks[1].mode = PlaylistTrackMode::audio;
    song.playlistTracks[1].mixerTrack = 6;

    Automation vol;
    vol.target = { AutomationTargetKind::mixerVolume, 6, 0, 0 };
    Automation sendLevel;
    sendLevel.target = { AutomationTargetKind::mixerSend, 2, 4, 0 };
    Automation fxParam;
    fxParam.target = { AutomationTargetKind::effectParam, 3, 0, 1 };
    fxParam.source.track = 5; // a controller in "fx" slot 1
    fxParam.source.slot = 1;
    song.automations = { vol, sendLevel, fxParam };
    song.midiLinks = { MidiLink { 7, -1, { AutomationTargetKind::mixerPan, 6, 0, 0 } } };
    song.initialValues = { Song::InitialValue { { AutomationTargetKind::mixerVolume, 7, 0, 0 }, 0.5f } };
    if (song.patterns.empty())
        song.patterns.emplace_back();
    song.patterns[0].events = { PatternEvents { { AutomationTargetKind::mixerPan, 2, 0, 0 }, {} } };
    return song;
}

// every reference still names the same track
void checkRoutedSong (const lynx::Song& song)
{
    using namespace lynx;
    const auto lead = insertNamed (song, "lead"), kick = insertNamed (song, "kick"), bass = insertNamed (song, "bass"),
               bus = insertNamed (song, "bus"), fx = insertNamed (song, "fx"), verb = insertNamed (song, "verb"),
               delay = insertNamed (song, "delay"), last = insertNamed (song, "last");
    CHECK (song.routes (kick, bus) && song.routes (bass, bus) && ! song.routes (kick, verb));
    CHECK (song.mixer[(std::size_t) bass].effects[0].sidechain == kick);
    CHECK (fruitySendTarget (song.mixer[(std::size_t) fx].effects[0]) == verb);
    CHECK (fruitySendTarget (song.mixer[(std::size_t) fx].effects[1]) == delay);
    CHECK (sendParamTrack (song.mixer[(std::size_t) fx].effects[1].params[(std::size_t) transporter::sendParam]) == delay);
    const auto& tx = song.mixer[(std::size_t) lead].effects;
    CHECK (tx[0].sendTracks[0] == verb && tx[0].sendTracks[1] == delay);
    CHECK (fruitySendTarget (tx[1]) == verb);
    CHECK (flbasic::sendTrack (tx[1].patcher.nodes[0].effect.params[3]) == delay);

    CHECK (song.channels[0].mixerTrack == kick && song.channels[1].mixerTrack == fx);
    CHECK (patcher::auxOutputTrack (song.channels[1], 1) == verb);
    CHECK ((int) song.channels[2].modelParams[(std::size_t) sf::pReverbTrack] == verb);
    CHECK ((int) song.channels[2].modelParams[(std::size_t) sf::pChorusTrack] == delay);
    const auto vs = voltage::parse (song.channels[3].pluginState);
    CHECK (vs && vs->tracks[0] == verb && vs->tracks[1] == 0 && vs->tracks[2] == last);
    CHECK (song.playlistTracks[1].mixerTrack == verb && song.playlistTracks[1].mode == PlaylistTrackMode::audio);

    CHECK (song.automations[0].target.index == verb);
    CHECK (song.automations[1].target.index == kick && song.automations[1].target.slot == bus);
    CHECK (song.automations[2].target.index == bass && song.automations[2].source.track == fx);
    CHECK (song.midiLinks.size() == 1 && song.midiLinks[0].target.index == verb);
    CHECK (song.initialValues.size() == 1 && song.initialValues[0].target.index == delay);
    CHECK (song.patterns[0].events[0].target.index == kick);
}
} // namespace

TEST (mixer177a_moving_inserts_keeps_every_route_on_its_track)
{
    using namespace lynx;
    auto song = routedSong();
    checkRoutedSong (song);

    std::vector<int> selection { 6 }; // "verb" left past "fx": the Patcher's relative output must not become "fx" + 1
    CHECK (mixerops::moveTracks (song, selection, -1) && selection == std::vector<int> { 5 });
    checkRoutedSong (song);
    CHECK (song.channels[1].patcher.outputTracks == std::vector<int> { 5 }); // pinned to "verb"

    selection = { 2, 3, 4 }; // a block to the far right and back
    for (int i = 0; i < 5; ++i)
        CHECK (mixerops::moveTracks (song, selection, 1));
    checkRoutedSong (song);
    for (int i = 0; i < 6; ++i)
        CHECK (mixerops::moveTracks (song, selection, -1));
    checkRoutedSong (song);

    mixerops::swapTracks (song, 1, 9);
    checkRoutedSong (song);
    CHECK (mixerops::insertTracks (song, 1, 3) == 3);
    checkRoutedSong (song);
    CHECK (mixerops::insertTrack (song, (int) song.mixer.size() - 1));
    checkRoutedSong (song);
}

TEST (mixer177a_relative_patcher_output_stays_relative_when_it_still_lands_right)
{
    using namespace lynx;
    auto song = routedSong();
    // inserts added before both the channel's insert and the output's: the output is still "the next insert"
    CHECK (mixerops::insertTracks (song, 1, 2) == 2);
    CHECK (song.channels[1].patcher.outputTracks.empty() || song.channels[1].patcher.outputTracks[0] == -1);
    checkRoutedSong (song);
    // one added between them: pinned
    CHECK (mixerops::insertTrack (song, insertNamed (song, "verb")));
    CHECK (song.channels[1].patcher.outputTracks == std::vector<int> { insertNamed (song, "verb") });
    checkRoutedSong (song);
}

TEST (mixer177a_deleting_an_insert_drops_its_links_and_never_moves_them)
{
    using namespace lynx;
    auto song = routedSong();
    const auto count = song.mixer.size();
    CHECK (mixerops::deleteTrack (song, 6)); // "verb"
    CHECK (song.mixer.size() == count && insertNamed (song, "verb") < 0 && song.mixer.back().name == "Insert 9");
    CHECK (song.mixer.back().effects.empty() && song.mixer.back().sends.empty());

    const auto fx = insertNamed (song, "fx"), lead = insertNamed (song, "lead"), delay = insertNamed (song, "delay");
    CHECK (fruitySendTarget (song.mixer[(std::size_t) fx].effects[0]) == -1);     // Fruity Send: none
    CHECK (fruitySendTarget (song.mixer[(std::size_t) fx].effects[1]) == delay);  // the others follow
    CHECK (song.mixer[(std::size_t) lead].effects[0].sendTracks[0] == 0 && song.mixer[(std::size_t) lead].effects[0].sendTracks[1] == delay);
    CHECK (fruitySendTarget (song.mixer[(std::size_t) lead].effects[1]) == -1);
    CHECK (patcher::auxOutputTrack (song.channels[1], 1) == 0); // the generator's output: to the Master, as a channel
    CHECK ((int) song.channels[2].modelParams[(std::size_t) sf::pReverbTrack] == -1);
    CHECK ((int) song.channels[2].modelParams[(std::size_t) sf::pChorusTrack] == delay);
    const auto vs = voltage::parse (song.channels[3].pluginState);
    CHECK (vs && vs->tracks[0] == 0 && vs->tracks[2] == insertNamed (song, "last"));
    CHECK (song.playlistTracks[1].mixerTrack == -1 && song.playlistTracks[1].mode == PlaylistTrackMode::unassigned);

    // the automation of the deleted insert has no target (FL: -1), it does not drive the blank insert at the end
    CHECK (song.automations[0].target.kind == AutomationTargetKind::none);
    CHECK (song.automations[1].target.kind == AutomationTargetKind::mixerSend && song.automations[1].target.index == insertNamed (song, "kick"));
    CHECK (song.midiLinks.empty());
    CHECK (song.initialValues.size() == 1 && song.initialValues[0].target.index == delay);

    // a side-chain from a deleted insert is cleared (-1: none; 0 would be the Master)
    CHECK (mixerops::deleteTrack (song, insertNamed (song, "kick")));
    CHECK (song.mixer[(std::size_t) insertNamed (song, "bass")].effects[0].sidechain == -1);
    CHECK (song.channels[0].mixerTrack == 0);
    CHECK (song.automations[1].target.kind == AutomationTargetKind::none);
    CHECK (song.patterns[0].events[0].target.kind == AutomationTargetKind::none);
    // a controller in a deleted insert is no longer live
    CHECK (mixerops::deleteTrack (song, insertNamed (song, "fx"))); // (kick is gone: "fx" moved one left)
    CHECK (song.automations[2].source.track == -1 && song.automations[2].target.index == insertNamed (song, "bass"));
}

TEST (mixer177a_moved_track_exports_the_right_insert_numbers_to_flp)
{
    // An imported Fruity Send / Transmitter keeps FL's own state for the .flp export (FlOrigin). After a move its insert
    // numbers are rewritten with the track, the other bytes stay as imported, and it is still exported as FL's state.
    using namespace lynx;
    auto le = [] (std::vector<std::uint8_t>& d, int v) {
        for (int b = 0; b < 4; ++b)
            d.push_back ((std::uint8_t) (((std::uint32_t) v >> (8 * b)) & 0xFF));
    };
    auto i32 = [] (const std::vector<std::uint8_t>& d, std::size_t at) {
        return at + 4 <= d.size() ? (int) ((std::uint32_t) d[at] | (std::uint32_t) d[at + 1] << 8 | (std::uint32_t) d[at + 2] << 16 | (std::uint32_t) d[at + 3] << 24) : -999;
    };
    auto song = Song::makeDefault();
    song.channels.clear();
    song.mixer = Song::defaultMixer (8);
    song.mixer[3].name = "verb";
    song.mixer[4].name = "delay";

    std::vector<std::uint8_t> sendState;
    for (const auto v : { 12, 200, 256, 3 }) // pan, volume, dry, insert 3
        le (sendState, v);
    auto send = makeEffectSlot (EffectKind::fruitySend);
    flbasic::sendToParams (*flbasic::parseSend (sendState), send.params.data());
    send.flOrigin = { "Fruity Send", sendState, 0 };

    std::vector<std::uint8_t> txState;
    le (txState, 1);
    for (int k = 0; k < transmitter::count; ++k)
        le (txState, 0);
    for (const auto v : { 3, 4, 0, 0 }) // transient send, sustain send, side-chain input, spare
        le (txState, v);
    auto tx = makeEffectSlot (EffectKind::transmitter);
    tx.sendTracks = { 3, 4 };
    tx.flOrigin = { "Transmitter", txState, 0 };
    song.mixer[1].effects = { send, tx };
    flpexport::stampOrigins (song);

    auto exported = [&song] {
        flpexport::Options options;
        options.lynxOnly = flpexport::LynxOnly::substitute;
        flpexport::Report report;
        return flpexport::toProject (song, options, report);
    };
    auto slots = [] (const flp::Project& p, int t) {
        for (const auto& m : p.mixer)
            if (m.index == t)
                return m.slots;
        return std::vector<flp::MixerInsert::Slot> {};
    };

    // unmoved: exactly the imported bytes
    auto before = slots (exported(), 1);
    CHECK (before.size() == 2 && before[0].plugin == "Fruity Send" && before[0].state == sendState && before[1].state == txState);

    std::vector<int> selection { 3, 4 }; // verb and delay one place right, then the Fruity Send's insert to the end
    CHECK (mixerops::moveTracks (song, selection, 1));
    mixerops::swapTracks (song, 1, 8);
    const auto verb = insertNamed (song, "verb"), delay = insertNamed (song, "delay");
    CHECK (verb == 4 && delay == 5);
    auto after = slots (exported(), 8);
    CHECK (after.size() == 2);
    if (after.size() == 2)
    {
        CHECK (after[0].plugin == "Fruity Send" && i32 (after[0].state, 12) == verb);
        CHECK (std::equal (sendState.begin(), sendState.begin() + 12, after[0].state.begin())); // pan / volume / dry as imported
        const auto at = 4 + 4 * (std::size_t) transmitter::count;
        CHECK (after[1].plugin == "Transmitter" && i32 (after[1].state, at) == verb && i32 (after[1].state, at + 4) == delay);
        CHECK (after[1].state.size() == txState.size());
    }
    CHECK (slots (exported(), 1).empty());

    // a LYNX load / save never remaps: the project's own numbers are written as they are (no move = same bytes)
    CHECK (song.mixer[8].effects[0].flOrigin.fingerprint == flpexport::fingerprint (song.mixer[8].effects[0]));
}

TEST (mixer177a_side_input_numbers_follow_the_source_in_the_flp_export)
{
    // FL's Fruity Limiter names its side input by place k in the insert's inputs list (itself, then the inserts routing
    // into it by number). After the two sources swap places, the exported k still names the source LYNX listens to.
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.clear();
    song.mixer = Song::defaultMixer (6);
    song.mixer[2].name = "kick";
    song.mixer[3].name = "snare";
    song.mixer[5].name = "bass";
    song.setRoute (2, 5, true, 0.0f);
    song.setRoute (3, 5, true, 0.0f);
    std::vector<std::uint8_t> state (0xA8, 0);
    state[0] = 6;                  // version 6
    state[0x10] = 0x5A;            // something else in the state, kept
    state[0xA4] = 2;               // k = 2: [bass, kick, snare] -> snare
    auto limiter = makeEffectSlot (EffectKind::fruityLimiter);
    limiter.sidechain = 3;
    limiter.flOrigin = { "Fruity Limiter", state, 0 };
    song.mixer[5].effects = { limiter };
    flpexport::stampOrigins (song);

    auto exportedState = [&song] {
        flpexport::Options options;
        options.lynxOnly = flpexport::LynxOnly::substitute;
        flpexport::Report report;
        const auto p = flpexport::toProject (song, options, report);
        for (const auto& m : p.mixer)
            if (m.index == insertNamed (song, "bass") && ! m.slots.empty())
                return m.slots[0].state;
        return std::vector<std::uint8_t> {};
    };
    CHECK (exportedState() == state); // unmoved: byte for byte

    mixerops::swapTracks (song, 2, 3); // snare now 2, kick 3
    CHECK (song.mixer[5].effects[0].sidechain == 2);
    auto after = exportedState();
    CHECK (after.size() == state.size());
    if (after.size() == state.size())
    {
        CHECK (after[0xA4] == 1); // [bass, snare, kick] -> snare is k = 1 (FL itself would keep 2: the kick)
        after[0xA4] = 2;
        CHECK (after == state); // nothing else changed
    }
}
