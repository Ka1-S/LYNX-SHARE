// Round 190el: the Drum Rack made to match what Ableton Live 12.4 showed in the 190ei live check
// (/mnt/e/lynx-work/live190ei/ANSWERS.md §B): the empty rack's views, return chains named with their letter, Live's
// "Slice N" pads and the rack / track named after the clip, the Slice to New MIDI Track list, the note editor's pad rows,
// Save as Default Pad (copyPad) and the .als a Live check opens (written by LYNX's own export when
// LYNX_DRUMRACK190EL_EXPORT names a folder).

namespace drumrack190el
{
using namespace lynx;

PatchNode sampler (const std::string& path, const std::string& name)
{
    PatchNode n;
    n.generator = true;
    n.channel.instrument = InstrumentKind::sampler;
    n.channel.name = name;
    n.channel.samplePath = path;
    n.channel.sampler.release = 0.0f; // (Live's pad Simpler: one-shot)
    return n;
}

// a 16-bit mono WAV (no JUCE: the test writes the samples the exported sets play)
bool writeWav (const std::string& path, const std::vector<float>& x, int rate = 44100)
{
    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const auto put32 = [f] (std::uint32_t v) { std::uint8_t b[4] { (std::uint8_t) v, (std::uint8_t) (v >> 8), (std::uint8_t) (v >> 16), (std::uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    const auto put16 = [f] (std::uint16_t v) { std::uint8_t b[2] { (std::uint8_t) v, (std::uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    const auto bytes = (std::uint32_t) (x.size() * 2);
    std::fwrite ("RIFF", 1, 4, f);
    put32 (36 + bytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    put32 (16);
    put16 (1);
    put16 (1);
    put32 ((std::uint32_t) rate);
    put32 ((std::uint32_t) rate * 2);
    put16 (2);
    put16 (16);
    std::fwrite ("data", 1, 4, f);
    put32 (bytes);
    for (const auto v : x)
        put16 ((std::uint16_t) (std::int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32000.0f));
    return std::fclose (f) == 0;
}

// the test kit's sounds: a falling sine (kick), noise bursts of several lengths, a tone burst
std::vector<float> drumSound (int kind, double seconds, std::uint32_t seed)
{
    std::vector<float> x ((std::size_t) (seconds * 44100.0));
    double phase = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto t = (double) i / 44100.0;
        seed = seed * 1664525u + 1013904223u;
        const auto noise = (double) (seed >> 8) / 8388608.0 - 1.0;
        const auto decay = std::exp (-t * (kind == 0 ? 9.0 : kind == 1 ? 18.0 : kind == 2 ? 60.0 : kind == 3 ? 6.0 : 12.0));
        phase += 2.0 * 3.14159265358979 * (kind == 0 ? 45.0 + 120.0 * std::exp (-t * 30.0) : 190.0) / 44100.0;
        const auto v = kind == 0 ? std::sin (phase) : kind == 4 ? 0.6 * std::sin (phase * 2.0) + 0.3 * noise : noise * (kind == 1 ? 0.7 : 0.5) + (kind == 1 ? 0.3 * std::sin (phase) : 0.0);
        x[i] = (float) (0.9 * v * decay);
    }
    return x;
}

// the loop sliced by the second rack: 2 bars at 120 BPM (4 s), a hit every half beat, each a different sound
std::vector<float> drumLoop()
{
    std::vector<float> loop ((std::size_t) (4.0 * 44100.0));
    for (int hit = 0; hit < 8; ++hit)
    {
        const auto s = drumSound (hit % 4 == 0 ? 0 : hit % 4 == 2 ? 1 : hit == 7 ? 3 : 2, 0.45, 77u + (std::uint32_t) hit);
        const auto at = (std::size_t) (hit * 0.5 * 44100.0);
        for (std::size_t i = 0; i < s.size() && at + i < loop.size(); ++i)
            loop[at + i] += s[i];
    }
    return loop;
}

struct TestKit
{
    Song song;
    std::vector<std::pair<std::string, std::vector<float>>> samples; // file name, audio
};

// The set for the Live check: track "LYNX Kit" (a Drum Rack, 7 pads, two return chains, sends, a pad into a return,
// two hats in choke group 1, a pad with its own Play note), track "drums" (the loop sliced per 1/4 note: 8 pads
// "Slice 1"… in choke group 1, the rack named "drums"). `dir`: where the samples are (as the set will refer to them).
TestKit testKit (const std::string& dir)
{
    TestKit kit;
    auto& song = kit.song;
    song.bpm = 120.0;
    song.beatsPerBar = 4;
    song.channels.clear();
    song.patterns.clear();
    song.playlist.clear();
    struct PadSpec { const char* name; const char* file; int key, kind; double seconds; };
    const PadSpec pads[] { { "Kick", "kick.wav", 36, 0, 0.6 },  { "Rim", "rim.wav", 37, 2, 0.15 },         { "Snare", "snare.wav", 38, 1, 0.35 },
                           { "Clap", "clap.wav", 39, 1, 0.3 },  { "Hat Closed", "hat-closed.wav", 42, 2, 0.08 }, { "Tom", "tom.wav", 43, 4, 0.5 },
                           { "Hat Open", "hat-open.wav", 46, 3, 0.7 } };
    ChannelDef kitChannel;
    kitChannel.name = "LYNX Kit";
    kitChannel.instrument = InstrumentKind::patcher;
    kitChannel.patcher = rack::makeDrumRack();
    auto& doc = kitChannel.patcher;
    for (const auto& p : pads)
    {
        kit.samples.push_back ({ p.file, drumSound (p.kind, p.seconds, (std::uint32_t) p.key) });
        rack::addPad (doc, p.key, p.name, sampler (dir + "/" + p.file, p.name));
    }
    rack::rackOf (doc)->name = "LYNX Kit";
    rack::addReturn (doc, "Mix Bus");
    rack::addReturn (doc, "Reverb");
    rack::setChoke (doc, 4, 1);                          // Hat Closed
    rack::setChoke (doc, 6, 1);                          // Hat Open
    rack::setPlay (doc, 5, 62);                          // Tom plays D3 (two semitones up)
    rack::setSend (doc, 2, 0, 0.5f);                     // Snare → a Mix Bus, −6 dB
    rack::setSend (doc, 6, 1, 0.25f);                    // Hat Open → b Reverb, −12 dB
    rack::setSend (doc, 0, 1, 0.5f, false);              // Kick → b Reverb, −6 dB but switched off
    rack::setAudioTo (doc, 3, 0);                        // Clap plays into a Mix Bus
    rack::setChainVolume (doc, 1, rack::volumeGain (0.5f)); // Rim quieter
    rack::setChainPan (doc, 4, -0.5f);                   // Hat Closed half left
    auto* r = rack::rackOf (doc);
    r->showPads = true, r->showChains = true, r->showDevices = true, r->showMacros = false;
    r->showIO = true, r->showSends = true, r->showReturns = true;
    r->selectedChain = 0;
    rack::compile (doc);
    kitChannel.mixerTrack = 1;
    song.channels.push_back (kitChannel);

    // the sliced loop (Live's Slice to New MIDI Track, per 1/4 note: 8 slices)
    kit.samples.push_back ({ "drums.wav", drumLoop() });
    const auto slices = rack::gridSlices (4.0, 0.0f, 1.0f, 0.5);
    auto sliced = rack::sliceToDrumRack (dir + "/drums.wav", slices, "Slice", rack::SliceTemplate::gated, 1.0f, "drums");
    sliced.mixerTrack = 2;
    song.channels.push_back (sliced);

    // the patterns: a two-bar beat on the kit, the slices replayed where they were
    Pattern beat;
    beat.name = "LYNX Kit";
    for (int step = 0; step < 32; ++step)
    {
        const auto add = [&beat, step] (int key, float velocity) {
            Note n;
            n.channel = 0;
            n.key = key;
            n.start = (std::int64_t) step * ppq / 4;
            n.length = ppq / 4;
            n.velocity = velocity;
            beat.notes.push_back (n);
        };
        if (step % 8 == 0)
            add (36, 0.9f);
        if (step % 8 == 4)
            add (38, 0.85f);
        if (step % 16 == 12)
            add (39, 0.7f);
        if (step % 2 == 0 && step % 16 != 14)
            add (42, 0.6f);
        if (step % 16 == 14)
            add (46, 0.7f);
        if (step == 30)
            add (43, 0.8f);
        if (step == 31)
            add (37, 0.6f);
    }
    beat.sortNotes();
    song.patterns.push_back (beat);
    Pattern loop;
    loop.name = "drums";
    loop.notes = rack::sliceNotes (slices, 1.0f, [] (float f) { return (double) f * 8.0 * ppq; }, 1);
    song.patterns.push_back (loop);
    for (int i = 0; i < 2; ++i)
    {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = i;
        c.track = i;
        c.start = 0;
        c.length = 8 * ppq;
        song.playlist.push_back (c);
        song.playlistTracks.resize (2);
        song.playlistTracks[(std::size_t) i].name = i == 0 ? "LYNX Kit" : "drums";
    }
    return kit;
}

// "/mnt/e/x" → "E:/x" (the sets are opened by Live on Windows)
std::string windowsPath (const std::string& path)
{
    if (path.size() > 7 && path.rfind ("/mnt/", 0) == 0 && path[6] == '/')
        return std::string (1, (char) std::toupper ((unsigned char) path[5])) + ":" + path.substr (6);
    return path;
}
} // namespace drumrack190el

TEST (drumrack190el_empty_drum_rack_opens_as_live_pads_and_devices_only)
{
    using namespace lynx;
    const auto doc = rack::makeDrumRack();
    const auto& r = *rack::rackOf (doc);
    // Live 12.4's new Drum Rack (190ei shots/live-drumrack-empty.png): the pads and "Drop an Instrument or Sample Here";
    // macros, variations and the chain list hidden; C1…D#2 shown
    CHECK (r.showPads && r.showDevices && ! r.showMacros && ! r.showVariations && ! r.showChains);
    CHECK (! r.showIO && ! r.showSends && ! r.showReturns && r.padScroll == rack::drumDefaultScroll);
}

TEST (drumrack190el_return_chains_show_their_letter_and_filled_pads_list)
{
    using namespace lynx;
    auto doc = rack::makeDrumRack();
    rack::addPad (doc, 36, "Bass Drum");
    rack::addPad (doc, 42, "Closed Hi Hat");
    rack::addPad (doc, 38, "Snare Drum");
    CHECK (rack::addReturn (doc, "Mix Bus") == 0 && rack::addReturn (doc, "Reverb") == 1 && rack::addReturn (doc) == 2);
    const auto& r = *rack::rackOf (doc);
    // Live: "a Mix Bus", "b Reverb" (EffectiveName), the names kept without the letter (UserName); an unnamed one: "c"
    CHECK (r.returns[0].name == "Mix Bus" && rack::returnLabel (r, 0) == "a Mix Bus" && rack::returnLabel (r, 1) == "b Reverb");
    CHECK (rack::returnLabel (r, 2) == "c" && rack::returnLetter (1) == "b" && rack::returnLabel (r, 3).empty());
    // the note editor's rows for a Drum Rack (190ei B8): the filled pads, highest first
    CHECK ((rack::filledPadKeys (r) == std::vector<int> { 42, 38, 36 }));
    CHECK (rack::filledPadKeys (*rack::rackOf (rack::makeRack (RackDoc::Kind::instrument))).empty());
    // their key rows: one each, top to bottom; other keys folded away
    const auto rows = notetools::keyRowsOf ({ 36, 42, 38 });
    CHECK (rows.count == 3 && rows.keyOfRow[0] == 42 && rows.keyOfRow[2] == 36 && rows.rowOfKey[38] == 1 && ! rows.shown (40) && rows.shown (36));
    CHECK (notetools::keyRowsOf ({}).count == 128);
}

TEST (drumrack190el_slice_per_list_and_names_follow_live)
{
    using namespace lynx;
    // Live's "Create one slice per" (190ei B6a): Warp Marker, Transient, Bar, 1/2 Note … 1/32 Note, in that order
    const char* expected[] { "Warp Marker", "Transient", "Bar", "1/2 Note", "1/4 Note", "1/8 Note", "1/16 Note", "1/32 Note" };
    for (int k = 0; k < rack::slicePerCount; ++k)
        CHECK (std::string (rack::slicePerName ((rack::SlicePer) k)) == expected[k]);
    CHECK (rack::slicePerBeats (rack::SlicePer::bar, 4) == 4.0 && rack::slicePerBeats (rack::SlicePer::bar, 3) == 3.0);
    CHECK (rack::slicePerBeats (rack::SlicePer::half, 4) == 2.0 && rack::slicePerBeats (rack::SlicePer::thirtySecond, 4) == 0.125);
    CHECK (rack::slicePerBeats (rack::SlicePer::transient, 4) == 0.0 && rack::slicePerBeats (rack::SlicePer::warpMarker, 4) == 0.0);
    // pads "Slice 1"…, the rack and the channel named after the clip (190ei B6b)
    const auto drum = rack::sliceToDrumRack ("/x/drums.wav", { 0.0f, 0.25f, 0.5f, 0.75f }, "Slice", rack::SliceTemplate::gated, 1.0f, "drums");
    const auto& r = *rack::rackOf (drum.patcher);
    CHECK (drum.name == "drums" && r.name == "drums" && r.chains.size() == 4);
    CHECK (r.chains[0].name == "Slice 1" && r.chains[3].name == "Slice 4" && r.chains[3].receive == 39 && r.chains[0].choke == 1);
    // the new track takes the clip's name too
    Song song;
    song.channels.clear();
    song.playlist.clear();
    ChannelDef audio;
    audio.name = "drums";
    audio.instrument = InstrumentKind::audioClip;
    audio.samplePath = "/x/drums.wav";
    song.channels.push_back (audio);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.track = 0;
    clip.length = 8 * ppq;
    song.playlist.push_back (clip);
    const auto notes = rack::sliceNotes ({ 0.0f, 0.5f }, 1.0f, [] (float f) { return (double) f * 8.0 * ppq; }, 0);
    CHECK (rack::addSliceTrack (song, 0, drum, notes, "drums") == 1);
    CHECK (song.playlistTracks.size() >= 2 && song.playlistTracks[1].name == "drums" && song.patterns.back().name == "drums");
}

TEST (drumrack190el_copy_pad_saves_and_applies_a_default_pad)
{
    using namespace lynx;
    using drumrack190el::sampler;
    auto from = rack::makeDrumRack();
    const auto c = rack::addPad (from, 40, "Snare", sampler ("/x/snare.wav", "Snare"));
    PatchNode comp;
    comp.effect = makeEffectSlot (EffectKind::compressor);
    rack::addDevice (from, c, comp);
    rack::setChainVolume (from, c, rack::volumeGain (0.6f));
    rack::setChainPan (from, c, 0.25f);
    rack::setChoke (from, c, 3);
    rack::setPlay (from, c, 64);
    // the pad copied as is (Save as Default Pad keeps it this way)
    auto saved = rack::makeDrumRack();
    CHECK (rack::copyPad (from, c, saved, 36) == 0);
    const auto& s = rack::rackOf (saved)->chains[0];
    CHECK (s.name == "Snare" && s.receive == 36 && s.play == 64 && s.choke == 3 && std::abs (s.pan - 0.25f) < 1.0e-6f && s.nodes.size() == 2);
    CHECK (saved.nodes[(std::size_t) s.nodes[0]].channel.samplePath == "/x/snare.wav" && ! saved.nodes[(std::size_t) s.nodes[1]].generator);
    // a sample dropped on an empty pad with that default: its devices and settings, playing the new file, named after it
    auto target = rack::makeDrumRack();
    CHECK (rack::copyPad (saved, 0, target, 45, "/y/Big Clap.wav") == 0);
    const auto& t = rack::rackOf (target)->chains[0];
    const auto& tn = target.nodes[(std::size_t) t.nodes[0]].channel;
    CHECK (t.name == "Big Clap" && t.receive == 45 && t.choke == 3 && tn.instrument == InstrumentKind::sampler && tn.samplePath == "/y/Big Clap.wav" && tn.name == "Big Clap");
    // a default pad holding a synth: the sample gets a Sampler in its place, the effects stay
    auto synthPad = rack::makeDrumRack();
    rack::addPad (synthPad, 36, "Synth", rack183c::synthNode());
    rack::addDevice (synthPad, 0, comp);
    auto target2 = rack::makeDrumRack();
    CHECK (rack::copyPad (synthPad, 0, target2, 36, "C:\\samples\\kick.wav") == 0);
    const auto& u = rack::rackOf (target2)->chains[0];
    CHECK (u.name == "kick" && u.nodes.size() == 2 && target2.nodes[(std::size_t) u.nodes[0]].channel.instrument == InstrumentKind::sampler);
    // not into an Instrument Rack, not from a missing chain
    auto ir = rack::makeRack (RackDoc::Kind::instrument);
    CHECK (rack::copyPad (from, c, ir, 36) == -1 && rack::copyPad (from, 7, target, 50) == -1);
}

TEST (drumrack190el_als_return_names_carry_the_letter_live_shows)
{
    using namespace lynx;
    using namespace drumrack190el;
    auto kit = testKit ("E:/kit");
    for (const auto version : { AlsVersion::live12, AlsVersion::live11 })
    {
        AlsExportOptions o;
        o.version = version;
        o.sampleFormat = [] (const std::string&, double& frames, double& rate) {
            frames = 44100.0, rate = 44100.0;
            return true;
        };
        const auto out = exportAls (kit.song, o);
        CHECK (! out.bytes.empty());
        std::string why;
        CHECK (als175a::loaderRulesHold (out.xml, 0, why));
        if (! why.empty())
            std::printf ("  loader rule: %s\n", why.c_str());
        // Live's EffectiveName "a Mix Bus" / UserName "Mix Bus"; the sliced pads "Slice 1"…; the rack "drums"
        CHECK (out.xml.find ("<EffectiveName Value=\"a Mix Bus\" />") != std::string::npos && out.xml.find ("<UserName Value=\"Mix Bus\" />") != std::string::npos);
        CHECK (out.xml.find ("<EffectiveName Value=\"b Reverb\" />") != std::string::npos);
        CHECK (out.xml.find ("\"Slice 1\"") != std::string::npos && out.xml.find ("\"Slice 8\"") != std::string::npos);
        // back in: the names without the letter, every pad and its routing
        auto options = drumrack190ec::drumOptions (nullptr);
        options.common.fileExists = [] (const std::string& p) { return p.rfind ("E:/kit/", 0) == 0; };
        const auto back = importAls (out.bytes, options);
        CHECK (back.has_value());
        if (! back)
            continue;
        std::vector<const RackDoc*> racks;
        for (const auto& ch : back->channels)
            if (rack::isDrumRack (ch))
                racks.push_back (rack::rackOf (ch.patcher));
        CHECK (racks.size() == 2);
        if (racks.size() != 2)
            continue;
        CHECK (racks[0]->returns.size() == 2 && racks[0]->returns[0].name == "Mix Bus" && rack::returnLabel (*racks[0], 1) == "b Reverb");
        CHECK (racks[0]->chains.size() == 7 && racks[0]->chains[3].audioTo == 0 && racks[0]->chains[4].choke == 1 && racks[0]->chains[5].play == 62);
        CHECK (racks[1]->chains.size() == 8 && racks[1]->chains[0].name == "Slice 1" && racks[1]->chains[7].choke == 1);
    }
    // an older set whose return has no UserName: Live's EffectiveName loses its letter
    auto patched = drumrack190ec::drumSet();
    if (const auto at = patched.find ("<EffectiveName Value=\"B Bus\" />"); at != std::string::npos)
        patched.replace (at, std::string ("<EffectiveName Value=\"B Bus\" />").size(), "<EffectiveName Value=\"b Bus\" />");
    const auto song = importAls (als175a::bytesOf (patched), drumrack190ec::drumOptions (nullptr));
    CHECK (song.has_value() && drumrack190ec::drumChannel (*song) != nullptr);
    if (song && drumrack190ec::drumChannel (*song) != nullptr)
    {
        const auto& r = *rack::rackOf (drumrack190ec::drumChannel (*song)->patcher);
        CHECK (r.returns.size() == 2 && r.returns[0].name == "A Verb" && r.returns[1].name == "Bus" && rack::returnLabel (r, 1) == "b Bus");
    }
}

// The sets for the next Live check (round 190el): LYNX's own export of testKit(), Live 12 and Live 11, with the samples
// beside them. Runs only with LYNX_DRUMRACK190EL_EXPORT=<folder>.
TEST (drumrack190el_export_sets_for_the_live_check)
{
    using namespace lynx;
    using namespace drumrack190el;
    const char* dir = std::getenv ("LYNX_DRUMRACK190EL_EXPORT");
    if (dir == nullptr)
        return;
    const std::string folder = dir;
    const auto samples = folder + "/Samples";
    std::filesystem::create_directories (samples);
    auto kit = testKit (windowsPath (samples));
    for (const auto& [name, audio] : kit.samples)
        CHECK (writeWav (samples + "/" + name, audio));
    std::map<std::string, double> frames;
    for (const auto& [name, audio] : kit.samples)
        frames[windowsPath (samples) + "/" + name] = (double) audio.size();
    for (const auto version : { AlsVersion::live12, AlsVersion::live11 })
    {
        AlsExportOptions o;
        o.version = version;
        o.creator = "LYNX SHARE";
        o.sampleFormat = [&frames] (const std::string& path, double& count, double& rate) {
            const auto it = frames.find (path);
            count = it != frames.end() ? it->second : 0.0, rate = 44100.0;
            return count > 0.0;
        };
        o.sampleSeconds = [&frames] (const std::string& path) {
            const auto it = frames.find (path);
            return it != frames.end() ? it->second / 44100.0 : 0.0;
        };
        const auto out = exportAls (kit.song, o);
        const auto file = folder + (version == AlsVersion::live12 ? "/lynx-drumrack-live12.als" : "/lynx-drumrack-live11.als");
        std::FILE* f = std::fopen (file.c_str(), "wb");
        CHECK (f != nullptr && ! out.bytes.empty());
        if (f != nullptr)
        {
            std::fwrite (out.bytes.data(), 1, out.bytes.size(), f);
            std::fclose (f);
        }
        std::FILE* x = std::fopen ((file + ".xml").c_str(), "wb");
        if (x != nullptr)
        {
            std::fwrite (out.xml.data(), 1, out.xml.size(), x);
            std::fclose (x);
        }
        std::printf ("  wrote %s (%zu bytes)\n", file.c_str(), out.bytes.size());
        for (const auto& n : out.notes)
            std::printf ("    note: %s\n", n.c_str());
    }
}
