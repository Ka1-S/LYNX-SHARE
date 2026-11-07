// Round 177b: FL's automation clip LFO ("LFO mode", REA FUN_00fe8640 / FUN_00ba1700 / FUN_00ba0710 / FUN_00ba03d0;
// docs/analise/REA_rodada177b.md §LFO). Event 0xEA keeps the LFO after the points; LYNX reads it, plays it and writes
// it back byte for byte.
namespace lfo177b
{
// the 112 bytes FL 2026 writes after the points of a clip whose LFO was never touched [DATA: every clip of FL 26's demos]
const std::uint8_t untouchedTail[112] = {
    0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xf0, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfb, 0xb2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

std::vector<std::uint8_t> automationEvent (const std::vector<std::uint8_t>& file)
{
    flp::EventReader reader (file);
    flp::Event e;
    while (reader.next (e))
        if (e.id == 0xEA)
            return { e.data.begin(), e.data.end() };
    return {};
}

flp::Project projectWith (const flp::AutomationLfo& lfo, int minLevel = 0, int maxLevel = 12800)
{
    flp::Project p;
    flp::Channel c;
    c.index = 0;
    c.type = flp::ChannelType::automationClip;
    c.name = "Cutoff";
    c.hasLevels = true; // a clip's channel levels are its range (pan = minimum, volume = maximum)
    c.pan = minLevel;
    c.volume = maxLevel;
    c.automation = { { 0.0, 0.25, 0.0f, 0 }, { 4.0, 0.75, 0.0f, 0 } };
    c.automationLfo = lfo;
    p.channels.push_back (c);
    return p;
}

// FL's "Astes - Raubana - LIFE.flp" ch 42 [DATA]: level 128, add, speed 35989, skew −128, LFO on, two amplitude
// points (0, 0.5, curve 7) → (64 beats, 1.0, curve 7), one with FL's own bytes after the curve
flp::AutomationLfo lifeLfo()
{
    flp::AutomationLfo lfo;
    lfo.present = true;
    lfo.level = 128;
    lfo.mode = 1;
    lfo.enabled = -1;
    lfo.speed = 35989;
    lfo.skew = -128;
    lfo.points = { { 0.0, 0.5, 0.0f, 7 }, { 64.0, 1.0, 0.0f, 7 } };
    // the records as FL wrote them: Δx, y, tension, curve, then three bytes LYNX does not know (kept as read)
    auto record = [] (double dx, double y, std::uint8_t curve, std::uint8_t extra) {
        std::vector<std::uint8_t> r (24, 0);
        std::memcpy (r.data(), &dx, 8);
        std::memcpy (r.data() + 8, &y, 8);
        r[20] = curve;
        r[21] = extra;
        return r;
    };
    lfo.rawPoints = record (0.0, 0.5, 7, 0);
    const auto second = record (64.0, 1.0, 7, 0x2A);
    lfo.rawPoints.insert (lfo.rawPoints.end(), second.begin(), second.end());
    return lfo;
}
} // namespace lfo177b

TEST (lfo177b_untouched_clip_writes_fls_tail)
{
    const auto event = lfo177b::automationEvent (flp::writeProject (lfo177b::projectWith ({})));
    CHECK (event.size() == 21 + 2 * 24 + 112);
    CHECK (event.size() >= 112 && std::equal (event.end() - 112, event.end(), std::begin (lfo177b::untouchedTail)));
    CHECK (event[4] == 64 && event[8] == 0); // header: LFO level 64, multiply
    const auto back = flp::parseProject (flp::writeProject (lfo177b::projectWith ({})));
    CHECK (back.channels.size() == 1 && back.channels[0].automationLfo.present && back.channels[0].automationLfo.enabled == 0);
}

TEST (lfo177b_flp_round_trip_is_byte_exact)
{
    const auto lfo = lfo177b::lifeLfo();
    const auto file = flp::writeProject (lfo177b::projectWith (lfo));
    const auto event = lfo177b::automationEvent (file);
    CHECK (event.size() == 21 + 2 * 24 + 112 + 24); // FL's 136-byte trailer for two LFO points
    const auto back = flp::parseProject (file);
    CHECK (back.channels.size() == 1);
    const auto& f = back.channels[0].automationLfo;
    CHECK (f.present && f.level == 128 && f.mode == 1 && f.enabled == -1 && f.speed == 35989 && f.skew == -128);
    CHECK (f.points.size() == 2 && f.points[1].position == 64.0 && f.points[1].value == 1.0 && f.points[1].curve == 7);
    CHECK (f.rawPoints == lfo.rawPoints);

    // through LYNX's song and back: the same event, byte for byte
    using namespace lynx;
    auto song = importFlp (back, fx171e::importOptions());
    CHECK (song.automations.size() == 1);
    const auto& a = song.automations[0];
    CHECK (a.lfo.on && a.lfo.add && a.lfo.level == 128 && a.lfo.speed == 35989 && a.lfo.skew == -128);
    CHECK (a.lfo.amp.size() == 2 && a.lfo.amp[1].tick == 64 * ppq && a.lfo.amp[0].value == 0.5f);
    flpexport::Options options;
    flpexport::Report report;
    const auto again = lfo177b::automationEvent (flpexport::exportFlp (song, options, report));
    CHECK (again.size() == event.size());
    CHECK (again.size() >= 136 && std::equal (again.end() - 136, again.end(), event.end() - 136));
    CHECK (again.size() > 8 && again[4] == event[4] && again[8] == event[8]);

    // an edited amplitude point is written from the model (the unknown bytes go)
    song.automations[0].lfo.amp[0].value = 0.25f;
    const auto edited = flp::parseProject (flpexport::exportFlp (song, options, report));
    for (const auto& c : edited.channels)
        if (c.type == flp::ChannelType::automationClip)
            CHECK (c.automationLfo.points.size() == 2 && c.automationLfo.points[0].value == 0.25 && c.automationLfo.rawPoints[21 + 24] == 0);
}

TEST (lfo177b_export_scales_the_level_by_a_partial_range)
{
    // FL scales the LFO by the clip's range (amt = level/128 * range); the export always writes the whole range, so
    // the range moves into the level. The played amount stays the same, the header byte does not.
    using namespace lynx;
    const auto file = flp::writeProject (lfo177b::projectWith (lfo177b::lifeLfo(), 6400, 10000));
    const auto song = importFlp (flp::parseProject (file), fx171e::importOptions());
    CHECK (song.automations.size() == 1);
    CHECK (std::abs (song.automations[0].lfo.range - 0.28125f) < 1e-6f);
    flpexport::Options options;
    flpexport::Report report;
    const auto again = lfo177b::automationEvent (flpexport::exportFlp (song, options, report));
    CHECK (again.size() > 8 && again[4] == 36 && again[8] == 1); // 128 * 3600 / 12800, still "add"
}

TEST (lfo177b_rate_and_shape_match_fl)
{
    using L = lynx::AutomationLfo;
    // FL's stored speeds [DATA + DECOMP FUN_00ba03d0]: 45819 = 1 beat, 40898 = 2, 26257 = 16, 65536 = 1/16
    CHECK (std::abs (1.0 / L::cyclesPerBeat (45819) - 1.0) < 0.001);
    CHECK (std::abs (1.0 / L::cyclesPerBeat (40898) - 2.0) < 0.002);
    CHECK (std::abs (1.0 / L::cyclesPerBeat (26257) - 16.0) < 0.01);
    CHECK (std::abs (L::cyclesPerBeat (65536) - 16.0) < 0.01);
    CHECK (L::speedForBeats (1.0) == 45819); // FL's new-clip default (FUN_00ba0490 (1.0))
    CHECK (L::speedForBeats (2.0) == 40898);

    // the default waveform: a triangle falling from +1 at phase 0 to −1 at the middle, then rising back
    CHECK (std::abs (L::shape (0u, 0, 0, 0, false) - 1.0f) < 1.0e-4f);           // the start of the falling ramp: +1
    CHECK (std::abs (L::shape (0x80000000u, 0, 0, 0, false) + 1.0f) < 1.0e-4f);  // the bottom
    CHECK (std::abs (L::shape (0xC0000000u, 0, 0, 0, false)) < 1.0e-4f);         // halfway up: 0
    CHECK (L::shape (0xF0000000u, 0, 0, 64, false) == 1.0f);                       // pulse width > 0: a flat top
    CHECK (L::shape (0x60000000u, 0, 0, -64, false) == -1.0f);                     // < 0: a flat bottom
    // tension bends the ramp but keeps its ends and its sign
    const auto bent = L::shape (0xE0000000u, 64, 0, 0, false), straight = L::shape (0xE0000000u, 0, 0, 0, false);
    CHECK (bent > 0.0f && std::abs (bent - straight) > 0.05f);

    // played: the LFO starts at 0 going up (FUN_00ba06a0), a quarter cycle later it is at its top
    lynx::Automation a;
    a.points = { { 0, 0.5f, 0.0f } };
    a.lfo.on = true;
    a.lfo.add = true;
    a.lfo.level = 64; // ± 0.25 · range: FL's renders show the swing at half the waveform (L = ±0.5)
    CHECK (std::abs (a.playAt (0.0, 0.0) - 0.5f) < 0.01f);
    CHECK (std::abs (a.playAt (lynx::ppq * 0.25, 0.0) - 0.75f) < 0.01f);
    CHECK (std::abs (a.playAt (lynx::ppq * 0.75, 0.0) - 0.25f) < 0.01f);
    // multiply (FL's usual): value · (1 + 2·L·level/128), L = ±0.5 at most
    a.lfo.add = false;
    a.lfo.level = 32;
    CHECK (std::abs (a.playAt (lynx::ppq * 0.25, 0.0) - 0.625f) < 0.01f);
    // FL's level 128 in multiply mode reaches 0 only at the waveform's very bottom
    a.lfo.level = 128;
    CHECK (std::abs (a.playAt (lynx::ppq * 0.75, 0.0) - 0.0f) < 0.01f);
    CHECK (std::abs (a.playAt (lynx::ppq * 0.25, 0.0) - 1.0f) < 0.01f);
    // frozen: no movement inside the clip
    a.lfo.flags = 1 | 8;
    CHECK (std::abs (a.playAt (lynx::ppq * 0.25, 0.0) - 0.5f) < 0.01f);
    // off: only the points (and nothing computed)
    a.lfo.on = false;
    CHECK (a.playAt (lynx::ppq * 0.25, 0.0) == 0.5f);
}
