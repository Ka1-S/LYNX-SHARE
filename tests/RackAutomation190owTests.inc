// Round 190ow (PAT-7 rest, details DE-123): what FL's automation clip channels do in the Channel rack, now that LYNX's rows
// do it too (core/RackAutomation.h). Steps / notes on a row (Pattern::autoNotes) start the clip from its beginning and
// play it while the note is held, then the value holds (FL's manual, "Triggering"; FL 2026 /R renders of round 190ow's
// probes); a step of no length plays the clip once. The row's knobs are the clip's MIN / MAX (FL: "a scaling factor that
// re-ranges the output", the points stay). The mute light silences the clip. The .flp export writes the channels in the
// rack's order, and the import puts every row where FL's channel order has it. The .lynx round trip and the copies for
// 0.1.30 / 0.1.28 are checked in the app's --selftest ("rackauto190ow": ProjectFile).

#include "core/RackAutomation.h"

#define RA190OW_REQUIRE(c) do { CHECK (c); if (! (c)) return; } while (false)

namespace ra190ow
{
// A constant through the Master's input and the Master's fader automated by automation 0 (Auto125's rig): the renderer's
// automation slot 0 shows what the automation plays at each block.
struct Rig
{
    lynx::Song song;
    explicit Rig (lynx::PlayMode mode = lynx::PlayMode::song)
    {
        song = lynx::Song::makeDefault();
        song.mode = mode;
        song.channels.clear();
        song.channels.emplace_back().name = "Tone";
        song.playlist.clear();
        song.automations.clear();
        song.mixer = lynx::Song::defaultMixer (2);
        lynx::Automation a;
        a.name = "Duck";
        a.target = { lynx::AutomationTargetKind::mixerVolume, 0 };
        a.points = { { 0, 0.0f, 0.0f }, { 2 * lynx::ppq, 1.0f, 0.0f } }; // 0 -> 1 over two beats
        song.automations.push_back (a);
        song.patterns.resize (1);
        song.patterns[0].lengthTicks = 16 * lynx::ppq;
        lynx::Clip pc;
        pc.kind = lynx::ClipKind::pattern;
        pc.pattern = 0;
        pc.length = 16 * lynx::ppq;
        song.playlist.push_back (pc);
        // (an empty pattern's clip to beat 64 on another track keeps the song long: the position never wraps)
        song.patterns.emplace_back().lengthTicks = 4 * lynx::ppq;
        lynx::Clip pad;
        pad.kind = lynx::ClipKind::pattern;
        pad.pattern = 1;
        pad.track = 3;
        pad.length = 64 * lynx::ppq;
        song.playlist.push_back (pad);
    }
    void trigger (double beat, double beats)
    {
        lynx::Note n;
        n.start = (std::int64_t) std::llround (beat * lynx::ppq);
        n.length = (std::int64_t) std::llround (beats * lynx::ppq);
        n.channel = 0;
        song.patterns[0].autoNotes.push_back (n);
        song.patterns[0].sortNotes();
    }
    // the automation's value at beat `beat` (the renderer runs blocks from 0 up to it); NaN = not automated
    float at (double beat) const
    {
        auto state = lynx::RackState::build (song, nullptr, 48000.0);
        lynx::RackRenderer renderer;
        renderer.prepare (48000.0, 64);
        std::vector<float> l (64), r (64);
        // (64 samples at 120 BPM and 48 kHz: 2.56 ticks; the position is set straight, as the engine does each block)
        renderer.process (state.get(), true, beat * lynx::ppq, 64, l.data(), r.data());
        return renderer.automationValueForCheck (0);
    }
};

bool near (float a, float b) { return std::abs (a - b) < 2.0e-3f; }
} // namespace ra190ow

