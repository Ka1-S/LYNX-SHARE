// The graphs of round 168r's designed effect editors (core/PluginLayout168r.inc), included by ui/PluginEditors.cpp:
// - "fxresponse": the effect's measured magnitude response (core/EffectResponse.h: an impulse through a fresh instance
//   of the slot's engine with the slot's settings), for the linear filters and EQs;
// - "fxtransfer": the effect's measured output over one cycle of a full-scale sine against its input (shapers);
// - "spreadfield", "fshift", "lowlifter", "delaytaps", "reverbspace", "chorusvoices", "utilitymatrix", "rhythmcurve",
//   "pitchshift": pictures of the settings;
// - "peakmeter": the Peak Controller's three outputs scrolling (live).

#include <map>
#include <mutex>
#include <optional>

#include "core/DistructorFx.h"
#include "core/EffectResponse.h"

namespace lynx::designui
{
struct History
{
    static constexpr int length = 240; // 8 s at 30 Hz
    std::array<std::array<float, 3>, length> values {};
    int pos = 0, count = 0;
    void push (float a, float b, float c) noexcept
    {
        values[(std::size_t) pos] = { a, b, c };
        pos = (pos + 1) % length;
        count = count < length ? count + 1 : length;
    }
};

inline bool handles (const char* id) noexcept
{
    if (id == nullptr)
        return false;
    for (const auto* d : { "fxresponse", "fxtransfer", "spreadfield", "fshift", "lowlifter", "peakmeter", "delaytaps", "reverbspace",
                           "chorusvoices", "utilitymatrix", "rhythmcurve", "pitchshift", "distructormodule" })
        if (std::strcmp (id, d) == 0)
            return true;
    return false;
}

inline bool isLive (const char* id) noexcept { return id != nullptr && std::strcmp (id, "peakmeter") == 0; }

namespace detail
{
using namespace theme;

inline float freqX (double hz, juce::Rectangle<float> a)
{
    return a.getX() + a.getWidth() * (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0));
}

inline void freqGrid (juce::Graphics& g, juce::Rectangle<float> a, float s, bool labels)
{
    g.setColour (colour::gridBeat);
    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        g.drawVerticalLine ((int) freqX (hz, a), a.getY(), a.getBottom());
    if (! labels)
        return;
    g.setColour (colour::textDisabled);
    g.setFont (font (juce::jmax (7.5f, 8.5f * s)));
    for (const auto hz : { 100.0, 1000.0, 10000.0 })
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (28.0f * s, 11.0f * s).withCentre ({ freqX (hz, a), a.getBottom() - 6.0f * s }).toNearestInt(),
                    juce::Justification::centred, false);
}

inline void stroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float s, bool enabled)
{
    skin::glowStroke (g, p, c, juce::jmax (1.5f, 2.0f * s), enabled ? 1.0f : 0.0f);
}

inline void caption (juce::Graphics& g, juce::Rectangle<float> a, float s, const juce::String& text, juce::Justification j = juce::Justification::topLeft)
{
    g.setColour (colour::textSecondary);
    g.setFont (font (juce::jmax (8.0f, 9.5f * s), true));
    g.drawFittedText (text, a.reduced (4.0f * s, 2.0f * s).toNearestInt(), j, 2, 0.7f);
}

// the response of a setting, measured once per setting (a few ms) and kept for the next paints
inline const std::vector<float>& cachedResponse (const EffectSlot& fx, const std::vector<double>& hz)
{
    static std::mutex lock;
    static std::map<std::vector<float>, std::vector<float>> cache;
    std::vector<float> key { (float) fx.kind };
    key.insert (key.end(), fx.params.begin(), fx.params.end());
    const std::lock_guard<std::mutex> guard (lock);
    if (auto it = cache.find (key); it != cache.end())
        return it->second;
    if (cache.size() > 64)
        cache.clear();
    return cache.emplace (key, fxresponse::responseDb (fx, hz)).first->second;
}

