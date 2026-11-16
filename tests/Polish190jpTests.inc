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
    CHECK (std::string (dotId (Dot::selection)) == "selection" && std::string (dotId (Dot::text)) == "text" && dotCount == 8);
}
