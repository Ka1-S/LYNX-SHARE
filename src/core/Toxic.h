#pragma once

// FL Studio's Toxic Biohazard rebuilt as the engine decoded in docs/analise/REA_rodada45.md (clean-room; a 1:1 port of
// our own round-45 reference model r45/py/toxmodel.py + toxwave.py + the effects model toxfx.py, which reproduce renders
// of the installation's Toxic Biohazard_x64.dll bit for bit on the demo channels).
//
// Six operators per voice, each with up to eight unison lanes, read band-limited tables (128 levels of 256 cubic
// segments) built from 128-harmonic spectra, phase-modulated by every operator through the FM matrix with a one-sample
// delay; per-operator envelopes and levels with a Catmull-Rom ramp across each 128-sample block; FL's 4-pole ladder with
// its filter envelope (REA rounds 26 and 33); the master amplitude envelope; two table LFOs; the CC/velocity/LFO
// modulation matrix; then the output rack (distortion + DC blocker, two effect slots, 8-band EQ; core/ToxicFx.h) and the
// master gain.
//
// The channel keeps the plugin's raw saved state (record 0xD5: "SSLF" + the TBi2 preset) in ChannelDef::pluginState and
// the path of the user's own Toxic Biohazard_x64.dll in ChannelDef::samplePath: the built-in waves 2…32 are spectra
// stored inside that DLL and are read from it at run time (SHA-256 checked, never bundled). Wave 0 (the preset's drawn
// spectrum) comes from the state and wave 1 is a plain sine, so states that only use those play without the DLL.
//
// Host interface as FL's wrapper: MIDI velocity from Init.Vol, per-note level tables from ComputeLRVol(Final.Pan,
// Final.Vol) and the pitch-offset table, engine chunks of 64 samples. The phase generator of free-running operators is
// process-global in FL (shared by every instance); LYNX gives each instance its own generator started like a freshly
// loaded DLL, so free-running phases are deterministic and differ from any given FL render by design.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this Toxic Biohazard state ("SSLF" + TBi2/TBio/TBiD preset of 10 050 bytes)
bool toxicStateUsable (const std::vector<std::uint8_t>& state);

// true when the state selects a built-in wave other than the sine (operators or LFOs), i.e. it needs the DLL's spectra
bool toxicNeedsDll (const std::vector<std::uint8_t>& state);

// true when `dllPath` is the decoded build of Toxic Biohazard_x64.dll (SHA-256 checked)
bool toxicDllUsable (const std::string& dllPath);

// byte offset of the raw parameter P[index] (0…339, float) inside the state, or −1 (channel settings panel)
int toxicParamOffset (const std::vector<std::uint8_t>& state, int index);

// a state with the plugin's constructor values (TBi2 layout, no drawn waves): used to create a fresh channel and by the
// tests
std::vector<std::uint8_t> toxicDefaultState();

// One cycle of the wave an operator (slot 0…5) or LFO (6, 7) plays, peak-normalised, `points` long (the editor's wave
// displays); empty when its wave needs the DLL and `dllPath` isn't usable, or the state can't be read
std::vector<float> toxicWaveShape (const std::vector<std::uint8_t>& state, const std::string& dllPath, int slot, int points);

// The parameters the editor shows and automation clips drive (round 168n), sorted by index: P[0…156], the EQ and
// effect fields P[227…293] (0…1), and the raw-integer settings P[295…315] (LFO sync, filter mode, polyphony, unison,
// glide mode, delay sync, waves, effect types) with their ranges
const std::vector<EngineParam>& toxicParams();
// a sequencer step's text: "---" (rest), "SUS" (hold) or the scale degree (FUN_18003b1b0; round 169h)
std::string toxicStepText (int unit);
// a modulation slot's source: 0 none, 1 velocity, 2/3 LFO 1/2, 4 aftertouch, 5 pitch wheel, 7…134 MIDI CC 0…127
std::string toxicModSourceName (int source);

std::unique_ptr<Instrument> makeToxicInstrument();

namespace toxic
{
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch offset
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like toxmodel.run: host blocks of `block` samples split at every event, engine chunks of 64, host law A for
// the per-note levels, tempo `bpm`, max-polyphony event value `maxPoly` (0 → 32). effects = false switches the rack's
// slots, EQ and distortion off (the DC blocker stays, as in the DLL). Interleaved stereo (2·length); empty if the state
// is unusable or needs the DLL and `dllPath` is not usable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const std::string& dllPath, int block = 512, double bpm = 125.0, int maxPoly = 0,
                                    bool effects = true);

// Round 169h probes of the MIDI and SEQ pages (tests): MIDI input at a sample (status 0xB0 CC / 0xD0 channel pressure)
struct RefMidi
{
    int at = 0, status = 0xB0, data1 = 0, data2 = 0;
};
// the notes the engine starts and stops (velocity 0 = off) at the sample of the engine chunk that played them
struct SeqEvent
{
    long long sample = 0;
    int key = 0;
    float velocity = 0.0f;
};
std::vector<SeqEvent> sequencerEvents (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes,
                                       const std::vector<RefMidi>& midi, double sampleRate, int length, double bpm = 125.0);
// parameter `index` as the engine reads it (the modulation slots applied) after the MIDI input
float modulatedParam (const std::vector<std::uint8_t>& state, const std::vector<RefMidi>& midi, int index);
} // namespace toxic
} // namespace lynx
