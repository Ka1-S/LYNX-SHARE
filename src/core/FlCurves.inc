// FL's curve library (the envelopes, LFOs and maps of Image-Line's articulators: "TMEnvelope", "TMLFO", "TMMap"), as
// decoded for Fruity Love Philter in docs/analise/REA_rodada66.md and found again in Slicex (REA_rodada190h.md Part 2
// §4.1). Clean-room. Textual include: the including file defines, in the same (unnamed) namespace and before this
// line, `Pt` and `Env` (the point and curve records), F (double → float), rnd, trunc64, phaseWrap, hiBelowHalf and
// u32, includes core/DelphiMath.h, and sets its floating-point pragmas (no contraction). The arithmetic follows the
// plugins' operation by operation.

// ------------------------------------------------------------------------------- envelope curves (TMEnvelope)
struct Curve
{
    std::vector<Pt> pts;
    double offset = 0.0; // 0.5 for additive articulators (and the LFO's amplitude envelope)
    double scale = 1.0;  // time units per tick
    int flags = 0, on = 0, decay = -1, loop = -1, sus = -1, endpt = -1;
    bool zeroloop = false;

    static int stepCount (const Pt& p) noexcept
    {
        if (p.mode > 7 || ((0x78u >> p.mode) & 1u) == 0)
            return -1;
        const double a = (double) F (1.0 - std::abs ((double) p.t) * 0.6);
        auto n = (int) rnd ((double) F (dmath::exp (a * dmath::ln (1000001.0)) - 1.0) * 0.001);
        if (p.mode == 3 && p.t >= 0.0f)
            ++n;
        return n;
    }

    // `noDecay`: the decay point is dropped at load (pattern envelopes, LFO envelopes, maps); `map`: decay, loop and
    // sustain are dropped and the x axis is normalised to a length of 1 (TMMap)
    void build (const Env& e, double off, bool map)
    {
        pts = e.pts;
        offset = off;
        flags = e.flags;
        on = e.on;
        decay = -1;
        loop = map ? -1 : e.loop;
        sus = map ? -1 : e.sus;
        if (map)
            normalise();
        if (pts.empty())
            pts.push_back (Pt {});
        prepare();
    }

    void normalise()
    {
        if (pts.empty())
        {
            pts.push_back (Pt {});
            return;
        }
        if (pts[0].dt > 0.0f)
        {
            Pt p0;
            p0.y = pts[0].y;
            pts.insert (pts.begin(), p0);
        }
        double total = 0.0;
        for (const auto& p : pts)
            total += (double) p.dt;
        if (total > 0.0)
        {
            const double f = 1.0 / total;
            if (f != 1.0)
                for (auto& p : pts)
                    p.dt = (float) ((double) p.dt * f);
        }
        else
        {
            Pt p1;
            p1.dt = 1.0f;
            p1.y = pts.back().y;
            pts.push_back (p1);
        }
    }

    // curve mode 10 ("smooth"): slopes from the neighbouring points (the same envelope library as Fruity
    // WaveShaper's maps, REA_rodada51 §2.4)
    void prepareSmooth (int i) noexcept
    {
        constexpr double kEps = 5.960464477539063e-08;
        auto& p = pts[(std::size_t) i];
        const int n = (int) pts.size();
        const double h = (double) p.dt > kEps ? (double) p.dt : kEps;
        const double y1 = p.y;
        const int j = i - 1;
        const double y0 = j >= 0 ? (double) pts[(std::size_t) j].y : y1;
        double rp = 1.0, ypp = y0;
        if (j >= 0 && pts[(std::size_t) j].mode == 10)
        {
            rp = (double) pts[(std::size_t) j].dt / h;
            if (i - 2 >= 0)
                ypp = pts[(std::size_t) (i - 2)].y;
        }
        double rn = 1.0, yn = y1;
        if (i + 1 < n && pts[(std::size_t) (i + 1)].mode == 10)
        {
            rn = (double) pts[(std::size_t) (i + 1)].dt / h;
            yn = pts[(std::size_t) (i + 1)].y;
        }
        const double d = y1 - y0;
        if (std::abs (d) < kEps)
        {
            p.sign = 2;
            return;
        }
        const double a = (ypp - y0) / d, b = (yn - y0) / d;
        rp = rp > kEps ? rp : kEps;
        rn = rn > kEps ? rn : kEps;
        const double sr = (b - 1.0) / rn, sl = -a / rp;
        if (! (F (sr) * 1.0f > 0.0f))
            p.p4 = 0.0f;
        else
        {
            const float w1 = F (rn) * 2.0f + 1.0f, w2 = F (rn) + 2.0f;
            p.p4 = (F (sr) * 1.0f * (w1 + w2)) / (w1 * F (sr) + w2 * 1.0f);
        }
        if (! (1.0f * F (sl) > 0.0f))
            p.p5 = 0.0f;
        else
        {
            const float w1 = 2.0f + F (rp), w2 = 1.0f + F (rp) * 2.0f;
            p.p5 = (1.0f * F (sl) * (w1 + w2)) / (w1 * 1.0f + w2 * F (sl));
        }
        p.p6 = F (1.0 - std::abs ((double) p.t));
    }

