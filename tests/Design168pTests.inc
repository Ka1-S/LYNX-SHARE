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
    // Plucked!: an empty (LYNX default) state has no knobs to edit, a bank state has
    ChannelDef empty;
    empty.instrument = InstrumentKind::plucked;
    CHECK (! drumcontrols::value (empty, *drumcontrols::find (InstrumentKind::plucked, "Decay")));
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
