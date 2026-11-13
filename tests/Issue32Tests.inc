// Round 168s (issue #32, Majed Salih): the problems found while making a whole track (docs/analise/REA_rodada168s.md)
#define I32_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

namespace i32
{
// a hosted plugin effect stand-in: counts how many were made, keeps a "live setting" that only the instance knows
struct FakePluginFx final : lynx::Effect
{
    static inline int made = 0, destroyed = 0;
    float liveSetting = 0.0f;
    FakePluginFx() { ++made; }
    ~FakePluginFx() override { ++destroyed; }
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const lynx::EffectParams&, double) noexcept override {}
};

lynx::RackState::Factories fakePluginFactories()
{
    lynx::RackState::Factories f;
    f.pluginEffects = [] (const lynx::PluginRef&, double) { return std::make_shared<FakePluginFx>(); };
    f.samples = [] (const std::string& path) {
        auto s = std::make_shared<lynx::SampleData>();
        s->sampleRate = 44100.0;
        s->left.assign (44100, path.size() % 2 == 0 ? 0.25f : -0.25f);
        return std::shared_ptr<const lynx::SampleData> (s);
    };
    return f;
}

lynx::EffectSlot pluginSlot (const std::string& id)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::plugin);
    slot.plugin.id = id;
    slot.plugin.name = id;
    slot.plugin.state = "c3RhdGU="; // what the project saved (the plugin's live settings are newer)
    return slot;
}

lynx::ChannelDef samplerChannel (const std::string& path)
{
    lynx::ChannelDef c;
    c.name = path;
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = path;
    c.mixerTrack = 1;
    return c;
}
} // namespace i32

TEST (issue32_moving_a_plugin_in_the_effect_chain_keeps_its_instance)
{
    // #5 "When moving non native plugins inside the mixer chain up or down they reset all their parameters": the slot's
    // running plugin moves with it (main: both slots were made again from the saved state, every live change lost)
    lynx::Song song;
    song.mixer = lynx::Song::defaultMixer (3);
    song.mixer[1].effects = { i32::pluginSlot ("SynthMaster FX"), i32::pluginSlot ("Vital FX") };
    const auto f = i32::fakePluginFactories();
    i32::FakePluginFx::made = i32::FakePluginFx::destroyed = 0;
    auto a = lynx::RackState::build (song, nullptr, 48000.0, f);
    I32_REQUIRE (a->effects.size() > 1 && a->effects[1].size() == 2 && i32::FakePluginFx::made == 2);
    auto* first = a->effects[1][0].get();
    auto* second = a->effects[1][1].get();
    static_cast<i32::FakePluginFx*> (first)->liveSetting = 0.7f; // turned in its editor, not saved yet

    song.swapEffectSlots (1, 0, 1); // Move down
    song.uniqueInstances();
    auto b = lynx::RackState::build (song, a.get(), 48000.0, f);
    CHECK (i32::FakePluginFx::made == 2); // nothing made again
    CHECK (b->effects[1][0].get() == second && b->effects[1][1].get() == first);
    CHECK (static_cast<i32::FakePluginFx*> (b->effects[1][1].get())->liveSetting == 0.7f);

    // and to another insert (drag to insert 2): the same instance
    auto moved = song.mixer[1].effects[1];
    song.mixer[1].effects.erase (song.mixer[1].effects.begin() + 1);
    song.mixer[2].effects.push_back (moved);
    song.uniqueInstances();
    auto c = lynx::RackState::build (song, b.get(), 48000.0, f);
    CHECK (i32::FakePluginFx::made == 2);
    CHECK (c->effects[2].size() == 1 && c->effects[2][0].get() == first && c->effects[1][0].get() == second);

    // issue #36 (Presswerk crashed in its teardown after a slot move): with every older rack freed, no plugin was torn
    // down for the moves (main / PR #37 alone: a new instance was made and the old one destroyed)
    a.reset();
    b.reset();
    CHECK (i32::FakePluginFx::destroyed == 0);
    // two of the same plugin swapped (PR #37's duplicate case): each keeps its own instance
    song.mixer[1].effects.push_back (i32::pluginSlot ("SynthMaster FX"));
    song.uniqueInstances();
    auto d = lynx::RackState::build (song, c.get(), 48000.0, f);
    auto* x = d->effects[1][0].get();
    auto* y = d->effects[1][1].get();
    song.swapEffectSlots (1, 0, 1);
    song.uniqueInstances();
    auto e = lynx::RackState::build (song, d.get(), 48000.0, f);
    CHECK (e->effects[1][0].get() == y && e->effects[1][1].get() == x && i32::FakePluginFx::destroyed == 0);
}

