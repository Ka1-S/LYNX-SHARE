#include "ui/PluginSkin.h"
#include "ui/ImageCaches.h"

#include "core/Effects.h"
#include "ui/I18n.h"
#include "ui/WindowOwner.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

#if JUCE_WINDOWS
extern "C" __declspec (dllimport) int __stdcall SystemParametersInfoW (unsigned int, unsigned int, void*, unsigned int);
static int lynxSystemParametersInfoW (unsigned int a, unsigned int b, void* c, unsigned int d) { return SystemParametersInfoW (a, b, c, d); }
#endif

namespace lynx::skin
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float arcStart = -pi * 0.75f;
constexpr float arcRange = pi * 1.5f;

juce::Path arcPath (juce::Point<float> c, float r, float from, float to)
{
    juce::Path p;
    p.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    return p;
}

juce::PathStrokeType roundStroke (float w)
{
    return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}
} // namespace

juce::Colour accentFor (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::reverb: case EffectKind::reeverb2: case EffectKind::reeverb1: case EffectKind::luxeVerb: case EffectKind::convolver:
        case EffectKind::epochVerb: case EffectKind::atrium: case EffectKind::foilPlate: case EffectKind::nebula: case EffectKind::halo:
        case EffectKind::horizon:
        case EffectKind::pocketReverbClassic: case EffectKind::pocketReverb2: case EffectKind::pocketReverb: // round 190dw
            return colour::violet;
        case EffectKind::delay: case EffectKind::fruityDelay2: case EffectKind::fruityDelay3: case EffectKind::fruityDelayBank: case EffectKind::pocketTranceDelay: case EffectKind::pocketTapeDelay: case EffectKind::pocketTapeStop:
        case EffectKind::multibandDelay: case EffectKind::echoform: case EffectKind::spiralEcho: case EffectKind::geminiDoubler:
        case EffectKind::newtime:
            return colour::azure;
        case EffectKind::chorus: case EffectKind::fruityChorus: case EffectKind::hyperChorus: case EffectKind::vintageChorus:
        case EffectKind::fruityFlanger: case EffectKind::fruityFlangus: case EffectKind::fruityPhaser: case EffectKind::vintagePhaser:
        case EffectKind::orbit: case EffectKind::latticeMod: case EffectKind::autoPan: case EffectKind::fruityPanOMatic:
        case EffectKind::effector: case EffectKind::pocketMultiFx: case EffectKind::pocketWowFlutter: case EffectKind::pocketPhaser: case EffectKind::pocketFlanger: case EffectKind::pocketTremolo: case EffectKind::pocketChorus: case EffectKind::freqShift: case EffectKind::grossBeat: case EffectKind::volumeGate: case EffectKind::rhythmGain:
            return colour::teal;
        case EffectKind::compressor: case EffectKind::limiter: case EffectKind::fruityLimiter: case EffectKind::fruityCompressor:
        case EffectKind::fruityMultiband: case EffectKind::crestCompressor: case EffectKind::prismMultiband: case EffectKind::sentryGate:
        case EffectKind::apexLimiter: case EffectKind::glowLeveler: case EffectKind::hushDeEsser: case EffectKind::transientProcessor: case EffectKind::shellChannel: case EffectKind::strikeShaper:
        case EffectKind::maximus: case EffectKind::peakController: case EffectKind::pocketLimiterClassic: case EffectKind::pocketLimiter: case EffectKind::pocketLeveller: case EffectKind::pocketGate:
        case EffectKind::pocketAutoduck:
        case EffectKind::pocketCompressor: case EffectKind::pocketMultiband:
            return colour::amber;
        case EffectKind::distortion: case EffectKind::fruityBloodOverdrive: case EffectKind::fruityFastDist: case EffectKind::ember: case EffectKind::kilnChannel: case EffectKind::fretChannel:
        case EffectKind::hardcore: case EffectKind::distructor: case EffectKind::fruityWaveShaper: case EffectKind::fruitySoftClipper:
        case EffectKind::lofi: case EffectKind::squeeze: case EffectKind::pocketDistortion: case EffectKind::pocketWaveshaper:
            return colour::ember;
        case EffectKind::lynxTune: case EffectKind::pitchCorrect: case EffectKind::pitchShift: case EffectKind::flPitchShifter:
        case EffectKind::vocoder: case EffectKind::vocodex: case EffectKind::quellDenoiser: case EffectKind::aether: case EffectKind::newtone: case EffectKind::edison:
        case EffectKind::voiceShift:
            return colour::rose;
        case EffectKind::utility: case EffectKind::stereoShaper: case EffectKind::stereoEnhancer: case EffectKind::spreader:
        case EffectKind::fruityBalance: case EffectKind::fruitySend: case EffectKind::pocketSpacer: case EffectKind::pocketStereoizer:
        case EffectKind::pocketSpreader:
            return colour::lime;
        default:
            return colour::crimson;
    }
}

juce::Colour accentForInstrument (const juce::String& name)
{
    if (name.containsIgnoreCase ("Keys") || name.containsIgnoreCase ("Piano")) return colour::azure;
    if (name.containsIgnoreCase ("Bass")) return colour::ember;
    if (name.containsIgnoreCase ("Harmor") || name.containsIgnoreCase ("Sytrus")) return colour::violet;
    return colour::crimson;
}


// --- effects level -------------------------------------------------------------------------------

namespace
{
bool reduced = false;
Style currentStyle = Style::glow;
Art currentArt = Art::none;
bool motion = true;

// a deterministic grain tile (white and black specks at a few % alpha) laid over the canvases
const juce::Image& grainTile()
{
    static juce::Image tile;
    static const bool registered = lynx::ui::registerImageCache ([] { tile = {}; }); (void) registered;
    if (tile.isNull())
        tile = [] {
        juce::Image img (juce::Image::ARGB, 128, 128, true);
        juce::Random rnd (126);
        juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
            {
                const auto v = rnd.nextFloat();
                const auto light = rnd.nextBool();
                const auto a = (juce::uint8) (v * v * 11.0f);
                px.setPixelColour (x, y, (light ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            }
        return img;
        }();
    return tile;
}

float pixelScale (juce::Graphics& g)
{
    return juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
}

// a static layer rendered once per (size, accent, scale, style) into an image and blitted afterwards
void cachedLayer (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent, int style,
                  const std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>& draw)
{
    if (a.isEmpty()) return;
    const auto scale = pixelScale (g);
    const auto w = (int) std::ceil (a.getWidth() * scale), h = (int) std::ceil (a.getHeight() * scale);
    if (w <= 0 || h <= 0 || (std::int64_t) w * h > 12'000'000) { draw (g, a, accent); return; }
    std::uint64_t key = (std::uint64_t) w;
    key = key * 16411u + (std::uint64_t) h;
    key = key * 31u + (std::uint64_t) style * 2u + (reduced ? 1u : 0u);
    key = key * 7u + (std::uint64_t) currentStyle;
    key = key * 11u + (std::uint64_t) currentArt;
    key ^= (std::uint64_t) accent.getARGB() * 0x9e3779b97f4a7c15ull;
    static std::map<std::uint64_t, juce::Image> cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.clear(); }); (void) registered;
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 10) cache.clear();
        juce::Image img (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale ((float) w / a.getWidth(), (float) h / a.getHeight()));
            draw (ig, { 0.0f, 0.0f, a.getWidth(), a.getHeight() }, accent);
        }
        // Both layers (canvas, backdrop) are an editor's opaque base: every canvas starts with an opaque fill. Quartz's
        // 8-bit source-over rounds the alpha of an opaque pixel under a faint translucent blit (a bloom sprite) down to
        // 254, so on macOS the base came out a hair see-through (CI selftest plugin_art). Snap such pixels back to opaque
        // (premultiplied data: the colour stays valid); real translucency is far below this threshold.
        {
            juce::Image::BitmapData px (img, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < px.height; ++y)
            {
                auto* row = px.getLinePointer (y);
                for (int x = 0; x < px.width; ++x)
                {
                    auto* p = reinterpret_cast<juce::PixelARGB*> (row + x * px.pixelStride);
                    if (p->getAlpha() >= 0xfc && p->getAlpha() < 0xff) p->setAlpha (0xff);
                }
            }
        }
        it = cache.emplace (key, img).first;
    }
    g.setOpacity (1.0f);
    g.drawImage (it->second, a, juce::RectanglePlacement::stretchToFit);
}