TEST (rackauto190ow_steps_on_the_row)
{
    lynx::Pattern p;
    CHECK (! p.hasAutoStep (0, 3));
    p.toggleAutoStep (0, 3);
    CHECK (p.hasAutoStep (0, 3) && ! p.hasAutoStep (1, 3) && ! p.hasAutoStep (0, 2));
    CHECK (p.autoNotes.size() == 1 && p.autoNotes[0].start == 3 * lynx::ticksPerStep && p.notes.empty()); // (never a channel's)
    p.toggleAutoStep (0, 3);
    CHECK (p.autoNotes.empty());
    // a muted note in the step counts as off: setting the step replaces it
    lynx::Note muted;
    muted.start = 2 * lynx::ticksPerStep;
    muted.channel = 0;
    muted.muted = true;
    p.autoNotes.push_back (muted);
    CHECK (! p.hasAutoStep (0, 2));
    p.toggleAutoStep (0, 2);
    CHECK (p.hasAutoStep (0, 2) && p.autoNotes.size() == 1 && ! p.autoNotes[0].muted);
    // FL's pattern length counts the automation rows' notes
    lynx::Pattern q;
    lynx::Note late;
    late.start = 5 * lynx::ppq * 4; // bar 6
    late.length = lynx::ticksPerStep;
    q.autoNotes.push_back (late);
    CHECK (q.effectiveLength ({ 4, 4 }) == 6 * 4 * lynx::ppq);
}

TEST (rackauto190ow_removing_an_automation_takes_its_steps)
{
    lynx::Song s;
    s.automations.resize (3);
    for (auto& a : s.automations)
        a.points = { { 0, 0.5f, 0.0f } };
    s.patterns.resize (1);
    for (int a = 0; a < 3; ++a)
        s.patterns[0].toggleAutoStep (a, a);
    s.removeAutomation (1);
    CHECK (s.patterns[0].autoNotes.size() == 2);
    CHECK (s.patterns[0].hasAutoStep (0, 0) && s.patterns[0].hasAutoStep (1, 2)); // (automation 2 is 1 now, its step stays)
}

TEST (rackauto190ow_a_note_starts_the_clip_and_plays_it_while_held)
{
    ra190ow::Rig rig;
    CHECK (std::isnan (rig.at (0.5))); // nothing triggered, no clip: the fader's own value
    rig.trigger (4.0, 1.0);            // a one-beat note at beat 4 (the clip is two beats long)
    CHECK (std::isnan (rig.at (3.5)));                 // before it: the target's own value
    CHECK (ra190ow::near (rig.at (4.0), 0.0f));        // the clip from its start
    CHECK (ra190ow::near (rig.at (4.5), 0.25f));
    CHECK (ra190ow::near (rig.at (4.9), 0.45f));
    CHECK (ra190ow::near (rig.at (6.0), 0.5f));        // the note ended at beat 5: the value it reached holds
    CHECK (ra190ow::near (rig.at (12.0), 0.5f));
}

TEST (rackauto190ow_a_step_plays_the_clip_once_and_a_new_one_restarts_it)
{
    ra190ow::Rig rig;
    rig.trigger (4.0, 0.0); // FL's step: no length of its own, the clip plays to its end
    CHECK (ra190ow::near (rig.at (5.0), 0.5f));
    CHECK (ra190ow::near (rig.at (5.9), 0.95f));
    CHECK (ra190ow::near (rig.at (7.0), 1.0f)); // past its end: the last value
    rig.trigger (5.0, 0.0);                     // a second step a beat later starts it again
    CHECK (ra190ow::near (rig.at (5.5), 0.25f));
    CHECK (ra190ow::near (rig.at (4.5), 0.25f)); // (the first one before it)
}

TEST (rackauto190ow_a_note_longer_than_the_clip_holds_its_last_value)
{
    ra190ow::Rig rig;
    rig.trigger (2.0, 6.0);
    CHECK (ra190ow::near (rig.at (3.0), 0.5f));
    CHECK (ra190ow::near (rig.at (6.0), 1.0f));
    CHECK (ra190ow::near (rig.at (9.0), 1.0f));
}

TEST (rackauto190ow_the_pattern_clip_decides_where_its_steps_play)
{
    ra190ow::Rig rig;
    rig.trigger (4.0, 0.0);
    rig.song.playlist[0].start = 8 * lynx::ppq; // the pattern clip from beat 8, its first four beats cut (offset)
    rig.song.playlist[0].offset = 2 * lynx::ppq;
    rig.song.playlist[0].length = 4 * lynx::ppq;
    CHECK (std::isnan (rig.at (9.5)));
    CHECK (ra190ow::near (rig.at (10.5), 0.25f)); // pattern beat 4 plays at song beat 8 - 2 + 4 = 10
    // a step the clip's end cuts plays on to its own end (FL 2026: probes p13_cut, p15_cutnote)
    rig.song.playlist[0].length = 3 * lynx::ppq; // ends at beat 11
    CHECK (ra190ow::near (rig.at (11.5), 0.75f));
    CHECK (ra190ow::near (rig.at (13.0), 1.0f));
    CHECK (std::isnan (rig.at (8.5)));            // a step before the clip's offset never plays
    rig.song.playlist[0].muted = true;            // a muted pattern clip starts nothing
    CHECK (std::isnan (rig.at (10.5)));
}