inline const std::vector<float>& cachedTransfer (const EffectSlot& fx)
{
    static std::mutex lock;
    static std::map<std::vector<float>, std::vector<float>> cache;
    std::vector<float> key { (float) fx.kind };
    key.insert (key.end(), fx.params.begin(), fx.params.end());
    const std::lock_guard<std::mutex> guard (lock);
    if (auto it = cache.find (key); it != cache.end())
        return it->second;
    if (cache.size() > 64)
        cache.clear();
    return cache.emplace (key, fxresponse::transferCycle (fx, 256)).first->second;
}

inline float p01 (const EffectSlot& fx, const char* name)
{
    const auto i = plugui::paramIndex (fx.kind, name);
    return i >= 0 && i < (int) fx.params.size() ? fx.params[(std::size_t) i] : 0.0f;
}

// a parameter's displayed number ("250 ms", "+3.0 dB" → 250, 3) through the engine's own text
inline double shownNumber (const EffectSlot& fx, const char* name, double bpm = 120.0)
{
    const auto i = plugui::paramIndex (fx.kind, name);
    if (i < 0)
        return 0.0;
    const auto text = formatEffectParam (fx.kind, i, fx.params[(std::size_t) i], bpm);
    const auto* c = text.c_str();
    while (*c != 0 && ! ((*c >= '0' && *c <= '9') || *c == '-' || *c == '+' || *c == '.'))
        ++c;
    return std::atof (c);
}

inline void paintResponse (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    static const std::vector<double> hz = [] {
        std::vector<double> f;
        for (int i = 0; i <= 120; ++i)
            f.push_back (20.0 * std::pow (1000.0, i / 120.0));
        return f;
    }();
    const auto& db = cachedResponse (fx, hz);
    // range: ±12 dB, or ±24 dB when the curve goes further
    float extreme = 0.0f;
    for (const auto v : db)
        extreme = juce::jmax (extreme, std::abs (v));
    const auto range = extreme > 12.5f ? 24.0f : 12.0f;
    freqGrid (g, a, s, true);
    g.setColour (colour::gridBar);
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
    g.setColour (colour::textDisabled);
    g.setFont (font (juce::jmax (7.5f, 8.5f * s)));
    g.drawText ("+" + juce::String ((int) range) + " dB", a.withHeight (11.0f * s).reduced (3.0f * s, 0).toNearestInt(), juce::Justification::left, false);
    g.drawText ("-" + juce::String ((int) range) + " dB", a.withTop (a.getBottom() - 22.0f * s).withHeight (11.0f * s).reduced (3.0f * s, 0).toNearestInt(),
                juce::Justification::left, false);
    if (db.size() != hz.size())
        return;
    juce::Path p, fill;
    for (std::size_t i = 0; i < hz.size(); ++i)
    {
        const juce::Point<float> pt { freqX (hz[i], a), a.getCentreY() - a.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db[i] / range) };
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }
    fill = p;
    fill.lineTo (a.getRight(), a.getCentreY());
    fill.lineTo (a.getX(), a.getCentreY());
    fill.closeSubPath();
    g.setColour (c.withAlpha (0.14f));
    g.fillPath (fill);
    stroke (g, p, c, s, fx.enabled);
}

inline void paintTransfer (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const auto& y = cachedTransfer (fx);
    g.setColour (colour::gridBeat);
    g.drawVerticalLine ((int) a.getCentreX(), a.getY(), a.getBottom());
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
    g.setColour (colour::gridBar);
    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f); // unity
    if (y.empty())
        return;
    // the first quarter of the cycle rises from 0 to +1, the third falls to −1: plot the output against the input sine
    juce::Path p;
    const auto n = (int) y.size();
    bool first = true;
    for (int i = -n / 4; i <= n / 4; ++i)
    {
        const auto k = (i + n) % n;
        const auto in = std::sin (6.283185307179586 * (double) k / n);
        const juce::Point<float> pt { a.getCentreX() + a.getWidth() * 0.5f * (float) in, a.getCentreY() - a.getHeight() * 0.5f * juce::jlimit (-1.2f, 1.2f, y[(std::size_t) k]) };
        if (first)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
        first = false;
    }
    stroke (g, p, c, s, fx.enabled);
    caption (g, a, s, tr ("In") + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + tr ("Out"), juce::Justification::bottomRight);
}

