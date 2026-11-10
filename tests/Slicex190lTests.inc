// Round 190l: Slicex as a channel (core/SlicexInstrument.h): the instrument on the engine, the rack and the FLP import
#include "core/FlResampler.h"
#include "core/SlicexEngine.h"
#include "core/SlicexInstrument.h"
#include <cstring>

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

// ---- slice 2: what FL's renders of edited states settled (docs/analise/REA_rodada190l.md)
namespace slicex190l
{
// the engine on the four-region sample, with the state edited by `edit` and optional keys per region
struct Rig
{
    std::shared_ptr<lynx::SampleData> audio = sample();
    lynx::slicex::State state = lynx::slicex::fresh();
    lynx::slicex::Engine engine;

    Rig()
    {
        lynx::slicex::setDeckFile (state, 0, "a.wav", 4);
        engine.setRate (44100, false);
        engine.setTempo (120.0f);
    }

    void load (std::vector<int> keys = {})
    {
        lynx::slicex::DeckAudio a;
        a.owner = audio, a.left = audio->leftData(), a.frames = (std::int64_t) audio->frames();
        a.regions = lynx::slicex::regionsFromMarkers (audio->markerStarts, a.frames);
        for (std::size_t i = 0; i < keys.size() && i < a.regions.size(); ++i)
            a.regions[i].key = keys[i];
        engine.setProgram (lynx::slicex::makeProgram (state, a, {}));
    }

    int play (int key, float velocity = 0.78125f)
    {
        lynx::slicex::Note n;
        n.key = key, n.velocity = velocity, n.pitchCents = (float) (100 * (key - 60));
        return engine.trigger (n);
    }

    std::vector<float> render (int frames)
    {
        std::vector<float> l ((std::size_t) frames, 0.0f), r ((std::size_t) frames, 0.0f);
        engine.render (l.data(), r.data(), frames);
        return l;
    }
};
} // namespace slicex190l

TEST (slicex190l_a_region_that_ends_holds_its_last_frame_into_the_declick)
{
    // FL's renders of regions that end before their note (a start offset, a raised pitch): the output does not drop
    // to zero at the region's last frame, the closing declick fades that level out over 20 ms
    slicex190l::Rig rig;
    rig.state.setOptionByte (lynx::slicex::oStartWholeLength, 1);
    for (auto& r : rig.state.decks[0].regions)
        r[lynx::slicex::rStart] = 6400; // the second half of each region: 5512 frames
    rig.load();
    const int base = lynx::slicex::autoBaseKey (4);
    CHECK (rig.play (base + 1) >= 0); // level 0.2
    const auto out = rig.render (8000);
    const float level = out[5000];
    CHECK (level > 0.05f);
    CHECK (std::abs (out[5511] - level) < 1.0e-4f);
    CHECK (std::abs (out[5513] - level) < level * 0.02f);  // just past the end: still (almost) the held level
    CHECK (out[5540] > level * 0.5f);
    CHECK (std::abs (out[5513 + 882 + 48]) < 1.0e-6f);     // 20 ms later: silent
    CHECK (rig.engine.idle());
}

TEST (slicex190l_a_cut_voice_keeps_its_envelope_level)
{
    // FL's render with a cut group and a volume envelope: the voice that is cut fades from the level its envelope had
    // reached (it used to jump back to the level at the start of the envelope's segment)
    slicex190l::Rig rig;
    for (auto& r : rig.state.decks[0].regions)
        r[lynx::slicex::rCutGroup] = 1;
    auto& env = rig.state.groups[0].art[lynx::slicex::volume].envelope; // FL's default: 1 → 0 over half a second
    env.enabled = -1;
    env.sustain = -1;
    rig.load();
    const int base = lynx::slicex::autoBaseKey (4);
    CHECK (rig.play (base + 3) >= 0); // level 0.4
    const auto first = rig.render (9600);
    const float before = std::abs (first[9599]);
    CHECK (before > 0.01f && before < std::abs (first[100]) * 0.8f); // the envelope has come down
    CHECK (rig.play (base) >= 0);     // level 0.1, cuts the first
    const auto after = rig.render (2000);
    // the two voices together: the cut voice from the level it had (0.115), the new one at its own (0.1); before the fix
    // the cut voice jumped back to 0.4
    float peak = 0.0f;
    for (int i = 0; i < 900; ++i)
        peak = std::max (peak, std::abs (after[(std::size_t) i]));
    CHECK (peak < before + 0.105f);
    CHECK (std::abs (after[0] - (before + 0.1f)) < 0.005f);
}

TEST (slicex190l_global_lfo_phase_follows_the_song)
{
    // FL's render of the default LFO (tempo-based, global phase) on the volume: the LFO is where it would be had it
    // started with the song; with a local phase every note starts it over
    auto level = [] (int flags, double beats) {
        slicex190l::Rig rig;
        auto& lfo = rig.state.groups[0].art[lynx::slicex::volume].lfo;
        lfo.enabled = -1;
        lfo.flags = flags;
        rig.load();
        rig.engine.setSongPosition (beats, beats * 0.5);
        rig.play (lynx::slicex::autoBaseKey (4) + 3);
        return rig.render (512)[400];
    };
    // the default speed's period is 8 beats (REA_rodada190h §4.1): a quarter and three quarters of it are the extremes
    const float local0 = level (1, 0.0), local2 = level (1, 2.0);
    CHECK (local0 > 0.01f && std::abs (local0 - local2) < 1.0e-6f);
    const float global0 = level (3, 0.0), global2 = level (3, 2.0), global6 = level (3, 6.0), global8 = level (3, 8.0);
    CHECK (std::abs (global0 - local0) < 1.0e-6f);
    CHECK (std::abs (global2 - global6) > 0.02f);
    CHECK (std::abs (global8 - global0) < 1.0e-4f);
}

TEST (slicex190l_song_time_layering_plays_the_layer_the_song_is_in)
{
    slicex190l::Rig rig;
    rig.state.setOptionInt (lynx::slicex::oLayering, 7);
    rig.state.setOptionInt (lynx::slicex::oCrossfade, 0);
    // the sample is one bar: 4 beats over its second
    const double beats[2] = { 0.0, 4.0 };
    std::memcpy (rig.state.decks[0].params.data() + 0x10, &beats[0], 8);
    std::memcpy (rig.state.decks[0].params.data() + 0x18, &beats[1], 8);
    rig.load ({ 60, 60, 60, 60 }); // all four regions on one key
    auto at = [&] (double beat) {
        rig.engine.killAll();
        rig.engine.setSongPosition (beat, 0.0);
        rig.play (60);
        return rig.render (512)[400];
    };
    const float q0 = at (0.1), q1 = at (1.1), q2 = at (2.1), q3 = at (7.1); // (7.1: the sample loops over the song)
    CHECK (q0 > 0.005f);
    CHECK (std::abs (q1 / q0 - 2.0f) < 0.05f);
    CHECK (std::abs (q2 / q0 - 3.0f) < 0.05f);
    CHECK (std::abs (q3 / q0 - 4.0f) < 0.05f);
}