struct OpenAnimation final : private juce::Timer
{
    OpenAnimation (juce::Component& c, Style st) : target (&c), t0 (juce::Time::getMillisecondCounterHiRes()), animStyle (st)
    {
        apply (0.0f);
        startTimerHz (60);
    }
    void apply (float u)
    {
        if (target == nullptr) return;
        const auto e = 1.0f - std::pow (1.0f - u, 3.0f); // ease out
        target->setAlpha (u >= 1.0f ? 1.0f : juce::jmax (0.02f, juce::jmin (1.0f, e * 1.4f)));
        const auto b = target->getLocalBounds().toFloat();
        // Vivid springs (a small overshoot), the others glide
        const auto spring = 1.0f - std::exp (-6.0f * u) * std::cos (u * 9.0f);
        const auto s = animStyle == Style::vivid ? 0.9f + 0.1f * spring : 0.955f + 0.045f * e;
        target->setTransform (u >= 1.0f ? juce::AffineTransform() : juce::AffineTransform::scale (s, s, b.getCentreX(), b.getCentreY()));
    }
    void timerCallback() override
    {
        const auto u = (float) juce::jmin (1.0, (juce::Time::getMillisecondCounterHiRes() - t0) / 220.0);
        apply (u);
        if (u >= 1.0f || target == nullptr)
        {
            stopTimer();
            delete this;
        }
    }
    juce::Component::SafePointer<juce::Component> target;
    double t0;
    Style animStyle;
};
} // namespace

bool reducedEffects() { return reduced; }
void setReducedEffects (bool on)
{
    reduced = on;
    kickArtLayers();
}

Style style() { return currentStyle; }
void setStyle (Style s) { currentStyle = s; }
Art art() { return currentArt; }
void setArt (Art a) { currentArt = a; }
bool artMotion() { return motion; }
void setArtMotion (bool on)
{
    motion = on;
    kickArtLayers();
}
const char* styleName (Style s)
{
    switch (s)
    {
        case Style::neon: return "Neon";
        case Style::hardware: return "Hardware";
        case Style::vivid: return "Vivid";
        case Style::themed: return "Themed";
        default: return "Glow";
    }
}
Style styleFromName (const juce::String& name)
{
    for (int i = 0; i < styleCount; ++i)
        if (name.trim().equalsIgnoreCase (styleName ((Style) i))) return (Style) i;
    return Style::glow;
}

void playOpenAnimation (juce::Component& content, Style st)
{
    if (reduced) return;
    new OpenAnimation (content, st); // deletes itself when done (or when the content goes away)
}

// --- per-plugin styles -----------------------------------------------------------------------------

namespace
{
int forced = -1;
std::map<int, int> overrides;
constexpr int soundgoodizerKey = 1000;
} // namespace

int forcedStyle() { return forced; }
void setForcedStyle (int s)
{
    forced = s >= 0 && s < styleCount ? s : -1;
    kickArtLayers();
}
int styleKey (EffectKind kind, bool soundgoodizer) { return soundgoodizer ? soundgoodizerKey : (int) kind; }

Style defaultStyleFor (int key)
{
    if (key >= 0 && key < soundgoodizerKey && artForEffect ((EffectKind) key) != Art::none) return Style::themed; // round 170j: its own art
    return plainStyleFor (key);
}

Style plainStyleFor (int key)
{
    using K = EffectKind;
    constexpr auto G = Style::glow, N = Style::neon, H = Style::hardware, V = Style::vivid;
    if (key == soundgoodizerKey) return V;
    switch ((K) key)
    {
        // the user's picks
        case K::lynxTune: return V;
        case K::epochVerb: return H;
        case K::crestCompressor: return G;
        // analytic, display-first tools: Glow
        case K::facetEq: case K::apexLimiter: case K::prismMultiband: case K::hushDeEsser: case K::frequencySplitter: case K::quellDenoiser:
        case K::fruityMultiband: case K::fruitySend: case K::fruityBalance: case K::luxeVerb: case K::convolver: case K::fruityDelay2:
        case K::fruityChorus: case K::fruitySoftClipper: case K::reeverb1: case K::peakController:
            return G;
        // analogue / vintage-modelled roles: Hardware
        case K::glowLeveler: case K::ridgeStrip: case K::foilPlate: case K::echoform: case K::vintageChorus: case K::vintagePhaser: case K::maximus:
        case K::fruityBloodOverdrive: case K::fruityCompressor: case K::fruityLimiter: case K::reeverb2: case K::aether: case K::vocoder: case K::kilnChannel: case K::fretChannel: case K::shellChannel: case K::strikeShaper:
            return H;
        // creative, pitch and fun: Vivid
        case K::geminiDoubler: case K::voiceShift: case K::ember: case K::grossBeat: case K::flPitchShifter: case K::fruityFastDist: case K::effector:
        case K::fruityPanOMatic: case K::transientProcessor: case K::squeeze: case K::hyperChorus: case K::fruityFlanger: case K::fruityDelay3:
        case K::multibandDelay: case K::horizon: case K::distructor:
            return V;
        // modulation, synthesis and space: Neon
        case K::latticeMod: case K::orbit: case K::spiralEcho: case K::halo: case K::nebula: case K::atrium: case K::fruityFlangus:
        case K::fruityPhaser: case K::fruityDelayBank: case K::vocodex: case K::hardcore: case K::fruityWaveShaper: case K::stereoShaper:
        case K::stereoEnhancer: case K::equo: case K::parametricEq: case K::fruityFilter: case K::fruityFreeFilter: case K::sentryGate:
        case K::pitchShift: case K::spreader:
            return N;
        default: return G;
    }
}

Style resolve (Style automatic) { return forced >= 0 ? (Style) forced : automatic; }

Style resolve (Style automatic, Art a)
{
    const auto s = resolve (automatic);
    if (s != Style::themed || a != Art::none) return s;
    return automatic != Style::themed ? automatic : Style::glow; // Themed everywhere: a page without art keeps its own style
}

Style styleFor (EffectKind kind, bool soundgoodizer)
{
    const auto key = styleKey (kind, soundgoodizer);
    const auto hasArt = ! soundgoodizer && artForEffect (kind) != Art::none;
    auto fallback = [&] (Style s) {
        if (s != Style::themed || hasArt) return s;
        const auto own = defaultStyleFor (key);
        return own != Style::themed ? own : Style::glow;
    };
    if (forced >= 0) return fallback ((Style) forced);
    if (const auto o = overrideFor (key); o >= 0) return fallback ((Style) o);
    return fallback (defaultStyleFor (key));
}

int overrideFor (int key)
{
    const auto it = overrides.find (key);
    return it != overrides.end() ? it->second : -1;
}

void setOverride (int key, int s)
{
    if (s >= 0 && s < styleCount) overrides[key] = s;
    else overrides.erase (key);
    kickArtLayers();
}

juce::String overridesToString()
{
    juce::StringArray parts;
    for (const auto& [k, s] : overrides) parts.add (juce::String (k) + ":" + styleName ((Style) s));
    return parts.joinIntoString (",");
}

void overridesFromString (const juce::String& text)
{
    overrides.clear();
    for (const auto& part : juce::StringArray::fromTokens (text, ",", ""))
        if (part.containsChar (':'))
            setOverride (part.upToFirstOccurrenceOf (":", false, false).getIntValue(), (int) styleFromName (part.fromFirstOccurrenceOf (":", false, false)));
}

void addStyleMenu (juce::PopupMenu& menu, int key, std::function<void()> changed)
{
    juce::PopupMenu sub;
    const auto now = overrideFor (key);
    auto apply = [key, changed] (int s) {
        setOverride (key, s);
        if (onStyleSettingsChanged) onStyleSettingsChanged();
        if (changed) changed();
    };
    sub.addItem (tr ("Automatic ({0})", { tr (styleName (defaultStyleFor (key))) }), true, now < 0, [apply] { apply (-1); });
    const auto hasArt = key >= 0 && key < soundgoodizerKey && artForEffect ((EffectKind) key) != Art::none;
    for (int i = 0; i < styleCount; ++i)
        if ((Style) i != Style::themed || hasArt)
            sub.addItem (tr (styleName ((Style) i)), true, now == i, [apply, i] { apply (i); });
    if (forced >= 0)
    {
        sub.addSeparator();
        sub.addItem (tr ("Settings force one style for every plugin"), false, false, [] {});
    }
    menu.addSubMenu (tr ("Plugin style"), sub);
}

