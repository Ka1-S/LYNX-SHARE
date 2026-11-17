// Round 190jz: GitHub issues #192 (the Browser docked as in FL: a maximized window fills what the docked Browser leaves),
// #194 (audio clip fades from the clip's top corners, FL's handles and tension) and #195 (Home goes to the start; REAPER's
// W as a LYNX extra).

#include "core/KeyMap.h"
#include "core/LynxExtras.h"
#include "core/TransportEdit.h"

// --- #195 -------------------------------------------------------------------------------------------------------------

TEST (issues190jz_home_moves_the_playback_marker_to_the_start)
{
    using namespace lynx::keys;
    KeyMap map;
    // FL's Home: "Move playback marker to start" while the Playlist or the Piano roll has the focus (FL 2026: nothing with
    // the Mixer or the Channel rack focused), with the typing keyboard on or off
    for (const auto piano : { false, true })
    {
        CHECK (map.find ({ home, none }, Context::playlist, piano) == "playlist.toStart");
        CHECK (map.find ({ home, none }, Context::pianoRoll, piano) == "pianoRoll.toStart");
        for (const auto context : { Context::global, Context::channelRack, Context::mixer })
            CHECK (map.find ({ home, none }, context, piano).empty());
    }
    // the action itself has no key (the shortcut editor can give it one; the W extra runs it)
    CHECK (map.chordsFor ("transport.toStart").empty());
    // FL's Ctrl+Alt+Home stays the Playlist's "Allow resizing from left"
    CHECK (map.find ({ home, ctrl | alt }, Context::playlist, false) == "playlist.leftResizing");
    CHECK (map.conflicts().empty());
    // plain W is free in the key map (the extra takes it in the main window, not the key map)
    for (const auto context : { Context::global, Context::playlist, Context::channelRack, Context::pianoRoll, Context::mixer, Context::browser })
        CHECK (map.find ({ 'W', none }, context, false).empty());
}

TEST (issues190jz_w_goes_to_the_start_only_as_an_extra)
{
    using namespace lynx;
    CHECK (! transportedit::wGoesToStart()); // off in a fresh profile
    // the switch, no modifier, the typing keyboard not taking W, no text field
    CHECK (! transportedit::wKeyGoesToStart (false, 'W', false, false, false));
    CHECK (transportedit::wKeyGoesToStart (true, 'W', false, false, false));
    CHECK (transportedit::wKeyGoesToStart (true, 'w', false, false, false));
    CHECK (! transportedit::wKeyGoesToStart (true, 'W', true, false, false));  // Alt+W is the Piano roll's Claw machine
    CHECK (! transportedit::wKeyGoesToStart (true, 'W', false, true, false));  // a note of the typing keyboard
    CHECK (! transportedit::wKeyGoesToStart (true, 'W', false, false, true));  // typing a name
    CHECK (! transportedit::wKeyGoesToStart (true, 'Q', false, false, false));
    // Settings › LYNX extras lists it under the preference the main window saves
    bool listed = false;
    for (const auto& item : extras::items)
        if (item.id == "transport.wToStart")
            listed = item.settingsKey == "ui-options.txt:wToStart" && item.section == "Transport";
    CHECK (listed);
    // the stop target after Home: the mark at 0 (Remember seek time), playing or not
    CHECK (transportedit::stopReturnTarget (false, true, true, 0, 1920) == 0);
    CHECK (transportedit::stopReturnTarget (false, true, false, 0, 1920) == 0);
}

// --- #192 -------------------------------------------------------------------------------------------------------------

#include "core/Workspace.h"

