#pragma once

// M4A export (owner decision D5, round als190ka-export): AAC-LC in an MPEG-4 audio file, encoded by the operating
// system's own encoder. LYNX ships no AAC encoder of its own (no FDK-AAC, no FFmpeg: their licences and AAC's patents
// stay with the platform vendor, who licenses the codec with the system):
//  - Windows: Media Foundation's Sink Writer (the MPEG-4 file sink with Microsoft's AAC encoder MFT) —
//    engine/AacEncoderWindows.cpp. Windows "N" editions without the Media Feature Pack have no AAC encoder: then
//    available() is false and M4A is not offered.
//  - macOS: AudioToolbox's ExtAudioFile (kAudioFileM4AType, kAudioFormatMPEG4AAC) — engine/AacEncoderMac.cpp.
//  - Linux and the BSDs: no system AAC encoder can be called without bundling one (GStreamer / FFmpeg encoders are
//    optional packages, often without AAC), so available() is false there: the Export window leaves M4A out and
//    `--export out.m4a` says it is not available (engine/AacEncoder.cpp).
// The interface is plain C++ (no JUCE, no platform headers): interleaving, rate and bit-depth handling live here.

#include <string>

namespace lynx::aac
{
// Whether this system can write M4A (checked once, then cached).
bool available();

// Writes `frames` frames of `channels` (1 or 2) float channels at `sampleRate` (44100 or 48000: callers bring other
// rates there first, exportopts::decimate) as AAC-LC at `kbps` (96 / 128 / 160 / 192) into the MPEG-4 file at
// `utf8Path` (created or replaced). Samples are clipped to ±1 (Windows' encoder takes 16-bit PCM, macOS' float).
// False with `error` set when the system cannot encode (or the file cannot be written).
bool encode (const std::string& utf8Path, const float* left, const float* right, long long frames, int channels, int sampleRate,
             int kbps, std::string& error);

// Why available() is false on this system, for the command line and the window's tooltip ("" when it is available).
std::string unavailableReason();
} // namespace lynx::aac
