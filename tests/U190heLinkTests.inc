// Round 190he, issue #152 (Nuelka, 0.1.27, item 2): "a MIDI link on FL-engine plugins changes the sound but the knob
// doesn't move; LYNX SHARE's own plugins can't be linked". LYNX Synth's knobs are automation targets now
// (core/SynthParams.h: right-click = FL's parameter menu, MIDI links, automation clips); LYNX Soundfont's knobs take a
// linked controller (Song::setTargetValue had no case for them, so a link moved nothing). The FL engines' knobs follow
// the channel's state and their automation in ui/ChannelSettings.cpp (ModelPanel); here the state side they read.
#include "core/SynthParams.h"
#include "core/SoundFont.h"
#include "core/LastTweaked.h"
#include "core/ModelParams.h"
#include "core/PoiZone.h"
#include "core/Instruments.h"
#include "core/RackRenderer.h"
#include "core/LinkFormula.h"
#include "core/MidiControls.h"

TEST (u190he_link_synth_param_table_follows_the_knobs)
{
    using namespace lynx;
    CHECK (synthparam::count == 30);
    for (int k = 0; k < synthparam::count; ++k)
    {
        const auto& r = synthparam::rangeOf (k);
        CHECK (r.name != nullptr && r.name[0] != 0 && r.hi > r.lo);
        CHECK (synthparam::valid (synthparam::target (k)) && synthparam::indexOf (synthparam::target (k)) == k);
        for (float v : { 0.0f, 0.25f, 0.5f, 0.9f, 1.0f })
        {
            const auto x = synthparam::toNatural (k, v);
            CHECK (x >= r.lo - 1.0e-6f && x <= r.hi + 1.0e-6f);
            if (r.step <= 0.0f) // (stepped knobs land on their steps)
                CHECK (std::abs (synthparam::toValue (k, x) - v) < 1.0e-4f);
        }
        CHECK (std::abs (synthparam::toNatural (k, 0.0f) - r.lo) < 1.0e-5f && std::abs (synthparam::toNatural (k, 1.0f) - r.hi) < 1.0e-3f);
    }
    // the page's skews: half travel is the knob's mid point (attack 0.5 s, cutoff 1 kHz, osc level 1, porta 1/4 beat)
    CHECK (std::abs (synthparam::toNatural (synthparam::attack, 0.5f) - 0.5f) < 1.0e-4f);
    CHECK (std::abs (synthparam::toNatural (synthparam::cutoff, 0.5f) - 1000.0f) < 0.05f);
    CHECK (std::abs (synthparam::toNatural (synthparam::oscParam (1, synthparam::level), 0.5f) - 1.0f) < 1.0e-4f);
    CHECK (std::abs (synthparam::toNatural (synthparam::porta, 0.5f) - 0.25f) < 1.0e-4f);
    // whole octaves and semitones, as the knobs step
    CHECK (synthparam::toNatural (synthparam::oscParam (0, synthparam::octave), 0.5f) == 0.0f);
    CHECK (synthparam::toNatural (synthparam::oscParam (0, synthparam::octave), 0.6f) == 1.0f); // (-3 + 3.6 = 0.6 -> 1)
    CHECK (synthparam::toNatural (synthparam::oscParam (2, synthparam::semitones), 1.0f) == 11.0f);
    CHECK (synthparam::oscOf (synthparam::oscParam (2, synthparam::pan)) == 3 && synthparam::oscOf (synthparam::cutoff) == 0);
    // the numbers sit apart from an FL plugin's (an FL 3x Osc converted to LYNX Synth keeps its automation dead), the
    // MIDI controls (0x1000…) and the other engines' bases
    CHECK (! synthparam::valid (5) && ! synthparam::valid (midictl::ccBase + 1) && ! synthparam::valid (midictl::pitchBendBase));
    CHECK (! synthparam::valid (synthparam::base - 1) && ! synthparam::valid (synthparam::base + synthparam::count));
}