TEST (issues190jz_maximized_panels_fill_what_the_docked_browser_leaves)
{
    using namespace lynx::workspace;
    auto l = Layout::makeDefault (1600, 900);
    const auto bw = l[Panel::browser].bounds.w;
    CHECK (l.browserDock == Dock::left && l.browserDocked() && bw >= minWidth);
    // FL: a maximized Playlist / Piano roll / Channel rack / Mixer beside the docked Browser
    CHECK ((l.freeArea() == Rect { bw, 0, 1600 - bw, 900 }));
    for (const auto p : { Panel::playlist, Panel::channelRack, Panel::pianoRoll, Panel::mixer })
        CHECK ((l.maximisedArea (p) == Rect { bw, 0, 1600 - bw, 900 }));
    // the Browser closed (Alt+F8) or detached: the whole desktop
    l.hide (Panel::browser);
    CHECK (! l.browserDocked() && (l.freeArea() == Rect { 0, 0, 1600, 900 }));
    l.show (Panel::browser);
    l[Panel::browser].detached = true;
    CHECK ((l.maximisedArea (Panel::playlist) == Rect { 0, 0, 1600, 900 }));
    l[Panel::browser].detached = false;
    // docked at the right: full height at the right edge, the free area at the left
    l.dockBrowser (Dock::right);
    CHECK ((l[Panel::browser].bounds == Rect { 1600 - bw, 0, bw, 900 }));
    CHECK ((l.maximisedArea (Panel::pianoRoll) == Rect { 0, 0, 1600 - bw, 900 }));
    // the floating panels moved out of its column (the Playlist that sat beside it at the left is now at the left edge)
    for (const auto p : { Panel::playlist, Panel::channelRack, Panel::pianoRoll, Panel::mixer })
        CHECK (l[p].bounds.right() <= 1600 - bw && l[p].bounds.x >= 0);
    CHECK (l[Panel::playlist].bounds.w <= 1600 - bw);
    // back at the left
    l.dockBrowser (Dock::left);
    CHECK (l[Panel::browser].bounds.x == 0 && l[Panel::browser].bounds.h == 900);
    for (const auto p : { Panel::playlist, Panel::channelRack, Panel::pianoRoll, Panel::mixer })
        CHECK (l[p].bounds.x >= bw);
    // the Browser maximized itself: the whole desktop (it stays behind the others)
    CHECK ((l.maximisedArea (Panel::browser) == Rect { 0, 0, 1600, 900 }));
}

TEST (issues190jz_panels_move_with_the_free_area_as_fl_windows_do)
{
    using namespace lynx::workspace;
    // FL 2026 (shots fl-192-restored-windows-*): Alt+F8 moved the Mixer left by the Browser's width, and the Playlist that
    // touched the right edge kept that edge (wider); Alt+F8 again put them back
    auto l = Layout::makeDefault (1600, 900);
    const auto bw = l[Panel::browser].bounds.w;
    const auto rack = l[Panel::channelRack].bounds, playlist = l[Panel::playlist].bounds;
    auto before = l.freeArea();
    l.hide (Panel::browser);
    l.followFreeArea (before);
    CHECK ((l[Panel::channelRack].bounds == Rect { rack.x - bw, rack.y, rack.w, rack.h }));
    CHECK ((l[Panel::playlist].bounds == Rect { 0, 0, 1600, 900 }));
    before = l.freeArea();
    l.show (Panel::browser);
    l.followFreeArea (before);
    CHECK (l[Panel::channelRack].bounds == rack && l[Panel::playlist].bounds == playlist);
    // a docked Browser made wider pushes the panels at its side along
    before = l.freeArea();
    l[Panel::browser].bounds.w = bw + 40;
    l.placeDockedBrowser();
    l.followFreeArea (before);
    CHECK (l[Panel::channelRack].bounds.x == rack.x + 40 && l[Panel::playlist].bounds.x == bw + 40 && l[Panel::playlist].bounds.right() == 1600);
    // a free Browser (a layout from before round 190jz with the Browser moved off its side) moves nothing
    l.browserDock = Dock::none;
    const auto frozen = l.panels;
    before = l.freeArea();
    l.hide (Panel::browser);
    l.followFreeArea (before);
    for (int i = 0; i < panelCount; ++i)
        CHECK (l.panels[(std::size_t) i].bounds == frozen[(std::size_t) i].bounds);
}