TEST (issue36_retired_racks_go_to_the_owner_not_freed_in_the_collecting_callback)
{
    // issue #36: the racks the audio thread returns are handed to `dispose` (the engine frees them from a message of
    // their own) instead of being deleted inside collectGarbage(), which ran in the engine's timer callback
    struct Probe
    {
        int* alive;
        explicit Probe (int* a) : alive (a) { ++*alive; }
        ~Probe() { --*alive; }
    };
    int alive = 0;
    std::vector<const Probe*> handed;
    {
        lynx::SnapshotExchange<Probe> x;
        x.dispose = [&handed] (const Probe* p) { handed.push_back (p); };
        x.publish (std::make_unique<Probe> (&alive));
        x.acquire();
        x.publish (std::make_unique<Probe> (&alive));
        x.acquire(); // the first one is returned
        x.collectGarbage();
        CHECK (handed.size() == 1 && alive == 2); // not freed by collectGarbage
        for (const auto* p : handed)
            delete p;
        CHECK (alive == 1);
    }
    CHECK (alive == 0); // the exchange still frees what it holds itself
}

TEST (issue32_a_pasted_copy_of_a_plugin_slot_gets_its_own_instance_once)
{
    // a duplicated slot (paste, preset copy) shares the original's tag: it gets its own instance, made once, and later
    // edits rebuild nothing
    lynx::Song song;
    song.mixer = lynx::Song::defaultMixer (2);
    song.mixer[1].effects = { i32::pluginSlot ("Vital FX") };
    const auto f = i32::fakePluginFactories();
    i32::FakePluginFx::made = 0;
    song.uniqueInstances();
    auto a = lynx::RackState::build (song, nullptr, 48000.0, f);
    song.mixer[1].effects.push_back (song.mixer[1].effects[0]);
    song.uniqueInstances();
    CHECK (song.mixer[1].effects[0].instance.id != song.mixer[1].effects[1].instance.id);
    auto b = lynx::RackState::build (song, a.get(), 48000.0, f);
    CHECK (i32::FakePluginFx::made == 2 && b->effects[1][0].get() == a->effects[1][0].get());
    song.mixer[1].volume = 0.5f;
    song.uniqueInstances();
    auto c = lynx::RackState::build (song, b.get(), 48000.0, f);
    CHECK (i32::FakePluginFx::made == 2 && c->effects[1][1].get() == b->effects[1][1].get());
    // the slots still compare equal (the tag is identity, not content)
    CHECK (song.mixer[1].effects[0] == song.mixer[1].effects[1]);
}

TEST (issue32_adding_a_channel_while_playing_keeps_every_other_channels_instrument)
{
    // #4 "The playback of the whole track is affected (reset sounds and loops) when adding new sounds": a channel
    // inserted above others (FL puts it after the selected channel) shifted every later channel, and each was made
    // again (new sample, no voices) or took its neighbour's instrument
    lynx::Song song;
    song.mixer = lynx::Song::defaultMixer (2);
    song.channels = { i32::samplerChannel ("kick.wav"), i32::samplerChannel ("loop1.wav"), i32::samplerChannel ("pad.wav") };
    song.patterns.emplace_back();
    const auto f = i32::fakePluginFactories();
    song.uniqueInstances();
    auto a = lynx::RackState::build (song, nullptr, 48000.0, f);
    I32_REQUIRE (a->instruments.size() == 3);

    lynx::insertChannel (song, 1, i32::samplerChannel ("snare.wav"));
    song.uniqueInstances();
    auto b = lynx::RackState::build (song, a.get(), 48000.0, f);
    I32_REQUIRE (b->instruments.size() == 4);
    CHECK (b->instruments[0].get() == a->instruments[0].get());
    CHECK (b->instruments[2].get() == a->instruments[1].get());
    CHECK (b->instruments[3].get() == a->instruments[2].get());
    CHECK (b->channelsMoved && b->channelFrom == (std::vector<int> { 0, -1, 1, 2 }));

    // removing it again: back where they were
    lynx::removeChannels (song, { 1 });
    song.uniqueInstances();
    auto c = lynx::RackState::build (song, b.get(), 48000.0, f);
    I32_REQUIRE (c->instruments.size() == 3);
    CHECK (c->instruments[1].get() == a->instruments[1].get() && c->instruments[2].get() == a->instruments[2].get());

    // a clone (after the original) is a new channel: the original keeps its instrument
    lynx::cloneChannel (song, 1);
    song.uniqueInstances();
    auto d = lynx::RackState::build (song, c.get(), 48000.0, f);
    I32_REQUIRE (d->instruments.size() == 4);
    CHECK (d->instruments[1].get() == a->instruments[1].get() && d->instruments[2].get() != a->instruments[1].get());
    CHECK (d->instruments[3].get() == a->instruments[2].get());
}