    double time (int i) const noexcept
    {
        double s = 0.0;
        if (i >= 0 && i < (int) pts.size())
            for (int k = 0; k <= i; ++k)
                s += (double) pts[(std::size_t) k].dt;
        return s;
    }

    void prepare()
    {
        const int n = (int) pts.size();
        endpt = sus;
        if (endpt < 0 && loop >= 0)
            endpt = n - 1;
        const int D = std::max (decay, 0);
        const int S = sus >= 0 ? sus : n - 1;
        const int L = loop >= 0 ? loop : S;
        double A = 0, M = 0, R = 0;
        for (int i = 0; i < n; ++i)
        {
            auto& p = pts[(std::size_t) i];
            if (p.dt <= 0.0f)
                p.dt = 0.0f;
            if (i > D)
            {
                if (i <= L)
                    M += (double) p.dt;
            }
            else
                A += (double) p.dt;
            if (i > S)
                R += (double) p.dt;
            if (i == 0)
                continue;
            const float t = p.t;
            p.sign = t == 0.0f ? 0 : (std::signbit (t) ? -1 : 1);
            const int cnt = stepCount (p);
            const int m = p.mode;
            if (m == 6)
            {
                p.p4 = F ((double) cnt + 0.5);
                p.p5 = 6.2831855f;
                if (cnt == 0 && p.sign > 0)
                    p.sign = 2;
            }
            else if (m < 2)
            {
                const auto e1 = F (dmath::exp ((double) std::abs (t) * dmath::ln (1000001.0)) - 1.0);
                const float sg = t > 0.0f ? 1.0f : (t < 0.0f ? -1.0f : 0.0f);
                p.k = e1 * sg;
                if (p.k != 0.0f)
                {
                    p.ilk = F (1.0 / std::abs ((double) p.k));
                    p.L = F (dmath::ln (std::abs ((double) p.k) + 1.0));
                }
            }
            else if (m == 3)
            {
                const int sg = t < 0.0f ? -1 : 1;
                p.p6 = (float) sg * 0.5f;
                p.p4 = (float) (cnt + 1);
                p.p5 = 1.0f / (p.p4 - (float) sg);
            }
            else if (m == 4)
            {
                p.p4 = (float) (cnt + 1);
                p.p5 = 1.0f / p.p4;
            }
            else if (m == 5)
            {
                const int sg = t < 0.0f ? -1 : 1;
                p.p6 = (float) sg * 0.5f;
                p.p4 = (float) ((cnt + 1) * 2);
                p.p5 = 6.2831855f;
            }
            else if (m == 7 || m == 8)
                p.p4 = t;
            else if (m == 9)
                p.p4 = F (std::abs ((double) t));
            else if (m == 10)
                prepareSmooth (i);
            else if (m == 11 || m == 12)
            {
                auto dd = std::abs ((double) t) * 0.9;
                if (dd <= 1.52587890625e-05)
                    dd = 1.52587890625e-05;
                const auto uu = 1.0 - dd;
                p.p4 = F (1.0 - uu * uu * uu);
                p.p5 = (1.0f - p.p4) / p.p4;
                p.p6 = p.p5;
                if (p.sign > 0)
                {
                    p.p5 = -p.p5;
                    p.p6 = p.p5 - 1.0f;
                }
                if (m == 12)
                {
                    p.p5 = F ((double) p.p5 * 0.5);
                    p.p6 = F ((double) p.p6 * 0.5);
                }
            }
            if (m < 16 && ((0x1b83u >> m) & 1u) != 0 && t == 0.0f)
                p.sign = 2;
        }
        // modulation weights (only used with a sustain offset; kept for completeness)
        double r = A;
        for (int i = 0; i < n; ++i)
        {
            auto& p = pts[(std::size_t) i];
            double nr = 0.0;
            p.w = 0.0f;
            if (i > D)
            {
                p.w = 1.0f;
                if (i > S)
                {
                    nr = r - (double) p.dt;
                    p.w = F (nr / std::max (R, 1.52587890625e-05));
                }
                else
                {
                    if (i <= L)
                        p.w = F ((r + (double) p.dt) / std::max (M, 1.52587890625e-05));
                    nr = r + (double) p.dt;
                    if (i == S)
                        nr = R;
                }
            }
            else if (i != D)
            {
                nr = r - (double) p.dt;
                p.w = F (nr / std::max (A, 1.52587890625e-05));
            }
            r = nr;
        }
        zeroloop = false;
        if (endpt >= 0 && loop >= 0)
            zeroloop = time (endpt) - time (loop) <= 0.0;
    }

