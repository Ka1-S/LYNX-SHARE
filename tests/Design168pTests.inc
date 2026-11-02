// Round 168p: the designed pages of FL's drum and physical-model generators and LYNX's own banks for them.

namespace design168p
{
const lynx::InstrumentKind kinds[] = { lynx::InstrumentKind::fruitkick, lynx::InstrumentKind::plucked, lynx::InstrumentKind::drumpad,
                                       lynx::InstrumentKind::drumaxx, lynx::InstrumentKind::bassdrum, lynx::InstrumentKind::drumsynth,
                                       lynx::InstrumentKind::sakura, lynx::InstrumentKind::slayer };
} // namespace design168p

TEST (design168p_every_drum_engine_has_fl_arrangement)
{
    using namespace lynx;
    for (const auto kind : design168p::kinds)
    {
        const auto* layout = plugui::instrumentLayout (kind);
        CHECK (layout != nullptr);
        if (layout == nullptr)
            continue;
        CHECK (*layout->fl != 0);
        CHECK (std::find (plugui::instrumentLayoutKinds().begin(), plugui::instrumentLayoutKinds().end(), kind) != plugui::instrumentLayoutKinds().end());
        for (const auto& item : layout->items)
        {
            // inside the page (FL's panel size)
            CHECK (item.x >= -1.0f && item.y >= -1.0f);
            CHECK (item.x + item.w <= (float) layout->width + 1.0f);
            CHECK (item.y + item.h <= (float) layout->height + 1.0f);
            CHECK (item.page < (int) std::max<std::size_t> (1, layout->pages.size()));
        }
    }
}

TEST (design168p_layout_controls_match_the_engine_tables)
{
    using namespace lynx;
    for (const auto kind : design168p::kinds)
    {
        const auto* layout = plugui::instrumentLayout (kind);
        if (layout == nullptr)
            continue;
        const auto& table = drumcontrols::controls (kind);
        CHECK (! table.empty());
        // every value item edits a control of the engine, at most once per page
        std::set<std::pair<int, std::string>> seen;
        for (const auto& item : layout->items)
        {
            if (! plugui::isValueControl (item.type) || item.param == nullptr)
                continue;
            const auto* c = drumcontrols::find (kind, item.param);
            CHECK (c != nullptr);
            if (c == nullptr)
                std::printf ("  no control '%s'\n", item.param);
            CHECK (seen.insert ({ item.page, item.param }).second);
            // menus / steppers / radios: one option per whole value of the control
            if (c != nullptr && (item.type == plugui::Item::menu || item.type == plugui::Item::radio))
                CHECK ((int) plugui::optionLabels (item).size() == (int) (c->high - c->low) + 1);
        }
        // every control of the engine is reachable on the page (the panel's displays edit none)
        for (const auto& c : table)
        {
            const auto shown = std::any_of (layout->items.begin(), layout->items.end(),
                                            [&c] (const plugui::LayoutItem& i) { return i.param != nullptr && std::strcmp (i.param, c.name) == 0; });
            CHECK (shown);
            if (! shown)
                std::printf ("  '%s' not on the page\n", c.name);
        }
    }
}

TEST (design168p_controls_read_and_write_their_storage)
{
    using namespace lynx;
    // Fruit Kick (version-0 state built by the bank code)
    auto kick = bankstates::channelFor ("fruitkick", { 100, -500, 70, 130, 20, 5 });
    CHECK (kick && kick->instrument == InstrumentKind::fruitkick && fruitKickStateUsable (kick->pluginState));
    const auto* minFreq = drumcontrols::find (InstrumentKind::fruitkick, "Min freq");
    CHECK (minFreq != nullptr && drumcontrols::value (*kick, *minFreq) == -500.0f);
    CHECK (drumcontrols::setValue (*kick, *minFreq, -5000.0f)); // clamped to the knob's range
    CHECK (fruitKickParam (kick->pluginState, 1) == -1200);
    // Plucked!: a fresh channel (no state) shows the engine's defaults and gets a state on the first edit
    ChannelDef empty;
    empty.instrument = InstrumentKind::plucked;
    CHECK (drumcontrols::value (empty, *drumcontrols::find (InstrumentKind::plucked, "Decay")) == 128.0f);
    CHECK (drumcontrols::setValue (empty, *drumcontrols::find (InstrumentKind::plucked, "Gate"), 1.0f));
    CHECK (pluckedStateUsable (empty.pluginState) && drumcontrols::value (empty, *drumcontrols::find (InstrumentKind::plucked, "Color")) == 64.0f);
    auto pluck = bankstates::channelFor ("plucked", { 200, 30, 1, 0, 1 });
    CHECK (pluck && pluckedStateUsable (pluck->pluginState));
    CHECK (drumcontrols::value (*pluck, *drumcontrols::find (InstrumentKind::plucked, "Color")) == 30.0f);
    // Drumaxx: the selected pad's voice is at pad·44 + 5 + Drumpad's index
    std::vector<bankstates::DrumaxxPad> pads (3);
    pads[2].voice.assign (38, 0.25f);
    pads[2].voice[29] = 0.75f; // pitch
    ChannelDef maxx;
    maxx.instrument = InstrumentKind::drumaxx;
    maxx.pluginState = bankstates::drumaxxState ("LYNX test", pads);
    CHECK (drumaxxStateUsable (maxx.pluginState));
    CHECK (drumaxxKitName (maxx.pluginState) == "LYNX test");
    const auto* pitch = drumcontrols::find (InstrumentKind::drumaxx, "Pitch");
    CHECK (pitch != nullptr && pitch->perPart);
    CHECK (drumcontrols::value (maxx, *pitch, 2) == 0.75f);
    CHECK (drumcontrols::value (maxx, *pitch, 0) == 0.0f);
    CHECK (drumcontrols::setValue (maxx, *pitch, 0.4f, 1));
    CHECK (drumaxxParam (maxx.pluginState, 44 + 5 + 29) == 0.4f);
    CHECK (drumcontrols::value (maxx, *drumcontrols::find (InstrumentKind::drumaxx, "Pad 3 volume")) == 0.8f);
    // DrumSynth Live: the editor key's patch
    std::vector<bankstates::DrumSynthPatch> patches (1);
    patches[0].key = 50;
    patches[0].values.assign (38, 10);
    ChannelDef dsl;
    dsl.instrument = InstrumentKind::drumsynth;
    dsl.modelParams = bankstates::drumSynthParams (patches);
    CHECK (drumcontrols::defaultPart (dsl) == 50);
    const auto* level = drumcontrols::find (InstrumentKind::drumsynth, "Level");
    CHECK (drumcontrols::value (dsl, *level, 50) == 10.0f);
    CHECK (drumcontrols::value (dsl, *level, 51) == 0.0f);
    CHECK (drumcontrols::setValue (dsl, *level, 200.0f, 51) && drumcontrols::value (dsl, *level, 51) == 128.0f);
}