TEST (u190he_link_synth_targets_read_and_write_the_song)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels[0].instrument = InstrumentKind::synth;
    const AutomationTarget cutoff { AutomationTargetKind::channelParam, 0, 0, synthparam::target (synthparam::cutoff) };
    CHECK (song.setTargetValue (cutoff, 0.5f));
    CHECK (std::abs (song.channels[0].synth.cutoff - 1000.0f) < 0.05f);
    CHECK (std::abs (song.targetValue (cutoff) - 0.5f) < 1.0e-4f);
    const AutomationTarget octave { AutomationTargetKind::channelParam, 0, 0, synthparam::target (synthparam::oscParam (1, synthparam::octave)) };
    CHECK (song.setTargetValue (octave, 1.0f) && song.channels[0].synth.osc[1].octave == 3);
    CHECK (std::abs (song.targetValue (octave) - 1.0f) < 1.0e-6f);
    // FM decay 0 = a constant index, as the page writes it
    song.channels[0].synth.fmSustain = 0.3f;
    CHECK (song.setTargetValue ({ AutomationTargetKind::channelParam, 0, 0, synthparam::target (synthparam::fmDecay) }, 0.0f));
    CHECK (song.channels[0].synth.fmDecay == 0.0f && song.channels[0].synth.fmSustain == 1.0f);
    // the old numbers (an FL plugin's P on a converted 3x Osc) stay no target: the synth is untouched
    const auto before = song.channels[0].synth;
    CHECK (! song.setTargetValue ({ AutomationTargetKind::channelParam, 0, 0, 5 }, 0.9f));
    CHECK (song.channels[0].synth == before);
    // another instrument's channel: not a synth target
    song.channels[0].instrument = InstrumentKind::sampler;
    CHECK (! song.setTargetValue (cutoff, 0.2f) || song.channels[0].synth == before);
    song.channels[0].instrument = InstrumentKind::synth;
    // a modulator may drive them (LYNX's own controls)
    CHECK (Modulator::canModulate (song, cutoff));
    // Last tweaked: the page's units
    CHECK (lasttweak::valueText (song, cutoff, 0.5f) == "1.0 kHz");
    CHECK (lasttweak::valueText (song, { AutomationTargetKind::channelParam, 0, 0, synthparam::target (synthparam::attack) }, 0.5f) == "0.500 s");
    CHECK (lasttweak::valueText (song, octave, 1.0f) == "+3 oct");
}

TEST (u190he_link_a_midi_link_moves_the_synth_and_its_sound)
{
    using namespace lynx;
    // the engine's link path for LYNX's own controls (AudioEngine::applyMidiLinks -> applyTargetValues): the
    // controller's position through the link's mode and formula, then Song::setTargetValue; the instrument takes the
    // Song's settings (configure) as for a knob turned by hand
    auto song = Song::makeDefault();
    auto& c = song.channels[0];
    c.instrument = InstrumentKind::synth;
    c.synth.osc[0] = { Waveform::saw, 0, 0.0f, 1.0f };
    c.synth.cutoff = 20000.0f;
    const AutomationTarget level { AutomationTargetKind::channelParam, 0, 0, synthparam::target (synthparam::oscParam (0, synthparam::level)) };
    static const linkformula::Formula oneToOne;
    auto energyOf = [] (Instrument& synth) {
        synth.allNotesOff();
        synth.noteOn (60, 0.8f);
        std::vector<float> l (4096), r (4096);
        synth.render (l.data(), r.data(), (int) l.size(), 0.7f, 0.7f);
        double e = 0.0;
        for (std::size_t i = 1024; i < l.size(); ++i)
            e += (double) l[i] * l[i];
        return e;
    };
    auto synth = makeInstrument (InstrumentKind::synth);
    synth->prepare (48000.0);
    synth->configure (c, 120.0);
    const auto open = energyOf (*synth);

    // CC value 0 on a plain link: the level knob goes to 0 and the synth is silent
    const auto v = linkformula::linkModeValue ((int) linkformula::LinkMode::standard, oneToOne, 0.0f / 127.0f, song.targetValue (level));
    CHECK (v.has_value());
    CHECK (song.setTargetValue (level, *v));
    CHECK (song.channels[0].synth.osc[0].level == 0.0f);
    CHECK (std::abs (song.targetValue (level)) < 1.0e-6f); // (what the knob shows: SynthPanel refreshes from the Song)
    synth->configure (song.channels[0], 120.0);
    const auto linked = energyOf (*synth);
    CHECK (open > 1.0);
    CHECK (linked < open * 1.0e-6);
}

