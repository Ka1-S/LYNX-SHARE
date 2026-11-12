// Round 190ec (docs/analise/REA_rodada190ec.md): Live's Drum Rack as an Instrument Rack in drum mode (core/Rack.h). Pads
// receive one key and play their Play note into their chain; choke groups cut each other; chains route into return chains
// and send to them; .als Drum Racks come in as LYNX Drum Racks and go back out as Live's.

namespace drumrack190ec
{
using namespace lynx;
using rack183c::graphOf;
using rack183c::peak;
using rack183c::synthNode;
using rack183c::utilityNode;

// a block of the graph's output (left channel), after `blocks` blocks
std::vector<float> render (patcher::Graph& g, int blocks = 8)
{
    std::vector<float> all;
    for (int b = 0; b < blocks; ++b)
    {
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        g.process (l.data(), r.data(), nullptr, nullptr, l.data(), r.data(), 512, 120.0);
        all.insert (all.end(), l.begin(), l.end());
    }
    return all;
}

// rising zero crossings per second: the pitch of a plain synth note
double crossingsPerSecond (const std::vector<float>& x)
{
    int n = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        n += x[i - 1] < 0.0f && x[i] >= 0.0f;
    return (double) n * 48000.0 / (double) x.size();
}

double rms (const std::vector<float>& x)
{
    double s = 0.0;
    for (const auto v : x)
        s += (double) v * v;
    return x.empty() ? 0.0 : std::sqrt (s / (double) x.size());
}

double hz (int key) { return 440.0 * std::pow (2.0, (key - 69) / 12.0); }

// a Drum Rack with a synth pad on `key` playing `play`
PatcherDoc onePad (int key, int play)
{
    auto doc = rack::makeDrumRack();
    const auto c = rack::addPad (doc, key, "Pad", synthNode());
    rack::setPlay (doc, c, play);
    return doc;
}
} // namespace drumrack190ec

TEST (drumrack190ec_a_new_drum_rack_is_an_instrument_rack_with_pads)
{
    using namespace drumrack190ec;
    auto doc = rack::makeDrumRack();
    const auto* r = rack::rackOf (doc);
    CHECK (r != nullptr && r->drum && r->kind == RackDoc::Kind::instrument && r->chains.empty() && r->returns.empty());
    CHECK (r->showPads && r->padScroll == 19 && rack::isDrumRack (doc));
    // the pad view: 32 rows of 4 from the top; row 19's top shows 48…51, row 22 (the bottom of the view) C1 = 36
    CHECK (rack::padRow (36) == 22 && rack::padRow (51) == 19 && rack::rowFirstKey (22) == 36 && rack::rowFirstKey (0) == 124 && rack::padRow (127) == 0);
    const auto kick = rack::addPad (doc, 36, "Kick", synthNode());
    CHECK (kick == 0 && rack::padChain (*rack::rackOf (doc), 36) == 0 && rack::padChain (*rack::rackOf (doc), 37) == -1);
    CHECK (rack::padName (*rack::rackOf (doc), 36) == "Kick" && rack::padNames (*rack::rackOf (doc)).at (36) == "Kick");
    // its chain: the synth then a mixer; Receive C1, Play C3, no choke, to the rack's output (Live's defaults [DATA])
    const auto& c = rack::rackOf (doc)->chains[0];
    CHECK (c.receive == 36 && c.play == 60 && c.choke == 0 && c.audioTo == -1 && c.nodes.size() == 1 && c.mixer >= 0);
    // the compiled zone: key 36 only, playing 60, the chain's instrument as its choke node
    CHECK (doc.zones.size() == 1 && doc.zones[0].key == (RackZone { 36, 36, 36, 36 }) && doc.zones[0].playKey == 60 && doc.zones[0].chokeNode == c.nodes[0]);
    // what a LYNX without Drum Racks plays: the chain's key zone is the pad's note
    CHECK (c.key == (RackZone { 36, 36, 36, 36 }));
    // swapping pads trades the Receive notes (also onto an empty pad)
    rack::addPad (doc, 38, "Snare", synthNode());
    CHECK (rack::swapPads (doc, 36, 38) && rack::rackOf (doc)->chains[0].receive == 38 && rack::rackOf (doc)->chains[1].receive == 36);
    CHECK (rack::swapPads (doc, 38, 40) && rack::rackOf (doc)->chains[0].receive == 40 && ! rack::swapPads (doc, 50, 51));
    // an Instrument Rack is no Drum Rack: addPad refuses
    auto ir = rack::makeRack (RackDoc::Kind::instrument);
    CHECK (rack::addPad (ir, 36, "x") == -1 && ! rack::isDrumRack (ir));
}