    double level (int i) const noexcept
    {
        if (i < 0 || i >= (int) pts.size())
            return 0.0;
        return (double) pts[(std::size_t) i].y - offset;
    }

    static double curveK (const Pt& p, double x) noexcept
    {
        if (p.k == 0.0f)
            return x;
        if (p.k > 0.0f)
            return 1.0 - (dmath::exp ((1.0 - x) * (double) p.L) - 1.0) * (double) p.ilk;
        return (dmath::exp (x * (double) p.L) - 1.0) * (double) p.ilk;
    }

    // fraction (0…1) of the segment ending at point i at phase ph
    float shape (int i, double ph) const noexcept
    {
        if (i < 0 || i >= (int) pts.size())
            return 0.0f;
        const auto& p = pts[(std::size_t) i];
        if (p.sign == 2)
            return F (ph);
        const double p4 = p.p4, p5 = p.p5, p6 = p.p6;
        switch (p.mode)
        {
            case 0: return F (curveK (p, ph));
            case 1:
                if (hiBelowHalf (ph))
                    return F (curveK (p, ph * 2.0) * 0.5);
                return F (1.0 - curveK (p, (1.0 - ph) * 2.0) * 0.5);
            case 3: return (float) (int) rnd (ph * p4 - p6) * p.p5;
            case 4:
            {
                const auto v = ph * p4 - 0.5;
                const auto i2 = (int) rnd (v - 0.5);
                auto dd = (v - (double) i2) * 2.0 - 1.0;
                if (p.sign < 0)
                {
                    const auto z = 1.0 - std::abs (dd);
                    dd = (1.0 - z * z * z) * (std::signbit (dd) ? -1.0 : 1.0);
                }
                else
                    dd = dd * dd * dd;
                return F (((double) i2 + dd * 0.5 + 1.0) * p5);
            }
            case 5: return (float) (rnd (ph * p4 - p6) & 1);
            case 6:
            {
                if (p.sign < 0)
                    return F ((dmath::sin ((ph * p4 + 0.75) * p5) + 1.0) * 0.5);
                const auto v = ph * p4 - 0.5;
                const auto i2 = (int) rnd (v - 0.5);
                return F (std::abs ((v - (double) i2) * 2.0 - 1.0));
            }
            case 7:
            {
                const auto dd = p.sign > 0 ? 1.0 - ph : ph;
                const auto d3 = dd * dd * dd;
                return F (ph - (d3 * d3 - dd) * p4);
            }
            case 8:
            {
                if (hiBelowHalf (ph))
                {
                    const auto x = ph * 2.0;
                    const auto dd = p.sign > 0 ? 1.0 - x : x;
                    const auto d3 = dd * dd * dd;
                    return F ((x - (d3 * d3 - dd) * p4) * 0.5);
                }
                const auto x = (1.0 - ph) * 2.0;
                const auto dd = p.sign > 0 ? 1.0 - x : x;
                const auto d3 = dd * dd * dd;
                return F (1.0 - (x - (d3 * d3 - dd) * p4) * 0.5);
            }
            case 9:
            {
                const auto hp = (double) 1.5707963705062866;
                const auto dd = p.sign > 0 ? dmath::sin (ph * hp) : 1.0 - dmath::cos (ph * hp);
                return F (ph - (ph - dd) * p4);
            }
            case 10:
            {
                const auto x2 = ph * ph, x1 = ph - 1.0;
                return F (ph + ((((x2 * 3.0 - x2 * 2.0 * ph) + x2 * x1 * p4) + ph * x1 * x1 * p5) - ph) * p6);
            }
            case 11:
                if (p.sign < 1)
                    return F (p5 / (1.0 - p4 * ph) - p6);
                return F (p5 / (1.0 - p4 * (1.0 - ph)) - p6);
            case 12:
                if (hiBelowHalf (ph))
                {
                    if (p.sign < 1)
                        return F (p5 / (1.0 - p4 * ph * 2.0) - p6);
                    return F (p5 / (1.0 - p4 * (1.0 - ph * 2.0)) - p6);
                }
                else
                {
                    const auto x = (1.0 - ph) * 2.0;
                    if (p.sign < 1)
                        return F ((1.0 - p5 / (1.0 - p4 * x)) + p6);
                    return F ((1.0 - p5 / (1.0 - p4 * (1.0 - x))) + p6);
                }
            default: return 0.0f; // 2 (hold) and unknown modes
        }
    }

