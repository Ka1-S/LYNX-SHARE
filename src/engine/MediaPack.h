#pragma once

// Online Sessions (round 169j, ONLINE_SESSIONS.md §3.2): lossless FLAC for a live session's media transfer.
//
// A medium is identified by the SHA-256 of its bytes, so the receiver must end with exactly the sender's file. A PCM
// WAV (16 or 24 bits, integer) travels as a "pack": the file's bytes before and after its sample data, verbatim, plus
// the samples as FLAC; unpacking writes the samples back in their own format between the two, byte for byte. Anything
// else (32-bit float stand-ins, MP3, a WAV FLAC does not shrink) travels as it is.

#include <juce_core/juce_core.h>

namespace lynx::mediapack
{
// The smallest file worth packing (below it the raw bytes go faster than an encoder starts).
constexpr juce::int64 minimumSize = 256 * 1024;

// Packs `wav` into `pack` (replaced). False when the file is not a PCM WAV FLAC can hold, when the pack would not be
// at least 10 % smaller, or when unpacking it would not give back the same bytes (checked before it is kept).
bool pack (const juce::File& wav, const juce::File& pack);
// Writes the original file from a pack (false: not a pack, or damaged).
bool unpack (const juce::File& pack, const juce::File& out);
// Whether the bytes start like a pack.
bool isPack (const juce::File& file);
} // namespace lynx::mediapack
