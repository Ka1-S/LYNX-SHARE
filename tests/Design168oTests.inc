// Round 168o (docs/analise/REA_rodada168o.md): designed editors of the synth group (Harmless, Fruity DX10, SimSynth,
// Ogun / Autogun, MiniSynth, GMS, Morphine), their automatable parameters (core/ModelParamsSynths.h) and LYNX's own
// preset banks of Harmless, DX10 and SimSynth.

#include "core/Dx10.h"
#include "core/Harmless.h"
#include "core/LynxBanks.h"
#include "core/ModelParamsSynths.h"
#include "core/PluginLayoutSynths.h"
#include "core/SimSynth.h"

namespace design168o
{
const std::vector<lynx::EngineParam>& tableOf (lynx::InstrumentKind kind)
{
    using namespace lynx;
    switch (kind)
    {
        case InstrumentKind::harmless: return harmlessEngineParams();
        case InstrumentKind::dx10: return dx10EngineParams();
        case InstrumentKind::simsynth: return simSynthEngineParams();
        case InstrumentKind::ogun: return ogunEngineParams();
        case InstrumentKind::autogun: return autogunEngineParams();
        case InstrumentKind::minisynth: return miniSynthEngineParams();
        case InstrumentKind::gms: return gmsEngineParams();
        default: return morphineEngineParams();
    }
}

std::unique_ptr<lynx::Instrument> make (lynx::InstrumentKind kind)
{
    using namespace lynx;
    switch (kind)
    {
        case InstrumentKind::harmless: return makeHarmlessInstrument();
        case InstrumentKind::dx10: return makeDx10Instrument();
        default: return makeSimSynthInstrument();
    }
}

struct Level
{
    double peak = 0.0, rmsHeld = 0.0;
    bool finite = true;
};

// the preset's phrase: a held note (a chord for pads / keys) for 1 s, released, 0.5 s of tail at 44.1 kHz, on a channel
// at FL's default volume (78 %: channelGain 0.549)
Level play (lynx::Instrument& inst, const std::string& category)
{
    const auto gain = lynx::channelGain (0.78f);
    const int sr = 44100, len = sr * 3 / 2, offAt = sr;
    std::vector<int> keys { 60 };
    if (category == "Bass")
        keys = { 36 };
    else if (category == "Pad" || category == "Keys")
        keys = { 48, 55, 60, 64 };
    std::vector<float> L ((std::size_t) len, 0.0f), R ((std::size_t) len, 0.0f);
    for (const auto k : keys)
        inst.noteOn (k, 0.78f);
    for (int pos = 0; pos < len; pos += 256)
    {
        const auto n = std::min (256, len - pos);
        if (pos <= offAt && pos + n > offAt)
            for (const auto k : keys)
                inst.noteOff (k);
        inst.render (L.data() + pos, R.data() + pos, n, gain, gain);
    }
    Level l;
    double sum = 0.0;
    for (int i = 0; i < len; ++i)
    {
        if (! std::isfinite (L[(std::size_t) i]) || ! std::isfinite (R[(std::size_t) i]))
            l.finite = false;
        l.peak = std::max (l.peak, (double) std::max (std::fabs (L[(std::size_t) i]), std::fabs (R[(std::size_t) i])));
        if (i < offAt)
            sum += (double) L[(std::size_t) i] * L[(std::size_t) i] + (double) R[(std::size_t) i] * R[(std::size_t) i];
    }
    l.rmsHeld = std::sqrt (sum / (2.0 * offAt));
    return l;
}
} // namespace design168o

TEST (design168o_engine_param_tables)
{
    using namespace lynx;
    for (const auto kind : plugui::synthGroupLayoutKinds())
    {
        const auto& table = design168o::tableOf (kind);
        CHECK (table.size() >= 20);
        std::set<std::string> names;
        for (std::size_t i = 0; i < table.size(); ++i)
        {
            const auto& p = table[i];
            CHECK (names.insert (p.name).second);         // unique names (knobs' and clips' names)
            CHECK (i == 0 || table[i - 1].index < p.index); // sorted by index
            CHECK (p.high > p.low);
            CHECK (findEngineParam (table, p.index) == &p);
        }
    }
    // FL's ranges (the editor forms' Min / Max): DX10's octave −3…3, Harmless's unison order, SimSynth's coarse pitch
    const auto* oct = findEngineParam (dx10EngineParams(), 21);
    CHECK (oct != nullptr && oct->low == -3.0f && oct->high == 3.0f && oct->stepped);
    CHECK (harmlessEngineParams().size() > 100);
    CHECK (simSynthEngineParams().size() == (std::size_t) simsynth::hostParamCount);
}

