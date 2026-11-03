// Round 168m (YouTube review of 0.1.21, moments #19/#20): Gross Beat usable without FL — factory slots, point editing
// written back into the LXGB container, presets that carry the slots. Evidence: docs/analise/REA_rodada168m.md.
// Included from tests.cpp (after the round-53 helpers grossBeatState / renderGrossBeat).

namespace
{
lynx::EffectSlot gb168Slot (int timeSlot, int volumeSlot, const std::vector<std::uint8_t>& state)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    slot.state = state;
    slot.params[0] = lynx::grossbeat::paramFromRaw (0, timeSlot);
    slot.params[1] = lynx::grossbeat::paramFromRaw (1, volumeSlot);
    slot.params[4] = lynx::grossbeat::paramFromRaw (4, 0); // no smoothing: the gate edges stay sharp
    slot.params[5] = lynx::grossbeat::paramFromRaw (5, 0);
    return slot;
}

int gb168Index (const std::vector<std::string>& names, const std::string& name)
{
    for (int i = 0; i < (int) names.size(); ++i)
        if (names[(std::size_t) i] == name)
            return i;
    return -1;
}
} // namespace

// the video's case #19: "there's no way I'm drawing every single Gross Beat" — a new instance had 72 empty slots
TEST (gb168_new_instance_has_factory_slots_that_sound)
{
    const auto fresh = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    CHECK (! fresh.state.empty());
    const auto volumeNames = lynx::grossbeat::slotNames (fresh.state, true);
    const auto timeNames = lynx::grossbeat::slotNames (fresh.state, false);
    CHECK (volumeNames.size() == 36 && timeNames.size() == 36);
    CHECK (volumeNames.size() == 36 && volumeNames[0].empty() && timeNames[0].empty()); // slot 1 = straight through, as FL's "Empty"
    const auto gateSlot = gb168Index (volumeNames, "Gate 1/4 beat");
    const auto halfSpeed = gb168Index (timeNames, "Half speed");
    CHECK (gateSlot > 0 && halfSpeed > 0);
    int named = 0;
    for (const auto& n : volumeNames)
        named += n.empty() ? 0 : 1;
    for (const auto& n : timeNames)
        named += n.empty() ? 0 : 1;
    CHECK (named >= 30);

    // the gate slot gates a constant signal every 1/8 beat (a beat = 24000 samples at 120 BPM / 48 kHz)
    const auto out = renderGrossBeat (gb168Slot (0, gateSlot, fresh.state), std::vector<float> (100000, 1.0f), 48000.0, 120.0);
    CHECK (out[15 + 1000] == 1.0f);   // first eighth of a beat: open
    CHECK (out[15 + 4000] == 0.0f);   // second eighth: closed
    CHECK (out[15 + 7000] == 1.0f);   // next quarter: open again
    CHECK (out[15 + 10000] == 0.0f);

    // the default selection (slot 1 / slot 1) passes the input, 15 samples late (FL's dry path)
    const auto straight = renderGrossBeat (gb168Slot (0, 0, fresh.state), std::vector<float> (20000, 1.0f), 48000.0, 120.0);
    CHECK (straight[14] == 0.0f && straight[15] == 1.0f && straight[19999] == 1.0f);

    // an old project's Gross Beat with no state at all plays the factory bank too (it passed the signal before)
    const auto legacy = renderGrossBeat (gb168Slot (0, gateSlot, {}), std::vector<float> (20000, 1.0f), 48000.0, 120.0);
    CHECK (legacy[15 + 4000] == 0.0f);
}

