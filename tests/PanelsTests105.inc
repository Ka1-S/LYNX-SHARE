#include "core/EquoFx.h"
#include "core/FruityDelayBank.h"
#include "core/MultibandDelayFx.h"
#include "core/StateParams.h"

// --- round 105: FL arrangements of the remaining native plugin windows, state-backed controls ---------------------

namespace panels105
{
// stereo noise (fixed seed), n frames
inline std::vector<float> noise (int n, std::uint32_t seed)
{
    std::vector<float> v ((std::size_t) n);
    for (auto& x : v)
    {
        seed = seed * 1664525u + 1013904223u;
        x = ((float) (seed >> 8) / 16777216.0f - 0.5f) * 0.5f;
    }
    return v;
}

inline double energy (const std::vector<float>& l, const std::vector<float>& r)
{
    double e = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
        e += (double) l[i] * l[i] + (double) r[i] * r[i];
    return e;
}
} // namespace panels105

TEST (state_params_delay_bank_round_trip)
{
    using namespace lynx;
    const auto* table = stateparams::tableFor (EffectKind::fruityDelayBank);
    CHECK (table != nullptr);
    if (table == nullptr)
        return;
    CHECK (table->params.size() == 8u * 27u + 2u);
    const auto state = fldbank::serialise (fldbank::defaultState());
    const auto time = table->index ("B1 Time");
    CHECK (time == 8);
    CHECK (table->get (state, time).value_or (-1) == fldbank::defaultState().p[12]);
    const auto edited = table->with (state, time, 960);
    CHECK (table->get (edited, time).value_or (-1) == 960);
    CHECK (fldbank::parse (edited)->p[12] == 960);
    CHECK (table->get (table->with (state, time, 99999), time).value_or (-1) == 7680); // clamped
    CHECK (table->text (edited, time, 1920) == "4.00 steps");                         // synced: one beat
    const auto mode = table->index ("B2 Mode");
    CHECK (mode == 27 + 16 + 5);
    CHECK (table->get (table->with (state, mode, 3), mode).value_or (-1) == 3);
    CHECK (fldbank::parse (table->with (state, mode, 3))->bank[1].mode == 3);
    CHECK (table->text (state, mode, 3) == "Ping-pong");
    CHECK (table->text (state, table->index ("Solo"), -1) == "None");
    CHECK (table->text (state, table->index ("B1 Input pan"), -64) == "50% left");
    CHECK (table->text (state, table->index ("B1 Filter 1 gain"), 128) == "+0.0 dB");
    // editor indices: the four globals first, then the state controls
    CHECK (plugui::paramIndex (EffectKind::fruityDelayBank, "Feedback") == 3);
    CHECK (plugui::paramIndex (EffectKind::fruityDelayBank, "B1 Time") == 4 + 8);
    CHECK (plugui::stateIndex (EffectKind::fruityDelayBank, 3) == -1);
    CHECK (plugui::stateIndex (EffectKind::fruityDelayBank, 12) == 8);
    CHECK (std::abs (plugui::stateValue01 (EffectKind::fruityDelayBank, 12, edited) - 960.0f / 7680.0f) < 1e-6f);
    CHECK (plugui::stateRaw (EffectKind::fruityDelayBank, 12, 0.25f) == 1920);
}

TEST (state_params_band_tables)
{
    using namespace lynx;
    // Multiband Delay: bank·48 + target·16 + band, then the editor's target and bank
    const auto* mbd = stateparams::tableFor (EffectKind::multibandDelay);
    CHECK (mbd != nullptr && mbd->params.size() == 8u * 48u + 2u);
    const auto mstate = mbdelay::serialize (mbdelay::State {});
    const auto cell = 2 * 48 + 1 * 16 + 5;
    CHECK (mbd->index ("Bank 3 volume 6") == cell);
    const auto m2 = mbd->with (mstate, cell, 1 << 29);
    CHECK (mbdelay::parse (m2)->raw[(std::size_t) cell] == 1 << 29);
    CHECK (mbd->text (m2, cell, 1 << 29) == "50%");
    CHECK (mbd->text (m2, 2 * 48 + 2 * 16, 1 << 29) == "Centered");
    CHECK (mbd->get (mbd->with (mstate, 8 * 48 + 1, 2), 8 * 48 + 1).value_or (-1) == 2);

    // EQUO: (bank·31 + band)·3 + target, thousandths of FL's 0…2 cells
    const auto* equo = stateparams::tableFor (EffectKind::equo);
    CHECK (equo != nullptr && equo->params.size() == 9u * 31u * 3u);
    CHECK (equo->index ("Master pan 1") == (8 * 31) * 3 + 1);
    CHECK (equo->get ({ 1, 2, 3 }, 0) == std::nullopt); // not an EQUO state

    // Convolver: the knobs of the FL state inside the slot blob
    const auto* conv = stateparams::tableFor (EffectKind::convolver);
    CHECK (conv != nullptr && conv->params.size() == 5u);
    CHECK (conv->text ({}, 2, 1000) == "2.00x");
    CHECK (conv->text ({}, 0, 0) == "0.0 ms");

    // Maximus keeps its 59 controls at their old indices
    CHECK (plugui::paramIndex (EffectKind::maximus, "Master release") == 7 + 13 * 3 + 9);
}