TEST (drumrack190ec_a_pad_plays_its_play_note_and_only_on_its_key)
{
    using namespace drumrack190ec;
    // C1 into the pad: the synth plays C3 (Play 60), not C1
    {
        auto g = graphOf (onePad (36, 60));
        g->noteOn (36, 0.9f);
        const auto x = render (*g);
        CHECK (rms (x) > 1.0e-3);
        const auto f = crossingsPerSecond (x);
        CHECK (std::abs (f - hz (60)) < hz (60) * 0.06);
        // another key doesn't enter the pad
        g->noteOff (36);
        render (*g, 400);
        g->noteOn (37, 0.9f);
        CHECK (peak (*g, 4) < 1.0e-4f);
    }
    // Play C4 (72): an octave higher
    {
        auto g = graphOf (onePad (36, 72));
        g->noteOn (36, 0.9f);
        const auto f = crossingsPerSecond (render (*g));
        CHECK (std::abs (f - hz (72)) < hz (72) * 0.06);
        // its note-off follows to the Play note (the voice ends)
        g->noteOff (36);
        render (*g, 400);
        CHECK (peak (*g, 2) < 1.0e-3f);
    }
    // Receive "All Notes" (−1): every key enters, unchanged
    {
        auto doc = onePad (36, 60);
        rack::setReceive (doc, 0, -1);
        CHECK (doc.zones[0].playKey == -1 && doc.zones[0].key == RackZone {});
        auto g = graphOf (doc);
        g->noteOn (48, 0.9f);
        const auto f = crossingsPerSecond (render (*g));
        CHECK (std::abs (f - hz (48)) < hz (48) * 0.06);
    }
}

TEST (drumrack190ec_chain_order_returns_sends_and_audio_to)
{
    using namespace drumrack190ec;
    // the pad's chain keeps Live's order whatever the drop place: MIDI effects, the instrument, audio effects
    auto doc = rack::makeDrumRack();
    const auto c = rack::addPad (doc, 36, "Kick");
    const auto fx = rack::addDevice (doc, c, utilityNode());
    const auto synth = rack::addDevice (doc, c, synthNode());
    const auto midi = rack::addDevice (doc, c, rack183c::vfxNode());
    CHECK (rack::devices (*rack::rackOf (doc), c) == (std::vector<int> { midi, synth, fx }));
    CHECK (doc.zones[0].chokeNode == synth); // (the zone's choke target is the instrument, past the MIDI effect)

    // levels: the pad alone; with a send at −6 dB to an empty return (the return adds half again); routed into the
    // return instead of the rack's output (one path, at the return's level)
    auto pad = onePad (36, 60);
    auto level = [] (const PatcherDoc& d) {
        auto g = graphOf (d);
        g->noteOn (36, 0.9f);
        return rms (render (*g, 16));
    };
    const auto dry = level (pad);
    CHECK (dry > 1.0e-3);
    const auto ret = rack::addReturn (pad, "A Reverb");
    CHECK (ret == 0 && rack::rackOf (pad)->returns[0].mixer >= 0 && rack::rackOf (pad)->chains[0].sends.size() == 1);
    CHECK (rack::chainSlots (*rack::rackOf (pad)) == 2 && rack::isReturnSlot (*rack::rackOf (pad), 1) && rack::chainAt (*rack::rackOf (pad), 1)->name == "A Reverb");
    CHECK (std::abs (level (pad) / dry - 1.0) < 0.01); // (a send at −inf adds nothing)
    rack::setSend (pad, 0, 0, 0.5f);
    CHECK (std::abs (level (pad) / dry - 1.5) < 0.02);
    rack::setSend (pad, 0, 0, 0.5f, false); // (switched off: Live's EnabledByUser false)
    CHECK (std::abs (level (pad) / dry - 1.0) < 0.01);
    rack::setAudioTo (pad, 0, 0);
    CHECK (std::abs (level (pad) / dry - 1.0) < 0.01);
    rack::setChainVolume (pad, 1, 0.5f); // (the return chain's volume: slot 1)
    CHECK (std::abs (level (pad) / dry - 0.5) < 0.01);
    rack::setActive (pad, 1, false); // (the return switched off: the pad routed into it is silent)
    CHECK (level (pad) < dry * 1.0e-3);
    // a return chain takes audio effects only
    PatchNode s = synthNode();
    CHECK (rack::addDevice (pad, 1, s) == -1 && rack::addDevice (pad, 1, utilityNode()) >= 0 && rack::devices (*rack::rackOf (pad), 1).size() == 1);
    // removing the return: the pad goes back to the rack's output, its send goes
    rack::setActive (pad, 1, true);
    CHECK (rack::removeReturn (pad, 0) && rack::rackOf (pad)->returns.empty() && rack::rackOf (pad)->chains[0].audioTo == -1
           && rack::rackOf (pad)->chains[0].sends.empty());
    CHECK (std::abs (level (pad) / dry - 1.0) < 0.01);
}