// a bloom is a cached sprite per colour and strength (a radial gradient is costly per pixel; a blit is not)
void bloom (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float alpha)
{
    if (reduced || radius <= 0.5f || alpha <= 0.002f) return;
    const auto level = juce::jlimit (1, 64, (int) std::lround (alpha * 64.0f));
    const auto key = (std::uint64_t) colour.withAlpha ((juce::uint8) 255).getARGB() * 67u + (std::uint64_t) level;
    static std::unordered_map<std::uint64_t, juce::Image> sprites;
    static const bool registered = lynx::ui::registerImageCache ([] { sprites.clear(); }); (void) registered;
    auto it = sprites.find (key);
    if (it == sprites.end())
    {
        if (sprites.size() > 400) sprites.clear();
        constexpr int n = 96;
        juce::Image img (juce::Image::ARGB, n, n, true);
        {
            juce::Graphics ig (img);
            const auto a = (float) level / 64.0f;
            juce::ColourGradient grad (colour.withAlpha (a), n * 0.5f, n * 0.5f, colour.withAlpha (0.0f), (float) n, n * 0.5f, true);
            grad.addColour (0.45, colour.withAlpha (a * 0.42f));
            ig.setGradientFill (grad);
            ig.fillEllipse (0.0f, 0.0f, (float) n, (float) n);
        }
        it = sprites.emplace (key, img).first;
    }
    g.setOpacity (1.0f);
    g.drawImage (it->second, juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), juce::RectanglePlacement::stretchToFit);
}

#include "ui/PluginStyles.inc"
#include "ui/PluginArt.inc"

// --- surfaces ------------------------------------------------------------------------------------

namespace
{
void drawCanvas (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent, bool display)
{
    if (currentStyle == Style::themed) { artkit::canvas (g, a, accent, display); return; }
    if (currentStyle != Style::glow) { styles::canvas (g, a, accent, display, currentStyle); return; }
    // the deep base
    g.setGradientFill (juce::ColourGradient (display ? juce::Colour (0xff14161c) : juce::Colour (0xff1c1e26), 0.0f, a.getY(),
                                             display ? juce::Colour (0xff060709) : juce::Colour (0xff0b0c10), 0.0f, a.getBottom(), false));
    g.fillRect (a);
    const auto big = juce::jmax (a.getWidth(), a.getHeight());
    if (! reduced)
    {
        // aurora: a large bloom of the accent behind the upper left, a cooler one to the right, LYNX red low on the right
        auto blob = [&] (float fx, float fy, float r, juce::Colour c, float alpha) {
            const juce::Point<float> p { a.getX() + a.getWidth() * fx, a.getY() + a.getHeight() * fy };
            juce::ColourGradient grad (c.withAlpha (alpha), p.x, p.y, c.withAlpha (0.0f), p.x + r, p.y, true);
            grad.addColour (0.35, c.withAlpha (alpha * 0.55f));
            grad.addColour (0.7, c.withAlpha (alpha * 0.14f));
            g.setGradientFill (grad);
            g.fillRect (a);
        };
        const auto second = accent.withRotatedHue (accent == colour::crimson ? 0.07f : -0.09f);
        blob (0.24f, display ? 0.0f : 0.06f, big * 0.62f, accent, display ? 0.26f : 0.32f);
        blob (0.86f, display ? 0.15f : 0.22f, big * 0.42f, second, display ? 0.12f : 0.17f);
        blob (0.95f, 1.05f, big * 0.42f, colour::crimson, 0.12f);
        blob (0.05f, 1.0f, big * 0.32f, accent, 0.06f);
        // grain
        g.setTiledImageFill (grainTile(), 0, 0, 1.0f);
        g.fillRect (a);
    }
    // vignette
    juce::ColourGradient vig (juce::Colours::transparentBlack, a.getCentreX(), a.getCentreY(), juce::Colours::black.withAlpha (0.42f),
                              a.getCentreX() + big * 0.72f, a.getCentreY(), true);
    vig.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (vig);
    g.fillRect (a);
}
} // namespace

void canvas (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent)
{
    cachedLayer (g, a, accent, 0, [] (juce::Graphics& lg, juce::Rectangle<float> r, juce::Colour c) { drawCanvas (lg, r, c, false); });
}

void backdrop (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent)
{
    cachedLayer (g, a, accent, 1, [] (juce::Graphics& lg, juce::Rectangle<float> r, juce::Colour c) { drawCanvas (lg, r, c, true); });
}

void headerBar (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent)
{
    if (currentStyle == Style::themed && artkit::headerBar (g, a, accent)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::headerBar (g, a, accent, currentStyle); return; }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2f39), 0.0f, a.getY(), juce::Colour (0xff15171c), 0.0f, a.getBottom(), false));
    g.fillRect (a);
    if (! reduced)
    {
        // the accent washing in from the left behind the name plate, the brand red from the right
        juce::ColourGradient wash (accent.withAlpha (0.30f), a.getX() + 70.0f, a.getCentreY(), accent.withAlpha (0.0f), a.getX() + 70.0f + juce::jmin (520.0f, a.getWidth() * 0.6f),
                                   a.getCentreY(), true);
        wash.addColour (0.4, accent.withAlpha (0.10f));
        g.setGradientFill (wash);
        g.fillRect (a);
        juce::ColourGradient brand (colour::crimson.withAlpha (0.10f), a.getRight(), a.getCentreY(), colour::crimson.withAlpha (0.0f), a.getRight() - 300.0f, a.getCentreY(), true);
        g.setGradientFill (brand);
        g.fillRect (a);
    }
    // glass: a sheen over the top half and a crisp top highlight
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.075f), 0.0f, a.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, a.getCentreY(), false));
    g.fillRect (a.withHeight (a.getHeight() * 0.5f));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) a.getY(), a.getX(), a.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawHorizontalLine ((int) a.getBottom() - 1, a.getX(), a.getRight());
    // the lit accent edge: bright on the left, fading across
    const auto lineW = juce::jmin (a.getWidth(), 640.0f);
    if (! reduced)
    {
        juce::ColourGradient glow (accent.withAlpha (0.35f), a.getX(), 0.0f, accent.withAlpha (0.0f), a.getX() + lineW, 0.0f, false);
        g.setGradientFill (glow);
        g.fillRect (juce::Rectangle<float> (a.getX(), a.getBottom() - 4.0f, lineW, 4.0f));
    }
    juce::ColourGradient line (accent.brighter (0.25f), a.getX(), 0.0f, accent.withAlpha (0.0f), a.getX() + lineW, 0.0f, false);
    line.addColour (0.3, accent.withAlpha (0.8f));
    g.setGradientFill (line);
    g.fillRect (juce::Rectangle<float> (a.getX(), a.getBottom() - 2.0f, lineW, 1.5f));
}

void brandMark (juce::Graphics& g, juce::Rectangle<float> a)
{
    const auto d = juce::jmin (a.getWidth(), a.getHeight());
    if (d < 6.0f) return;
    // the logo resampled once per size (×2 for high-DPI)
    static std::map<int, juce::Image> cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.clear(); }); (void) registered;
    const auto px = juce::jlimit (8, 128, (int) std::ceil (d * 2.0f));
    auto& img = cache[px];
    if (img.isNull())
    {
        const auto logo = logoImage();
        if (logo.isNull()) return;
        // halve step by step: one big jump from 512 px aliases at the 16-24 px header size
        img = logo;
        while (px > 0 && img.getWidth() / 2 >= px)
            img = img.rescaled (img.getWidth() / 2, img.getHeight() / 2, juce::Graphics::highResamplingQuality);
        if (img.getWidth() != px)
            img = img.rescaled (px, px, juce::Graphics::highResamplingQuality);
    }
    const auto r = a.withSizeKeepingCentre (d, d);
    bloom (g, r.getCentre(), d * 1.05f, colour::crimson, 0.55f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (r.expanded (1.5f).translated (0.0f, 0.8f));
    g.setColour (colour::crimson.withAlpha (0.55f));
    g.drawEllipse (r.expanded (1.2f), 1.0f);
    g.setOpacity (1.0f);
    g.drawImage (img, r, juce::RectanglePlacement::centred);
}

namespace
{
TitleFit lastFit;

// round 170k2: how each style sets a plugin's name (its size for a box this high, kerning, capitals, padding)
struct TitleLook
{
    float nominal, kerning, pad;
    bool upper;
};
TitleLook titleLook (Style s, float h)
{
    switch (s)
    {
        case Style::neon: return { juce::jlimit (14.0f, 21.0f, h * 0.74f), 0.06f, 4.0f, false };
        case Style::hardware: return { juce::jlimit (12.0f, 16.0f, h * 0.56f), 0.08f, 16.0f, true };
        case Style::vivid: return { juce::jlimit (14.0f, 20.0f, h * 0.72f), 0.03f, 3.5f, true };
        default: return { juce::jlimit (13.0f, 18.0f, h * 0.62f), 0.02f, 2.0f, false };
    }
}
float titleWidth (const TitleLook& look, const juce::String& name, float size)
{
    const auto f = juce::Font (theme::font (size, true)).withExtraKerningFactor (look.kerning);
    return juce::GlyphArrangement::getStringWidth (f, look.upper ? name.toUpperCase() : name) + look.pad;
}
// the largest size from the style's own down to minSize at which the name fits `w`; 0 when even minSize does not
float fittingSize (const TitleLook& look, const juce::String& name, float w, float minSize)
{
    for (auto size = look.nominal; size >= minSize - 0.01f; size -= 0.5f)
        if (titleWidth (look, name, size) <= w - 1.0f) // (a pixel of slack: drawText rounds its own way)
            return size;
    return 0.0f;
}
constexpr float titleMinSize = 10.0f; // smaller than this a header name stops reading as a title

float drawTitleLine (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& name, juce::Colour accent, bool underline, float size);
} // namespace

