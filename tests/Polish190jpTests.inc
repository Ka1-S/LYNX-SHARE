// --- round 190jp: design190ia slice 26 FW-S5b (Settings › Theme Adjustments and colour dots), "Bold interface text",
// Simple follow-ups (the tour card beside the dock) --------------------------------------------------------------------

#include "core/ThemePalette.h"

TEST (polish190jp_adjustments_none_keep_the_palette_bit_identical)
{
    using namespace lynx::themes;
    CHECK (withAdjustments (defaultPalette(), Spec {}) == defaultPalette());
    CHECK (adjustedPalette (Spec {}) == defaultPalette());
    for (const auto& t : builtInThemes())
        CHECK (adjustedPalette (t) == derivePalette (t.accent, t.background));
    CHECK (Spec {}.isDefaultLook() && ! Spec {}.hasAdjustments() && ! Spec {}.hasDots());
}

TEST (polish190jp_adjustments_move_the_surfaces_and_text_not_the_accent)
{
    using namespace lynx::themes;
    const auto base = defaultPalette();
    auto spec = Spec {};
    spec.brightness = 1.0f;
    const auto bright = adjustedPalette (spec);
    CHECK (relativeLuminance (bright.bg1) > relativeLuminance (base.bg1) && relativeLuminance (bright.bg4) > relativeLuminance (base.bg4));
    CHECK (bright.accent == base.accent && bright.accentHover == base.accentHover && bright.onAccent == base.onAccent);
    CHECK (! spec.isDefaultLook() && spec.hasAdjustments());
    spec.brightness = -1.0f;
    CHECK (relativeLuminance (adjustedPalette (spec).bg2) < relativeLuminance (base.bg2));

    spec = {};
    spec.contrast = 1.0f; // the surfaces spread apart
    const auto spread = adjustedPalette (spec);
    CHECK (contrastRatio (spread.bg0, spread.bg6) > contrastRatio (base.bg0, base.bg6));
    spec.contrast = -1.0f;
    const auto flat = adjustedPalette (spec);
    CHECK (contrastRatio (flat.bg0, flat.bg6) < contrastRatio (base.bg0, base.bg6));

    spec = {};
    spec.text = 1.0f; // the text away from the panels, or towards them
    CHECK (contrastRatio (adjustedPalette (spec).textSecondary, base.bg2) > contrastRatio (base.textSecondary, base.bg2));
    spec.text = -1.0f;
    CHECK (contrastRatio (adjustedPalette (spec).textPrimary, base.bg2) < contrastRatio (base.textPrimary, base.bg2));

    spec = {};
    spec.hue = 180.0f;
    spec.saturation = 1.0f; // the grey surfaces take a clear tint of the turned hue
    const auto tinted = adjustedPalette (spec);
    CHECK (tinted.bg2 != base.bg2 && tinted.accent == base.accent);
    const auto r = (int) ((tinted.bg3 >> 16) & 0xff), b = (int) (tinted.bg3 & 0xff);
    const auto r0 = (int) ((base.bg3 >> 16) & 0xff), b0 = (int) (base.bg3 & 0xff);
    CHECK (b - r != b0 - r0); // LYNX's bluish grey turned (180°: towards the warm side)
}

TEST (polish190jp_colour_dots_set_their_tokens)
{
    using namespace lynx::themes;
    auto spec = Spec {};
    spec.dots[(std::size_t) Dot::highlight] = 0xff30a5ff;
    spec.dots[(std::size_t) Dot::selection] = 0xff00ff00;
    spec.dots[(std::size_t) Dot::text] = 0xffffffff;
    spec.dots[(std::size_t) Dot::mute] = 0xff808080;
    const auto p = adjustedPalette (spec);
    CHECK (p.accentHover == 0xff30a5ff && p.textPrimary == 0xffffffff && p.textDisabled == 0xff808080);
    CHECK (p.selectionFill == 0x2e00ff00); // (the fill keeps its own alpha)
    CHECK (p.accent == defaultAccent);
    CHECK (spec.hasDots() && ! spec.isDefaultLook());
    CHECK (std::string (dotId (Dot::selection)) == "selection" && std::string (dotId (Dot::text)) == "text" && dotCount == 9 && std::string (dotId (Dot::tracks)) == "tracks"); // (round 190od: + Tracks, issue #238)
}

#include "core/TextSize.h"

TEST (polish190jp_bold_text_per_role)
{
    using namespace lynx::textsize;
    // off: nothing; labels: the small and body roles (labels, buttons, menus, lists); all: every role
    for (const auto role : { Role::caption, Role::small, Role::body, Role::title, Role::display })
    {
        CHECK (! boldFor ((int) Bold::off, role));
        CHECK (boldFor ((int) Bold::all, role));
        CHECK (boldFor ((int) Bold::labels, role) == (role == Role::small || role == Role::body));
    }
    CHECK (parseBold ("1") == 1 && parseBold ("2\n") == 2 && parseBold ("") == 0 && parseBold ("x") == 0 && parseBold ("7") == 0);
    // the role of a size: the nearest role, the boundary half-way (as the roles are at 100 % and at 150 %)
    const std::array<float, 5> standard { 11.0f, 12.0f, 13.0f, 15.0f, 20.0f };
    CHECK (roleOf (11.0f, standard) == Role::caption && roleOf (12.0f, standard) == Role::small && roleOf (13.0f, standard) == Role::body);
    CHECK (roleOf (14.0f, standard) == Role::body && roleOf (15.0f, standard) == Role::title && roleOf (18.0f, standard) == Role::display);
    CHECK (roleOf (9.0f, standard) == Role::caption && roleOf (26.0f, standard) == Role::display);
    std::array<float, 5> large {};
    for (int i = 0; i < 5; ++i)
        large[(std::size_t) i] = scaled (standard[(std::size_t) i], 150, (Role) i);
    CHECK (roleOf (large[2], large) == Role::body && roleOf (large[3], large) == Role::title);
}

#include "core/FirstSongTour.h"

TEST (polish190jp_tour_card_stays_off_the_dock_its_ring_is_in)
{
    using namespace lynx::tour;
    // step 4 (190jc follow-up): the Chord pad button in the bottom dock; below it the card covered the keys
    const Box window { 0, 0, 1920, 1080 };
    const Box dock { 0, 760, 1920, 320 };
    const Box chordPad { 300, 790, 90, 26 };
    const auto before = placeCard (chordPad, window, 380, 184, 14);
    CHECK (before.side == Side::below && before.card.intersects (dock)); // (what 190jc did)
    const auto p = placeCard (chordPad, window, 380, 184, 14, {}, dock);
    CHECK (p.side == Side::above && ! p.card.intersects (dock) && p.card.bottom() == dock.y - 14 && p.card.inside (window)); // (above the dock, 14 px off)
    // nothing to avoid: as before
    const Box addTrack { 71, 69, 122, 42 };
    CHECK (placeCard (addTrack, window, 380, 184, 14, {}, {}).side == Side::below);
}
