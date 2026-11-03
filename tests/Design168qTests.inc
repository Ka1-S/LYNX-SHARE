// Round 168q (docs/analise/REA_rodada168q.md): designed editors of Kepler, Kepler Exo, Transistor Bass, Sawer, BeepMap,
// Fruity Granulizer and the Wasp placeholder, their automatable parameters (core/ModelParams.h) and LYNX's own BeepMap
// and Granulizer banks (core/LynxBanks.h).

#include "core/BeepMap.h"
#include "core/EngineLayouts.h"
#include "core/EngineParams.h"
#include "core/Granulizer.h"
#include "core/Kepler.h"
#include "core/LynxBanks.h"
#include "core/ModelParams.h"
#include "core/PluginLayout.h"
#include "core/Sawer.h"
#include "core/TransistorBass.h"

namespace lynx::banks
{
std::vector<Preset> beepMapBank();   // core/BeepMapBank.cpp
std::vector<Preset> granulizerBank(); // core/GranulizerBank.cpp
} // namespace lynx::banks

namespace design168q
{
constexpr double twoPi = 6.283185307179586;

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

template <typename Note>
std::vector<Note> phrase (std::initializer_list<int> keys, int release)
{
    std::vector<Note> notes;
    for (const auto key : keys)
    {
        Note n;
        n.key = key;
        n.start = 0;
        n.release = release;
        notes.push_back (n);
    }
    return notes;
}

// a test sample: 2 s of a decaying chord with a few clicks (transients), 44.1 kHz mono
std::shared_ptr<lynx::SampleData> testSample()
{
    auto s = std::make_shared<lynx::SampleData>();
    const int n = 88200;
    s->sampleRate = 44100.0;
    s->left.resize ((std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto t = (double) i / 44100.0;
        const auto beat = std::fmod (t, 0.5);
        s->left[(std::size_t) i] = (float) (0.3 * std::exp (-beat * 6.0)
                                            * (std::sin (twoPi * 220.0 * t) + 0.5 * std::sin (twoPi * 277.2 * t)
                                               + 0.4 * std::sin (twoPi * 329.6 * t)));
    }
    s->right = s->left;
    return s;
}

struct Engine
{
    lynx::InstrumentKind kind;
    const std::vector<lynx::EngineParam>* params;
};

std::vector<Engine> engines()
{
    using namespace lynx;
    return { { InstrumentKind::kepler, &keplerParams() },          { InstrumentKind::keplerExo, &keplerExoParams() },
             { InstrumentKind::transistorbass, &transistorBassParams() }, { InstrumentKind::sawer, &sawerParams() },
             { InstrumentKind::beepmap, &beepMapEngineParams() },  { InstrumentKind::granulizer, &granulizerEngineParams() } };
}
} // namespace design168q

TEST (design168q_engine_param_tables)
{
    using namespace lynx;
    for (const auto& e : design168q::engines())
    {
        const auto& table = *e.params;
        CHECK (table.size() >= 8);
        std::set<std::string> names;
        for (std::size_t i = 0; i < table.size(); ++i)
        {
            const auto& p = table[i];
            CHECK (names.insert (p.name).second);
            CHECK (i == 0 || table[i - 1].index < p.index);
            CHECK (p.high > p.low);
            CHECK (findEngineParam (table, p.index) == &p);
        }
        // the registry knows the table
        CHECK (modelparams::tableOf (e.kind) != nullptr);
    }
    CHECK (keplerParams().size() == 36);
    CHECK (sawerParams().size() == 94);
    CHECK (transistorBassParams().size() == 42);
    const auto* scale = findEngineParam (beepMapEngineParams(), 2);
    CHECK (scale != nullptr && scale->stepped && scale->raw (1.0f) == 2.0f && scale->raw (0.5f) == 1.0f);
    const auto* wave = findEngineParam (granulizerEngineParams(), 3);
    CHECK (wave != nullptr && wave->low == -1000.0f && wave->high == 1000.0f);
    const auto* keyTo = findEngineParam (granulizerEngineParams(), 13);
    CHECK (keyTo != nullptr && keyTo->name == std::string ("Key to") && keyTo->raw (1.0f) == 3.0f);
}

TEST (design168q_layouts_name_real_parameters)
{
    using namespace lynx;
    struct Page { InstrumentKind kind; plugui::Layout (*make)(); int controls; };
    const Page pages[] = { { InstrumentKind::kepler, &plugui::keplerLayout, 34 },
                           { InstrumentKind::keplerExo, &plugui::keplerExoLayout, 120 },
                           { InstrumentKind::transistorbass, &plugui::transistorBassLayout, 40 },
                           { InstrumentKind::sawer, &plugui::sawerLayout, 90 },
                           { InstrumentKind::beepmap, &plugui::beepMapLayout, 8 },
                           { InstrumentKind::granulizer, &plugui::granulizerLayout, 15 } };
    for (const auto& page : pages)
    {
        const auto layout = page.make();
        CHECK (layout.width > 0 && layout.height > 0 && layout.fl != nullptr);
        // the channel settings show this page for the engine
        const auto* shown = plugui::instrumentLayout (page.kind);
        CHECK (shown != nullptr && shown->fl != nullptr && std::string (shown->fl) == layout.fl);
        int controls = 0;
        std::set<std::string> used;
        for (const auto& item : layout.items)
        {
            if (item.param != nullptr && plugui::isValueControl (item.type))
            {
                ++controls;
                used.insert (item.param);
                const auto* p = modelparams::findByName (page.kind, item.param);
                CHECK (p != nullptr);
                if (p == nullptr)
                    std::printf ("  (%s: no parameter \"%s\")\n", layout.fl, item.param);
            }
            if (item.whenParam != nullptr)
                CHECK (modelparams::findByName (page.kind, item.whenParam) != nullptr);
            CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) layout.width + 0.5f && item.y + item.h <= (float) layout.height + 0.5f);
            if (item.page >= 0)
                CHECK (item.page < (int) std::max<std::size_t> (layout.pages.size(), 5));
        }
        CHECK (controls >= page.controls);
        if (controls < page.controls)
            std::printf ("  (%s: %d controls)\n", layout.fl, controls);
    }
    // Wasp: FL's placeholder text, no controls
    const auto wasp = plugui::waspLayout();
    CHECK (! wasp.items.empty());
    for (const auto& item : wasp.items)
        CHECK (item.param == nullptr);
}

