// Round 169g2 (discussion #56 follow-up: "you can't change any parameter such as cutoff… all knobs are always 0 by
// default and don't do anything"): Transistor Bass's knobs, their FL values and their effect on the sound, with the
// sequencer off and on (a note selects program = key and FL's editor follows it, REA_rodada57 §2.10)

#include "core/ModelParams.h"
#include "core/NativeEngines.h"
#include "core/TransistorBass.h"

namespace tb169g2
{
// a TB channel from one of FL's presets (the plugin list's way), or nullopt without FL
std::optional<lynx::ChannelDef> channel (const char* preset)
{
    if (realFlpDir.empty())
        return std::nullopt;
    const auto path = (std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "Transistor Bass" / preset).string();
    return lynx::natives::channelFromPresetFile (lynx::InstrumentKind::transistorbass, path, natives142::flAccess());
}

struct Player
{
    std::unique_ptr<lynx::Instrument> inst = lynx::makeTransistorBassInstrument();
    explicit Player (const lynx::ChannelDef& c)
    {
        inst->prepare (44100.0);
        inst->configure (c, 140.0);
    }
    void play (std::vector<float>& out, int blocks)
    {
        std::vector<float> l (512), r (512);
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            inst->render (l.data(), r.data(), 512, 1.0f, 1.0f);
            for (int i = 0; i < 512; ++i)
                out.push_back (l[(std::size_t) i]), out.push_back (r[(std::size_t) i]);
        }
    }
};

// a held note through a new instrument, at full velocity (sequencer off: an accent) then at 0.6 (none), 1.5 s each
// (the delay's feedback needs a few echoes): interleaved samples
std::vector<float> render (const lynx::ChannelDef& c, int key)
{
    std::vector<float> out;
    for (const auto velocity : { 1.0f, 0.6f })
    {
        Player p (c);
        p.inst->noteOn (key, velocity);
        p.play (out, 130);
    }
    return out;
}

// every control in play: the effect sections on, a delay send (pulse width, its LFO and the 303 pulse act on it),
// the LFO on the pulse width, and accents on every other step of the edit program's sequence (the accent knob acts on
// accented steps, decay on the others)
void sectionsOn (lynx::ChannelDef& c)
{
    for (const auto i : { 14, 27, 32 })
        lynx::modelparams::setRawValue (c, i, 1.0f);
    lynx::modelparams::setRawValue (c, 1, 0.0f);
    lynx::modelparams::setRawValue (c, 13, 0.5f);
    lynx::modelparams::setRawValue (c, 34, 0.5f); // (some factory programs send nothing to the delay)
    for (int i = 0; i < lynx::tbseq::steps; ++i)
        lynx::modelparams::setRawValue (c, lynx::tbseq::firstStep + 5 * i + 4, i % 2 == 0 ? 1.0f : 0.0f);
}

// the parameters of the patch whose change does not move this note: the names, for the message
std::string unmoved (const lynx::ChannelDef& c, int key)
{
    using namespace lynx;
    const auto base = render (c, key);
    std::string out;
    for (const auto& p : transistorBassParams())
    {
        if (p.index >= 0x2A)
            continue;
        const auto v = modelparams::rawValue (c, p.index);
        auto changed = c;
        modelparams::setRawValue (changed, p.index, v && *v > 0.5f ? 0.0f : 1.0f);
        if (render (changed, key) == base)
            out += p.name + "; ";
    }
    return out;
}
} // namespace tb169g2

// FL adds a new Transistor Bass from its Plugin database (Generators\Synth classic\Transistor Bass.fst = "Factory
// programs"): LYNX's new channel has those values, not zeros
TEST (tb169g2_new_channel_has_fl_values)
{
    using namespace lynx;
    const auto factory = tb169g2::channel ("Factory programs.fst");
    if (! factory)
        return;
    const auto made = natives::makeChannel (InstrumentKind::transistorbass, natives142::flAccess());
    CHECK (made.has_value());
    if (! made)
        return;
    CHECK (made->instrument == InstrumentKind::transistorbass);
    CHECK (made->pluginState == factory->pluginState);
    int nonZero = 0;
    for (const auto& p : transistorBassParams())
        if (p.index < 0x2A)
        {
            const auto v = modelparams::rawValue (*made, p.index);
            CHECK (v.has_value());
            nonZero += v && *v != 0.0f ? 1 : 0;
        }
    CHECK (nonZero > 20);
    // (the program's own values: cutoff, resonance, env mod of FL's first factory program)
    CHECK (std::fabs (*modelparams::rawValue (*made, 2) - 0.159058f) < 1e-4f);
    CHECK (std::fabs (*modelparams::rawValue (*made, 4) - 0.934372f) < 1e-4f);
    CHECK (std::fabs (*modelparams::rawValue (*made, 5) - 0.774994f) < 1e-4f);
}

