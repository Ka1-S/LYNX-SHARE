// Round 190bi: the shared pixel scale of the headless picture routes (core/ShotScale.h): --shot-scale / LYNX_SHOT_SCALE /
// LYNX_PARITYSHOT_SCALE (round 190ar's name, kept as an alias). No option must mean scale 1 and "not given", so every
// route draws exactly the pictures it drew before.

#include "core/ShotScale.h"

TEST (s190bi_shot_scale_parse)
{
    using lynx::shotscale::parse;
    CHECK (parse ("") == 0.0f);
    CHECK (parse ("   ") == 0.0f);
    CHECK (parse ("abc") == 0.0f);
    CHECK (parse ("2abc") == 0.0f);
    CHECK (parse ("-2") == 0.0f);
    CHECK (parse ("0") == 0.0f);
    CHECK (parse ("x") == 0.0f);
    CHECK (parse ("nan") == 0.0f);
    CHECK (parse ("1") == 1.0f);
    CHECK (parse ("1.5") == 1.5f);
    CHECK (parse ("2") == 2.0f);
    CHECK (parse (" 3 ") == 3.0f);
    CHECK (parse ("2x") == 2.0f);
    CHECK (parse ("200%") == 2.0f);
    CHECK (parse ("150%") == 1.5f);
    CHECK (parse ("0.5") == 1.0f);  // never below 1 (the pictures are never drawn smaller than the layout)
    CHECK (parse ("9") == 4.0f);    // at most 4
}

TEST (s190bi_shot_scale_default_is_unchanged)
{
    using lynx::shotscale::choose;
    // nothing set: scale 1, not given -> the routes keep their old pictures (same sizes, same calls)
    const auto none = choose ("", "", "");
    CHECK (none.scale == 1.0f && ! none.given);
    CHECK (lynx::shotscale::pixels (1600, none.scale) == 1600 && lynx::shotscale::pixels (900, none.scale) == 900);
    // unreadable values count as not set
    const auto junk = choose ("big", "", "");
    CHECK (junk.scale == 1.0f && ! junk.given);
    // "1" is given (an explicit 1x picture: --pluginthumbs then draws the whole editor) but the scale stays 1
    const auto one = choose ("1", "", "");
    CHECK (one.scale == 1.0f && one.given);
}

TEST (s190bi_shot_scale_order_and_alias)
{
    using lynx::shotscale::choose;
    CHECK (choose ("2", "3", "1.5").scale == 2.0f);   // the argument wins
    CHECK (choose ("", "3", "1.5").scale == 3.0f);    // then LYNX_SHOT_SCALE
    CHECK (choose ("", "", "1.5").scale == 1.5f);     // then LYNX_PARITYSHOT_SCALE (round 190ar)
    CHECK (choose ("", "", "1.5").given);
    CHECK (choose ("junk", "2", "").scale == 2.0f);   // an unreadable argument does not hide the variable
    CHECK (lynx::shotscale::pixels (1600, 1.5f) == 2400 && lynx::shotscale::pixels (517, 1.5f) == 776);
}

TEST (s190bi_shot_suffix)
{
    using lynx::shotscale::withSuffix;
    CHECK (withSuffix ("out/s-mixer.png", "") == "out/s-mixer.png");
    CHECK (withSuffix ("out/s-mixer.png", "@2x") == "out/s-mixer@2x.png");
    CHECK (withSuffix ("C:\\shots\\Facet EQ.png", "@2x") == "C:\\shots\\Facet EQ@2x.png");
    CHECK (withSuffix ("out.dir/name", "@2x") == "out.dir/name@2x");
    CHECK (withSuffix ("out/.hidden", "@2x") == "out/.hidden@2x");
    CHECK (withSuffix ("a.b.png", "-200") == "a.b-200.png");
}