// Spreader: the stereo image — the source's left / right as two arcs spread by Width, Separation pushing the halves
// apart, the bass under Low bypass kept in the middle (or mono)
inline void paintSpread (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const auto width = p01 (fx, "Width"), sep = p01 (fx, "Separation") * 2.0f - 1.0f;
    const auto on = p01 (fx, "Enable") >= 0.5f;
    const auto centre = juce::Point<float> (a.getCentreX(), a.getBottom() - 14.0f * s);
    const auto radius = juce::jmin (a.getWidth() * 0.46f, a.getHeight() - 30.0f * s);
    g.setColour (colour::gridBeat);
    for (int k = 1; k <= 3; ++k)
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f * (float) k / 3.0f, radius * 2.0f * (float) k / 3.0f).withCentre (centre), 1.0f);
    g.setColour (colour::gridBar);
    g.drawLine (centre.x, centre.y, centre.x, centre.y - radius, 1.0f);
    g.drawLine (centre.x, centre.y, centre.x - radius * 0.707f, centre.y - radius * 0.707f, 1.0f);
    g.drawLine (centre.x, centre.y, centre.x + radius * 0.707f, centre.y - radius * 0.707f, 1.0f);
    g.setColour (colour::textDisabled);
    g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
    g.drawText ("L", juce::Rectangle<float> (16.0f * s, 14.0f * s).withCentre ({ centre.x - radius * 0.76f, centre.y - radius * 0.76f }).toNearestInt(), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> (16.0f * s, 14.0f * s).withCentre ({ centre.x + radius * 0.76f, centre.y - radius * 0.76f }).toNearestInt(), juce::Justification::centred, false);
    // the image: an arc from −spread to +spread (spread 0 … 90°), the halves pushed out by separation
    const auto spread = on ? juce::jlimit (0.0f, 1.0f, width) * juce::MathConstants<float>::halfPi : 0.785f;
    const auto push = on ? sep * 0.35f : 0.0f;
    auto arc = [&] (float from, float to, float r) {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, r, r, 0.0f, from, to, true);
        return p;
    };
    for (int k = 0; k < 6; ++k)
    {
        const auto r = radius * (0.35f + 0.11f * (float) k);
        const auto lean = push * (1.0f - 0.12f * (float) k);
        stroke (g, arc (-spread - lean, -0.04f - lean * 0.5f, r), c.withAlpha (0.35f + 0.1f * (float) k), s * 0.8f, fx.enabled);
        stroke (g, arc (0.04f + lean * 0.5f, spread + lean, r), c.withAlpha (0.35f + 0.1f * (float) k), s * 0.8f, fx.enabled);
    }
    // the low end: centred when Mono is on
    const auto mono = p01 (fx, "Mono bass") >= 0.5f;
    g.setColour ((mono ? colour::info : colour::textDisabled).withAlpha (fx.enabled ? 0.9f : 0.4f));
    g.fillEllipse (juce::Rectangle<float> (12.0f * s, 12.0f * s).withCentre (centre.translated (0.0f, -radius * 0.18f)));
    caption (g, a, s, tr ("Stereo image") + "   " + tr ("Width") + " " + juce::String (juce::roundToInt (width * 100.0f)) + " %   "
                          + tr ("Separation") + " " + juce::String (juce::roundToInt (sep * 100.0f)) + " %"
                          + (mono ? "   " + tr ("Bass in mono") : juce::String()));
}