TEST (issues190jz_docked_browser_survives_resize_and_restart)
{
    using namespace lynx::workspace;
    auto l = Layout::makeDefault (1600, 900);
    l[Panel::browser].bounds.w = 300;
    l.dockBrowser (Dock::right);
    l.resize (1200, 700); // a docked Browser keeps its width, at its edge, full height
    CHECK ((l[Panel::browser].bounds == Rect { 900, 0, 300, 700 }));
    // its width never leaves the other panels less than their minimum
    l[Panel::browser].bounds.w = 5000;
    l.placeDockedBrowser();
    CHECK (l[Panel::browser].bounds.w == 1200 - minWidth && l[Panel::browser].bounds.x == minWidth);
    l[Panel::browser].bounds.w = 300;
    l.placeDockedBrowser();
    // the saved layout keeps the side (a "dock" line; 0.1.29 and earlier skip it)
    const auto text = l.serialise();
    CHECK (text.find ("\ndock right\n") != std::string::npos);
    const auto back = Layout::parse (text, 1200, 700);
    CHECK (back.has_value() && back->browserDock == Dock::right && ((*back)[Panel::browser].bounds == Rect { 900, 0, 300, 700 }));
    // a layout saved before: a full-height Browser at the left edge was docked; one moved off its side stays free
    std::string old = text.substr (0, text.find ("dock "));
    const auto fromOld = Layout::parse (old, 1200, 700);
    CHECK (fromOld.has_value() && fromOld->browserDock == Dock::right); // (at the right edge, full height)
    auto moved = Layout::makeDefault (1600, 900);
    moved[Panel::browser].bounds = { 300, 120, 260, 500 };
    auto movedText = moved.serialise();
    movedText = movedText.substr (0, movedText.find ("dock "));
    const auto freeLayout = Layout::parse (movedText, 1600, 900);
    CHECK (freeLayout.has_value() && freeLayout->browserDock == Dock::none && ! freeLayout->browserDocked());
    CHECK (freeLayout.has_value() && ((*freeLayout)[Panel::browser].bounds == Rect { 300, 120, 260, 500 }));
    CHECK (freeLayout.has_value() && (freeLayout->maximisedArea (Panel::playlist) == Rect { 0, 0, 1600, 900 })); // (as before)
    const auto fresh = Layout::parse (Layout::makeDefault (1600, 900).serialise().substr (0, Layout::makeDefault (1600, 900).serialise().find ("dock ")), 1600, 900);
    CHECK (fresh.has_value() && fresh->browserDock == Dock::left);
    // an unknown value reads as the default side
    CHECK (Layout::parse (old + "dock sideways\n", 1200, 700)->browserDock == Dock::left);
}

// --- #194 -------------------------------------------------------------------------------------------------------------

#include "core/ClipOps.h"

TEST (issues190jz_fade_tension_handle_maps_like_fl)
{
    using namespace lynx;
    // FL 2026's saved values for the handle typed in as 0.25, 0.5 and 1 (round 190jz, p189l-t*.flp)
    CHECK (std::abs (clipops::fadeTensionFromHandle (0.25) - 7.0899f) < 0.002f);
    CHECK (std::abs (clipops::fadeTensionFromHandle (0.5) - 516.8467f) < 0.05f);
    CHECK (std::abs (clipops::fadeTensionFromHandle (1.0) / 2672339.25f - 1.0f) < 1.0e-4f);
    CHECK (clipops::fadeTensionFromHandle (0.0) == 0.0f && clipops::fadeTensionFromHandle (-0.5) == -clipops::fadeTensionFromHandle (0.5));
    CHECK (clipops::fadeTensionFromHandle (3.0) == clipops::fadeTensionFromHandle (1.0)); // (-1..1)
    for (const auto v : { -1.0, -0.527, -0.25, 0.0, 0.01, 0.15, 0.5, 0.9, 1.0 })
        CHECK (std::abs (clipops::fadeTensionHandle (clipops::fadeTensionFromHandle (v)) - v) < 1.0e-5);
    // Thief's fade-out tension (-812.933) shows as FL's -53 %
    CHECK (std::lround (clipops::fadeTensionHandle (-812.933f) * 100.0) == -53);
    // the handle sits at the curve's middle: 0 = FL's S curve at one half; up (positive) raises it on both sides
    CHECK (std::abs (clipops::fadeLevel (0.5f, true, false, 0.0f) - 0.5f) < 1.0e-6f);
    CHECK (std::abs (clipops::fadeLevel (0.5f, false, false, 0.0f) - 0.5f) < 1.0e-6f);
    for (const auto v : { 0.15, 0.5 })
    {
        const auto raw = clipops::fadeTensionFromHandle (v);
        CHECK (clipops::fadeLevel (0.5f, true, false, raw) > 0.55f && clipops::fadeLevel (0.5f, false, false, raw) > 0.55f);
        CHECK (clipops::fadeLevel (0.5f, true, false, -raw) < 0.45f && clipops::fadeLevel (0.5f, false, false, -raw) < 0.45f);
    }
    // FL's screen at 15 % and 50 %: the circle 58 % and 92 % of the way up the clip (measured 0.582, 0.925)
    CHECK (std::abs (clipops::fadeLevel (0.5f, true, false, clipops::fadeTensionFromHandle (0.15)) - 0.58f) < 0.04f);
    CHECK (std::abs (clipops::fadeLevel (0.5f, true, false, clipops::fadeTensionFromHandle (0.5)) - 0.93f) < 0.03f);
    // ends: silent at the outer edge, full at the inner one; straight ("Single curve") without tension is a line
    for (const auto linear : { false, true })
        for (const auto raw : { 0.0f, 516.8467f, -7.09f })
        {
            CHECK (std::abs (clipops::fadeLevel (0.0f, true, linear, raw)) < 1.0e-5f && std::abs (clipops::fadeLevel (1.0f, true, linear, raw) - 1.0f) < 1.0e-5f);
            CHECK (std::abs (clipops::fadeLevel (1.0f, false, linear, raw)) < 1.0e-5f && std::abs (clipops::fadeLevel (0.0f, false, linear, raw) - 1.0f) < 1.0e-5f);
        }
    CHECK (std::abs (clipops::fadeLevel (0.3f, true, true, 0.0f) - 0.3f) < 1.0e-6f);
}

