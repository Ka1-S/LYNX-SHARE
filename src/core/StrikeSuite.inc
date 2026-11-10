// Round trans189d (issue #94): Strike Shaper, LYNX's level-independent transient designer — the Effect around
// core/StrikeShaper.h. Included by core/MixSuite.cpp at namespace mixsuite level, after StripSuite.inc (it uses `fmt`).

namespace
{
class StrikeShaperFx final : public Effect
{
public:
    void prepare (double rate) noexcept override { shaper.prepare (rate); }
    void reset() noexcept override { shaper.reset(); }
    // 0 the attack gain, 1 the sustain gain (0.5 = 0 dB; ±0.5 = ±24 dB, signed), 2 / 3 input / output peaks
    float controlOutput (int k) const noexcept override
    {
        if (k == 0) return attackLamp.load (std::memory_order_relaxed);
        if (k == 1) return sustainLamp.load (std::memory_order_relaxed);
        if (k == 2) return inPeak.load (std::memory_order_relaxed);
        if (k == 3) return outPeak.load (std::memory_order_relaxed);
        return 0.0f;
    }
    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        inPeak.store (Meters::peak (l, r, n), std::memory_order_relaxed);
        shaper.process (l, r, n, p.data());
        outPeak.store (Meters::peak (l, r, n), std::memory_order_relaxed);
        attackLamp.store ((float) std::clamp (0.5 + shaper.lastAttackDb / 48.0, 0.0, 1.0), std::memory_order_relaxed);
        sustainLamp.store ((float) std::clamp (0.5 + shaper.lastSustainDb / 48.0, 0.0, 1.0), std::memory_order_relaxed);
    }

private:
    strike::Shaper shaper;
    std::atomic<float> attackLamp { 0.5f }, sustainLamp { 0.5f }, inPeak { 0.0f }, outPeak { 0.0f };
};
} // namespace

std::unique_ptr<Effect> makeStrike() { return std::make_unique<StrikeShaperFx>(); }

const EffectInfo& strikeInfo()
{
    // the two-knob front panel's neutral start: Attack and Sustain at 0 dB, Output 0 dB, Guard off, Mix 100 %, linked
    static const EffectInfo info { "Strike Shaper",
                                   { { "Attack", 0.5f }, { "Sustain", 0.5f }, { "Output", 0.5f }, { "Guard", 0.0f },
                                     { "Mix", 1.0f }, { "Link", 1.0f }, { "Detect high-pass", 0.0f } } };
    static_assert (strike::count == 7);
    return info;
}

std::string strikeParamText (int param, float v)
{
    namespace s = strike;
    switch (param)
    {
        case s::attack: return fmt ("%+.1f dB", s::attackDb (v));
        case s::sustain: return fmt ("%+.1f dB", s::sustainDb (v));
        case s::output: return fmt ("%+.1f dB", s::outputDb (v));
        case s::mix: return fmt ("%.0f%%", v * 100.0);
        case s::detectHp: return v <= 0.0f ? std::string ("off") : hzText (s::detectHpHz (v));
        default: return v >= 0.5f ? "On" : "Off";
    }
}