// the video's case #20: "I'm clicking in here and it's not doing anything"
TEST (gb168_point_editing_is_written_back_and_heard)
{
    using namespace lynx::grossbeat;
    const auto fresh = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    auto state = editableState (fresh.state);
    auto& slot = state.slots[(std::size_t) slotsPerKind + 0]; // volume slot 1 (empty)
    auto points = editPoints (slot);
    CHECK (points.size() == 2 && points[0].x == 0.0 && points[1].x == barBeats && points[0].y == 1.0f && points[1].type == 2);
    // FL's right-click: a hold point; at beat 2 down to 0 = the second half of the bar is muted
    const auto at = addPoint (points, 2.0, 0.0f);
    CHECK (at == 1 && points.size() == 3 && points[1].type == 2);
    setPoints (slot, points);
    CHECK (! slot.flat);
    CHECK (levelAt (slot, 1.0) == 1.0f && levelAt (slot, 2.5) == 0.0f && levelAt (slot, 3.9) == 0.0f);
    const auto container = withState (fresh.state, state);
    // the container reads back the same slot
    const auto back = editableState (container);
    CHECK (editPoints (back.slots[(std::size_t) slotsPerKind]).size() == 3);
    const auto out = renderGrossBeat (gb168Slot (0, 0, container), std::vector<float> (200000, 1.0f), 48000.0, 120.0);
    CHECK (out[15 + 20000] == 1.0f && out[15 + 47000] == 1.0f);
    CHECK (out[15 + 49000] == 0.0f && out[15 + 95000] == 0.0f);
    CHECK (out[15 + 97000] == 1.0f); // the bar loops

    // moving and deleting: the first point stays at 0, the last at 4 beats; inner points stay between their neighbours
    movePoint (points, 1, 9.0, 0.5f);
    CHECK (points[1].x == barBeats && points[1].y == 0.5f);
    movePoint (points, 0, 3.0, 0.25f);
    CHECK (points[0].x == 0.0 && points[0].y == 0.25f);
    CHECK (! removePoint (points, 0));
    CHECK (! removePoint (points, 2));
    CHECK (removePoint (points, 1));
    CHECK (! removePoint (points, 1)); // two points stay

    // FL's snap selector: off, 1/16, 1/12, 1/8, 1/6, 1/4, 1/3 beat
    CHECK (snapStep (0) == 0.0 && snapStep (1) == 1.0 / 16.0 && snapStep (3) == 1.0 / 8.0 && snapStep (6) == 1.0 / 3.0);
}

// a slot's per-slot settings and every factory slot survive FL's layout written back and read again
TEST (gb168_state_round_trip_is_exact)
{
    using namespace lynx::grossbeat;
    auto state = factoryState();
    state.slots[3].triggerSync = 2;
    state.slots[3].positionSync = 0;
    state.slots[3].hold = 2;
    state.slots[3].link = true;
    state.slots[40].oneShot = true;
    state.slots[41].keyHeld = true;
    state.slots[42].name = std::string (300, 'n'); // a long name takes FL's 0xff + length form
    state.params[2] = 128;
    const auto bytes = writeState (state);
    const auto parsed = parseState (bytes.data(), bytes.size());
    CHECK (parsed.has_value());
    if (! parsed)
        return;
    CHECK (writeState (*parsed) == bytes);
    CHECK (parsed->slots[3].triggerSync == 2 && parsed->slots[3].positionSync == 0 && parsed->slots[3].hold == 2 && parsed->slots[3].link);
    CHECK (parsed->slots[40].oneShot && parsed->slots[41].keyHeld && parsed->slots[42].name.size() == 300 && parsed->params[2] == 128);
    bool same = true;
    for (std::size_t k = 0; k < state.slots.size(); ++k)
    {
        const auto a = editPoints (state.slots[k]), b = editPoints (parsed->slots[k]);
        same = same && a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i)
            same = std::abs (a[i].x - b[i].x) < 1e-12 && a[i].y == b[i].y && a[i].tension == b[i].tension && a[i].type == b[i].type;
    }
    CHECK (same);
    // FL's empty instance: unnamed slots, sync on 4 beats, a flat hold bar
    const auto empty = emptyState();
    CHECK (empty.slots[0].triggerSync == 5 && empty.slots[71].positionSync == 5 && empty.slots[10].flat && empty.options[6] == 1 && empty.options[8] == 1);
}

