// Round 190hr (slice RK-2, "Live Racks finish"): Group / Ungroup of an instrument (an Instrument Rack), Live rack presets
// (.adg) written and read back in Live's preset form, instrument .adv / .adg presets, and the chains of Live's own .adg
// files (their BranchPresets) read at last. Facts: docs/analise/ALS_FORMAT.md §26.

#include "core/XmlLite.h"
#include "core/Gzip.h"

namespace rack190hr
{
using namespace lynx;

bool approx (float a, float b, float tol = 1.0e-4f) { return std::abs (a - b) <= tol; }
constexpr const char* liveRoot = "/mnt/c/ProgramData/Ableton/Live 12 Trial/Resources";

std::vector<std::uint8_t> readAll (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

ForeignPlugin vst3 (const char* name, std::uint8_t seed)
{
    ForeignPlugin f;
    f.format = ForeignPlugin::Format::vst3;
    f.name = name;
    for (int i = 0; i < 16; ++i)
        f.classId[(std::size_t) i] = (std::uint8_t) (seed + i * 7);
    f.hasClassId = true;
    return f;
}

PatchNode pluginEffect (const char* name, std::uint8_t seed)
{
    PatchNode n;
    n.effect.kind = EffectKind::plugin;
    n.effect.plugin.name = name;
    n.effect.plugin.foreign = vst3 (name, seed);
    return n;
}

PatchNode pluginInstrument (const char* name, std::uint8_t seed)
{
    PatchNode n;
    n.generator = true;
    n.channel.instrument = InstrumentKind::plugin;
    n.channel.name = name;
    n.channel.plugin.name = name;
    n.channel.plugin.foreign = vst3 (name, seed);
    return n;
}

int count (const std::string& text, const std::string& what)
{
    int n = 0;
    for (auto at = text.find (what); at != std::string::npos; at = text.find (what, at + what.size()))
        ++n;
    return n;
}
} // namespace rack190hr

TEST (rack190hr_group_an_instrument_into_an_instrument_rack_and_back)
{
    using namespace rack190hr;
    const auto song = perfmode165::patcherizeSong(); // (a synth on insert 1 with five effects)
    const auto before = perfmode165::render (song);
    auto grouped = song;
    CHECK (rack::canGroupChannel (grouped, 0));
    CHECK (rack::groupChannel (grouped, 0));
    const auto& ch = grouped.channels[0];
    const auto* r = rack::rackOf (ch.patcher);
    CHECK (rack::isRack (ch) && r != nullptr && r->kind == RackDoc::Kind::instrument && r->chains.size() == 1 && r->showDevices);
    if (r == nullptr || r->chains.empty())
        return;
    // the channel keeps its own settings; the instrument in the chain plays with neutral ones
    CHECK (ch.name == "Lead" && approx (ch.volume, 0.6f) && approx (ch.pan, -0.3f) && ch.mixerTrack == 1);
    const auto& devices = rack::devices (*r, 0);
    CHECK (devices.size() == 1 && ch.patcher.nodes[(std::size_t) devices[0]].generator
           && ch.patcher.nodes[(std::size_t) devices[0]].channel.instrument == InstrumentKind::synth
           && approx (ch.patcher.nodes[(std::size_t) devices[0]].channel.pan, 0.0f));
    const auto inRack = perfmode165::render (grouped);
    const auto d1 = perfmode165::maxDiff (before, inRack);
    CHECK (perfmode165::peak (before) > 0.01f && d1 < 1.0e-4f);
    CHECK (! rack::canGroupChannel (grouped, 0)); // (a rack already)
    CHECK (rack::canUngroupChannel (grouped, 0));
    auto back = grouped;
    CHECK (rack::ungroupChannel (back, 0));
    CHECK (back.channels[0].instrument == InstrumentKind::synth && ! rack::isRack (back.channels[0]) && back.channels[0].synth == song.channels[0].synth
           && approx (back.channels[0].volume, 0.6f) && back.channels[0].mixerTrack == 1 && back.mixer[1].effects.size() == 5);
    const auto d2 = perfmode165::maxDiff (before, perfmode165::render (back));
    CHECK (d2 < 1.0e-4f);
    std::printf ("  group instrument: max diff %.3g (rack), %.3g (ungrouped)\n", d1, d2);

    // an effect after the instrument in the chain goes to the front of the channel's insert on Ungroup
    auto withFx = grouped;
    auto& doc = withFx.channels[0].patcher;
    PatchNode utility;
    utility.effect = makeEffectSlot (EffectKind::utility);
    CHECK (rack::addDevice (doc, 0, utility) >= 0);
    CHECK (rack::ungroupChannel (withFx, 0));
    CHECK (withFx.mixer[1].effects.size() == 6 && withFx.mixer[1].effects[0].kind == EffectKind::utility
           && withFx.mixer[1].effects[1].kind == EffectKind::eq);

    // refused: Master as the insert with effects to place, two chains, a Drum Rack, an audio clip channel
    auto toMaster = grouped;
    toMaster.channels[0].mixerTrack = 0;
    CHECK (rack::addDevice (toMaster.channels[0].patcher, 0, utility) >= 0);
    CHECK (! rack::canUngroupChannel (toMaster, 0) && ! rack::ungroupChannel (toMaster, 0));
    auto two = grouped;
    rack::addChain (two.channels[0].patcher);
    CHECK (! rack::canUngroupChannel (two, 0));
    auto drums = song;
    drums.channels[0].instrument = InstrumentKind::patcher;
    drums.channels[0].patcher = rack::makeDrumRack();
    CHECK (! rack::canUngroupChannel (drums, 0) && ! rack::canGroupChannel (drums, 0));
    auto clip = song;
    clip.channels[0].instrument = InstrumentKind::audioClip;
    CHECK (! rack::canGroupChannel (clip, 0));
}

TEST (rack190hr_group_a_plugin_instrument_keeps_its_automation_through_the_macros)
{
    using namespace rack190hr;
    auto song = perfmode165::patcherizeSong();
    auto& ch = song.channels[0];
    ch.instrument = InstrumentKind::plugin;
    ch.plugin.name = "Keys";
    ch.plugin.foreign = vst3 ("Keys", 3);
    Automation a;
    a.target = { AutomationTargetKind::channelParam, 0, 0, 7 };
    song.automations.push_back (a);
    // without the plugin's value now, no grouping (a macro at 0 would move the parameter)
    auto refused = song;
    CHECK (! rack::groupChannel (refused, 0) && refused.channels[0].instrument == InstrumentKind::plugin);
    auto grouped = song;
    CHECK (rack::groupChannel (grouped, 0, [] (int node, int param) { return node == 0 && param == 7 ? 0.25f : -1.0f; }));
    const auto* r = rack::rackOf (grouped.channels[0].patcher);
    CHECK (r != nullptr && r->macros[0].mappings.size() == 1 && r->macros[0].mappings[0].param == 7 && approx (r->macros[0].value, 31.75f, 1.0e-3f));
    CHECK (grouped.automations[0].target == (AutomationTarget { AutomationTargetKind::channelParam, 0, 0, 0 })); // (macro 1)
    CHECK (grouped.channels[0].plugin.name.empty()); // (the plugin lives in the chain now)
    CHECK (rack::ungroupChannel (grouped, 0));
    CHECK (grouped.channels[0].instrument == InstrumentKind::plugin && grouped.channels[0].plugin.name == "Keys");
    CHECK (grouped.automations[0].target == (AutomationTarget { AutomationTargetKind::channelParam, 0, 0, 7 }));
}

TEST (rack190hr_rack_presets_are_written_in_lives_preset_form_and_read_back)
{
    using namespace rack190hr;
    // an Audio Effect Rack: two chains (a nested rack and a plugin; a nested rack holding a nested rack), zones,
    // volume / pan, a macro on a chain's volume and one on the nested rack's macro 3, a variation. Plugins stay out of
    // rack presets (Live 12.4 closed on them, round 190hr live check): listed in the notes.
    auto doc = rack::makeRack (RackDoc::Kind::audioEffect);
    PatchNode nested;
    nested.effect = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    CHECK (rack::addDevice (doc, 0, nested) >= 0);
    CHECK (rack::addDevice (doc, 0, pluginEffect ("Crusher", 1)) >= 0);
    const auto second = rack::addChain (doc, "Wet");
    PatchNode outer;
    outer.effect = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    rack::addDevice (outer.effect.patcher, 0, nested);
    CHECK (rack::addDevice (doc, second, outer) >= 0);
    {
        auto* r = rack::rackOf (doc);
        r->name = "My Rack";
        r->chains[0].name = "Dry";
        r->chains[(std::size_t) second].select = { 64, 127, 70, 120 };
        rack::compile (doc);
    }
    rack::setChainPan (doc, second, -0.25f);
    RackMapping vol;
    vol.target = RackMapping::Target::chainVolume;
    vol.chain = second;
    CHECK (rack::map (doc, 1, vol));
    rack::setMacro (doc, 1, 100.0f);
    RackMapping p;
    p.node = rack::devices (*rack::rackOf (doc), 0).front();
    p.param = 2;
    p.min = 0.2f, p.max = 0.8f;
    CHECK (rack::map (doc, 0, p));
    rack::setMacro (doc, 0, 64.0f);
    rack::storeVariation (doc);

    const auto out = exportLiveRackPreset (doc, "My Rack");
    CHECK (! out.bytes.empty() && gzip::isGzip (out.bytes.data(), out.bytes.size()));
    const auto& x = out.xml;
    // Live's preset form [DATA]: the chains in BranchPresets, the rack's own Branches list empty, ids 0
    CHECK (x.find ("<GroupDevicePreset>") != std::string::npos && x.find ("<OverwriteProtectionNumber Value=\"3077\" />") != std::string::npos);
    CHECK (x.find ("MinorVersion=\"12.0_12402\"") != std::string::npos);
    CHECK (count (x, "<AudioEffectBranchPreset ") == 5 && x.find ("<AudioEffectBranch ") == std::string::npos); // (2 + 3 nested racks' chains)
    CHECK (x.find ("<Branches />") != std::string::npos && x.find ("<ReturnBranchPresets />") != std::string::npos);
    CHECK (count (x, "<GroupDevicePreset") == 4 && x.find ("<PluginDevice") == std::string::npos);
    CHECK (std::any_of (out.notes.begin(), out.notes.end(), [] (const std::string& n) { return n.find ("Plugin left out") != std::string::npos; }));
    CHECK (x.find ("<AudioBranchMixerDevice Id=\"0\">") != std::string::npos && x.find ("<Name Value=\"Wet\" />") != std::string::npos);
    CHECK (x.find ("<AutomationTarget Id=\"0\">") != std::string::npos && x.find ("<AutomationTarget Id=\"1") == std::string::npos);
    CHECK (x.find ("<UserName Value=\"My Rack\" />") != std::string::npos);

    // read back: the same rack
    FlpImportReport report;
    AlsImportOptions options;
    options.common.report = &report;
    std::string error;
    const auto back = importLiveDevicePreset (out.bytes, options, &error);
    CHECK (back.has_value() && back->rack && rack::isRack (back->slot));
    if (! back)
        return std::printf ("  error: %s\n", error.c_str()), void();
    const auto& a = *rack::rackOf (doc);
    const auto& b = *rack::rackOf (back->slot.patcher);
    CHECK (b.chains.size() == 2 && b.chains[0].name == "Dry" && b.chains[1].name == "Wet" && b.chains[1].select == a.chains[1].select
           && approx (b.chains[1].volume, a.chains[1].volume, 1.0e-4f) && approx (b.chains[1].pan, -0.25f, 1.0e-5f));
    CHECK (rack::devices (b, 0).size() == 1 && rack::devices (b, 1).size() == 1);
    if (rack::devices (b, 1).size() == 1)
    {
        const auto& inner = back->slot.patcher.nodes[(std::size_t) rack::devices (b, 1)[0]];
        CHECK (rack::isRack (inner.effect) && rack::devices (*rack::rackOf (inner.effect.patcher), 0).size() == 1);
    }
    CHECK (b.macros[0].mappings.size() == 1 && approx (b.macros[0].mappings[0].min, 0.2f) && approx (b.macros[0].mappings[0].max, 0.8f)
           && b.macros[0].value == a.macros[0].value);
    CHECK (b.macros[1].mappings.size() == 1 && b.macros[1].mappings[0].target == RackMapping::Target::chainVolume && b.macros[1].mappings[0].chain == 1);
    CHECK (b.variations == a.variations);

    // an Instrument Rack (a VST3 instrument and an effect) as an instrument preset; a Drum Rack with a return chain
    auto inst = rack::makeRack (RackDoc::Kind::instrument);
    CHECK (rack::addDevice (inst, 0, pluginInstrument ("Piano", 5)) >= 0 && rack::addDevice (inst, 0, pluginEffect ("Room", 11)) >= 0);
    rack::rackOf (inst)->chains[0].key = { 36, 72, 40, 70 };
    rack::compile (inst);
    const auto instOut = exportLiveRackPreset (inst, "Keys Rack");
    CHECK (instOut.xml.find ("<InstrumentBranchPreset ") != std::string::npos && instOut.xml.find ("<KeyRange>") != std::string::npos);
    CHECK (! importLiveDevicePreset (instOut.bytes, {}, &error).has_value()); // (not an insert effect)
    const auto instBack = importLiveInstrumentPreset (instOut.bytes, {}, &error);
    CHECK (instBack.has_value() && instBack->rack && rack::isRack (instBack->channel));
    if (instBack)
    {
        const auto& ir = *rack::rackOf (instBack->channel.patcher);
        CHECK (ir.kind == RackDoc::Kind::instrument && ir.chains.size() == 1 && ir.chains[0].key == (RackZone { 36, 72, 40, 70 })
               && rack::devices (ir, 0).empty()); // (the plugins stay out)
    }

    auto drums = rack::makeDrumRack();
    PatchNode pad;
    pad.generator = true;
    pad.channel.instrument = InstrumentKind::plugin;
    pad.channel.plugin.foreign = vst3 ("Kick", 21);
    pad.channel.plugin.name = "Kick";
    const auto kick = rack::addPad (drums, 36, "Kick", pad);
    const auto ret = rack::addReturn (drums, "Verb");
    CHECK (kick >= 0 && ret == 0);
    rack::setChoke (drums, kick, 2);
    rack::setAudioTo (drums, kick, 0);
    const auto drumOut = exportLiveRackPreset (drums, "Kit");
    CHECK (drumOut.xml.find ("<DrumBranchPreset ") != std::string::npos && drumOut.xml.find ("<ReceivingNote Value=\"92\" />") != std::string::npos
           && drumOut.xml.find ("AudioOut/RELATIVE/DeviceIn.PARENT.R0,ChainIn") != std::string::npos
           && count (drumOut.xml, "<AudioEffectBranchPreset ") == 1);
    const auto drumBack = importLiveInstrumentPreset (drumOut.bytes, {}, &error);
    CHECK (drumBack.has_value() && rack::isDrumRack (drumBack->channel));
    if (drumBack)
    {
        const auto& dr = *rack::rackOf (drumBack->channel.patcher);
        CHECK (dr.chains.size() == 1 && dr.chains[0].receive == 36 && dr.chains[0].choke == 2 && dr.chains[0].audioTo == 0 && dr.returns.size() == 1
               && dr.returns[0].name == "Verb");
    }

    // a MIDI Effect Rack preset is refused with its reason; so is a plain Live set
    auto midi = rack::makeRack (RackDoc::Kind::midiEffect);
    const auto midiOut = exportLiveRackPreset (midi, "Notes");
    CHECK (midiOut.xml.find ("<MidiEffectBranchPreset ") != std::string::npos && midiOut.xml.find ("<MidiBranchMixerDevice Id=\"0\">") != std::string::npos);
    CHECK (! importLiveInstrumentPreset (midiOut.bytes, {}, &error).has_value() && error.find ("MIDI") != std::string::npos);
    CHECK (exportLiveRackPreset (PatcherDoc(), "x").bytes.empty());
}

TEST (rack190hr_lives_own_rack_presets_open_with_their_chains)
{
    using namespace rack190hr;
    // Live's Core Library on this machine (read-only; skipped without Live): every rack's chain count = its BranchPresets
    const auto lib = std::string (rack190hr::liveRoot) + "/Core Library/Racks";
    if (! std::filesystem::exists (lib))
        return;
    int audio = 0, audioOk = 0, instruments = 0, instrumentsOk = 0, midi = 0, midiRefused = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator (lib))
    {
        if (e.path().extension() != ".adg")
            continue;
        const auto bytes = readAll (e.path());
        xml::Document doc;
        const auto raw = gzip::decompress (bytes.data(), bytes.size());
        if (! raw || ! doc.parse (std::string (raw->begin(), raw->end())))
            continue;
        const auto preset = doc.root().child ("GroupDevicePreset");
        const auto tag = std::string (preset.path ("Device").children().front().name());
        const auto chains = (int) preset.path ("BranchPresets").children().size();
        if (tag == "AudioEffectGroupDevice")
        {
            ++audio;
            if (const auto p = importLiveDevicePreset (bytes); p && rack::isRack (p->slot) && (int) rack::rackOf (p->slot.patcher)->chains.size() == chains)
                ++audioOk;
        }
        else if (tag == "InstrumentGroupDevice" || tag == "DrumGroupDevice")
        {
            if (instruments >= 120) // (a sample of the 800 or so, to keep the test short)
                continue;
            ++instruments;
            if (const auto p = importLiveInstrumentPreset (bytes); p && rack::isRack (p->channel) && (int) rack::rackOf (p->channel.patcher)->chains.size() == chains)
                ++instrumentsOk;
        }
        else if (tag == "MidiEffectGroupDevice")
        {
            ++midi;
            std::string error;
            if (! importLiveInstrumentPreset (bytes, {}, &error) && error.find ("MIDI") != std::string::npos)
                ++midiRefused;
        }
    }
    std::printf ("  Core Library racks with their chains: audio %d of %d, instrument / drum %d of %d; MIDI refused %d of %d\n", audioOk, audio,
                 instrumentsOk, instruments, midiRefused, midi);
    CHECK (audioOk == audio && instrumentsOk == instruments && midiRefused == midi);
}

