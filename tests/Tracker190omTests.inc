// Round 190om (LYNX 0.1.31): tracker stage 2. The module import's volume changes as pattern events of the channel's
// Volume multiplier (exact timing; the automation clip of round 190ob where a pattern's plays differ), the pitch changes
// of sounding notes as the notes' pitch curves (pitch slides, tone portamento on the sounding note, vibrato, arpeggio),
// several patterns' lanes on one target in the renderer, the Tracker's Bxx / Vxx, and SimSynth's velocity law.

namespace tracker190om
{
using namespace lynx;

struct Cell
{
    int pattern, row, channel, sample, period, fx, param;
};

// ProTracker M.K., one looping sample (64 frames, volume 64), speed 6 / tempo 125 (F06 on row 0 of every pattern), the
// order list `orders`, every pattern broken on row `rows` − 1 (channel 4)
inline std::vector<std::uint8_t> mod (const std::vector<Cell>& cells, const std::vector<int>& orders, int rows = 16)
{
    std::vector<std::uint8_t> b;
    tracker190nw::putText (b, "lynx 190om", 20);
    for (int s = 0; s < 31; ++s)
    {
        tracker190nw::putText (b, s == 0 ? "sine" : "", 22);
        const auto words = s == 0 ? 32 : 0;
        b.push_back ((std::uint8_t) (words >> 8));
        b.push_back ((std::uint8_t) words);
        b.push_back (0);
        b.push_back (s == 0 ? 64 : 0);
        b.push_back (0), b.push_back (0);
        b.push_back (0), b.push_back (s == 0 ? 32 : 1); // (the whole sample loops: notes hold)
    }
    b.push_back ((std::uint8_t) orders.size());
    b.push_back (127);
    int patterns = 0;
    for (int o = 0; o < 128; ++o)
    {
        const auto p = o < (int) orders.size() ? orders[(std::size_t) o] : 0;
        patterns = std::max (patterns, p + 1);
        b.push_back ((std::uint8_t) p);
    }
    tracker190nw::putText (b, "M.K.", 4);
    std::vector<std::uint8_t> data ((std::size_t) patterns * 64 * 4 * 4, 0);
    auto put = [&] (int pattern, int row, int channel, int sample, int period, int fx, int param) {
        auto* c = &data[(std::size_t) (((pattern * 64 + row) * 4 + channel) * 4)];
        c[0] = (std::uint8_t) ((sample & 0xF0) | ((period >> 8) & 0x0F));
        c[1] = (std::uint8_t) period;
        c[2] = (std::uint8_t) (((sample & 15) << 4) | fx);
        c[3] = (std::uint8_t) param;
    };
    for (int p = 0; p < patterns; ++p)
    {
        put (p, 0, 3, 0, 0, 0xF, 0x06);
        put (p, rows - 1, 3, 0, 0, 0xD, 0x00);
    }
    for (const auto& c : cells)
        put (c.pattern, c.row, c.channel, c.sample, c.period, c.fx, c.param);
    b.insert (b.end(), data.begin(), data.end());
    for (auto v : tracker190nw::wave (64))
        b.push_back ((std::uint8_t) v);
    return b;
}

inline float pitchAt (const Note& n, double tick) { return n.curves->valueAt (NoteCurveLane::pitch, tick, 0.0f); }
inline double cents (double fromPeriod, double toPeriod) { return 1200.0 * std::log2 (fromPeriod / toPeriod); }

inline const PatternEvents* volumeLaneOf (const Song& s, int pattern, int channel)
{
    return tracker::volumeLane (s.patterns[(std::size_t) pattern], channel);
}
inline double gainAt (const PatternEvents& lane, double tick) { return (double) levelAdjustGain (lane.valueAt (tick) * 2.0f); }
} // namespace tracker190om

