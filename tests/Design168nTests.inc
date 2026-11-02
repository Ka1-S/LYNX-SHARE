// Round 168n (docs/analise/REA_rodada168n.md): designed editors of Toxic Biohazard and PoiZone, their automatable
// parameters (core/ModelParams.h), LYNX's own preset banks (core/LynxBanks.h) and the live parameter queue.

#include "core/EngineParams.h"
#include "core/LynxBanks.h"
#include "core/ModelParams.h"
#include "core/PluginLayout.h"
#include "core/PoiZone.h"
#include "core/Toxic.h"

namespace design168n
{
struct Level
{
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

Level measure (const std::vector<float>& interleaved)
{
    Level l;
    double sum = 0.0;
    for (const auto v : interleaved)
    {
        if (! std::isfinite (v))
            l.finite = false;
        l.peak = std::max (l.peak, std::fabs (v));
        sum += (double) v * v;
    }
    l.rms = interleaved.empty() ? 0.0 : std::sqrt (sum / (double) interleaved.size());
    return l;
}

// a held C3 (and for polyphonic presets an E3 / G3 on top), 2 s, released, 1 s of tail at 48 kHz
template <typename Note>
std::vector<Note> phrase (bool chord)
{
    std::vector<Note> notes;
    for (const auto key : { 48, 52, 55 })
    {
        Note n;
        n.key = key;
        n.start = 0;
        n.release = 96000;
        notes.push_back (n);
        if (! chord)
            break;
    }
    return notes;
}
} // namespace design168n

TEST (design168n_engine_param_tables)
{
    using namespace lynx;
    for (const auto* table : { &toxicParams(), &poizoneParams() })
    {
        CHECK (table->size() > 80);
        std::set<std::string> names;
        for (std::size_t i = 0; i < table->size(); ++i)
        {
            const auto& p = (*table)[i];
            CHECK (names.insert (p.name).second);              // unique names (the knobs' and the clips' names)
            CHECK (i == 0 || (*table)[i - 1].index < p.index); // sorted by index
            CHECK (p.high > p.low);
            CHECK (findEngineParam (*table, p.index) == &p);
        }
    }
    CHECK (findEngineParam (toxicParams(), 200) == nullptr); // (the sequencer is not modelled)
    const auto* poly = findEngineParam (toxicParams(), 301);
    CHECK (poly != nullptr && poly->stepped && poly->raw (1.0f) == 32.0f && poly->raw (0.5f) == 16.0f);
    const auto* unison = findEngineParam (poizoneParams(), 75);
    CHECK (unison != nullptr && unison->raw (0.34f) == 2.0f && std::fabs (unison->normalised (3.0f) - 0.5f) < 1e-6f);
}

TEST (design168n_layouts_name_real_parameters)
{
    using namespace lynx;
    // every value control of the two designed pages edits a listed parameter of its engine
    for (const auto kind : { InstrumentKind::toxic, InstrumentKind::poizone })
    {
        const auto* layout = plugui::instrumentLayout (kind);
        CHECK (layout != nullptr);
        if (layout == nullptr)
            continue;
        int controls = 0;
        for (const auto& item : layout->items)
        {
            if (item.param != nullptr && plugui::isValueControl (item.type))
            {
                ++controls;
                const auto* p = modelparams::findByName (kind, item.param);
                CHECK (p != nullptr);
                if (p == nullptr)
                    std::printf ("  (no parameter \"%s\")\n", item.param);
            }
            if (item.whenParam != nullptr)
                CHECK (modelparams::findByName (kind, item.whenParam) != nullptr);
            // inside the page
            CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) layout->width + 0.5f && item.y + item.h <= (float) layout->height + 0.5f);
        }
        CHECK (controls > (kind == InstrumentKind::toxic ? 150 : 70));
    }
    // the FM grid: 36 cells, "FM i to j" = parameter 109 + 6 (j − 1) + (i − 1) (REA_rodada45 §3.6)
    const auto* cell = modelparams::findByName (InstrumentKind::toxic, "FM 2 to 1");
    CHECK (cell != nullptr && cell->index == 110);
}

