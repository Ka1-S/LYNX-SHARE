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