TEST (tracker190om_a_pitch_slide_becomes_the_notes_pitch_curve)
{
    using namespace tracker190om;
    // row 0 C-2, row 1 104: the period goes down 4 a tick on frames 1..5 (rows of 240 ticks, frames of 40)
    const auto in = tracker190nw::importOf (mod ({ { 0, 0, 0, 1, 428, 0, 0 }, { 0, 1, 0, 0, 0, 0x1, 0x04 } }, { 0 }));
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    const auto& s = *in.song;
    const auto* n = tracker190nw::noteAt (s, 0, 0, 60);
    CHECK (n != nullptr && n->curves.has() && in.summary.pitchCurveNotes == 1);
    if (n == nullptr || ! n->curves.has())
        return;
    CHECK (std::abs (pitchAt (*n, 0.0)) < 0.01f && std::abs (pitchAt (*n, 240.0 + 20.0)) < 0.01f);
    for (int f = 1; f <= 5; ++f)
        CHECK (std::abs (pitchAt (*n, 240.0 + f * 40 + 20) - cents (428.0, 428.0 - 4.0 * f)) < 0.02);
    // held after the slide; stepped (each frame one value), on the steps a .lynx keeps
    CHECK (std::abs (pitchAt (*n, 5 * 240.0) - cents (428.0, 408.0)) < 0.02);
    for (const auto& q : n->curves->lane (NoteCurveLane::pitch))
        CHECK (std::abs (q.tick * 100.0f - std::round (q.tick * 100.0f)) < 1e-2f && std::abs (q.value * 100.0f - std::round (q.value * 100.0f)) < 1e-2f);
    CHECK (! n->porta);
    CHECK (in.report.converted.count ("Module effect as the note's pitch curve: pitch slide up (1xx)") == 1);
}

TEST (tracker190om_tone_portamento_slides_the_sounding_note_on)
{
    using namespace tracker190om;
    // row 0 C-2; row 4 E-2 + 308, rows 5-6 300: no new note, the C-2 slides 8 a tick to E-2 (400 cents) and stays
    const auto in = tracker190nw::importOf (
        mod ({ { 0, 0, 0, 1, 428, 0, 0 }, { 0, 4, 0, 0, 340, 0x3, 0x08 }, { 0, 5, 0, 0, 0, 0x3, 0x00 }, { 0, 6, 0, 0, 0, 0x3, 0x00 } }, { 0 }));
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    const auto& s = *in.song;
    CHECK (s.patterns[0].notes.size() == 1 && in.summary.continuedPortas == 1);
    const auto* n = tracker190nw::noteAt (s, 0, 0, 60);
    CHECK (n != nullptr && n->curves.has() && n->length == 16 * 240);
    if (n == nullptr || ! n->curves.has())
        return;
    CHECK (std::abs (pitchAt (*n, 4 * 240.0 - 1.0)) < 0.01f);
    CHECK (std::abs (pitchAt (*n, 4 * 240.0 + 40 + 20) - cents (428.0, 420.0)) < 0.5);
    CHECK (std::abs (pitchAt (*n, 8 * 240.0) - 400.0f) < 0.5f && std::abs (pitchAt (*n, 14 * 240.0) - 400.0f) < 0.5f);
    CHECK (in.report.converted.count ("Module tone portamentos on a sounding note: the note plays on and slides (no new note)") == 1);
}

TEST (tracker190om_an_arpeggio_steps_the_curve_each_frame)
{
    using namespace tracker190om;
    const auto in = tracker190nw::importOf (mod ({ { 0, 0, 0, 1, 428, 0x0, 0x47 } }, { 0 }));
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    const auto* n = tracker190nw::noteAt (*in.song, 0, 0, 60);
    CHECK (n != nullptr && n->curves.has());
    if (n == nullptr || ! n->curves.has())
        return;
    const float want[6] = { 0.0f, 400.0f, 700.0f, 0.0f, 400.0f, 700.0f };
    for (int f = 0; f < 6; ++f)
        CHECK (std::abs (pitchAt (*n, f * 40 + 20) - want[f]) < 0.5f);
    CHECK (std::abs (pitchAt (*n, 240.0 + 20)) < 0.5f); // (the row after: the key again)
}

TEST (tracker190om_volume_changes_are_pattern_events_where_every_play_agrees)
{
    using namespace tracker190om;
    // round 190ob's module: row 0 C-2, row 4 A04 (60, 56, 52, 48, 44 on frames 1..5), row 8 C10 (no note)
    const auto in = tracker190nw::importOf (tracker190ob::volumeModFile (false));
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    const auto& s = *in.song;
    CHECK (in.summary.volumeEventChannels == 1 && in.summary.volumeAutomations == 0 && tracker190ob::volumeAutomationOf (s, 0) == nullptr);
    const auto* lane = volumeLaneOf (s, 0, 0);
    CHECK (lane != nullptr);
    if (lane == nullptr)
        return;
    CHECK (std::abs (gainAt (*lane, 0.0) - 1.0) < 1e-3 && std::abs (gainAt (*lane, 4 * 240.0 + 39) - 1.0) < 1e-3);
    // each frame's volume from the frame's own tick on (a jump: the renderer plays a pattern event at its sample)
    for (int f = 1; f <= 5; ++f)
    {
        CHECK (std::abs (gainAt (*lane, 4 * 240.0 + f * 40) - (64.0 - 4.0 * f) / 64.0) < 2e-3);
        CHECK (std::abs (gainAt (*lane, 4 * 240.0 + f * 40 + 39) - (64.0 - 4.0 * f) / 64.0) < 2e-3);
    }
    CHECK (std::abs (gainAt (*lane, 8 * 240.0 - 1) - 44.0 / 64.0) < 2e-3 && std::abs (gainAt (*lane, 8 * 240.0) - 16.0 / 64.0) < 2e-3);
    CHECK (std::all_of (lane->events.begin(), lane->events.end(), [] (const PatternEvent& e) {
        const auto raw = e.value * 10000.0f;
        return std::abs (raw - std::round (raw)) < 1e-3f;
    }));
    // no automation clip in the Playlist
    CHECK (std::none_of (s.playlist.begin(), s.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::automation; }));
    // the 190ob way stays one option away (and for a channel whose plays differ)
    const auto clips = tracker190nw::importOf (tracker190ob::volumeModFile (false), false);
    CHECK (clips.song && clips.summary.volumeAutomations == 1 && clips.summary.volumeEventChannels == 0);
}