TEST (issue32_a_note_sounding_on_a_shifted_channel_keeps_sounding)
{
    // the renderer side of #4: a held loop on channel 1 keeps playing through the edit that puts a channel above it
    lynx::Song song;
    song.mixer = lynx::Song::defaultMixer (2);
    song.channels = { i32::samplerChannel ("loop1.wav") };
    song.patterns.emplace_back();
    const auto f = i32::fakePluginFactories();
    song.uniqueInstances();
    auto a = lynx::RackState::build (song, nullptr, 48000.0, f);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.type = lynx::NoteEvent::Type::on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 0.8f;
    live.push (on);
    std::vector<float> l (256), rr (256);
    for (int i = 0; i < 8; ++i)
        r.process (a.get(), false, 0.0, 256, l.data(), rr.data(), &live);
    CHECK (std::abs (l[128]) > 0.01f);
    lynx::insertChannel (song, 0, i32::samplerChannel ("kick.wav"));
    song.uniqueInstances();
    auto b = lynx::RackState::build (song, a.get(), 48000.0, f);
    r.process (b.get(), false, 0.0, 256, l.data(), rr.data(), &live);
    r.process (b.get(), false, 0.0, 256, l.data(), rr.data(), &live);
    CHECK (std::abs (l[128]) > 0.01f); // still sounding (main: the loop's sampler was made again, silent)
    CHECK ((r.soundingKeys (1)[0] >> 60) & 1u); // and it shows on its channel, now number 1
}

TEST (issue32_the_clicked_playlist_comes_to_front_like_fl)
{
    // #8 "The sequencer window is not always on top. Some windows just get stuck on top until pressing F6 or F7": in FL
    // a click on the Playlist brings it over the Channel Rack and F6 brings the rack back (r168s f02 → f03 → f04); LYNX
    // kept the Playlist behind every floating panel. Only the Browser (docked) stays at the back.
    using namespace lynx::workspace;
    auto l = Layout::makeDefault (1600, 900);
    l.show (Panel::mixer);
    auto front = [&l] { return l.stacking().back(); };
    CHECK (front() == Panel::mixer);
    l.bringToFront (Panel::playlist); // the click on the Playlist
    CHECK (front() == Panel::playlist);
    CHECK (l.toggle (Panel::channelRack) && front() == Panel::channelRack); // F6: visible but covered → to front
    l.bringToFront (Panel::browser);
    CHECK (l.stacking().front() == Panel::browser && front() == Panel::channelRack);
    CHECK (isBackground (Panel::browser) && ! isBackground (Panel::playlist));
}

TEST (issue32_one_wheel_notch_moves_a_fader_one_fl_step)
{
    // #2 "faders can react suddenly on the mouse wheel": one notch over an FL fader = 2 % of its travel (r168s c_f07: 0 dB
    // → +0.6 dB); LYNX read a Windows notch (JUCE deltaY 0.234) as 1.95 notches and moved 3.9 %
    constexpr double windowsNotch = 120.0 * 0.5 / 256.0, linuxNotch = 50.0 / 256.0;
    CHECK (lynx::controls::wheelNotches (windowsNotch, windowsNotch) == 1.0);
    CHECK (lynx::controls::wheelNotches (-windowsNotch, windowsNotch) == -1.0);
    CHECK (lynx::controls::wheelNotches (linuxNotch, linuxNotch) == 1.0);
    CHECK (lynx::controls::wheelNotches (3 * windowsNotch, windowsNotch) == 3.0);
    CHECK (lynx::controls::wheelNotches (windowsNotch / 4, windowsNotch) == 1.0); // a fine wheel's part-notch: still a step
    CHECK (lynx::controls::wheelNotches (0.0, windowsNotch) == 0.0);
    const auto after = lynx::controls::wheelValue (lynx::unityFader, lynx::controls::wheelNotches (windowsNotch, windowsNotch), 0.0, 1.0, false);
    CHECK (std::abs (after - 0.82) < 1e-6); // (unityFader is a float)
    CHECK (std::abs (20.0 * std::log10 (lynx::faderGain ((float) after)) - 0.6) < 0.05); // FL's hint: "0.6dB"
}