TEST (u190he_link_synth_automation_plays_and_a_hand_move_takes_over)
{
    using namespace lynx;
    ChannelDef c;
    c.instrument = InstrumentKind::synth;
    c.synth.osc[0] = { Waveform::saw, 0, 0.0f, 1.0f };
    c.synth.cutoff = 20000.0f;
    auto energyOf = [] (Instrument& synth) {
        synth.allNotesOff();
        synth.noteOn (60, 0.8f);
        std::vector<float> l (4096), r (4096);
        synth.render (l.data(), r.data(), (int) l.size(), 0.7f, 0.7f);
        double e = 0.0;
        for (std::size_t i = 1024; i < l.size(); ++i)
            e += (double) l[i] * l[i];
        return e;
    };
    auto synth = makeInstrument (InstrumentKind::synth);
    synth->prepare (48000.0);
    synth->configure (c, 120.0);
    const auto open = energyOf (*synth);

    const auto level = synthparam::target (synthparam::oscParam (0, synthparam::level));
    synth->setParameter (level, 0.0f); // an automation clip at its bottom (audio thread)
    const auto automated = energyOf (*synth);
    synth->configure (c, 120.0); // the song re-applied unchanged (an edit elsewhere): the automation keeps its value
    const auto kept = energyOf (*synth);
    synth->setParameter (5, 0.0f); // (not one of the synth's numbers: ignored)
    const auto ignored = energyOf (*synth);
    c.synth.osc[0].level = 0.5f; // the knob turned by hand (or a linked controller)
    synth->configure (c, 120.0);
    const auto byHand = energyOf (*synth);

    CHECK (open > 1.0);
    CHECK (automated < open * 1.0e-6);
    CHECK (kept < open * 1.0e-6);
    CHECK (ignored < open * 1.0e-6);
    CHECK (byHand > open * 0.1 && byHand < open * 0.5);

    // an automation clip on a synth knob binds to the instrument (RackRenderer), as the Sampler's do
    auto song = Song::makeDefault();
    song.channels[0].instrument = InstrumentKind::synth;
    auto& p = song.patterns[(std::size_t) song.currentPattern];
    PatternEvents lane;
    lane.target = { AutomationTargetKind::channelParam, 0, 0, synthparam::target (synthparam::cutoff) };
    lane.events.push_back ({ 0, 0.5f });
    p.events.push_back (lane);
    auto state = RackState::build (song, nullptr, 48000.0);
    auto bound = false;
    for (const auto& b : state->pluginParams)
        bound = bound || (b.channel == 0 && b.param == synthparam::target (synthparam::cutoff));
    CHECK (bound);
    // ... and an FL plugin number on a synth channel does not
    PatternEvents dead;
    dead.target = { AutomationTargetKind::channelParam, 0, 0, 5 };
    dead.events.push_back ({ 0, 0.5f });
    p.events = { dead };
    state = RackState::build (song, nullptr, 48000.0);
    auto deadBound = false;
    for (const auto& b : state->pluginParams)
        deadBound = deadBound || (b.channel == 0 && b.param == 5);
    CHECK (! deadBound);
}

TEST (u190he_link_soundfont_knobs_take_a_linked_controller)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels[0] = sf::makeChannel();
    const AutomationTarget attack { AutomationTargetKind::channelParam, 0, 0, sf::pAttack };
    CHECK (sf::automatable (sf::pAttack) && sf::automatable (sf::pVolume) && sf::automatable (sf::pModWheel));
    CHECK (! sf::automatable (sf::pBank) && ! sf::automatable (sf::pProgram) && ! sf::automatable (sf::pReverbTrack)
           && ! sf::automatable (sf::pChorusTrack) && ! sf::automatable (sf::paramCount) && ! sf::automatable (-1));
    // before: no case in Song::setTargetValue, so AudioEngine::applyTargetValues found no plugin and nothing moved
    CHECK (song.setTargetValue (attack, 0.5f));
    const auto params = sf::readParams (song.channels[0].modelParams);
    CHECK (std::abs (params[(std::size_t) sf::pAttack] - sf::denormalise (sf::pAttack, 0.5f)) < 1.0e-6f);
    CHECK (std::abs (song.targetValue (attack) - 0.5f) < 1.0e-4f);
    // integer parameters land on whole numbers; the bank / program stay out of reach
    CHECK (song.setTargetValue ({ AutomationTargetKind::channelParam, 0, 0, sf::pTranspose }, 0.73f));
    const auto t = sf::readParams (song.channels[0].modelParams)[(std::size_t) sf::pTranspose];
    CHECK (t == std::round (t) || ! sf::paramInfo (sf::pTranspose).integer);
    const auto bankBefore = sf::readParams (song.channels[0].modelParams)[(std::size_t) sf::pBank];
    CHECK (! song.setTargetValue ({ AutomationTargetKind::channelParam, 0, 0, sf::pBank }, 1.0f));
    CHECK (sf::readParams (song.channels[0].modelParams)[(std::size_t) sf::pBank] == bankBefore);
}