// Frequency Shifter: partials of a harmonic input (grey) and where they land (accent; both sidebands for ring modulation)
inline void paintShift (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    // FL's Frequency Shifter (Type, Range, Frequency) or LYNX's (Shift, read through its displayed Hz)
    const auto lynxOwn = fx.kind == EffectKind::freqShift;
    const auto type = lynxOwn ? 0.0f : p01 (fx, "Type"), range = lynxOwn ? 0.0f : p01 (fx, "Range");
    const auto x = 2.0f * p01 (fx, "Frequency") - 1.0f;
    const auto top = range < 1.0f / 3.0f ? 20000.0 : 200.0;
    const auto hz = lynxOwn ? shownNumber (fx, "Shift")
                            : range >= 2.0f / 3.0f ? 0.0 : (x < 0 ? -1.0 : 1.0) * top * std::pow (std::abs ((double) x), 2.0);
    g.setColour (colour::gridBeat);
    g.drawHorizontalLine ((int) (a.getBottom() - 1.0f), a.getX(), a.getRight());
    auto line = [&] (double f, float h, juce::Colour col) {
        if (f <= 20.0 || f >= 20000.0)
            return;
        const auto px = freqX (f, a);
        g.setColour (col);
        g.fillRect (juce::Rectangle<float> (juce::jmax (1.5f, 2.0f * s), a.getHeight() * h).withPosition (px - 1.0f, a.getBottom() - a.getHeight() * h));
    };
    for (int k = 1; k <= 8; ++k)
    {
        const auto f = 220.0 * k;
        const auto h = 0.85f / (float) k + 0.1f;
        line (f, h, colour::textDisabled.withAlpha (0.6f));
        line (f + hz, h * 0.9f, c.withAlpha (fx.enabled ? 1.0f : 0.4f));
        if (type >= 2.0f / 3.0f)
            line (f - hz, h * 0.9f, c.withAlpha (fx.enabled ? 0.7f : 0.3f));
    }
    caption (g, a, s, range >= 2.0f / 3.0f ? tr ("Tempo") : (hz >= 0 ? "+" : "") + juce::String (hz, std::abs (hz) < 10 ? 1 : 0) + " Hz");
}

// Low Lifter: the low band (lifted, its harmonics decaying with the slope), the crossover, the high band, the low cut
inline void paintLowLifter (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    freqGrid (g, a, s, true);
    const auto multiband = p01 (fx, "Multiband") >= 0.5f;
    const auto xoverHz = 20.0 * std::pow (1000.0, (double) p01 (fx, "Crossover") * 0.75);
    const auto lowCutHz = 20.0 * std::pow (1000.0, (double) p01 (fx, "Low cut") * 0.5);
    const auto lowGain = p01 (fx, "Low band gain"), highGain = p01 (fx, "High band gain");
    const auto slope = p01 (fx, "Slope"), boost = p01 (fx, "Pre gain");
    const auto mid = a.getY() + a.getHeight() * 0.55f;
    // bands
    const auto xx = freqX (multiband ? xoverHz : 20000.0, a);
    g.setColour (c.withAlpha (0.12f + 0.25f * lowGain));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (a.getX(), mid - a.getHeight() * 0.4f * lowGain, xx, mid));
    if (multiband)
    {
        g.setColour (colour::info.withAlpha (0.10f + 0.25f * highGain));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xx, mid - a.getHeight() * 0.4f * highGain, a.getRight(), mid));
        g.setColour (colour::textSecondary);
        g.drawVerticalLine ((int) xx, a.getY(), a.getBottom());
    }
    // the generated harmonics of a 55 Hz bass note
    for (int k = 1; k <= 10; ++k)
    {
        const auto f = 55.0 * k;
        const auto h = (k == 1 ? 0.55f : 0.5f * std::pow (0.35f + 0.6f * (1.0f - slope), (float) (k - 1)) * (0.4f + boost));
        const auto px = freqX (f, a);
        g.setColour ((k == 1 ? colour::textSecondary : c).withAlpha (fx.enabled ? 0.95f : 0.4f));
        g.fillRect (juce::Rectangle<float> (juce::jmax (2.0f, 3.0f * s), a.getHeight() * 0.8f * h).withPosition (px - 1.5f, a.getBottom() - a.getHeight() * 0.8f * h - 14.0f * s));
    }
    if (p01 (fx, "Low cut") > 0.001f)
    {
        g.setColour (colour::bg0.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (a.getX(), a.getY(), freqX (lowCutHz, a), a.getBottom()));
    }
    caption (g, a, s, tr ("Harmonics of the low end") + (multiband ? "   " + tr ("Multiband") : juce::String()));
}

