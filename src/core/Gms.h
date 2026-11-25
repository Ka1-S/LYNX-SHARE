#pragma once

// FL Studio's GMS (Groove Machine Synth) rebuilt as the engine decoded in docs/analise/REA_rodada39.md §1 (clean-room;
// a 1:1 port of our own round-39 reference model r39/py/gmsmodel.py + gmspatch.py, gmsfx.py, gmseq.py, gmsrev.py, which
// was checked against renders of the installation's GMS_x64.dll: 9/9 demo channels within −60 dB).
//
// Per chunk of at most 512 samples: per-voice control values through the MIDI matrix (§1.2–1.4), per-sample envelopes,
// glide, level and modulation (§1.5), three dithered wavetable oscillators + noise per unison slot (§1.6) on 512
// band-limited tables per wave built from the installation's GMS\Synth Waves\<name>.wav (§1.6, read at run time, never
// bundled), the engine-wide LFOs (§1.8), the 4-iteration ladder per voice lane (§1.9), the ten effect slots and the
// output EQ (§1.13) and the master level/pan (§1.12).
//
// The channel keeps the plugin's raw saved state (record 0xD5, "SSLF" + the .gmsynth preset) in ChannelDef::pluginState
// and the folder of the wave files (…\Plugins\Fruity\Generators\GMS\Synth Waves) in ChannelDef::samplePath.
//
// Host interface as FL's wrapper (§1.3, §1.10, §1.11): MIDI velocity from Init.Vol, per-note L/R level tables from
// ComputeLRVol(Final.Pan, Final.Vol) and the pitch-offset table; FL's channel volume/pan/pitch reach GMS only through
// those tables, so LYNX recovers them from its channel gains and does not apply them a second time. The max-polyphony
// event is sent with value 0 (→ 99 voices; FL's value at channel creation is not confirmed, §1.3). Random draws: GMS
// uses one process-global LCG shared by every instance (§1.7); LYNX gives every instance its own generator started like
// a freshly created plugin (state 0, then the constructor's and the rate set's voice resets), so renders are
// deterministic and FL's noise/dither/random phases differ from any given FL render by design.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this GMS state ("SSLF" + Syn3/Syn4 preset with the 212 parameters)
bool gmsStateUsable (const std::vector<std::uint8_t>& state);

// the three oscillator wave base names of a state (as GMS resolves them: "Devils Horn" → "Double Spike"); empty if
// the state is unusable
std::vector<std::string> gmsWaveNames (const std::vector<std::uint8_t>& state);

// true when `path` is the folder of GMS' waves (here, or in this computer's FL installation: core/NativeData.h)
bool gmsDataUsable (const std::string& path);

// byte offset of parameter P[index] (0…211, float) inside the state, or −1 (used by the channel settings panel)
int gmsParamOffset (const std::vector<std::uint8_t>& state, int index);

std::unique_ptr<Instrument> makeGmsInstrument();

namespace gms
{
// One note for the reference renderer (the verification harness drives the engine exactly like the Python model).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch offset
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like gmsmodel.GmsPlugin.run: host blocks of `block` samples split at every event, chunks of ≤ 512,
// host law A for the per-note levels, tempo `bpm`, max-polyphony event value `maxPoly` (0 → 99), songPos ≥ 0 = the
// transport plays from that song position (beats); effects = false runs the voices only (the model's fx=False).
// Waves are read from `wavesDir` (a missing wave plays silent tables, like the model). Interleaved stereo float
// samples (2·length); empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const std::string& wavesDir, int block = 512, double bpm = 140.0, int maxPoly = 0,
                                    double songPos = -1.0, bool effects = true);
} // namespace gms
} // namespace lynx
