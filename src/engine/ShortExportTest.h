#pragma once

// Issue #138 (round 190c): a short note at the very start of the Playlist must export like the same note a bar later.
// Each case goes through the Export window's path (an ExportJob made on the message thread, run on its own thread
// while the message loop runs) twice: the note at tick 0, and at bar 2. See engine/ShortExportTest.cpp.

#include <functional>

#include <juce_core/juce_core.h>

namespace lynx::shortexport
{
// Message thread, with the message loop running; returns at once. `done` gets "OK (...)" or "ERROR ..." on the message
// thread. `everyUnit`: every instrument LYNX can make here (else a handful, for the selftest); `repeats`: each pair
// that many times (a race shows up in some of them); `pluginFiles`: hosted instruments to add; `log`: a line per pair.
void run (bool everyUnit, int repeats, const juce::StringArray& pluginFiles, std::function<void (const juce::String&)> log,
          std::function<void (juce::String)> done);

// Round 190hn (issue #138): `count` cases drawn from every export setting (format and its options, rate, normalising,
// tail, range, loops, stems, mono, dither, HQ, warm-up, effects, the PDC trim; with `realtime`, the real-time export
// through a stand-in device too) over short sounds at tick 0 (one-shots of 6 ms … 3 s, from a start offset, looped,
// audio clips, synth, drums), each exported again a bar later: a file that comes out silent or quieter fails. `seed`
// makes a run repeatable. Message thread with the loop running; returns at once, works on a thread of its own.
// `pluginFile`: a hosted instrument that joins the sounds (its first instrument; empty = none).
void stress (int count, std::int64_t seed, bool realtime, const juce::String& pluginFile, std::function<void (const juce::String&)> log,
             std::function<void (juce::String)> done);

// The --selftest's check of the AIFF and M4A exports (owner decision D5): a 24-bit AIFF written from the same render as
// a 24-bit WAV holds the same samples; a 16-bit AIFF streamed alone matches a 16-bit WAV render; M4A (a 96 kHz render)
// comes out as an MPEG-4 file where the system has an AAC encoder, and is refused without a file where it has none.
// Synchronous, on the calling thread. "OK (...)" or "ERROR ...".
juce::String formatsCheck();
} // namespace lynx::shortexport