TitleFit lastTitleFit() { return lastFit; }
void resetTitleFit() { lastFit = {}; }

// the prefix that may go to a small line above the name when the full name does not fit on one ("Fruity Blood
// Overdrive": "Fruity" over "Blood Overdrive")
static int titlePrefixLength (const juce::String& name)
{
    for (const auto* p : { "Fruity " })
        if (name.startsWithIgnoreCase (p) && name.length() > (int) std::strlen (p) + 2)
            return (int) std::strlen (p);
    return 0;
}

float titleMinWidth (const juce::String& name, float height)
{
    // the widest any style needs (a window's minimum size does not change with the style)
    auto most = 0.0f;
    for (const auto st : { Style::glow, Style::neon, Style::hardware, Style::vivid })
    {
        if (const auto n = titlePrefixLength (name); n > 0 && height >= 20.0f)
        {
            const auto low = titleLook (st, height * 0.7f);
            most = juce::jmax (most, titleWidth (low, name.substring (n), titleMinSize));
        }
        else
            most = juce::jmax (most, titleWidth (titleLook (st, height), name, titleMinSize));
    }
    return std::ceil (most) + 2.0f;
}

float titleText (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& name, juce::Colour accent, bool underline)
{
    const BaseScope base;
    const auto look = titleLook (currentStyle, a.getHeight());
    lastFit = { titleWidth (look, name, look.nominal), a.getWidth(), look.nominal, 1, false };
    // 1. one line, the style's size or a little smaller
    if (const auto size = fittingSize (look, name, a.getWidth(), juce::jmax (titleMinSize, look.nominal * 0.75f)); size > 0.0f)
    {
        lastFit.size = size;
        return drawTitleLine (g, a, name, accent, underline, size);
    }
    // 2. a known prefix ("Fruity") on a small line above, the rest of the name below it
    if (const auto n = titlePrefixLength (name); n > 0 && a.getHeight() >= 20.0f)
    {
        const auto rest = name.substring (n);
        const auto top = a.withHeight (a.getHeight() * 0.36f).translated (0.0f, 1.0f);
        const auto low = a.withTrimmedTop (a.getHeight() * 0.36f);
        const auto lowLook = titleLook (currentStyle, low.getHeight());
        if (const auto size = fittingSize (lowLook, rest, low.getWidth(), titleMinSize); size > 0.0f)
        {
            const auto prefix = name.substring (0, n).trim();
            const auto prefixSize = juce::jmax (8.0f, top.getHeight() * 0.95f);
            g.setFont (juce::Font (theme::font (prefixSize, true)).withExtraKerningFactor (0.12f));
            const auto prefixWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), prefix.toUpperCase()) + 4.0f;
            g.setColour (juce::Colours::white.withAlpha (0.62f));
            g.drawText (prefix.toUpperCase(), top.withTrimmedLeft (lowLook.pad > 8.0f ? 4.0f : 1.0f), juce::Justification::centredLeft, false);
            lastFit.size = size;
            lastFit.lines = 2;
            const auto w = drawTitleLine (g, low, rest, accent, false, size);
            return juce::jmax (w, prefixWidth);
        }
    }
    // 3. one line at the smallest title size (ellipsis only when even that does not fit)
    const auto size = juce::jmax (titleMinSize, fittingSize (look, name, a.getWidth(), titleMinSize));
    lastFit.size = size;
    lastFit.clipped = titleWidth (look, name, size) > a.getWidth() + 0.5f;
    return drawTitleLine (g, a, name, accent, underline, size);
}

namespace
{
float drawTitleLine (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& name, juce::Colour accent, bool underline, float size)
{
    if (currentStyle != Style::glow) return styles::titleText (g, a, name, accent, underline, currentStyle, size);
    // the name plate: big and bold, lit from behind in the accent
    g.setFont (juce::Font (theme::font (size, true)).withExtraKerningFactor (0.02f));
    const auto w = juce::jmin (a.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 2.0f);
    const auto box = a.withWidth (w);
    if (! reduced)
    {
        bloom (g, box.getCentre(), w * 0.62f, accent, 0.16f);
        g.setColour (accent.withAlpha (0.22f));
        for (const auto& o : { juce::Point<float> { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
            g.drawText (name, box.translated (o.x, o.y), juce::Justification::centredLeft, true);
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawText (name, box.translated (0.0f, 1.0f), juce::Justification::centredLeft, true);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0.0f, a.getCentreY() - size * 0.5f, juce::Colour (0xffd6d9e0).interpolatedWith (accent, 0.18f), 0.0f,
                                             a.getCentreY() + size * 0.5f, false));
    drawFilledText (g, name, box, juce::Justification::centredLeft); // (not drawText: see drawFilledText)
    if (underline)
    {
        const auto line = juce::Rectangle<float> (a.getX(), a.getBottom() - 3.0f, w, 2.0f);
        if (! reduced)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (line.expanded (0.0f, 1.5f), 2.0f);
        }
        g.setGradientFill (juce::ColourGradient (accent.brighter (0.3f), line.getX(), 0.0f, accent.withAlpha (0.1f), line.getRight(), 0.0f, false));
        g.fillRoundedRectangle (line, 1.0f);
    }
    return w;
}
} // namespace

void drawFilledText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification)
{
    if (text.isEmpty() || area.isEmpty()) return;
    juce::GlyphArrangement glyphs;
    glyphs.addCurtailedLineOfText (g.getCurrentFont(), text, 0.0f, 0.0f, area.getWidth(), true);
    glyphs.justifyGlyphs (0, glyphs.getNumGlyphs(), area.getX(), area.getY(), area.getWidth(), area.getHeight(), justification);
    juce::Path outline;
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i)
    {
        auto& glyph = glyphs.getGlyph (i);
        if (glyph.isWhitespace()) continue;
        juce::Path one;
        glyph.createPath (one);
        if (! one.isEmpty()) outline.addPath (one);
    }
    if (! outline.isEmpty()) g.fillPath (outline);
}

juce::String rendererProbeReport()
{
    // what a filled shape changed outside its own box, on this platform's native image type (CoreGraphics on macOS)
    const auto changedOutside = [] (const std::function<void (juce::Graphics&)>& draw, juce::Rectangle<int> inside) {
        juce::Image img (juce::Image::ARGB, 240, 80, true);
        {
            juce::Graphics g (img);
            g.fillAll (juce::Colours::black);
            draw (g);
        }
        int changed = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                if (! inside.contains (x, y) && img.getPixelAt (x, y).getBrightness() > 0.05f) ++changed;
        return changed;
    };
    const auto gradient = [] (juce::Graphics& g) {
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0.0f, 20.0f, juce::Colour (0xffd6c0c8), 0.0f, 40.0f, false));
        g.setFont (theme::font (16.0f, true));
    };
    const juce::Rectangle<int> box (20, 20, 120, 24);
    const auto rawText = changedOutside ([&] (juce::Graphics& g) { gradient (g); g.drawText ("Facet EQ", box.toFloat(), juce::Justification::centredLeft, true); }, box);
    const auto safeText = changedOutside ([&] (juce::Graphics& g) { gradient (g); drawFilledText (g, "Facet EQ", box.toFloat(), juce::Justification::centredLeft); }, box);
    const auto emptyPath = changedOutside ([&] (juce::Graphics& g) { gradient (g); g.fillPath (juce::Path()); }, {});
    juce::String r;
    r << "renderer probe (pixels changed outside the shape; 0 = right)\n";
    r << "gradient drawText \"Facet EQ\": " << rawText << "\n";
    r << "gradient drawFilledText \"Facet EQ\": " << safeText << "\n";
    r << "gradient fillPath (empty path): " << emptyPath << "\n";
    r << "result=" << (safeText == 0 ? "OK" : "FAIL") << "\n";
    return r;
}

