// --- FL Studio Mobile (.flm), round 189h: Pocket MultiFX runs in chunks of one song tick (1/96 beat) on the song's grid,
// as FLM does inside FL (A/B in REA_rodada182c.md "F5 effects, round 189h").

namespace flm189h
{
// HP Filter (kind 3), X 0.5, square LFO of one beat at full amount; a 1 kHz tone at 48 kHz, 120 BPM (a tick = 250 samples);
// the host calls with blocks of `block` frames (0 = one call per tick) and gives the song position before each call
inline std::vector<float> squareHp (int block)
{
    using namespace lynx;
    auto fx = makeEffect (EffectKind::pocketMultiFx);
    fx->prepare (48000.0);
    auto p = makeEffectSlot (EffectKind::pocketMultiFx).params;
    p[0] = 3.0f / 7.0f + 1e-4f;
    p[1] = 0.5f;
    p[2] = 0.5f;
    p[3] = 0.61f;
    p[4] = 1.0f;
    p[5] = 1.0f;
    std::vector<float> l (30000), r;
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = 0.5f * (float) std::sin (6.283185307179586 * 1000.0 * (double) i / 48000.0);
    r = l;
    for (int at = 0; at < (int) l.size();)
    {
        const auto n = std::min (block > 0 ? block : 250, (int) l.size() - at);
        fx->setSongPosition ((double) at * 0.04); // LYNX ticks (960 a beat): 24000 samples a beat
        fx->process (l.data() + at, r.data() + at, n, p, 120.0);
        at += n;
    }
    return l;
}
inline float peak (const std::vector<float>& x, int from, int to)
{
    float m = 0.0f;
    for (int i = from; i < to; ++i)
        m = std::max (m, std::abs (x[(std::size_t) i]));
    return m;
}
} // namespace flm189h

TEST (flm189h_pocket_multifx_tick_chunks)
{
    // ph ≤ ½ → LFO +1 → the HP at 5.5 kHz silences the tone; ph > ½ → −1 → 32 Hz lets it through. At the half beat (12000)
    // the chunk starts on ph = ½ exactly, so the switch waits that chunk: the tone comes back at 12250, not at 12000
    const auto ticks = flm189h::squareHp (0);
    CHECK (flm189h::peak (ticks, 2000, 12250) < 1e-3f);
    CHECK (flm189h::peak (ticks, 12300, 24000) > 0.4f);
    CHECK (flm189h::peak (ticks, 24300, 30000) < 1e-3f); // the next beat switches back on its tick
    // the host's block size doesn't change a sample: a chunk that a block cuts goes on with its LFO value
    for (const int block : { 64, 100, 256, 1000, 4096 })
        CHECK (flm189h::squareHp (block) == ticks);
}
