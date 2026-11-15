// Round 190ih: behaviour details from the 190if audit (PLAN R2, R3, R4, R7, R9, R12, R14).

// R7 (DP-68): the Envelope Controller publishes the last key and velocity it received (outputs 8 / 9) for the
// editor's Keyboard / Velocity mapping line, as FL's manual describes ("the vertical white line ... the last note/key
// received"); before any note both are -1, and outputs 0 … 7 are unchanged
TEST (details190ih_envelope_controller_last_key_and_velocity)
{
    lynx::EnvelopeControllerData d;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::controller;
    c.controllerState = lynx::writeEnvelopeController (d);
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::controller);
    inst->prepare (48000.0);
    inst->configure (c, 120.0);
    std::vector<float> l (480), r (480);
    inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
    CHECK (inst->controlOutput (8) == -1.0f);
    CHECK (inst->controlOutput (9) == -1.0f);
    const auto before = inst->controlOutput (0);
    inst->noteOn (64, 0.75f);
    CHECK (inst->controlOutput (8) == 64.0f);
    CHECK (inst->controlOutput (9) == 0.75f);
    inst->noteOff (64);
    inst->noteOn (36, 0.25f);
    CHECK (inst->controlOutput (8) == 36.0f); // the last note wins
    CHECK (inst->controlOutput (9) == 0.25f);
    inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
    CHECK (std::isfinite (before) && std::isfinite (inst->controlOutput (0)));
}

// R2 (DP-1/2/3, DC-1/2): Gross Beat publishes its play state for the editor's locator and slot grids. FL's manual:
// the locator is "visible only when the host is playing"; "Green rectangle = Queued for playback. Green filled =
// Playing". With POS SYNC 4 beats the envelope's position is the song's beat within the 4-beat bar.
TEST (details190ih_gross_beat_publishes_its_play_position)
{
    using namespace lynx::grossbeat;
    const auto fresh = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::grossBeat);
    slot.state = fresh.state;
    slot.params[0] = paramFromRaw (0, 0);
    slot.params[1] = paramFromRaw (1, 0);
    auto fx = lynx::makeEffect (slot);
    CHECK (fx != nullptr);
    if (fx == nullptr)
        return;
    fx->prepare (48000.0);
    const auto tps = 120.0 / 60.0 * lynx::ppq / 48000.0; // ticks per sample at 120 BPM
    std::vector<float> l (480), r (480);
    std::size_t done = 0;
    auto block = [&] (bool moving) {
        std::fill (l.begin(), l.end(), 0.5f);
        std::fill (r.begin(), r.end(), 0.5f);
        fx->setSongPosition ((double) done * tps);
        fx->process (l.data(), r.data(), 480, slot.params, 120.0);
        if (moving)
            done += 480;
    };
    CHECK (fx->controlOutput (0) == 0.0f); // nothing published before it plays
    double worst = 0.0;
    for (int b = 0; b < 600; ++b) // 6 s = 12 beats: three passes of the 4-beat envelope
    {
        block (true);
        const auto beats = (double) done * tps / lynx::ppq;
        const auto expect = std::fmod (beats, 4.0) / 4.0;
        auto d = std::abs ((double) fx->controlOutput (1) - expect);
        d = std::min (d, 1.0 - d); // (at the wrap 0.999 and 0.0 are neighbours)
        if (b > 0)
            worst = std::max (worst, d);
        CHECK (fx->controlOutput (0) == 1.0f);
        CHECK (fx->controlOutput (3) == 1.0f && fx->controlOutput (4) == 1.0f); // slot 0 of each grid plays (+1)
    }
    CHECK (worst < 0.01); // within 1 % of the bar (the attack compensation runs a hair ahead)
    // the volume envelope moves with the same position sync
    CHECK (std::abs (fx->controlOutput (2) - fx->controlOutput (1)) < 0.01f);

    // a click on another time slot with a trigger sync (the factory's 6th time slot: 4 beats) queues it until then;
    // then it plays (a slot whose TRIG SYNC is off would switch at once)
    for (int b = 0; b < 50; ++b) // (one beat on: off the bar line, where the sync would switch at once)
        block (true);
    slot.params[0] = paramFromRaw (0, 5);
    block (true);
    CHECK (fx->controlOutput (5) == 6.0f); // queued: slot 5
    CHECK (fx->controlOutput (3) == 1.0f); // still playing slot 0
    for (int b = 0; b < 200 && fx->controlOutput (5) != 0.0f; ++b)
        block (true);
    CHECK (fx->controlOutput (5) == 0.0f);
    CHECK (fx->controlOutput (3) == 6.0f);

    // stopped (the song position holds): not playing, so the editor draws no locator
    block (false);
    block (false);
    CHECK (fx->controlOutput (0) == 0.0f);
}