TEST (issue32_fit_to_tempo_keeps_a_loop_in_time)
{
    // #7 "Fit to project tempo stretch sometimes out of tempo": main assumed 1, 2, 4, 8... beats, so a 3-bar loop came
    // out 16 beats long and a loop with a tail was squeezed so its beats came early
    using lynx::clipops::fitBeats;
    const auto threeBars = 12.0 * 60.0 / 100.0; // 3 bars at 100 BPM in a 128 BPM project
    CHECK (std::abs (fitBeats (threeBars, 128.0, 0.0f, 100.0) - 12.0f) < 1e-4f); // main: 16
    const auto withTail = 16.0 * 60.0 / 90.0 + 0.3; // 4 bars at 90 BPM + a 0.3 s tail, at 100 BPM
    CHECK (std::abs (fitBeats (withTail, 100.0, 0.0f, 90.0) - 16.45f) < 1e-3f); // main: 16 (the loop 2.7 % short)
    CHECK (fitBeats (withTail, 100.0, 6.0f, 90.0) == 6.0f); // a WAV's ACID beats win ("embedded")
    CHECK (std::abs (fitBeats (16.0 * 60.0 / 90.0, 140.0, 0.0f, 90.0) - 32.0f) < 1e-3f); // the octave nearest 140: 180 BPM
    CHECK (fitBeats (2.4, 128.0) == 4.0f); // nothing known: a power of two, as before
    const auto presets = lynx::clipops::timePresetBeats (lynx::Meter (4, 4));
    CHECK (presets == (std::vector<float> { 1.0f, 2.0f, 4.0f, 8.0f, 12.0f, 16.0f }));

    // the whole path: a click loop of 3 bars at 100 BPM with a tail, detected (no ACID chunk)
    lynx::SampleData s;
    s.sampleRate = 44100.0;
    const auto beatFrames = 44100.0 * 60.0 / 100.0;
    s.left.assign ((std::size_t) (12 * beatFrames + 0.25 * 44100.0), 0.0f);
    for (int b = 0; b < 12; ++b)
        for (int i = 0; i < 600; ++i)
            s.left[(std::size_t) (b * beatFrames) + (std::size_t) i] = (b % 4 == 0 ? 0.9f : 0.5f) * std::exp (-i / 120.0f) * (i % 20 < 10 ? 1.0f : -1.0f);
    const auto got = lynx::tempodetect::fitBeatsOf (&s, 128.0);
    const auto want = (float) ((double) s.left.size() / 44100.0 * 100.0 / 60.0); // 12.42 beats: the loop stays 12 beats long
    CHECK (std::abs (got - want) < 0.05f);
}

TEST (digiwheel168s_number_boxes_are_dark_displays_red_only_while_touched)
{
    // RANGE / TRACK in the channel settings header and the rack's insert box: FL draws its digiwheels as small dark
    // displays, not filled buttons; in the LYNX skin the accent (red) shows only while hovered or dragged
    using namespace lynx::themes;
    for (const auto& t : builtInThemes())
    {
        const auto p = derivePalette (t.accent, t.background);
        const auto at = [&p] (bool hot, bool dimmed)
        { return digitDisplayColours (p.bg0, p.border, p.accent, p.accentHover, p.textPrimary, p.textDisabled, hot, dimmed); };
        const auto rest = at (false, false);
        CHECK (rest.fill == p.bg0 && rest.edge == p.border && rest.text == p.textPrimary); // no red at rest
        CHECK (rest.fill != p.accent && rest.edge != p.accent && rest.text != p.accent);
        CHECK (contrastRatio (rest.text, rest.fill) >= 7.0);
        CHECK (at (false, true).text == p.textDisabled); // "-": no insert
        const auto hot = at (true, true);
        CHECK (hot.fill == p.bg0 && hot.edge == p.accent && hot.text == p.accentHover); // red while touched, still dark
        CHECK (contrastRatio (hot.text, hot.fill) >= 3.0);
    }
}
