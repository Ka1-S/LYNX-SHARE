#pragma once

// Round 190bi: the pixel scale of the headless picture routes (--snapshot, --plugui, --pluginthumbs, --flextrashot,
// --danceshot, --parityshot and the other *shot routes). The same layout is drawn at that many pixels per logical pixel
// (vector and text drawn at the scale, as on a 150 % / 200 % display), so the website and the README get sharp
// pictures on dense screens. Chosen with `--shot-scale <v>` or the environment variable LYNX_SHOT_SCALE (the older
// LYNX_PARITYSHOT_SCALE of round 190ar still works); no option = 1, the pictures stay exactly as before.
// This part is plain C++ (no JUCE) so the core tests can check it; ui/ShotScale.h holds the drawing helpers.

#include <cmath>
#include <cstdlib>
#include <string>

namespace lynx::shotscale
{
constexpr float minScale = 1.0f, maxScale = 4.0f;

// "2", "1.5", "2x", "200%", " 3 " -> the scale (kept within 1..4); empty or unreadable text -> 0 (= not given)
inline float parse (const std::string& text)
{
    auto t = text;
    while (! t.empty() && (t.back() == ' ' || t.back() == '\t'))
        t.pop_back();
    std::size_t start = 0;
    while (start < t.size() && (t[start] == ' ' || t[start] == '\t'))
        ++start;
    t = t.substr (start);
    if (t.empty())
        return 0.0f;
    auto percent = false;
    if (t.back() == '%')
        percent = true, t.pop_back();
    else if (t.back() == 'x' || t.back() == 'X')
        t.pop_back();
    if (t.empty())
        return 0.0f;
    char* end = nullptr;
    const auto value = std::strtod (t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || ! std::isfinite (value) || value <= 0.0)
        return 0.0f;
    const auto scale = (float) (percent ? value / 100.0 : value);
    return scale < minScale ? minScale : scale > maxScale ? maxScale : scale;
}

// the scale in force: the argument (--shot-scale) wins, then LYNX_SHOT_SCALE, then LYNX_PARITYSHOT_SCALE, else 1.
// `given` tells whether any of them was set (some routes change more than the pixel density when asked: --pluginthumbs
// then draws the whole editor instead of the picker's small thumbnail)
struct Choice
{
    float scale = 1.0f;
    bool given = false;
};

inline Choice choose (const std::string& argument, const std::string& environment, const std::string& parityEnvironment)
{
    for (const auto* text : { &argument, &environment, &parityEnvironment })
        if (const auto s = parse (*text); s > 0.0f)
            return { s, true };
    return {};
}

// "a/b/name.png" + "@2x" -> "a/b/name@2x.png" (the suffix goes before the extension; no extension: at the end)
inline std::string withSuffix (const std::string& path, const std::string& suffix)
{
    if (suffix.empty())
        return path;
    const auto slash = path.find_last_of ("/\\");
    const auto dot = path.find_last_of ('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || dot == (slash == std::string::npos ? 0 : slash + 1))
        return path + suffix;
    return path.substr (0, dot) + suffix + path.substr (dot);
}

// a size in logical pixels at the scale (what the picture's width / height becomes)
inline int pixels (int logical, float scale)
{
    return (int) std::lround ((double) logical * (double) scale);
}
} // namespace lynx::shotscale
