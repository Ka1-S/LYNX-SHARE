// Round 190df: FL's multilink menu (MLWheelMenu), sample history, channel envelope presets (.CHENV), the sampler's FL
// voice as the Envelope / instrument page sees it and the plugin spare state (core/FlControlMenus.h). FL facts in
// /mnt/e/lynx-work/flctl190df/FL-FACTS.md.
namespace flctl190df
{
inline std::vector<std::string> texts (const std::vector<lynx::mlmenu::Entry>& entries)
{
    std::vector<std::string> out;
    for (const auto& e : entries)
        out.push_back (e.kind == lynx::mlmenu::Entry::Kind::separator ? std::string ("-")
                       : e.kind == lynx::mlmenu::Entry::Kind::caption ? "[" + e.text + "]"
                                                                       : e.text);
    return out;
}
inline bool near (double a, double b, double tolerance = 1.0e-7) { return std::abs (a - b) <= tolerance; }
} // namespace flctl190df

TEST (flctl190df_multilink_menu_is_fls)
{
    using namespace lynx;
    // multilink on, two queued controls: FL's live menu (shot link190bt-09a), top to bottom
    const std::vector<std::string> on = { "[2 parameters selected]", "[Automation]", "Edit events", "Init song with these positions",
                                          "Init pattern with these positions", "-", "Create automation clips", "[Remote control]",
                                          "Link to controllers...", "Override global links...", "[Value]", "Randomize", "Humanize", "-",
                                          "Cancel" };
    CHECK (flctl190df::texts (mlmenu::build (true, 2)) == on);
    for (const auto& e : mlmenu::build (true, 2))
        CHECK (e.enabled);
    CHECK (mlmenu::captionFor (true, 1) == "1 parameter selected");
    CHECK (mlmenu::captionFor (true, 0) == "No parameter selected");
    // off: the last tweaked parameters, and no Cancel (FL hides it and its line)
    CHECK (mlmenu::captionFor (false, 1) == "Last tweaked parameter");
    CHECK (mlmenu::captionFor (false, 2) == "Last 2 tweaked parameters");
    CHECK (mlmenu::captionFor (false, 0) == "No parameter selected");
    const auto off = mlmenu::build (false, 2);
    CHECK (off.size() == on.size() - 2 && off.back().action == mlmenu::Action::humanize);
    // nothing to act on: every item greyed
    for (const auto& e : mlmenu::build (false, 0))
        if (e.kind == mlmenu::Entry::Kind::item)
            CHECK (! e.enabled);
    // Randomize / Humanize keep the queue while multilink is on; everything else ends it
    CHECK (! mlmenu::endsMultilink (mlmenu::Action::randomize, true) && ! mlmenu::endsMultilink (mlmenu::Action::humanize, true));
    CHECK (mlmenu::endsMultilink (mlmenu::Action::initSong, true) && mlmenu::endsMultilink (mlmenu::Action::cancel, true));
    CHECK (mlmenu::endsMultilink (mlmenu::Action::linkToControllers, true) && mlmenu::endsMultilink (mlmenu::Action::editEvents, true));
}

TEST (flctl190df_randomize_and_humanize_laws)
{
    using namespace lynx;
    using flctl190df::near;
    // Randomize: Random(2^30 + 1) over the whole range
    CHECK (mlmenu::randomize (0) == 0.0f);
    CHECK (mlmenu::randomize (0xFFFFFFFFu) == 1.0f);
    CHECK (near (mlmenu::randomize (0x80000000u), 0.5, 1.0e-6));
    // Humanize: at most an eighth of the range either way, clamped
    CHECK (near (mlmenu::humanize (0.5f, 0), 0.375));
    CHECK (near (mlmenu::humanize (0.5f, 0xFFFFFFFFu), 0.625, 1.0e-6));
    CHECK (near (mlmenu::humanize (0.5f, 0x80000000u), 0.5, 1.0e-6));
    CHECK (mlmenu::humanize (0.95f, 0xFFFFFFFFu) == 1.0f && mlmenu::humanize (0.05f, 0) == 0.0f);
    // the steps seen live (link190bt-09c: three Humanize steps of at most ~11 points)
    for (std::uint32_t r : { 1u, 12345u, 0x40000000u, 0xC0000000u, 0xFFFFFFFEu })
        CHECK (std::abs (mlmenu::humanize (0.4f, r) - 0.4f) <= 0.125f + 1.0e-6f);
}

TEST (flctl190df_init_pattern_writes_the_value_at_the_patterns_start)
{
    using namespace lynx;
    Song song;
    song.patterns.resize (2);
    const AutomationTarget t { AutomationTargetKind::channelVolume, 0, 0, 0 };
    CHECK (mlmenu::initPattern (song, 1, t, 0.3f));
    CHECK (song.patterns[1].events.size() == 1 && song.patterns[1].events[0].events.size() == 1);
    CHECK (song.patterns[1].events[0].events[0].tick == 0 && song.patterns[1].events[0].events[0].value == 0.3f);
    CHECK (song.patterns[0].events.empty());
    // a later event stays, the start event is replaced (one event per tick)
    song.patterns[1].events[0].events.push_back ({ 96, 0.9f });
    CHECK (mlmenu::initPattern (song, 1, t, 0.7f));
    CHECK (song.patterns[1].events[0].events.size() == 2 && song.patterns[1].events[0].events[0].value == 0.7f);
    CHECK (! mlmenu::initPattern (song, 1, t, 0.7f)); // (nothing changes)
    // a lane whose first event is later gets one at the start
    const AutomationTarget pan { AutomationTargetKind::channelPan, 0, 0, 0 };
    song.patterns[0].events.push_back ({ pan, { { 48, 0.2f } } });
    CHECK (mlmenu::initPattern (song, 0, pan, 0.5f));
    CHECK (song.patterns[0].events[0].events.size() == 2 && song.patterns[0].events[0].events[0].tick == 0
           && song.patterns[0].events[0].events[1].tick == 48);
    CHECK (! mlmenu::initPattern (song, 5, pan, 0.5f)); // (no such pattern)
}

