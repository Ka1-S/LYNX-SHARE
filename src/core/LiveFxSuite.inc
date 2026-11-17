// Round 190kk: LYNX's own engines that Live's still-dropped devices open on — the Effects around core/Corrode.h (Live's
// Erosion) core/Soundboard.h (Live's Corpus) and core/Platter.h (Live's Vinyl Distortion). Included by core/MixSuite.cpp at namespace mixsuite level, after AnvilSuite.inc (it uses `fmt` and `hzText`).

namespace
{
class CorrodeFx final : public Effect
{
public:
    void prepare (double rate) noexcept override { engine.prepare (rate); }
    void reset() noexcept override { engine.reset(); }
    int latencySamples() const noexcept override { return engine.latency(); }
    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override { engine.process (l, r, n, p.data()); }

private:
    corrode::Engine engine;
};
} // namespace

std::unique_ptr<Effect> makeCorrode() { return std::make_unique<CorrodeFx>(); }

const EffectInfo& corrodeInfo()
{
    // a fresh device: noise at 2 kHz, half an octave wide, a third of the way in, mono
    static const EffectInfo info { "Corrode",
                                   { { "Amount", 0.3f }, { "Frequency", corrode::frequencyValue (2000.0) }, { "Width", corrode::widthValue (0.5) },
                                     { "Noise", 1.0f }, { "Stereo", 0.0f } } };
    static_assert (corrode::count == 5);
    return info;
}

std::string corrodeParamText (int param, float v)
{
    namespace c = corrode;
    switch (param)
    {
        case c::amount:
        case c::noise:
        case c::stereo: return fmt ("%.0f%%", v * 100.0);
        case c::frequency: return hzText (c::frequencyHz (v));
        case c::width: return fmt ("%.2f oct", c::widthOct (v));
        default: return {};
    }
}

namespace
{
class SoundboardFx final : public Effect
{
public:
    void prepare (double rate) noexcept override { engine.prepare (rate); }
    void reset() noexcept override { engine.reset(); }
    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override { engine.process (l, r, n, p.data()); }

private:
    soundboard::Engine engine;
};
} // namespace

std::unique_ptr<Effect> makeSoundboard() { return std::make_unique<SoundboardFx>(); }

const EffectInfo& soundboardInfo()
{
    namespace s = soundboard;
    // a fresh device: a membrane at C2, medium decay, centred hit and pick-ups slightly apart, fully wet
    static const EffectInfo info { "Soundboard",
                                   { { "Type", s::kindValue (s::Kind::membrane) }, { "Quality", s::qualityValue (3) }, { "Tune", 0.25f },
                                     { "Decay", 0.5f }, { "Material", 0.5f }, { "Brightness", 0.5f }, { "Inharmonics", 0.5f },
                                     { "Radius", 0.5f }, { "Opening", 1.0f }, { "Ratio", 0.5f }, { "Hit", 0.5f }, { "Listen L", 0.3f },
                                     { "Listen R", 0.7f }, { "Spread", 0.5f }, { "Gain", 0.0f }, { "Bleed", 0.0f }, { "Dry/Wet", 1.0f },
                                     { "Width", 1.0f }, { "Filter", 0.0f }, { "Filter freq.", s::filterHzValue (1000.0) },
                                     { "Filter width", s::filterOctavesValue (4.0) } } };
    static_assert (s::count == 21);
    return info;
}

std::string soundboardParamText (int param, float v)
{
    namespace s = soundboard;
    switch (param)
    {
        case s::type: return s::kindName (s::kindOf (v));
        case s::quality:
        {
            static const char* const q[4] { "Eco", "Mid", "High", "Full" };
            return q[s::qualityOf (v)];
        }
        case s::tune: return hzText (s::tuneHz (v));
        case s::decay: return fmt ("%.2f s", s::decaySeconds (v));
        case s::material:
        case s::brightness:
        case s::inharmonics: return fmt ("%+.0f%%", s::bipolar (v) * 100.0);
        case s::spread: return fmt ("%+.2f st", s::bipolar (v));
        case s::gain: return fmt ("%+.1f dB", s::gainDb (v));
        case s::filterOn: return s::on (v) ? "On" : "Off";
        case s::filterHz: return hzText (s::filterHzOf (v));
        case s::filterWidth: return fmt ("%.1f oct", s::filterOctaves (v));
        default: return fmt ("%.0f%%", v * 100.0);
    }
}

namespace
{
class PlatterFx final : public Effect
{
public:
    void prepare (double rate) noexcept override { engine.prepare (rate); }
    void reset() noexcept override { engine.reset(); }
    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override { engine.process (l, r, n, p.data()); }

private:
    platter::Engine engine;
};
} // namespace

std::unique_ptr<Effect> makePlatter() { return std::make_unique<PlatterFx>(); }

const EffectInfo& platterInfo()
{
    namespace v = platter;
    // a fresh device: Tracing on at 1 kHz, Drive at a third, soft, no crackle
    static const EffectInfo info { "Platter",
                                   { { "Drive", 0.33f }, { "Tracing drive", 0.5f }, { "Tracing freq.", v::bandHzValue (1000.0) },
                                     { "Pinch drive", 0.5f }, { "Pinch freq.", v::bandHzValue (684.0) }, { "Crackle volume", 0.0f },
                                     { "Crackle density", 0.2f }, { "Tracing", 1.0f }, { "Tracing width", v::bandQValue (3.0) },
                                     { "Pinch", 0.0f }, { "Pinch width", v::bandQValue (0.32) }, { "Hard", 0.0f }, { "Mono", 0.0f } } };
    static_assert (v::count == 13);
    return info;
}

std::string platterParamText (int param, float v)
{
    namespace p = platter;
    switch (param)
    {
        case p::tracingOn:
        case p::pinchOn:
        case p::mono: return p::on (v) ? "On" : "Off";
        case p::hard: return p::on (v) ? "Hard" : "Soft";
        case p::tracingFreq:
        case p::pinchFreq: return hzText (p::bandHz (v));
        case p::tracingWidth:
        case p::pinchWidth: return fmt ("%.2f", p::bandQ (v));
        case p::crackleDensity: return fmt ("%.1f", p::density (v));
        default: return fmt ("%.0f%%", v * 100.0);
    }
}