TEST (tracker190om_a_pattern_played_with_other_volumes_keeps_the_automation_clip)
{
    using namespace tracker190om;
    // pattern 1: C-2 + C20 (volume 32); pattern 2: C-2 + C10 (16); pattern 0: C-2 without a sample (the volume stays) and
    // A01 on row 2. Orders 1 0 2 0: pattern 0's note starts at 32 the first time, 16 the second
    const std::vector<Cell> cells = { { 1, 0, 0, 1, 428, 0xC, 0x20 }, { 2, 0, 0, 1, 428, 0xC, 0x10 }, { 0, 0, 0, 0, 428, 0, 0 }, { 0, 2, 0, 0, 0, 0xA, 0x01 } };
    const auto in = tracker190nw::importOf (mod (cells, { 1, 0, 2, 0 }, 8));
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    CHECK (in.summary.volumeEventChannels == 0 && in.summary.volumeAutomations == 1);
    CHECK (in.report.converted.count ("Module channels whose volume changes are pattern events (exact timing; the others an automation clip)") == 0);
}

TEST (tracker190om_lanes_of_several_patterns_on_one_target_each_play_their_pattern)
{
    using namespace lynx;
    // FL plays each pattern's events while the pattern plays; LYNX drove the target from the last pattern's lane only
    Song s;
    s.bpm = 120.0;
    s.channels.resize (1);
    s.channels[0].instrument = InstrumentKind::synth;
    s.mode = PlayMode::song;
    s.playlistTracks.resize (2);
    for (const float v : { 0.2f, 0.8f })
    {
        auto& p = s.patterns.emplace_back();
        p.lengthTicks = 4 * ppq;
        Note n;
        n.key = 60;
        n.length = ppq;
        p.notes.push_back (n);
        PatternEvents lane;
        lane.target = { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::volume };
        lane.events = { { 0, v }, { 2 * ppq, v + 0.1f } };
        p.events.push_back (lane);
    }
    s.playlist.clear();
    for (int k = 0; k < 2; ++k)
    {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = k;
        c.track = 0;
        c.start = k * 4 * ppq;
        c.length = 4 * ppq;
        s.playlist.push_back (c);
    }
    RackState::Factories factories;
    auto state = RackState::build (s, nullptr, 48000.0, factories);
    CHECK (state->eventLanes.size() == 2 && state->sharedLanes.size() == 1);
    if (state->eventLanes.size() != 2 || state->sharedLanes.size() != 1)
        return;
    const auto slot = state->eventLanes[0].shared;
    CHECK (slot == state->eventLanes[1].slot && state->eventLanes[1].shared == slot);
    RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> left (512), right (512);
    const auto tps = ticksPerSample (s.bpm, 48000.0);
    double tick = 0.0;
    int checked = 0;
    for (int b = 0; tick < 8.0 * ppq; ++b)
    {
        r.process (state.get(), true, tick, 512, left.data(), right.data(), nullptr);
        const auto v = r.automationValueForCheck (slot);
        const auto beat = tick / ppq;
        const auto want = beat < 2.0 ? 0.2f : beat < 4.0 ? 0.3f : beat < 6.0 ? 0.8f : 0.9f;
        if (std::fmod (beat, 2.0) > 0.1 && std::fmod (beat, 2.0) < 1.9) // (away from the events' own chunks)
        {
            CHECK (std::abs (v - want) < 1e-6f);
            ++checked;
        }
        tick += 512 * tps;
    }
    CHECK (checked > 20);
}