    // "default" map: flat at the neutral value (1 multiplicative, 0.5 additive)
    bool isDefault() const noexcept
    {
        for (const auto& p : pts)
            if (p.y != pts[0].y)
                return false;
        return (double) pts[0].y == (offset == 0.5 ? 0.5 : 1.0);
    }

    // the map iterator stepped through x = 0, 1/1024, … (the plugin's table builder)
    void table (float* out, int count, double step) const noexcept
    {
        const int n = (int) pts.size();
        int stage = 1, idx = 0;
        double phase, rate;
        if (pts[0].dt == 0.0f)
            rate = 1.0, phase = 1.0;
        else
            rate = scale / (double) pts[0].dt, phase = 0.0;
        float start = F (level (0)), delta = 0.0f, value = start;
        for (int k = 0; k < count; ++k)
        {
            out[k] = value;
            // step
            if (! (idx >= 0 && idx <= n - 1))
                continue;
            phase += step * rate;
            while (phaseWrap (phase))
            {
                const int prev = idx;
                if (rate == 0.0)
                    phase = step;
                else
                    phase = (phase - 1.0) / rate;
                start = F (level (idx));
                if (idx < n - 1)
                    ++idx;
                else
                    stage = 9;
                delta = F (level (idx) - (double) start);
                if (prev == idx)
                    phase = rate = 0.0;
                else
                {
                    const double dt = (double) pts[(std::size_t) idx].dt;
                    if (dt <= 0.0)
                        rate = 1.0, phase += 1.0;
                    else
                        rate = 1.0 / dt, phase *= rate;
                }
            }
            (void) stage;
            value = start + shape (idx, phase) * delta;
        }
    }
};

// envelope state (the plugin's per-instance evaluation record)
struct EnvSt
{
    int stage = 9, idx = 0, sub = 9;
    double phase = 0, rate = 0;
    float start = 0, delta = 0, out = 0;
    bool hold = false;

    void begin (const Curve& c) noexcept
    {
        if (c.on == 0)
        {
            stage = sub = 9;
            start = delta = out = 0.0f;
            return;
        }
        idx = 0;
        stage = c.decay < 0 ? 3 : 1;
        if (c.pts[0].dt == 0.0f)
            rate = 1.0, phase = 1.0;
        else
            rate = c.scale / (double) c.pts[0].dt, phase = 0.0;
        sub = stage;
        start = F (c.level (0));
        delta = 0.0f;
        out = start;
        hold = c.zeroloop; // the D knob is 1 for these envelopes
    }

    void eval (const Curve& c, double adv) noexcept
    {
        const int n = (int) c.pts.size();
        if (! (idx >= 0 && idx <= n - 1))
            return;
        phase += adv * rate;
        while (phaseWrap (phase))
        {
            if (rate == 0.0)
                phase = adv;
            else
                phase = (phase - 1.0) / rate;
            start = F (c.level (idx));
            bool stop;
            if (idx < c.endpt || c.endpt < 0 || stage > 5)
            {
                if (c.decay <= idx && c.decay >= 0 && sub < 3)
                {
                    stage = std::max (stage, 3);
                    sub = 3;
                }
                if (idx < n - 1)
                {
                    ++idx;
                    stop = false;
                }
                else
                {
                    stage = 9;
                    stop = true;
                }
            }
            else if (c.loop < 0)
                stop = true;
            else
            {
                sub = 4;
                idx = c.loop + 1;
                stop = hold;
            }
            delta = F (c.level (idx) - (double) start);
            if (! stop)
            {
                const double dur = (double) c.pts[(std::size_t) idx].dt; // × knob factor 1
                if (dur <= 0.0)
                {
                    rate = 1.0;
                    phase += 1.0;
                }
                else
                {
                    rate = c.scale / dur;
                    phase *= rate;
                }
            }
            else
                phase = rate = 0.0;
        }
        if (delta == 0.0f)
            out = start;
        else
            out = start + c.shape (idx, phase) * delta;
    }