TEST (drumrack190ec_choke_groups_cut_the_other_pads)
{
    using namespace drumrack190ec;
    // two synth pads, the first held: when the second starts, a shared choke group stops the first (Live's ChokeGroup)
    auto run = [] (int chokeA, int chokeB) {
        auto doc = rack::makeDrumRack();
        rack::addPad (doc, 42, "Hat Closed", synthNode());
        rack::addPad (doc, 46, "Hat Open", synthNode());
        rack::setChoke (doc, 0, chokeA);
        rack::setChoke (doc, 1, chokeB);
        auto g = graphOf (doc);
        g->noteOn (42, 0.9f); // (held all the way)
        render (*g, 4);
        g->noteOn (46, 0.9f);
        g->noteOff (46);
        render (*g, 400);
        return peak (*g, 2);
    };
    CHECK (run (1, 1) < 1.0e-3f); // choked: silent although its note is still held
    CHECK (run (0, 0) > 1.0e-3f); // no group: the held pad goes on
    CHECK (run (1, 2) > 1.0e-3f); // other groups don't choke
    CHECK (run (0, 1) > 1.0e-3f); // a pad without a group isn't choked
    // the zones carry the group and the node it cuts
    auto doc = rack::makeDrumRack();
    rack::addPad (doc, 42, "A", synthNode());
    rack::setChoke (doc, 0, 3);
    CHECK (doc.zones[0].choke == 3);
    rack::setChoke (doc, 0, 40);
    CHECK (rack::rackOf (doc)->chains[0].choke == 16); // (16 groups)
}

