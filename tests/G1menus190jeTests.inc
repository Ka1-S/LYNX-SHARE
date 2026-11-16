// Round 190je: design190ia slice 10 (G1: contrast floors, one state language, grid line intensity). The rules live in core/ThemePalette.h.

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