// sequencer off: every knob of the patch changes the sound
TEST (tb169g2_every_knob_moves_the_sound_sequencer_off)
{
    auto c = tb169g2::channel ("Sequencer off.fst");
    if (! c)
        return;
    tb169g2::sectionsOn (*c);
    const auto still = tb169g2::unmoved (*c, 36);
    if (! still.empty())
        std::printf ("  unmoved: %s\n", still.c_str());
    CHECK (still.empty());
}

// sequencer on (Factory programs, Empty): a note selects program = key; once the editor follows it (the state's current
// program), every knob changes what that note plays
TEST (tb169g2_every_knob_moves_the_sound_sequencer_on)
{
    using namespace lynx;
    for (const auto* preset : { "Factory programs.fst", "Empty.fst" })
    {
        auto c = tb169g2::channel (preset);
        if (! c)
            return;
        CHECK (transistorBassProgram (c->pluginState)->sequencer);
        // the instrument reports the note's program
        tb169g2::Player p (*c);
        p.inst->noteOn (40, 1.0f);
        CHECK (p.inst->selectedProgram() == 40);
        CHECK (transistorBassSelectProgram (c->pluginState, 40));
        CHECK (transistorBassProgram (c->pluginState)->program == 40);
        tb169g2::sectionsOn (*c);
        const auto still = tb169g2::unmoved (*c, 40);
        if (! still.empty())
            std::printf ("  %s unmoved: %s\n", preset, still.c_str());
        CHECK (still.empty());
    }
}

// live: the page follows the note's program (state edit, no rebuild: the sequence keeps running) and a knob turned
// then changes the sound at once
TEST (tb169g2_live_knob_after_note_program)
{
    using namespace lynx;
    const auto c = tb169g2::channel ("Factory programs.fst");
    if (! c)
        return;
    auto run = [&c] (bool turn) {
        tb169g2::Player p (*c);
        p.inst->noteOn (45, 1.0f);
        std::vector<float> out;
        p.play (out, 10);
        auto followed = *c;
        CHECK (transistorBassSelectProgram (followed.pluginState, p.inst->selectedProgram()));
        p.inst->configure (followed, 140.0);
        CHECK (p.inst->selectedProgram() == 45); // (not rebuilt: still the note's program)
        p.play (out, 10);
        if (turn)
        {
            const auto v = *modelparams::rawValue (followed, 2);
            modelparams::setRawValue (followed, 2, v > 0.5f ? 0.0f : 1.0f);
            p.inst->configure (followed, 140.0);
        }
        std::vector<float> tail;
        p.play (tail, 20);
        return std::pair { out, tail };
    };
    const auto [headA, tailA] = run (false);
    const auto [headB, tailB] = run (true);
    CHECK (headA == headB);
    CHECK (tailA != tailB);
}

// FL's channel volume reaches Transistor Bass only as MIDI CC 7 (FUN_18000d780: output × cc/127; Final.Vol unread): at
// FL's default volume (10000 of 12800 → CC 100) the output is 100/127 of full volume — LYNX used its channel gain curve
// (0.552 at 78 %), 3 dB below FL's render of the Plugin database state (REA_rodada169l §5)
TEST (tb169g2_channel_volume_as_midi_volume)
{
    using namespace lynx;
    const auto c = tb169g2::channel ("Sequencer off.fst");
    if (! c)
        return;
    auto run = [&c] (float gain) {
        tb169g2::Player p (*c);
        p.inst->noteOn (48, 0.8f);
        std::vector<float> out, l (512), r (512);
        for (int b = 0; b < 40; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            p.inst->render (l.data(), r.data(), 512, gain, gain);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto full = run (1.0f), def = run (channelGain (10000.0f / 12800.0f));
    double e = 0.0, worst = 0.0;
    for (std::size_t i = 0; i < full.size(); ++i)
    {
        e = std::max (e, (double) std::fabs (full[i]));
        worst = std::max (worst, std::fabs ((double) def[i] - (double) full[i] * (100.0 / 127.0)));
    }
    CHECK (e > 0.01);
    CHECK (worst < 1e-5 * e);
}