TEST (design168q_model_params_read_and_write_the_state)
{
    using namespace lynx;
    // BeepMap: the int32 settings, whole numbers in their ranges
    ChannelDef b;
    b.instrument = InstrumentKind::beepmap;
    const auto& beeps = banks::beepMapBank();
    CHECK (! beeps.empty());
    beeps[0].apply (b);
    CHECK (beepMapStateUsable (b.pluginState));
    CHECK (modelparams::setValue01 (b, 1, 0.5f) && modelparams::rawValue (b, 1) == 2048.0f);
    CHECK (modelparams::setValue01 (b, 5, 0.0f) && (*beepMapParams (b.pluginState))[5] == 0);
    CHECK (modelparams::setValue01 (b, 2, 0.5f) && (*beepMapParams (b.pluginState))[2] == 1);
    CHECK (! modelparams::setValue01 (b, 9, 0.5f)); // (settings 7…13 are unused: not listed)
    CHECK (beepMapStateUsable (b.pluginState));
    // the bitmap: shown by the page, replaced by Open / Paste (the settings stay)
    const auto pic = beepMapPicture (b.pluginState);
    CHECK (pic && pic->width == 1 && pic->height == 32 && pic->bgr.size() == 96);
    ChannelDef saw;
    beeps[6].apply (saw); // "Additive Saw": 64 rows
    const std::vector<std::uint8_t> bmp (saw.pluginState.begin() + 4 + 0x3c + 4, saw.pluginState.end());
    const auto swapped = beepMapWithBitmap (b.pluginState, bmp);
    CHECK (beepMapStateUsable (swapped) && beepMapPicture (swapped)->height == 64);
    CHECK ((*beepMapParams (swapped))[1] == 2048 && (*beepMapParams (swapped))[5] == 0);
    CHECK (beepMapWithBitmap (b.pluginState, { 'B', 'M', 0, 0 }).empty());

    // Granulizer: the knobs and the modes
    ChannelDef g;
    g.instrument = InstrumentKind::granulizer;
    g.pluginState = granulizerDefaultState();
    CHECK (granulizerStateUsable (g.pluginState));
    CHECK (modelparams::rawValue (g, 0) == 204.0f && modelparams::rawValue (g, 5) == 180.0f && modelparams::rawValue (g, 11) == 228.0f);
    CHECK (modelparams::setValue01 (g, 3, 0.0f) && granulizerParam (g.pluginState, 3) == -1000);
    CHECK (modelparams::setValue01 (g, 13, 1.0f) && granulizerMode (g.pluginState, 1) == 3);
    CHECK (modelparams::setValue01 (g, 14, 1.0f) && granulizerMode (g.pluginState, 2) == 1);

    // automation clips reach the engines' parameters through the Song
    Song s;
    s.channels.push_back (g);
    const AutomationTarget t { AutomationTargetKind::channelParam, 0, 0, 7 };
    CHECK (s.setTargetValue (t, 0.5f));
    CHECK (granulizerParam (s.channels[0].pluginState, 7) == 32);
}

