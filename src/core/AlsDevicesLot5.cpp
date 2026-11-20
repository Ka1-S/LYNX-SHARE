// Live's own devices → LYNX's, lot 5 (round 190kk; core/AlsDevices.h has the list and the evidence tags): MIDI Effect
// Racks written into the notes, and the devices that were still dropped (Erosion, Vinyl Distortion, Corpus, Looper).
// Same rules as the earlier lots: each conversion is a law in a unit, what is fitted or has no counterpart becomes a
// note for the import report, and only the device's saved state is read. The laws were measured on Live 12.4's own
// renders of probe sets (round 190kk, /mnt/e/lynx-work/livedev190kk: sets k1-render / k2-render, RESULTS.md).

#include "core/AlsDevices.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Corrode.h"
#include "core/Effects.h"
#include "core/Platter.h"
#include "core/Soundboard.h"
#include "core/XmlLite.h"

namespace lynx::alsdev
{
namespace
{
using xml::Element;

double manual (const Element& device, const std::string& name, double fallback) { return device.number (name + "/Manual", fallback); }
bool has (const Element& device, const std::string& name) { return (bool) device.path (name + "/Manual"); }

RackZone zoneOf (const Element& e, RackZone fallback)
{
    if (! e)
        return fallback;
    RackZone z;
    z.lo = std::clamp ((int) e.number ("Min", fallback.lo), 0, 127);
    z.hi = std::clamp ((int) e.number ("Max", fallback.hi), 0, 127);
    z.fadeLo = std::clamp ((int) e.number ("CrossfadeMin", z.lo), 0, 127);
    z.fadeHi = std::clamp ((int) e.number ("CrossfadeMax", z.hi), 0, 127);
    return z;
}

bool isFull (const RackZone& z, int lo) noexcept { return z.lo <= lo && z.hi >= 127 && z.fadeLo <= z.lo && z.fadeHi >= z.hi; }

std::string deviceLabel (const Element& d)
{
    static const std::pair<const char*, const char*> names[] {
        { "MidiArpeggiator", "Arpeggiator" }, { "MidiScale", "Scale" }, { "MidiRandom", "Random" }, { "MidiNoteEcho", "Note Echo" },
        { "MidiPitcher", "Pitch" }, { "MidiVelocity", "Velocity" }, { "MidiChord", "Chord" }, { "MidiNoteLength", "Note Length" },
        { "MxDeviceMidiEffect", "Max MIDI effect" }, { "MidiEffectGroupDevice", "MIDI Effect Rack" } };
    for (const auto& [tag, name] : names)
        if (d.name() == tag)
            return name;
    return std::string (d.name());
}
} // namespace

std::array<double, 16> rackMacroValues (const Element& rack)
{
    std::array<double, 16> m {};
    for (int k = 0; k < 16; ++k)
        m[(std::size_t) k] = std::clamp (rack.number ("MacroControls." + std::to_string (k) + "/Manual", 0.0), 0.0, 127.0);
    return m;
}

double liveValue (const Element& device, const std::string& name, double fallback, const Context& context)
{
    const auto value = manual (device, name, fallback);
    if (context.macros == nullptr)
        return value;
    const auto p = device.path (name);
    const auto key = p ? p.child ("KeyMidi") : Element {};
    if (! key || (int) key.number ("Channel", -1) != 16)
        return value;
    const auto macro = (int) key.number ("NoteOrController", -1);
    if (macro < 0 || macro >= 16)
        return value;
    const auto lo = p.number ("MidiControllerRange/Min", value), hi = p.number ("MidiControllerRange/Max", value);
    return lo + (hi - lo) * (*context.macros)[(std::size_t) macro] / 127.0;
}

bool midiRackIsPlain (const Element& rack)
{
    const auto branches = rack.path ("Branches").children();
    if (branches.size() != 1)
        return false;
    const auto& b = branches.front();
    const auto select = zoneOf (b.child ("BranchSelectorRange"), RackZone { 0, 0, 0, 0 });
    const auto selector = (int) rack.number ("ChainSelector/Manual", 0.0);
    return b.boolean ("MixerDevice/Speaker/Manual", true) && isFull (zoneOf (b.path ("ZoneSettings/KeyRange"), RackZone {}), 0)
           && isFull (zoneOf (b.path ("ZoneSettings/VelocityRange"), RackZone { 1, 127, 1, 127 }), 1) && select.lo <= selector
           && select.hi >= selector && select.fadeLo <= selector && select.fadeHi >= selector;
}

std::optional<NoteEffect> midiRackNotes (const Element& rack, const Context& context, Notes& notes, std::string& unsupported)
{
    NoteEffect e;
    e.kind = NoteEffect::Kind::rack;
    e.name = "MIDI Effect Rack";
    const auto macros = rackMacroValues (rack);
    auto inner = context;
    inner.macros = &macros;
    // [MEASURED k2 m10] a rack switched off passes its notes unchanged: a rack with no chain does that
    if (! rack.boolean ("On/Manual", true))
        return e.chains.push_back ({ RackZone {}, RackZone { 1, 127, 1, 127 }, RackZone { 0, 127, 0, 127 }, true, {} }), e;
    e.selector = (float) std::clamp (liveValue (rack, "ChainSelector", 0.0, context), 0.0, 127.0);
    for (const auto& b : rack.path ("Branches").children())
    {
        NoteRackChain chain;
        chain.active = b.boolean ("MixerDevice/Speaker/Manual", true);
        chain.key = zoneOf (b.path ("ZoneSettings/KeyRange"), RackZone {});
        chain.velocity = zoneOf (b.path ("ZoneSettings/VelocityRange"), RackZone { 1, 127, 1, 127 });
        chain.select = zoneOf (b.child ("BranchSelectorRange"), RackZone { 0, 0, 0, 0 });
        for (const auto& d : b.path ("DeviceChain/MidiToMidiDeviceChain/Devices").children())
        {
            if (! d.boolean ("On/Manual", true))
                continue; // a device switched off passes the notes
            if (d.name() == "MidiEffectGroupDevice")
            {
                auto nested = midiRackNotes (d, inner, notes, unsupported);
                if (! nested)
                    return std::nullopt;
                chain.effects.push_back (std::move (*nested));
                continue;
            }
            auto n = noteEffectFromLiveDevice (d, inner, notes);
            if (! n)
            {
                unsupported = deviceLabel (d);
                return std::nullopt;
            }
            chain.effects.push_back (std::move (*n));
        }
        e.chains.push_back (std::move (chain));
    }
    return e;
}

// ================================================================================================ Erosion → Corrode

bool erosionToCorrode (const Element& device, Translated& out)
{
    namespace c = corrode;
    const auto legacy = device.name() == "Erosion";
    if (legacy ? ! has (device, "Amplitude") || ! has (device, "Freq") : ! has (device, "Amount") || ! has (device, "Frequency"))
        return false;
    out.slot = makeEffectSlot (EffectKind::corrode);
    auto& p = out.slot.params;
    if (legacy)
    {
        // Mode [MEASURED 190kk: 0 noise (both channels the same), 1 wide noise (independent channels), 2 sine]
        const auto mode = std::clamp ((int) manual (device, "Mode", 0), 0, 2);
        const auto amplitude = manual (device, "Amplitude", 0.0);
        const auto hz = std::clamp (manual (device, "Freq", 1000.0), 20.0, 18000.0);
        p[c::amount] = (float) (mode == 2 ? c::legacySineAmount (amplitude) : c::legacyNoiseAmount (amplitude, hz));
        p[c::frequency] = c::frequencyValue (manual (device, "Freq", 1000.0));
        p[c::width] = c::widthValue (manual (device, "BandQ", 0.5));
        p[c::noise] = mode == 2 ? 0.0f : 1.0f;
        p[c::stereo] = mode == 1 ? 1.0f : 0.0f;
        out.links.push_back ({ "Freq", c::frequency, [] (double hz) { return c::frequencyValue (hz); } });
        out.links.push_back ({ "BandQ", c::width, [] (double w) { return c::widthValue (w); } });
        if (mode == 2)
            out.links.push_back ({ "Amplitude", c::amount, [] (double a) { return (float) c::legacySineAmount (a); } });
        else
            out.links.push_back ({ "Amplitude", c::amount, [hz] (double a) { return (float) c::legacyNoiseAmount (a, hz); } });
        return true;
    }
    p[c::amount] = (float) std::clamp (manual (device, "Amount", 0.5), 0.0, 1.0);
    p[c::frequency] = c::frequencyValue (manual (device, "Frequency", 1000.0));
    p[c::width] = c::widthValue (manual (device, "FilterWidth", 0.5));
    p[c::noise] = (float) std::clamp (manual (device, "NoiseBlend", 1.0), 0.0, 1.0);
    p[c::stereo] = (float) std::clamp (manual (device, "StereoWidth", 0.0), 0.0, 1.0);
    out.links.push_back ({ "Amount", c::amount, [] (double v) { return (float) std::clamp (v, 0.0, 1.0); } });
    out.links.push_back ({ "Frequency", c::frequency, [] (double hz) { return c::frequencyValue (hz); } });
    out.links.push_back ({ "FilterWidth", c::width, [] (double w) { return c::widthValue (w); } });
    out.links.push_back ({ "NoiseBlend", c::noise, [] (double v) { return (float) std::clamp (v, 0.0, 1.0); } });
    out.links.push_back ({ "StereoWidth", c::stereo, [] (double v) { return (float) std::clamp (v, 0.0, 1.0); } });
    const auto st = (double) p[c::stereo];
    if (st > 0.001 && st < 0.999)
        out.notes.approximated.push_back ("Stereo between 0 and 100 %: the right channel's blend measured only at the ends");
    return true;
}

// ================================================================================================ Vinyl Distortion → Platter

bool vinylToPlatter (const Element& device, Translated& out)
{
    namespace v = platter;
    if (! has (device, "Drive") || ! has (device, "Gain1") || ! has (device, "CracleVolume"))
        return false;
    out.slot = makeEffectSlot (EffectKind::platter);
    auto& p = out.slot.params;
    const auto unit = [] (double x) { return (float) std::clamp (x, 0.0, 1.0); };
    const auto onOff = [] (double x) { return x >= 0.5 ? 1.0f : 0.0f; };
    const auto hz = [] (double f) { return v::bandHzValue (f); };
    const auto q = [] (double x) { return v::bandQValue (x); };
    struct Map
    {
        const char* live;
        int param;
        double fallback;
        std::function<float (double)> law;
    };
    const Map maps[] { { "Gain1", v::tracingDrive, 0.5, unit }, { "BandFreq1", v::tracingFreq, 1000.0, hz }, { "BandQ1", v::tracingWidth, 3.0, q },
                       { "Gain2", v::pinchDrive, 0.5, unit }, { "BandFreq2", v::pinchFreq, 684.0, hz }, { "BandQ2", v::pinchWidth, 0.32, q },
                       { "Drive", v::drive, 0.0, unit }, { "SoftHard", v::hard, 0.0, onOff }, { "NormalMono", v::mono, 0.0, onOff },
                       { "CracleDensity", v::crackleDensity, 10.0, [] (double d) { return (float) std::clamp (d / 50.0, 0.0, 1.0); } },
                       { "CracleVolume", v::crackleVolume, 0.0, unit } };
    for (const auto& m : maps)
    {
        p[(std::size_t) m.param] = m.law (manual (device, m.live, m.fallback));
        out.links.push_back ({ m.live, m.param, m.law });
    }
    p[v::tracingOn] = device.boolean ("Band1On/Manual", true) ? 1.0f : 0.0f;
    p[v::pinchOn] = device.boolean ("Band2On/Manual", true) ? 1.0f : 0.0f;
    out.links.push_back ({ "Band1On", v::tracingOn, onOff });
    out.links.push_back ({ "Band2On", v::pinchOn, onOff });
    if (manual (device, "CracleVolume", 0.0) > 0.0)
        out.notes.approximated.push_back ("crackle: a random texture with Live's level and colour, not its clicks");
    return true;
}

// ================================================================================================ Looper

bool looperToSlot (const Element& device, Translated& out)
{
    if (! has (device, "Monitor") || ! has (device, "State"))
        return false;
    // A set holds no Looper recording (the loop lives only while Live runs [DATA: State 0 = stopped in every shipped
    // preset; no sample reference in the device]): an empty Looper. What it does to its input then follows Monitor
    // [MEASURED 190kk, 4 renders: 0 and 3 pass the input unchanged (bit-exact), 1 and 2 are silent]
    const auto monitor = std::clamp ((int) manual (device, "Monitor", 0), 0, 3);
    if (monitor == 1 || monitor == 2)
    {
        out.slot = makeEffectSlot (EffectKind::utility);
        out.slot.params[0] = 0.0f; // (the gain knob's bottom: muted)
        out.to = "Utility (muted: Live's Looper does not pass its input with this Monitor setting while it holds no loop)";
    }
    else
    {
        // LYNX's slot recorder: it passes the input and records / plays a take in the slot, as a looper does
        out.slot = makeEffectSlot (EffectKind::edison);
        out.to = "Edison (an empty slot recorder: the input passes, as through Live's empty Looper)";
    }
    out.notes.dropped.push_back ("the Looper's recording (a set saves none) and its transport controls (Song / Tempo control, quantization)");
    if (device.boolean ("Reverse/Manual", false) || std::abs (manual (device, "Pitch", 0.0)) > 0.01 || manual (device, "Feedback", 1.0) < 0.999)
        out.notes.dropped.push_back ("Reverse / Pitch / Feedback (they act on the loop only)");
    return true;
}

// ================================================================================================ Corpus → Soundboard

bool corpusToSoundboard (const Element& device, Translated& out)
{
    namespace s = soundboard;
    if (! has (device, "ResonanceType") || ! has (device, "Frequency") || ! has (device, "Decay"))
        return false;
    // a pipe or tube with its Opening below 100 %: Soundboard has no model for it — on Live's renders (Opening 0.5 / 0)
    // it scores worse than leaving the device out, so it stays dropped and listed, as before (the quality gate)
    if (const auto t = (int) std::lround (manual (device, "ResonanceType", 3.0)); (t == 5 || t == 6) && manual (device, "TubeOpening", 1.0) < 0.95)
        return false;
    out.slot = makeEffectSlot (EffectKind::soundboard);
    auto& p = out.slot.params;
    const auto unit = [] (double v) { return (float) std::clamp (v, 0.0, 1.0); };
    const auto bip = [] (double v) { return s::bipolarValue (v); };
    struct Map
    {
        const char* live;
        int param;
        double fallback;
        std::function<float (double)> law;
    };
    const auto kindLaw = [] (double v) { return s::kindValue ((s::Kind) std::clamp ((int) std::lround (v), 0, 6)); };
    const auto qualityLaw = [] (double v) { return s::qualityValue ((int) std::lround (v)); };
    const Map maps[] {
        { "ResonanceType", s::type, 3.0, kindLaw }, { "ResonatorQuality", s::quality, 3.0, qualityLaw }, { "Frequency", s::tune, 0.25, unit },
        { "Decay", s::decay, 0.5, unit }, { "FreqDamping", s::material, 0.0, bip }, { "AmpFreq", s::brightness, 0.0, bip },
        { "Inharmonics", s::inharmonics, 0.0, bip }, { "Radius", s::radius, 0.5, unit }, { "TubeOpening", s::opening, 1.0, unit },
        { "Ratio", s::ratio, 0.5, unit }, { "ExcitationX", s::hit, 0.5, unit }, { "ListeningXL", s::listenL, 0.3, unit },
        { "ListeningXR", s::listenR, 0.7, unit }, { "Detune", s::spread, 0.0, bip }, { "Drive", s::gain, 0.0, unit }, { "Bleed", s::bleed, 0.0, unit },
        { "DryWet", s::dryWet, 1.0, unit }, { "StereoWidth", s::width, 1.0, unit },
        { "FilterMidFreq", s::filterHz, 1000.0, [] (double hz) { return s::filterHzValue (hz); } },
        { "FilterBandWidth", s::filterWidth, 4.0, [] (double o) { return s::filterOctavesValue (o); } } };
    for (const auto& m : maps)
    {
        p[(std::size_t) m.param] = m.law (manual (device, m.live, m.fallback));
        out.links.push_back ({ m.live, m.param, m.law });
    }
    p[s::filterOn] = device.boolean ("FilterOn/Manual", false) ? 1.0f : 0.0f;
    out.links.push_back ({ "FilterOn", s::filterOn, [] (double v) { return v >= 0.5 ? 1.0f : 0.0f; } });
    // what Soundboard does not hold
    if (device.boolean ("LfoOnOff/Manual", false) && manual (device, "LfoAmount", 0.0) > 0.001)
        out.notes.dropped.push_back ("the LFO on the tuning");
    if (device.boolean ("MidiPitch/Manual", false))
        out.notes.dropped.push_back ("MIDI frequency (the side-chain notes set the pitch in Live; Soundboard keeps Tune)");
    if (device.boolean ("MidiGate/Manual", false))
        out.notes.dropped.push_back ("MIDI gate (Off Decay)");
    const auto kind = s::kindOf (p[s::type]);
    if ((kind == s::Kind::tube || kind == s::Kind::pipe) && manual (device, "TubeOpening", 1.0) < 0.999)
        out.notes.approximated.push_back ("Opening slightly below 100 % (held as closed)");
    if (kind != s::Kind::pipe && kind != s::Kind::tube)
        out.notes.approximated.push_back ("hit and listening positions as an ideal string's mode shapes (fitted; the other objects' shapes approximated)");
    if (std::abs (manual (device, "FreqDamping", 0.0)) > 0.01)
        out.notes.approximated.push_back ("Material: the damping tilt fitted at -100, 0 and +100 %");
    return true;
}
} // namespace lynx::alsdev
