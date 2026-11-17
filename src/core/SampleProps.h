#pragma once

// FL Edison's "Sample properties" form (Format › Edit properties..., F2), round gaps190kf. Static study of FL Studio
// 2026's Edison_x64.dll (sha256 928d4487…fe89bb) with Ghidra: the form TMEWAVPropForm (layout from its DFM), its
// handlers (Autodetect pitch 0x7b2ed0, Default pitch 0x7b2e70, Round pitch 0x7b3240, Tempo / Length edits 0x7b2910 /
// 0x7b2a80, Round tempo 0x7b3310, Limit 0x7b3470, Default tempo 0x7b2d90, Tempo-sync 0x7b3430, the quick tempo
// estimation 0x7b30c0) and the Accept handler 0x878c90. Behaviour in our words; JUCE-free (tests in
// tests/Gaps190kfNativesTests.inc). The values live in audioedit::SampleProps and travel in the WAV (core/AudioEdit).

#include <cstdint>
#include <string>

#include "core/AudioEdit.h"

namespace lynx::sampleprops
{
// --- tempo ↔ length (FL 0x62c120): `frames` = the selection's length (the whole sample without one) -----------------
double beatsOf (double tempo, double frames, double sampleRate) noexcept;  // frames · tempo / (rate · 60)
double tempoOf (double beats, double frames, double sampleRate) noexcept;  // rate · beats · 60 / frames (0 for no frames)
// The Tempo box's value as FL takes it (0x7b2910): held in 0 … 100 000
double clampTypedTempo (double tempo) noexcept;

// "Quick estimation for short loops" (0x7b30c0): the length in beats (0 = none) from the bars the selection lasts at
// the project tempo — under 0.75 bar: 2 beats; else 4 × the rounded bars (at most 256); none when the tempo it gives
// lies outside 30 … 240 BPM. `tempoOut` gets that tempo (whatever the beats).
int quickBeats (double frames, double sampleRate, double projectTempo, double& tempoOut) noexcept;

// The Limit menu (0x7b3470: 50 to 100, 75 to 150, 100 to 200, 150 to 300): a tempo above 1 halved or doubled into
// [low, 2·low]
double limitTempo (double tempo, int low) noexcept;

// Round (0x7b3240): the fine tune's whole semitones go into the middle note (0 … 127), the fine tune becomes 0
void roundPitch (int& middleNote, double& fineTune) noexcept;

// Autodetect (0x7b2ed0): a detected pitch (MIDI semitones, < 0 = none) → middle note and fine tune (cents): the pitch in
// cents rounded, its hundreds the note and the rest the fine tune. False (nothing set) for no pitch.
bool fromDetectedPitch (double semitones, int& middleNote, int& fineCents) noexcept;

// --- Accept (0x878c90) -----------------------------------------------------------------------------------------------
enum class Channels
{
    stereo,      // "Stereo" (a mono sample is doubled)
    monoMerged,  // "Mono - Merged" ((L + R) / 2)
    monoLeft,    // "Mono - Left only"
    monoRight    // "Mono - Right only"
};

struct Form
{
    std::string name, comments, fileName;
    double sampleRate = 44100.0;
    bool resample = false;       // "Resample": resampled to the rate; off: the rate changes and the audio stays
    bool int16 = false;          // Format: 16Bit int / 32Bit float
    Channels channels = Channels::stereo;
    double tempo = 0.0;
    bool tempoSync = false;
    int middleNote = 60, lowNote = 0, highNote = 127;
    double fineTune = 0.0;
};

// The form's values for a document (what FL fills in when it opens: 0x7b2140)
Form formOf (const audioedit::Document& doc, const std::string& fileName);

// The rate FL accepts (below 1: 1, above 100 000 000: that) and the tempo (below 0: 0, above 960: 960)
double acceptedRate (double rate) noexcept;
double acceptedTempo (double tempo) noexcept;

// FL's precision change to 16-bit integers (0x626d00): a peak above 0 dB is first brought to 0 dB, then every sample
// is held in ±1 and set on the 16-bit grid (×32767, rounded). The document keeps floats on that grid.
void toInt16 (audioedit::Document& doc);
// FL's channel change (0x627330): Stereo doubles a mono sample; the Mono items act on a stereo one
void setChannels (audioedit::Document& doc, Channels channels);

// Everything Accept does to the document, in FL's order: name and comments, the rate (resampled or not), the channels
// and the precision, the tempo and tempo-sync, the fine tune and the note mapping. Returns true when the rate changed.
// `fileName` is the caller's (it names the file the document is saved to).
bool accept (audioedit::Document& doc, const Form& form);

// MIDI note name as FL's note lists show them (C5 = 60)
std::string noteName (int note);
} // namespace lynx::sampleprops