TEST (design168q_live_changes_keep_the_notes)
{
    using namespace lynx;
    // a BeepMap knob moved while a note plays: the voice goes on (only a new bitmap stops the voices, as FL)
    ChannelDef c;
    c.instrument = InstrumentKind::beepmap;
    banks::beepMapBank()[0].apply (c);
    auto inst = makeBeepMapInstrument();
    inst->prepare (44100.0);
    inst->configure (c, 140.0);
    std::vector<float> l (2048), r (2048);
    inst->noteOn (60, 1.0f);
    inst->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    CHECK (! inst->idle());
    CHECK (modelparams::setValue01 (c, 0, 0.3f));
    inst->configure (c, 140.0);
    std::fill (l.begin(), l.end(), 0.0f);
    inst->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    CHECK (! inst->idle());
    float peak = 0.0f;
    for (const auto v : l)
        peak = std::max (peak, std::fabs (v));
    CHECK (peak > 1.0e-3f);
    // automation of a setting while playing
    inst->setParameter (1, 0.9f);
    inst->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    CHECK (! inst->idle());
}

TEST (design168q_banks_play_clean)
{
    using namespace lynx;
    using namespace design168q;
    // BeepMap: a held note, then a three-note chord (each preset is drawn to stay under 0.55 per note)
    const auto& beeps = banks::beepMapBank();
    CHECK (beeps.size() >= 16);
    std::set<std::string> names, categories;
    for (const auto& preset : beeps)
    {
        CHECK (names.insert (preset.name).second);
        categories.insert (preset.category);
        CHECK (! preset.tags.empty());
        ChannelDef c;
        c.instrument = InstrumentKind::beepmap;
        preset.apply (c);
        CHECK (beepMapStateUsable (c.pluginState));
        for (const auto& keys : { std::initializer_list<int> { 60 }, std::initializer_list<int> { 48, 55, 64 } })
        {
            const auto out = beepmap::renderReference (c.pluginState, phrase<beepmap::RefNote> (keys, 88200), 44100.0, 132300);
            const auto level = measure (out);
            const auto clean = level.finite && level.peak < 1.0f && level.rms > 2.0e-3;
            CHECK (clean);
            if (! clean || std::getenv ("LYNX_BANK_LEVELS") != nullptr)
                std::printf ("  BeepMap / %s (%d keys): peak %.3f rms %.4f\n", preset.name.c_str(), (int) keys.size(), level.peak, level.rms);
        }
    }
    for (const auto* category : { "Organ", "Lead", "Bass", "Keys", "Pad", "Arp", "Chord", "FX" })
        CHECK (categories.count (category) == 1);

    // Granulizer: settings over the channel's own sample
    const auto sample = testSample();
    const auto& grains = banks::granulizerBank();
    CHECK (grains.size() >= 10);
    names.clear();
    for (const auto& preset : grains)
    {
        CHECK (names.insert (preset.name).second);
        ChannelDef c;
        c.instrument = InstrumentKind::granulizer;
        preset.apply (c);
        CHECK (granulizerStateUsable (c.pluginState) && c.pluginState.size() == 428);
        granulizer::RefOptions o;
        o.slices = { 0, 22050, 44100, 66150 };
        const auto out = granulizer::renderReference (c.pluginState, *sample, phrase<granulizer::RefNote> ({ 60 }, 88200), 44100.0, 110250, o);
        const auto level = measure (out);
        const auto clean = level.finite && level.peak < 1.0f && level.rms > 2.0e-3;
        CHECK (clean);
        if (! clean || std::getenv ("LYNX_BANK_LEVELS") != nullptr)
            std::printf ("  Granulizer / %s: peak %.3f rms %.4f\n", preset.name.c_str(), level.peak, level.rms);
    }
    // a preset keeps the channel's channel-settings snapshot and replaces every knob
    ChannelDef c;
    c.instrument = InstrumentKind::granulizer;
    grains[4].apply (c);
    grains[0].apply (c);
    CHECK (c.pluginState == granulizerDefaultState());

    // without FL Studio, both engines start from their bank and are offered in the pickers
    CHECK (banks::hasBank (InstrumentKind::beepmap) && banks::hasBank (InstrumentKind::granulizer));
    const auto fresh = banks::defaultChannel (InstrumentKind::beepmap, "BeepMap");
    CHECK (fresh && beepMapStateUsable (fresh->pluginState));
}