namespace drumrack190ec
{
using als175a::v;

inline std::string simpler (const std::string& path, int mode)
{
    return "<OriginalSimpler Id=\"0\"><On><Manual Value=\"true\" /></On>" + v ("UserName", "") +
           "<Player><MultiSampleMap><SampleParts><MultiSamplePart Id=\"0\">" + v ("RootKey", "60") + v ("Detune", "0") + v ("Volume", "1") +
           "<KeyRange>" + v ("Min", "0") + v ("Max", "127") + "</KeyRange><VelocityRange>" + v ("Min", "1") + v ("Max", "127") + "</VelocityRange>" +
           "<SampleRef><FileRef>" + v ("RelativePathType", "0") + v ("RelativePath", "") + v ("Path", path) + "</FileRef></SampleRef>" +
           "</MultiSamplePart></SampleParts></MultiSampleMap></Player><Globals>" + v ("PlaybackMode", std::to_string (mode)) + "</Globals>" +
           "<VolumeAndPan><Volume>" + v ("Manual", "-6") + "</Volume></VolumeAndPan></OriginalSimpler>";
}

inline std::string sendInfo (int index, double level, bool on)
{
    return "<AudioBranchSendInfo Id=\"" + std::to_string (index) + "\"><Send>" + v ("Manual", std::to_string (level)) + "</Send>" +
           v ("EnabledByUser", on ? "true" : "false") + v ("Index", std::to_string (index)) + "</AudioBranchSendInfo>";
}

// A pad as Live writes it [DATA: the shipped kits' DrumBranch members]
inline std::string pad (const std::string& name, int receiving, int sending, int choke, const std::string& devices, const std::string& sends,
                        const std::string& target = "AudioOut/None")
{
    return "<DrumBranch Id=\"0\"><Name>" + v ("EffectiveName", name) + v ("UserName", "") + "</Name>" + v ("IsSoloed", "false") +
           "<DeviceChain><MidiToAudioDeviceChain><Devices>" + devices + "</Devices></MidiToAudioDeviceChain></DeviceChain>" +
           rack183c::range ("BranchSelectorRange", 0, 127, 0, 127) + "<MixerDevice><Speaker><Manual Value=\"true\" /></Speaker><Volume>" +
           v ("Manual", "1") + "</Volume><Panorama>" + v ("Manual", "0") + "</Panorama><SendInfos>" + sends + "</SendInfos><RoutingHelper><Routable>" +
           v ("Target", target) + "</Routable></RoutingHelper></MixerDevice><BranchInfo>" + v ("ReceivingNote", std::to_string (receiving)) +
           v ("SendingNote", std::to_string (sending)) + v ("ChokeGroup", std::to_string (choke)) + "</BranchInfo></DrumBranch>";
}

// Our own fixture: a MIDI track holding a Drum Rack with four pads and two return chains, and a clip on C1 and F#1
inline std::string drumSet()
{
    const auto kick = pad ("Kick", 92, 60, 0, simpler ("E:/kit/kick.wav", 1), sendInfo (0, 0.0003162277571, true) + sendInfo (1, 0.0003162277571, true));
    const auto hatC = pad ("Hat Closed", 86, 60, 1, simpler ("E:/kit/hat-closed.wav", 1) + als175a::vst2Device ("00"),
                           sendInfo (0, 0.5, true) + sendInfo (1, 0.0003162277571, true));
    const auto hatO = pad ("Hat Open", 82, 64, 1, simpler ("E:/kit/hat-open.wav", 0), sendInfo (0, 0.25, false) + sendInfo (1, 0.1, true),
                           "AudioOut/Track.10/DeviceIn.0.R1,ChainIn");
    const auto all = pad ("Every Note", 0, 60, 0, "", sendInfo (0, 0.0003162277571, true) + sendInfo (1, 0.0003162277571, true));
    const auto ret = [] (const std::string& name, const std::string& devices) {
        return "<ReturnBranch Id=\"0\"><Name>" + v ("EffectiveName", name) + v ("UserName", "") + "</Name>" + v ("IsSoloed", "false") +
               "<DeviceChain><AudioToAudioDeviceChain><Devices>" + devices + "</Devices></AudioToAudioDeviceChain></DeviceChain>" +
               "<MixerDevice><Speaker><Manual Value=\"true\" /></Speaker><Volume>" + v ("Manual", "0.5") + "</Volume><Panorama>" + v ("Manual", "0") +
               "</Panorama></MixerDevice></ReturnBranch>";
    };
    const auto drum = "<DrumGroupDevice Id=\"0\"><On><Manual Value=\"true\" /></On>" + v ("UserName", "Our Kit") + "<Branches>" + kick + hatC + hatO + all +
                      "</Branches><ReturnBranches>" + ret ("A Verb", als175a::vst2Device ("00")) + ret ("B Bus", "") + "</ReturnBranches>" +
                      rack183c::macros ({}, {}, {}) + v ("ArePadsVisible", "true") + v ("IsMidiSectionVisible", "true") + v ("AreSendsVisible", "true") +
                      v ("IsReturnBranchesListVisible", "false") + v ("PadScrollPosition", "18") + "</DrumGroupDevice>";
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" Creator=\"test\">\n<LiveSet><Tracks>";
    s += "<MidiTrack Id=\"10\">" + als175a::name ("Drums") + v ("Color", "3") + v ("TrackGroupId", "-1") + "<DeviceChain>" + als175a::mixer (100, 1.0, 0.0, true) +
         "<AudioOutputRouting>" + v ("Target", "AudioOut/Main") + "</AudioOutputRouting>" +
         "<MainSequencer><ClipTimeable><ArrangerAutomation><Events><MidiClip Id=\"0\" Time=\"0\">" + v ("CurrentStart", "0") + v ("CurrentEnd", "4") +
         v ("Name", "Beat") + "<Loop>" + v ("LoopStart", "0") + v ("LoopEnd", "4") + v ("StartRelative", "0") + v ("LoopOn", "false") + "</Loop>" +
         "<Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>" + als175a::midiNote (0, 0.25, 100) + "</Notes>" + v ("MidiKey", "36") + "</KeyTrack>" +
         "<KeyTrack Id=\"1\"><Notes>" + als175a::midiNote (1, 0.25, 90) + "</Notes>" + v ("MidiKey", "42") + "</KeyTrack></KeyTracks></Notes>" +
         "</MidiClip></Events></ArrangerAutomation></ClipTimeable></MainSequencer><DeviceChain><Devices>" + drum + "</Devices></DeviceChain></DeviceChain></MidiTrack>";
    s += "</Tracks><MainTrack><DeviceChain>" + als175a::mixer (900, 1.0, 0.0, true, {}, "<Tempo><Manual Value=\"120\" /></Tempo>") + "</DeviceChain></MainTrack></LiveSet>\n</Ableton>\n";
    return s;
}

inline AlsImportOptions drumOptions (FlpImportReport* report)
{
    auto o = als175a::options (report);
    o.common.fileExists = [] (const std::string& p) { return p.rfind ("E:/kit/", 0) == 0; };
    return o;
}

inline const ChannelDef* drumChannel (const Song& song)
{
    for (const auto& c : song.channels)
        if (rack::isDrumRack (c))
            return &c;
    return nullptr;
}
} // namespace drumrack190ec

