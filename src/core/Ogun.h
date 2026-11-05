#pragma once

// FL Studio's Ogun rebuilt as the inverse-FFT timbre engine decoded in docs/analise/REA_rodada37.md (clean-room; a 1:1
// port of our own round-37 reference model r37/py/ogmodel.py, which was checked against renders of the installation's
// DLL: 20 of 20 demo channels within −60 dB, median −123 dB). Per voice: up to 32 767 partials (Richness) whose
// complex spectrum (seeded uniform weights, fullness shaping, morph, Forced/Impulse/Decay/Release per-partial maps
// with fixed patterns, 1/√h, logarithmic axis and EQ shift, pre-decay, RMS normalisation, band limit) evolves per
// 50/25 ms frame by the hold / ALT DECAY / release laws and is turned into an N-point table by an inverse real FFT;
// the two latest tables are read with the quality's kernel (linear, 6-point Hermite, 32-point windowed sinc) and
// crossfaded with the unison sub-voices' pan gains; then declick, the 4-type × 1–3-stage filter (2× zero-order-hold
// oversampled below 88.2 kHz), the volume/pan matrix with gain ramps, and the 13 articulators (point-curve envelopes,
// LFOs, key/velocity/X/Y maps, velocity options, portamento); the plugin-level 3-band EQ, chorus, delay and reverb
// (FL's shared plate reverb, core/PlateReverb.h); qualities 2–4 run at twice the rate with the 2:1 polyphase
// decimator. The channel keeps the plugin's raw saved state (record 0xD5, as FL wrote it) in ChannelDef::pluginState.
//
// Level: Ogun applies Final.Vol × (3^(P0/128) − 1)/2 × the volume articulator and its own pan matrix (FL's host does
// not apply the channel volume/pan to it, REA_rodada28): LYNX's channel gains are turned back into Final.Vol/Pan and
// applied once, inside the engine.
//
// Rendering: FL forces quality 4 when it renders to a file (dispatcher process-mode flag); LYNX does the same while
// it exports (offlineRendering()); live playback uses the state's quality (P54).
//
// Differences from FL by design / not modelled (REA_rodada37 §1.20 and open questions):
// - Seed 2 "Random": FL's per-note counter starts from its RTL random seed (clock); LYNX's starts at a fixed value per
//   instance (0), so Random spectra are deterministic and differ from any given FL render.
// - Read start j₀ (argmin of the first table) is ill-conditioned: an occasional time shift of a note's table vs FL.
// - IPP's 11-bit exp in the fullness shaping is evaluated exactly (FL's last bits differ; −52 dB worst synthetic case).
// - Global LFOs/envelopes (curve header flag 2) run as per-voice ones; host answers 2/6 are 0, release velocity is
//   0.5 (FL's default note-off velocity), Final.FCut/FRes (Mod X/Y) are 0; parameter changes rebuild the instance.
// - Voices: a pool grown on demand up to 64 voices per instance (the oldest is cut beyond that).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can play this Ogun state (version ≥ 14, every articulator and curve parsed)
bool ogunStateUsable (const std::vector<std::uint8_t>& state);
// the same for an Autogun state (Autogun_x64.dll: Ogun's layout plus a 4-byte seed after the comment from version 19 on;
// REA_rodada57)
bool autogunStateUsable (const std::vector<std::uint8_t>& state);

// Raw integer parameter P[index] (0…63) of a state, or nullopt
// An Ogun state of version 10…13 (FL's Plugin database Ogun is version 11) rewritten in version 14's layout, as Ogun's
// own loader reads it (round 169l); newer or unreadable states unchanged. Keeps ogunParam / ogunSetParam valid.
std::vector<std::uint8_t> ogunCurrentLayout (const std::vector<std::uint8_t>& state);
// index 53 is the reverb's on switch (P[53]'s low byte); 64 / 65 are the delay's tempo-sync and on switches, the two
// bytes after P (round 170i3: the editor had no switch for them, so a new channel's delay and reverb knobs did nothing)
constexpr int ogunReverbOnIndex = 53, ogunDelayTempoIndex = 64, ogunDelayOnIndex = 65;
std::optional<int> ogunParam (const std::vector<std::uint8_t>& state, int index);
// Writes P[index] (the state is re-wrapped as zlib with stored blocks); false if the state can't be read
bool ogunSetParam (std::vector<std::uint8_t>& state, int index, int value);

// what this state uses that is not modelled exactly (empty: nothing), for reports
std::vector<std::string> ogunFallbacks (const std::vector<std::uint8_t>& state, bool autogun = false);

std::unique_ptr<Instrument> makeOgunInstrument();
// Autogun (REA_rodada57): Ogun's engine with the master level × 0.25 and the Magic Dust stage (FL's Soundgoodizer engine
// with the preset Autogun\Data\SG.dat read at run time from ChannelDef::samplePath, band mix = parameter 63)
std::unique_ptr<Instrument> makeAutogunInstrument();

namespace ogun
{
// One note for the reference renderer (drives the engine like ogmodel.Plugin.run).
struct RefNote
{
    int start = 0, key = 60;
    double velocity = 1.0, finalVol = 1.0, finalPan = 0.0, pitchOffset = 0.0, fcut = 0.0, fres = 0.0, releaseVelocity = 0.0;
    int release = -1; // sample of the note-off (−1: held)
};

struct RefOptions
{
    int quality = -1;          // −1: the state's P54
    double tempo = 140.0;      // ProcessEvent 0 (140 when never sent)
    int block = 512;           // host block (split at every event)
    int counter = 0;           // Seed-2 "Random" counter at the first note
    int maxPolyphony = 0;      // ProcessEvent 1 (0 = unlimited)
    bool hostKills = false;    // false: like the EXEC harness / model default (finished voices stay listed)
    std::vector<int> j0Shift;  // per note (in note order): read-start shift (verification only; 0 = own choice)
    bool autogun = false;      // the state is Autogun's (REA_rodada57)
    std::vector<std::uint8_t> sgState; // Autogun: the bytes of Autogun\Data\SG.dat (empty: no Magic Dust stage)
};

// Renders `notes` from `state` like ogmodel.Plugin(...).run(length, events). Returns interleaved stereo float samples
// (2·length), empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const RefOptions& options = {});
} // namespace ogun
} // namespace lynx
