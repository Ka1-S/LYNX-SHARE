// Round 190ix: Tetra (core/FmSynth.h) follows FL's host pitch features as the other LYNX generators do: a slide note
// moves the held voices (issue #14), a Porta channel starts a note at the last one's pitch and glides (round 168l), a
// Mono channel moves its voice legato. The FL-Data audits (issue14_pitch_audit_of_the_instruments,
// mono168l_every_engine_plays_mono_legato_and_porta, ctlwake173d) check the same with every engine; these run without
// FL's Data folder (CI). Uses issue14::renderPattern / pitchAt / cents.

namespace tetra190ix
{
inline lynx::ChannelDef channel()
{
    lynx::ChannelDef c;
    c.name = "Tetra";
    c.instrument = lynx::InstrumentKind::tetra;
    return c;
}
} // namespace tetra190ix

// a slide note (C5 held, G5 slide note at beat 1 for a quarter beat): the held voice ends 7 semitones up, keeps sounding
TEST (tetra190ix_follows_slide_notes)
{
    using namespace lynx;
    const auto c = tetra190ix::channel();
    Note held;
    held.key = 60;
    held.length = ppq * 8;
    held.velocity = 0.8f;
    Note slide;
    slide.key = 67;
    slide.start = ppq;
    slide.length = ppq / 4;
    slide.slide = true;
    auto fifth = held;
    fifth.key = 67;
    const auto base = issue14::pitchAt (issue14::renderPattern (c, { held }, 0.0f, 1.5), 1.0);
    const auto ref = issue14::pitchAt (issue14::renderPattern (c, { fifth }, 0.0f, 1.5), 1.0);
    const auto out = issue14::renderPattern (c, { held, slide }, 0.0f, 1.5);
    CHECK (base > 200.0 && std::abs (issue14::cents (base, ref) - 700.0) < 10.0);
    CHECK (std::abs (issue14::cents (ref, issue14::pitchAt (out, 1.0))) < 10.0);
    CHECK (std::abs (issue14::cents (base, issue14::pitchAt (out, 0.3))) < 10.0); // (before the slide note)
    // half-way through the slide (beat 1 + 1/8 = 0.5625 s; the window is 85 ms): between the two
    const auto mid = issue14::cents (base, issue14::pitchAt (out, 0.53));
    CHECK (mid > 100.0 && mid < 650.0);
    // the main pitch and the channel's PITCH knob still move a slid voice
    const auto master = issue14::pitchAt (issue14::renderPattern (c, { held, slide }, 100.0f, 1.5), 1.0);
    CHECK (std::abs (issue14::cents (ref, master) - 100.0) < 10.0);
}

// FL's channel porta and mono on Tetra (round 168l's laws): the voice takes the rack's glide at note on, glides from the
// last note over the slide time and stays; a Mono channel moves its voice to the second note without a new attack
TEST (tetra190ix_follows_channel_porta_and_mono)
{
    using namespace lynx;
    auto inst = makeInstrument (InstrumentKind::tetra);
    CHECK (inst != nullptr && inst->followsChannelPorta() && inst->followsSlides());
    if (inst == nullptr)
        return;
    inst->prepare (48000.0);
    inst->configure (tetra190ix::channel(), 120.0);
    HostSlide::pendingGlide() = { -700.0f, 1000.0f, false };
    inst->setNextNoteExpression (0.0f, -700.0f, false);
    inst->noteOn (60, 0.8f);
    CHECK (HostSlide::pendingGlide().taken);
    HostSlide::pendingGlide() = {};

    const auto c = tetra190ix::channel();
    Note low;
    low.key = 60;
    low.velocity = 0.8f;
    auto high = low;
    high.key = 67;
    auto held = low;
    held.key = 67;
    held.length = ppq * 8;
    const auto ref = issue14::pitchAt (issue14::renderPattern (c, { held }, 0.0f, 1.6), 1.0);
    // Porta: C5 for half a beat, G5 a beat later with a slide of 1 beat (FL: 97/96 beat ≈ 0.505 s at 120 BPM)
    auto porta = c;
    setChannelPortamento (porta, true, 1.0f);
    low.length = ppq / 2;
    high.start = ppq;
    high.length = ppq * 3;
    const auto glide = issue14::renderPattern (porta, { low, high }, 0.0f, 1.6);
    const auto mid = issue14::cents (ref, issue14::pitchAt (glide, 0.6)), end = issue14::cents (ref, issue14::pitchAt (glide, 1.2));
    CHECK (mid > -620.0 && mid < -250.0);
    CHECK (std::abs (end) < 10.0);
    // Mono: C5 for 2 beats, G5 from beat 1: the voice moves to G5 at 0.5 s
    auto mono = c;
    setChannelMono (mono, true);
    low.length = ppq * 2;
    high.length = ppq * 2;
    const auto legato = issue14::renderPattern (mono, { low, high }, 0.0f, 1.6);
    CHECK (std::abs (issue14::cents (ref, issue14::pitchAt (legato, 0.3)) + 700.0) < 10.0);
    CHECK (std::abs (issue14::cents (ref, issue14::pitchAt (legato, 1.1))) < 10.0);
}

// the engine alone: a slide moves only held voices, a released voice keeps its pitch; a voice started without the rack's
// glide plays at its own pitch (the 190hs renders stay as they were)
TEST (tetra190ix_engine_slide_moves_held_voices_only)
{
    using namespace lynx;
    namespace f = fm;
    const double fs = 48000.0;
    auto s = tetra190hs::state();
    f::Engine e;
    e.prepare (fs);
    e.setState (s.data());
    const int n = 24000;
    std::vector<float> l (n), r (n);
    auto pitch = [&] {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), n, 120.0, -1.0);
        return issue14::pitchAt (l, 0.2);
    };
    e.note (60, 1.0f);
    const auto c5 = pitch();
    CHECK (std::abs (issue14::cents (261.63, c5)) < 10.0);
    e.slideTo (67.0f, 480.0f);
    CHECK (std::abs (issue14::cents (c5, pitch()) - 700.0) < 10.0);
    e.slideTo (60.0f, 1.0f);
    CHECK (std::abs (issue14::cents (c5, pitch())) < 10.0);
    // released (a long release keeps it sounding): a slide does not move it
    s[(std::size_t) (f::opBase (0) + f::op::envBase + f::env::release)] = 5000.0;
    e.setState (s.data());
    e.note (60, 0.0f);
    e.slideTo (72.0f, 1.0f);
    CHECK (std::abs (issue14::cents (c5, pitch())) < 10.0);
}
