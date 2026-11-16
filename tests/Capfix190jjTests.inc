// Round 190jj (capture190ji's "Change in LYNX" lines): the transport states each toolbar look shows (BandLab's Simple
// capsule: Pause and Stop while it runs, Record steady; Live: steady glyphs; FL alone blinks) and the Master-only meter
// reader behind the Simple top bar's master volume.

#include "core/Meters.h"
#include "core/ToolbarLayout.h"

TEST (capfix190jj_transport_states_per_look)
{
    using namespace lynx::toolbarlayout;
    const auto fl = transportStates (Look::fl), simple = transportStates (Look::simple), live = transportStates (Look::live);
    CHECK (fl.pauseWhilePlaying && fl.recordBlinks && ! fl.stopWhilePlaying); // (round 190ih's R3, unchanged)
    CHECK (simple.pauseWhilePlaying && simple.stopWhilePlaying);              // BandLab: Play→Pause, Back→Stop
    CHECK (! simple.recordBlinks && ! live.recordBlinks);                     // BandLab and Live: steady
    CHECK (! live.pauseWhilePlaying && ! live.stopWhilePlaying);              // Live: Play lit green, no swap
}

TEST (capfix190jj_master_bar_meter_reads_the_master_only)
{
    auto bank = std::make_unique<lynx::MeterBank>();
    bank->accumulate (3, 0.9f, 0.9f); // (an insert: not the Master)
    CHECK (bank->takeMasterBarPeak (0) == 0.0f && bank->takeMasterBarPeak (1) == 0.0f);
    bank->accumulate (0, 0.5f, 0.25f);
    bank->accumulate (0, 0.4f, 0.75f);
    CHECK (bank->takeMasterBarPeak (0) == 0.5f);
    CHECK (bank->takeMasterBarPeak (1) == 0.75f);
    CHECK (bank->takeMasterBarPeak (0) == 0.0f); // (taken: the next frame starts again)
    // its own reader: the Mixer's Master strip still gets its peaks
    CHECK (bank->takePeak (0, 0) == 0.5f && bank->takePeak (0, 1) == 0.75f);
}