TEST (rackauto190ow_a_trigger_and_a_playlist_clip_the_later_start_wins)
{
    ra190ow::Rig rig;
    lynx::Clip ac;
    ac.kind = lynx::ClipKind::automation;
    ac.automation = 0;
    ac.track = 1;
    ac.start = 2 * lynx::ppq;
    ac.length = 2 * lynx::ppq;
    rig.song.playlist.push_back (ac);
    CHECK (ra190ow::near (rig.at (3.0), 0.5f)); // the Playlist clip
    rig.trigger (3.0, 0.0);
    CHECK (ra190ow::near (rig.at (3.5), 0.25f)); // the step started later: it plays
    CHECK (ra190ow::near (rig.at (2.5), 0.25f)); // (before it, the clip)
}

TEST (rackauto190ow_a_muted_clip_drives_nothing)
{
    ra190ow::Rig rig;
    rig.trigger (1.0, 0.0);
    lynx::Clip ac;
    ac.kind = lynx::ClipKind::automation;
    ac.automation = 0;
    ac.track = 1;
    ac.start = 4 * lynx::ppq;
    ac.length = 2 * lynx::ppq;
    rig.song.playlist.push_back (ac);
    CHECK (! std::isnan (rig.at (1.5)) && ! std::isnan (rig.at (5.0)));
    rig.song.automations[0].muted = true;
    CHECK (std::isnan (rig.at (1.5)));
    CHECK (std::isnan (rig.at (5.0)));
}

TEST (rackauto190ow_pattern_mode_plays_the_current_patterns_steps)
{
    ra190ow::Rig rig (lynx::PlayMode::pattern);
    rig.song.patterns[0].lengthTicks = 8 * lynx::ppq;
    rig.song.currentPattern = 0;
    rig.trigger (2.0, 0.0);
    CHECK (std::isnan (rig.at (1.0)));          // the first pass: the target's own value before the step
    CHECK (ra190ow::near (rig.at (3.0), 0.5f));
    CHECK (ra190ow::near (rig.at (6.0), 1.0f));
    CHECK (ra190ow::near (rig.at (9.0), 1.0f));  // the second pass, before the step: the pass before still holds
    CHECK (ra190ow::near (rig.at (11.0), 0.5f)); // started again at 8 + 2
    lynx::Pattern other;
    other.lengthTicks = 8 * lynx::ppq;
    rig.song.patterns.push_back (other);
    rig.song.currentPattern = (int) rig.song.patterns.size() - 1; // another pattern: its steps only
    CHECK (std::isnan (rig.at (3.0)));
}

TEST (rackauto190ow_the_knobs_rerange_the_clip_and_leave_it_alone_untouched)
{
    lynx::Automation a;
    a.points = { { 0, 0.0f, 0.0f }, { lynx::ppq, 1.0f, 0.0f } };
    CHECK (! a.reranged());
    const auto plain = a.playAt (0.3 * lynx::ppq, 0.0);
    a.rangeMin = 0.25f;
    a.rangeMax = 0.75f;
    CHECK (a.reranged());
    CHECK (ra190ow::near (a.playAt (0.0, 0.0), 0.25f) && ra190ow::near (a.playAt (lynx::ppq, 0.0), 0.75f));
    CHECK (ra190ow::near (a.playAt (0.5 * lynx::ppq, 0.0), 0.5f));
    CHECK (a.valueAt (0.5 * lynx::ppq) == 0.5f); // the points stay as drawn (FL: "the Playlist display will remain unchanged")
    a.rangeMin = 0.0f;
    a.rangeMax = 1.0f;
    CHECK (a.playAt (0.3 * lynx::ppq, 0.0) == plain); // back: the same bits
    // an FL clip's own range (the points written in it): untouched = nothing re-ranged; MIN up to its MAX = flat
    a.pointsMin = a.rangeMin = 0.2f;
    a.pointsMax = a.rangeMax = 0.6f;
    a.points = { { 0, 0.2f, 0.0f }, { lynx::ppq, 0.6f, 0.0f } };
    CHECK (! a.reranged() && a.playAt (0.5 * lynx::ppq, 0.0) == a.valueAt (0.5 * lynx::ppq));
    a.rangeMax = 1.0f; // MAX 0.6 -> 1.0: the top of the curve goes up, its bottom stays
    CHECK (ra190ow::near (a.playAt (0.0, 0.0), 0.2f) && ra190ow::near (a.playAt (lynx::ppq, 0.0), 1.0f));
}