namespace design168p
{
lynx::drumranges::Range rangeOf (lynx::InstrumentKind kind, int index)
{
    using K = lynx::InstrumentKind;
    namespace r = lynx::drumranges;
    switch (kind)
    {
        case K::fruitkick: return r::fruitKick (index);
        case K::plucked: return r::plucked (index);
        case K::drumpad: return r::drumpad (index);
        case K::drumaxx: return r::drumaxx (index);
        case K::bassdrum: return r::bassDrum (index);
        case K::drumsynth: return r::drumSynth (index);
        case K::sakura: return r::sakura (index);
        default: return r::none;
    }
}
} // namespace design168p

TEST (design168p_automation_tables_match_the_engines)
{
    using namespace lynx;
    for (const auto kind : design168p::kinds)
    {
        const auto* table = modelparams::tableOf (kind);
        CHECK (table != nullptr && table->params != nullptr && ! table->params->empty());
        if (table == nullptr || table->params == nullptr)
            continue;
        const auto& params = *table->params;
        std::set<std::string> names;
        for (std::size_t i = 0; i < params.size(); ++i)
        {
            const auto& p = params[i];
            CHECK (i == 0 || params[i - 1].index < p.index); // sorted, unique
            CHECK (names.insert (p.name).second);
            CHECK (modelparams::find (kind, p.index) == &p);
            if (kind == InstrumentKind::slayer)
                continue; // (Slayer's ranges live in its engine: slayerParamMax)
            // the engine maps automation over the same range as the knob
            const auto r = design168p::rangeOf (kind, p.index);
            CHECK (r.valid && r.low == p.low && r.high == p.high && r.integer == p.stepped);
            if (! (r.valid && r.low == p.low && r.high == p.high && r.integer == p.stepped))
                std::printf ("  %s: range differs\n", p.name.c_str());
        }
    }
    CHECK (modelparams::tableOf (InstrumentKind::drumaxx)->params->size() == 16 * 42 + 13);
    CHECK (modelparams::findByName (InstrumentKind::drumaxx, "Pad 3 Pitch")->index == 2 * 44 + 5 + 29);
    CHECK (modelparams::findByName (InstrumentKind::drumsynth, "C5 Level")->index == drumSynthPatchParams * drumSynthKeyToSlot (60));
    CHECK (modelparams::tableOf (InstrumentKind::drumsynth)->params->size() == (std::size_t) (120 * 36));
    // values through the shared API: 0…1 over the range
    auto kick = *bankstates::channelFor ("fruitkick", { 100, -500, 70, 130, 20, 5 });
    CHECK (modelparams::setValue01 (kick, 5, 0.5f) && fruitKickParam (kick.pluginState, 5) == 64);
    CHECK (std::abs (*modelparams::value01 (kick, 0) - 2500.0f / 6000.0f) < 1e-6f);
}

TEST (design168p_engines_take_automation_while_playing)
{
    using namespace lynx;
    // a Fruit Kick hit keeps sounding when its knobs move (live parameter, no engine restart)
    auto def = *bankstates::channelFor ("fruitkick", { 600, -300, 90, 140, 16, 0 });
    auto inst = makeInstrument (InstrumentKind::fruitkick);
    inst->prepare (48000.0);
    inst->configure (def, 120.0);
    std::vector<float> l (4800), r (4800);
    inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
    inst->noteOn (60, 0.8f);
    inst->render (l.data(), r.data(), 2400, 1.0f, 1.0f);
    auto edited = def;
    CHECK (drumcontrols::setRawValue (edited, 5, 100.0f));
    inst->configure (edited, 120.0);
    inst->setParameter (3, 0.9f);
    std::fill (l.begin(), l.end(), 0.0f);
    inst->render (l.data(), r.data(), 2400, 1.0f, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 2400; ++i)
        peak = std::max (peak, std::abs (l[(std::size_t) i]));
    CHECK (peak > 0.01f); // (a restart would have dropped the hit)
    // every engine accepts every parameter of its table without trouble
    for (const auto kind : design168p::kinds)
    {
        auto e = makeInstrument (kind);
        e->prepare (48000.0);
        for (const auto& p : *modelparams::tableOf (kind)->params)
            e->setParameter (p.index, 0.5f);
        e->setParameter (-1, 0.5f);
        e->setParameter (100000, 0.5f);
    }
}
