// --- "Open without third-party plugins" (core/PluginHold.h) - round 190ap ---

namespace pluginsoff190ap
{
inline lynx::PluginRef ref (const char* id, const char* name, const char* state)
{
    lynx::PluginRef r;
    r.id = id;
    r.name = name;
    r.state = state;
    return r;
}

// two channels of one synth, a second synth, an insert effect, a Master effect, a Patcher on insert 2 with a generator
// and an effect node, and a LYNX substitute that keeps its original
inline lynx::Song song()
{
    auto s = lynx::Song::makeDefault();
    // (every channel and slot its own instance tag, as in a project: the rack gives a copy that kept its tag a new instance)
    while (s.channels.size() < 4)
    {
        s.channels.push_back (s.channels.front());
        s.channels.back().instance.renew();
    }
    while (s.mixer.size() < 4)
        s.mixer.push_back (s.mixer.back());
    s.channels[0].name = "Lead";
    s.channels[0].instrument = lynx::InstrumentKind::plugin;
    s.channels[0].plugin = ref ("synthA", "Synth A", "state-lead");
    s.channels[1].name = "Pad";
    s.channels[1].instrument = lynx::InstrumentKind::plugin;
    s.channels[1].plugin = ref ("synthA", "Synth A", "state-pad");
    s.channels[2].name = "Bass";
    s.channels[2].instrument = lynx::InstrumentKind::plugin;
    s.channels[2].plugin = ref ("synthB", "Synth B", "state-bass");
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::plugin;
    fx.plugin = ref ("verb", "Verb", "state-verb");
    s.mixer[1].name = "Vocals";
    s.mixer[1].effects.push_back (fx);
    fx.plugin = ref ("comp", "Comp", "state-comp");
    fx.instance.renew();
    s.mixer[0].effects.push_back (fx);
    auto patcher = lynx::makeEffectSlot (lynx::EffectKind::patcher);
    lynx::PatchNode generator;
    generator.generator = true;
    generator.channel.instrument = lynx::InstrumentKind::plugin;
    generator.channel.plugin = ref ("synthA", "Synth A", "state-in-patcher");
    lynx::PatchNode effect;
    effect.effect.kind = lynx::EffectKind::plugin;
    effect.effect.plugin = ref ("delay", "Delay", "state-delay");
    patcher.patcher.nodes.push_back (generator);
    patcher.patcher.nodes.push_back (effect);
    s.mixer[2].effects.push_back (patcher);
    auto substitute = lynx::makeEffectSlot (lynx::EffectKind::compressor);
    substitute.substituteFor = ref ("limiter", "Limiter", "state-limiter");
    s.mixer[3].effects.push_back (substitute);
    return s;
}
} // namespace pluginsoff190ap

TEST (pluginsoff190ap_state_holds_all_until_released)
{
    lynx::pluginhold::State hold;
    CHECK (! hold.holds ("synthA")); // not started: nothing is off
    hold.release ("synthA");         // (ignored while the mode is not on)
    hold.start();
    CHECK (hold.holds ("synthA") && hold.holds ("verb") && ! hold.holds (""));
    hold.release ("synthA");
    hold.release ("");
    CHECK (! hold.holds ("synthA") && hold.holds ("verb") && hold.active);
    hold.start(); // a new open: everything off again
    CHECK (hold.holds ("synthA"));
    hold.end();
    CHECK (! hold.holds ("synthA") && ! hold.holds ("verb") && hold.released.empty());
}

TEST (pluginsoff190ap_lists_every_hosted_plugin_once)
{
    const auto song = pluginsoff190ap::song();
    lynx::pluginhold::State hold;
    hold.start();
    const auto uses = lynx::pluginhold::uses (song, hold);
    CHECK (uses.size() == 5);
    if (uses.size() == 5)
    {
        CHECK (uses[0].id == "synthA" && uses[0].instruments == 3 && uses[0].effects == 0 && uses[0].off);
        CHECK (uses[0].usedBy.size() == 3 && uses[0].usedBy[0] == "Channel: Lead" && uses[0].usedBy[1] == "Channel: Pad");
        CHECK (uses[0].usedBy.size() == 3 && uses[0].usedBy[2].find ("Insert 2") == 0 && uses[0].usedBy[2].find ("(Patcher)") != std::string::npos);
        CHECK (uses[1].id == "synthB" && uses[1].instruments == 1);
        CHECK (uses[2].id == "comp" && uses[2].effects == 1 && uses[2].usedBy[0].find ("Master") == 0);
        CHECK (uses[3].id == "verb" && uses[3].usedBy[0] == "Insert 1: Vocals");
        CHECK (uses[4].id == "delay" && uses[4].effects == 1);
    }
    CHECK (lynx::pluginhold::countOff (song, hold) == 5);
    hold.release ("synthA");
    CHECK (lynx::pluginhold::countOff (song, hold) == 4);
    CHECK (! lynx::pluginhold::uses (song, hold)[0].off && lynx::pluginhold::uses (song, hold)[1].off);
    CHECK (lynx::pluginhold::countOff (song, lynx::pluginhold::State()) == 0);

    // the badges: channels 0-2 and inserts 0-2 at first; with Synth A on, insert 2 stays (its Patcher's Delay is off)
    lynx::pluginhold::State all;
    all.start();
    CHECK (lynx::pluginhold::channelsOff (song, all) == (std::vector<int> { 0, 1, 2 }));
    CHECK (lynx::pluginhold::insertsOff (song, all) == (std::vector<int> { 0, 1, 2 }));
    CHECK (lynx::pluginhold::channelsOff (song, hold) == (std::vector<int> { 2 }));
    CHECK (lynx::pluginhold::insertsOff (song, hold) == (std::vector<int> { 0, 1, 2 }));
    hold.release ("delay");
    CHECK (lynx::pluginhold::insertsOff (song, hold) == (std::vector<int> { 0, 1 }));
    CHECK (lynx::pluginhold::channelsOff (song, lynx::pluginhold::State()).empty());

    // every plugin's settings, the substitute's original included
    const auto states = lynx::pluginhold::pluginStates (song);
    CHECK (states.size() == 8);
    CHECK (std::count_if (states.begin(), states.end(), [] (const std::string& s) { return s.find ("|state-") != std::string::npos; }) == 8);
    CHECK (std::find (states.begin(), states.end(), "substitute|limiter|Limiter|state-limiter") != states.end());
}