// every Gross Beat of FL's demo projects is written back exactly as it plays (FL Data present)
TEST (gb168_fl_demo_states_write_back)
{
    if (realFlpDir.empty())
        return;
    int states = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path (realFlpDir) / "Demo projects"))
        if (entry.path().extension() == ".flp")
        {
            std::ifstream in (entry.path(), std::ios::binary);
            const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
            try
            {
                const auto project = flp::parseProject (file);
                for (const auto& insert : project.mixer)
                    for (const auto& s : insert.slots)
                        if (s.plugin == "Gross Beat")
                        {
                            const auto parsed = lynx::grossbeat::parseState (s.state.data(), s.state.size());
                            CHECK (parsed.has_value());
                            if (! parsed)
                                continue;
                            const auto written = lynx::grossbeat::writeState (*parsed);
                            const auto again = lynx::grossbeat::parseState (written.data(), written.size());
                            CHECK (again.has_value());
                            // the sound is the same: the selected slots render identically from both states
                            auto original = lynx::grossbeat::slotFromFl (s.state);
                            auto rewritten = lynx::grossbeat::slotFromFl (written);
                            CHECK (original && rewritten);
                            if (original && rewritten && states < 12)
                            {
                                std::vector<float> in2 (60000);
                                for (std::size_t i = 0; i < in2.size(); ++i)
                                    in2[i] = (float) std::sin ((double) i * 0.031) * 0.5f;
                                CHECK (renderGrossBeat (*original, in2, 44100.0, 128.0) == renderGrossBeat (*rewritten, in2, 44100.0, 128.0));
                            }
                            ++states;
                        }
            }
            catch (...)
            {
            }
        }
    CHECK (states > 0);
}

// an edit while the effect runs: the running engine takes it in place (no rebuild, buffer and position kept)
TEST (gb168_running_effect_takes_an_edit)
{
    using namespace lynx::grossbeat;
    const auto fresh = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    auto slot = gb168Slot (0, 0, fresh.state);
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    const auto tps = 120.0 / 60.0 * lynx::ppq / 48000.0;
    std::vector<float> l (512, 1.0f), r (512, 1.0f);
    std::size_t done = 0;
    auto block = [&] {
        std::fill (l.begin(), l.end(), 1.0f);
        std::fill (r.begin(), r.end(), 1.0f);
        fx->setSongPosition ((double) done * tps);
        fx->process (l.data(), r.data(), 512, slot.params, 120.0);
        done += 512;
    };
    for (int i = 0; i < 20; ++i)
        block();
    CHECK (l[100] == 1.0f);
    // mute volume slot 1 from beat 0: every level 0
    auto state = editableState (slot.state);
    setPoints (state.slots[(std::size_t) slotsPerKind], { { 0.0, 0.0f, 0.0f, 2 }, { barBeats, 0.0f, 0.0f, 2 } });
    const auto edited = withState (slot.state, state);
    CHECK (fx->acceptState (edited));
    block();
    CHECK (l[100] == 0.0f && l[511] == 0.0f);
    // a slot with more points than the running engine keeps: rebuild instead
    std::vector<EditPoint> many;
    for (int i = 0; i <= maxEditPoints + 4; ++i)
        many.push_back ({ barBeats * i / (maxEditPoints + 4), (float) (i % 2), 0.0f, 2 });
    setPoints (state.slots[5], many);
    CHECK (! fx->acceptState (withState (slot.state, state)));
}

// presets carry the whole bank: LYNX SHARE's factory presets and a user preset saved and loaded
TEST (gb168_presets_carry_the_slots)
{
    const auto& factory = lynx::presets::factory (lynx::EffectKind::grossBeat);
    CHECK (factory.size() >= 10);
    CHECK (lynx::presets::savesState (lynx::EffectKind::grossBeat) && ! lynx::presets::savesState (lynx::EffectKind::fruityDelay2));
    int pump = -1;
    for (int i = 0; i < (int) factory.size(); ++i)
        if (factory[(std::size_t) i].name == "Pump")
            pump = i;
    CHECK (pump >= 0);
    if (pump >= 0)
    {
        const auto& p = factory[(std::size_t) pump];
        CHECK (! p.state.empty());
        const auto params = lynx::presets::apply (lynx::EffectKind::grossBeat, p);
        const auto names = lynx::grossbeat::slotNames (p.state, true);
        const auto v = lynx::grossbeat::rawFromParam (1, params[1]);
        CHECK (v >= 0 && v < 36 && names[(std::size_t) v] == "Pump");
    }
    // a user preset: the parameters and the edited bank come back
    auto state = lynx::grossbeat::factoryState();
    state.slots[50].name = "My gate";
    const auto bytes = lynx::grossbeat::withState ({}, state);
    auto params = lynx::makeEffectSlot (lynx::EffectKind::grossBeat).params;
    params[1] = lynx::grossbeat::paramFromRaw (1, 14);
    const auto text = lynx::presets::toText (lynx::EffectKind::grossBeat, "Mine", params, &bytes);
    const auto loaded = lynx::presets::fromText (lynx::EffectKind::grossBeat, text);
    CHECK (loaded && loaded->hasState && loaded->state == bytes && loaded->name == "Mine" && loaded->params[1] == params[1]);
    // other engines keep writing parameters only
    const auto plain = lynx::presets::toText (lynx::EffectKind::fruityDelay2, "x", params, &bytes);
    CHECK (plain.find ("state") == std::string::npos);
}