TEST (rackauto190ow_flp_import_reads_the_rows_steps_mute_and_range)
{
    using namespace lynx;
    flp::Project p;
    p.header.ppq = 96;
    const auto add = [&p] (int index, flp::ChannelType type, const char* name) -> flp::Channel& {
        flp::Channel c;
        c.index = index;
        c.type = type;
        c.name = name;
        if (type == flp::ChannelType::automationClip)
        {
            c.automation.push_back ({ 0.0, 0.0, 0.0f, 0 });
            c.automation.push_back ({ 2.0, 1.0, 0.0f, 0 });
            c.hasLevels = true;
            c.pan = 1600;   // MIN 12.5 %
            c.volume = 9600; // MAX 75 %
        }
        p.channels.push_back (c);
        return p.channels.back();
    };
    add (0, flp::ChannelType::sampler, "Kick");
    add (1, flp::ChannelType::automationClip, "Duck").enabled = false;
    add (2, flp::ChannelType::sampler, "Bass");
    flp::AutomationLink link;
    link.channel = 1;
    link.target.raw = (2u << 16) | 0u; // Bass's volume
    p.automationLinks.push_back (link);
    flp::Pattern pat;
    pat.index = 1;
    flp::Note kick;
    kick.channel = 0;
    pat.notes.push_back (kick);
    flp::Note step;
    step.channel = 1; // a step on the automation clip channel
    step.position = 96;
    step.length = 0;
    pat.notes.push_back (step);
    flp::Note note = step;
    note.position = 192;
    note.length = 48;
    note.key = 72;
    pat.notes.push_back (note);
    p.patterns.push_back (pat);

    auto song = importFlp (flp::parseProject (flp::writeProject (p)), fx171e::importOptions());
    RA190OW_REQUIRE (song.automations.size() == 1 && song.channels.size() == 2 && ! song.patterns.empty());
    const auto& a = song.automations[0];
    CHECK (a.muted);
    CHECK (a.rackSlot == 1); // between Kick and Bass, as FL's rack shows it
    CHECK (a.rangeMin == 0.125f && a.rangeMax == 0.75f && ! a.reranged());
    CHECK (ra190ow::near (a.points[1].value, 0.75f)); // (the played values, as before this round)
    const auto& pt = song.patterns[0];
    CHECK (pt.notes.size() == 1 && pt.notes[0].channel == 0);
    RA190OW_REQUIRE (pt.autoNotes.size() == 2);
    CHECK (pt.autoNotes[0].channel == 0 && pt.autoNotes[0].start == ppq && pt.autoNotes[0].length == 0);
    CHECK (pt.autoNotes[1].start == 2 * ppq && pt.autoNotes[1].length == ppq / 2 && pt.autoNotes[1].key == 72);

    // and back to FL: the automation clip channel between the two (FL's index 1), its steps, its mute, its levels
    flpexport::Options options;
    options.ppq = 96;
    flpexport::Report report;
    const auto again = flp::parseProject (flpexport::exportFlp (song, options, report));
    RA190OW_REQUIRE (again.channels.size() == 3);
    CHECK (again.channels[0].name == "Kick" && again.channels[1].type == flp::ChannelType::automationClip && again.channels[2].name == "Bass");
    CHECK (again.channels[1].index == 1 && ! again.channels[1].enabled && again.channels[1].pan == 1600 && again.channels[1].volume == 9600);
    CHECK (ra190ow::near ((float) again.channels[1].automation[1].value, 1.0f)); // (FL's 0..1 of its range)
    RA190OW_REQUIRE (! again.automationLinks.empty());
    CHECK (again.automationLinks[0].channel == 1 && again.automationLinks[0].target.channel() == 2); // Bass is FL's 2 now
    int onAuto = 0, onKick = 0;
    for (const auto& n : again.patterns[0].notes)
        (n.channel == 1 ? onAuto : n.channel == 0 ? onKick : onAuto) += 1;
    CHECK (onAuto == 2 && onKick == 1);
    const auto back = importFlp (again, fx171e::importOptions());
    RA190OW_REQUIRE (back.automations.size() == 1 && ! back.patterns.empty());
    CHECK (back.automations[0].rackSlot == 1 && back.automations[0].muted && back.patterns[0].autoNotes == pt.autoNotes);
    CHECK (back.automations[0].rangeMin == a.rangeMin && back.automations[0].rangeMax == a.rangeMax);
    CHECK (ra190ow::near (back.automations[0].points[1].value, a.points[1].value));
}

