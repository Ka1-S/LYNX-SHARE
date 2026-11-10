// Round 190l: Slicex as a channel (core/SlicexInstrument.h): the instrument on the engine, the rack and the FLP import
#include "core/FlResampler.h"
#include "core/SlicexEngine.h"
#include "core/SlicexInstrument.h"

namespace slicex190l
{
// a 1 s mono sample at 44.1 kHz with four cue points: each quarter a constant level 0.1 · (index + 1)
inline std::shared_ptr<lynx::SampleData> sample (float scale = 1.0f)
{
    auto s = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 44100; ++i)
        s->left.push_back (scale * 0.1f * (float) (i / 11025 + 1));
    s->sampleRate = 44100.0;
    s->markerStarts = { 0, 11025, 22050, 33075 };
    return s;
}

inline std::vector<std::uint8_t> state (int regionsA, int regionsB = 0)
{
    auto st = lynx::slicex::fresh();
    st.setOptionByte (lynx::slicex::oSameDeckOnly, 1); // (a note plays its own deck's regions only)
    lynx::slicex::setDeckFile (st, 0, "a.wav", regionsA);
    if (regionsB > 0)
        lynx::slicex::setDeckFile (st, 1, "b.wav", regionsB);
    return lynx::slicex::write (st);
}

inline lynx::ChannelDef channel (int regionsB = 0)
{
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::slicex;
    c.samplePath = "a.wav";
    if (regionsB > 0)
        lynx::slicex::setDeckBPath (c, "b.wav");
    c.pluginState = state (4, regionsB);
    return c;
}
} // namespace slicex190l

TEST (slicex190l_instrument_plays_what_the_engine_plays)
{
    using namespace lynx;
    const auto a = slicex190l::sample();
    const auto def = slicex190l::channel();
    auto inst = makeSlicexInstrument (a, nullptr);
    inst->prepare (44100.0);
    inst->configure (def, 120.0);
    inst->settleNew();
    CHECK (inst->idle());

    // the engine alone, with what FL's host would hand it for the same note
    slicex::Engine engine;
    engine.setRate (44100, false);
    engine.setTempo (120.0f);
    engine.setInterpolation (flrs::liveQuality);
    slicex::DeckAudio deck;
    deck.owner = a, deck.left = a->leftData(), deck.frames = (std::int64_t) a->frames();
    deck.regions = slicex::regionsFromMarkers (a->markerStarts, deck.frames);
    engine.setProgram (slicex::makeProgram (*slicex::parse (def.pluginState), deck, {}));

    const float velocity = 0.5f, gain = 0.7f;
    const int key = slicex::autoBaseKey (4) + 2; // the third region
    std::vector<float> l (4096, 0.0f), r (4096, 0.0f), el (4096, 0.0f), er (4096, 0.0f);
    inst->setNextNoteExpression (0.0f, 0.0f, false);
    inst->setNextNoteModulation (0.5f, 0.5f);
    inst->setNextNoteColour (0);
    inst->noteOn (key, velocity);
    CHECK (! inst->idle());
    inst->render (l.data(), r.data(), 4096, gain, gain);

    slicex::Note n;
    n.key = key, n.velocity = velocity;
    n.vol = (float) ((double) gain * (double) (float) ((std::pow (11.0, 1.28 * 0.5) - 1.0) / 10.0));
    n.pitchCents = (float) (100 * (key - 60));
    CHECK (engine.trigger (n) >= 0);
    engine.render (el.data(), er.data(), 4096);

    double peak = 0.0, diff = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        peak = std::max (peak, (double) std::abs (l[i]));
        diff = std::max ({ diff, (double) std::abs (l[i] - el[i]), (double) std::abs (r[i] - er[i]) });
    }
    CHECK (peak > 0.05);   // (the third region's level 0.3 at about 0.21 × 0.7, through the pan law)
    CHECK (diff < 1.0e-6);

    // note off: FL's 20 ms declick, then silence
    inst->noteOff (key);
    std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
    inst->render (l.data(), r.data(), 4096, gain, gain);
    CHECK (std::abs (l[4095]) < 1.0e-6f);
    CHECK (inst->idle());

    // a key with no region: nothing
    inst->noteOn (slicex::autoBaseKey (4) + 9, velocity);
    CHECK (inst->idle());
}