TEST (flctl190df_sample_history_is_fls)
{
    using namespace lynx;
    auto& l = samplehistory::list();
    const auto kept = l;
    l.clear();
    CHECK (! samplehistory::step ("a.wav", -1)); // (fewer than two)
    samplehistory::add ("a.wav");
    samplehistory::add ("b.wav");
    samplehistory::add ("c.wav");
    CHECK ((l == std::vector<std::string> { "c.wav", "b.wav", "a.wav" })); // newest first
    samplehistory::add ("a.wav");                                           // already there: stays where it is
    CHECK (l.front() == "c.wav" && l.back() == "a.wav");
    samplehistory::add ("");
    CHECK (l.size() == 3);
    // the wheel: down = the next (older), up = the previous; the ends and an unknown sample do nothing
    CHECK (samplehistory::step ("c.wav", -1) == std::optional<std::string> ("b.wav"));
    CHECK (samplehistory::step ("b.wav", 1) == std::optional<std::string> ("c.wav"));
    CHECK (! samplehistory::step ("a.wav", -1) && ! samplehistory::step ("c.wav", 1) && ! samplehistory::step ("x.wav", -1));
    // at most 30: the oldest go
    for (int i = 0; i < 40; ++i)
        samplehistory::add ("s" + std::to_string (i) + ".wav");
    CHECK (l.size() == samplehistory::capacity && l.front() == "s39.wav" && l.back() == "s10.wav");
    l = kept;
}

TEST (flctl190df_channel_envelope_presets_round_trip)
{
    using namespace lynx;
    flvoice::Articulator a;
    a.flags = 1;
    a.envelope = true;
    a.delay = 1234, a.attack = 5000, a.hold = 600, a.decay = 30000, a.sustain = 64, a.release = 12000, a.amount = -50;
    a.lfoDelay = 200, a.lfoAttack = 300, a.lfoAmount = 40, a.lfoSpeed = 32000, a.lfoShape = 2;
    a.attackTension = -60, a.decayTension = 20, a.releaseTension = 128;
    const auto bytes = chenv::write (a);
    CHECK (bytes.size() == 68);
    std::int32_t r[17];
    std::memcpy (r, bytes.data(), 68);
    CHECK (r[0] == 1 && r[1] == -1 && r[2] == 1234 && r[14] == -60 && r[15] == 20 && r[16] == 128); // (the 0xDA record's order)
    const auto back = chenv::read (bytes);
    CHECK (back.has_value() && *back == a);
    CHECK (! chenv::read (std::vector<std::uint8_t> (40)).has_value());
}

TEST (flctl190df_sampler_fl_voice_page_reads_and_writes_fl_values)
{
    using namespace lynx;
    SamplerSettings s;
    CHECK (! flsamplervoice::has (s));
    s.flChannel.assign (90, 0);
    for (int i = 0; i < 90; ++i)
        s.flChannel[(std::size_t) i] = 1000 + i;
    s.flChannel[17 + 1] = 7; // the volume envelope "on" with a truthy value of FL's own
    CHECK (flsamplervoice::has (s));
    auto v = flsamplervoice::read (s);
    CHECK (v.articulators[flvoice::volume].envelope && v.articulators[flvoice::volume].delay == 1000 + 17 + 2);
    CHECK (v.articulators[flvoice::pitch].releaseTension == 1000 + 68 + 16 && v.modX == 1085 && v.filterType == 1087);
    const auto before = s.flChannel;
    flsamplervoice::write (s, v);
    CHECK (s.flChannel == before); // (read and written back: the same values, FL's truthy 7 kept)
    v.articulators[flvoice::volume].attackTension = -100;
    v.articulators[flvoice::volume].delay = 4321;
    v.articulators[flvoice::cutoff].envelope = false;
    flsamplervoice::write (s, v);
    CHECK (s.flChannel[17 + 14] == -100 && s.flChannel[17 + 2] == 4321 && s.flChannel[34 + 1] == 0);
    CHECK (s.flChannel[88] == 1088 && s.flChannel[89] == 1089); // PPQ and pan law untouched
}

TEST (flctl190df_spare_state_store_and_flip)
{
    using namespace lynx;
    sparestate::Store store;
    int pluginA = 0, pluginB = 0;
    CHECK (! store.flip (&pluginA, { 1 }).has_value()); // nothing stored: nothing to flip
    store.store (&pluginA, { 1, 2, 3 });
    CHECK (store.has (&pluginA) && ! store.has (&pluginB));
    const auto first = store.flip (&pluginA, { 9 });
    CHECK (first.has_value() && *first == std::vector<std::uint8_t> ({ 1, 2, 3 }));
    const auto second = store.flip (&pluginA, { 1, 2, 3 });
    CHECK (second.has_value() && *second == std::vector<std::uint8_t> ({ 9 })); // (A/B: back and forth)
    store.store (&pluginA, { 5 });
    CHECK (*store.find (&pluginA) == std::vector<std::uint8_t> ({ 5 }));
    store.forget (&pluginA);
    CHECK (! store.has (&pluginA));
}