TEST (design168o_layouts_name_real_parameters)
{
    using namespace lynx;
    for (const auto kind : plugui::synthGroupLayoutKinds())
    {
        const auto* layout = plugui::synthGroupLayout (kind);
        CHECK (layout != nullptr);
        if (layout == nullptr)
            continue;
        const auto& table = design168o::tableOf (kind);
        std::set<std::string> names, shown;
        for (const auto& p : table)
            names.insert (p.name);
        for (const auto& item : layout->items)
        {
            for (const char* p : { item.param, item.param2, item.whenParam, item.when2Param })
                if (p != nullptr)
                {
                    CHECK (names.count (p) == 1); // every control edits a parameter of its engine
                    shown.insert (p);
                }
            // inside the page (readable at 100…200 %: the editor scales the whole design)
            CHECK (item.x >= 0.0f && item.y >= 0.0f);
            CHECK (item.x + item.w <= (float) layout->width + 0.5f && item.y + item.h <= (float) layout->height + 0.5f);
            CHECK (item.page < (int) layout->pages.size());
        }
        CHECK (shown.size() * 10 >= names.size() * 6); // most of the plugin's controls are on the page
    }
}

TEST (design168o_banks_play_clean)
{
    using namespace lynx;
    for (const auto kind : { InstrumentKind::harmless, InstrumentKind::dx10, InstrumentKind::simsynth })
    {
        const auto& bank = banks::bankOf (kind);
        CHECK (bank.size() >= 12);
        std::set<std::string> names;
        for (const auto& preset : bank)
        {
            CHECK (names.insert (preset.name).second);
            ChannelDef c;
            c.instrument = kind;
            preset.apply (c);
            auto inst = design168o::make (kind);
            inst->configure (c, 120.0);
            inst->prepare (44100.0);
            inst->settleNew();
            const auto l = design168o::play (*inst, preset.category);
            CHECK (l.finite);
            CHECK (l.rmsHeld > 0.002); // not silent (−54 dB)
            CHECK (l.peak < 0.89);     // headroom: under −1 dBFS
        }
    }
}

TEST (design168o_state_parameter_round_trip)
{
    using namespace lynx;
    // SimSynth: FL's host parameters read back what was set, the rest of the state untouched
    auto s = simsynth::defaultState();
    const auto before = s;
    CHECK (simsynth::setHostParam (s, 0, 12345));
    CHECK (simsynth::hostParam (s, 0) == std::optional<int> (12345));
    for (int h = 1; h < simsynth::hostParamCount; ++h)
        CHECK (simsynth::hostParam (s, h) == simsynth::hostParam (before, h));
    // the model tables write through the same accessors
    for (const auto& [kind, table] : { std::pair { InstrumentKind::dx10, modelparams::dx10ModelTable() },
                                       std::pair { InstrumentKind::simsynth, modelparams::simSynthModelTable() } })
    {
        ChannelDef c;
        c.instrument = kind;
        banks::bankOf (kind).front().apply (c);
        const auto& p = table.params->front();
        CHECK (table.set (c, p, p.high));
        CHECK (table.get (c, p) == std::optional<float> (p.high));
    }
    ChannelDef h;
    h.instrument = InstrumentKind::harmless;
    banks::bankOf (InstrumentKind::harmless).front().apply (h);
    CHECK (h.modelParams.size() >= (std::size_t) harmlessParamCount);
    const auto ht = modelparams::harmlessModelTable();
    const auto* cutoff = findEngineParam (*ht.params, 54);
    CHECK (cutoff != nullptr && ht.set (h, *cutoff, 3000.0f) && ht.get (h, *cutoff) == std::optional<float> (3000.0f));
}

TEST (design168o_automation_changes_the_sound_live)
{
    using namespace lynx;
    // an automation clip on a knob (Instrument::setParameter, 0..1) changes the held note without restarting it
    for (const auto& [kind, index] : { std::pair { InstrumentKind::harmless, 54 }, std::pair { InstrumentKind::dx10, 5 },
                                       std::pair { InstrumentKind::simsynth, 0 } })
    {
        ChannelDef c;
        c.instrument = kind;
        banks::bankOf (kind).front().apply (c);
        auto a = design168o::make (kind), b = design168o::make (kind);
        for (auto* i : { a.get(), b.get() })
        {
            i->configure (c, 120.0);
            i->prepare (44100.0);
            i->settleNew();
            i->noteOn (60, 0.8f);
        }
        std::vector<float> la (8192), ra (8192), lb (8192), rb (8192);
        a->render (la.data(), ra.data(), 4096, 1.0f, 1.0f);
        b->render (lb.data(), rb.data(), 4096, 1.0f, 1.0f);
        b->setParameter (index, 0.05f);
        a->render (la.data() + 4096, ra.data() + 4096, 4096, 1.0f, 1.0f);
        b->render (lb.data() + 4096, rb.data() + 4096, 4096, 1.0f, 1.0f);
        double diff = 0.0, level = 0.0;
        for (int i = 4096; i < 8192; ++i)
            diff += std::fabs (la[(std::size_t) i] - lb[(std::size_t) i]), level += std::fabs (lb[(std::size_t) i]);
        CHECK (diff > 1e-3); // the change is heard
        CHECK (level > 1e-3); // the note still sounds
    }
}