void powerLed (juce::Graphics& g, juce::Point<float> c, float d, bool on)
{
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::powerLed (g, c, d, on, currentStyle); return; }
    const auto r = juce::Rectangle<float> (d, d).withCentre (c);
    if (on) bloom (g, c, d * 1.9f, theme::colour::ledOn, 0.55f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (r.expanded (1.2f));
    g.setGradientFill (juce::ColourGradient (on ? theme::colour::ledOn.brighter (0.5f) : theme::colour::ledOff.brighter (0.1f), c.x, r.getY(),
                                             on ? theme::colour::ledOn.darker (0.2f) : theme::colour::ledOff.darker (0.3f), c.x, r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.7f : 0.14f));
    g.fillEllipse (r.reduced (d * 0.3f).translated (-d * 0.1f, -d * 0.14f));
}

void section (juce::Graphics& g, juce::Rectangle<float> a, float corner, bool raised, juce::Colour tint)
{
    if (currentStyle == Style::themed && artkit::section (g, a, corner, raised, tint)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::section (g, a, corner, raised, tint, currentStyle); return; }
    // glass with real depth: a soft shadow under it, a translucent body (the aurora shows through), a sheen on the
    // upper part, the rim catching light along the top and a dark lip at the bottom
    if (! reduced)
    {
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (a.translated (0.0f, 4.0f).expanded (1.5f), corner + 1.5f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (a.translated (0.0f, 1.5f), corner);
    const auto top = raised ? juce::Colour (0xc8303440) : juce::Colour (0xb4242731);
    const auto bottom = raised ? juce::Colour (0xe6222530) : juce::Colour (0xdc15171d);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, a.getY(), bottom, 0.0f, a.getBottom(), false));
    g.fillRoundedRectangle (a, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (raised ? 0.07f : 0.05f), 0.0f, a.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f,
                                             a.getY() + juce::jmin (54.0f, a.getHeight() * 0.5f), false));
    g.fillRoundedRectangle (a, corner);
    if (tint.getAlpha() > 0 && ! reduced) // the accent's light falling on the glass from above
    {
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.13f), 0.0f, a.getY(), tint.withAlpha (0.0f), 0.0f, a.getY() + juce::jmin (140.0f, a.getHeight() * 0.8f), false));
        g.fillRoundedRectangle (a, corner);
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.0f), 0.0f, a.getBottom() - juce::jmin (60.0f, a.getHeight() * 0.4f), tint.withAlpha (0.06f), 0.0f, a.getBottom(), false));
        g.fillRoundedRectangle (a, corner);
    }
    // rim light (top) fading to a dark lip (bottom)
    juce::ColourGradient rim (tint.getAlpha() > 0 ? tint.interpolatedWith (juce::Colours::white, 0.55f).withAlpha (0.32f) : juce::Colours::white.withAlpha (0.20f), 0.0f, a.getY(), juce::Colours::white.withAlpha (0.02f), 0.0f, a.getY() + juce::jmin (40.0f, a.getHeight()), false);
    g.setGradientFill (rim);
    g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (a.expanded (0.5f), corner + 0.5f, 1.0f);
}

void sectionTitle (juce::Graphics& g, juce::Rectangle<float> strip, const juce::String& text, juce::Colour accent, float fontSize)
{
    if (currentStyle == Style::themed && artkit::sectionTitle (g, strip, text, accent, fontSize)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::sectionTitle (g, strip, text, accent, fontSize, currentStyle); return; }
    g.setFont (juce::Font (theme::font (fontSize, true)).withExtraKerningFactor (0.1f));
    const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
    const auto dotD = juce::jmax (4.0f, fontSize * 0.5f);
    const auto x0 = strip.getCentreX() - (w + dotD + 7.0f) * 0.5f;
    const juce::Point<float> dc { x0 + dotD * 0.5f, strip.getCentreY() };
    // an LED in the accent
    bloom (g, dc, dotD * 2.2f, accent, 0.55f);
    g.setGradientFill (juce::ColourGradient (accent.brighter (0.6f), dc.x, dc.y - dotD * 0.5f, accent, dc.x, dc.y + dotD * 0.5f, false));
    g.fillEllipse (juce::Rectangle<float> (dotD, dotD).withCentre (dc));
    g.setColour (juce::Colour (0xffdde0e7));
    g.drawText (text, juce::Rectangle<float> (x0 + dotD + 7.0f, strip.getY(), w + 6.0f, strip.getHeight()), juce::Justification::centredLeft, false);
    // lit hairlines to the edges, fading out
    const auto y = strip.getCentreY() - 0.5f;
    const auto l0 = strip.getX() + 8.0f, l1 = x0 - 7.0f, r0 = x0 + dotD + w + 15.0f, r1 = strip.getRight() - 8.0f;
    if (l1 > l0)
    {
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.0f), l0, 0.0f, accent.withAlpha (0.45f), l1, 0.0f, false));
        g.fillRect (juce::Rectangle<float> (l0, y, l1 - l0, 1.0f));
    }
    if (r1 > r0)
    {
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.45f), r0, 0.0f, accent.withAlpha (0.0f), r1, 0.0f, false));
        g.fillRect (juce::Rectangle<float> (r0, y, r1 - r0, 1.0f));
    }
}

void well (juce::Graphics& g, juce::Rectangle<float> a, float corner, juce::Colour glow)
{
    if (currentStyle == Style::themed && artkit::well (g, a, corner, glow)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::well (g, a, corner, glow, currentStyle); return; }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050608), 0.0f, a.getY(), juce::Colour (0xff0f1015), 0.0f, a.getBottom(), false));
    g.fillRoundedRectangle (a, corner);
    if (! reduced && glow.getAlpha() > 0)
    {
        // light rising from the bottom of the display
        g.saveState();
        g.reduceClipRegion (a.toNearestInt());
        juce::ColourGradient rise (glow.withAlpha (0.20f), a.getCentreX(), a.getBottom() + a.getHeight() * 0.1f, glow.withAlpha (0.0f), a.getCentreX() + a.getWidth() * 0.62f,
                                   a.getBottom(), true);
        rise.addColour (0.4, glow.withAlpha (0.07f));
        g.setGradientFill (rise);
        g.fillRect (a);
        g.restoreState();
    }
    // inner shadows: top and sides
    const auto sh = juce::jmin (10.0f, a.getHeight() * 0.3f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, a.getY(), juce::Colours::transparentBlack, 0.0f, a.getY() + sh, false));
    g.fillRoundedRectangle (a.withHeight (sh), corner);
    const auto sw = juce::jmin (6.0f, a.getWidth() * 0.1f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), a.getX(), 0.0f, juce::Colours::transparentBlack, a.getX() + sw, 0.0f, false));
    g.fillRect (a.withWidth (sw).reduced (0.0f, corner));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), a.getRight(), 0.0f, juce::Colours::transparentBlack, a.getRight() - sw, 0.0f, false));
    g.fillRect (a.withLeft (a.getRight() - sw).reduced (0.0f, corner));
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.0f);
    // the lip catching light at the bottom
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) a.getBottom(), a.getX() + corner, a.getRight() - corner);
}

void floatingPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edge, float corner)
{
    if (currentStyle == Style::themed && artkit::floatingPanel (g, r, edge, corner)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::floatingPanel (g, r, edge, corner, currentStyle); return; }
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 24, { 0, 8 }).drawForRectangle (g, r.toNearestInt());
    if (! reduced)
    {
        g.setColour (edge.withAlpha (0.10f));
        g.fillRoundedRectangle (r.expanded (4.0f), corner + 4.0f);
        g.setColour (edge.withAlpha (0.16f));
        g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xf72a2d36).interpolatedWith (edge, 0.07f), 0.0f, r.getY(), juce::Colour (0xf8141519), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f,
                                             r.getY() + juce::jmin (50.0f, r.getHeight() * 0.5f), false));
    g.fillRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f,
                                             r.getY() + juce::jmin (30.0f, r.getHeight()), false));
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.0f, 1.0f);
    g.setGradientFill (juce::ColourGradient (edge.brighter (0.2f), 0.0f, r.getY(), edge.withAlpha (0.45f), 0.0f, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.4f);
}

void bottomBar (juce::Graphics& g, juce::Rectangle<float> a)
{
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::bottomBar (g, a, currentStyle); return; }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f2128), 0.0f, a.getY(), juce::Colour (0xff111216), 0.0f, a.getBottom(), false));
    g.fillRect (a);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawHorizontalLine ((int) a.getY(), a.getX(), a.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) a.getY() + 1, a.getX(), a.getRight());
}

// --- controls ------------------------------------------------------------------------------------

namespace
{
void drawKnobDirect (juce::Graphics& g, juce::Rectangle<float> bounds, const KnobLook& k);
}