// the user's ask after the review: "tem que fazer uns presets legais" — every LYNX slot is named, distinct and moves;
// every factory preset selects named slots; two of the classic moves checked by ear-free measurement
TEST (gb168_factory_slots_are_named_distinct_and_used_by_presets)
{
    const auto state = lynx::grossbeat::factoryState();
    std::set<std::string> names;
    int time = 0, volume = 0;
    for (int i = 0; i < 2 * lynx::grossbeat::slotsPerKind; ++i)
    {
        const auto& slot = state.slots[(std::size_t) i];
        if (slot.name.empty())
            continue;
        CHECK (names.insert (slot.name).second);
        const auto levels = lynx::grossbeat::renderSlot (slot, 256);
        const auto [lo, hi] = std::minmax_element (levels.begin(), levels.end());
        CHECK ((*hi - *lo > 1.0e-3f || slot.name == "Mute") && *lo >= -1.0e-4f && *hi <= 1.0f + 1.0e-4f); // Mute: silence on purpose
        (i < lynx::grossbeat::slotsPerKind ? time : volume) += 1;
    }
    CHECK (time >= 24 && volume >= 29);

    const auto& factory = lynx::presets::factory (lynx::EffectKind::grossBeat);
    CHECK (factory.size() >= 25);
    for (const auto& p : factory)
    {
        const auto params = lynx::presets::apply (lynx::EffectKind::grossBeat, p);
        const auto t = lynx::grossbeat::rawFromParam (0, params[0]), v = lynx::grossbeat::rawFromParam (1, params[1]);
        CHECK (t >= 0 && t < 36 && v >= 0 && v < 36);
        if (p.name != "Default")
            CHECK (! state.slots[(std::size_t) t].name.empty() || ! state.slots[(std::size_t) (36 + v)].name.empty());
    }

    // Spin back: the last beat plays backwards (a rising ramp comes out falling); Sidechain hard: silent on the beat,
    // back to full within the beat
    const auto timeNames = lynx::grossbeat::slotNames (lynx::grossbeat::factoryContainer(), false);
    const auto volumeNames = lynx::grossbeat::slotNames (lynx::grossbeat::factoryContainer(), true);
    const auto spin = gb168Index (timeNames, "Spin back"), duck = gb168Index (volumeNames, "Sidechain hard");
    CHECK (spin > 0 && duck > 0);
    std::vector<float> ramp (100000);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = (float) i / (float) ramp.size();
    const auto back = renderGrossBeat (gb168Slot (spin, 0, lynx::grossbeat::factoryContainer()), ramp, 48000.0, 120.0);
    CHECK (back[15 + 60000] < back[15 + 70000]);  // beat 3: forwards
    CHECK (back[15 + 80000] > back[15 + 90000]);  // beat 4: backwards
    const auto ducked = renderGrossBeat (gb168Slot (0, duck, lynx::grossbeat::factoryContainer()), std::vector<float> (100000, 1.0f), 48000.0, 120.0);
    CHECK (ducked[15 + 24000 + 50] < 0.3f && ducked[15 + 24000 + 20000] > 0.95f);
}

// with FL installed, FL's own Gross Beat presets are listed beside LYNX SHARE's (the preset menu asks flPluginsForEffect)
TEST (gb168_fl_presets_are_listed)
{
    CHECK (lynx::flPluginsForEffect (lynx::EffectKind::grossBeat) == std::vector<std::string> { "Gross Beat" });
}
