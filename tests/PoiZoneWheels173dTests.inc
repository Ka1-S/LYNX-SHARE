// Round 173d (wheel173d, PoiZone): the plugin's own MODULATION and PITCH BEND wheels.

#include "core/ModelParams.h"
#include "core/PoiZone.h"

namespace pzwheel173d
{
// one held C4 on a PoiZone channel; `edit` applied to the channel at `at` seconds as an editor edit does (the rack
// state rebuilt from the edited song, the instrument kept and configured again)
std::vector<float> renderEdit (const lynx::ChannelDef& channel, const std::function<void (lynx::ChannelDef&)>& edit, double at, double seconds)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (2);
    s.bpm = 120.0;
    auto c = channel;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    Note held;
    held.key = 60;
    held.length = ppq * 16;
    held.velocity = 0.8f;
    s.patterns[0].notes = { held };
    s.patterns[0].lengthTicks = (std::int64_t) (ppq * 32);
    s.mode = PlayMode::pattern;
    const auto none = [] (const std::string&) { return std::shared_ptr<const SampleData> {}; };
    std::shared_ptr<const RackState> state = RackState::build (s, nullptr, 48000.0, none);
    RackRenderer r;
    r.prepare (48000.0, 512);
    const auto tps = 120.0 / 60.0 * ppq / 48000.0;
    std::vector<float> out;
    bool edited = false;
    for (int done = 0; done < (int) (seconds * 48000.0); done += 512)
    {
        if (! edited && done >= (int) (at * 48000.0))
        {
            edited = true;
            if (edit)
                edit (s.channels[0]);
            state = RackState::build (s, state.get(), 48000.0, none);
        }
        std::vector<float> l (512), rr (512);
        r.process (state.get(), true, done * tps, 512, l.data(), rr.data(), nullptr);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}
} // namespace pzwheel173d

// FL's PoiZone has its own MODULATION and PITCH BEND wheels on its editor (TSynthEditorForm's ModWheel /
// PitchBendWheel; parameters 157 / 158, REA_rodada44 §1.3): PB range scales the bend wheel (range r → r + 1
// semitones), MW amount / destination act through the mod wheel. LYNX's editor had neither, so those controls did
// nothing; the wheels are kept in the channel's modelParams (FL's state has no room for them)
TEST (wheel173d_poizone_wheels_drive_bend_range_and_mod_wheel)
{
    using namespace lynx;
    ChannelDef c;
    c.instrument = InstrumentKind::poizone;
    c.pluginState = poizoneDefaultState();
    CHECK (modelparams::find (InstrumentKind::poizone, poizone::modWheelParam) != nullptr);
    CHECK (modelparams::findByName (InstrumentKind::poizone, "Pitch bend") != nullptr);
    CHECK (modelparams::rawValue (c, poizone::modWheelParam) == 0.0f);
    CHECK (modelparams::rawValue (c, poizone::bendWheelParam) == 0.5f); // (the centre)
    CHECK (modelparams::setRawValue (c, 78, 1.0f));                    // PB range 1 → ±2 semitones
    const auto still = pzwheel173d::renderEdit (c, {}, 0.75, 2.0);
    const auto base = issue14::pitchAt (still, 0.5);
    auto bendTo = [&] (float wheel, float range) {
        const auto out = pzwheel173d::renderEdit (c, [wheel, range] (ChannelDef& d) {
            modelparams::setRawValue (d, 78, range);
            modelparams::setRawValue (d, poizone::bendWheelParam, wheel);
        }, 0.75, 2.0);
        return issue14::cents (base, issue14::pitchAt (out, 1.25));
    };
    const auto up = bendTo (1.0f, 1.0f), down = bendTo (0.0f, 1.0f), wide = bendTo (1.0f, 11.0f), centre = bendTo (0.5f, 1.0f);
    std::printf ("  PoiZone bend wheel: up %+.0f down %+.0f range 11 %+.0f centre %+.0f cents\n", up, down, wide, centre);
    CHECK (std::abs (up - 200.0) < 30.0);    // the held note follows the wheel at once
    CHECK (std::abs (down + 200.0) < 30.0);
    CHECK (std::abs (wide - 1200.0) < 30.0);
    CHECK (std::abs (centre) < 30.0);
    // the mod wheel: MW amount on the cutoff (destination 0) does nothing at wheel 0, and opens the filter at wheel 1
    // (the cutoff is lowered first: the Default preset's filter is already near its top, where the wheel is clamped)
    CHECK (modelparams::setRawValue (c, 43, 1.0f) && modelparams::setRawValue (c, 71, 0.0f) && modelparams::setRawValue (c, 18, 0.3f));
    const auto plain = pzwheel173d::renderEdit (c, {}, 0.75, 2.0);
    const auto moved = pzwheel173d::renderEdit (c, [] (ChannelDef& d) { modelparams::setRawValue (d, poizone::modWheelParam, 1.0f); }, 0.75, 2.0);
    double diff = 0.0, energy = 0.0;
    for (std::size_t i = (std::size_t) (1.0 * 48000.0); i < moved.size() && i < plain.size(); ++i)
        diff += std::pow (moved[i] - plain[i], 2.0), energy += std::pow (plain[i], 2.0);
    CHECK (energy > 0.0 && diff > energy * 1e-3);
    auto amountOff = c; // (amount at its centre = 0: the wheel does nothing)
    CHECK (modelparams::setRawValue (amountOff, 43, 0.5f));
    const auto offPlain = pzwheel173d::renderEdit (amountOff, {}, 0.75, 2.0);
    const auto offMoved = pzwheel173d::renderEdit (amountOff, [] (ChannelDef& d) { modelparams::setRawValue (d, poizone::modWheelParam, 1.0f); }, 0.75, 2.0);
    CHECK (offPlain == offMoved);
    // automation reaches the wheels too (setParameter, 0…1 over their range)
    auto inst = makePoiZoneInstrument();
    inst->prepare (48000.0);
    inst->configure (c, 120.0);
    inst->setParameter (poizone::bendWheelParam, 1.0f);
    std::vector<float> l (512), rr (512);
    inst->render (l.data(), rr.data(), 512, 1.0f, 1.0f);
    CHECK (poizone::wheelsOf (c).second == 0.5f); // (automation leaves the saved wheel alone)
    // saved with the channel: modelParams { mod wheel, pitch bend }
    CHECK (modelparams::setRawValue (c, poizone::bendWheelParam, 0.25f));
    CHECK ((c.modelParams == std::vector<float> { 0.0f, 0.25f }));
}
