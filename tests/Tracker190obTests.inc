// Round 190ob (LYNX 0.1.31): tracker follow-ups. Stage 2 start of the module import: the volume changes of sounding
// notes become the channels' Volume multiplier automation (core/ModuleImport.cpp), exact through .flp; the Tracker's
// default key and its place in the Channel rack's channel menu. (Round 190om makes exact volume changes pattern events:
// these tests import with volumeAsPatternEvents off, the automation clip path every other channel still takes.)

namespace tracker190ob
{
using namespace lynx;
using namespace tracker190nw;

// ProTracker M.K., speed 6 / tempo 125, one sample (64 frames, volume 64), one pattern. Channel 1: row 0 C-2 + F06,
// row 4 A04 (the volume slides down 4 a tick), row 8 C10 (volume 16, no note). Channel 2: row 2 C-2 (the same sample,
// sounding with channel 1's note), no volume change. Row 16 (channel 4): pattern break, the song ends.
inline std::vector<std::uint8_t> volumeModFile (bool secondChannel = true)
{
    std::vector<std::uint8_t> b;
    tracker190nw::putText (b, "lynx volume", 20);
    for (int s = 0; s < 31; ++s)
    {
        tracker190nw::putText (b, s == 0 ? "sine" : "", 22);
        const auto words = s == 0 ? 32 : 0;
        b.push_back ((std::uint8_t) (words >> 8));
        b.push_back ((std::uint8_t) words);
        b.push_back (0);
        b.push_back (s == 0 ? 64 : 0);
        b.push_back (0), b.push_back (0);
        b.push_back (0), b.push_back (s == 0 ? 16 : 1); // sample 1 loops (the notes hold)
    }
    b.push_back (1);
    b.push_back (127);
    for (int o = 0; o < 128; ++o)
        b.push_back (0);
    tracker190nw::putText (b, "M.K.", 4);
    std::vector<std::uint8_t> patterns (64 * 4 * 4, 0);
    auto cell = [&] (int row, int channel, int sample, int period, int fx, int param) {
        auto* c = &patterns[(std::size_t) ((row * 4 + channel) * 4)];
        c[0] = (std::uint8_t) ((sample & 0xF0) | ((period >> 8) & 0x0F));
        c[1] = (std::uint8_t) period;
        c[2] = (std::uint8_t) (((sample & 15) << 4) | fx);
        c[3] = (std::uint8_t) param;
    };
    cell (0, 0, 1, 428, 0xF, 0x06);
    cell (4, 0, 0, 0, 0xA, 0x04);
    cell (8, 0, 0, 0, 0xC, 0x10);
    if (secondChannel)
        cell (2, 1, 1, 428, 0, 0);
    cell (16, 3, 0, 0, 0xD, 0x00);
    b.insert (b.end(), patterns.begin(), patterns.end());
    for (auto v : tracker190nw::wave (64))
        b.push_back ((std::uint8_t) v);
    return b;
}

inline const Automation* volumeAutomationOf (const Song& s, int channel)
{
    for (const auto& a : s.automations)
        if (a.target.kind == AutomationTargetKind::channelAdjust && a.target.param == leveladjust::volume && a.target.index == channel)
            return &a;
    return nullptr;
}

// the gain the multiplier gives at song tick `tick`
inline double gainAt (const Automation& a, double tick) { return (double) levelAdjustGain (automationCurveAt (a.points, tick) * 2.0f); }
} // namespace tracker190ob