TEST (u190he_link_fl_engine_knob_reads_what_a_link_wrote)
{
    using namespace lynx;
    // the FL engines' page (ModelPanel) shows modelparams::rawValue of the channel's state; a linked controller writes
    // it through Song::setTargetValue -> modelparams::setValue01, and the page now reads it again on the engine's change
    // message (it read the state only when it opened: the sound changed, the knob did not)
    auto song = Song::makeDefault();
    song.channels[0].instrument = InstrumentKind::poizone;
    song.channels[0].pluginState = poizoneDefaultState();
    const auto* p = modelparams::find (InstrumentKind::poizone, 18);
    CHECK (p != nullptr);
    if (p == nullptr)
        return;
    const auto stateBefore = song.channels[0].pluginState;
    const AutomationTarget t { AutomationTargetKind::channelParam, 0, 0, 18 };
    CHECK (song.setTargetValue (t, 0.3f));
    CHECK (song.channels[0].pluginState != stateBefore); // (the page's change test: the state differs)
    const auto raw = modelparams::rawValue (song.channels[0], 18);
    CHECK (raw.has_value() && std::abs (*raw - p->raw (0.3f)) < 1.0e-6f);
    // an automation's 0…1 value shown on the knob is the same raw value (ModelPanel::showAutomation: p->raw (v))
    CHECK (std::abs (p->normalised (p->raw (0.3f)) - 0.3f) < 1.0e-5f);
}

// Coordinator's gate (owner rule: saved projects keep sounding the same). A LYNX Modulation route on a Soundfont knob
// centred on 0.5 before round 190he (Song::targetValue had no Soundfont case); a project saved before keeps that start,
// new songs centre on the knob's real position. RackState binds the centre (ModPlay::base) once per build.
TEST (u190he_link_soundfont_modulation_of_old_projects_keeps_the_old_centre)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    CHECK (song.soundfontModBaseReal); // a new song
    song.channels[0] = sf::makeChannel();
    const AutomationTarget cutoff { AutomationTargetKind::channelParam, 0, 0, sf::pCutoff };
    CHECK (song.setTargetValue (cutoff, 0.8f));
    Modulator m;
    m.routes = { { cutoff, 0.25f } };
    song.modulators.push_back (m);
    const auto baseOf = [] (const Song& s) {
        const auto state = RackState::build (s, nullptr, 48000.0);
        return state != nullptr && state->modRoutes.size() == 1 ? state->modRoutes[0].base : -1.0f;
    };
    // new: around the knob
    CHECK (std::abs (baseOf (song) - 0.8f) < 1.0e-4f);
    CHECK (std::abs (song.modulationBase (cutoff) - song.targetValue (cutoff)) < 1.0e-6f);
    // saved before 190he: exactly 0.5 whatever the knob (what 0.1.27 bound: targetValue's 0.5 for every Soundfont knob)
    auto old = song;
    old.soundfontModBaseReal = false;
    CHECK (baseOf (old) == 0.5f);
    CHECK (old.setTargetValue (cutoff, 0.1f));
    CHECK (baseOf (old) == 0.5f);
    // the rest of an old project is untouched: links and the knob's own value read the real position
    CHECK (std::abs (old.targetValue (cutoff) - 0.1f) < 1.0e-4f);
    // other instruments are not gated
    auto sampler = old;
    sampler.channels[0] = ChannelDef {};
    sampler.channels[0].instrument = InstrumentKind::sampler;
    const AutomationTarget attack { AutomationTargetKind::channelParam, 0, 0, samplerparam::attack };
    CHECK (std::abs (sampler.modulationBase (attack) - sampler.targetValue (attack)) < 1.0e-6f);
}