TEST (pluginsoff190ap_rack_never_asks_for_a_plugin_that_is_off)
{
    // the rack as the engine builds it, with the host's rule in the factories: a plugin that is off yields nothing
    const auto song = pluginsoff190ap::song();
    const auto before = lynx::pluginhold::pluginStates (song);
    lynx::pluginhold::State hold;
    hold.start();
    std::vector<std::string> made;
    lynx::RackState::Factories factories;
    factories.pluginInstruments = [&] (const lynx::PluginRef& r, double) -> std::shared_ptr<lynx::Instrument> {
        if (hold.holds (r.id))
            return nullptr;
        made.push_back (r.id + "/" + r.state);
        return std::shared_ptr<lynx::Instrument> (lynx::makeInstrument (lynx::InstrumentKind::sampler, nullptr));
    };
    factories.pluginEffects = [&] (const lynx::PluginRef& r, double) -> std::shared_ptr<lynx::Effect> {
        if (hold.holds (r.id))
            return nullptr;
        made.push_back (r.id + "/" + r.state);
        return std::shared_ptr<lynx::Effect> (lynx::makeEffect (lynx::EffectKind::utility));
    };
    lynx::RackState shape; // what the engine keeps of the last build
    const auto build = [&] {
        auto state = lynx::RackState::build (song, shape.instruments.empty() ? nullptr : &shape, 44100.0, factories);
        shape.song.channels = state->song.channels;
        shape.song.mixer = state->song.mixer;
        shape.song.bpm = state->song.bpm;
        shape.instruments = state->instruments;
        shape.effects = state->effects;
        shape.serial = state->serial;
        shape.sampleRate = state->sampleRate;
        return state;
    };

    auto first = build();
    CHECK (made.empty());                                          // all off: nothing was made
    CHECK (first->instruments.size() == song.channels.size());     // and the rack is whole (silent / pass-through slots)
    CHECK (lynx::pluginhold::pluginStates (first->song) == before); // the settings ride along untouched

    // a plugin switched on without telling the rack: its slots keep their silent placeholders (same plugin, kept)
    hold.release ("synthA");
    build();
    CHECK (made.empty());
    // told (Factories::rebuildPlugin, for one build): exactly that plugin's slots are built again, with the settings
    // of the song; the Patcher that holds one is built anew, and its other plugin (still off) yields nothing
    CHECK (lynx::pluginhold::slotsOf (song, "synthA") == 3); // 2 channels + the Patcher
    factories.rebuildPlugin = [] (const lynx::PluginRef& r) { return r.id == "synthA"; };
    build();
    factories.rebuildPlugin = nullptr;
    CHECK (made.size() == 3);
    CHECK (std::count (made.begin(), made.end(), "synthA/state-lead") == 1 && std::count (made.begin(), made.end(), "synthA/state-pad") == 1
           && std::count (made.begin(), made.end(), "synthA/state-in-patcher") == 1);
    // an edit later: nothing is made twice
    made.clear();
    build();
    CHECK (made.empty());

    // all on: every slot once (the ones already running too: the engine saved their settings first)
    hold.end();
    CHECK (lynx::pluginhold::slotsOf (song, "") == 6); // 3 channels, 2 insert effects, the Patcher
    factories.rebuildPlugin = [] (const lynx::PluginRef&) { return true; };
    const auto last = build();
    factories.rebuildPlugin = nullptr;
    CHECK (std::count (made.begin(), made.end(), "synthB/state-bass") == 1 && std::count (made.begin(), made.end(), "verb/state-verb") == 1
           && std::count (made.begin(), made.end(), "comp/state-comp") == 1 && std::count (made.begin(), made.end(), "delay/state-delay") == 1);
    CHECK (made.size() == 7);
    CHECK (lynx::pluginhold::pluginStates (last->song) == before);
    made.clear();
    build();
    for (const auto& again : made)
        std::printf ("  made again after all were on: %s\n", again.c_str());
    CHECK (made.empty());
    CHECK (lynx::pluginhold::pluginStates (song) == before); // the song itself was never touched
    CHECK (lynx::pluginhold::slotsOf (song, "nope") == 0);
}