TEST (design168n_model_params_read_and_write_the_state)
{
    using namespace lynx;
    ChannelDef c;
    c.instrument = InstrumentKind::toxic;
    c.pluginState = toxicDefaultState();
    CHECK (modelparams::rawValue (c, 0) == 0.5f);  // Gain's constructor value
    CHECK (modelparams::rawValue (c, 301) == 8.0f); // polyphony 8
    CHECK (modelparams::setValue01 (c, 15, 0.25f) && modelparams::rawValue (c, 15) == 0.25f);
    CHECK (modelparams::setValue01 (c, 300, 1.0f) && modelparams::rawValue (c, 300) == 3.0f); // filter mode HP
    CHECK (! modelparams::setValue01 (c, 200, 0.5f));                                           // not listed

    // automation clips: the Song reads and writes them like any target
    Song s;
    s.channels.push_back (c);
    const AutomationTarget t { AutomationTargetKind::channelParam, 0, 0, 16 };
    CHECK (s.setTargetValue (t, 0.75f));
    CHECK (std::fabs (s.targetValue (t) - 0.75f) < 1e-6f);
    CHECK (modelparams::rawValue (s.channels[0], 16) == 0.75f);

    ChannelDef pz;
    pz.instrument = InstrumentKind::poizone;
    pz.pluginState = poizoneDefaultState();
    CHECK (poizoneStateUsable (pz.pluginState));
    CHECK (modelparams::rawValue (pz, 76) == 12.0f); // constructor polyphony
    CHECK (modelparams::setValue01 (pz, 18, 0.3f) && poizoneParam (pz.pluginState, 18) == 0.3f);
}

TEST (design168n_live_param_queue)
{
    lynx::LiveParamQueue q;
    CHECK (q.space() == 1023);
    for (int i = 0; i < 1023; ++i)
        CHECK (q.push (i, (float) i));
    CHECK (! q.push (5000, 1.0f)); // full
    int n = 0;
    bool ordered = true;
    q.drain ([&] (int i, float v) { ordered = ordered && i == n && v == (float) n; ++n; });
    CHECK (n == 1023 && ordered && q.space() == 1023);
}

TEST (design168n_banks_play_clean)
{
    using namespace lynx;
    using namespace design168n;
    for (const auto kind : { InstrumentKind::toxic, InstrumentKind::poizone })
    {
        const auto& bank = banks::bankOf (kind);
        CHECK (bank.size() >= 20);
        std::set<std::string> names, categories;
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            const auto& preset = bank[(std::size_t) i];
            CHECK (names.insert (preset.name).second);
            categories.insert (preset.category);
            CHECK (! preset.tags.empty());
            ChannelDef c;
            c.instrument = kind;
            c.name = "Mine";
            c.volume = 0.6f;
            const auto loaded = banks::withPreset (c, i);
            CHECK (loaded && loaded->name == preset.name && loaded->volume == 0.6f && loaded->instrument == kind);
            if (! loaded)
                continue;
            std::vector<float> out;
            if (kind == InstrumentKind::toxic)
            {
                CHECK (toxicStateUsable (loaded->pluginState) && ! toxicNeedsDll (loaded->pluginState));
                out = toxic::renderReference (loaded->pluginState, phrase<toxic::RefNote> (true), 48000.0, 144000, "");
            }
            else
            {
                CHECK (poizoneStateUsable (loaded->pluginState));
                out = poizone::renderReference (loaded->pluginState, phrase<poizone::RefNote> (true), 48000.0, 144000);
            }
            const auto level = measure (out);
            const auto clean = level.finite && level.peak < 1.0f && level.rms > 1.0e-3;
            CHECK (clean);
            if (! clean || std::getenv ("LYNX_BANK_LEVELS") != nullptr)
                std::printf ("  %s / %s: peak %.3f rms %.4f\n", kind == InstrumentKind::toxic ? "Toxic" : "PoiZone", preset.name.c_str(),
                             level.peak, level.rms);
        }
        for (const auto* category : { "Bass", "Lead", "Pad", "Pluck", "Keys", "FX" })
            CHECK (categories.count (category) == 1);
    }
    CHECK (banks::indexOfPath (banks::pathOf (17)) == 17 && banks::indexOfPath ("C:/x.fst") == -1 && banks::indexOfPath ("lynxbank:") == -1);
    CHECK (banks::relativeOf (banks::bankOf (InstrumentKind::poizone)[1]) == "LYNX/Bass/Acid Line");
}