TEST (slicex190l_note_colour_picks_the_deck_and_the_state_is_taken_live)
{
    using namespace lynx;
    auto def = slicex190l::channel (4);
    auto inst = makeSlicexInstrument (slicex190l::sample(), slicex190l::sample (-1.0f));
    inst->prepare (44100.0);
    inst->configure (def, 120.0);
    inst->settleNew();
    const int key = slicex::autoBaseKey (4);
    auto play = [&] (int colour) {
        std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
        inst->setNextNoteColour (colour);
        inst->noteOn (key, 0.78125f);
        inst->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
        inst->panic();
        return l[2000];
    };
    const auto deckA = play (0), deckB = play (1);
    CHECK (deckA > 0.01f);
    CHECK (deckB < -0.01f); // (deck B's sample is deck A's inverted)
    CHECK (std::abs (deckA + deckB) < 1.0e-6f);

    // the master volume changes in the state: the next note follows without a new instrument
    auto st = *slicex::parse (def.pluginState);
    st.masters[slicex::mVolume] = 0;
    def.pluginState = slicex::write (st);
    inst->configure (def, 120.0);
    CHECK (std::abs (play (0)) < 1.0e-6f);
    inst->configure (def, 120.0); // (frees the retired program on this thread)

    // "Link velocity to volume" off: every velocity at the same level
    st.masters[slicex::mVolume] = 12800;
    st.setOptionByte (slicex::oVelocityToVolume, 0);
    def.pluginState = slicex::write (st);
    inst->configure (def, 120.0);
    auto level = [&] (float velocity) {
        std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
        inst->noteOn (key, velocity);
        inst->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
        inst->panic();
        return l[2000];
    };
    const auto soft = level (0.2f), loud = level (1.0f);
    CHECK (soft > 0.01f && std::abs (soft - loud) < 1.0e-6f);
}

TEST (slicex190l_flp_import_makes_a_slicex_channel_with_both_decks)
{
    using namespace lynx;
    // FL's state as the importer gets it: deck A names a file, deck B holds its WAV
    auto st = slicex::fresh();
    slicex::setDeckFile (st, 0, "%FLStudioFactoryData%\\Data\\Patches\\a.wav", 4);
    std::vector<std::string> saved;
    auto existing = [] (const std::string& p) { return p.find ("a.wav") != std::string::npos ? std::string ("/found/a.wav") : std::string(); };
    auto save = [&saved] (const std::vector<std::uint8_t>&, const std::string& name) { saved.push_back (name); return "/embedded/" + name + ".wav"; };

    ChannelDef def;
    def.name = "Breaker";
    CHECK (slicex::importChannel (def, slicex::write (st), existing, save));
    CHECK (def.instrument == InstrumentKind::slicex && def.samplePath == "/found/a.wav" && slicex::deckBPath (def).empty());
    CHECK (def.pluginState == slicex::write (st));

    st.decks[1].referenceOnly = 0;
    st.decks[1].wav = { 'R', 'I', 'F', 'F' };
    ChannelDef two;
    two.name = "Breaker";
    CHECK (slicex::importChannel (two, slicex::write (st), existing, save));
    CHECK (two.samplePath == "/found/a.wav" && slicex::deckBPath (two) == "/embedded/Breaker (deck B).wav" && two.zones.size() == 1);
    CHECK (saved.size() == 1);

    // no deck's audio on this machine, or not a Slicex state: the importer keeps its fallback
    ChannelDef none;
    CHECK (! slicex::importChannel (none, slicex::write (slicex::fresh()), existing, save));
    CHECK (none.instrument != InstrumentKind::slicex && none.pluginState.empty());
    CHECK (! slicex::importChannel (none, { 1, 2, 3 }, existing, save));

    // in the rack: the channel sounds through RackState's instrument factory
    const auto a = slicex190l::sample();
    auto channel = slicex190l::channel();
    auto inst = makeInstrument (InstrumentKind::slicex, a);
    inst->prepare (44100.0);
    inst->configure (channel, 120.0);
    std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
    inst->noteOn (slicex::autoBaseKey (4), 0.78125f);
    inst->render (l.data(), r.data(), 1024, 1.0f, 1.0f);
    CHECK (l[1000] > 0.01f);
}
