// Round 190je: design190ia slice 10 (G1: contrast floors, one state language, grid line intensity) and slice 16 (FW-S3:
// menus with FL's landmarks and columns, the FL classic menu style). The rules live in core/ThemePalette.h.

#include "core/ThemePalette.h"

TEST (g1menus190je_built_in_palette_meets_the_contrast_floors)
{
    using namespace lynx::themes;
    const auto& p = defaultPalette();
    CHECK (p.textDisabled == 0xff767b85 && p.outline == 0xff747983);
    for (const auto surface : { p.bg1, p.bg2, p.bg3 })
    {
        CHECK (contrastRatio (p.textPrimary, surface) >= textContrastFloor);
        CHECK (contrastRatio (p.textSecondary, surface) >= textContrastFloor);
        CHECK (contrastRatio (p.textDisabled, surface) >= disabledContrastFloor);
        CHECK (contrastRatio (p.outline, surface) >= partContrastFloor);
    }
    CHECK (contrastRatio (p.outline, p.bg4) >= partContrastFloor); // (combo boxes on the bg4 toolbars)
    // the old disabled grey read 2.6:1 on the menus
    CHECK (contrastRatio (0xff60656f, p.bg3) < disabledContrastFloor);
}

TEST (g1menus190je_derived_palettes_keep_the_floors)
{
    using namespace lynx::themes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> looks;
    for (const auto& t : builtInThemes())
        looks.emplace_back (t.accent, t.background);
    for (const auto bg : { 0xffe8e8ecu, 0xff6a6a6au, 0xff2a2a2au, 0xff3c3326u, 0xffc0c8d0u, 0xff000000u, 0xffffffffu })
        looks.emplace_back (defaultAccent, bg);
    for (const auto& [accent, background] : looks)
    {
        const auto p = derivePalette (accent, background);
        CHECK (contrastRatio (p.textSecondary, p.bg3) >= textContrastFloor - 1.0e-9);
        CHECK (contrastRatio (p.textDisabled, p.bg3) >= disabledContrastFloor - 1.0e-9);
        CHECK (contrastRatio (p.outline, p.bg4) >= partContrastFloor - 1.0e-9);
    }
}

TEST (g1menus190je_with_contrast_moves_only_as_far_as_needed)
{
    using namespace lynx::themes;
    CHECK (withContrast (0xffeceef2, 0xff23262c, 4.5) == 0xffeceef2); // already reads
    const auto lifted = withContrast (0xff60656f, 0xff23262c, 3.0);
    CHECK (contrastRatio (lifted, 0xff23262c) >= 3.0 && contrastRatio (lifted, 0xff23262c) < 3.2);
    CHECK (relativeLuminance (lifted) > relativeLuminance (0xff60656f)); // lighter on a dark background
    const auto darker = withContrast (0xffa0a0a0, 0xffe8e8ec, 4.5);
    CHECK (contrastRatio (darker, 0xffe8e8ec) >= 4.5 && relativeLuminance (darker) < relativeLuminance (0xffa0a0a0));
    // white text on LYNX red is 4.2:1: the red a hair deeper reaches 4.5
    CHECK (contrastRatio (0xffffffff, 0xffe5383b) < 4.5);
    CHECK (contrastRatio (0xffffffff, withContrast (0xffe5383b, 0xffffffff, 4.5)) >= 4.5);
}

TEST (g1menus190je_one_state_rule_for_every_button_kind)
{
    using namespace lynx::themes;
    const auto& p = defaultPalette();
    const auto alpha = [] (std::uint32_t c) { return (int) (c >> 24); };
    // a secondary button: bg4 + the 3:1 outline at rest, bg5 under the mouse, bg0 pressed
    auto l = buttonLook (p, ButtonKind::secondary, false, 0.0, false, true);
    CHECK (l.fill == p.bg4 && l.edge == p.outline && l.content == p.textPrimary);
    CHECK (buttonLook (p, ButtonKind::secondary, false, 1.0, false, true).fill == p.bg5);
    CHECK (buttonLook (p, ButtonKind::secondary, false, 0.0, true, true).fill == p.bg0);
    // on: the accent (text keeps 4.5:1), pressed accentPressed
    l = buttonLook (p, ButtonKind::secondary, true, 0.0, false, true);
    CHECK (contrastRatio (l.content, l.fill) >= textContrastFloor);
    CHECK (buttonLook (p, ButtonKind::secondary, true, 0.0, true, true).fill == p.accentPressed);
    // a flat icon that acts once: nothing at rest, a bg5 tile with an edge under the mouse
    l = buttonLook (p, ButtonKind::flatMomentary, false, 0.0, false, true);
    CHECK (alpha (l.fill) == 0 && alpha (l.edge) == 0);
    l = buttonLook (p, ButtonKind::flatMomentary, false, 1.0, false, true);
    CHECK ((l.fill & 0x00ffffffu) == (p.bg5 & 0x00ffffffu) && alpha (l.fill) == 255 && l.content == p.textPrimary);
    // a flat switch previews its "on" look under the mouse (FL 2024): the accent's muted fill, an accent edge, a red icon
    l = buttonLook (p, ButtonKind::flatToggle, false, 1.0, false, true);
    CHECK ((l.fill & 0x00ffffffu) == (p.accentMuted & 0x00ffffffu) && alpha (l.fill) > 150 && alpha (l.fill) < 200);
    CHECK ((l.edge & 0x00ffffffu) == (p.accent & 0x00ffffffu) && l.content == p.accentHover);
    CHECK (buttonLook (p, ButtonKind::flatToggle, true, 0.0, false, true).fill == p.accent);
    // disabled: the disabled text token, no hover, half the fill
    for (const auto kind : { ButtonKind::secondary, ButtonKind::flatMomentary, ButtonKind::flatToggle })
    {
        const auto d = buttonLook (p, kind, false, 1.0, false, false);
        CHECK (d.content == p.textDisabled);
        CHECK (d == buttonLook (p, kind, false, 0.0, false, false));
    }
    CHECK (alpha (buttonLook (p, ButtonKind::secondary, false, 0.0, false, false).fill) == 128);
}

