// Round 190jd (the "what LYNX should change" lines of live check 190iw): Piano roll previews, the Playlist's new
// automation clip colours, FL's count-in readout, Gross Beat's FL-colours locator, and the .flp audio clip channel step
// that played only its 1/16 (FL plays the whole clip). The paint is checked by the --snapshot pairs; these are the parts
// without JUCE.

#include "core/CountIn.h"
#include "core/EditorGestures.h"
#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/GrossBeatLocator.h"
#include "core/PlaylistOps.h"
#include "flp/Flp.h"
#include <cmath>

// Live check 190iw Q1 (FL 2026, stopped): a placed note and a grabbed unselected note sound; a resize (either edge) and a
// grab of an already-selected note don't. (While playing, previewsEditedNote keeps everything silent, round 190ib.)
TEST (livefix190jd_piano_roll_grab_preview_as_fl)
{
    using lynx::gesture::previewsGrabbedNote;
    CHECK (previewsGrabbedNote (false, false));  // grab an unselected note: sounds
    CHECK (! previewsGrabbedNote (false, true)); // grab a selected note: silent
    CHECK (! previewsGrabbedNote (true, false)); // resize: silent
    CHECK (! previewsGrabbedNote (true, true));
}

// Live check 190iw Q4: FL colours a NEW automation clip by its parameter (channel volume #8C5F5F, pan #7F8C5E); the rest
// keep FL's default grey (0 = #485156)
TEST (livefix190jd_new_automation_clip_colour_by_parameter)
{
    using namespace lynx;
    AutomationTarget t;
    t.kind = AutomationTargetKind::channelVolume;
    CHECK (autoclipplace::colourFor (t) == 0xff8c5f5fu);
    t.kind = AutomationTargetKind::channelPan;
    CHECK (autoclipplace::colourFor (t) == 0xff7f8c5eu);
    t.kind = AutomationTargetKind::mixerVolume;
    CHECK (autoclipplace::colourFor (t) == 0u);
    t.kind = AutomationTargetKind::none;
    CHECK (autoclipplace::colourFor (t) == 0u);
}

// Live check 190iw Q7: "REC IN 4" … "REC IN 1", one per beat of the count-in
TEST (livefix190jd_count_in_beats_left)
{
    using lynx::countin::beatsLeft;
    constexpr int q = 96;
    CHECK (beatsLeft (0.0, 4, q) == 0);
    CHECK (beatsLeft (-5.0, 4, q) == 0);
    CHECK (beatsLeft (4.0 * q, 4, q) == 4);  // the start of a 1-bar count-in in 4/4
    CHECK (beatsLeft (3.0 * q + 1, 4, q) == 4);
    CHECK (beatsLeft (3.0 * q, 4, q) == 3);
    CHECK (beatsLeft (1.0, 4, q) == 1);
    CHECK (beatsLeft (8.0 * q, 4, q) == 8);  // 2 bars
    CHECK (beatsLeft (6.0 * q / 2, 8, q) == 6); // 6/8: a beat is an eighth note
}

// Live check 190iw Q7: FL's locator colours over a beat, and its trail that fades with it
TEST (livefix190jd_gross_beat_fl_locator_colours)
{
    using namespace lynx::gblocator;
    CHECK (lineColour (0.0) == 0xFFFFFCC1u); // cream on the beat
    CHECK (lineColour (1.0) == 0xFF764A5Au); // dusty red at its end
    CHECK (lineColour (0.35) == 0xFFE1B179u); // orange between
    // the colour only darkens over the beat
    for (int i = 1; i <= 20; ++i)
    {
        const auto a = lineColour ((i - 1) / 20.0), b = lineColour (i / 20.0);
        CHECK (((a >> 16) & 0xFF) >= ((b >> 16) & 0xFF));
    }
    CHECK (std::abs (beatPhase (0.0) - 0.0) < 1e-9);
    CHECK (std::abs (beatPhase (0.3) - 0.2) < 1e-9); // 1.2 beats of 4
    CHECK (stepOf (0.0) == 0 && stepOf (0.999) == stepsPerBeat - 1);
    // 3 px on the beat, nearer pixels stronger, none at the beat's end
    CHECK (trailAlpha (1, 0.0) > trailAlpha (2, 0.0) && trailAlpha (2, 0.0) > trailAlpha (3, 0.0) && trailAlpha (3, 0.0) > 0.0f);
    CHECK (trailAlpha (3, 0.6) == 0.0f);
    CHECK (trailAlpha (1, 0.95) == 0.0f);
    CHECK (trailAlpha (4, 0.0) == 0.0f && trailAlpha (0, 0.0) == 0.0f);
}

// Live check 190iw Q6: one step on an audio clip channel of an .flp plays the whole clip in FL (FL's render: 1 / 2 / 5 s
// clips play 1.010 / 2.010 / 5.010 s); LYNX's import played only the step's 1/16 (0.13 s at 120 BPM). The project is the
// one LYNX's own .flp export writes (the probe the live check rendered in FL), read back by the import.
TEST (livefix190jd_flp_audio_clip_channel_step_plays_the_whole_clip)
{
    using namespace lynx;
    const auto data = r190io::sine (2.0);
    auto song = r190io::stepSong (InstrumentKind::audioClip, "C:\\samples\\sine2s.wav");
    flpexport::Report report;
    const auto bytes = flpexport::exportFlp (song, {}, report);
    CHECK (! bytes.empty());

    FlpImportOptions options;
    options.fileExists = [] (const std::string&) { return true; };
    const auto imported = importFlp (flp::parseProject (bytes), options);
    CHECK (imported.channels.size() >= 1);
    if (imported.channels.empty())
        return;
    CHECK (imported.channels[0].instrument == InstrumentKind::audioClip);
    // (a window shorter than the 1-bar pattern, so neither a pattern loop nor a song loop starts the step again)
    const auto seconds = r190io::soundingSeconds (imported, data, 1.9);
    std::printf ("livefix190jd: the imported audio clip channel's step sounds %.3f s of a 1.9 s window (clip 2 s)\n", seconds);
    CHECK (seconds > 1.85);
}

// Coordinator 190jd (add, never remove): Fruity Limiter's display keeps LYNX's transfer curve as a second view, chosen per
// slot; FL's scrolling graph is the default. The choice is part of the slot (undo, compare, copy) and of nothing audible.
TEST (livefix190jd_limiter_display_view_is_kept_per_slot)
{
    using namespace lynx;
    auto a = makeEffectSlot (EffectKind::fruityLimiter);
    CHECK (a.displayView == 0); // FL's graph by default
    auto b = a;
    b.displayView = 1;
    CHECK (! (a == b)); // a slot's own setting: compared, so an edit of it is an undoable change
    b.displayView = 0;
    CHECK (a == b);
}
