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

// a held note (full velocity: an accent) through a new instrument: interleaved samples
std::vector<float> render (const lynx::ChannelDef& c, int key)
{
    Player p (c);
    p.inst->noteOn (key, 1.0f);
    std::vector<float> out;
    p.play (out, 40);
    return out;
}

// the effect sections on and the LFO on the pulse width, so their own knobs act
void sectionsOn (lynx::ChannelDef& c)
{
    for (const auto i : { 14, 27, 32 })
        lynx::modelparams::setRawValue (c, i, 1.0f);
    lynx::modelparams::setRawValue (c, 13, 0.5f);
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
        CHECK (p.inst->selectedProgram() == transistorBassProgram (c->pluginState)->program);
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