inline void paintPeakMeter (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c, const History& h, bool demo)
{
    g.setColour (colour::gridBeat);
    for (int k = 1; k < 4; ++k)
        g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * (float) k / 4.0f), a.getX(), a.getRight());
    History shown = h;
    if (demo && shown.count == 0)
        for (int i = 0; i < History::length; ++i)
        {
            const auto t = (float) i / 30.0f;
            const auto beat = std::fmod (t * 2.0f, 1.0f);
            const auto peak = 0.15f + 0.7f * std::exp (-beat * 5.0f);
            const auto lfo = 0.5f + 0.4f * std::sin (t * 1.6f);
            shown.push (juce::jmin (1.0f, peak + lfo * 0.3f), peak, lfo * 0.3f);
        }
    const juce::Colour colours[3] { colour::textPrimary, c, colour::info };
    for (int o = 2; o >= 0; --o)
    {
        juce::Path p;
        for (int i = 0; i < shown.count; ++i)
        {
            const auto idx = (shown.pos - shown.count + i + History::length) % History::length;
            const auto v = juce::jlimit (0.0f, 1.0f, shown.values[(std::size_t) idx][(std::size_t) o]);
            const juce::Point<float> pt { a.getRight() - a.getWidth() * (float) (shown.count - 1 - i) / (float) (History::length - 1), a.getBottom() - a.getHeight() * v };
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        skin::glowStroke (g, p, colours[o].withAlpha (o == 0 ? 0.55f : 1.0f), juce::jmax (1.2f, 1.6f * s), fx.enabled ? 1.0f : 0.0f);
    }
    auto legend = a.reduced (4.0f * s, 2.0f * s).removeFromTop (12.0f * s);
    g.setFont (font (juce::jmax (7.5f, 9.0f * s), true));
    const char* names[3] = { TR_NOOP ("Peak + LFO"), TR_NOOP ("Peak"), TR_NOOP ("LFO") };
    for (int o = 0; o < 3; ++o)
    {
        g.setColour (colours[o]);
        g.drawText (tr (names[o]), legend.removeFromLeft (80.0f * s).toNearestInt(), juce::Justification::centredLeft, false);
    }
}

// Delay: the echoes on a time axis (two bars), left up and right down when they ping-pong, darker as damping grows
inline void paintDelayTaps (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const auto timeText = formatEffectParam (fx.kind, 0, fx.params[0], 120.0);
    const auto ms = [&] { const auto open = timeText.find ('('); return open != std::string::npos ? std::atof (timeText.c_str() + open + 1) : 250.0; }();
    const auto feedback = p01 (fx, "Feedback"), damping = p01 (fx, "Damping"), mix = p01 (fx, "Mix");
    const auto pingPong = p01 (fx, "Ping-pong") > 0.5f;
    const auto span = 4000.0; // two bars at 120 BPM
    g.setColour (colour::gridBeat);
    for (int b = 1; b < 8; ++b)
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) b / 8.0f), a.getY(), a.getBottom());
    g.setColour (colour::gridBar);
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
    auto tap = [&] (double t, float level, bool down, juce::Colour col) {
        const auto x = a.getX() + a.getWidth() * (float) (t / span);
        const auto h = a.getHeight() * 0.46f * juce::jlimit (0.0f, 1.0f, level);
        g.setColour (col);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmax (3.0f, 4.0f * s), h).withPosition (x - 1.5f * s, down ? a.getCentreY() : a.getCentreY() - h), 1.5f);
    };
    tap (0.0, 1.0f, false, colour::textSecondary);
    if (! pingPong)
        tap (0.0, 1.0f, true, colour::textSecondary);
    auto level = mix;
    for (int k = 1; k <= 24 && k * ms < span; ++k)
    {
        tap (k * ms, level, pingPong ? (k % 2 == 1) : false, c.withAlpha (fx.enabled ? 1.0f - 0.5f * damping * (float) k / 8.0f : 0.4f));
        if (! pingPong)
            tap (k * ms, level, true, c.withAlpha (fx.enabled ? 1.0f - 0.5f * damping * (float) k / 8.0f : 0.4f));
        level *= feedback;
        if (level < 0.01f)
            break;
    }
    caption (g, a, s, juce::String (timeText) + (pingPong ? "   " + tr ("Ping-pong") : juce::String()));
}

