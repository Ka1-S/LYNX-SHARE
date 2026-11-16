#pragma once

// GigaStudio / LinuxSampler instruments (.gig) as LYNX DirectWave zones (round 190ii, issue #149).
//
// LYNX's own reader (read-only, pure C++). The layout of the format — DLS Level 2 RIFF ('DLS ', 'ptbl', 'lins',
// 'wvpl') with GigaStudio's chunks ('3lnk' dimensions, '3prg' / '3ewl' dimension regions, '3ewa' parameters,
// 'xfil' extension files) — follows the MMA's DLS specification and the description of the format in libgig
// (C. Schoenebeck, GPL-2.0-or-later, compatible with LYNX's AGPL-3.0; see THIRD_PARTY.md); no libgig code is
// compiled in. Details: docs/analise/REA_rodada190ii.md §2.
//
// A region (key range × velocity range) splits into dimension regions; LYNX makes one zone per dimension region
// that plays on a plain note-on:
//   velocity           → the zone's velocity range (GigaStudio 3's custom split points, or even splits)
//   layer              → zones that sound together
//   round robin / random (and round robin over the keyboard) → a DirectWave alternation group per region
//                         (cycle / random)
//   sample channel     → the first one only (the region's stereo sample plays as one stereo zone)
//   release trigger, keyswitch (gig "keyboard"), smart MIDI, aftertouch, MIDI controllers (mod wheel, pedals, …)
//                      → only the zone a plain note plays (the first); the others are reported as skipped
// Per zone: the sample (written out as a WAV file), unity note, fine tune, attenuation, pan, pitch tracking, the
// first loop (forward), the sample start offset, the amplitude envelope (attack, decay 1, sustain, release) and the
// region's key group (DirectWave's exclusive group). Not mapped (reported): filters, LFOs, the pitch / filter
// envelopes, velocity curves, crossfades, compressed samples (GigaStudio's lossless compression).

#include <optional>
#include <string>
#include <vector>

#include "core/SamplerImport.h"

namespace lynx::msimport::gig
{
std::vector<std::string> instrumentNames (const std::string& path, const Access& access);
// Instrument `index`: its zones with their samples stored through access.saveWav (nullopt + report.error)
std::optional<Program> readInstrument (const std::string& path, const Access& access, int index, Report& report);

// GigaStudio's time code (32-bit) in seconds: 1.000000008813822^code (≈ 2^(code / 2^26.3))
double timeOf (std::int32_t code);
} // namespace lynx::msimport::gig