// The presets opened in Live 12.4 for the live check (HANDOFF): written only when RACK190HR_LIVE_DIR is set
TEST (rack190hr_write_presets_for_the_live_check)
{
    using namespace rack190hr;
    const auto* dir = std::getenv ("RACK190HR_LIVE_DIR");
    if (dir == nullptr)
        return;
    const auto save = [dir] (const PatcherDoc& doc, const std::string& name) {
        const auto out = exportLiveRackPreset (doc, name);
        CHECK (! out.bytes.empty());
        std::ofstream f (std::string (dir) + "/" + name + ".adg", std::ios::binary);
        f.write (reinterpret_cast<const char*> (out.bytes.data()), (std::streamsize) out.bytes.size());
        for (const auto& n : out.notes)
            std::printf ("  %s: %s\n", name.c_str(), n.c_str());
    };
    // kHs Gain (VST3, installed here) in two chains, a nested rack, macros on the plugin's slot and a chain volume, a variation
    ForeignPlugin khs;
    khs.format = ForeignPlugin::Format::vst3;
    khs.name = "kHs Gain";
    khs.classId = alsVst3ClassId ({ als175a::khsFields[0], als175a::khsFields[1], als175a::khsFields[2], als175a::khsFields[3] });
    khs.hasClassId = true;
    PatchNode gain;
    gain.effect.kind = EffectKind::plugin;
    gain.effect.plugin.name = "kHs Gain";
    gain.effect.plugin.foreign = khs;
    auto aer = rack::makeRack (RackDoc::Kind::audioEffect);
    rack::addDevice (aer, 0, gain);
    const auto wet = rack::addChain (aer, "Wet");
    PatchNode nested;
    nested.effect = rack::makeRackSlot (RackDoc::Kind::audioEffect);
    rack::addDevice (nested.effect.patcher, 0, gain);
    rack::addDevice (aer, wet, nested);
    rack::rackOf (aer)->chains[0].name = "Dry";
    rack::rackOf (aer)->chains[(std::size_t) wet].select = { 64, 127, 70, 120 };
    rack::compile (aer);
    rack::setChainVolume (aer, wet, 0.5f);
    rack::setChainPan (aer, wet, -0.25f);
    RackMapping vol;
    vol.target = RackMapping::Target::chainVolume;
    vol.chain = wet;
    rack::map (aer, 1, vol);
    rack::setMacro (aer, 1, 90.0f);
    rack::storeVariation (aer);
    save (aer, "LYNX AER 190hr");
    // an Instrument Rack: two key-split chains (empty: LYNX's own instruments have no Live device), a selector macro
    auto inst = rack::makeRack (RackDoc::Kind::instrument);
    rack::addChain (inst, "High");
    rack::rackOf (inst)->chains[0].key = { 0, 59, 0, 59 };
    rack::rackOf (inst)->chains[1].key = { 60, 127, 60, 127 };
    rack::compile (inst);
    RackMapping sel;
    sel.target = RackMapping::Target::selector;
    rack::map (inst, 0, sel);
    save (inst, "LYNX Instrument Rack 190hr");
    // a Drum Rack: a pad with kHs Gain into a return chain, choke group 1
    auto drums = rack::makeDrumRack();
    const auto pad = rack::addPad (drums, 36, "Kick");
    rack::addDevice (drums, pad, gain);
    rack::addReturn (drums, "Verb");
    rack::setChoke (drums, pad, 1);
    rack::setAudioTo (drums, pad, 0);
    save (drums, "LYNX Drum Rack 190hr");
    // a MIDI Effect Rack with two chains
    auto midi = rack::makeRack (RackDoc::Kind::midiEffect);
    rack::addChain (midi, "Up");
    save (midi, "LYNX MIDI Rack 190hr");
}
