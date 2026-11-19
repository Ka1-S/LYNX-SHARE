#pragma once

// Round 173d2 (issue #77, the generators): a control of an FL-native generator that moved nothing in the round-170i3
// pass (tools/CtlAudit.h: the chord pattern, the far end of the control, 6 random contexts) is searched for the
// context that wakes it, the way tools/FxAudit.h does for the effects. A context is a key:
//   pat:mono      one note, then a second one while it is held (legato / glide / retrigger of a mono patch)
//   pat:bend      the pitch wheel and the channel's PITCH knob (FL's pitch bend of a generator) up 2 semitones
//   pat:mod       the mod wheel (CC 1) at full
//   pat:vel:x     the notes at velocity x
//   pat:rel       a note-off velocity of 0.1
//   pat:long      the notes held 3.5 s (slow LFOs, sequencers, long envelopes)
//   pat:retrig    the same chord hit again while it sounds
//   pat:pads      every key 36…51 (all 16 pads of a drum engine)
//   pat:low / pat:high   the chord two octaves down / up (key tracking, ranges)
//   pat:bpm       a tempo of 97 BPM (tempo-synced times equal free ones at 120)
//   pat:offline   rendered as an export (the render-quality settings)
//   pat:gap       a note, a gap, then another note (retriggered LFOs)
//   pat:overlap   a second note over the first, then the first let go (mono note priority)
//   pat:one       key 60 alone (a key that picks a program)
//   pat:allkeys   every key 0…127 (a drum engine's pads wherever they sit)
//   pat:vlong     the notes held 13 s (every step of a 64-step sequencer)
//   set:i:v       control i of the editor list (CtlAudit::editorControls) at raw value v
//   hint:id       a hand-written group of settings per engine (hintsFor; id = hintId of its note)
//   rand:k        a random setting of every other control (seed 1730 + k; levels and mutes keep their values)
// Keys combine with '+' ("pat:mod+set:3:1"). A control moves when its far end (or either end) changes the new
// instrument's render by more than −60 dB relative RMS against the same context (the effects' rule, round 173d).

#include <chrono>
#include <cstdlib>
#include <map>
#include <sstream>

#include "CtlAudit.h"
#include "core/FmSynth.h"
#include "core/WavetableSynth.h"
#include "core/Braid.h"
#include "core/Morphine.h"
#include "core/TransistorBass.h"