// Reverb: the predelay gap, then the tail decaying with the room size, its brightness shown by the damping
inline void paintReverbSpace (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const auto room = p01 (fx, "Room"), damping = p01 (fx, "Damping"), mix = p01 (fx, "Mix"), width = p01 (fx, "Width");
    const auto pre = p01 (fx, "Predelay"); // 0 … 1 s
    const auto span = 4.0f;               // seconds shown
    const auto decay = 0.3f + room * 4.5f;
    g.setColour (colour::gridBeat);
    for (int k = 1; k < 8; ++k)
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) k / 8.0f), a.getY(), a.getBottom());
    juce::Path tail, bright;
    const auto x0 = a.getX() + a.getWidth() * pre / span;
    tail.startNewSubPath (x0, a.getBottom());
    bright.startNewSubPath (x0, a.getBottom());
    for (int i = 0; i <= 120; ++i)
    {
        const auto t = (float) i / 120.0f * (span - pre);
        const auto env = std::exp (-t * 6.9f / decay) * (0.4f + 0.6f * mix);
        const auto x = x0 + a.getWidth() * t / span;
        tail.lineTo (x, a.getBottom() - a.getHeight() * 0.9f * env);
        bright.lineTo (x, a.getBottom() - a.getHeight() * 0.9f * env * std::exp (-t * 4.0f * damping));
    }
    tail.lineTo (a.getRight(), a.getBottom());
    tail.closeSubPath();
    g.setColour (c.withAlpha (0.10f + 0.12f * width));
    g.fillPath (tail);
    stroke (g, bright, c, s, fx.enabled);
    g.setColour (colour::textSecondary);
    g.fillRect (juce::Rectangle<float> (juce::jmax (2.0f, 3.0f * s), a.getHeight() * 0.9f).withPosition (a.getX(), a.getBottom() - a.getHeight() * 0.9f));
    caption (g, a, s, tr ("Decay") + juce::String (juce::CharPointer_UTF8 (" \xe2\x89\x88 ")) + juce::String (decay, 1) + " s", juce::Justification::topRight);
}

// Chorus: the delay line's sweep (rate, depth) of the two voices (left / right in opposite phase)
inline void paintChorus (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const auto rate = shownNumber (fx, "Rate");
    const auto depth = p01 (fx, "Depth"), fb = p01 (fx, "Feedback");
    g.setColour (colour::gridBar);
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
    for (int voice = 0; voice < 2; ++voice)
    {
        juce::Path p;
        for (int i = 0; i <= 160; ++i)
        {
            const auto t = (double) i / 160.0 * 4.0; // 4 s
            const auto y = std::sin (6.283185307179586 * rate * t + voice * 3.141592653589793) * (0.15 + 0.8 * depth);
            const juce::Point<float> pt { a.getX() + a.getWidth() * (float) i / 160.0f, a.getCentreY() - a.getHeight() * 0.45f * (float) y };
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        stroke (g, p, voice == 0 ? c : colour::info, s * (0.8f + fb * 0.6f), fx.enabled);
    }
    caption (g, a, s, "L / R", juce::Justification::topRight);
}

// Utility: how each input reaches each output (line thickness = |gain|, dashed = inverted)
inline void paintMatrix (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const float m[4] = { (float) shownNumber (fx, "L -> L"), (float) shownNumber (fx, "R -> L"), (float) shownNumber (fx, "L -> R"), (float) shownNumber (fx, "R -> R") };
    const juce::Point<float> inL { a.getX() + 14.0f * s, a.getY() + a.getHeight() * 0.3f }, inR { a.getX() + 14.0f * s, a.getY() + a.getHeight() * 0.7f };
    const juce::Point<float> outL { a.getRight() - 14.0f * s, inL.y }, outR { a.getRight() - 14.0f * s, inR.y };
    auto wire = [&] (juce::Point<float> from, juce::Point<float> to, float gain) {
        if (std::abs (gain) < 0.01f)
            return;
        juce::Path p;
        p.startNewSubPath (from);
        p.cubicTo (from.translated (a.getWidth() * 0.4f, 0), to.translated (-a.getWidth() * 0.4f, 0), to);
        const auto col = (gain < 0.0f ? colour::info : c).withAlpha (fx.enabled ? 0.95f : 0.4f);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.0f, 3.0f * s * juce::jmin (2.0f, std::abs (gain)))));
    };
    wire (inL, outL, m[0]);
    wire (inR, outL, m[1]);
    wire (inL, outR, m[2]);
    wire (inR, outR, m[3]);
    g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
    for (const auto& [pt, name] : { std::pair { inL, "L" }, std::pair { inR, "R" }, std::pair { outL, "L" }, std::pair { outR, "R" } })
    {
        g.setColour (colour::bg3);
        g.fillEllipse (juce::Rectangle<float> (18.0f * s, 18.0f * s).withCentre (pt));
        g.setColour (colour::textPrimary);
        g.drawText (name, juce::Rectangle<float> (18.0f * s, 18.0f * s).withCentre (pt).toNearestInt(), juce::Justification::centred, false);
    }
    caption (g, a, s, tr ("In"), juce::Justification::topLeft);
    caption (g, a, s, tr ("Out"), juce::Justification::topRight);
}

