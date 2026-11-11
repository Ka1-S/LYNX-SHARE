#pragma once

// FL's FPC as one instrument (round 190q, issue #135): a zoned sampler per pad — what the .flp import's channel per
// pad plays (FlpImport.h drumPadChannel), so the two sound the same — with FPC's own rules inside: a note triggers
// every pad on its key at the sample's own pitch, a pad cuts the pads whose "cut by" is its "cut" group, a pad has its
// own pan and tuning, and a pad with an output offset plays into the mixer insert that many after the channel's.

#include <functional>
#include <memory>
#include <string>

#include "core/Instruments.h"
#include "core/Song.h"

namespace lynx
{
// The instrument of an InstrumentKind::fpc channel (FlSamplers.h fpcChannelPads: its state and the layers' files).
// `samples` decodes a file (null = no file plays). Not prepared, not configured.
std::unique_ptr<Instrument> makeFpcInstrument (const ChannelDef& channel,
                                               const std::function<std::shared_ptr<const SampleData> (const std::string& path)>& samples);
} // namespace lynx