TEST (rackauto190ow_flp_export_keeps_numbers_when_the_rows_follow_the_channels)
{
    using namespace lynx;
    Song song;
    for (int c = 0; c < 3; ++c)
        song.channels.emplace_back().name = "C" + std::to_string (c);
    Automation a;
    a.name = "A";
    a.target = { AutomationTargetKind::channelVolume, 2 };
    a.points = { { 0, 0.2f, 0.0f }, { ppq, 0.8f, 0.0f } };
    song.automations.push_back (a);
    song.patterns.resize (1);
    Note n;
    n.channel = 2;
    song.patterns[0].notes.push_back (n);
    flpexport::Options options;
    flpexport::Report report;
    const auto out = flp::parseProject (flpexport::exportFlp (song, options, report));
    RA190OW_REQUIRE (out.channels.size() == 4);
    CHECK (out.channels[3].type == flp::ChannelType::automationClip && out.channels[3].index == 3);
    CHECK (out.channels[3].pan == 0 && out.channels[3].volume == 12800 && out.channels[3].enabled); // (a LYNX clip: as before)
    CHECK (out.automationLinks.size() == 1 && out.automationLinks[0].channel == 3 && out.automationLinks[0].target.channel() == 2);
    // the row placed above C1: FL's channel 1 is the clip, C1 and C2 move down one, and everything that names them
    song.automations[0].rackSlot = 1;
    song.patterns[0].toggleAutoStep (0, 4);
    const auto placed = flp::parseProject (flpexport::exportFlp (song, options, report));
    RA190OW_REQUIRE (placed.channels.size() == 4);
    CHECK (placed.channels[1].type == flp::ChannelType::automationClip && placed.channels[3].name == "C2");
    CHECK (placed.automationLinks.size() == 1 && placed.automationLinks[0].channel == 1 && placed.automationLinks[0].target.channel() == 3);
    bool c2Note = false, autoStep = false;
    for (const auto& f : placed.patterns[0].notes)
    {
        c2Note = c2Note || (f.channel == 3 && f.position == 0);
        autoStep = autoStep || (f.channel == 1 && f.position > 0);
    }
    CHECK (c2Note && autoStep);
    const auto back = importFlp (placed, fx171e::importOptions());
    CHECK (back.automations.size() == 1 && back.automations[0].rackSlot == 1 && back.automations[0].target.index == 2);
    CHECK (back.patterns.size() == 1 && back.patterns[0].hasAutoStep (0, 4) && back.patterns[0].notes.size() == 1
           && back.patterns[0].notes[0].channel == 2);
}

TEST (rackauto190ow_editor_playhead_follows_a_triggered_clip)
{
    ra190ow::Rig rig;
    rig.trigger (4.0, 1.0);
    CHECK (lynx::rackauto::triggeredAt (rig.song, 0, 3.0 * lynx::ppq) < 0.0);
    CHECK (std::abs (lynx::rackauto::triggeredAt (rig.song, 0, 4.5 * lynx::ppq) - 0.5 * lynx::ppq) < 1.0);
    CHECK (lynx::rackauto::triggeredAt (rig.song, 0, 5.5 * lynx::ppq) < 0.0); // the note ended: no playhead
    const auto v = lynx::rackauto::triggeredValue (rig.song, 0, 5.5 * lynx::ppq);
    CHECK (ra190ow::near (v.first, 0.5f) && v.second == 4.0 * lynx::ppq); // (the knobs on screen hold the value)
    rig.song.automations[0].muted = true;
    CHECK (std::isnan (lynx::rackauto::triggeredValue (rig.song, 0, 4.5 * lynx::ppq).first));
}
