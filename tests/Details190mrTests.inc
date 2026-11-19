// Round details8_190mr (0.1.30, details pass 8): FL's click-and-hold functions, Live's rack Auto Select, FL's undo /
// warning / layout options.

// FL's click-and-hold glue (DE-63): the touching note on the same channel and key, on either side; none across keys,
// channels or a gap
TEST (details190mr_touching_neighbour)
{
    using namespace lynx;
    Pattern p;
    auto note = [&p] (std::int64_t start, std::int64_t length, int key, int channel = 0) {
        Note n;
        n.start = start;
        n.length = length;
        n.key = key;
        n.channel = channel;
        p.notes.push_back (n);
    };
    note (0, 96, 60);    // 0
    note (96, 96, 60);   // 1: touches 0 on its right
    note (96, 96, 62);   // 2: another key
    note (192, 48, 60, 1); // 3: another channel
    note (240, 48, 60);  // 4: a gap after 1
    CHECK (details190mr::touchingNeighbour (p, 0, true) == 1);
    CHECK (details190mr::touchingNeighbour (p, 1, false) == 0);
    CHECK (details190mr::touchingNeighbour (p, 1, true) == -1);
    CHECK (details190mr::touchingNeighbour (p, 2, false) == -1);
    CHECK (details190mr::touchingNeighbour (p, 4, false) == -1);
    CHECK (details190mr::touchingNeighbour (p, 9, true) == -1);
    // the glue itself is FL's Glue notes (join): one note over both
    CHECK (noteedit::join (p, { 0, 1 }) == 1);
    CHECK (p.notes.size() == 4);
    CHECK (p.notes[0].start == 0 && p.notes[0].length == 192);
}

// FL's click-and-hold cycle while adding a note (DE-63): plain → slide → porta → plain
TEST (details190mr_hold_cycle)
{
    using namespace lynx;
    Note n;
    details190mr::setHoldCycle (n, 1);
    CHECK (n.slide && ! n.porta);
    details190mr::setHoldCycle (n, 2);
    CHECK (! n.slide && n.porta);
    details190mr::setHoldCycle (n, 3);
    CHECK (! n.slide && ! n.porta);
}

// Live's chain list Auto Select (DL-92): the chains that process now — audible, the selector in their chain-select zone,
// and (instruments) a sounding key in their key zone; a Drum Rack pad by its Receive note
TEST (details190mr_rack_processing_chains)
{
    using namespace lynx;
    RackDoc r;
    r.kind = RackDoc::Kind::instrument;
    r.chains.resize (3);
    r.chains[0].key = { 0, 59, 0, 59 };
    r.chains[1].key = { 60, 127, 60, 127 };
    r.chains[2].key = { 0, 127, 0, 127 };
    r.chains[2].active = false;
    std::array<std::uint64_t, 2> keys {};
    CHECK (details190mr::processingChains (r, keys).empty()); // nothing sounds: nothing processes
    keys[0] |= std::uint64_t (1) << 48;                 // C3 (48)
    CHECK ((details190mr::processingChains (r, keys) == std::vector<int> { 0 }));
    keys[1] |= std::uint64_t (1) << (72 - 64);          // and C5 (72)
    CHECK ((details190mr::processingChains (r, keys) == std::vector<int> { 0, 1 }));
    r.chains[1].solo = true; // a solo: only the soloed chain is heard
    CHECK ((details190mr::processingChains (r, keys) == std::vector<int> { 1 }));
    r.chains[1].solo = false;
    r.chains[1].select = { 10, 20, 10, 20 }; // the selector (0) outside its chain-select zone
    CHECK ((details190mr::processingChains (r, keys) == std::vector<int> { 0 }));
    r.selector = 15.0f;
    r.chains[0].select = { 0, 5, 0, 5 };
    CHECK ((details190mr::processingChains (r, keys) == std::vector<int> { 1 }));

    // an Audio Effect Rack: the selector alone decides
    RackDoc a;
    a.kind = RackDoc::Kind::audioEffect;
    a.chains.resize (2);
    a.chains[1].select = { 64, 127, 64, 127 };
    CHECK ((details190mr::processingChains (a, {}) == std::vector<int> { 0 }));
    a.selector = 100.0f;
    a.chains[0].select = { 0, 63, 0, 63 };
    CHECK ((details190mr::processingChains (a, {}) == std::vector<int> { 1 }));

    // a Drum Rack: the pad whose Receive note sounds; "All Notes" with any key
    RackDoc d;
    d.kind = RackDoc::Kind::instrument;
    d.drum = true;
    d.chains.resize (3);
    d.chains[0].receive = 36;
    d.chains[1].receive = 38;
    d.chains[2].receive = -1;
    std::array<std::uint64_t, 2> snare {};
    snare[0] |= std::uint64_t (1) << 38;
    CHECK ((details190mr::processingChains (d, snare) == std::vector<int> { 1, 2 }));
    CHECK (details190mr::processingChains (d, {}).empty());
}

// FL's options of this round (core/FlSettings.h): defaults, the options file, the data size choices
TEST (details190mr_fl_options)
{
    using namespace lynx;
    flsettings::reset();
    auto& o = flsettings::options();
    CHECK (! o.historyOnUndo.load() && o.projectSizeWarnMb.load() == 100 && o.projectWindowLayout.load());
    CHECK (! o.clickHold.load() && ! o.bothButtonsSteps.load()); // (FL 2026: both off, capture 190mr A1.1)
    CHECK (flsettings::parse ("flHistoryOnUndo", "1") && o.historyOnUndo.load());
    CHECK (flsettings::parse ("flClickHold", "1") && o.clickHold.load());
    CHECK (flsettings::parse ("flBothButtonsSteps", "1") && o.bothButtonsSteps.load());
    CHECK (flsettings::parse ("projectWindowLayout", "0") && ! o.projectWindowLayout.load());
    CHECK (flsettings::parse ("flProjectSizeWarnMb", "250") && o.projectSizeWarnMb.load() == 250);
    CHECK (flsettings::parse ("flProjectSizeWarnMb", "251") && o.projectSizeWarnMb.load() == 100); // (not a choice: FL's default)
    CHECK (flsettings::parse ("flProjectSizeWarnMb", "0") && o.projectSizeWarnMb.load() == 0); // "Don't warn me"
    CHECK (flsettings::parse ("flProjectSizeWarnMb", "x9") && o.projectSizeWarnMb.load() == 0); // (unreadable: kept)
    o.projectSizeWarnMb = 500;
    const auto text = flsettings::serialise();
    CHECK (text.find ("\nflHistoryOnUndo=1") != std::string::npos);
    CHECK (text.find ("\nflClickHold=1") != std::string::npos);
    CHECK (text.find ("\nflBothButtonsSteps=1") != std::string::npos);
    CHECK (text.find ("\nprojectWindowLayout=0") != std::string::npos);
    CHECK (text.find ("\nflProjectSizeWarnMb=500") != std::string::npos);
    CHECK (! flsettings::projectTooBig (600LL * 1024 * 1024, 0));
    CHECK (! flsettings::projectTooBig (50LL * 1024 * 1024, 50));
    CHECK (flsettings::projectTooBig (50LL * 1024 * 1024 + 1, 50));
    flsettings::reset();
    CHECK (! o.clickHold.load() && ! o.historyOnUndo.load() && o.projectSizeWarnMb.load() == 100);
}
