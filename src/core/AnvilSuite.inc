// Round 190fk: Anvil Bus, LYNX's drum-bus processor — the Effect around core/AnvilBus.h. Included by core/MixSuite.cpp at
// namespace mixsuite level, after StrikeSuite.inc (it uses `fmt` and `hzText`).

namespace
{
class AnvilBusFx final : public Effect
{
public:
    void prepare (double rate) noexcept override { engine.prepare (rate); }
    void reset() noexcept override { engine.reset(); }
    int latencySamples() const noexcept override { return anvil::Engine::latency(); }
    // 0 / 1 the input / output peaks, 2 Comp's gain reduction (0..1 = 0..24 dB), 3 the transient gain now (0.5 = 0 dB, ±24 dB)
    float controlOutput (int k) const noexcept override
    {
        if (k == 0) return inPeak.load (std::memory_order_relaxed);
        if (k == 1) return outPeak.load (std::memory_order_relaxed);
        if (k == 2) return compLamp.load (std::memory_order_relaxed);
        if (k == 3) return shapeLamp.load (std::memory_order_relaxed);
        return 0.0f;
    }
    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        inPeak.store (Meters::peak (l, r, n), std::memory_order_relaxed);
        engine.process (l, r, n, p.data());
        outPeak.store (Meters::peak (l, r, n), std::memory_order_relaxed);
        compLamp.store ((float) std::clamp (engine.lastCompDb / 24.0, 0.0, 1.0), std::memory_order_relaxed);
        shapeLamp.store ((float) std::clamp (0.5 + (engine.lastAttackDb + engine.lastSustainDb) / 96.0, 0.0, 1.0), std::memory_order_relaxed);
    }

private:
    anvil::Engine engine;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, compLamp { 0.0f }, shapeLamp { 0.5f };
};
} // namespace

std::unique_ptr<Effect> makeAnvil() { return std::make_unique<AnvilBusFx>(); }

const EffectInfo& anvilInfo()
{
    // a fresh device: 20 % of Soft drive (the colour always on), everything else neutral, fully wet
    static const EffectInfo info { "Anvil Bus",
                                   { { "Drive", 0.2f }, { "Drive type", 0.0f }, { "Crunch", 0.0f }, { "Damp", 1.0f }, { "Transients", 0.5f },
                                     { "Boom", 0.0f }, { "Boom freq.", 1.0f / 3.0f }, { "Boom decay", 0.5f }, { "Comp", 0.0f },
                                     { "Trim", 1.0f }, { "Output", 40.0f / 52.0f }, { "Dry/Wet", 1.0f } } };
    static_assert (anvil::count == 12);
    return info;
}

std::string anvilParamText (int param, float v)
{
    namespace a = anvil;
    switch (param)
    {
        case a::drive:
        case a::crunch:
        case a::boom:
        case a::boomDecay:
        case a::dryWet: return fmt ("%.0f%%", v * 100.0);
        case a::driveType: return a::typeName (a::typeOf (v));
        case a::damp: return hzText (a::dampHz (v));
        case a::transients: return fmt ("%+.0f%%", a::transientsAmount (v) * 100.0);
        case a::boomFreq: return fmt ("%.1f Hz", a::boomHz (v));
        case a::comp: return v >= 0.5f ? "On" : "Off";
        case a::trim:
        {
            const auto db = a::trimDb (v);
            return db <= -69.95 ? std::string ("-70.0 dB") : db > -0.05 ? std::string ("0.0 dB") : fmt ("%.1f dB", db);
        }
        case a::output: return fmt ("%+.1f dB", a::outputDb (v));
        default: return {};
    }
}