// Knobs are drawn once per look into a small cached image (value to 1/1000, hover in 8 steps, the screen's pixel
// scale) and blitted afterwards: a display editor repainting at 30 Hz re-uses its unchanged knobs for free. The
// bloom a big knob casts on the panel (brighter as it is turned up) is one gradient, drawn live.
juce::Colour capTintFor (const char* param)
{
    return currentStyle == Style::themed && param != nullptr ? artkit::capTintFor (param) : juce::Colour();
}

juce::Colour offLabelColour()
{
    if (currentStyle == Style::themed && artkit::onDarkFill) return juce::Colour (0xffd0d3d6); // round tp172d
    return currentStyle == Style::themed ? artkit::offLabelColour() : juce::Colour();
}

void knob (juce::Graphics& g, juce::Rectangle<float> boundsIn, const KnobLook& k)
{
    // Hardware and Vivid draw their knobs a little bigger than the cell's circle (the ring sits close to the caption)
    const auto grow = currentStyle == Style::vivid ? styles::knobScaleVivid : currentStyle == Style::hardware ? styles::knobScaleHardware
                    : currentStyle == Style::themed ? artkit::knobScale() : 1.0f;
    const auto bounds = boundsIn.withSizeKeepingCentre (boundsIn.getWidth() * grow, boundsIn.getHeight() * grow);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    if (size <= 4.0f) return;
    if (size >= 52.0f && k.enabled && (currentStyle == Style::glow || currentStyle == Style::neon))
    {
        const auto v = juce::jlimit (0.0f, 1.0f, k.bipolar ? std::abs (k.value - 0.5f) * 2.0f : k.value);
        bloom (g, bounds.getCentre(), size * 0.8f, k.accent, 0.05f + 0.11f * v + 0.06f * juce::jmax (k.hover, k.active ? 1.0f : 0.0f));
    }
    const auto scale = pixelScale (g);
    const auto pad = size * 0.16f + 3.0f;
    const auto area = bounds.withSizeKeepingCentre (size, size).expanded (pad);
    const auto px = (int) std::ceil (area.getWidth() * scale);
    if (px > 420) { drawKnobDirect (g, bounds, k); return; }

    std::uint64_t key = (std::uint64_t) juce::jlimit (0, 1000, (int) std::lround (k.value * 1000.0f));
    key = key * 1009u + (std::uint64_t) std::lround (size * 4.0f);
    key = key * 9u + (std::uint64_t) std::lround (juce::jlimit (0.0f, 1.0f, k.hover) * 8.0f);
    key = key * 16u + (k.bipolar ? 1u : 0u) + (k.active ? 2u : 0u) + (k.enabled ? 4u : 0u) + (reduced ? 8u : 0u);
    key = key * 7u + (std::uint64_t) currentStyle;
    key = key * 11u + (std::uint64_t) currentArt;
    key = key * 401u + (std::uint64_t) std::lround (scale * 100.0f);
    key ^= (std::uint64_t) k.accent.getARGB() * 0x9e3779b97f4a7c15ull;
    key ^= (std::uint64_t) k.capTint.getARGB() * 0xc2b2ae3d27d4eb4full;

    static std::unordered_map<std::uint64_t, juce::Image> cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.clear(); }); (void) registered;
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 1500) cache.clear();
        juce::Image img (juce::Image::ARGB, px, px, true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale ((float) px / area.getWidth()));
            drawKnobDirect (ig, juce::Rectangle<float> (pad, pad, size, size).expanded (1.0f), k);
        }
        it = cache.emplace (key, img).first;
    }
    g.setOpacity (1.0f); // (drawImage uses the current colour's alpha)
    g.drawImage (it->second, area, juce::RectanglePlacement::stretchToFit);
}

namespace
{
void drawKnobDirect (juce::Graphics& g, juce::Rectangle<float> bounds, const KnobLook& k)
{
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    if (size <= 4.0f) return;
    const auto area = bounds.withSizeKeepingCentre (size, size);
    if (currentStyle == Style::themed) { artkit::knob (g, area, k); return; }
    if (currentStyle == Style::neon) { styles::knobNeon (g, area, k); return; }
    if (currentStyle == Style::hardware) { styles::knobHardware (g, area, k); return; }
    if (currentStyle == Style::vivid) { styles::knobVivid (g, area, k); return; }
    const auto c = area.getCentre();
    const auto small = size < 26.0f;
    const auto fx = ! small && ! reduced;
    const auto thickness = juce::jlimit (2.0f, 7.0f, size * 0.085f);
    const auto arcR = size * 0.5f - thickness * (small ? 0.6f : 0.85f);
    const auto value = juce::jlimit (0.0f, 1.0f, k.value);
    const auto accent = k.enabled ? (k.active ? k.accent.brighter (0.3f) : k.accent) : theme::colour::textDisabled;
    const auto hover = juce::jlimit (0.0f, 1.0f, k.active ? 1.0f : k.hover);

    // dragging: a halo of the accent around the whole knob
    if (k.active && fx)
    {
        juce::ColourGradient halo (accent.withAlpha (0.0f), c.x, c.y, accent.withAlpha (0.0f), c.x + size * 0.66f, c.y, true);
        halo.addColour (0.62, accent.withAlpha (0.0f));
        halo.addColour (0.76, accent.withAlpha (0.26f));
        g.setGradientFill (halo);
        g.fillEllipse (area.expanded (size * 0.16f));
    }

    // the track: a dark groove with the unlit ring (and, on big knobs, dim LED ticks along it)
    const auto track = arcPath (c, arcR, arcStart, arcStart + arcRange);
    g.setColour (juce::Colour (0xff050607));
    g.strokePath (track, roundStroke (thickness + 3.0f));
    g.setColour (juce::Colour (0xff23262e).interpolatedWith (juce::Colour (0xff3a3f4b), hover * 0.7f));
    g.strokePath (track, roundStroke (thickness));
    if (size >= 56.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.16f + 0.1f * hover));
        const auto d = thickness * 0.34f;
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = arcStart + arcRange * (float) i / 10.0f;
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ c.x + arcR * std::sin (a), c.y - arcR * std::cos (a) }));
        }
    }

    // the lit value arc: a wide glow, the gradient core, a specular line and a hot tip
    const auto from = k.bipolar ? arcStart + arcRange * 0.5f : arcStart;
    const auto to = arcStart + arcRange * value;
    if (std::abs (to - from) > 0.002f)
    {
        const auto arc = arcPath (c, arcR, from, to);
        if (k.enabled && fx)
        {
            g.setColour (accent.withAlpha (0.09f + 0.08f * hover));
            g.strokePath (arc, roundStroke (thickness * 3.6f));
            g.setColour (accent.withAlpha (0.20f + 0.12f * hover));
            g.strokePath (arc, roundStroke (thickness * 2.0f));
        }
        g.setGradientFill (juce::ColourGradient (accent.darker (0.35f), area.getX(), area.getBottom(), accent.brighter (0.35f), area.getRight(), area.getY(), false));
        g.strokePath (arc, roundStroke (thickness));
        if (! small)
        {
            g.setColour (juce::Colours::white.withAlpha (k.enabled ? 0.38f : 0.12f));
            g.strokePath (arc, roundStroke (thickness * 0.28f));
        }
    }
    if (k.enabled && fx && std::abs (to - from) > 0.02f) // the tip of the arc burns white-hot
    {
        const juce::Point<float> tip { c.x + arcR * std::sin (to), c.y - arcR * std::cos (to) };
        juce::ColourGradient hot (juce::Colours::white.withAlpha (0.95f), tip.x, tip.y, accent.withAlpha (0.0f), tip.x + thickness * 1.9f, tip.y, true);
        hot.addColour (0.3, accent.brighter (0.5f).withAlpha (0.8f));
        g.setGradientFill (hot);
        g.fillEllipse (juce::Rectangle<float> (thickness * 3.8f, thickness * 3.8f).withCentre (tip));
    }
    if (k.bipolar) // the centre mark
    {
        g.setColour (k.enabled ? juce::Colours::white.withAlpha (0.8f) : theme::colour::textSecondary);
        const auto m = juce::jmax (2.0f, thickness * 0.5f);
        g.fillEllipse (juce::Rectangle<float> (m, m).withCentre ({ c.x, c.y - arcR - thickness * (small ? 0.9f : 1.25f) }));
    }

    // the cap: contact shadow, a turned metal side wall, a face lit from the upper left, a specular highlight and
    // a rim that catches the accent's light from below
    const auto capR = juce::jmax (2.0f, arcR - thickness * (small ? 1.3f : 1.25f));
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    if (! small)
    {
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.75f), c.x, c.y + capR * 0.22f, juce::Colours::transparentBlack, c.x + capR * 1.3f,
                                     c.y + capR * 0.22f, true);
        shadow.addColour (0.7, juce::Colours::black.withAlpha (0.4f));
        g.setGradientFill (shadow);
        g.fillEllipse (cap.expanded (capR * 0.3f).translated (0.0f, capR * 0.16f));
    }
    const auto lift = hover * 0.1f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a606d).brighter (lift), c.x, cap.getY(), juce::Colour (0xff121317), c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (capR > 8.0f)
    {
        const auto face = cap.reduced (capR * 0.11f);
        juce::ColourGradient faceGrad (juce::Colour (0xff434854).brighter (lift), c.x - capR * 0.35f, c.y - capR * 0.45f, juce::Colour (0xff1a1c22), c.x + capR * 0.9f,
                                       c.y + capR * 0.9f, true);
        faceGrad.addColour (0.5, juce::Colour (0xff2b2e36).brighter (lift));
        g.setGradientFill (faceGrad);
        g.fillEllipse (face);
        if (fx && capR > 14.0f) // fine concentric machining
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            for (auto rr = capR * 0.25f; rr < capR * 0.85f; rr += juce::jmax (1.6f, capR * 0.07f))
                g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.7f);
        }
        if (fx) // specular highlight
        {
            juce::ColourGradient spec (juce::Colours::white.withAlpha (0.22f + 0.08f * hover), c.x - capR * 0.28f, c.y - capR * 0.5f, juce::Colours::white.withAlpha (0.0f),
                                       c.x - capR * 0.28f + capR * 0.62f, c.y - capR * 0.5f, true);
            g.setGradientFill (spec);
            g.fillEllipse (face);
        }
    }
    // rim: white along the top, the accent's reflection along the bottom
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.32f), c.x, cap.getY(),
                                             k.enabled && fx ? accent.withAlpha (0.25f + 0.35f * value) : juce::Colours::black.withAlpha (0.5f), c.x, cap.getBottom(), false));
    g.drawEllipse (cap.reduced (0.5f), small ? 1.0f : 1.3f);

    // the pointer: an LED line in the accent with a white core and a lit dot at its end
    const juce::Point<float> dir { std::sin (to), -std::cos (to) };
    const auto inner = capR * (capR > 8.0f ? 0.32f : 0.1f);
    const auto outer = capR * 0.8f;
    const auto pw = juce::jlimit (1.5f, 3.8f, size * 0.055f);
    juce::Path pointer;
    pointer.startNewSubPath (c + dir * inner);
    pointer.lineTo (c + dir * outer);
    if (k.enabled && fx)
    {
        g.setColour (accent.withAlpha (0.35f + 0.3f * hover));
        g.strokePath (pointer, roundStroke (pw * 3.0f));
        g.setColour (accent.brighter (0.2f));
        g.strokePath (pointer, roundStroke (pw * 1.5f));
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.strokePath (pointer, roundStroke (pw * 0.7f));
        const auto end = c + dir * outer;
        juce::ColourGradient dot (juce::Colours::white, end.x, end.y, accent.withAlpha (0.0f), end.x + pw * 2.6f, end.y, true);
        dot.addColour (0.35, accent.brighter (0.4f).withAlpha (0.9f));
        g.setGradientFill (dot);
        g.fillEllipse (juce::Rectangle<float> (pw * 5.2f, pw * 5.2f).withCentre (end));
    }
    else
    {
        g.setColour (k.enabled ? (k.active ? accent.brighter (0.6f) : theme::colour::textPrimary) : theme::colour::textSecondary);
        g.strokePath (pointer, roundStroke (pw));
    }
}
} // namespace