    // positioning at `pos` ticks (FUN of the transport resync): negative = not started yet
    void position (const Curve& c, double pos) noexcept
    {
        begin (c);
        if (pos < 0.0)
        {
            if (stage != 9)
            {
                rate = -1.0 / pos;
                phase = 0.0;
            }
            return;
        }
        double t = pos * c.scale;
        if (c.loop >= 0)
        {
            const double t0 = c.time (c.loop), len = c.time (c.endpt) - t0;
            if (len > 0.0)
            {
                const auto q = trunc64 ((t - t0) / len);
                t = (t0 + (t - t0)) - (double) q * len;
            }
        }
        eval (c, t / c.scale);
    }
};

// LFO waveform (shape and tension of the LFO object)
struct LfoWave
{
    float k = 0, ik = 0, L = 0;
    bool uni = false;
    u32 p5 = 0, p6 = 0, p7 = 0;
    float s0 = 0, s1 = 0;
    u32 startPhase = 0;

    void build (int tension, int skew, int pw, bool unipolar)
    {
        const float at = (float) std::abs (tension) * 0.0078125f;
        const double a = (double) F (dmath::exp ((double) at * dmath::ln (21.0)) - 1.0) * 0.05;
        const float sg = tension == 0 ? 0.0f : (tension < 0 ? -1.0f : 1.0f);
        k = sg * F (dmath::exp ((double) F (a) * dmath::ln (9.999999843067494e17)) - 1.0);
        if (k != 0.0f)
        {
            ik = F (1.0 / std::abs ((double) k));
            L = F (dmath::ln (std::abs ((double) k) + 1.0));
        }
        const float sk = (float) skew * 0.00390625f;
        const float pwf = (float) pw * 0.0078125f;
        uni = unipolar;
        if (0.0078125f >= pwf)
            p7 = 0xFFFFFFFFu;
        else
            p7 = (u32) rnd ((double) F (F (1.0f - pwf) * F (4294967296.0) - F (1.0)));
        auto dd = ((double) F ((1.0f - pwf) / 2.0f) - (double) sk * (1.0 - std::abs ((double) pwf))) * 4294967296.0;
        const auto hi = (u32) (p7 - 1u);
        if (dd > (double) hi)
            dd = (double) hi;
        else if (dd < 1.0)
            dd = 1.0;
        p5 = (u32) (std::uint64_t) rnd (dd);
        if (pwf < 0.0078125f)
        {
            p6 = (u32) (std::uint64_t) rnd (dd + (double) F (pwf * 4294967296.0f));
            if (p6 == 0)
                p6 = 1;
        }
        else
            p6 = p5;
        s0 = F (2.0 / (double) p6);
        s1 = F (2.0 / (double) (u32) (p7 - p5));
        startPhase = (u32) (p5 + (u32) (p7 - p5) / 2 + 1);
    }

    double curveK (double x) const noexcept
    {
        if (k == 0.0f)
            return x;
        if (k > 0.0f)
            return 1.0 - (dmath::exp ((1.0 - x) * (double) L) - 1.0) * (double) ik;
        return (dmath::exp (x * (double) L) - 1.0) * (double) ik;
    }

    float value (u32 u) const noexcept
    {
        float v;
        if (u > p7)
            return 1.0f;
        if (u < p6)
            v = 1.0f - (float) u * s0;
        else if (u < p5)
            return -1.0f;
        else
            v = (float) (u32) (u - p5) * s1 - 1.0f;
        if (! uni)
        {
            const auto r = F (curveK (std::abs ((double) v)));
            return std::signbit (v) ? -r : r;
        }
        const float x = (v + 1.0f) * 0.5f;
        return F (curveK ((double) x) * 2.0 - 1.0);
    }
};