// Rhythmic envelope: one period of the gain curve with its points
inline void paintRhythm (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const auto count = juce::jlimit (0, 12, (int) std::lround (fx.params[1] * 12.0f));
    auto curve = [] (double x, double t) {
        if (std::abs (t) < 1e-6) return x;
        const auto q = std::pow (1000001.0, std::abs (t)) - 1.0;
        return t > 0.0 ? 1.0 - (std::pow (1.0 + q, 1.0 - x) - 1.0) / q : (std::pow (1.0 + q, x) - 1.0) / q;
    };
    auto valueAt = [&] (double u) {
        if (count == 0) return 1.0;
        auto prevX = 0.0, prevY = (double) fx.params[3];
        for (int k = 0; k < count; ++k)
        {
            const auto x = (double) fx.params[(std::size_t) (2 + 3 * k)], y = (double) fx.params[(std::size_t) (3 + 3 * k)];
            if (u < x)
            {
                const auto f = x > prevX ? (u - prevX) / (x - prevX) : 1.0;
                return prevY + (y - prevY) * curve (f, 2.0 * fx.params[(std::size_t) (4 + 3 * k)] - 1.0);
            }
            prevX = x, prevY = y;
        }
        return prevY;
    };
    g.setColour (colour::gridBeat);
    for (int k = 1; k < 8; ++k)
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) k / 8.0f), a.getY(), a.getBottom());
    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const auto u = (double) i / 200.0;
        const juce::Point<float> pt { a.getX() + a.getWidth() * (float) u, a.getBottom() - a.getHeight() * (float) juce::jlimit (0.0, 1.0, valueAt (u)) };
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    auto fill = p;
    fill.lineTo (a.getRight(), a.getBottom());
    fill.lineTo (a.getX(), a.getBottom());
    fill.closeSubPath();
    g.setColour (c.withAlpha (0.14f));
    g.fillPath (fill);
    stroke (g, p, c, s, fx.enabled);
    g.setColour (colour::textPrimary);
    for (int k = 0; k < count; ++k)
        g.fillEllipse (juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre ({ a.getX() + a.getWidth() * fx.params[(std::size_t) (2 + 3 * k)],
                                                                               a.getBottom() - a.getHeight() * fx.params[(std::size_t) (3 + 3 * k)] }));
    caption (g, a, s, juce::String (formatEffectParam (fx.kind, 0, fx.params[0], 120.0)), juce::Justification::topRight);
}

