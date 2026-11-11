#pragma once

// FL's Slicex as a channel (round 190l): core/SlicexEngine.h on the channel's state and samples.
//
// A Slicex channel keeps FL's state in ChannelDef::pluginState, deck A's sample in ChannelDef::samplePath and deck B's
// sample, when it has one, in ChannelDef::zones[0].path (the one zone a Slicex channel may have; its other fields are
// not used). The state itself still names both files (or holds their WAVs), so an .flp written from the channel is
// FL's own state; the two paths are where LYNX found or saved the audio on this machine.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"
#include "core/Slicex.h"

namespace lynx
{
std::unique_ptr<Instrument> makeSlicexInstrument (std::shared_ptr<const SampleData> deckA, std::shared_ptr<const SampleData> deckB);

namespace slicex
{
// deck B's sample of a Slicex channel ("" = none), and setting it
const std::string& deckBPath (const ChannelDef& channel) noexcept;
void setDeckBPath (ChannelDef& channel, const std::string& path);

// The beat count of a sample that is a deck by reference (its WAV is not in the state): the file's own (ACID chunk),
// else FL's guess from its length (guessBeats). What the editor writes as the deck's range when it loads the file,
// and what the engine and the editor take the deck's tempo from once the saved range holds the editor's zoom
// (round 190aj)
double sampleBeats (const SampleData& sample) noexcept;

// Fills an imported channel (FlpImport): the state and the decks' samples (the files it names, resolved with
// `existing`, or the WAVs it holds, stored with `saveEmbedded`). False: not a state LYNX reads, or no deck's audio is
// on this machine.
bool importChannel (ChannelDef& def, const std::vector<std::uint8_t>& state,
                    const std::function<std::string (const std::string&)>& existing,
                    const std::function<std::string (const std::vector<std::uint8_t>&, const std::string&)>& saveEmbedded);
} // namespace slicex
} // namespace lynx