namespace
{
// an audio clip of a steady 1.0 sample with both fades, rendered by the rack in blocks of `block` from tick 0 for `seconds`
std::vector<float> renderFadedClip (int block, float inTension, float outTension, bool linearOut, double seconds = 2.2)
{
    lynx::Song s;
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    s.mixer = lynx::Song::defaultMixer (1);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "steady.wav";
    c.volume = 1.0f;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 0;
    clip.length = lynx::ppq * 4;  // 2 s at 120 BPM
    clip.fadeIn = lynx::ppq;      // 0.5 s
    clip.fadeOut = lynx::ppq * 2; // 1 s
    clip.fadeInTension = inTension;
    clip.fadeOutTension = outTension;
    clip.linearFades = linearOut ? 2 : 0;
    lynx::clipops::setManualFade (clip, 0, true);
    lynx::clipops::setManualFade (clip, 1, true);
    s.playlist = { clip };
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (48000 * 4, 1.0f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> all, l ((std::size_t) block), rr ((std::size_t) block);
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
    const auto total = (std::size_t) (seconds * 48000.0);
    double tick = 0.0;
    while (all.size() < total)
    {
        r.process (state.get(), true, tick, block, l.data(), rr.data());
        all.insert (all.end(), l.begin(), l.end());
        tick += block * tps;
    }
    all.resize (total);
    return all;
}
} // namespace

TEST (issues190jz_clip_fades_render_as_drawn_and_the_same_in_any_block_size)
{
    using namespace lynx;
    const auto in = clipops::fadeTensionFromHandle (0.5), out = clipops::fadeTensionFromHandle (-0.3);
    // the export renders in blocks of 512, playback in the device's blocks (441 here): the same samples
    const auto exported = renderFadedClip (512, in, out, false);
    const auto played = renderFadedClip (441, in, out, false);
    float worst = 0.0f;
    for (std::size_t i = 0; i < exported.size(); ++i)
        worst = std::max (worst, std::abs (exported[i] - played[i]));
    CHECK (worst < 1.0e-6f);
    // the level along each fade is the curve the Playlist draws (clipops::fadeLevel), against the clip's full level
    const auto full = exported[(std::size_t) (0.75 * 48000.0)];
    CHECK (full > 0.1f);
    for (const auto x : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
    {
        const auto atIn = exported[(std::size_t) (x * 0.5 * 48000.0)] / full;
        CHECK (std::abs (atIn - clipops::fadeLevel (x, true, false, in)) < 0.01f);
        const auto atOut = exported[(std::size_t) ((1.0 + x * 1.0) * 48000.0)] / full;
        CHECK (std::abs (atOut - clipops::fadeLevel (x, false, false, out)) < 0.01f);
    }
    // "Single curve" on the fade-out: a straight line bent only by its tension
    const auto straight = renderFadedClip (512, 0.0f, 0.0f, true);
    CHECK (std::abs (straight[(std::size_t) (1.5 * 48000.0)] / straight[(std::size_t) (0.75 * 48000.0)] - 0.5f) < 0.01f);
}