TEST (drumrack190ec_als_drum_rack_imports_pads_chokes_sends_and_returns)
{
    using namespace drumrack190ec;
    FlpImportReport report;
    std::string error;
    const auto song = importAls (als175a::bytesOf (drumSet()), drumOptions (&report), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* ch = drumChannel (*song);
    CHECK (ch != nullptr);
    if (ch == nullptr)
        return;
    const auto& r = *rack::rackOf (ch->patcher);
    CHECK (r.name == "Our Kit" && r.chains.size() == 4 && r.returns.size() == 2);
    if (r.chains.size() != 4 || r.returns.size() != 2)
        return;
    // ReceivingNote 92 / 86 / 82 = C1 / F#1 / A#1; 0 = all notes; SendingNote; ChokeGroup
    CHECK (r.chains[0].name == "Kick" && r.chains[0].receive == 36 && r.chains[0].play == 60 && r.chains[0].choke == 0);
    CHECK (r.chains[1].name == "Hat Closed" && r.chains[1].receive == 42 && r.chains[1].choke == 1);
    CHECK (r.chains[2].receive == 46 && r.chains[2].play == 64 && r.chains[2].choke == 1);
    CHECK (r.chains[3].name == "Every Note" && r.chains[3].receive == -1 && r.chains[3].nodes.empty());
    // the pads' devices: a Sampler each (one-shot = release 0), the closed hat's plugin effect after it
    const auto& n0 = ch->patcher.nodes[(std::size_t) r.chains[0].nodes.at (0)];
    CHECK (n0.generator && n0.channel.instrument == InstrumentKind::sampler && n0.channel.samplePath == "E:/kit/kick.wav" && n0.channel.sampler.release == 0.0f);
    CHECK (std::abs (n0.channel.sampler.gainDb + 6.0f) < 0.01f); // (the Simpler's own volume)
    CHECK (r.chains[1].nodes.size() == 2 && ! ch->patcher.nodes[(std::size_t) r.chains[1].nodes[1]].generator);
    // sends: Index → return chain, linear level, EnabledByUser; the floor is −inf
    CHECK (r.chains[1].sends.size() == 2 && std::abs (r.chains[1].sends[0].level - 0.5f) < 1.0e-5f && r.chains[1].sends[0].on && r.chains[1].sends[1].level == 0.0f);
    CHECK (std::abs (r.chains[2].sends[0].level - 0.25f) < 1.0e-5f && ! r.chains[2].sends[0].on && std::abs (r.chains[2].sends[1].level - 0.1f) < 1.0e-5f);
    // Audio To "…DeviceIn.0.R1,ChainIn" = return chain 1; "AudioOut/None" = the rack's output
    CHECK (r.chains[2].audioTo == 1 && r.chains[0].audioTo == -1);
    CHECK (r.returns[0].name == "A Verb" && r.returns[0].nodes.size() == 1 && r.returns[1].nodes.empty() && std::abs (r.returns[0].volume - 0.5f) < 1.0e-5f);
    CHECK (r.showPads && r.showIO && r.showSends && ! r.showReturns && r.padScroll == 18);
    // the clip's notes stay on their keys (no key shift for a Drum Rack)
    bool c1 = false, fs1 = false;
    for (const auto& p : song->patterns)
        for (const auto& n : p.notes)
            if (n.channel == (int) (ch - song->channels.data()))
                c1 = c1 || n.key == 36, fs1 = fs1 || n.key == 42;
    CHECK (c1 && fs1);
    CHECK (report.converted.count ("Drum rack") == 1);
    // the import before 190ec stays as an option: one sampler channel per pad
    auto old = drumOptions (nullptr);
    old.drumPadsAsChannels = true;
    const auto split = importAls (als175a::bytesOf (drumSet()), old);
    CHECK (split.has_value() && drumChannel (*split) == nullptr);
}

TEST (drumrack190ec_als_drum_rack_round_trip)
{
    using namespace drumrack190ec;
    const auto first = importAls (als175a::bytesOf (drumSet()), drumOptions (nullptr));
    CHECK (first.has_value() && drumChannel (*first) != nullptr);
    if (! first || drumChannel (*first) == nullptr)
        return;
    const auto before = *rack::rackOf (drumChannel (*first)->patcher);
    for (const auto version : { AlsVersion::live12, AlsVersion::live11 })
    {
        AlsExportOptions o;
        o.version = version;
        o.sampleFormat = [] (const std::string&, double& frames, double& rate) {
            frames = 96000.0, rate = 48000.0;
            return true;
        };
        const auto out = exportAls (*first, o);
        CHECK (! out.bytes.empty());
        std::string why;
        CHECK (als175a::loaderRulesHold (out.xml, 0, why));
        if (! why.empty())
            std::printf ("  loader rule: %s\n", why.c_str());
        CHECK (out.xml.find ("<DrumGroupDevice") != std::string::npos && out.xml.find ("<DrumBranch ") != std::string::npos
               && out.xml.find ("<ReturnBranch ") != std::string::npos && out.xml.find ("<OriginalSimpler") != std::string::npos);
        // the pad routed into return 1 by this track's and device's Ids [DATA form]
        CHECK (out.xml.find ("/DeviceIn.0.R1,ChainIn\" />") != std::string::npos);
        const auto back = importAls (out.bytes, drumOptions (nullptr));
        CHECK (back.has_value() && drumChannel (*back) != nullptr);
        if (! back || drumChannel (*back) == nullptr)
            continue;
        const auto& after = *rack::rackOf (drumChannel (*back)->patcher);
        CHECK (after.chains.size() == before.chains.size() && after.returns.size() == before.returns.size());
        for (std::size_t c = 0; c < std::min (after.chains.size(), before.chains.size()); ++c)
        {
            const auto& a = before.chains[c];
            const auto& b = after.chains[c];
            CHECK (a.name == b.name && a.receive == b.receive && a.play == b.play && a.choke == b.choke && a.audioTo == b.audioTo);
            CHECK (a.sends.size() == b.sends.size());
            for (std::size_t k = 0; k < std::min (a.sends.size(), b.sends.size()); ++k)
                CHECK (std::abs (a.sends[k].level - b.sends[k].level) < 1.0e-5f && a.sends[k].on == b.sends[k].on);
            // the pads' Samplers come back as Samplers with their sample and their one-shot / classic mode (the plugin
            // effect only when Live's plugin record survives: kept by reference)
            CHECK (a.nodes.empty() == b.nodes.empty());
            if (! a.nodes.empty() && ! b.nodes.empty())
            {
                const auto& x = drumChannel (*first)->patcher.nodes[(std::size_t) a.nodes[0]].channel;
                const auto& y = drumChannel (*back)->patcher.nodes[(std::size_t) b.nodes[0]].channel;
                CHECK (y.instrument == InstrumentKind::sampler && x.samplePath == y.samplePath && (x.sampler.release == 0.0f) == (y.sampler.release == 0.0f)
                       && std::abs (x.sampler.gainDb - y.sampler.gainDb) < 0.01f);
            }
        }
        for (std::size_t k = 0; k < std::min (after.returns.size(), before.returns.size()); ++k)
            CHECK (after.returns[k].name == before.returns[k].name && std::abs (after.returns[k].volume - before.returns[k].volume) < 1.0e-5f
                   && after.returns[k].nodes.size() == before.returns[k].nodes.size());
        CHECK (after.padScroll == before.padScroll && after.showIO == before.showIO && after.showSends == before.showSends);
    }
}