// Pitch shift: the interval on a keyboard (from C4 to the shifted note)
inline void paintPitchShift (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, juce::Colour c)
{
    const auto semis = (int) std::lround (shownNumber (fx, "Semitones"));
    const auto cents = shownNumber (fx, "Cents");
    constexpr int lo = -24, hi = 24;
    const auto keyW = a.getWidth() / (float) (hi - lo + 1);
    for (int k = lo; k <= hi; ++k)
    {
        const auto pc = ((k % 12) + 12) % 12;
        const auto black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        auto key = juce::Rectangle<float> (a.getX() + keyW * (float) (k - lo), a.getY() + 16.0f * s, keyW - 1.0f, a.getHeight() - 16.0f * s);
        if (black)
            key = key.withHeight (key.getHeight() * 0.62f);
        g.setColour (k == 0 ? colour::textSecondary : k == semis ? c : (black ? colour::bg0 : colour::bg4));
        g.fillRect (key);
    }
    caption (g, a, s, (semis > 0 ? "+" : "") + juce::String (semis) + " st  " + (cents >= 0 ? "+" : "") + juce::String ((int) cents) + " ct");
}

// Distructor: the module in this slot (from the FL state), its kind and algorithm, bypass shown dim
inline void paintDistructorModule (juce::Graphics& g, juce::Rectangle<float> a, float s, const EffectSlot& fx, const char* param)
{
    const auto index = juce::jmax (0, plugui::paramIndex (fx.kind, param)) / 3;
    static std::mutex lock;
    static std::vector<std::uint8_t> lastBlob;
    static std::optional<distructor::State> lastState;
    std::optional<distructor::ModuleState> module;
    bool parsed = false;
    {
        const std::lock_guard<std::mutex> guard (lock);
        if (lastBlob != fx.state)
        {
            lastBlob = fx.state;
            lastState = fx.state.empty() ? std::nullopt : distructor::parseBlob (fx.state);
        }
        parsed = lastState.has_value();
        if (lastState)
        {
            const auto slot = (std::size_t) index < lastState->order.size() ? lastState->order[(std::size_t) index] : index;
            if (slot >= 0 && slot < (int) lastState->modules.size())
                module = lastState->modules[(std::size_t) slot];
        }
    }
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (a, 3.0f * s);
    const auto text = module ? juce::String (distructor::moduleName (*module))
                             : parsed ? tr ("Empty slot") : (index == 0 ? tr ("Modules come from an FL Studio Distructor preset or project") : juce::String());
    g.setColour (module && ! module->bypass && fx.enabled ? colour::textPrimary : colour::textDisabled);
    g.setFont (font (juce::jmax (8.5f, 10.5f * s), module.has_value()));
    g.drawFittedText (text, a.reduced (6.0f * s).toNearestInt(), juce::Justification::centred, 4, 0.8f);
}
} // namespace detail

inline bool paint (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> area, float s, const EffectSlot& fx, juce::Colour accent,
                   const History& history, bool demo, const char* param = nullptr)
{
    if (! handles (id.toRawUTF8()))
        return false;
    const auto a = area.reduced (5.0f * s);
    const auto c = fx.enabled ? accent : theme::colour::textDisabled;
    if (id == "fxresponse")
        detail::paintResponse (g, a, s, fx, c);
    else if (id == "fxtransfer")
        detail::paintTransfer (g, a, s, fx, c);
    else if (id == "spreadfield")
        detail::paintSpread (g, a, s, fx, c);
    else if (id == "fshift")
        detail::paintShift (g, a, s, fx, c);
    else if (id == "lowlifter")
        detail::paintLowLifter (g, a, s, fx, c);
    else if (id == "delaytaps")
        detail::paintDelayTaps (g, a, s, fx, c);
    else if (id == "reverbspace")
        detail::paintReverbSpace (g, a, s, fx, c);
    else if (id == "chorusvoices")
        detail::paintChorus (g, a, s, fx, c);
    else if (id == "utilitymatrix")
        detail::paintMatrix (g, a, s, fx, c);
    else if (id == "rhythmcurve")
        detail::paintRhythm (g, a, s, fx, c);
    else if (id == "pitchshift")
        detail::paintPitchShift (g, a, s, fx, c);
    else if (id == "distructormodule")
        detail::paintDistructorModule (g, area.reduced (1.0f), s, fx, param);
    else if (id == "peakmeter")
        detail::paintPeakMeter (g, a, s, fx, c, history, demo);
    return true;
}
} // namespace lynx::designui
