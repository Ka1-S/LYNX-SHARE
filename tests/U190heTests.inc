// Round 190he (issue #152, Nuelka's list for 0.1.27): docked windows that a click in a plugin view made by another
// thread or process must raise (core/DockStack.h), and the size of LYNX's plugin windows (FL's Scaling, core/EditorScale.h).
#include "core/DockStack.h"
#include "core/EditorScale.h"

TEST (u190he_a_click_from_another_thread_or_process_raises_its_docked_window)
{
    using lynx::dockstack::clickNeedsParentNotice;
    CHECK (clickNeedsParentNotice (4242, 17));   // a plugin view of another thread / a bridged plugin's process
    CHECK (! clickNeedsParentNotice (17, 17));   // LYNX's UI thread: the message hook raises it (once)
    CHECK (! clickNeedsParentNotice (0, 17));    // the window is gone
}

TEST (u190he_scaling_offers_fl_sizes_and_lynx_default_stays_125)
{
    using namespace lynx::editorscale;
    // FL's PluginScalingMenu: 100 % … 400 %; LYNX opens at FL's size × 1.25
    CHECK (menuPercents.front() == 100 && menuPercents.back() == 400);
    CHECK (percentOf (0) == 125); // FL's "Host" (stored 0: nothing written in the project)
    CHECK (storedFor (125) == 125); // (125 % chosen by hand is not Host: FL ticks one or the other)
    CHECK (storedFor (100) == 100);
    CHECK (percentOf (100) == 100);
    // FL's 100 % is smaller than LYNX's default: the window can be made smaller (the reporter's request)
    CHECK (factorOf (100) < 1.0 && std::abs (factorOf (100) - 0.8) < 1e-9);
    CHECK (std::abs (factorOf (0) - 1.0) < 1e-9);
    CHECK (std::abs (factorOf (250) - 2.0) < 1e-9);
    // out of range values (a hand-edited project) are held in range
    CHECK (percentOf (9000) == maxPercent && percentOf (5) == minPercent);
    CHECK (storedFor (-3) == minPercent);
}

TEST (u190he_dragging_the_edge_gives_the_matching_percentage)
{
    using namespace lynx::editorscale;
    CHECK (percentForWidth (800, 800) == 125);  // unchanged
    CHECK (percentForWidth (640, 800) == 100);  // FL's own size
    CHECK (percentForWidth (1600, 800) == 250);
    CHECK (percentForWidth (10, 800) == minPercent);
    CHECK (percentForWidth (100000, 800) == maxPercent);
    CHECK (percentForWidth (500, 0) == defaultPercent); // (no page yet)
}

// Issue #175 (FL's Playlist): a clip clicked with the Pencil / Brush is what they paint next, as it is
#include "core/PaintTemplate.h"
#include "core/KeyMap.h"

TEST (u190he_pick_a_clicked_clip_paints_as_it_is)
{
    using namespace lynx;
    Clip clicked;
    clicked.kind = ClipKind::audio;
    clicked.channel = 3;
    clicked.track = 5;
    clicked.start = 7680;
    clicked.length = 1234;   // trimmed
    clicked.offset = 480;    // starts later in the sample
    clicked.stretch = 1.5;   // time-stretched
    clicked.pitch = -2.0f;
    clicked.gain = 0.5f;
    clicked.fadeIn = 96;
    clicked.reversed = true;
    clicked.muted = true;    // (belongs to that placed clip)
    clicked.uid = 77;
    clicked.gridSlot = 2;
    clicked.crossfadeIn = 30;
    const auto t = painttemplate::fromClicked (clicked);
    const auto painted = painttemplate::paint (t, 1, 960);
    CHECK (painted.track == 1 && painted.start == 960);
    CHECK (painted.kind == ClipKind::audio && painted.channel == 3);
    CHECK (painted.length == 1234 && painted.offset == 480 && painted.stretch == 1.5 && painted.pitch == -2.0f);
    CHECK (painted.gain == 0.5f && painted.fadeIn == 96 && painted.reversed);
    CHECK (! painted.muted && painted.uid == 0 && painted.gridSlot == -1 && painted.crossfadeIn == 0);
    CHECK (painttemplate::paint (t, 0, -50).start == 0);
}

TEST (u190he_pick_the_template_stands_while_its_source_is_what_the_tools_paint)
{
    using namespace lynx;
    Clip pat;
    pat.kind = ClipKind::pattern;
    pat.pattern = 4;
    CHECK (painttemplate::matches (pat, ClipKind::pattern, -1, 4));
    CHECK (! painttemplate::matches (pat, ClipKind::pattern, -1, 2)); // another pattern chosen: its plain clip
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.channel = 6;
    CHECK (painttemplate::matches (audio, ClipKind::audio, 6, 0));
    CHECK (! painttemplate::matches (audio, ClipKind::audio, 7, 0));
    CHECK (! painttemplate::matches (audio, ClipKind::automation, 6, 0));
    Clip autom;
    autom.kind = ClipKind::automation;
    autom.automation = 2;
    CHECK (painttemplate::matches (autom, ClipKind::automation, 2, 0) && ! painttemplate::matches (autom, ClipKind::automation, 1, 0));
}

TEST (u190he_pick_alt_p_toggles_the_picker_panel_in_the_playlist)
{
    using namespace lynx;
    const auto* action = keys::findAction ("playlist.pickerPanel");
    CHECK (action != nullptr);
    keys::KeyMap map;
    CHECK (map.find ({ 'P', keys::alt }, keys::Context::playlist, false) == "playlist.pickerPanel");
    // (Alt+P stays the Piano roll's chord progression there)
    CHECK (map.find ({ 'P', keys::alt }, keys::Context::pianoRoll, false) == "pianoRoll.chordProgression");
}