void slider (juce::Graphics& g, juce::Rectangle<float> t, float value01, bool vertical, bool bipolar, juce::Colour accent, bool hot, bool enabled,
             float thumbLength, float thumbThickness)
{
    if (currentStyle == Style::themed && artkit::slider (g, t, value01, vertical, bipolar, accent, hot, enabled, thumbLength, thumbThickness)) return;
    const BaseScope base;
    if (currentStyle != Style::glow)
    {
        styles::slider (g, t, juce::jlimit (0.0f, 1.0f, value01), vertical, bipolar, accent, hot, enabled, thumbLength, thumbThickness, currentStyle);
        return;
    }
    const auto v = juce::jlimit (0.0f, 1.0f, value01);
    const auto thick = vertical ? t.getWidth() : t.getHeight();
    g.setColour (juce::Colour (0xff040506));
    g.fillRoundedRectangle (t.expanded (1.5f), thick * 0.5f + 1.5f);
    g.setColour (juce::Colour (0xff1c1e24));
    g.fillRoundedRectangle (t, thick * 0.5f);
    juce::Rectangle<float> fill;
    juce::Point<float> thumb;
    if (vertical)
    {
        const auto y = t.getBottom() - t.getHeight() * v;
        const auto from = bipolar ? t.getCentreY() : t.getBottom();
        fill = { t.getX(), juce::jmin (y, from), t.getWidth(), std::abs (from - y) };
        thumb = { t.getCentreX(), y };
    }
    else
    {
        const auto x = t.getX() + t.getWidth() * v;
        const auto from = bipolar ? t.getCentreX() : t.getX();
        fill = { juce::jmin (x, from), t.getY(), std::abs (from - x), t.getHeight() };
        thumb = { x, t.getCentreY() };
    }
    const auto c = enabled ? accent : theme::colour::textDisabled;
    if (enabled && ! fill.isEmpty() && ! reduced)
    {
        g.setColour (c.withAlpha (hot ? 0.16f : 0.10f));
        g.fillRoundedRectangle (fill.expanded (thick * 1.6f), thick * 1.6f);
        g.setColour (c.withAlpha (hot ? 0.34f : 0.22f));
        g.fillRoundedRectangle (fill.expanded (thick * 0.7f), thick);
    }
    g.setGradientFill (vertical ? juce::ColourGradient (c.brighter (0.35f), 0.0f, fill.getY(), c.darker (0.35f), 0.0f, fill.getBottom(), false)
                                : juce::ColourGradient (c.darker (0.35f), fill.getX(), 0.0f, c.brighter (0.35f), fill.getRight(), 0.0f, false));
    g.fillRoundedRectangle (fill, thick * 0.5f);
    if (enabled && ! fill.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        if (vertical) g.fillRect (fill.withWidth (juce::jmax (1.0f, thick * 0.3f)).withX (fill.getCentreX() - thick * 0.15f));
        else g.fillRect (fill.withHeight (juce::jmax (1.0f, thick * 0.3f)).withY (fill.getCentreY() - thick * 0.15f));
    }

    // the thumb: a raised metal cap carrying an LED line in the accent
    const auto tr = vertical ? juce::Rectangle<float> (thumbLength, thumbThickness).withCentre (thumb)
                             : juce::Rectangle<float> (thumbThickness, thumbLength).withCentre (thumb);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (tr.translated (0.0f, 2.0f).expanded (0.5f), 3.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff666c79).brighter (hot ? 0.15f : 0.0f), 0.0f, tr.getY(), juce::Colour (0xff23262d), 0.0f,
                                             tr.getBottom(), false));
    g.fillRoundedRectangle (tr, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawHorizontalLine ((int) tr.getY() + 1, tr.getX() + 2.0f, tr.getRight() - 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (tr.reduced (0.5f), 3.0f, 1.0f);
    const auto led = vertical ? juce::Rectangle<float> (tr.getX() + 3.0f, thumb.y - 1.0f, tr.getWidth() - 6.0f, 2.0f)
                              : juce::Rectangle<float> (thumb.x - 1.0f, tr.getY() + 3.0f, 2.0f, tr.getHeight() - 6.0f);
    if (enabled && ! reduced)
    {
        g.setColour (c.withAlpha (hot ? 0.6f : 0.4f));
        g.fillRoundedRectangle (led.expanded (1.5f), 2.0f);
    }
    g.setColour (enabled ? (hot ? c.brighter (0.6f) : c.brighter (0.3f)) : theme::colour::textSecondary);
    g.fillRoundedRectangle (led, 1.0f);
}

void button (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool hover, juce::Colour accent, float corner)
{
    if (currentStyle == Style::themed && artkit::button (g, r, lit, hover, accent, corner)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::button (g, r, lit, hover, accent, corner, currentStyle); return; }
    if (lit)
    {
        // illuminated: the light spills out around the button
        if (! reduced)
        {
            g.setColour (accent.withAlpha (hover ? 0.14f : 0.09f));
            g.fillRoundedRectangle (r.expanded (4.0f), corner + 4.0f);
            g.setColour (accent.withAlpha (hover ? 0.24f : 0.16f));
            g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
        }
        g.setColour (juce::Colour (0xff15161b));
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.78f), 0.0f, r.getY(), accent.darker (0.3f).withAlpha (0.55f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY(), false));
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.5f), corner);
        g.setColour (accent.brighter (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        return;
    }
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f), corner);
    g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xff454a56 : 0xff3a3e48), 0.0f, r.getY(), juce::Colour (hover ? 0xff2a2d35 : 0xff222429), 0.0f,
                                             r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (hover ? 0.18f : 0.11f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);
    if (hover)
    {
        g.setColour (accent.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }
}