TEST (tracker190ob_volume_changes_become_volume_multiplier_automation)
{
    using namespace tracker190ob;
    const auto in = tracker190nw::importOf (volumeModFile (false), false);
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    const auto& s = *in.song;
    CHECK (s.channels.size() == 1 && in.summary.splitChannels == 0);
    const auto* a = volumeAutomationOf (s, 0);
    CHECK (a != nullptr && in.summary.volumeAutomations == 1);
    if (a == nullptr)
        return;
    // the note keeps its start's volume (64); rows are 240 ticks, a frame 40
    const auto* c2 = tracker190nw::noteAt (s, 0, 0, 60);
    CHECK (c2 != nullptr && std::abs (c2->velocity - 1.0f) < 1e-6f && c2->length == 17 * 240);
    // only Hold and straight segments
    CHECK (std::all_of (a->points.begin(), a->points.end(), [] (const AutomationPoint& p) { return p.mode == flcurve::hold || (p.mode == flcurve::singleCurve && p.tension == 0.0f); }));
    CHECK (std::abs (gainAt (*a, 0.0) - 1.0) < 1e-3 && std::abs (gainAt (*a, 4 * 240.0) - 1.0) < 1e-3);
    // row 4: 60, 56, 52, 48, 44 on frames 1..5 (libxmp's slide), each reached half a frame (20 ticks) after its frame
    // starts, in a straight line from the one before (halfway at the frame's start); then held
    for (int f = 1; f <= 5; ++f)
    {
        CHECK (std::abs (gainAt (*a, 4 * 240.0 + f * 40 + 20) - (64.0 - 4.0 * f) / 64.0) < 2e-3);
        CHECK (std::abs (gainAt (*a, 4 * 240.0 + f * 40) - (66.0 - 4.0 * f) / 64.0) < 2e-3);
    }
    CHECK (std::abs (gainAt (*a, 7 * 240.0) - 44.0 / 64.0) < 2e-3);
    // row 8: volume 16, a set on the row's first frame: a jump right there (libxmp sets it on that frame)
    CHECK (std::abs (gainAt (*a, 8 * 240.0 - 1) - 44.0 / 64.0) < 2e-3 && std::abs (gainAt (*a, 8 * 240.0) - 16.0 / 64.0) < 2e-3);
    CHECK (std::abs (gainAt (*a, 8 * 240.0 + 20) - 16.0 / 64.0) < 2e-3 && std::abs (gainAt (*a, 15 * 240.0) - 16.0 / 64.0) < 2e-3);
    // on the steps a .lynx project stores a point's value in (1/10000: the save keeps them exactly)
    CHECK (std::all_of (a->points.begin(), a->points.end(), [] (const AutomationPoint& p) {
        const auto raw = p.value * 10000.0f;
        return std::abs (raw - std::round (raw)) < 1e-3f;
    }));
    // its clip: from the song's start over the whole song
    const auto clip = std::find_if (s.playlist.begin(), s.playlist.end(), [&] (const Clip& c) { return c.kind == ClipKind::automation && c.automation == (int) (a - s.automations.data()); });
    CHECK (clip != s.playlist.end() && clip->start == 0 && clip->length == 17 * 240 && clip->track != 0);
    // the report: imported as automation, no longer "not imported"
    CHECK (in.report.converted.count ("Module effect as volume automation: volume slide (Axy)") == 1);
    CHECK (in.report.converted.count ("Module effect as volume automation: volume set on a sounding note (Cxx without a note)") == 1);
    CHECK (in.report.notReproduced.count ("Module effect not imported: volume slide (Axy)") == 0);
}

TEST (tracker190ob_an_instrument_in_two_module_channels_gets_two_channels)
{
    using namespace tracker190ob;
    const auto in = tracker190nw::importOf (volumeModFile (true), false);
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    const auto& s = *in.song;
    // channel 2's note sounds with channel 1's while channel 1's volume moves: a channel of its own
    CHECK (s.channels.size() == 2 && in.summary.splitChannels == 1);
    const auto* first = tracker190nw::noteAt (s, 0, 0, 60);
    const auto* second = tracker190nw::noteAt (s, 0, 2 * 240, 60);
    CHECK (first != nullptr && second != nullptr && first->channel != second->channel);
    if (first == nullptr || second == nullptr)
        return;
    CHECK (volumeAutomationOf (s, first->channel) != nullptr && volumeAutomationOf (s, second->channel) == nullptr);
    CHECK (s.channels.size() == 2 && s.channels[1].zones == s.channels[0].zones && s.channels[1].name.find ("(ch ") != std::string::npos);
}

TEST (tracker190ob_volume_automation_survives_the_flp_round_trip)
{
    using namespace tracker190ob;
    const auto in = tracker190nw::importOf (volumeModFile (false), false);
    CHECK (in.song.has_value());
    if (! in.song)
        return;
    auto s = *in.song;
    s.channels[0].instrument = InstrumentKind::synth; // (no sample files in a test)
    s.channels[0].zones.clear();
    flpexport::Options options;
    flpexport::Report report;
    const auto flp = flpexport::exportFlp (s, options, report);
    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return false; };
    const auto back = importFlp (flp::parseProject (flp), io);
    const auto* a = volumeAutomationOf (s, 0);
    const auto* b = volumeAutomationOf (back, 0);
    CHECK (a != nullptr && b != nullptr);
    if (a == nullptr || b == nullptr)
        return;
    // the same multiplier at every frame of the song
    bool same = true;
    for (int t = 0; t < 17 * 240; t += 20)
        same = same && std::abs (automationCurveAt (a->points, t) - automationCurveAt (b->points, t)) < 1e-6f;
    CHECK (same);
}

// The Tracker's default key: Shift+F7, beside FL's F7 (Piano roll). Nothing else in LYNX takes it in any context (FL
// 2026 neither: its ShortcutsModule has no Shift+F7, and FL did nothing on it in the live check of round 190ob).
TEST (tracker190ob_the_tracker_key_is_shift_f7_and_free)
{
    using namespace lynx;
    keys::KeyMap map;
    const auto& chords = map.chordsFor ("view.tracker");
    const keys::Chord shiftF7 { keys::f7, keys::shift };
    CHECK (chords.size() == 1 && chords.front() == shiftF7);
    CHECK (keys::toString (shiftF7) == "Shift+F7");
    for (const auto& [a, b] : map.conflicts())
        CHECK (a != "view.tracker" && b != "view.tracker");
    for (const auto context : { keys::Context::global, keys::Context::channelRack, keys::Context::pianoRoll, keys::Context::playlist, keys::Context::mixer, keys::Context::browser })
    {
        const auto others = map.actionsWith (shiftF7, context, "view.tracker");
        CHECK (others.empty());
    }
}