namespace lynx::ctlwake
{
using ctlaudit::Control;
using ctlaudit::Pattern;
using ctlaudit::Samples;

constexpr double wakeDb = -60.0;

struct Hint
{
    std::string note;
    std::vector<std::pair<int, float>> sets; // (parameter index as Control::index, raw value)
    std::string pattern;                     // "" or a pat: key it goes with
    std::function<void (ChannelDef&)> apply; // a state edit the editor list can't make (a part switched on)
    std::string scope;                       // "" or the name prefix of the controls the search tries it on (a pad's)
};

// a hint's key: "hint:" + a hash of its note, so a survey's keys stay valid when hints are added or reordered
inline std::string hintId (const std::string& note)
{
    std::uint32_t h = 2166136261u; // FNV-1a
    for (const auto ch : note)
        h = (h ^ (std::uint8_t) ch) * 16777619u;
    char buf[12];
    std::snprintf (buf, sizeof buf, "%08x", h);
    return buf;
}

// the hint a key's token names: its id, or (older surveys) its index
inline int findHint (const std::vector<Hint>& hints, const std::string& token)
{
    for (int i = 0; i < (int) hints.size(); ++i)
        if (hintId (hints[(std::size_t) i].note) == token)
            return i;
    if (! token.empty() && token.size() < 6 && std::all_of (token.begin(), token.end(), [] (char c) { return c >= '0' && c <= '9'; }))
        if (const auto i = std::atoi (token.c_str()); i < (int) hints.size())
            return i;
    return -1;
}

// the groups that wake a section, per engine (the controls are matched by name in the editor list)
inline std::vector<Hint> hintsFor (InstrumentKind kind, const std::vector<Control>& controls)
{
    std::vector<Hint> out;
    auto idx = [&controls] (const std::string& name) {
        for (const auto& k : controls)
            if (k.name == name)
                return k.index;
        return -1;
    };
    auto named = [&] (std::string note, std::vector<std::pair<std::string, float>> sets, std::string pat = {}) {
        Hint h { std::move (note), {}, std::move (pat) };
        for (const auto& [n, v] : sets)
            if (const auto i = idx (n); i >= 0)
                h.sets.push_back ({ i, v });
        if (! h.sets.empty() || ! h.pattern.empty() || h.apply)
            out.push_back (std::move (h));
    };
    switch (kind)
    {
        case InstrumentKind::harmor:
        {
            // part B on (its enable byte: misc + 0x44 after the part's 386 parameters and 0x30 order bytes)
            Hint h { "part B switched on", {}, {} };
            h.apply = [] (ChannelDef& d) {
                const auto at = harmorParamOffset (d.pluginState, 1, 0);
                if (at >= 0 && (std::size_t) at + 4 * 386 + 0x30 + 0x44 < d.pluginState.size())
                    d.pluginState[(std::size_t) at + 4 * 386 + 0x30 + 0x44] = 1;
            };
            out.push_back (std::move (h));
            break;
        }
        case InstrumentKind::ogun:
            // the delay's tempo sync: P27·0.3125/tempo vs P27/384 are the same time at 120 BPM (Ogun.cpp designDelay)
            named ("the delay on at 97 BPM", { { "Delay on", 1.0f } }, "pat:bpm");
            break;
        case InstrumentKind::kepler:
            // the LFO only matters routed (DCO / VCF LFO up); retrig and phase only at a note hit while it runs
            named ("the LFO on the pitch, retriggered by a note after a gap", { { "DCO LFO", 1.0f }, { "LFO retrig", 1.0f }, { "LFO rate", 0.6f }, { "LFO phase", 0.5f }, { "LFO sync", 0.0f },
                      { "LFO trig mode", 0.0f }, { "LFO delay", 0.0f } }, "pat:gap");
            break;
        case InstrumentKind::slayer:
            // FL Slayer's own wheel (parameter 28, sent as CC 1) bends the pitch in glissando mode
            named ("glissando on with Slayer's wheel up", { { "Glissando", 1.0f }, { "Pitchbend 2", 127.0f } });
            break;
        case InstrumentKind::sawer:
            // a modulation slot acts with a source LYNX feeds (velocity) and an audible destination (the cutoff)
            for (int k = 1; k <= 6; ++k)
            {
                const auto slot = "Mod " + std::to_string (k);
                std::vector<Control> three;
                for (const auto& c : controls)
                    if (c.name == slot + " source" || c.name == slot + " amount" || c.name == slot + " destination")
                        three.push_back (c);
                if (three.size() != 3)
                    continue;
                Hint h { slot + ": velocity to the cutoff at full amount", {}, "pat:vel:0.2" };
                h.apply = [three, slot] (ChannelDef& d) {
                    for (const auto& c : three)
                        c.set (d, c.name == slot + " source" ? 1.0f : c.name == slot + " amount" ? 1.0f : 27.0f);
                };
                out.push_back (std::move (h));
            }
            break;
        case InstrumentKind::toxic:
        {
            // the two effect slots: each type with its slot on (ToxicFx.cpp: 0 delay, 1 chorus, 2 reverb, 3 flanger, 4 phaser,
            // 5 lo-fi); the delay's sync only differs from its free time away from 120 BPM
            for (int sl = 1; sl <= 2; ++sl)
                for (int t = 0; t < 6; ++t)
                {
                    const auto n = "FX " + std::to_string (sl);
                    named (n + " on as type " + std::to_string (t), { { n + " on", 1.0f }, { n + " type", (float) t } }, t == 0 ? "pat:long+pat:bpm" : "pat:long");
                    out.back().scope = n + " ";
                }
            // the flanger's inverted feedback needs feedback; a delay's time is heard with its sync off
            for (int sl = 1; sl <= 2; ++sl)
            {
                const auto n = "FX " + std::to_string (sl);
                named (n + " a flanger with feedback", { { n + " on", 1.0f }, { n + " type", 3.0f }, { n + " flanger feedback", 1.0f }, { n + " flanger mix", 1.0f } }, "pat:long");
                named (n + " a free-running delay", { { n + " on", 1.0f }, { n + " type", 0.0f }, { n + " delay sync", 0.0f }, { n + " delay mix", 1.0f }, { n + " delay feedback", 0.5f } }, "pat:long");
            }
            // the SEQ page: FL's Default has every step a rest ("---"), so the sequencer plays silence until a step holds a
            // note; a step's own value is heard once another step sounds, the order switches once the steps differ
            for (int first = 1; first <= 2; ++first)
            {
                named ("the sequencer playing, step " + std::to_string (first) + " a note", { { "Seq play", 1.0f }, { "Seq step " + std::to_string (first), 26.0f / 50.0f } }, "pat:vlong");
                named ("the sequencer playing 64 steps, step " + std::to_string (first) + " a note", { { "Seq play", 1.0f }, { "Seq steps", 1.0f }, { "Seq step " + std::to_string (first), 26.0f / 50.0f } }, "pat:vlong");
            }
            {
                Hint h { "the sequencer playing 64 rising steps", {}, "pat:vlong" };
                h.apply = [controls] (ChannelDef& d) {
                    for (const auto& k : controls)
                    {
                        if (k.name == "Seq play")
                            k.set (d, 1.0f);
                        for (int i = 1; i <= 64; ++i)
                            if (k.name == "Seq step " + std::to_string (i))
                                k.set (d, (float) (26 + (i * 5) % 24) / 50.0f);
                    }
                };
                out.push_back (std::move (h));
            }
            // a modulation slot: LFO 1 on the cutoff (destination 1 + parameter 15) at +100. (A velocity source acts only
            // when its destination is next set: FUN_1800413f0 reads the slots in setParam, the LFOs every chunk)
            for (int k = 1; k <= 8; ++k)
            {
                const auto n = "Mod " + std::to_string (k);
                // (the source is set by apply, which a move of the source itself then overrides: its far end, CC 127,
                // is never sent, so it is heard against the LFO it replaces)
                Hint h { n + ": LFO 1 on the cutoff", {}, "pat:long" };
                h.apply = [controls, n] (ChannelDef& d) {
                    for (const auto& c : controls)
                        if (c.name == n + " source" || c.name == n + " destination" || c.name == n + " amount" || c.name == "Cutoff" || c.name == "LFO 1 speed")
                            c.set (d, c.name == n + " source" ? 2.0f : c.name == n + " destination" ? 16.0f : c.name == n + " amount" ? 1.0f : c.name == "Cutoff" ? 0.3f : 0.5f);
                };
                h.scope = n + " ";
                out.push_back (std::move (h));
            }
            // an operator on its own: its start phase (free run off), its envelope's decay (no sustain)
            for (int o = 1; o <= 6; ++o)
            {
                const auto n = "Osc " + std::to_string (o);
                // (a start phase is heard against another operator at the same pitch, both restarting theirs)
                const auto m = "Osc " + std::to_string (o % 6 + 1);
                named (n + " sounding with " + m + ", both restarting their phase", { { n + " on", 1.0f }, { n + " mix", 1.0f }, { n + " free run", 0.0f },
                          { m + " on", 1.0f }, { m + " mix", 1.0f }, { m + " free run", 0.0f }, { m + " coarse", 0.5f }, { n + " coarse", 0.5f }, { m + " fine", 0.5f }, { n + " fine", 0.5f } });
                named (n + " sounding, decaying to silence", { { n + " on", 1.0f }, { n + " mix", 1.0f }, { n + " attack", 0.0f }, { n + " sustain", 0.0f } }, "pat:long");
                out.back().scope = n + " "; // (the pair's hint stays open: it wakes the other operator's phase too)
            }
            named ("the filter envelope decaying onto a low cutoff", { { "Filter env amount", 1.0f }, { "Filter attack", 0.0f }, { "Filter sustain", 0.0f }, { "Cutoff", 0.2f } }, "pat:long");
            // (the LFOs restart while no voice sounds unless free: the release short enough for the gap to be silent)
            named ("LFO 1 on the cutoff, a note after a gap", { { "Filter LFO 1 amount", 1.0f }, { "LFO 1 speed", 0.5f }, { "Release", 0.0f } }, "pat:gap");
            named ("LFO 2 on the pitch, a note after a gap", { { "LFO 2 to pitch", 1.0f }, { "LFO 2 speed", 0.5f }, { "Release", 0.0f } }, "pat:gap");
            break;
        }
        case InstrumentKind::transistorbass:
        {
            // with the sequencer on, a note picks the program it plays (key = program, REA_rodada169g2; a chord: its last
            // key) and the page edits the state's program: play the edited one alone, its 16 steps gated and on rising notes so each field is heard
            auto byName = [controls] (ChannelDef& d, const std::string& name, float v) {
                for (const auto& k : controls)
                    if (k.name == name)
                        k.set (d, v);
            };
            auto ranged = [controls] (const std::string& name, float x) {
                for (const auto& k : controls)
                    if (k.name == name)
                        return k.low + x * (k.high - k.low);
                return 0.0f;
            };
            for (int variant = 0; variant < 3; ++variant)
            {
                Hint h { variant == 0 ? "the sequencer on, the note playing the edited program"
                                      : variant == 1 ? "the sequencer on, the edited program's 16 steps gated on rising notes"
                                                     : "the sequencer on, every step gated and accented",
                         {}, "pat:one+pat:long" };
                h.apply = [byName, ranged, variant] (ChannelDef& d) {
                    transistorBassSelectProgram (d.pluginState, 60);
                    byName (d, "Sequencer", 1.0f);
                    if (variant == 0)
                        return;
                    for (int i = 1; i <= 16; ++i)
                    {
                        const auto n = "Step " + std::to_string (i);
                        byName (d, n + " gate", 1.0f);
                        if (variant == 1)
                            byName (d, n + " note", ranged (n + " note", (float) (i % 12) / 11.0f * 0.09f));
                        else
                            byName (d, n + " accent", 1.0f);
                    }
                };
                out.push_back (std::move (h));
            }
            named ("the delay on, the sequencer off", { { "Sequencer", 0.0f }, { "Delay on", 1.0f } }, "pat:long");
            break;
        }
        case InstrumentKind::drumaxx:
        {
            // a pad's layers up and every key hit (the Default Kit's pads 11…16 sit outside 36…51), then retriggered and
            // with each velocity modulation on a target
            for (int pad = 1; pad <= 16; ++pad)
            {
                const auto n = "Pad " + std::to_string (pad);
                const std::vector<std::pair<std::string, float>> up { { n + " volume", 1.0f }, { n + " mute", 0.0f }, { n + " SOF level", 1.0f },
                                                                      { n + " Mid level", 1.0f }, { n + " High level", 1.0f }, { n + " Mallet amp", 1.0f } };
                const auto first = out.size();
                named (n + "'s layers up", up, "pat:allkeys");
                named (n + "'s layers up, hit again", up, "pat:allkeys+pat:retrig");
                // the MID band's envelope (its decay, the "Mid attack" knob) only moves its level / frequency modulation,
                // the LOW band's resonance only acts with the band on (frequency above 0), and the mallet noise's seed
                // only at a hit with pad 1's Noise Retrig on (the engine reads pad 1's for every pad, REA_rodada67 §6.2)
                auto more = up;
                for (const auto& kv : std::vector<std::pair<std::string, float>> { { n + " Mid level mod", 1.0f }, { n + " Mid freq mod", 1.0f }, { n + " Low freq", 0.5f },
                                                                                   { n + " Mallet noise", 1.0f }, { "Pad 1 Mallet retrig", 1.0f } })
                    more.push_back (kv);
                named (n + "'s layers up, its MID modulated, LOW on, the noise reseeded", more, "pat:allkeys");
                for (int j = 1; j <= 4; ++j)
                {
                    auto v = up;
                    v.push_back ({ n + " Vel mod " + std::to_string (j) + " target", 28.0f });
                    v.push_back ({ n + " Vel mod " + std::to_string (j) + " value", 1.0f });
                    named (n + "'s velocity modulation " + std::to_string (j) + " on its pitch", v, "pat:allkeys+pat:vel:0.2");
                }
                for (auto i = first; i < out.size(); ++i)
                    out[i].scope = n + " "; // (only this pad's controls)
            }
            named ("the master EQ on with bands raised", { { "EQ on", 1.0f }, { "EQ band 1", 1.0f }, { "EQ band 5", 1.0f } }, "pat:allkeys");
            named ("the master EQ on", { { "EQ on", 1.0f } }, "pat:allkeys");
            named ("the limiter on, hit hard", { { "Limiter on", 1.0f }, { "Master", 1.0f } }, "pat:allkeys+pat:vel:1");
            break;
        }
        case InstrumentKind::keplerExo:
            // Release mode: in mono, letting the first note go restarts the envelopes on the one still held (or not);
            // heard with the amplifier on envelope 1 and a slow attack
            named ("mono on envelope 1 with a slow attack, a second note over the first", { { "Mono", 1.0f }, { "VCA source", 0.0f }, { "Env1 attack", 0.5f } }, "pat:overlap");
            // Saturation post moves the saturation after the EQ, chorus, delay and reverb: heard with those on
            named ("the saturation on and driven", { { "Saturation on", 1.0f }, { "Saturation", 1.0f } });
            named ("the saturation on and driven, the reverb and chorus on", { { "Saturation on", 1.0f }, { "Saturation", 1.0f }, { "Reverb on", 1.0f }, { "Hyper chorus on", 1.0f } }, "pat:long");
            named ("the saturation on and driven, high notes", { { "Saturation on", 1.0f }, { "Saturation", 1.0f } }, "pat:high");
            named ("the amplifier on envelope 1, snapping", { { "VCA source", 0.0f }, { "Env1 attack", 0.0f }, { "Env1 release", 0.0f } }, "pat:gap");
            named ("the high-pass up", { { "High-pass", 1.0f } });
            break;
        case InstrumentKind::sytrus:
            // the modulation matrix (round 172a): FL's Default patch sends only operator 1 out; an operator's pan is heard
            // with it sent out, a filter's cells with an operator fed into it and the filter sent out
            for (int o = 3; o <= 6; ++o)
                named ("operator " + std::to_string (o) + " sent out", { { "Op " + std::to_string (o) + " out", 256.0f } });
            for (int f = 1; f <= 3; ++f)
            {
                const auto n = "filter " + std::to_string (f);
                named ("operator 1 through " + n + ", sent out", { { "FM op 1 to " + n, 256.0f }, { "Filter " + std::to_string (f) + " out", 256.0f } });
                named ("operator 2 through " + n + ", sent out", { { "FM op 2 to " + n, 256.0f }, { "Op 2 out", 256.0f }, { "Filter " + std::to_string (f) + " out", 256.0f } });
                named ("operator 1 through " + n + ", sent out and to the FX", { { "FM op 1 to " + n, 256.0f }, { "Filter " + std::to_string (f) + " out", 256.0f }, { "Filter " + std::to_string (f) + " FX", 256.0f } });
            }
            break;
        case InstrumentKind::poizone:
            // PoiZone's Bend range and MW amount / destination act through its own wheels (params 158 / 157, round 173d)
            named ("PoiZone's wheels up", { { "Mod wheel", 1.0f }, { "Pitch bend", 1.0f } });
            break;
        case InstrumentKind::granulizer:
        {
            // Tempo locked needs the sample's length in beats (an acid sample's beats or the channel's Time stretch
            // beats, Granulizer.cpp hostBeats); the measured sample has neither, so the lock has nothing to follow
            Hint h { "the sample's length set to 4 beats (Channel settings, Time)", {}, "pat:bpm" };
            h.apply = [] (ChannelDef& d) { d.sampler.stretchBeats = 4.0f; };
            out.push_back (std::move (h));
            break;
        }
        case InstrumentKind::morphine:
        {
            // a noise sample loaded (its name in the state and its file in the channel, as FL's Noise browser does):
            // the measured preset has none, so the noise section (mix, pan, mute, velocity) is silent without it
            Hint h { "a noise sample loaded (Ambient Hit 1.wav)", {}, {} };
            h.apply = [] (ChannelDef& d) {
                const auto a0 = morphineParamOffset (d.pluginState, 0); // (global A[0] = block byte 4, the name at 172)
                const std::string name = "Ambient Hit 1.wav";
                if (a0 < 0 || ctlaudit::flAccess == nullptr || (std::size_t) a0 - 4 + 172 + 1024 > d.pluginState.size())
                    return;
                const auto at = a0 - 4 + 172;
                std::fill (d.pluginState.begin() + at, d.pluginState.begin() + at + 1024, (std::uint8_t) 0);
                std::copy (name.begin(), name.end(), d.pluginState.begin() + at);
                d.samplePath = ctlaudit::flAccess->roots.factoryData + "/Plugins/Fruity/Generators/Morphine/Noise Samples/" + name;
            };
            out.push_back (std::move (h));
            break;
        }
        case InstrumentKind::pocketSuperSaw:
            // (round 190cm) FLM's defaults leave most sections off: one oscillator (no stack to detune or spread), no
            // pulse, no SuperSync, the filter wide open with its envelope at 0, sustains at full, no slide time, the
            // LFO, arpeggiator, delay and mod wheel at 0 (PocketSuperSaw.cpp, docs/analise/REA_rodada190by.md §2…3)
            named ("pulse mode on", { { "Osc pulse mode", 1.0f } });
            named ("a stack of 12 oscillators", { { "Osc unison", 1.0f } });
            named ("a stack of 12 oscillators restarting their phases", { { "Osc unison", 1.0f }, { "Osc re-trig phases", 1.0f } });
            named ("SuperSync on, its envelope at full amount decaying into a release",
                   { { "SuperSync mode", 1.0f }, { "Osc sync", 0.2f }, { "Osc sync EG amount", 1.0f }, { "Osc sync decay", 0.4f }, { "Osc sync sustain", 0.3f }, { "Level EG release", 0.6f } });
            named ("the filter closed to 30 %, its envelope at full amount decaying into a release",
                   { { "Filter cutoff", 0.3f }, { "Filter EG amount", 1.0f }, { "Filter EG decay", 0.4f }, { "Filter EG sustain", 0.2f }, { "Level EG release", 0.6f } });
            named ("the level envelope's sustain at 30 %", { { "Level EG sustain", 0.3f } });
            // (Slide held mode: heard on a note played after the last one was let go, pat:gap; Static: on any glide)
            named ("a slide time of 160 ms", { { "Slide time", 0.8f } });
            named ("the LFO at full amount and 4 Hz, the filter half closed", { { "LFO amount", 1.0f }, { "LFO rate", 0.8f }, { "Filter cutoff", 0.5f } });
            named ("the LFO at full amount and tempo-synced, the filter half closed", { { "LFO amount", 1.0f }, { "LFO mode", 1.0f }, { "Filter cutoff", 0.5f } });
            // (Arp range: heard on one key alone, pat:one, whose second step is the octave above)
            named ("the arpeggiator going up", { { "Arp mode", 0.3f } });
            named ("the delay mixed in", { { "Delay mix", 0.7f } });
            named ("the mod wheel at full on a negative amount", { { "Mod wheel amount", 0.25f } }, "pat:mod");
            break;
        case InstrumentKind::tetra:
        {
            // (round 190ix) Tetra's new channel plays operator A alone (B…D off, at the −70 dB floor), with the LFO, the pitch
            // envelope and the filter off and every envelope a click attack onto a full sustain (core/FmSynth.cpp defaults):
            // a section's controls are heard with it on. Operator's laws keep some of them context-bound: an operator's
            // feedback acts only where nothing modulates it, a fixed frequency only with Fixed on, a loop time / repeat
            // only in its loop mode, a band-pass circuit only on a band / notch / morph filter.
            const char* const ops[] { "A", "B", "C", "D" };
            const char* const envs[] { "A Env", "B Env", "C Env", "D Env", "LFO Env", "Pitch Env", "Filter Env" };
            std::vector<std::pair<std::string, float>> on;
            for (const auto* o : ops)
            {
                on.push_back ({ std::string (o) + " On", 1.0f });
                on.push_back ({ std::string (o) + " Level", 0.5f });
            }
            auto with = [&on] (std::vector<std::pair<std::string, float>> more) {
                auto v = on;
                v.insert (v.end(), more.begin(), more.end());
                return v;
            };
            // every envelope slow and shaped (levels from a start to a sustain and on to an end), in a loop mode
            auto shaped = [&] (float loopMode) {
                std::vector<std::pair<std::string, float>> v;
                for (const auto* e : envs)
                {
                    const std::string n = e;
                    const bool pitch = n == "Pitch Env";
                    for (const auto& [what, x] : std::vector<std::pair<const char*, float>> {
                             { " Attack", 150.0f }, { " Initial", pitch ? 12.0f : 0.5f }, { " Decay", 150.0f }, { " Peak", pitch ? -12.0f : 1.0f },
                             { " Sustain", pitch ? 7.0f : 0.3f }, { " Release", 150.0f }, { " End", pitch ? -7.0f : 0.2f }, { " Loop Mode", loopMode } })
                        v.push_back ({ n + what, x });
                }
                for (const auto* o : ops)
                {
                    v.push_back ({ std::string (o) + " Pitch Env On", 1.0f });
                    v.push_back ({ std::string (o) + " LFO On", 1.0f });
                }
                for (const auto& kv : std::vector<std::pair<std::string, float>> { { "Pitch Env On", 1.0f }, { "LFO On", 1.0f }, { "LFO Amount", 0.5f },
                                                                                   { "Filter On", 1.0f }, { "Filter Freq", 800.0f }, { "Filter Env Amount", 36.0f },
                                                                                   { "LFO Filter On", 1.0f } })
                    v.push_back (kv);
                return with (v);
            };
            named ("every operator on at half level, in a chain", on);
            named ("every operator on at half level and heard (algorithm 11)", with ({ { "Algorithm", 10.0f } }));
            named ("every operator on at half level, at a fixed frequency", with ({ { "A Fixed", 1.0f }, { "B Fixed", 1.0f }, { "C Fixed", 1.0f }, { "D Fixed", 1.0f } }));
            named ("every operator on, the chord hit again", with ({ { "A Phase", 25.0f }, { "B Phase", 25.0f }, { "C Phase", 25.0f }, { "D Phase", 25.0f } }), "pat:retrig");
            named ("every operator on, the LFO at full on each", with ({ { "LFO On", 1.0f }, { "LFO Amount", 1.0f }, { "A LFO On", 1.0f }, { "B LFO On", 1.0f }, { "C LFO On", 1.0f }, { "D LFO On", 1.0f } }));
            named ("every operator on, the LFO at full on each, tempo-synced", with ({ { "LFO On", 1.0f }, { "LFO Amount", 1.0f }, { "LFO Range", 2.0f } }));
            named ("every operator on, the LFO at full on each, a note after a gap", with ({ { "LFO On", 1.0f }, { "LFO Amount", 1.0f }, { "LFO Rate", 40.0f } }), "pat:gap");
            named ("the LFO at full on the filter", { { "LFO On", 1.0f }, { "LFO Amount", 1.0f }, { "LFO Filter On", 1.0f }, { "Filter On", 1.0f }, { "Filter Freq", 1000.0f } });
            named ("every operator on, the pitch envelope on each from an octave up",
                   with ({ { "Pitch Env On", 1.0f }, { "Pitch Env Initial", 12.0f }, { "Pitch Env Attack", 150.0f }, { "A Pitch Env On", 1.0f }, { "B Pitch Env On", 1.0f },
                           { "C Pitch Env On", 1.0f }, { "D Pitch Env On", 1.0f } }));
            named ("every section on, every envelope slow and shaped", shaped (0.0f));
            named ("every section on, every envelope slow and shaped, looping", shaped (1.0f), "pat:long");
            named ("every section on, every envelope slow and shaped, in Beat mode", shaped (2.0f), "pat:long");
            named ("the filter on as a band pass", { { "Filter On", 1.0f }, { "Filter Type", 2.0f }, { "Filter Freq", 1000.0f }, { "Filter Res", 0.8f } });
            named ("the filter on as a morph filter", { { "Filter On", 1.0f }, { "Filter Type", 4.0f }, { "Filter Freq", 1000.0f }, { "Filter Res", 0.8f } });
            named ("the filter on at 1 kHz, its envelope at full", { { "Filter On", 1.0f }, { "Filter Freq", 1000.0f }, { "Filter Env Amount", 72.0f }, { "Filter Env Attack", 150.0f } });
            named ("the filter on at 1 kHz, its morph at the high pass", { { "Filter On", 1.0f }, { "Filter Freq", 1000.0f }, { "Filter Morph", 0.5f } });
            named ("the filter on at 1 kHz, a driven circuit", { { "Filter On", 1.0f }, { "Filter Freq", 1000.0f }, { "Filter Circuit", 1.0f }, { "Filter Res", 0.5f } });
            named ("the filter on at 1 kHz with the shaper", { { "Filter On", 1.0f }, { "Filter Freq", 1000.0f }, { "Shaper", 1.0f }, { "Shaper Drive", 12.0f } });
            named ("glide on, a legato second note", { { "Glide", 1.0f } }, "pat:mono");
            {
                // the user wave (no editor knob: the wave editor's 64 partials) as a saw, heard where an operator picks it
                named ("every operator on, its user wave a saw", on);
                out.back().apply = [] (ChannelDef& d) {
                    if ((int) d.modelParams.size() < fm::stateSize)
                    {
                        const auto def = tetraDefaults();
                        const auto have = d.modelParams.size();
                        d.modelParams.resize ((std::size_t) fm::stateSize);
                        std::copy (def.begin() + (std::ptrdiff_t) have, def.end(), d.modelParams.begin() + (std::ptrdiff_t) have);
                    }
                    for (int k = 0; k < fm::opCount; ++k)
                        for (int i = 0; i < fm::harmonics; ++i)
                            d.modelParams[(std::size_t) (fm::harmonicsBase + k * fm::harmonics + i)] = 1.0f / (float) (i + 1);
                };
            }
            break;
        }
        case InstrumentKind::helix:
        {
            // (rollup 0.1.30, round 190jk) Helix's new channel is Live's default Wavetable (core/WavetableSynth.cpp
            // defaults): oscillator 1 alone through filter 1 wide open, the sub and filter 2 off, an empty matrix apart from
            // the MIDI rows, so envelopes 2 / 3 and the LFOs move nothing until a matrix cell routes them; the effects'
            // modes are heard with their amounts up, a band-pass circuit on a band pass, Morph on the morph type, Drive on
            // a driven circuit, Unison's voices / amount with a unison mode, Glide on a Mono voice's legato note
            const auto routed = [] (std::vector<std::pair<int, float>> cells) {
                return [cells] (ChannelDef& d) {
                    if ((int) d.modelParams.size() < wt::stateSize)
                    {
                        const auto def = helixDefaults();
                        const auto have = d.modelParams.size();
                        d.modelParams.resize ((std::size_t) wt::stateSize);
                        std::copy (def.begin() + (std::ptrdiff_t) have, def.end(), d.modelParams.begin() + (std::ptrdiff_t) have);
                    }
                    for (const auto& [cell, v] : cells)
                        d.modelParams[(std::size_t) cell] = v;
                };
            };
            using namespace wt;
            named ("oscillator 2 on, a fifth up", { { "Osc 2 On", 1.0f }, { "Osc 2 Transpose", 7.0f }, { "Osc 2 Gain", 0.7f } });
            named ("both oscillators' effects at half, oscillator 2 on", { { "Osc 1 Effect 1", 0.5f }, { "Osc 1 Effect 2", 0.5f }, { "Osc 2 On", 1.0f },
                                                                           { "Osc 2 Effect Mode", 3.0f }, { "Osc 2 Effect 1", 0.5f }, { "Osc 2 Effect 2", 0.5f } });
            named ("the sub on", { { "Sub On", 1.0f }, { "Sub Gain", 0.8f }, { "Sub Tone", 0.5f } });
            for (int k = 1; k <= 2; ++k)
            {
                const auto f = "Filter " + std::to_string (k);
                std::vector<std::pair<std::string, float>> at1k { { f + " On", 1.0f }, { f + " Freq", 1000.0f }, { f + " Res", 0.8f } };
                auto plus = [&at1k] (std::vector<std::pair<std::string, float>> more) {
                    auto v = at1k;
                    v.insert (v.end(), more.begin(), more.end());
                    return v;
                };
                named ("filter " + std::to_string (k) + " on at 1 kHz, resonant", at1k);
                named ("filter " + std::to_string (k) + " on at 1 kHz as a morph filter", plus ({ { f + " Type", 4.0f } }));
                named ("filter " + std::to_string (k) + " on at 1 kHz as a band pass", plus ({ { f + " Type", 2.0f } }));
                named ("filter " + std::to_string (k) + " on at 1 kHz, a driven circuit", plus ({ { f + " Circuit", 1.0f }, { f + " Drive", 12.0f } }));
            }
            named ("both filters on at 1 kHz, filter 2 a high pass", { { "Filter 1 Freq", 1000.0f }, { "Filter 1 Res", 0.8f }, { "Filter 2 On", 1.0f },
                                                                       { "Filter 2 Type", 1.0f }, { "Filter 2 Freq", 1000.0f }, { "Filter 2 Res", 0.8f } });
            // envelopes 2 and 3 slow and shaped, routed to oscillator 1's pitch and filter 1 (at 1 kHz); looping on held notes
            for (int k = 2; k <= 3; ++k)
            {
                const auto e = "Env " + std::to_string (k);
                const auto source = k == 2 ? src::env2 : src::env3;
                const std::vector<std::pair<std::string, float>> shaped { { e + " Attack", 0.15f }, { e + " Decay", 0.15f }, { e + " Release", 0.2f },
                                                                          { e + " Sustain", 0.3f }, { e + " Initial", 0.2f }, { e + " Final", 0.5f },
                                                                          { "Filter 1 Freq", 1000.0f }, { "Filter 1 Res", 0.5f } };
                const auto cells = routed ({ { matrixIndex (tgt::oscPitch, source), 0.3f }, { matrixIndex (tgt::filterFreq, source), 0.5f } });
                named (e + " slow and shaped, on oscillator 1's pitch and filter 1", shaped);
                out.back().apply = cells;
                auto looping = shaped;
                looping.push_back ({ e + " Loop Mode", 2.0f });
                named (e + " slow and shaped, on oscillator 1's pitch and filter 1, looping", looping, "pat:long");
                out.back().apply = cells;
            }
            // the LFOs on oscillator 1's pitch and filter 1: free at 4 Hz, tempo-synced, after a gap
            for (int k = 1; k <= 2; ++k)
            {
                const auto l = "LFO " + std::to_string (k);
                const auto source = k == 1 ? src::lfo1 : src::lfo2;
                const auto cells = routed ({ { matrixIndex (tgt::oscPitch, source), 0.3f }, { matrixIndex (tgt::filterFreq, source), 0.5f } });
                const std::vector<std::pair<std::string, float>> at4 { { l + " Rate", 4.0f }, { l + " Attack", 0.0f }, { "Filter 1 Freq", 1000.0f } };
                named (l + " at 4 Hz on oscillator 1's pitch and filter 1", at4);
                out.back().apply = cells;
                named (l + " tempo-synced on oscillator 1's pitch and filter 1", { { l + " Sync", 1.0f }, { "Filter 1 Freq", 1000.0f } });
                out.back().apply = cells;
                named (l + " at 4 Hz on oscillator 1's pitch, a note after a gap", at4, "pat:gap");
                out.back().apply = cells;
                named (l + " at 4 Hz on oscillator 1's pitch, its attack slow", { { l + " Rate", 4.0f }, { l + " Attack", 0.3f } });
                out.back().apply = cells;
            }
            named ("unison on (Classic)", { { "Unison Mode", 1.0f }, { "Unison Amount", 0.5f } });
            named ("mono, a legato second note", { { "Poly", 0.0f }, { "Glide", 0.3f } }, "pat:mono");
            named ("the amp envelope shaped, held long", { { "Amp Decay", 0.15f }, { "Amp Sustain", 0.3f }, { "Amp Loop Mode", 2.0f } }, "pat:long");
            // (round 190mi) Helix+: a second warp needs its amount (and a mode), noise its switch, Unison+ a unison mode,
            // the macros and drawn LFOs a matrix cell, the effects' knobs their effect on
            named ("Helix+: both second warps Bend + at half, oscillator 2 on", { { "Osc 1 Warp 2", (float) warp::bendPlus }, { "Osc 1 Warp 2 Amount", 0.5f },
                                                                              { "Osc 2 On", 1.0f }, { "Osc 2 Gain", 0.7f },
                                                                              { "Osc 2 Warp 2", (float) warp::bendPlus }, { "Osc 2 Warp 2 Amount", 0.5f } });
            named ("Helix+: noise on, darker", { { "Noise On", 1.0f }, { "Noise Level", 0.5f }, { "Noise Color", 0.3f } });
            named ("Helix+: unison Classic, 6 voices", { { "Unison Mode", 1.0f }, { "Unison Amount", 0.5f }, { "Unison Voices", 6.0f } });
            named ("Helix+: filter 2 on at 1 kHz, resonant", { { "Filter 2 On", 1.0f }, { "Filter 2 Freq", 1000.0f }, { "Filter 2 Res", 0.8f } });
            named ("Helix+: the four macros on oscillator 1's pitch", { { "Filter 1 Freq", 1000.0f } });
            out.back().apply = routed ({ { cellIndex (tgt::oscPitch, src::macro1), 0.2f }, { cellIndex (tgt::oscPitch, src::macro1 + 1), 0.3f },
                                         { cellIndex (tgt::oscPitch, src::macro1 + 2), 0.4f }, { cellIndex (tgt::oscPitch, src::macro1 + 3), 0.5f } });
            for (int k = 1; k <= 2; ++k)
            {
                const auto l = "LFO " + std::to_string (k);
                named ("Helix+: " + l + " at 4 Hz on oscillator 1's pitch", { { l + " Rate", 4.0f }, { l + " Attack", 0.0f } });
                out.back().apply = routed ({ { matrixIndex (tgt::oscPitch, k == 1 ? src::lfo1 : src::lfo2), 0.3f } });
            }
            named ("Helix+: distortion on", { { "Distortion On", 1.0f }, { "Distortion Drive", 0.5f }, { "Distortion Mix", 0.7f } });
            named ("Helix+: chorus on", { { "Chorus On", 1.0f } });
            named ("Helix+: delay on", { { "Delay On", 1.0f }, { "Delay Time", 3.0f }, { "Delay Mix", 0.5f } });
            named ("Helix+: reverb on", { { "Reverb On", 1.0f } });
            // (round 190ml) Helix+ 2: oscillator 3 and the sample need switching on (the sample a sample in the channel's
            // data), their effects / second warps an amount and a mode, macros 5…8 and the chaos sources a matrix cell, the
            // shifter / EQ / multiband their switch (the EQ its bands away from 0 dB)
            named ("Helix+ 2: oscillator 3 on, a fifth up", { { "Osc 3 On", 1.0f }, { "Osc 3 Gain", 0.7f }, { "Osc 3 Transpose", 7.0f } });
            named ("Helix+ 2: oscillator 3's effect at half", { { "Osc 3 On", 1.0f }, { "Osc 3 Gain", 0.7f }, { "Osc 3 Effect Mode", 3.0f },
                                                               { "Osc 3 Effect 1", 0.5f }, { "Osc 3 Effect 2", 0.5f } });
            named ("Helix+ 2: oscillator 3's second warp Bend + at half", { { "Osc 3 On", 1.0f }, { "Osc 3 Gain", 0.7f },
                                                                           { "Osc 3 Warp 2", (float) warp::bendPlus }, { "Osc 3 Warp 2 Amount", 0.5f } });
            const auto withSample = [] (ChannelDef& d) {
                if ((int) d.modelParams.size() < wt::stateSize)
                {
                    const auto def = helixDefaults();
                    const auto have = d.modelParams.size();
                    d.modelParams.resize ((std::size_t) wt::stateSize);
                    std::copy (def.begin() + (std::ptrdiff_t) have, def.end(), d.modelParams.begin() + (std::ptrdiff_t) have);
                }
                wt::HelixUserData data; // (a 0.3 s tone with a falling pitch: Start, Loop and the pitch all heard)
                data.sample.rate = 44100.0;
                data.sample.l.resize (13230);
                double ph = 0.0;
                for (std::size_t i = 0; i < data.sample.l.size(); ++i)
                {
                    ph += (330.0 - 200.0 * (double) i / 13230.0) / 44100.0;
                    data.sample.l[i] = (float) (0.7 * std::sin (2.0 * 3.14159265358979 * ph));
                }
                d.pluginState = wt::encodeUserData (data);
            };
            named ("Helix+ 2: the sample on (a falling tone)", { { "Sample On", 1.0f } });
            out.back().apply = withSample;
            named ("Helix+ 2: the sample's second warp Sync at half", { { "Sample On", 1.0f }, { "Sample Warp 2", (float) warp::sync },
                                                                       { "Sample Warp 2 Amount", 0.5f } });
            out.back().apply = withSample;
            named ("Helix+ 2: macros 5 to 8 on oscillator 1's pitch", { { "Filter 1 Freq", 1000.0f } });
            out.back().apply = routed ({ { cellIndex (tgt::oscPitch, src::macro1 + 4), 0.2f }, { cellIndex (tgt::oscPitch, src::macro1 + 5), 0.3f },
                                         { cellIndex (tgt::oscPitch, src::macro1 + 6), 0.4f }, { cellIndex (tgt::oscPitch, src::macro1 + 7), 0.5f } });
            for (int k = 1; k <= chaosCount; ++k)
            {
                const auto c = "Chaos " + std::to_string (k);
                named ("Helix+ 2: " + c + " at 4 Hz on oscillator 1's pitch", { { c + " Rate", 4.0f }, { c + " Amount", 1.0f } });
                out.back().apply = routed ({ { cellIndex (tgt::oscPitch, src::chaos1 + k - 1), 0.3f } });
            }
            named ("Helix+ 2: the shifter on", { { "Shift On", 1.0f }, { "Shift", 0.3f }, { "Shift Mix", 1.0f } });
            named ("Helix+ 2: the EQ on, its bands moved", { { "EQ On", 1.0f }, { "EQ Low", 6.0f }, { "EQ Mid", -6.0f }, { "EQ High", 6.0f } });
            named ("Helix+ 2: the multiband on", { { "Multiband On", 1.0f } });
            // (round 190mt) Helix round 3: the granular and spectral oscillators need switching on and a sample in the
            // channel's data (the spectral one a rich one, its band narrowed and few partials, so Band, Width and Partials
            // are heard); Clean a rich sample played an octave up
            const auto withRich = [] (ChannelDef& d) {
                if ((int) d.modelParams.size() < wt::stateSize)
                {
                    const auto def = helixDefaults();
                    const auto have = d.modelParams.size();
                    d.modelParams.resize ((std::size_t) wt::stateSize);
                    std::copy (def.begin() + (std::ptrdiff_t) have, def.end(), d.modelParams.begin() + (std::ptrdiff_t) have);
                }
                wt::HelixUserData data; // (0.4 s of a falling tone with 30 harmonics)
                data.sample.rate = 44100.0;
                data.sample.l.resize (17640);
                double ph = 0.0;
                for (std::size_t i = 0; i < data.sample.l.size(); ++i)
                {
                    ph += (330.0 - 150.0 * (double) i / 17640.0) / 44100.0;
                    double y = 0.0;
                    for (int h = 1; h <= 30; ++h)
                        y += std::sin (2.0 * 3.14159265358979 * h * ph) / h;
                    data.sample.l[i] = (float) (0.4 * y);
                }
                d.pluginState = wt::encodeUserData (data);
            };
            named ("Helix 3: the grains on (a falling tone)", { { "Grain On", 1.0f } });
            out.back().apply = withSample;
            named ("Helix 3: the spectral oscillator on (a rich falling tone), a band of four partials",
                   { { "Spectral On", 1.0f }, { "Spectral Width", 2.0f }, { "Spectral Partials", 4.0f } });
            out.back().apply = withRich;
            named ("Helix 3: the sample on an octave up (a rich falling tone)", { { "Sample On", 1.0f }, { "Sample Transpose", 12.0f } });
            out.back().apply = withRich;
            break;
        }
        case InstrumentKind::braid:
        {
            // (rollup 0.1.30 step 7, round 190lc) Braid's new channel is Live's default Meld (core/BraidParams.cpp): both
            // engines Basic Shapes as a sine (Shape 0, so Tone has nothing to colour), filters wide open, amp sustain at
            // full (Decay / its slope / Loop unheard), and a matrix that routes only the bend, MPE pitch, velocity and the
            // voice spread, so each engine's mod envelope, LFO 1 (with its two FX) and LFO 2 move nothing until a matrix
            // cell routes them; glide wants a glide time and a mono legato note, Legato a mono patch, Link Envelopes an
            // engine B whose own amp envelope differs, Osc Reset a chord hit again while it sounds
            const auto routed = [] (std::vector<std::pair<int, float>> cells) {
                return [cells] (ChannelDef& d) {
                    if ((int) d.modelParams.size() < braid::stateSize)
                    {
                        const auto def = braidDefaults();
                        const auto have = d.modelParams.size();
                        d.modelParams.resize ((std::size_t) braid::stateSize);
                        std::copy (def.begin() + (std::ptrdiff_t) have, def.end(), d.modelParams.begin() + (std::ptrdiff_t) have);
                    }
                    for (const auto& [cell, v] : cells)
                        d.modelParams[(std::size_t) cell] = v;
                };
            };
            using namespace braid;
            for (int k = 0; k < 2; ++k)
            {
                const std::string e = k == 0 ? "A " : "B ";
                const auto t = k * tgt::perEngine;
                const auto modEnv = k == 0 ? src::aModEnv : src::bModEnv, lfo1 = k == 0 ? src::aLfo1 : src::bLfo1,
                           lfo1Fx = k == 0 ? src::aLfo1Fx : src::bLfo1Fx, lfo2 = k == 0 ? src::aLfo2 : src::bLfo2;
                // (Basic Shapes' second macro is the pulse width: heard near the square end of Shape)
                named (e + "shaped near a square, its filter at 1 kHz", { { e + "Osc Macro 1", 0.9f }, { e + "Filter Freq", 1000.0f },
                                                                          { e + "Filter Macro 1", 0.5f } });
                // (a mono, non-legato voice restarts on the same voice: its phase reset is heard)
                named (e + "shaped as a saw, mono, not legato, the chord hit again", { { e + "Osc Macro 1", 0.6f }, { "Poly", 0.0f }, { "Legato", 0.0f } },
                       "pat:retrig");
                const std::vector<std::pair<std::string, float>> amp { { e + "Amp Decay", 0.4f }, { e + "Amp Sustain", 0.3f }, { e + "Amp Attack", 0.05f } };
                named (e + "amp envelope decaying to 0.3", amp);
                auto ampLoop = amp;
                ampLoop.push_back ({ e + "Amp Loop", 1.0f });
                named (e + "amp envelope decaying to 0.3, held long", ampLoop, "pat:long");
                // the mod envelope slow and shaped, on the engine's filter (at 1 kHz) and pitch
                const std::vector<std::pair<std::string, float>> mod { { e + "Filter Freq", 1000.0f }, { e + "Mod Attack", 0.15f }, { e + "Mod Decay", 0.3f },
                                                                       { e + "Mod Release", 0.3f }, { e + "Mod Initial", 0.1f }, { e + "Mod Peak", 1.0f },
                                                                       { e + "Mod Sustain", 0.3f }, { e + "Mod Final", 0.2f } };
                const auto modCells = routed ({ { matrixIndex (t + tgt::filterFreq, modEnv), 0.6f }, { matrixIndex (t + tgt::pitch, modEnv), 0.3f } });
                named (e + "mod envelope slow and shaped, on its filter and pitch", mod);
                out.back().apply = modCells;
                auto modLoop = mod;
                modLoop.push_back ({ e + "Mod Loop", 1.0f });
                named (e + "mod envelope slow and shaped, on its filter and pitch, looping", modLoop, "pat:long");
                out.back().apply = modCells;
                // LFO 1 (and its FX output) and LFO 2 on the engine's filter and pitch: free at 4 Hz, tempo-synced, after a gap
                const auto lfoCells = routed ({ { matrixIndex (t + tgt::filterFreq, lfo1), 0.5f }, { matrixIndex (t + tgt::pitch, lfo1Fx), 0.3f },
                                                { matrixIndex (t + tgt::detune, lfo2), 0.8f }, { matrixIndex (t + tgt::filterMacro1, lfo2), 0.5f } });
                // (LFO 1 near a square, so its pulse width counts; its second FX a ramp up instead of the neutral Bipolar;
                // LFO 2 retriggered, so its phase counts after a gap; the free rates off the synced ones' 4 Hz at 120 BPM)
                const std::vector<std::pair<std::string, float>> lfos { { e + "Filter Freq", 1000.0f }, { e + "Filter Macro 1", 0.4f }, { e + "LFO 1 Rate", 3.0f },
                                                                        { e + "LFO 1 Macro 1", 0.9f }, { e + "LFO 1 FX 1 Macro", 0.5f },
                                                                        { e + "LFO 1 FX 2", 15.0f }, { e + "LFO 1 FX 2 Macro", 0.5f },
                                                                        { e + "LFO 2 Rate", 3.0f }, { e + "LFO 2 Retrigger", 1.0f } };
                named (e + "LFO 1 / its FX / LFO 2 at 4 Hz on its filter and pitch", lfos);
                out.back().apply = lfoCells;
                auto synced = lfos;
                synced.push_back ({ e + "LFO 1 Sync", 1.0f });
                synced.push_back ({ e + "LFO 2 Sync", 1.0f });
                named (e + "LFO 1 / its FX / LFO 2 tempo-synced on its filter and pitch", synced);
                out.back().apply = lfoCells;
                named (e + "LFO 1 / its FX / LFO 2 at 4 Hz on its filter and pitch, a note after a gap", lfos, "pat:gap");
                out.back().apply = lfoCells;
                named (e + "glide over a legato second note", { { "Poly", 0.0f }, { e + "Glide Time", 0.3f } }, "pat:mono");
            }
            named ("mono, a second note over the first, a slow attack", { { "Poly", 0.0f }, { "A Amp Attack", 0.2f }, { "B Amp Attack", 0.2f } }, "pat:mono");
            named ("engine B's own amp envelope slow", { { "B Amp Attack", 0.5f }, { "B Amp Sustain", 0.3f }, { "B Amp Decay", 0.3f } });
            break;
        }
        default:
            break;
    }
    (void) named;
    return out;
}

inline std::vector<std::string> split (const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::stringstream ss (s);
    std::string part;
    while (std::getline (ss, part, sep))
        out.push_back (part);
    return out;
}

// the context of `key`: the channel and the pattern; false when the key doesn't apply
inline bool context (const std::string& key, const ChannelDef& c, const std::vector<Control>& controls, const std::vector<Hint>& hints,
                     int target, ChannelDef& ctx, Pattern& p, std::string* note = nullptr)
{
    ctx = c;
    p = ctlaudit::patternFor (c.instrument);
    std::string text;
    for (const auto& part : split (key, '+'))
    {
        const auto f = split (part, ':');
        if (f.empty())
            return false;
        if (f[0] == "pat" && f.size() >= 2)
        {
            if (f[1] == "mono") p.keys = { 60 }, p.keys2 = { 64 }, text += "a legato second note";
            else if (f[1] == "bend") p.bend = 2.0f, p.channelPitch = 200.0f, text += "the pitch wheel / PITCH knob up 2 semitones";
            else if (f[1] == "mod") p.modWheel = 1.0f, text += "the mod wheel at full";
            else if (f[1] == "vel" && f.size() >= 3) p.velocity = (float) std::atof (f[2].c_str()), text += "velocity " + f[2];
            else if (f[1] == "rel") p.releaseVelocity = 0.1f, text += "a note-off velocity of 0.1";
            else if (f[1] == "long") p.heldBlocks = 300, p.releaseBlocks = 60, text += "the notes held 3.5 s";
            else if (f[1] == "gap") p.keys = { 60 }, p.keys2 = { 64 }, p.firstOffBlock = 6, p.secondBlock = 24, text += "a note, a gap, then another note";
            else if (f[1] == "offline") p.offline = true, text += "rendered as an export (offline)";
            else if (f[1] == "bpm") p.bpm = 97.0, text += "a tempo of 97 BPM (tempo-synced times differ from free ones at 120)";
            else if (f[1] == "retrig") p.keys2 = p.keys, p.secondBlock = 4, text += "the chord hit again while it sounds";
            else if (f[1] == "pads")
            {
                p.keys.clear();
                for (int k = 36; k <= 51; ++k)
                    p.keys.push_back (k);
                p.keys2 = p.keys, text += "every pad (keys 36…51)";
            }
            else if (f[1] == "allkeys")
            {
                p.keys.clear();
                for (int k = 0; k < 128; ++k)
                    p.keys.push_back (k);
                p.keys2 = p.keys, text += "every key (0…127: a drum engine's pads wherever they sit)";
            }
            else if (f[1] == "overlap") p.keys = { 60 }, p.keys2 = { 64 }, p.secondBlock = 6, p.firstOffBlock = 24, text += "a second note over the first, the first let go";
            else if (f[1] == "one") p.keys = { 60 }, p.keys2.clear(), text += "one key (60) alone";
            else if (f[1] == "vlong") p.heldBlocks = 1100, p.releaseBlocks = 60, text += "the notes held 13 s";
            else if (f[1] == "low" || f[1] == "high")
            {
                const int d = f[1] == "low" ? -24 : 24;
                for (auto& k : p.keys) k += d;
                for (auto& k : p.keys2) k += d;
                text += f[1] == "low" ? "the chord two octaves down" : "the chord two octaves up";
            }
            else
                return false;
        }
        else if (f[0] == "set" && f.size() >= 3)
        {
            const int i = std::atoi (f[1].c_str());
            if (i < 0 || i >= (int) controls.size() || i == target)
                return false;
            const auto v = (float) std::atof (f[2].c_str());
            if (! controls[(std::size_t) i].set (ctx, v))
                return false;
            std::ostringstream o;
            o << controls[(std::size_t) i].name << " = " << v;
            text += o.str();
        }
        else if (f[0] == "hint" && f.size() >= 2)
        {
            const int i = findHint (hints, f[1]);
            if (i < 0)
                return false;
            for (const auto& [index, v] : hints[(std::size_t) i].sets)
                for (std::size_t j = 0; j < controls.size(); ++j)
                    if (controls[j].index == index && (int) j != target)
                        controls[j].set (ctx, v);
            if (hints[(std::size_t) i].apply)
                hints[(std::size_t) i].apply (ctx);
            text += hints[(std::size_t) i].note;
        }
        else if (f[0] == "rand" && f.size() >= 2)
        {
            std::uint32_t seed = 1730u + (std::uint32_t) std::atoi (f[1].c_str()) * 7919u;
            auto rnd = [&seed] {
                seed = seed * 1664525u + 1013904223u;
                return (float) ((seed >> 8) & 0xffffff) / 16777216.0f;
            };
            for (std::size_t j = 0; j < controls.size(); ++j)
            {
                const auto& k = controls[j];
                // (round 190mc) Transistor Bass's program row picks WHICH program / patch / sequence the other controls
                // edit (FL's PROGRAM, AF, SEQUENCE, TRANSP., PATCH): a random setting keeps them, and draws no number for
                // them, so the other controls get the values they had before the row existed
                if (ctx.instrument == InstrumentKind::transistorbass && k.index >= tbprog::program && k.index <= tbprog::patch)
                    continue;
                std::string lower = k.name;
                std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
                const auto r = rnd();
                if ((int) j == target || lower.find ("master") != std::string::npos || lower == "volume" || lower == "output"
                    || lower.find ("mute") != std::string::npos)
                    continue;
                k.set (ctx, k.low + r * (k.high - k.low));
            }
            text += "random setting " + f[1];
        }
        else
            return false;
        text += ", ";
    }
    if (text.size() >= 2)
        text.resize (text.size() - 2);
    if (note != nullptr)
        *note = text;
    return true;
}

// the values a control is tried at from its current one: the far end, and the near end when the current value is inside
inline std::vector<float> valuesFrom (const Control& k, float v)
{
    std::vector<float> out { ctlaudit::flipped (k, v) };
    const auto other = out[0] == k.low ? k.high : k.low;
    if (std::abs (other - v) > 1e-6f * std::max (1.0f, std::abs (k.high - k.low)))
        out.push_back (other);
    // a phase's two ends are the same angle: half a cycle away too (the effects' rule, round 173d). Round 190ix: "Phase"
    // too, and Tetra's Filter Morph, whose two ends are both the low pass (Operator's low → band → high → notch → low)
    const auto endsWith = [&k] (const char* tail) {
        const auto n = std::strlen (tail);
        return k.name.size() >= n && k.name.compare (k.name.size() - n, n, tail) == 0;
    };
    // (rollup 0.1.30: Helix's "Filter 1 Morph" / "Filter 2 Morph", the same cyclic morph filter as Tetra's)
    const auto helixMorph = k.name.rfind ("Filter ", 0) == 0 && endsWith (" Morph");
    if (endsWith ("phase") || endsWith ("Phase") || endsWith ("Filter Morph") || helixMorph)
        out.push_back (0.5f * (k.low + k.high) + (v < 0.5f * (k.low + k.high) ? 0.0f : -0.25f * (k.high - k.low)));
    return out;
}

// the largest change of control `target` in this context (dB, −300 = none); `base` the context's own render
inline double moveIn (const ChannelDef& ctx, const Pattern& p, const Samples& s, const Control& k, const std::vector<float>& base)
{
    const auto v = k.get (ctx);
    if (! v)
        return -300.0;
    double best = -300.0;
    for (const auto to : valuesFrom (k, *v))
    {
        auto changed = ctx;
        if (! k.set (changed, to))
            continue;
        best = std::max (best, ctlaudit::diffDb (ctlaudit::render (changed, p, s), base));
        if (best > wakeDb)
            break;
    }
    return best;
}

struct Verdict
{
    int control = 0;      // index into the editor list
    double plain = -300.0; // the move with the plain pattern
    double best = -300.0;  // the move in the context that woke it
    std::string key, note; // "" = it moves plainly, or it never moved (best ≤ wakeDb)
};

// every control of the list: plain first, then for the silent ones the contexts in order (each context's base render
// is shared by the controls still silent when it doesn't depend on them). `known` (control name → key) is tried first;
// `searchAll` false stops there (the regression test); `only` limits the list (indices into `controls`).
inline std::vector<Verdict> wake (const ChannelDef& c, const std::vector<Control>& controls, const Samples& s,
                                  const std::map<std::string, std::string>& known = {}, bool searchAll = true,
                                  const std::vector<int>& only = {}, double budgetSeconds = 0.0)
{
    // (a budget: the slow engines stop searching after it, their silent controls left DEAD for a hint)
    const auto t0 = std::chrono::steady_clock::now();
    auto overBudget = [&] {
        return budgetSeconds > 0.0 && std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() > budgetSeconds;
    };
    std::vector<Verdict> out;
    const auto hints = hintsFor (c.instrument, controls);
    const auto p0 = ctlaudit::patternFor (c.instrument);
    const auto base0 = ctlaudit::render (c, p0, s);
    std::vector<int> silent;
    for (int i = 0; i < (int) controls.size(); ++i)
    {
        if (! only.empty() && std::find (only.begin(), only.end(), i) == only.end())
            continue;
        Verdict v;
        v.control = i;
        v.plain = v.best = moveIn (c, p0, s, controls[(std::size_t) i], base0);
        out.push_back (v);
        if (v.plain <= wakeDb)
            silent.push_back ((int) out.size() - 1);
    }
    auto tryKey = [&] (const std::string& key, std::vector<int>& left, bool fromKnown = false) {
        // a pattern / set: context is the same for every control (a set: on the control itself is refused), so its
        // render is shared; a hint: or rand: context leaves the control itself alone, so it is built per control
        const bool shared = key.find ("hint:") == std::string::npos && key.find ("rand:") == std::string::npos;
        std::vector<float> base;
        bool built = false;
        std::vector<int> next;
        const auto hintAt = key.rfind ("hint:", 0) == 0 ? findHint (hints, split (key.substr (5), '+').front()) : -1;
        const auto* scope = ! fromKnown && hintAt >= 0 && hintAt < (int) hints.size() && ! hints[(std::size_t) hintAt].scope.empty() ? &hints[(std::size_t) hintAt].scope : nullptr;
        for (const auto at : left)
        {
            if (overBudget() || (scope != nullptr && controls[(std::size_t) out[(std::size_t) at].control].name.rfind (*scope, 0) != 0))
            {
                next.push_back (at);
                continue;
            }
            auto& v = out[(std::size_t) at];
            ChannelDef cx;
            Pattern px;
            std::string nx;
            if (! context (key, c, controls, hints, v.control, cx, px, &nx))
            {
                next.push_back (at);
                continue;
            }
            if (! shared || ! built)
                base = ctlaudit::render (cx, px, s), built = true;
            static const bool debug = std::getenv ("CTLWAKE_DEBUG") != nullptr;
            if (ctlaudit::peak (base) < 1e-5)
            {
                if (debug)
                    std::fprintf (stderr, "    [%s] %s: silent context\n", controls[(std::size_t) v.control].name.c_str(), key.c_str());
                next.push_back (at);
                continue;
            }
            const auto m = moveIn (cx, px, s, controls[(std::size_t) v.control], base);
            if (debug)
                std::fprintf (stderr, "    [%s] %s: peak %.3g, move %.0f dB\n", controls[(std::size_t) v.control].name.c_str(), key.c_str(), ctlaudit::peak (base), m);
            if (m > wakeDb)
                v.best = m, v.key = key, v.note = nx;
            else
                next.push_back (at);
        }
        left = std::move (next);
    };
    // the known keys first (each control its own)
    if (! known.empty())
    {
        std::vector<int> next;
        for (const auto at : silent)
        {
            const auto it = known.find (controls[(std::size_t) out[(std::size_t) at].control].name);
            std::vector<int> one { at };
            if (it != known.end())
                tryKey (it->second, one, true); // (a known key is tried whatever its hint's search scope)
            if (! one.empty())
                next.push_back (at);
        }
        silent = std::move (next);
    }
    if (! searchAll || silent.empty())
        return out;
    static const char* const pats[] = { "pat:mono", "pat:bend", "pat:mod", "pat:vel:0.2", "pat:vel:1", "pat:rel", "pat:long",
                                        "pat:retrig", "pat:pads", "pat:low", "pat:high", "pat:bpm", "pat:offline", "pat:gap" };
    // the hand-written hints first (they name the context), then each performance alone
    for (int h = 0; h < (int) hints.size() && ! silent.empty(); ++h)
    {
        const std::string key = "hint:" + hintId (hints[(std::size_t) h].note) + (hints[(std::size_t) h].pattern.empty() ? "" : "+" + hints[(std::size_t) h].pattern);
        tryKey (key, silent);
    }
    for (const auto* k : pats)
        if (! silent.empty())
            tryKey (k, silent);
    // one other control alone, at each end (a section on)
    for (int j = 0; j < (int) controls.size() && ! silent.empty(); ++j)
    {
        const auto v = controls[(std::size_t) j].get (c);
        if (! v)
            continue;
        for (const auto to : valuesFrom (controls[(std::size_t) j], *v))
        {
            std::ostringstream key;
            key << "set:" << j << ":" << to;
            tryKey (key.str(), silent);
        }
    }
    // random settings, plain and with each performance the controls may need
    for (int r = 0; r < 24 && ! silent.empty(); ++r)
        for (const char* pat : { "", "+pat:mono", "+pat:mod", "+pat:bend", "+pat:long" })
            if (! silent.empty())
                tryKey ("rand:" + std::to_string (r) + pat, silent);
    // a performance with one other control (the mod wheel's destination needs its amount up)
    for (const char* pat : { "pat:mod", "pat:bend", "pat:mono", "pat:long", "pat:vel:0.2", "pat:rel", "pat:bpm" })
        for (int j = 0; j < (int) controls.size() && ! silent.empty(); ++j)
        {
            const auto v = controls[(std::size_t) j].get (c);
            if (! v)
                continue;
            for (const auto to : valuesFrom (controls[(std::size_t) j], *v))
            {
                std::ostringstream key;
                key << pat << "+set:" << j << ":" << to;
                tryKey (key.str(), silent);
            }
        }
    return out;
}
} // namespace lynx::ctlwake