void switchPill (juce::Graphics& g, juce::Rectangle<float> box, bool on, juce::Colour accent, bool enabled)
{
    if (currentStyle == Style::themed && artkit::switchPill (g, box, on, accent, enabled)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::switchPill (g, box, on, accent, enabled, currentStyle); return; }
    const auto c = enabled ? accent : theme::colour::textSecondary;
    const auto rr = box.getHeight() * 0.5f;
    if (on)
    {
        if (! reduced)
        {
            g.setColour (c.withAlpha (0.16f));
            g.fillRoundedRectangle (box.expanded (3.5f), rr + 3.5f);
            g.setColour (c.withAlpha (0.3f));
            g.fillRoundedRectangle (box.expanded (1.5f), rr + 1.5f);
        }
        g.setGradientFill (juce::ColourGradient (c.brighter (0.3f), 0.0f, box.getY(), c.darker (0.25f), 0.0f, box.getBottom(), false));
    }
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050607), 0.0f, box.getY(), juce::Colour (0xff15171c), 0.0f, box.getBottom(), false));
    g.fillRoundedRectangle (box, rr);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (box.reduced (0.5f), rr, 1.0f);
    const auto d = box.getHeight() - 2.0f;
    const auto knobR = juce::Rectangle<float> (d, d).withCentre ({ on ? box.getRight() - d * 0.5f - 1.0f : box.getX() + d * 0.5f + 1.0f, box.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (knobR.translated (0.0f, 1.0f));
    g.setGradientFill (juce::ColourGradient (on ? juce::Colours::white : juce::Colour (0xff7a808c), 0.0f, knobR.getY(), on ? juce::Colour (0xffd3d6dd) : juce::Colour (0xff42464f),
                                             0.0f, knobR.getBottom(), false));
    g.fillEllipse (knobR);
}

void selector (juce::Graphics& g, juce::Rectangle<float> r, bool hover, float corner)
{
    if (currentStyle == Style::themed && artkit::selector (g, r, hover, corner)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::selector (g, r, hover, corner, currentStyle); return; }
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.5f), corner);
    g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xff464b57 : 0xff393d47), 0.0f, r.getY(), juce::Colour (hover ? 0xff2b2e36 : 0xff23252c), 0.0f,
                                             r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (hover ? 0.2f : 0.12f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);
}

namespace
{
// the ladder of LED segments over a meter (gaps every 3 px)
void segmentGaps (juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> upTo, bool vertical)
{
    g.setColour (juce::Colour (0xff060708).withAlpha (0.75f));
    if (vertical)
        for (auto y = r.getBottom() - 3.0f; y > upTo.getY(); y -= 3.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    else
        for (auto x = r.getX() + 3.0f; x < upTo.getRight(); x += 3.0f) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
}
} // namespace

void meter (juce::Graphics& g, juce::Rectangle<float> r, float level01, float peak01, bool vertical)
{
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::meter (g, r, juce::jlimit (0.0f, 1.0f, level01), juce::jlimit (0.0f, 1.0f, peak01), vertical, currentStyle); return; }
    g.setColour (juce::Colour (0xff040506));
    g.fillRoundedRectangle (r.expanded (1.0f), 2.5f);
    const auto lv = juce::jlimit (0.0f, 1.0f, level01);
    auto gradient = [&] (float alpha) {
        auto gr = vertical ? juce::ColourGradient (theme::colour::meterLow.withAlpha (alpha), 0.0f, r.getBottom(), theme::colour::meterClip.withAlpha (alpha), 0.0f, r.getY(), false)
                           : juce::ColourGradient (theme::colour::meterLow.withAlpha (alpha), r.getX(), 0.0f, theme::colour::meterClip.withAlpha (alpha), r.getRight(), 0.0f, false);
        gr.addColour (0.72, theme::colour::meterMid.withAlpha (alpha));
        return gr;
    };
    // the unlit LEDs: the whole ladder, faint
    g.setGradientFill (gradient (0.10f));
    g.fillRoundedRectangle (r, 2.0f);
    if (lv > 0.0f)
    {
        const auto lit = vertical ? r.withTop (r.getBottom() - r.getHeight() * lv) : r.withWidth (r.getWidth() * lv);
        if (! reduced)
        {
            g.setGradientFill (gradient (0.22f));
            g.fillRoundedRectangle (vertical ? lit.expanded (2.0f, 0.0f) : lit.expanded (0.0f, 2.0f), 3.0f);
        }
        g.setGradientFill (gradient (1.0f));
        g.fillRoundedRectangle (lit, 2.0f);
        // the hot edge
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        if (vertical) g.fillRect (lit.withHeight (1.0f));
        else g.fillRect (lit.withLeft (lit.getRight() - 1.0f));
    }
    segmentGaps (g, r, r, vertical);
    // glass
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    if (vertical) g.fillRect (r.withWidth (r.getWidth() * 0.4f));
    else g.fillRect (r.withHeight (r.getHeight() * 0.4f));
    const auto pk = juce::jlimit (0.0f, 1.0f, peak01);
    if (pk > 0.01f)
    {
        const auto c = pk > 0.97f ? theme::colour::meterClip : juce::Colours::white;
        const auto tick = vertical ? juce::Rectangle<float> (r.getX(), r.getBottom() - r.getHeight() * pk - 1.0f, r.getWidth(), 2.0f)
                                   : juce::Rectangle<float> (r.getX() + r.getWidth() * pk - 1.0f, r.getY(), 2.0f, r.getHeight());
        if (! reduced)
        {
            g.setColour (c.withAlpha (0.3f));
            g.fillRect (tick.expanded (1.5f));
        }
        g.setColour (c.withAlpha (0.95f));
        g.fillRect (tick);
    }
}

void meterSolid (juce::Graphics& g, juce::Rectangle<float> r, float level01, juce::Colour c, bool fromTop)
{
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::meterSolid (g, r, juce::jlimit (0.0f, 1.0f, level01), c, fromTop, currentStyle); return; }
    g.setColour (juce::Colour (0xff040506));
    g.fillRoundedRectangle (r.expanded (1.0f), 2.5f);
    g.setColour (c.withAlpha (0.08f));
    g.fillRoundedRectangle (r, 2.0f);
    const auto lv = juce::jlimit (0.0f, 1.0f, level01);
    if (lv > 0.0f)
    {
        const auto lit = fromTop ? r.withHeight (r.getHeight() * lv) : r.withTop (r.getBottom() - r.getHeight() * lv);
        if (! reduced)
        {
            g.setColour (c.withAlpha (0.25f));
            g.fillRoundedRectangle (lit.expanded (2.0f, 0.0f), 3.0f);
        }
        g.setGradientFill (juce::ColourGradient (c.brighter (0.35f), 0.0f, lit.getY(), c.darker (0.3f), 0.0f, lit.getBottom(), false));
        g.fillRoundedRectangle (lit, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillRect (fromTop ? lit.withTop (lit.getBottom() - 1.0f) : lit.withHeight (1.0f));
    }
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (r.withWidth (r.getWidth() * 0.4f));
}

void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness, float glow)
{
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::glowStroke (g, p, c, thickness, glow, currentStyle); return; }
    if (glow > 0.0f && ! reduced)
    {
        g.setColour (c.withAlpha (0.07f * glow));
        g.strokePath (p, roundStroke (thickness * 6.0f));
        g.setColour (c.withAlpha (0.18f * glow));
        g.strokePath (p, roundStroke (thickness * 2.8f));
    }
    g.setColour (c);
    g.strokePath (p, roundStroke (thickness));
    if (glow > 0.0f && thickness >= 1.4f && ! reduced) // a white-hot core
    {
        g.setColour (c.brighter (0.9f).withAlpha (0.75f * glow));
        g.strokePath (p, roundStroke (thickness * 0.4f));
    }
}

void valuePlate (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    if (currentStyle == Style::themed && artkit::valuePlate (g, r, corner)) return;
    const BaseScope base;
    if (currentStyle != Style::glow) { styles::valuePlate (g, r, corner, currentStyle); return; }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff040507), 0.0f, r.getY(), juce::Colour (0xff101116), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    const auto sh = juce::jmin (5.0f, r.getHeight() * 0.35f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getY() + sh, false));
    g.fillRoundedRectangle (r.withHeight (sh), corner);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + corner, r.getRight() - corner);
}
} // namespace lynx::skin
