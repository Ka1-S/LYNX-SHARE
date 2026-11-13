#pragma once

// Voice Shift (round 135): LYNX's monophonic voice shifter, in the role of the small "voice manipulation" effects used
// on vocal chains (pitch, formant, a hard-tune mode and a robot mode with a bit of tube drive). Original code on LYNX's
// own YIN detector and TD-PSOLA shifter (core/VocalPitch.h); the controls follow the parameter list of the official
// trial of the plugin it stands in for, read black-box through the host API (docs/analise/REA_rodada135s.md §1):
//
//   Pitch     −12 … +12 semitones (0.1 st steps on screen)      Formant  −12 … +12 semitones
//   Link      the formants follow the pitch (a varispeed-like shift; Formant then offsets them)
//   Mode      Transpose (plain shift) · Quantize (the nearest semitone, at once) · Robot (one fixed note: C4 at
//             Pitch 0, measured on the trial; Pitch moves it)
//   Drive     0 … 10: a tube stage on the shifted voice           Mix      dry / wet (linear, measured)
//
// Latency: two periods of the lowest detected pitch (70 Hz), reported to PDC; the dry signal is delayed to match.
// Round trial187b: each channel has its own detector and shifter (as the reference, measured black-box); once shifted
// the right channel runs 1.25 ms behind the left; Transpose at Pitch 0 / Formant 0 passes the input itself.
// Round 190dp (Law = Measured, new instances): what the detector does not find voiced — noise, breath, consonants, a
// tone above its range — moves with the formants as the reference's does ([black-box] its sine sweep comes out at
// exactly the Formant shift above ~1.6 kHz: −3 st at Formant −3, +2 st at Formant +2, unshifted at Pitch +5 alone),
// through longer flat-topped grains (core/VocalPitch.h, Psola::setUnvoicedShift). Law = Classic (projects saved
// before) passes it through as round 135 … 190 did, bit for bit.
// Meters (controlOutput): 0 = sung note / 127 (0 = unvoiced), 1 = the note sent out / 127, 2 = the held MIDI note / 127
// (0 = none; round 137).

#include <memory>
#include <string>

namespace lynx
{
struct EffectInfo;
class Effect;

namespace voiceshift
{
enum Param : int
{
    pitch, formant, link, mode, drive, mix,
    midi, // round 137 (appended, Off by default): the Channel Rack channel whose held notes set the pitch (the
          // original's MIDI control): Transpose / Quantize shift by (highest held note − C4), Robot sings that note
    law,  // round 190dp (appended; Measured for new instances, Classic for projects saved before): the measured
          // unvoiced path — noise, breath and what lies above the detector's range move with the formants, as the
          // reference's do (black-box: its sweep and noise come out at the Formant shift where no pitch is found)
    count
};
enum class Mode { transpose, quantize, robot };

double pitchSemitones (float v) noexcept;   // −12 … +12
double formantSemitones (float v) noexcept; // −12 … +12
float semitoneValue (double st) noexcept;   // inverse of both
Mode modeOf (float v) noexcept;
double driveAmount (float v) noexcept;      // 0 … 10
constexpr double robotNote = 60.0;          // C4 (261.6 Hz) at Pitch 0 [EXEC trial: 262 Hz]
constexpr double minHz = 70.0, maxHz = 1000.0;
int latencyFor (double sampleRate) noexcept;
// round trial187b: the level (dB) the reference's voice gains when the pitch moves this many semitones against
// the formants (pitch − formant shift; negative = quieter)
double tiltDb (double semitones) noexcept;
// the drive stage's static curve (no state): input → output at a drive amount 0 … 10
float driveCurve (float x, double amount) noexcept;
int midiChannelIndex (float v) noexcept;    // −1 = off, else 0 … 127 (LYNX Tune's law)
float midiChannelValue (int channel) noexcept;

const EffectInfo& info();
std::string paramText (int param, float v);
std::unique_ptr<Effect> make();
} // namespace voiceshift
} // namespace lynx