TEST (tracker190om_tracker_shows_bend_and_volume_fx_and_keeps_them_exact)
{
    using namespace lynx;
    using namespace lynx::tracker;
    Song s;
    s.channels.resize (1);
    auto& p = s.patterns.emplace_back();
    p.lengthTicks = 16 * ticksPerStep;
    const auto rt = ticksPerStep;
    Note n;
    n.start = 0;
    n.length = 8 * rt;
    NoteCurves c;
    c.lane (NoteCurveLane::pitch) = { { 0.0f, 0.0f }, { (float) (2 * rt + 7), 0.0f }, { (float) (2 * rt) + 7.25f, 300.0f } };
    n.curves = c;
    p.notes.push_back (n);
    p.events.push_back ({ { AutomationTargetKind::channelAdjust, 0, 0, leveladjust::volume }, { { 0, 0.5f }, { 3 * rt + 5, 0.25f } } });
    const Pattern original = p; // (p is s.patterns[0], which the steps below replace)
    BuildOptions o;
    auto l = build (s, 0, o);
    CHECK (l.tracks.size() == 1);
    if (l.tracks.size() != 1)
        return;
    const auto& t = l.tracks[0];
    // Bxx on the note (the pitch it ends at: +3 semitones), Vxx in the slot after the note's fx on rows 0 and 3
    const auto* placed = l.placedAt (0, 0, 0);
    CHECK (placed != nullptr && shownFx (l, *placed, original.notes[0]) == std::vector<Fx> { Fx::bend });
    CHECK (fxValueOf (original.notes[0], Fx::bend, n.length) == 0x83);
    CHECK (t.volumeSlot == 1 && t.fxColumns == 2 && t.volumeRows.size() == 16 && t.volumeRows[0] == 0x80 && t.volumeRows[3] == 0x40 && t.volumeRows[1] == -1);
    Cursor v { 0, 0, Field::fxValue, 1, 6 };
    CHECK (isVolumeSlot (l, v));
    // a Vxx typed on an empty row: one event at the row's start; Del takes it out again; values typed come back the same
    auto q = original;
    CHECK (setField (q, l, v, 0xC0));
    CHECK (q.events[0].events.size() == 3 && q.events[0].events[2].tick == 6 * rt && volumeValueOf (q.events[0].events[2].value) == 0xC0);
    s.patterns[0] = q;
    l = build (s, 0, o);
    CHECK (l.tracks[0].volumeRows[6] == 0xC0);
    CHECK (clearCell (q, l, v) && q.events[0].events.size() == 2);
    // a row inserted above row 3: the event of row 3 and the note's bend (row 2) move down with their rows
    q = original;
    l = build (s, 0, o);
    CHECK (insertRow (q, l, 0, 1));
    CHECK (q.events[0].events[1].tick == 4 * rt + 5 && q.notes[0].length == 9 * rt);
    CHECK (std::abs (tracker190om::pitchAt (q.notes[0], 3.0 * rt + 7.5) - 300.0f) < 0.01f && std::abs (tracker190om::pitchAt (q.notes[0], 3.0 * rt + 6.0)) < 0.01f);
    // and taken out again: back to the start
    s.patterns[0] = q;
    l = build (s, 0, o);
    CHECK (deleteRow (q, l, 0, 1));
    CHECK (q.events == original.events && q.notes[0].length == original.notes[0].length && q.notes[0].curves == original.notes[0].curves);
    // a typed Bxx: a glide over the note's first row; Del: no curve
    q = original;
    Cursor b { 0, 0, Field::fxValue, 0, 0 };
    CHECK (setField (q, l, b, 0x8C));
    CHECK (fxValueOf (q.notes[0], Fx::bend, q.notes[0].length) == 0x8C && q.notes[0].curves->lane (NoteCurveLane::pitch).size() == 2);
    CHECK (clearCell (q, l, b) && ! q.notes[0].curves.has());
    // the text clipboard carries Bxx
    const auto text = clipboardText (s, l, Selection::of ({ 0, 0, Field::note, 0, 0 }, { 0, 0, Field::note, 0, 15 }), true);
    CHECK (text.find ("B83") != std::string::npos);
}