TEST (g1menus190je_grid_line_intensity)
{
    using namespace lynx::themes;
    const auto& p = defaultPalette();
    CHECK (withGridIntensity (p, 1.0) == p);
    CHECK (paletteFor (Spec {}) == p);
    const auto strong = withGridIntensity (p, 1.5), soft = withGridIntensity (p, 0.6);
    CHECK (contrastRatio (strong.gridBeat, p.rowWhite) > contrastRatio (p.gridBeat, p.rowWhite));
    CHECK (contrastRatio (soft.gridBeat, p.rowWhite) < contrastRatio (p.gridBeat, p.rowWhite));
    CHECK (contrastRatio (strong.gridBar, p.rowWhite) > contrastRatio (p.gridBar, p.rowWhite));
    CHECK (withGridIntensity (p, 9.0) == withGridIntensity (p, gridIntensityMax)); // clamped
    CHECK (strong.bg3 == p.bg3 && strong.textPrimary == p.textPrimary);           // only the lines
    Spec spec;
    spec.gridIntensity = 1.5f;
    CHECK (paletteFor (spec) == strong);
}

TEST (g1menus190je_menu_colours_read)
{
    using namespace lynx::themes;
    const auto& p = defaultPalette();
    const auto m = menuColours (p, MenuStyle::lynxDark);
    CHECK (m.background == p.bg3 && m.band == 0xff2d3139);
    CHECK (contrastRatio (m.text, m.background) >= textContrastFloor);
    CHECK (contrastRatio (m.shortcut, m.background) >= textContrastFloor);
    CHECK (contrastRatio (m.disabled, m.background) >= disabledContrastFloor);
    CHECK (contrastRatio (m.hoverText, m.hover) >= textContrastFloor);
    CHECK (contrastRatio (m.bandText, m.band) >= textContrastFloor);
}

TEST (g1menus190je_fl_classic_menu_style)
{
    using namespace lynx::themes;
    const auto& p = defaultPalette();
    CHECK (menuStyleFromId (menuStyleId (MenuStyle::flClassic)) == MenuStyle::flClassic);
    CHECK (menuStyleFromId ("lynx") == MenuStyle::lynxDark && menuStyleFromId ("bogus") == MenuStyle::lynxDark);
    const auto fl = menuColours (p, MenuStyle::flClassic);
    CHECK (fl.background == 0xffbbc4c9 && fl.text == 0xff0f1f29 && fl.band == 0xff919ca4 && fl.hover == 0xff5e6c75); // FL 2026's
    CHECK (contrastRatio (fl.text, fl.background) >= textContrastFloor);
    CHECK (contrastRatio (fl.shortcut, fl.background) >= textContrastFloor);
    CHECK (contrastRatio (fl.disabled, fl.background) >= disabledContrastFloor);
    CHECK (contrastRatio (fl.hoverText, fl.hover) >= textContrastFloor);
    CHECK (contrastRatio (fl.bandText, fl.band) >= textContrastFloor);
    Spec spec;
    CHECK (spec.menuStyle == MenuStyle::lynxDark); // dark menus stay the default
}

TEST (g1menus190je_tall_menus_break_at_a_group)
{
    using namespace lynx::themes;
    const auto item = [] { return MenuRow { 21, false, false }; };
    const auto sep = [] { return MenuRow { 5, true, false }; };
    const auto head = [] { return MenuRow { 17, false, true }; };
    std::vector<MenuRow> rows;
    for (int i = 0; i < 10; ++i)
        rows.push_back (item());
    CHECK (tallMenuBreak (rows, 756) == -1); // fits: one column
    // FL's mixer track menu shape: groups under section headers, 1370 px in LYNX
    rows.clear();
    for (int group = 0; group < 6; ++group)
    {
        rows.push_back (head());
        for (int i = 0; i < 10; ++i)
            rows.push_back (item());
    }
    const auto at = tallMenuBreak (rows, 756);
    CHECK (at == 32); // the row before the fourth header: three groups per column
    CHECK (rows[(std::size_t) at + 1].header);
    // only separators: after the one nearest the middle
    rows.clear();
    for (int group = 0; group < 4; ++group)
    {
        for (int i = 0; i < 12; ++i)
            rows.push_back (item());
        if (group < 3)
            rows.push_back (sep());
    }
    const auto atSep = tallMenuBreak (rows, 756);
    CHECK (atSep >= 0 && rows[(std::size_t) atSep].separator && atSep == 25);
    // no group boundary at all, or two columns still taller than the screen: JUCE's own split
    rows.assign (60, item());
    CHECK (tallMenuBreak (rows, 756) == -1);
    rows.clear();
    rows.push_back (head());
    for (int i = 0; i < 100; ++i)
        rows.push_back (item());
    rows.push_back (head());
    for (int i = 0; i < 100; ++i)
        rows.push_back (item());
    CHECK (tallMenuBreak (rows, 756) == -1);
}
