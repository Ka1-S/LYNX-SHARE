#include "core/AutoWarp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "core/TempoDetect.h"
#include "core/TimeStretch.h"

namespace lynx::autowarp
{
namespace
{
// The attacks (seconds): core/TimeStretch's detector, the one whose attacks match Live's analysis files best on drums
std::vector<double> attacksOf (const std::vector<float>& mono, double sampleRate)
{
    std::vector<double> out;
    for (const auto a : stretchdetail::detectAttacks (mono.data(), (long) mono.size(), sampleRate, 6.0))
        out.push_back ((double) a / sampleRate);
    return out;
}

// The tempo: known (the file name: Live's tempo for 148 of 156 named Core Library loops), else a loop up to a minute
// is taken as a power of two of beats, the one nearest the song's tempo (Live: 33 of 47 unnamed loops; LYNX's own
// tempo detection matched only 8), else (a long recording) detected [measured, /mnt/e/lynx-work/ableton175a/m2/warp.txt]
double tempoOf (const SampleData& s, const Options& o, double seconds, bool& fromLength)
{
    fromLength = false;
    if (o.knownBpm > 0.0)
        return o.knownBpm;
    const auto song = std::clamp (o.songBpm, 20.0, 400.0);
    const auto fit = [&] {
        fromLength = true;
        const auto beats = std::exp2 (std::round (std::log2 (std::max (1.0, seconds * song / 60.0))));
        return beats * 60.0 / seconds;
    };
    if (seconds <= 60.0)
        return fit();
    tempodetect::Options d;
    d.snapToLength = false;
    const auto r = tempodetect::detect (s.leftData(), s.isStereo() ? s.rightData() : nullptr, s.frames(), s.sampleRate, d);
    return r.bpm > 0.0 ? r.bpm : fit();
}
} // namespace

Result analyse (const SampleData& s, const Options& o)
{
    Result r;
    const auto frames = (long) s.frames();
    if (frames < 16 || ! (s.sampleRate > 0.0))
        return r;
    const auto seconds = (double) frames / s.sampleRate;
    std::vector<float> mono (s.leftData(), s.leftData() + frames);
    if (s.isStereo())
        for (long i = 0; i < frames; ++i)
            mono[(std::size_t) i] = 0.5f * (mono[(std::size_t) i] + s.rightData()[i]);
    const auto attacks = attacksOf (mono, s.sampleRate);

    bool fromLength = false;
    r.bpm = tempoOf (s, o, seconds, fromLength);
    if (! (r.bpm > 0.0))
        return r;
    const auto beat = 60.0 / r.bpm;
    const auto bar = std::max (1.0, o.beatsPerBar) * beat;

    // beat 0: the file's start (Live: 195 of 203 Core Library loops; starting on the first attack instead matched 144)
    // a loop (up to a minute: Live's loops get two markers too) or a short recording
    if (fromLength || seconds <= 60.0 || seconds / bar < o.trackBars)
    {
        // a loop, one tempo: a marker at the start and one on the last whole beat in the file
        const auto beats = std::floor (seconds / beat + 1.0e-6);
        if (beats >= 1.0)
            r.markers = { { 0.0, 0.0 }, { beats * beat, beats } };
        return r;
    }

    // a long recording: beat 0 near its first attack when it starts with silence (Live's vocal demo: 0.686 s), else
    // the start; then a marker on the first beat of every bar, on the beats tracked through the whole recording (a
    // performance that drifts stays on the grid)
    r.tracked = true;
    double peak = 0.0;
    for (const auto v : mono)
        peak = std::max (peak, (double) std::abs (v));
    if (! attacks.empty())
    {
        const auto first = std::min (frames, (long) ((attacks.front() - 0.002) * s.sampleRate));
        double before = 0.0;
        for (long i = 0; i < first; ++i)
            before = std::max (before, (double) std::abs (mono[(std::size_t) i]));
        if (before < 0.01 * peak)
            r.firstBeat = attacks.front();
    }
    // (onsetCurve rounds its hop to whole frames: 220 at 44.1 kHz, not 220.5 — timing beats with 0.005 s ran 82 ms
    // early after 36 s)
    const auto hop = (double) std::max<long long> (16, std::llround (0.005 * s.sampleRate)) / s.sampleRate;
    const auto curve = tempodetect::onsetCurve (s.leftData(), s.isStereo() ? s.rightData() : nullptr, s.frames(), s.sampleRate, hop);
    // the beats: dynamic programming over the onset curve (each beat = its onset strength + the best earlier beat,
    // less a penalty for an interval away from the tempo; Ellis 2007), traced back from the strongest end
    const auto n = (long) curve.size();
    const auto tau = beat / hop;
    std::vector<double> score ((std::size_t) n, 0.0);
    std::vector<long> from ((std::size_t) n, -1);
    double norm = 0.0;
    for (const auto v : curve)
        norm = std::max (norm, (double) v);
    norm = norm > 0.0 ? 1.0 / norm : 1.0;
    constexpr double tightness = 100.0;
    for (long i = 0; i < n; ++i)
    {
        double best = 0.0;
        long arg = -1;
        for (auto j = i - (long) std::ceil (2.0 * tau); j <= i - (long) std::floor (0.5 * tau); ++j)
        {
            if (j < 0)
                continue;
            const auto lg = std::log ((double) (i - j) / tau);
            const auto v = score[(std::size_t) j] - tightness * lg * lg;
            if (arg < 0 || v > best)
            {
                best = v;
                arg = j;
            }
        }
        score[(std::size_t) i] = (double) curve[(std::size_t) i] * norm + (arg >= 0 ? best : 0.0);
        from[(std::size_t) i] = arg;
    }
    long end = n - 1;
    for (auto i = std::max (0L, n - (long) std::ceil (tau)); i < n; ++i)
        if (score[(std::size_t) i] > score[(std::size_t) end])
            end = i;
    std::vector<double> beats;
    for (auto i = end; i >= 0; i = from[(std::size_t) i])
        beats.push_back ((double) i * hop);
    std::reverse (beats.begin(), beats.end());
    const auto perBar = std::max (1, (int) std::lround (o.beatsPerBar));
    // beat 0 = the tracked beat nearest the first beat; a marker on every bar's first beat from there
    std::size_t zero = 0;
    for (std::size_t k = 0; k < beats.size(); ++k)
        if (std::abs (beats[k] - r.firstBeat) < std::abs (beats[zero] - r.firstBeat))
            zero = k;
    if (! beats.empty())
        r.firstBeat = beats[zero];
    // each marker then moves onto the attack within a sixth of a beat, if any (the onset curve's peaks are broad: on
    // their own the beats ran up to 50 ms early on a drifting test performance)
    std::size_t next = 0;
    auto local = beat;
    for (auto k = zero; k < beats.size(); k += (std::size_t) perBar)
    {
        auto t = beats[k];
        while (next < attacks.size() && attacks[next] < t - beat / 6.0)
            ++next;
        double bestD = beat / 6.0;
        for (auto a = next; a < attacks.size() && attacks[a] <= t + beat / 6.0; ++a)
            if (std::abs (attacks[a] - t) < bestD)
            {
                bestD = std::abs (attacks[a] - t);
                t = attacks[a];
            }
        // its beat: counted from the previous marker at the tempo found so far (a beat the tracking added or dropped
        // must not shift every later marker by a beat)
        if (r.markers.empty())
            r.markers.push_back ({ t, 0.0 });
        else if (t > r.markers.back().seconds + 0.5 * beat)
        {
            const auto steps = std::max (1.0, std::round ((t - r.markers.back().seconds) / local));
            r.markers.push_back ({ t, r.markers.back().beats + steps });
            local = std::clamp (0.7 * local + 0.3 * (t - r.markers[r.markers.size() - 2].seconds) / steps, 0.92 * beat, 1.08 * beat);
        }
    }
    if (r.markers.size() < 2)
    {
        const auto beats = std::floor ((seconds - r.firstBeat) / beat + 1.0e-6);
        if (beats >= 1.0)
            r.markers.push_back ({ r.firstBeat + beats * beat, beats });
    }
    return r;
}

double bpmInName (const std::string& name)
{
    std::string s;
    for (const auto c : name)
        s += (char) std::tolower ((unsigned char) c);
    for (auto at = s.find ("bpm"); at != std::string::npos; at = s.find ("bpm", at + 3))
    {
        // the number right before "bpm" (a space, '_' or '-' allowed between)
        auto end = at;
        while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '_' || s[end - 1] == '-'))
            --end;
        auto begin = end;
        while (begin > 0 && (std::isdigit ((unsigned char) s[begin - 1]) || s[begin - 1] == '.'))
            --begin;
        if (begin == end)
            continue;
        const auto bpm = std::atof (s.substr (begin, end - begin).c_str());
        if (bpm >= 40.0 && bpm <= 300.0)
            return bpm;
    }
    return 0.0;
}
} // namespace lynx::autowarp