TEST (delay_bank_takes_state_edits_while_running)
{
    using namespace lynx;
    // an edited bank control reaches the running engine (no rebuild); an option change asks for a new effect
    auto state = fldbank::defaultState();
    const auto bytes = fldbank::serialise (state);
    auto fx = fldbank::make (bytes);
    fx->prepare (44100.0);
    auto slot = makeEffectSlot (EffectKind::fruityDelayBank);
    constexpr int n = 512;
    std::vector<float> l = panels105::noise (n, 1), r = panels105::noise (n, 2);
    for (int b = 0; b < 40; ++b)
    {
        auto x = l, y = r;
        fx->process (x.data(), y.data(), n, slot.params, 120.0);
    }
    const auto* table = stateparams::tableFor (EffectKind::fruityDelayBank);
    // bank 1's output volume to 0: only the dry signal is left
    auto quiet = table->with (bytes, table->index ("B1 Output volume"), 0);
    CHECK (fx->acceptState (quiet));
    CHECK (! fx->acceptState (table->with (quiet, table->index ("B1 Mode"), 3))); // previous one not taken yet
    slot.params[0] = 0.0f; // dry off
    double tail = 0.0;
    for (int b = 0; b < 20; ++b)
    {
        auto x = l, y = r;
        fx->process (x.data(), y.data(), n, slot.params, 120.0);
        if (b >= 10)
            tail += panels105::energy (x, y);
    }
    CHECK (tail < 1e-6 * panels105::energy (l, r));
    CHECK (! fx->acceptState (table->with (quiet, table->index ("B1 Mode"), 3))); // an option: rebuild
    CHECK (fx->acceptState (table->with (quiet, table->index ("B1 Time"), 480)));
}

TEST (band_table_effects_take_state_edits)
{
    using namespace lynx;
    constexpr int n = 512;
    const auto l = panels105::noise (n, 3), r = panels105::noise (n, 4);
    // Multiband Delay: every volume cell of every bank at 0 → the wet path falls silent, mix fully wet
    {
        auto bytes = mbdelay::serialize (mbdelay::State {});
        auto fx = mbdelay::make (bytes);
        fx->prepare (44100.0);
        auto slot = makeEffectSlot (EffectKind::multibandDelay);
        slot.params[(std::size_t) plugui::paramIndex (EffectKind::multibandDelay, "Mix")] = 1.0f;
        slot.params[(std::size_t) plugui::paramIndex (EffectKind::multibandDelay, "Feedback")] = 0.0f;
        for (int b = 0; b < 8; ++b)
        {
            auto x = l, y = r;
            fx->process (x.data(), y.data(), n, slot.params, 120.0);
        }
        const auto* table = stateparams::tableFor (EffectKind::multibandDelay);
        for (int bank = 0; bank < 8; ++bank)
            for (int band = 0; band < 16; ++band)
                bytes = table->with (bytes, bank * 48 + 16 + band, 0);
        CHECK (fx->acceptState (bytes));
        double tail = 0.0;
        for (int b = 0; b < 200; ++b)
        {
            auto x = l, y = r;
            fx->process (x.data(), y.data(), n, slot.params, 120.0);
            if (b >= 190)
                tail += panels105::energy (x, y);
        }
        CHECK (tail < 1e-4 * panels105::energy (l, r));
    }
}

TEST (round105_layouts_are_fl_arrangements)
{
    using namespace lynx;
    for (const auto kind : { EffectKind::vocoder, EffectKind::frequencySplitter, EffectKind::fruityDelayBank, EffectKind::equo,
                             EffectKind::multibandDelay, EffectKind::convolver })
    {
        CHECK (plugui::hasFlLayout (kind));
        const auto& l = plugui::layoutFor (kind);
        auto controls = 0;
        for (const auto& item : l.items)
            controls += plugui::isValueControl (item.type) && item.param != nullptr ? 1 : 0;
        CHECK (controls >= 8);
    }
    // the Delay Bank's pages are its eight banks, each with its own controls
    const auto& db = plugui::layoutFor (EffectKind::fruityDelayBank);
    CHECK (db.pages.size() == 8u);
    auto onBank3 = 0;
    for (const auto& item : db.items)
        if (item.page == 2 && item.param != nullptr && std::string (item.param).rfind ("B3 ", 0) == 0)
            ++onBank3;
    CHECK (onBank3 >= 20);
}

TEST (state_params_vocodex_in_place)
{
    using namespace lynx;
    // the Default preset of the installation (skipped without the FL Data directory)
    const auto file = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / "Vocodex" / "Default.fst";
    if (realFlpDir.empty() || ! std::filesystem::exists (file))
        return;
    std::ifstream in (file, std::ios::binary);
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const auto fx = effectFromFlPreset (bytes);
    CHECK (fx && fx->kind == EffectKind::vocodex);
    if (! fx || fx->kind != EffectKind::vocodex)
        return;
    const auto* table = stateparams::tableFor (EffectKind::vocodex);
    CHECK (table->get (fx->state, table->index ("Wet")).value_or (-1) == 1000);
    CHECK (table->get (fx->state, table->index ("Bands")).value_or (-1) == 47);
    CHECK (table->get (fx->state, table->index ("Draft")).value_or (-1) == 1);   // the field is 0 = Draft on
    CHECK (table->get (fx->state, table->index ("From car")).value_or (-1) == 0);
    auto edited = table->with (fx->state, table->index ("Bands"), 60);
    edited = table->with (edited, table->index ("Draft"), 0);
    edited = table->with (edited, table->index ("From car"), 1);
    CHECK (edited.size() == fx->state.size());
    const auto st = vocodex::parseState (vocodex::splitInstallData (edited).first);
    CHECK (st && st->bands == 60 && ! st->draft() && st->fromCar);
    CHECK (table->text (edited, table->index ("Mod pitch"), -1200) == "-1200 ct");
}