TEST (tracker190om_simsynth_plays_the_velocity_as_fl_does)
{
    // FL 2026 /R renders (round 190om, simsynth/vellaw.txt): the level follows Init.Vol = (11^(v/100) − 1) / 10 of FL's
    // velocity v exactly (−52.3, −21.7, −12.7, −6.0, 0, +6.0 dB at 1, 25, 50, 75, 100, 127); nothing else moves with it
    const auto state = lynx::simsynth::defaultState();
    auto levelAt = [&] (int rev, float velocity) {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::simsynth;
        c.pluginState = state;
        c.engineRev = rev;
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::simsynth);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        std::vector<float> l (512), r (512);
        double energy = 0.0;
        for (int k = 0; k < 40; ++k)
        {
            if (k == 0)
                inst->noteOn (60, velocity);
            inst->render (l.data(), r.data(), 512, 1.0f, 1.0f);
            for (int i = 0; i < 512; ++i)
                energy += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
        }
        return 10.0 * std::log10 (std::max (1e-30, energy));
    };
    const auto ref = levelAt (2, 100.0f / 128.0f);
    CHECK (ref > -200.0);
    const std::pair<int, double> laws[] = { { 1, -52.299 }, { 25, -21.711 }, { 50, -12.703 }, { 75, -5.951 }, { 127, 6.028 } };
    for (const auto& [v, db] : laws)
        CHECK (std::abs (levelAt (2, (float) v / 128.0f) - ref - db) < 0.01);
    // revision 1 (a project saved before): every velocity at full level, bit for bit the same
    CHECK (levelAt (1, 25.0f / 128.0f) == levelAt (1, 100.0f / 128.0f) && levelAt (1, 100.0f / 128.0f) == levelAt (1, 1.0f));
    CHECK (std::abs (levelAt (1, 0.5f) - ref) < 1e-9); // (and velocity 100 under the law is that full level)
    // new channels take revision 2 or later (round 190pr: 3)
    CHECK (lynx::ChannelDef {}.engineRev >= 2);
}

TEST (tracker190om_survey_projects_whose_patterns_share_an_event_target)
{
    // (a survey for the gold check of the renderer change above: LYNX_SURVEY_190OM=1 and a folder of .flp files)
    if (realFlpDir.empty() || std::getenv ("LYNX_SURVEY_190OM") == nullptr)
        return;
    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return false; };
    int files = 0, shared = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator (realFlpDir))
    {
        if (entry.path().extension() != ".flp")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        try
        {
            const auto song = lynx::importFlp (flp::parseProject (bytes), o);
            ++files;
            std::map<std::tuple<int, int, int, int>, std::set<int>> patternsOf;
            for (int p = 0; p < (int) song.patterns.size(); ++p)
                for (const auto& lane : song.patterns[(std::size_t) p].events)
                    if (! lane.events.empty() && lane.target.kind != lynx::AutomationTargetKind::tempo && lane.target.kind != lynx::AutomationTargetKind::none)
                        patternsOf[{ (int) lane.target.kind, lane.target.index, lane.target.slot, lane.target.param }].insert (p);
            int targets = 0;
            for (const auto& [t, ps] : patternsOf)
                targets += ps.size() >= 2 ? 1 : 0;
            if (targets > 0)
            {
                ++shared;
                std::printf ("  shared-lanes %d target(s): %s\n", targets, entry.path().string().c_str());
            }
        }
        catch (...)
        {
        }
    }
    std::printf ("  %d projects, %d with a target several patterns' lanes drive\n", files, shared);
}

TEST (tracker190om_survey_a_module)
{
    // (a survey: LYNX_SURVEY_MODULE=<file> prints the import's summary and report)
    const char* path = std::getenv ("LYNX_SURVEY_MODULE");
    if (path == nullptr)
        return;
    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const auto r = tracker190nw::importOf (bytes, std::getenv ("LYNX_SURVEY_CLIPS") == nullptr);
    if (! r.song)
        return;
    const auto& m = r.summary;
    int lanes = 0;
    for (const auto& p : r.song->patterns)
        lanes += (int) p.events.size();
    std::printf ("  channels %d patterns %d notes %d | volume: changed notes %d, clips %d (%d points), event channels %d, lanes %d (%d events), split %d | "
                 "pitch curves %d (%d points), fine %d, porta on %d, other plays %d | pan channels %d | pattern lanes %d, automations %d\n",
                 (int) r.song->channels.size(), m.patterns, m.notes, m.volumeChangedNotes, m.volumeAutomations, m.volumePoints, m.volumeEventChannels,
                 m.volumeEventLanes, m.volumeEvents, m.splitChannels, m.pitchCurveNotes, m.pitchCurvePoints, m.fineTunedNotes, m.continuedPortas,
                 m.pitchDifferentPlays, m.panEventChannels, lanes, (int) r.song->automations.size());
    for (const auto* list : { &r.report.converted, &r.report.approximated, &r.report.notReproduced })
        for (const auto& [what, n] : *list)
            std::printf ("  report: %s x%d\n", what.c_str(), n);
}
