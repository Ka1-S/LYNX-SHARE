#include "core/NewtoneFx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "core/AudioEdit.h"
#include "core/Instruments.h"
#include "core/Scheduler.h"
#include "core/VocalPitch.h"

namespace lynx::newtonefx
{
namespace
{
std::int32_t i32 (const std::uint8_t* p) noexcept
{
    return (std::int32_t) ((std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24));
}

std::int64_t i64 (const std::uint8_t* p) noexcept
{
    return (std::int64_t) ((std::uint64_t) (std::uint32_t) i32 (p) | ((std::uint64_t) (std::uint32_t) i32 (p + 4) << 32));
}

// value of attribute `name` in the XML element starting at `tag` (up to its '>'); nullopt = absent
std::optional<std::string> attribute (const std::string& xml, std::size_t tag, const char* name)
{
    const auto end = xml.find ('>', tag);
    const auto key = std::string (" ") + name + "=\"";
    const auto at = xml.find (key, tag);

    if (at == std::string::npos || (end != std::string::npos && at > end))
        return std::nullopt;

    const auto from = at + key.size();
    const auto to = xml.find ('"', from);

    if (to == std::string::npos)
        return std::nullopt;

    // the five XML entities JUCE writes
    std::string v;
    for (auto i = from; i < to; ++i)
    {
        if (xml[i] == '&')
        {
            static const std::pair<const char*, char> entities[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
            bool done = false;
            for (const auto& [e, c] : entities)
                if (xml.compare (i, std::strlen (e), e) == 0)
                {
                    v += c;
                    i += std::strlen (e) - 1;
                    done = true;
                    break;
                }
            if (done)
                continue;
        }
        v += xml[i];
    }
    return v;
}

double number (const std::string& xml, std::size_t tag, const char* name, double fallback)
{
    const auto v = attribute (xml, tag, name);
    if (! v)
        return fallback;
    char* end = nullptr;
    const auto d = std::strtod (v->c_str(), &end);
    return end != v->c_str() ? d : fallback;
}

// the element names that start with `prefix` + digits, from `from` up to `limit`
std::vector<std::size_t> elements (const std::string& xml, const std::string& prefix, std::size_t from, std::size_t limit)
{
    std::vector<std::size_t> out;
    const auto open = "<" + prefix;

    for (auto at = xml.find (open, from); at != std::string::npos && at < limit; at = xml.find (open, at + 1))
        if (at + open.size() < xml.size() && std::isdigit ((unsigned char) xml[at + open.size()]))
            out.push_back (at);

    return out;
}

bool differs (double a, double b) noexcept { return std::abs (a - b) > 1e-6 * std::max (1.0, std::abs (b)); }

// a note (pitch object) the user changed: pitch, timing, length, volume, formant, drift, vibrato/tremolo added
bool noteEdited (const std::string& xml, std::size_t tag)
{
    const auto n = [&] (const char* name, double fallback) { return number (xml, tag, name, fallback); };

    return differs (n ("fCurPitch", 0.0), n ("fOrigPitch", 0.0)) || differs (n ("iCurLength", 0.0), n ("iOrigLength", 0.0))
           || differs (n ("iCurStartSamplePos", 0.0), n ("iOrigStartSamplePos", 0.0)) || differs (n ("fCurVolume", 1.0), 1.0)
           || differs (n ("fCurFormantFactor", 1.0), 1.0) || differs (n ("fCurrentPitchOffset", 0.0), 0.0)
           || differs (n ("fAdditionalPitchOffset", 0.0), 0.0) || differs (n ("fDrift", 1.0), 1.0)
           || differs (n ("fCurInvStretchFactor", 1.0), 1.0) || differs (n ("fTremAmp", 0.0), 0.0)
           || differs (n ("fVibAmpStart", 0.0), 0.0) || differs (n ("fTimeVariationOffset", 0.0), 0.0)
           || differs (n ("fRampIn", 0.0), 0.0) || differs (n ("fRampOut", 0.0), 0.0)
           || n ("bIsEnabled", 1.0) == 0.0;
}

bool globalEdited (const std::string& xml, std::size_t tag)
{
    const auto n = [&] (const char* name, double fallback) { return number (xml, tag, name, fallback); };

    return differs (n ("GlobalTune", 0.0), 0.0) || differs (n ("GlobalDrift", 1.0), 1.0) || differs (n ("GlobalFormantShift", 1.0), 1.0)
           || differs (n ("GlobalTimeVariation", 0.0), 0.0) || differs (n ("GlobalTimeQuantize", 0.0), 0.0)
           || n ("EnableTempoAdaption", 0.0) != 0.0 || differs (n ("PlaybackOffset", 0.0), 0.0);
}

void readTracks (State& s)
{
    const auto& xml = s.projectXml;
    const auto tracks = xml.find ("<Tracks");

    if (tracks == std::string::npos)
        return;

    const auto trackTags = elements (xml, "Trk_", tracks, xml.size());

    for (std::size_t k = 0; k < trackTags.size(); ++k)
    {
        const auto tag = trackTags[k];
        const auto limit = k + 1 < trackTags.size() ? trackTags[k + 1] : xml.size();
        Track t;
        t.number = (int) number (xml, tag, "TrackNum", (double) k);
        t.timeWarp = attribute (xml, tag, "Type").value_or ("").find ("TimelineWarp") != std::string::npos;
        t.filePath = attribute (xml, tag, "FilePath").value_or ("");

        if (const auto object = xml.find ("<AudioObject", tag); object != std::string::npos && object < limit)
        {
            t.frames = (std::int64_t) number (xml, object, "OriginalFileLength", 0.0);
            t.selfBpm = number (xml, object, "SelfBPM", 0.0);
            t.edited = globalEdited (xml, object);
            t.globalFormant = number (xml, object, "GlobalFormantShift", 1.0);
            t.key = (int) number (xml, object, "KeyIdx", -1.0);

            for (const auto note : elements (xml, "Pitch_", object, limit))
            {
                ++t.notes;
                t.edited = t.edited || noteEdited (xml, note);
                const auto v = [&] (const char* name, double fallback) { return number (xml, note, name, fallback); };
                Note p;
                p.origStart = (std::int64_t) v ("iOrigStartSamplePos", 0.0);
                p.origEnd = (std::int64_t) v ("iOrigEndSamplePos", (double) p.origStart);
                p.curStart = (std::int64_t) v ("iCurStartSamplePos", (double) p.origStart);
                p.curEnd = (std::int64_t) v ("iCurEndSamplePos", (double) p.origEnd);
                p.origPitch = v ("fOrigPitch", 0.0);
                p.shift = v ("fCurrentPitchOffset", 0.0) + v ("fAdditionalPitchOffset", 0.0);
                p.stretch = v ("fCurInvStretchFactor", 1.0);
                p.volume = v ("fCurVolume", 1.0);
                p.formant = v ("fCurFormantFactor", 1.0);
                p.drift = v ("fDrift", 1.0);
                p.smoothIn = v ("fPitchSmoothInTime", 0.0);
                p.smoothOut = v ("fPitchSmoothOutTime", 0.0);
                p.origSmoothIn = v ("fOrigPitchSmoothInTime", p.smoothIn);
                p.origSmoothOut = v ("fOrigPitchSmoothOutTime", p.smoothOut);
                p.fadeInTime = v ("fVolumeFadeInTime", 0.0);
                p.fadeOutTime = v ("fVolumeFadeOutTime", 0.0);
                p.fadeInGain = v ("fVolumeFadeInGain", 1.0);
                p.fadeOutGain = v ("fVolumeFadeOutGain", 1.0);
                p.vibPeriod = v ("fVibFreq", 0.0);
                p.vibAmpStart = v ("fVibAmpStart", 0.0);
                p.vibAmpEnd = v ("fVibAmpEnd", 0.0);
                p.tremAmp = v ("fTremAmp", 0.0);
                p.rampIn = v ("fRampIn", 0.0);
                p.rampOut = v ("fRampOut", 0.0);
                p.enabled = v ("bIsEnabled", 1.0) != 0.0;
                p.validEnd = (std::int64_t) v ("iOrigValidNoteEnd", -1.0);
                p.velocity = v ("fOrigVelocity", -1.0);
                if (! std::isfinite (p.stretch) || p.stretch <= 0.0)
                    p.stretch = 1.0;
                t.pitches.push_back (p);
            }
        }

        s.tracks.push_back (std::move (t));
    }
}

// Record 2: FL's VST-wrapper record around the engine's chunk, which is JUCE's binary XML ("VC2!", u32 size, text)
std::string projectXmlOf (const std::uint8_t* d, std::size_t n)
{
    static const std::uint8_t magic[] = { 'V', 'C', '2', '!' };

    for (std::size_t i = 0; i + 8 <= n; ++i)
        if (std::memcmp (d + i, magic, 4) == 0)
        {
            const auto size = (std::size_t) (std::uint32_t) i32 (d + i + 4);
            const auto from = i + 8;
            auto len = std::min (size, n - from);
            while (len > 0 && d[from + len - 1] == 0)
                --len;
            return std::string ((const char*) d + from, len);
        }

    return {};
}

class Engine final : public Effect
{
public:
    explicit Engine (std::shared_ptr<const SampleData> s) : source (std::move (s)) {}

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept override
    {
        engaged = false;
        position = 0;
    }

    void setSongPosition (double t) noexcept override { ticks = t; }
    void setPlaying (bool p) noexcept override { playing = p; }

    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        // FL: the engine's transport follows the host only in slave mode (SetPlaying / SongPosChanged), §3.2
        if (params[0] < 0.5f || ! playing || source == nullptr || source->frames() == 0 || bpm <= 0.0)
        {
            engaged = false;
            return;
        }

        const auto tps = ticksPerSample (bpm, sampleRate);

        // play start or a jump: the position comes from the song time (ms × 0.001 × rate in FL)
        if (! engaged || std::abs (ticks - expectedTicks) > 1.0)
            position = (std::int64_t) std::llround (ticks / tps);

        engaged = true;
        expectedTicks = ticks + n * tps;

        const auto frames = (std::int64_t) source->frames();
        const auto* sl = source->leftData();
        const auto* sr = source->rightData();
        const auto ratio = source->sampleRate / sampleRate;

        if (std::abs (ratio - 1.0) < 1e-12)
        {
            // the source's own rate: its samples are added as they are (REA_rodada73 §4, EXEC)
            for (int k = 0; k < n; ++k)
            {
                const auto p = position + k;
                if (p < 0 || p >= frames)
                    continue;
                left[k] += sl[p];
                right[k] += sr[p];
            }
        }
        else
        {
            // another rate: FL's engine output is not a plain resampling (§5); LYNX resamples linearly
            for (int k = 0; k < n; ++k)
            {
                const auto x = (double) (position + k) * ratio;
                const auto p = (std::int64_t) std::floor (x);
                if (p < 0 || p >= frames)
                    continue;
                const auto f = (float) (x - (double) p);
                const auto q = std::min (p + 1, frames - 1);
                left[k] += sl[p] + (sl[q] - sl[p]) * f;
                right[k] += sr[p] + (sr[q] - sr[p]) * f;
            }
        }

        position += n;
    }

private:
    std::shared_ptr<const SampleData> source;
    double sampleRate = 44100.0, ticks = 0.0, expectedTicks = 0.0;
    std::int64_t position = 0;
    bool playing = false, engaged = false;
};
} // namespace

bool State::edited() const noexcept
{
    return std::any_of (tracks.begin(), tracks.end(), [] (const Track& t) { return t.edited; });
}

bool Note::edited (double globalFormant) const noexcept
{
    return differs (shift, 0.0) || differs (drift, 1.0) || differs (stretch, 1.0) || curStart != origStart
           || differs (volume, 1.0) || differs (formant * globalFormant, 1.0) || ! enabled || vibPeriod > 0.0
           || differs (tremAmp, 0.0) || differs (fadeInGain, 1.0) || differs (fadeOutGain, 1.0) || differs (rampIn, 0.0)
           || differs (rampOut, 0.0) || differs (modulation, 1.0) || differs (pitchDrift, 1.0);
}

namespace
{
constexpr double twoPi = 6.283185307179586;

// Analysis marks of the source (LYNX's own analysis, not Newtone's): one per period where voiced, locked to the
// peaks of a low-passed copy; every `unvoicedHop` samples elsewhere
struct Mark
{
    double pos = 0.0, period = 0.0, note = 0.0;
    bool voiced = false;
};

std::vector<Mark> analyse (const std::vector<float>& x, double fs, std::vector<vocal::PitchFrame>& frames, int hop, int unvoicedHop)
{
    const auto n = (int) x.size();
    frames = vocal::analysePitch (x.data(), n, fs, hop, 60.0, 1000.0);
    std::vector<float> env (x.size());
    const auto a = (float) (1.0 - std::exp (-twoPi * 900.0 / fs));
    float z = 0.0f;
    for (int i = 0; i < n; ++i)
        env[(std::size_t) i] = z += a * (x[(std::size_t) i] - z);

    std::vector<Mark> marks;
    double pos = 0.0;
    while (pos < n)
    {
        const auto f = std::min (frames.size() - 1, (std::size_t) std::lround (pos / hop));
        const auto hz = frames.empty() ? 0.0 : frames[f].hz;
        if (hz > 0.0)
        {
            const auto p = fs / hz;
            const auto from = std::max (0, (int) std::ceil (pos - 0.25 * p)), to = std::min (n - 1, (int) std::floor (pos + 0.25 * p));
            auto best = std::clamp ((int) std::lround (pos), 0, n - 1);
            for (int i = from; i <= to; ++i)
                if (env[(std::size_t) i] - 1e-4f * (float) std::abs (i - pos) / (float) p > env[(std::size_t) best] - 1e-4f * (float) std::abs (best - pos) / (float) p)
                    best = i;
            if (! marks.empty() && best <= marks.back().pos)
                best = (int) std::ceil (marks.back().pos + 1.0);
            marks.push_back ({ (double) best, p, vocal::hzToNote (hz), true });
            pos = best + p;
        }
        else
        {
            marks.push_back ({ pos, (double) unvoicedHop, 0.0, false });
            pos += unvoicedHop;
        }
    }
    return marks;
}

// Round 190li (engine 1): the voiced marks moved to where the waveform best repeats the one before (normalised
// cross-correlation over a period, within 0.15 of a period of the peak-locked mark; whole samples, so an unshifted
// render stays the source itself). A run's first mark keeps its peak. Consistent marks give consistent grains: the
// cycle-to-cycle jitter of peak picking would otherwise become roughness between the harmonics.
void refineMarks (std::vector<Mark>& marks, const std::vector<float>& x)
{
    const auto n = (std::ptrdiff_t) x.size();
    auto ncc = [&] (std::ptrdiff_t a, std::ptrdiff_t b, std::ptrdiff_t half) {
        double d = 0.0, ea = 0.0, eb = 0.0;
        for (auto o = -half; o < half; ++o)
        {
            const auto ia = a + o, ib = b + o;
            const double va = ia >= 0 && ia < n ? x[(std::size_t) ia] : 0.0, vb = ib >= 0 && ib < n ? x[(std::size_t) ib] : 0.0;
            d += va * vb, ea += va * va, eb += vb * vb;
        }
        return ea * eb > 1e-24 ? d / std::sqrt (ea * eb) : 0.0;
    };
    for (std::size_t i = 1; i < marks.size(); ++i)
    {
        auto& m = marks[i];
        const auto& prev = marks[i - 1];
        if (! m.voiced || ! prev.voiced)
            continue;
        const auto p = m.period;
        // (around the peak-locked mark itself: the frame's period, smeared over a glide or wrong at an onset, does not
        // steer the search)
        const auto guess = (std::ptrdiff_t) std::llround (m.pos);
        const auto reach = std::max<std::ptrdiff_t> (1, (std::ptrdiff_t) std::lround (0.15 * p));
        const auto half = std::max<std::ptrdiff_t> (4, (std::ptrdiff_t) std::lround (0.5 * p));
        auto best = (std::ptrdiff_t) m.pos;
        auto bestScore = ncc ((std::ptrdiff_t) prev.pos, best, half);
        for (auto c = guess - reach; c <= guess + reach; ++c)
        {
            if ((double) c <= prev.pos + 0.5 * p)
                continue;
            if (const auto sc = ncc ((std::ptrdiff_t) prev.pos, c, half); sc > bestScore + 1e-9)
                bestScore = sc, best = c;
        }
        m.pos = (double) best;
    }
    // (keep them strictly increasing)
    for (std::size_t i = 1; i < marks.size(); ++i)
        if (marks[i].pos <= marks[i - 1].pos)
            marks[i].pos = marks[i - 1].pos + 1.0;

    // Voiced runs reach back to the voice's real onset and on to its end: the pitch frames switch on ~20 ms late (their
    // window must fill with periods first), so the start of a sung note was read unshifted. From a run's first mark,
    // periods are walked backwards (and from its last one forwards) while the waveform still repeats (normalised
    // correlation over a period >= 0.7) and carries energy; the unvoiced marks they cover go.
    std::vector<Mark> out;
    out.reserve (marks.size() + 64);
    std::size_t i = 0;
    while (i < marks.size())
    {
        if (! marks[i].voiced)
        {
            out.push_back (marks[i++]);
            continue;
        }
        auto j = i;
        while (j + 1 < marks.size() && marks[j + 1].voiced)
            ++j;
        // backwards from marks[i]
        std::vector<Mark> head;
        {
            auto at = marks[i];
            const auto floorPos = out.empty() ? 0.0 : out.back().pos; // (never before what is already placed... see below)
            (void) floorPos;
            for (int step = 0; step < 8; ++step)
            {
                const auto p = at.period;
                const auto guess = (std::ptrdiff_t) std::llround (at.pos - p);
                const auto reach = std::max<std::ptrdiff_t> (1, (std::ptrdiff_t) std::lround (0.15 * p));
                const auto half = std::max<std::ptrdiff_t> (4, (std::ptrdiff_t) std::lround (0.5 * p));
                auto best = guess;
                auto bestScore = -2.0;
                for (auto c = guess - reach; c <= guess + reach; ++c)
                    if (const auto sc = ncc ((std::ptrdiff_t) at.pos, c, half); sc > bestScore)
                        bestScore = sc, best = c;
                // (the voiced run before, or an earlier voiced mark, ends the walk; so does a waveform that no longer repeats)
                const auto lastVoiced = [&] {
                    for (auto k = out.size(); k-- > 0;)
                        if (out[k].voiced)
                            return out[k].pos;
                    return -1.0;
                }();
                if (bestScore < 0.7 || best <= 1 || (double) best <= lastVoiced + 0.5 * p)
                    break;
                at.pos = (double) best;
                head.push_back (at);
            }
        }
        // drop the unvoiced marks the head covers, keep it in order
        if (! head.empty())
        {
            const auto from = head.back().pos - 0.5 * head.back().period;
            while (! out.empty() && ! out.back().voiced && out.back().pos >= from)
                out.pop_back();
            for (auto h = head.rbegin(); h != head.rend(); ++h)
                out.push_back (*h);
        }
        for (auto k = i; k <= j; ++k)
            out.push_back (marks[k]);
        // forwards from marks[j]
        auto at = marks[j];
        std::size_t next = j + 1;
        for (int step = 0; step < 8; ++step)
        {
            const auto p = at.period;
            const auto guess = (std::ptrdiff_t) std::llround (at.pos + p);
            const auto reach = std::max<std::ptrdiff_t> (1, (std::ptrdiff_t) std::lround (0.15 * p));
            const auto half = std::max<std::ptrdiff_t> (4, (std::ptrdiff_t) std::lround (0.5 * p));
            auto best = guess;
            auto bestScore = -2.0;
            for (auto c = guess - reach; c <= guess + reach; ++c)
                if (const auto sc = ncc ((std::ptrdiff_t) at.pos, c, half); sc > bestScore)
                    bestScore = sc, best = c;
            // (stop before the next voiced run)
            auto k = next;
            while (k < marks.size() && ! marks[k].voiced)
                ++k;
            const auto nextVoiced = k < marks.size() ? marks[k].pos : (double) n;
            if (bestScore < 0.7 || best + half >= n || (double) best >= nextVoiced - 0.5 * p)
                break;
            at.pos = (double) best;
            out.push_back (at);
            while (next < marks.size() && ! marks[next].voiced && marks[next].pos <= at.pos + 0.5 * p)
                ++next; // (the unvoiced marks it covers)
        }
        i = next;
    }
    marks.swap (out);
}

float readAt (const std::vector<float>& x, double pos) noexcept
{
    const auto i = (std::int64_t) std::floor (pos);
    const auto n = (std::int64_t) x.size();
    if (i < 1 || i + 2 >= n)
        return 0.0f;
    const auto t = (float) (pos - (double) i);
    const auto y0 = x[(std::size_t) i - 1], y1 = x[(std::size_t) i], y2 = x[(std::size_t) i + 1], y3 = x[(std::size_t) i + 2];
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
} // namespace

// ------------------------------------------------------------------------------------------- round 190li: engine 1
// LYNX's natural engine (Track::engine 1), scored against Melodyne 5 with tools/vocallab (docs/analise/REA_rodada190li.md).
// The same notes and edits as engine 0, rendered with textbook methods only:
//  - TD-PSOLA with asymmetric grains: every analysis mark's window reaches back to the mark before and on to the mark
//    after (raised-cosine halves), so at no shift the windows add up to exactly one and an unshifted part is the source
//    itself (seamless joins with the plain sample, no phase jumps); synthesis marks start on the analysis marks;
//  - pitch marks refined by normalised cross-correlation with the mark before (each period cut at the same phase of
//    the waveform; peak picking's cycle-to-cycle jitter was the main source of roughness between the harmonics, 5-10 dB);
//  - grain interpolation (waveform interpolation, W. B. Kleijn, "Encoding speech using prototype waveforms", IEEE
//    Trans. Speech Audio Proc. 1(4), 1993): a synthesis mark mixes the analysis grains around its source time with a
//    triangle one mark wide (1/ratio marks when shifting down, an anti-alias filter on the sequence of periods), so a
//    shift repeats or skips no single period;
//  - overlap normalised where grains overlap (upward shifts), the loudness kept: the rendered voice follows the
//    source's level envelope (30 ms RMS) instead of PSOLA's pulse-rate law (+/- 3 dB at a fifth);
//  - unvoiced marks (consonants, breaths, silence) are never shifted or formant-moved: they are read 1:1;
//  - note transitions: a moved note joins its neighbour through a raised-cosine blend of the two shifts centred on the
//    boundary (the analysed glide's length) instead of FL's step-and-ramp;
//  - the pitch modulation / drift split (Note::modulation, Note::pitchDrift): the sung deviation from the note's centre,
//    low-passed by two passes of a 180 ms moving average (a 5.5 Hz vibrato's period: it is removed), is the drift; the
//    rest is the modulation.
namespace
{
struct AnalysisCache
{
    std::uint64_t key = 0;
    std::int64_t frames = 0;
    std::vector<vocal::PitchFrame> pitch;
    std::vector<Mark> marks;
};

std::shared_ptr<SampleData> renderNatural (const SampleData& source, const Track& track)
{
    const auto n = (std::int64_t) source.frames();
    const auto& notes = track.pitches;
    const auto fs = source.sampleRate > 0.0 ? source.sampleRate : 44100.0;
    const auto stereo = source.isStereo();
    std::vector<float> in[2];
    in[0].assign (source.leftData(), source.leftData() + n);
    if (stereo)
        in[1].assign (source.rightData(), source.rightData() + n);
    std::vector<float> mono = in[0];
    if (stereo)
        for (std::int64_t i = 0; i < n; ++i)
            mono[(std::size_t) i] = 0.5f * (in[0][(std::size_t) i] + in[1][(std::size_t) i]);

    const int hop = 128, unvoicedHop = std::max (16, (int) std::lround (128.0 * fs / 44100.0));
    std::vector<vocal::PitchFrame> frames;
    // the analysis (pitch frames and marks: ~2/3 of a render's cost) is kept for the last sample rendered, so the
    // renders that follow each edit of the same sample skip it (the result is the same either way)
    std::vector<Mark> marks;
    {
        std::uint64_t key = 1469598103934665603ull;
        auto mix = [&key] (std::uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        mix ((std::uint64_t) n);
        mix ((std::uint64_t) std::llround (fs * 1000.0));
        mix ((std::uint64_t) (std::uintptr_t) source.leftData());
        mix ((std::uint64_t) (std::uintptr_t) (stereo ? source.rightData() : nullptr));
        const auto stride = std::max<std::int64_t> (1, n / 4096);
        for (std::int64_t i = 0; i < n; i += stride)
        {
            std::uint32_t bits = 0;
            std::memcpy (&bits, &mono[(std::size_t) i], 4);
            mix (bits);
        }
        static std::mutex lock;
        static std::shared_ptr<const AnalysisCache> last;
        std::shared_ptr<const AnalysisCache> hit;
        {
            const std::lock_guard<std::mutex> guard (lock);
            if (last != nullptr && last->key == key && last->frames == n)
                hit = last;
        }
        if (hit != nullptr)
        {
            frames = hit->pitch;
            marks = hit->marks;
        }
        else
        {
            marks = analyse (mono, fs, frames, hop, unvoicedHop);
            refineMarks (marks, mono);
            auto entry = std::make_shared<AnalysisCache>();
            entry->key = key;
            entry->frames = n;
            entry->pitch = frames;
            entry->marks = marks;
            const std::lock_guard<std::mutex> guard (lock);
            last = std::move (entry);
        }
    }
    if (marks.empty() || frames.empty())
        return nullptr;
    const auto frameOf = [&] (double pos) {
        return std::min (frames.size() - 1, (std::size_t) std::max<double> (0.0, std::round (pos / hop)));
    };
    auto voicedAt = [&] (double pos) { return ! frames.empty() && frames[frameOf (pos)].hz > 0.0; };

    // each note's centre (its mean sung pitch) and, per frame of it, the slow part of its deviation (the drift)
    std::vector<double> centre (notes.size(), 0.0);
    std::vector<double> slow (frames.size(), 0.0), fine (frames.size(), 0.0); // (fine: the deviation without the
                                                                                // pitch track's frame-to-frame noise)
    {
        const auto reach = std::max (1, (int) std::lround (0.09 * fs / hop)); // ±90 ms: a 180 ms moving average
        std::vector<double> dev, w, tmp;
        for (std::size_t k = 0; k < notes.size(); ++k)
        {
            const auto f0 = std::max<std::int64_t> (0, notes[k].origStart / hop);
            const auto f1 = std::min<std::int64_t> ((std::int64_t) frames.size(), notes[k].origEnd / hop + 1);
            double sum = 0.0;
            int count = 0;
            for (auto f = f0; f < f1; ++f)
                if (frames[(std::size_t) f].hz > 0.0)
                    sum += vocal::hzToNote (frames[(std::size_t) f].hz), ++count;
            centre[k] = notes[k].sungPitch > 0.0 ? notes[k].sungPitch : count > 0 ? sum / count : notes[k].origPitch;
            if (f1 <= f0)
                continue;
            const auto m = (std::size_t) (f1 - f0);
            dev.assign (m, 0.0);
            w.assign (m, 0.0);
            for (std::size_t i = 0; i < m; ++i)
                if (const auto hz = frames[(std::size_t) f0 + i].hz; hz > 0.0)
                    dev[i] = vocal::hzToNote (hz) - centre[k], w[i] = 1.0;
            // the deviation lightly smoothed (two passes of ±8 ms: a vibrato passes, the estimator's noise does not, so
            // scaling the modulation does not scale the noise into roughness)
            {
                const auto r8 = std::max (1, (int) std::lround (0.008 * fs / hop));
                auto d = dev;
                for (int pass = 0; pass < 2; ++pass)
                {
                    tmp.assign (m, 0.0);
                    for (std::size_t i = 0; i < m; ++i)
                    {
                        double s = 0.0, ws = 0.0;
                        for (auto j = (std::ptrdiff_t) i - r8; j <= (std::ptrdiff_t) i + r8; ++j)
                            if (j >= 0 && j < (std::ptrdiff_t) m && w[(std::size_t) j] > 0.0)
                                s += d[(std::size_t) j], ws += 1.0;
                        tmp[i] = ws > 0.0 ? s / ws : 0.0;
                    }
                    for (std::size_t i = 0; i < m; ++i)
                        if (w[i] > 0.0)
                            d[i] = tmp[i];
                }
                for (std::size_t i = 0; i < m; ++i)
                    fine[(std::size_t) f0 + i] = d[i];
            }
            // two passes of a moving average over the voiced frames (weights renormalised at the note's ends / gaps)
            for (int pass = 0; pass < 2; ++pass)
            {
                tmp.assign (m, 0.0);
                for (std::size_t i = 0; i < m; ++i)
                {
                    double s = 0.0, ws = 0.0;
                    for (auto j = (std::ptrdiff_t) i - reach; j <= (std::ptrdiff_t) i + reach; ++j)
                        if (j >= 0 && j < (std::ptrdiff_t) m && w[(std::size_t) j] > 0.0)
                            s += dev[(std::size_t) j], ws += 1.0;
                    tmp[i] = ws > 0.0 ? s / ws : 0.0;
                }
                for (std::size_t i = 0; i < m; ++i)
                    if (w[i] > 0.0)
                        dev[i] = tmp[i];
            }
            for (std::size_t i = 0; i < m; ++i)
                slow[(std::size_t) f0 + i] = dev[i];
        }
    }

    std::int64_t length = n;
    std::vector<std::pair<std::int64_t, std::int64_t>> owned (notes.size());
    std::int64_t covered = 0;
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto a = std::max (notes[k].curStart, covered), b = notes[k].curEnd;
        owned[k] = { a, std::max (a, b) };
        covered = std::max (covered, b);
        length = std::max (length, b);
    }
    auto adjacent = [&] (std::size_t j, std::size_t k) {
        return notes[j].curEnd == notes[k].curStart && voicedAt ((double) notes[k].origStart);
    };
    // the blend between note k-1 and k: its half-width (output samples) around their boundary
    auto joinHalf = [&] (std::size_t k) {
        const auto& p = notes[k];
        const auto lenA = (double) (notes[k - 1].curEnd - notes[k - 1].curStart), lenB = (double) (p.curEnd - p.curStart);
        return std::clamp (0.5 * std::max (p.smoothIn, 0.0), 0.01 * fs, std::max (0.01 * fs, 0.25 * std::min (lenA, lenB)));
    };
    auto joins = [&] (std::size_t k) { return k > 0 && k < notes.size() && adjacent (k - 1, k) && differs (notes[k - 1].shift, notes[k].shift); };
    auto blend = [] (double u) { return u <= 0.0 ? 0.0 : u >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos (3.141592653589793 * u); };

    // the shift (semitones) of note k at output sample t: its own, blended with a neighbour's across a join; ramps; vibrato
    auto shiftAt = [&] (std::size_t k, double t) {
        const auto& p = notes[k];
        auto s = p.shift;
        if (joins (k))
        {
            const auto h = joinHalf (k), b = (double) p.curStart;
            if (t < b + h)
                s = notes[k - 1].shift + (p.shift - notes[k - 1].shift) * blend ((t - (b - h)) / (2.0 * h));
        }
        if (joins (k + 1))
        {
            const auto h = joinHalf (k + 1), b = (double) p.curEnd;
            if (t >= b - h)
                s = p.shift + (notes[k + 1].shift - p.shift) * blend ((t - (b - h)) / (2.0 * h));
        }
        if (p.rampIn != 0.0 && p.smoothIn > 0.0 && t >= (double) p.curStart && t < (double) p.curStart + p.smoothIn)
            s += p.rampIn * (1.0 - (t - (double) p.curStart) / p.smoothIn);
        if (p.rampOut != 0.0 && p.smoothOut > 0.0 && t < (double) p.curEnd && t >= (double) p.curEnd - p.smoothOut)
            s += p.rampOut * (1.0 - ((double) p.curEnd - t) / p.smoothOut);
        if (p.vibPeriod > 0.0 && p.curEnd > p.curStart)
        {
            const auto u = (t - (double) p.curStart) / (double) (p.curEnd - p.curStart);
            const auto amp = p.vibAmpStart + (p.vibAmpEnd - p.vibAmpStart) * std::clamp (u, 0.0, 1.0);
            s += amp * std::sin (twoPi * (t - (double) p.curStart) / (p.vibPeriod * fs));
        }
        return s;
    };
    // the change of the sung deviation at source position `src` of note k (Variation, modulation, drift)
    auto deviationAt = [&] (std::size_t k, double src) {
        const auto& p = notes[k];
        if (! differs (p.drift, 1.0) && ! differs (p.modulation, 1.0) && ! differs (p.pitchDrift, 1.0))
            return 0.0;
        const auto f = frameOf (src);
        if (frames[f].hz <= 0.0)
            return 0.0;
        // (between frames: linear)
        const auto x = std::clamp (src / hop, 0.0, (double) (frames.size() - 1));
        const auto f0 = (std::size_t) std::floor (x), f1 = std::min (frames.size() - 1, f0 + 1);
        const auto a = x - (double) f0;
        auto noteOf = [&] (std::size_t i) { return frames[i].hz > 0.0 ? vocal::hzToNote (frames[i].hz) : vocal::hzToNote (frames[f].hz); };
        const auto dev = (1.0 - a) * noteOf (f0) + a * noteOf (f1) - centre[k];
        const auto sl = (1.0 - a) * slow[f0] + a * slow[f1];
        const auto fn = frames[f0].hz > 0.0 && frames[f1].hz > 0.0 ? (1.0 - a) * fine[f0] + a * fine[f1] : dev;
        // (the noise dev - fn stays as sung: it is not scaled with the modulation)
        return p.drift * (p.pitchDrift * sl + p.modulation * (fn - sl)) + (dev - fn) - dev;
    };

    auto out = std::make_shared<SampleData>();
    out->sampleRate = fs;
    out->left.assign ((std::size_t) length, 0.0f);
    if (stereo)
        out->right.assign ((std::size_t) length, 0.0f);
    float* const dst[2] { out->left.data(), stereo ? out->right.data() : out->left.data() };
    const auto channels = stereo ? 2 : 1;

    // the source's level envelope (30 ms RMS, mono): the rendered voice keeps it
    std::vector<float> level (mono.size());
    {
        const auto half = std::max<std::int64_t> (1, (std::int64_t) std::lround (0.015 * fs));
        std::vector<double> c (mono.size() + 1, 0.0);
        for (std::size_t i = 0; i < mono.size(); ++i)
            c[i + 1] = c[i] + (double) mono[i] * mono[i];
        for (std::int64_t i = 0; i < n; ++i)
        {
            const auto lo = std::max<std::int64_t> (0, i - half), hi = std::min (n, i + half);
            level[(std::size_t) i] = (float) std::sqrt ((c[(std::size_t) hi] - c[(std::size_t) lo]) / (double) std::max<std::int64_t> (1, hi - lo));
        }
    }

    // the note that owns output sample t (-1: none)
    auto ownerOf = [&] (double t) {
        const auto it = std::upper_bound (owned.begin(), owned.end(), t, [] (double v, const std::pair<std::int64_t, std::int64_t>& o) { return v < (double) o.first; });
        if (it == owned.begin())
            return -1;
        const auto k = (int) (it - owned.begin()) - 1;
        return t < (double) owned[(std::size_t) k].second ? k : -1;
    };
    // A stretched note holds its vowel (as PitchNet's NoteTimeMap, AGPL-3.0, SessionLoops/PitchNet, does; method
    // reimplemented): its onset glide (the analysed smooth-in) and its unvoiced tail (the silence / consonant after the
    // voice) keep their recorded length, the held part between takes all of the change; a note too short for that
    // stretches evenly. Per note: the head and tail lengths (source = output samples) and the body's rate.
    struct TimeMap
    {
        double head = 0.0, tail = 0.0, rate = 1.0; // rate = source samples per output sample in the body
        bool split = false;
    };
    std::vector<TimeMap> maps (notes.size());
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto& p = notes[k];
        auto& m = maps[k];
        m.rate = p.stretch;
        if (! differs (p.stretch, 1.0) || ! p.voiced())
            continue;
        const auto srcLen = (double) (p.origEnd - p.origStart), outLen = (double) (p.curEnd - p.curStart);
        const auto head = std::clamp (p.origSmoothIn, 0.0, 0.25 * srcLen);
        const auto tail = std::max (0.0, (double) (p.origEnd - p.voicedEnd()));
        const auto bodySrc = srcLen - head - tail, bodyOut = outLen - head - tail;
        if (bodySrc > 0.1 * srcLen && bodyOut > 0.0)
            m = { head, tail, bodySrc / bodyOut, true };
    }
    auto srcOf = [&] (int k, double t) {
        const auto& p = notes[(std::size_t) k];
        const auto& m = maps[(std::size_t) k];
        const auto u = t - (double) p.curStart;
        if (! m.split)
            return (double) p.origStart + u * p.stretch;
        const auto outLen = (double) (p.curEnd - p.curStart);
        if (u <= m.head)
            return (double) p.origStart + u;
        if (u >= outLen - m.tail)
            return (double) p.origEnd - (outLen - u);
        return (double) p.origStart + m.head + (u - m.head) * m.rate;
    };
    auto outOf = [&] (int k, double s) {
        const auto& p = notes[(std::size_t) k];
        const auto& m = maps[(std::size_t) k];
        const auto v = s - (double) p.origStart;
        if (! m.split)
            return (double) p.curStart + v / p.stretch;
        const auto srcLen = (double) (p.origEnd - p.origStart);
        if (v <= m.head)
            return (double) p.curStart + v;
        if (v >= srcLen - m.tail)
            return (double) p.curEnd - (srcLen - v);
        return (double) p.curStart + m.head + (v - m.head) / m.rate;
    };
    // the source plays 1:1 at output time t of note k (no stretch there)
    auto oneToOne = [&] (int k, double t) {
        const auto& p = notes[(std::size_t) k];
        const auto& m = maps[(std::size_t) k];
        if (! differs (p.stretch, 1.0))
            return true;
        if (! m.split)
            return false;
        const auto u = t - (double) p.curStart;
        return u <= m.head || u >= (double) (p.curEnd - p.curStart) - m.tail;
    };
    auto markBefore = [&] (double src) {
        const auto it = std::upper_bound (marks.begin(), marks.end(), src, [] (double v, const Mark& m) { return v < m.pos; });
        return (std::size_t) std::max<std::ptrdiff_t> (0, (it - marks.begin()) - 1);
    };
    auto gap = [&] (std::size_t i) { // source distance from mark i to the next (the window's right half; the next one's left)
        return i + 1 < marks.size() ? std::min (marks[i + 1].pos - marks[i].pos, 0.05 * fs) : marks[i].period;
    };

    // the output ranges to render: the edited notes, and the joins an edited neighbour gives an unedited note
    std::vector<std::pair<std::int64_t, std::int64_t>> dirty;
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto [a, b] = owned[k];
        if (a >= b || ! notes[k].enabled)
            continue;
        if (notes[k].edited (track.globalFormant))
            dirty.push_back ({ a, b });
        if (joins (k))
        {
            const auto h = (std::int64_t) std::ceil (joinHalf (k)) + 1;
            dirty.push_back ({ std::max<std::int64_t> (0, notes[k].curStart - h), std::min (length, notes[k].curStart + h) });
        }
    }
    std::sort (dirty.begin(), dirty.end());
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
    for (const auto& d : dirty)
        if (! ranges.empty() && d.first <= ranges.back().second)
            ranges.back().second = std::max (ranges.back().second, d.second);
        else
            ranges.push_back (d);

    // everything plays the source where it is not rendered; disabled notes are silent
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto& p = notes[k];
        const auto [a, b] = owned[k];
        for (auto t = a; t < b; ++t)
        {
            const auto src = p.origStart + (t - p.curStart);
            for (int c = 0; c < channels; ++c)
                dst[c][(std::size_t) t] = p.enabled && src >= 0 && src < n ? in[c][(std::size_t) src] : 0.0f;
        }
    }

    std::vector<float> accL, accR, wsum, gainCurve;
    for (const auto& [x, y] : ranges)
    {
        const auto margin = (std::int64_t) std::ceil (2.0 * fs / 60.0) + 4;
        const auto from = x - margin, to = y + margin;
        const auto span = (std::size_t) (to - from);
        accL.assign (span, 0.0f);
        if (channels == 2)
            accR.assign (span, 0.0f);
        wsum.assign (span, 0.0f);
        float* const sum[2] { accL.data(), channels == 2 ? accR.data() : accL.data() };
        // a grain of mark i at output time ts (formant factor f, weight g); `at` = the source position read at u = 0
        auto grain = [&] (std::size_t i, double ts, double at, double left, double right, double f, float g) {
            const auto o0 = (std::int64_t) std::max ((double) from, std::ceil (ts - left / f));
            const auto o1 = (std::int64_t) std::min ((double) (to - 1), std::floor (ts + right / f));
            (void) i;
            for (auto o = o0; o <= o1; ++o)
            {
                const auto u = ((double) o - ts) * f;
                const auto w = g * (float) (u < 0.0 ? 0.5 + 0.5 * std::cos (3.141592653589793 * u / left) : 0.5 + 0.5 * std::cos (3.141592653589793 * u / right));
                for (int c = 0; c < channels; ++c)
                    sum[c][o - from] += w * readAt (in[c], at + u);
                wsum[(std::size_t) (o - from)] += w;
            }
        };
        // the walk: synthesis marks over [from, to); each takes the note that owns it and that note's source time
        auto start = ownerOf ((double) std::max<std::int64_t> (0, from));
        if (start < 0)
            start = ownerOf ((double) x);
        if (start < 0)
            continue;
        auto i = markBefore (srcOf (start, (double) std::max<std::int64_t> (0, from)));
        double ts = outOf (start, marks[i].pos);
        int guard = 0;
        while (ts < (double) to && ++guard < 50000000)
        {
            const auto k = ownerOf (ts);
            if (k < 0 || ! notes[(std::size_t) k].enabled)
            {
                ts += unvoicedHop;
                continue;
            }
            const auto& p = notes[(std::size_t) k];
            const auto src = srcOf (k, ts);
            i = markBefore (src);
            const auto& m0 = marks[i];
            const auto voiced = m0.voiced;
            const auto nextVoiced = i + 1 < marks.size() && marks[i + 1].voiced; // (no: the run's last period)
            if (! voiced)
            {
                if (oneToOne (k, ts) && oneToOne (k, outOf (k, m0.pos)))
                {
                    // unvoiced, not stretched: each mark read 1:1 at its own place (with its neighbours' windows: exact)
                    const auto t = outOf (k, m0.pos);
                    grain (i, t, m0.pos, i > 0 ? gap (i - 1) : gap (i), gap (i), 1.0, 1.0f);
                    if (i + 1 >= marks.size())
                        break;
                    const auto next = outOf (k, marks[i + 1].pos);
                    ts = next > ts ? next : ts + 1.0;
                }
                else
                {
                    // stretched: regular grains reading the source at the mapped time (overlap-add, no pitch change)
                    const auto h = (double) unvoicedHop;
                    grain (i, ts, src, h, h, 1.0, 1.0f);
                    ts += h;
                }
                continue;
            }
            const auto period = nextVoiced ? std::max (1.0, marks[i + 1].pos - m0.pos) : std::max (1.0, m0.period);
            const auto alpha = nextVoiced ? std::clamp ((src - m0.pos) / period, 0.0, 1.0) : 0.0;
            const auto semis = shiftAt ((std::size_t) k, ts) + deviationAt ((std::size_t) k, src);
            const auto r = std::exp2 (std::clamp (semis, -36.0, 36.0) / 12.0);
            const auto f = std::clamp (p.formant * track.globalFormant, 0.25, 4.0);
            // the grain: the analysis grains around the source position mixed by a triangle `reach` marks wide (one
            // mark either side; wider when shifting down, where each synthesis mark moves the source on by more than a
            // period: the cycle-to-cycle variation is then low-passed instead of aliased into a rough under-tone)
            {
                const auto reach = std::max (1.0, 1.0 / r);
                const auto at = (double) i + alpha; // in marks
                const auto lo = (std::ptrdiff_t) std::ceil (at - reach + 1e-9), hi = (std::ptrdiff_t) std::floor (at + reach - 1e-9);
                double total = 0.0;
                for (auto j = std::max<std::ptrdiff_t> (0, lo); j <= std::min<std::ptrdiff_t> ((std::ptrdiff_t) marks.size() - 1, hi); ++j)
                    if (marks[(std::size_t) j].voiced)
                        total += 1.0 - std::abs ((double) j - at) / reach;
                for (auto j = std::max<std::ptrdiff_t> (0, lo); j <= std::min<std::ptrdiff_t> ((std::ptrdiff_t) marks.size() - 1, hi); ++j)
                {
                    const auto& mj = marks[(std::size_t) j];
                    const auto wj = 1.0 - std::abs ((double) j - at) / reach;
                    if (! mj.voiced || wj <= 1e-9 || total <= 0.0)
                        continue;
                    const auto jj = (std::size_t) j;
                    grain (jj, ts, mj.pos, jj > 0 ? gap (jj - 1) : gap (jj), gap (jj), f, (float) (wj / total));
                }
            }
            auto next = ts + period / r;
            // (into an unvoiced stretch: go on from its first mark's own place)
            if (const auto k2 = ownerOf (next); k2 >= 0)
            {
                const auto s2 = srcOf (k2, next);
                const auto j = markBefore (s2);
                if (! marks[j].voiced && k2 == k)
                    next = std::max (next, outOf (k, marks[j].pos));
            }
            ts = next;
        }
        // overlapping grains normalised (an upward shift stacks more than one); gaps (a downward one) left as they are
        for (std::size_t j = 0; j < span; ++j)
            if (wsum[j] > 1.0f)
                for (int c = 0; c < channels; ++c)
                    sum[c][j] /= wsum[j];
        // the loudness kept: where voiced, the output follows the source's 30 ms level at the mapped time
        gainCurve.assign (span, 1.0f);
        {
            const auto half = std::max<std::int64_t> (1, (std::int64_t) std::lround (0.015 * fs));
            std::vector<double> c (span + 1, 0.0);
            for (std::size_t j = 0; j < span; ++j)
            {
                const double v = channels == 2 ? 0.5 * ((double) sum[0][j] + (double) sum[1][j]) : (double) sum[0][j];
                c[j + 1] = c[j] + v * v;
            }
            for (auto t = std::max<std::int64_t> (0, x); t < y; ++t)
            {
                const auto k = ownerOf ((double) t);
                if (k < 0)
                    continue;
                const auto src = srcOf (k, (double) t);
                if (src < 0.0 || src >= (double) n || ! voicedAt (src))
                    continue;
                const auto j = t - from;
                const auto lo = std::max<std::int64_t> (0, j - half), hi = std::min<std::int64_t> ((std::int64_t) span, j + half);
                const auto outRms = std::sqrt ((c[(std::size_t) hi] - c[(std::size_t) lo]) / (double) std::max<std::int64_t> (1, hi - lo));
                const auto want = (double) level[(std::size_t) std::min<std::int64_t> (n - 1, std::llround (src))];
                if (outRms > 1e-7 && want > 1e-7)
                    gainCurve[(std::size_t) j] = (float) std::clamp (want / outRms, 0.25, 4.0);
            }
        }
        for (auto t = std::max<std::int64_t> (0, x); t < std::min (y, length); ++t)
        {
            const auto k = ownerOf ((double) t);
            if (k < 0 || ! notes[(std::size_t) k].enabled)
                continue;
            const auto& p = notes[(std::size_t) k];
            auto g = p.volume * gainCurve[(std::size_t) (t - from)];
            const auto since = (double) (t - p.curStart), until = (double) (p.curEnd - t);
            if (p.fadeInTime > 0.0 && since < p.fadeInTime)
                g *= p.fadeInGain + (1.0 - p.fadeInGain) * since / p.fadeInTime;
            if (p.fadeOutTime > 0.0 && until < p.fadeOutTime)
                g *= p.fadeOutGain + (1.0 - p.fadeOutGain) * until / p.fadeOutTime;
            const auto tremPeak = std::max (std::abs (p.vibAmpStart), std::abs (p.vibAmpEnd));
            if (p.tremAmp != 0.0 && p.vibPeriod > 0.0 && tremPeak > 0.0)
            {
                const auto u = std::clamp (since / (double) std::max<std::int64_t> (1, p.curEnd - p.curStart), 0.0, 1.0);
                const auto depth = (p.vibAmpStart + (p.vibAmpEnd - p.vibAmpStart) * u) / tremPeak;
                g *= 1.0 + p.tremAmp * depth * std::sin (twoPi * since / (p.vibPeriod * fs));
            }
            // (short joins with the plain sample at the range's ends: there the render is the source itself)
            const auto fade = 64.0;
            const auto edge = std::min (x > 0 ? (double) (t - x) + 1.0 : 1e9, y < length ? (double) (y - t) : 1e9);
            const auto w = (float) std::clamp (edge / (fade + 1.0), 0.0, 1.0);
            const auto wf = w >= 1.0f ? 1.0f : 0.5f - 0.5f * std::cos (3.14159265f * w);
            for (int c = 0; c < channels; ++c)
            {
                const auto v = (float) g * sum[c][t - from];
                auto& d = dst[c][(std::size_t) t];
                d = d + wf * ((std::isfinite (v) ? v : 0.0f) - d);
            }
        }
    }
    if (! stereo)
        out->right.clear();
    return out;
}
} // namespace

std::shared_ptr<SampleData> renderEdits (const SampleData& source, const Track& track)
{
    const auto n = (std::int64_t) source.frames();
    if (n <= 0 || track.timeWarp || track.pitches.empty())
        return nullptr;
    const auto& notes = track.pitches;
    if (std::none_of (notes.begin(), notes.end(), [&] (const Note& p) { return p.edited (track.globalFormant); }))
        return nullptr;
    if (track.engine >= 1)
        if (auto natural = renderNatural (source, track))
            return natural;

    const auto fs = source.sampleRate > 0.0 ? source.sampleRate : 44100.0;
    const auto stereo = source.isStereo();
    std::vector<float> in[2];
    in[0].assign (source.leftData(), source.leftData() + n);
    if (stereo)
        in[1].assign (source.rightData(), source.rightData() + n);
    std::vector<float> mono = in[0];
    if (stereo)
        for (std::int64_t i = 0; i < n; ++i)
            mono[(std::size_t) i] = 0.5f * (in[0][(std::size_t) i] + in[1][(std::size_t) i]);

    const int hop = 128, unvoicedHop = std::max (16, (int) std::lround (128.0 * fs / 44100.0));
    std::vector<vocal::PitchFrame> frames;
    const auto marks = analyse (mono, fs, frames, hop, unvoicedHop);
    auto voicedAt = [&] (double pos) {
        if (frames.empty())
            return false;
        const auto f = std::min (frames.size() - 1, (std::size_t) std::max<double> (0.0, std::round (pos / hop)));
        return frames[f].hz > 0.0;
    };

    // drift centre: the note's mean sung pitch over its voiced frames [model: REA_rodada95 §2.3, "about the mean"]
    std::vector<double> centre (notes.size(), 0.0);
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        double sum = 0.0;
        int count = 0;
        for (auto f = std::max<std::int64_t> (0, notes[k].origStart / hop); f < std::min<std::int64_t> ((std::int64_t) frames.size(), notes[k].origEnd / hop + 1); ++f)
            if (frames[(std::size_t) f].hz > 0.0)
            {
                sum += vocal::hzToNote (frames[(std::size_t) f].hz);
                ++count;
            }
        centre[k] = count > 0 ? sum / count : notes[k].origPitch;
    }

    // the span each note owns: [curStart, curEnd), minus what an earlier note still covers
    std::int64_t length = n;
    std::vector<std::pair<std::int64_t, std::int64_t>> owned (notes.size());
    std::int64_t covered = 0;
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto a = std::max (notes[k].curStart, covered), b = notes[k].curEnd;
        owned[k] = { a, std::max (a, b) };
        covered = std::max (covered, b);
        length = std::max (length, b);
    }

    // pitch shift (semitones) of note k at output sample t, with the transitions to its neighbours (§2.4)
    auto adjacent = [&] (std::size_t j, std::size_t k) {
        return notes[j].curEnd == notes[k].curStart && voicedAt ((double) notes[k].origStart);
    };
    auto shiftAt = [&] (std::size_t k, double t) {
        const auto& p = notes[k];
        auto s = p.shift;
        if (k > 0 && adjacent (k - 1, k) && differs (notes[k - 1].shift, p.shift) && p.smoothIn > 0.0)
        {
            const auto b = (double) p.curStart;
            const auto v0 = notes[k - 1].shift + 0.0435 * (p.shift - notes[k - 1].shift);
            if (t >= b && t < b + p.smoothIn)
                s = v0 + (p.shift - v0) * (t - b) / p.smoothIn;
        }
        if (k + 1 < notes.size() && adjacent (k, k + 1) && differs (notes[k + 1].shift, p.shift))
        {
            const auto b = (double) p.curEnd;
            if (t >= b - 0.5 * p.smoothOut && t < b)
                s = p.shift + 0.0435 * (notes[k + 1].shift - p.shift);
        }
        // pitch ramps [EXEC flnewt165]: rampIn semitones at the start, linear to 0 over smoothIn; rampOut reached
        // at the end, linear from 0 over the last smoothOut
        if (p.rampIn != 0.0 && p.smoothIn > 0.0 && t >= (double) p.curStart && t < (double) p.curStart + p.smoothIn)
            s += p.rampIn * (1.0 - (t - (double) p.curStart) / p.smoothIn);
        if (p.rampOut != 0.0 && p.smoothOut > 0.0 && t < (double) p.curEnd && t >= (double) p.curEnd - p.smoothOut)
            s += p.rampOut * (1.0 - ((double) p.curEnd - t) / p.smoothOut);
        if (p.vibPeriod > 0.0 && p.curEnd > p.curStart)
        {
            const auto u = (t - (double) p.curStart) / (double) (p.curEnd - p.curStart);
            const auto amp = p.vibAmpStart + (p.vibAmpEnd - p.vibAmpStart) * std::clamp (u, 0.0, 1.0);
            s += amp * std::sin (twoPi * (t - (double) p.curStart) / (p.vibPeriod * fs));
        }
        return s;
    };

    auto out = std::make_shared<SampleData>();
    out->sampleRate = fs;
    out->left.assign ((std::size_t) length, 0.0f);
    if (stereo)
        out->right.assign ((std::size_t) length, 0.0f);
    float* const dst[2] { out->left.data(), stereo ? out->right.data() : out->left.data() };
    const auto channels = stereo ? 2 : 1;

    // TD-PSOLA of note k over output samples [a, b): grains at synthesis marks one target period apart, each taken
    // from the analysis mark nearest the mapped source time; then the note's gain
    std::vector<float> accL, accR;
    auto render = [&] (std::size_t k, std::int64_t a, std::int64_t b) {
        const auto& p = notes[k];
        const auto margin = (std::int64_t) std::ceil (2.0 * fs / 60.0) + 4;
        const auto from = a - margin, to = b + margin;
        accL.assign ((std::size_t) (to - from), 0.0f);
        if (channels == 2)
            accR.assign ((std::size_t) (to - from), 0.0f);
        float* const sum[2] { accL.data(), channels == 2 ? accR.data() : accL.data() };
        const auto formant = std::clamp (p.formant * track.globalFormant, 0.25, 4.0);
        auto ts = (double) from;
        while (ts < (double) to)
        {
            const auto src = (double) p.origStart + (ts - (double) p.curStart) * p.stretch;
            const auto it = std::lower_bound (marks.begin(), marks.end(), src, [] (const Mark& m, double v) { return m.pos < v; });
            const Mark* mk = nullptr;
            if (it != marks.end())
                mk = &*it;
            if (it != marks.begin() && (mk == nullptr || std::abs (std::prev (it)->pos - src) < std::abs (mk->pos - src)))
                mk = &*std::prev (it);
            const auto voiced = mk != nullptr && mk->voiced;
            double ha, hs, r = 1.0, f = 1.0, ta = src;
            if (voiced)
            {
                const auto semis = shiftAt (k, ts) + (p.drift - 1.0) * (mk->note - centre[k]);
                r = std::exp2 (std::clamp (semis, -36.0, 36.0) / 12.0);
                f = formant;
                // the local period: the distance to the next mark (they lock to the waveform's peaks, so they follow
                // a glide that the pitch frames smear)
                ha = mk->period;
                if (const auto next = mk + 1; next < marks.data() + marks.size() && next->voiced)
                    ha = std::clamp (next->pos - mk->pos, 0.5 * mk->period, 2.0 * mk->period);
                // FL's grains [EXEC, round 165g flnewt165]: no gain correction; the window spans two periods of the
                // output pitch, at most three source periods, and is shortened by a formant factor above 1
                hs = ha * std::min (1.0 / r, 1.5) / std::max (1.0, f);
                ta = mk->pos;
            }
            else
                ha = hs = unvoicedHop;
            const auto gain = 1.0f;
            // bounds clamped as doubles before the conversion (the grain never reaches outside [from, to))
            const auto o0 = (std::int64_t) std::max ((double) from, std::ceil (ts - hs));
            const auto o1 = (std::int64_t) std::min ((double) (to - 1), std::floor (ts + hs));
            for (auto o = o0; o <= o1; ++o)
            {
                const auto u = ((double) o - ts) / hs;
                if (u <= -1.0 || u >= 1.0)
                    continue;
                const auto w = gain * (float) (0.5 + 0.5 * std::cos (3.141592653589793 * u));
                const auto at = ta + ((double) o - ts) * f;
                for (int c = 0; c < channels; ++c)
                    sum[c][o - from] += w * readAt (in[c], at);
            }
            ts += voiced ? ha / r : ha;
        }
        const auto tremPeak = std::max (std::abs (p.vibAmpStart), std::abs (p.vibAmpEnd));
        // the note's gain: volume, fade-in from fadeInGain over fadeInTime, fade-out to fadeOutGain [model]
        for (auto t = a; t < b; ++t)
        {
            auto g = p.volume;
            const auto since = (double) (t - p.curStart), until = (double) (p.curEnd - t);
            if (p.fadeInTime > 0.0 && since < p.fadeInTime)
                g *= p.fadeInGain + (1.0 - p.fadeInGain) * since / p.fadeInTime;
            if (p.fadeOutTime > 0.0 && until < p.fadeOutTime)
                g *= p.fadeOutGain + (1.0 - p.fadeOutGain) * until / p.fadeOutTime;
            // tremolo [EXEC flnewt165]: 1 + tremAmp·sin, in step with the vibrato, its depth following the vibrato's
            // start → end envelope normalised to its peak (FL's engine gives NaN when both ends are 0; LYNX: none)
            if (p.tremAmp != 0.0 && p.vibPeriod > 0.0 && tremPeak > 0.0)
            {
                const auto u = std::clamp (since / (double) std::max<std::int64_t> (1, p.curEnd - p.curStart), 0.0, 1.0);
                const auto depth = (p.vibAmpStart + (p.vibAmpEnd - p.vibAmpStart) * u) / tremPeak;
                g *= 1.0 + p.tremAmp * depth * std::sin (twoPi * since / (p.vibPeriod * fs));
            }
            for (int c = 0; c < channels; ++c)
            {
                const auto v = (float) g * sum[c][t - from];
                dst[c][(std::size_t) t] = std::isfinite (v) ? v : 0.0f;
            }
        }
    };

    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto& p = notes[k];
        const auto [a, b] = owned[k];
        if (a >= b || ! p.enabled)
            continue; // a disabled note plays silence (§2.4)
        // the sample itself where nothing changes (unedited notes in place); rendered: edited notes, and the
        // transition regions an edited neighbour gives an unedited note (§2.4)
        auto plainAt = [&] (int c, std::int64_t t) {
            const auto src = p.origStart + (t - p.curStart);
            return src >= 0 && src < n ? in[c][(std::size_t) src] : 0.0f;
        };
        for (auto t = a; t < b; ++t)
            for (int c = 0; c < channels; ++c)
                dst[c][(std::size_t) t] = plainAt (c, t);

        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
        if (p.edited (track.globalFormant))
            ranges.push_back ({ a, b });
        else
        {
            if (k > 0 && adjacent (k - 1, k) && differs (notes[k - 1].shift, p.shift) && p.smoothIn > 0.0)
                ranges.push_back ({ a, std::min (b, a + (std::int64_t) std::ceil (p.smoothIn)) });
            if (k + 1 < notes.size() && adjacent (k, k + 1) && differs (notes[k + 1].shift, p.shift))
            {
                const auto x = std::max (a, (std::int64_t) std::floor ((double) p.curEnd - 0.5 * p.smoothOut));
                if (! ranges.empty() && x <= ranges.back().second)
                    ranges.back().second = b;
                else
                    ranges.push_back ({ x, b });
            }
        }
        for (const auto& [x, y] : ranges)
        {
            if (x >= y)
                continue;
            render (k, x, y);
            // short cross-fades where a rendered span meets the plain sample (no click at the switch)
            const auto fade = std::min<std::int64_t> (64, (y - x) / 2);
            for (std::int64_t i = 0; i < fade; ++i)
            {
                const auto w = 0.5f - 0.5f * std::cos (3.14159265f * (float) (i + 1) / (float) (fade + 1));
                for (int c = 0; c < channels; ++c)
                {
                    if (x > a || x == p.curStart)
                    {
                        auto& v = dst[c][(std::size_t) (x + i)];
                        v = plainAt (c, x + i) + w * (v - plainAt (c, x + i));
                    }
                    if (y < b)
                    {
                        auto& v = dst[c][(std::size_t) (y - 1 - i)];
                        v = plainAt (c, y - 1 - i) + w * (v - plainAt (c, y - 1 - i));
                    }
                }
            }
        }
    }
    if (! stereo)
        out->right.clear();
    return out;
}

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4)
        return std::nullopt;

    State s;
    s.version = i32 (d.data());

    // Newtone loads versions ≤ 0, Newtime ≤ 1 (REA_rodada73 §2)
    if (s.version < 0 || s.version > 1)
        return std::nullopt;

    // records: i32 id, i64 size, data
    std::size_t at = 4;
    bool sawOptions = false;

    while (at + 12 <= d.size())
    {
        const auto id = i32 (d.data() + at);
        const auto size = i64 (d.data() + at + 4);
        at += 12;

        if (size < 0 || (std::uint64_t) size > d.size() - at)
            return std::nullopt;

        const auto* p = d.data() + at;
        const auto n = (std::size_t) size;

        if (id == 1 && n >= 12)
        {
            s.editorWidth = i32 (p);
            s.editorHeight = i32 (p + 4);
            s.flags = (std::uint32_t) i32 (p + 8);
            sawOptions = true;
        }
        else if (id == 2)
            s.projectXml = projectXmlOf (p, n);
        else if (id == 3)
            s.sourcePath.assign ((const char*) p, n);

        at += n;
    }

    if (at != d.size() || ! sawOptions)
        return std::nullopt;

    readTracks (s);
    return s;
}

void toParams (const State& s, EffectParams& params) noexcept
{
    params[0] = s.slave() ? 1.0f : 0.0f;
}

std::string paramText (int param, float v)
{
    if (param == 0)
        return v >= 0.5f ? "on" : "off";
    return {};
}

std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> source)
{
    return std::make_unique<Engine> (std::move (source));
}

std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> source, const std::vector<std::uint8_t>& state)
{
    return make (renderedSource (std::move (source), state));
}

// ------------------------------------------------------------------------------------------------- LYNX's projects
namespace
{
template <typename T>
void put (std::vector<std::uint8_t>& out, T v)
{
    const auto* p = reinterpret_cast<const std::uint8_t*> (&v);
    out.insert (out.end(), p, p + sizeof (T));
}

struct Reader
{
    const std::vector<std::uint8_t>& d;
    std::size_t at = 0;
    bool ok = true;
    template <typename T>
    T get()
    {
        T v {};
        if (at + sizeof (T) > d.size())
        {
            ok = false;
            return v;
        }
        std::memcpy (&v, d.data() + at, sizeof (T));
        at += sizeof (T);
        return v;
    }
};

constexpr std::uint32_t nativeVersion = 5; // 2 (round 165g): + the editor's knobs, the notes' pitch ramps and analysed smooth times;
                                           // 3 (round 190kj): + each note's voiced end and analysed velocity;
                                           // 4 (round 190li): + the engine revision, each note's modulation / pitch drift;
                                           // 5 (rollup 0.1.30 step 8): + the analysis's "Separate close notes" (fineSplit)
constexpr std::uint32_t warpVersion = 1;

double median (std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) (v.size() / 2), v.end());
    return v[v.size() / 2];
}
} // namespace

AnalyseOptions newSlotAnalyseOptions() noexcept
{
    AnalyseOptions o;
    o.fineSplit = latestEngine >= 1; // (owner, 2026-11-18: on for new slots with the natural engine)
    return o;
}

Track analyseNewSlot (const SampleData& source)
{
    auto t = analyseNotes (source, newSlotAnalyseOptions());
    t.engine = latestEngine;
    return t;
}

Track analyseNotes (const SampleData& source, const AnalyseOptions& options)
{
    Track t;
    t.fineSplit = options.fineSplit;
    const auto n = (std::int64_t) source.frames();
    t.frames = n;
    if (n <= 0)
        return t;
    const auto fs = source.sampleRate > 0.0 ? source.sampleRate : 44100.0;
    std::vector<float> mono (source.leftData(), source.leftData() + n);
    if (source.isStereo())
        for (std::int64_t i = 0; i < n; ++i)
            mono[(std::size_t) i] = 0.5f * (mono[(std::size_t) i] + source.rightData()[i]);

    const int hop = std::max (64, (int) std::lround (256.0 * fs / 44100.0));
    // (round 190kj: a looser voicing threshold than the tuner's 0.15, so rough and low voices stay voiced as in FL)
    auto frames = vocal::analysePitch (mono.data(), (int) n, fs, hop, 60.0, 1000.0, 0.3f);
    if (options.octaveGuard)
    {
        const auto strict = vocal::analysePitch (mono.data(), (int) n, fs, hop, 60.0, 1000.0, 0.15f);
        for (std::size_t k = 0; k < frames.size() && k < strict.size(); ++k)
            if (frames[k].hz > 0.0 && strict[k].hz > 0.0 && std::abs (std::log2 (frames[k].hz / strict[k].hz)) > 0.5)
                frames[k].hz = strict[k].hz;
    }
    float loudest = 0.0f;
    for (const auto& f : frames)
        loudest = std::max (loudest, f.level);
    const auto floor = loudest * 0.02f; // (−34 dB under the loudest frame: breaths and room tone are not notes)

    // a frame's pitch, smoothed over its neighbours (vibrato stays inside a note)
    std::vector<double> note (frames.size(), -1.0);
    for (std::size_t k = 0; k < frames.size(); ++k)
        if (frames[k].hz > 0.0 && frames[k].level >= floor)
            note[k] = vocal::hzToNote (frames[k].hz);
    auto smooth = [&] (std::size_t k) {
        std::vector<double> v;
        for (auto j = k >= 2 ? k - 2 : 0; j <= std::min (frames.size() - 1, k + 2); ++j)
            if (note[j] >= 0.0)
                v.push_back (note[j]);
        return median (v);
    };

    const auto minFrames = std::max<std::size_t> (2, (std::size_t) std::lround (0.06 * fs / hop)); // notes ≥ 60 ms
    const auto holdFrames = std::max (3, (int) std::lround (0.058 * fs / hop));
    struct Run
    {
        std::size_t a, b; // voiced frames [a, b)
        double hz;        // their median frequency
    };
    std::vector<Run> runs;
    auto close = [&] (std::size_t a, std::size_t b) { // frames [a, b) → a note
        if (b <= a || b - a < minFrames)
            return;
        std::vector<double> v;
        for (auto k = a; k < b; ++k)
            if (note[k] >= 0.0)
                v.push_back (frames[k].hz);
        if (v.empty())
            return;
        runs.push_back ({ a, b, median (v) });
    };

    // round 190kx, fineSplit: a voiced frame at a level dip (6 dB under the loudest frame within 60 ms on both sides, the
    // lowest frame there) starts a new note
    const auto stepLimit = options.fineSplit ? 0.33 : 0.7;
    const auto dipReach = std::max<std::size_t> (2, (std::size_t) std::lround (0.06 * fs / hop));
    const auto atDip = [&] (std::size_t k) {
        if (! options.fineSplit || k < dipReach || k + dipReach >= frames.size())
            return false;
        float before = 0.0f, after = 0.0f, lowest = frames[k].level;
        for (std::size_t j = k - dipReach; j < k; ++j)
            before = std::max (before, frames[j].level), lowest = std::min (lowest, frames[j].level);
        for (std::size_t j = k + 1; j <= k + dipReach; ++j)
            after = std::max (after, frames[j].level), lowest = std::min (lowest, frames[j].level);
        return frames[k].level <= lowest && frames[k].level < 0.3f * before && frames[k].level < 0.3f * after; // (−10 dB)
    };
    // (fineSplit: the pitch a step is measured on is the median over ±90 ms, a vibrato's whole period at 5.5 Hz, so a
    // vibrato is not a step)
    const auto wide = std::max<std::size_t> (2, (std::size_t) std::lround (0.09 * fs / hop));
    auto smoothWide = [&] (std::size_t k) {
        std::vector<double> v;
        for (auto j = k >= wide ? k - wide : 0; j <= std::min (frames.size() - 1, k + wide); ++j)
            if (note[j] >= 0.0)
                v.push_back (note[j]);
        return median (v);
    };

    std::size_t start = 0;
    bool inNote = false;
    double centre = 0.0;
    int away = 0;
    for (std::size_t k = 0; k <= frames.size(); ++k)
    {
        const auto voiced = k < frames.size() && note[k] >= 0.0;
        if (! inNote)
        {
            if (voiced)
                inNote = true, start = k, centre = note[k], away = 0;
            continue;
        }
        if (voiced && k - start >= minFrames && atDip (k))
        {
            close (start, k);
            start = k;
            centre = smoothWide (k);
            away = 0;
            continue;
        }
        // (fineSplit: the pitch is the median over ±90 ms, a vibrato's whole period at 5.5 Hz, so a vibrato is no step)
        const auto here = voiced ? (options.fineSplit ? smoothWide (k) : smooth (k)) : -1.0;
        if (! voiced || std::abs (here - centre) > stepLimit)
        {
            // a gap, or a new pitch held for 58 ms (FL's segmentation, fitted on 17 files), ends the note (glitches,
            // slides and short wobbles stay in it)
            if (! voiced || ++away >= holdFrames)
            {
                const auto end = voiced ? k - (std::size_t) (away - 1) : k;
                close (start, end);
                inNote = voiced;
                start = end;
                centre = voiced ? here : 0.0;
                away = 0;
            }
            continue;
        }
        away = 0;
        centre += (options.fineSplit ? 0.02 : 0.1) * (here - centre); // (follows a slow drift; fineSplit: a slower one, so
                                                                         // a smaller step is not taken in as drift)
    }

    // FL's layout (round 190kj, measured on FL's own analysis of 17 files): the notes tile the file. A note starts at
    // its voice's onset (or at the pitch change from the note before) and owns everything up to the next note; a lead-in
    // note (unvoiced, the first note's pitch) covers the file's start; validEnd = where the note's voice ends.
    const auto toSample = [&] (std::size_t k) { return std::clamp<std::int64_t> ((std::int64_t) k * hop - hop / 2, 0, n); };
    const auto flPitch = [&] (double hz) { // whole-sample periods, as FL's fOrigPitch
        const auto period = std::floor (fs / hz);
        return period >= 1.0 ? vocal::hzToNote (fs / period) : vocal::hzToNote (hz);
    };
    const auto defaultSmooth = 0.073 * fs; // FL's smooth-in after silence (3200-3300 samples at 44.1 kHz)
    const auto onsetLead = (std::int64_t) std::lround (1.75 * hop); // the voicing detector's lag behind the onset
    std::vector<Note> out;
    for (std::size_t i = 0; i < runs.size(); ++i)
    {
        const auto& r = runs[i];
        Note p;
        const bool legato = i > 0 && runs[i - 1].b + 2 >= r.a;
        auto first = r.a;
        if (legato) // a pitch change: the note starts where the pitch leaves the note before
        {
            const auto before = vocal::hzToNote (runs[i - 1].hz);
            while (first > runs[i - 1].a + 1 && note[first - 1] >= 0.0 && std::abs (note[first - 1] - before) > 0.33)
                --first;
        }
        p.origStart = std::max<std::int64_t> (0, toSample (first) - (legato ? 0 : onsetLead)); // (a pitch change: no lead)
        if (! out.empty() && p.origStart <= out.back().origStart)
            p.origStart = out.back().origStart + 1;
        if (p.origStart >= n)
            break;
        p.validEnd = std::max (p.origStart + 1, toSample (r.b));
        p.origPitch = flPitch (r.hz);
        // the glide into the note: from its start until the pitch settles within a third of a semitone of the note's
        // own (a note after silence: FL's default); the glide out: from where it leaves it to the voice's end
        const auto target = vocal::hzToNote (r.hz);
        // (a note after silence that starts on its pitch: FL's default; one that scoops into it: the scoop)
        auto settle = first;
        while (settle < r.b && (note[settle] < 0.0 || std::abs (note[settle] - target) > 0.33))
            ++settle;
        const auto scoop = (double) (settle - first) * hop;
        const double in = legato ? std::min (defaultSmooth, scoop) : (scoop > 0.0 ? std::min (defaultSmooth, scoop) : defaultSmooth);
        auto k = r.b;
        while (k > r.a && (note[k - 1] < 0.0 || std::abs (note[k - 1] - target) > 0.33))
            --k;
        p.smoothIn = p.origSmoothIn = in;
        p.smoothOut = p.origSmoothOut = (double) (r.b - k) * hop; // + the silence after the voice (below)
        {
            double sum = 0.0;
            int count = 0;
            for (auto j = settle; j < std::max (settle, k); ++j)
                if (note[j] >= 0.0)
                    sum += note[j], ++count;
            p.sungPitch = count > 0 ? sum / count : target;
        }
        out.push_back (p);
    }
    if (! out.empty() && out.front().origStart > 0)
    {
        Note lead;
        lead.origStart = 0;
        lead.origPitch = out.front().origPitch;
        lead.validEnd = 0;
        lead.velocity = 0.0;
        const auto len = (double) out.front().origStart;
        lead.smoothIn = lead.smoothOut = lead.origSmoothIn = lead.origSmoothOut = std::max (0.0, len - 0.0045 * fs);
        out.insert (out.begin(), lead);
    }
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        auto& p = out[i];
        p.origEnd = i + 1 < out.size() ? out[i + 1].origStart : n;
        p.validEnd = p.validEnd == 0 ? 0 : std::min (p.validEnd, p.origEnd);
        if (p.validEnd != 0)
            p.smoothOut = p.origSmoothOut = p.smoothOut + (double) (p.origEnd - p.validEnd);
        p.curStart = p.origStart;
        p.curEnd = p.origEnd;
        p.fadeInTime = p.fadeOutTime = 0.2 * (double) (p.origEnd - p.origStart);
    }
    t.pitches = std::move (out);
    t.key = detectKey (t);
    t.notes = (int) t.pitches.size();
    return t;
}

// ------------------------------------------------------------------------------------------------- editor tools
namespace
{
template <typename F>
void forNotes (Track& track, const std::vector<int>& which, F&& f)
{
    if (which.empty())
        for (auto& p : track.pitches)
            f (p);
    else
        for (const auto k : which)
            if (k >= 0 && k < (int) track.pitches.size())
                f (track.pitches[(std::size_t) k]);
}

void refreshEdited (Track& t)
{
    t.notes = (int) t.pitches.size();
    t.edited = std::any_of (t.pitches.begin(), t.pitches.end(), [&t] (const Note& p) { return p.edited (t.globalFormant); });
}
} // namespace

bool splitNote (Track& track, std::size_t k, std::int64_t at, double (*pitchOf) (const void*, std::int64_t, std::int64_t), const void* context)
{
    if (k >= track.pitches.size())
        return false;
    auto& p = track.pitches[k];
    if (at <= p.curStart || at >= p.curEnd)
        return false;
    const auto src = std::clamp<std::int64_t> (p.origStart + (std::int64_t) std::llround ((double) (at - p.curStart) * p.stretch), p.origStart + 1, p.origEnd - 1);
    if (src <= p.origStart || src >= p.origEnd)
        return false;
    Note b = p;
    p.curEnd = at;
    p.origEnd = src;
    b.curStart = at;
    b.origStart = src;
    // the cut is a hard boundary: no pitch ramp across it, and each half's own ramps stay at its outer end
    p.rampOut = 0.0;
    b.rampIn = 0.0;
    if (pitchOf != nullptr)
    {
        if (const auto v = pitchOf (context, p.origStart, p.origEnd); v > 0.0)
            p.origPitch = v;
        if (const auto v = pitchOf (context, b.origStart, b.origEnd); v > 0.0)
            b.origPitch = v;
        // (the shift stays: each half sounds as before; origPitch is what the editor shows and snaps)
    }
    track.pitches.insert (track.pitches.begin() + (std::ptrdiff_t) k + 1, b);
    refreshEdited (track);
    return true;
}

bool glueNote (Track& track, std::size_t k)
{
    if (k == 0 || k >= track.pitches.size())
        return false;
    auto& a = track.pitches[k - 1];
    const auto& b = track.pitches[k];
    if (a.curEnd != b.curStart || a.origEnd != b.origStart || differs (a.stretch, b.stretch))
        return false;
    const auto la = (double) (a.origEnd - a.origStart), lb = (double) (b.origEnd - b.origStart);
    if (a.origPitch > 0.0 && b.origPitch > 0.0 && la + lb > 0.0)
        a.origPitch = (a.origPitch * la + b.origPitch * lb) / (la + lb);
    a.curEnd = b.curEnd;
    a.origEnd = b.origEnd;
    a.rampOut = b.rampOut;
    a.smoothOut = b.smoothOut;
    a.origSmoothOut = b.origSmoothOut;
    track.pitches.erase (track.pitches.begin() + (std::ptrdiff_t) k);
    refreshEdited (track);
    return true;
}

void applyCentre (Track& track, double amount, const std::vector<int>& which)
{
    amount = std::clamp (amount, 0.0, 1.0);
    // (engine 1: the note's sung centre instead of FL's origPitch, whose whole-sample periods read sharp by up to
    // ~10 cents and whose median takes in the glides)
    const auto natural = track.engine >= 1;
    forNotes (track, which, [amount, natural] (Note& p) {
        if (p.origPitch <= 0.0)
            return;
        const auto pitch = natural && p.sungPitch > 0.0 ? p.sungPitch : p.origPitch;
        const auto target = std::round (pitch + p.shift);  // the semitone the note is on now
        const auto detune = pitch - std::round (pitch);    // how far it was sung from its own semitone
        p.shift = target - pitch + (1.0 - amount) * detune;
    });
    if (which.empty())
        track.centre = amount;
    refreshEdited (track);
}

void applyVariation (Track& track, double amount, const std::vector<int>& which)
{
    amount = std::clamp (amount, 0.0, 2.0); // (FL's Variation knob goes to 200 %: the drift exaggerated)
    forNotes (track, which, [amount] (Note& p) { p.drift = amount; });
    if (which.empty())
        track.variation = amount;
    refreshEdited (track);
}

void applyTransition (Track& track, double factor, const std::vector<int>& which)
{
    factor = std::clamp (factor, 0.0, 2.0);
    forNotes (track, which, [factor] (Note& p) {
        p.smoothIn = p.origSmoothIn * factor;
        p.smoothOut = p.origSmoothOut * factor;
    });
    if (which.empty())
        track.transition = factor;
    refreshEdited (track);
}

void applyModulation (Track& track, double amount, const std::vector<int>& which)
{
    amount = std::clamp (amount, 0.0, 2.0);
    forNotes (track, which, [amount] (Note& p) { p.modulation = amount; });
    refreshEdited (track);
}

void applyPitchDrift (Track& track, double amount, const std::vector<int>& which)
{
    amount = std::clamp (amount, 0.0, 2.0);
    forNotes (track, which, [amount] (Note& p) { p.pitchDrift = amount; });
    refreshEdited (track);
}

void correctPitch (Track& track, double centre, double drift, const std::vector<int>& which)
{
    // (Melodyne's Correct Pitch macro: the centre pulled to the semitone, the drift reduced; the vibrato stays)
    applyCentre (track, centre, which);
    applyPitchDrift (track, 1.0 - std::clamp (drift, 0.0, 1.0), which);
}

double nearestInScale (double pitch, int key, std::uint16_t mask) noexcept
{
    mask &= 0xFFF;
    if (mask == 0)
        mask = 0xFFF;
    const auto base = std::floor (pitch);
    double best = std::round (pitch), dist = 1e9;
    for (int d = -12; d <= 12; ++d)
    {
        const auto m = base + d;
        const auto cls = (((int) m - key) % 12 + 12) % 12;
        if ((mask >> cls & 1u) == 0)
            continue;
        const auto e = std::abs (m - pitch);
        if (e < dist - 1e-9)
            best = m, dist = e;
    }
    return best;
}

void snapToScale (Track& track, int key, std::uint16_t mask, const std::vector<int>& which)
{
    forNotes (track, which, [key, mask] (Note& p) {
        if (p.origPitch > 0.0)
            p.shift = nearestInScale (p.origPitch + p.shift, key, mask) - p.origPitch;
    });
    refreshEdited (track);
}

std::vector<ScoreNote> score (const Track& track, double sampleRate)
{
    std::vector<ScoreNote> out;
    if (sampleRate <= 0.0)
        return out;
    for (const auto& p : track.pitches)
        if (p.enabled && p.voiced() && p.origPitch > 0.0 && p.curEnd > p.curStart)
        {
            // (round 190kj: a note's voiced part; the silence FL's analysis gives it after its voice is not in the score)
            const auto end = std::min ((double) p.curEnd, (double) p.curStart + (double) (p.voicedEnd() - p.origStart) / std::max (1e-9, p.stretch));
            ScoreNote n;
            n.start = (double) p.curStart / sampleRate;
            n.length = std::max (1.0, end - (double) p.curStart) / sampleRate;
            n.key = std::clamp ((int) std::lround (p.origPitch + p.shift), 0, 127);
            n.velocity = (float) std::clamp (0.78 * p.volume, 0.0, 1.0);
            out.push_back (n);
        }
    return out;
}

std::vector<DetectedNote> detectScore (const SampleData& source, double* detune)
{
    std::vector<DetectedNote> out;
    auto track = analyseNotes (source);
    // (round 190kj: the analysis tiles the file; the score takes the voiced notes, each up to where its voice ends)
    track.pitches.erase (std::remove_if (track.pitches.begin(), track.pitches.end(), [] (const Note& p) { return ! p.voiced(); }), track.pitches.end());
    for (auto& p : track.pitches)
        p.origEnd = p.voicedEnd();
    if (track.pitches.empty())
    {
        if (detune != nullptr)
            *detune = 0.0;
        return out;
    }
    // the overall detune: how far the notes sit from their semitones, weighted by their length
    double sum = 0.0, weight = 0.0;
    for (const auto& p : track.pitches)
    {
        const auto w = (double) (p.origEnd - p.origStart);
        sum += w * (p.origPitch - std::round (p.origPitch));
        weight += w;
    }
    const auto offset = weight > 0.0 ? sum / weight : 0.0;
    if (detune != nullptr)
        *detune = offset;
    // each note's level (RMS of the channels' mean)
    std::vector<double> rms;
    double loudest = 0.0;
    for (const auto& p : track.pitches)
    {
        double e = 0.0;
        for (auto i = p.origStart; i < p.origEnd; ++i)
        {
            const auto x = 0.5 * ((double) source.leftData()[i] + (double) source.rightData()[i]);
            e += x * x;
        }
        rms.push_back (std::sqrt (e / (double) std::max<std::int64_t> (1, p.origEnd - p.origStart)));
        loudest = std::max (loudest, rms.back());
    }
    for (std::size_t k = 0; k < track.pitches.size(); ++k)
    {
        const auto& p = track.pitches[k];
        const auto key = (int) std::lround (p.origPitch - offset);
        if (key < 0 || key > 127)
            continue;
        DetectedNote n;
        n.start = p.origStart;
        n.end = p.origEnd;
        n.key = key;
        n.velocity = (float) (0.5 + 0.5 * (loudest > 0.0 ? rms[k] / loudest : 1.0));
        out.push_back (n);
    }
    return out;
}

int dumpScore (Song& song, int channel, const std::vector<DetectedNote>& notes, double sampleRate)
{
    if (sampleRate <= 0.0 || channel < 0)
        return 0;
    if (song.patterns.empty())
        song.patterns.emplace_back();
    song.currentPattern = std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
    auto& pattern = song.activePattern();
    pattern.notes.erase (std::remove_if (pattern.notes.begin(), pattern.notes.end(), [channel] (const lynx::Note& n) { return n.channel == channel; }),
                         pattern.notes.end());
    const auto ticksPerFrame = song.bpm / 60.0 * ppq / sampleRate;
    const std::int64_t shortest = 3 * ppq / 96;
    int count = 0;
    for (const auto& d : notes)
    {
        const auto start = std::max<std::int64_t> (0, std::llround ((double) d.start * ticksPerFrame));
        const auto end = std::max<std::int64_t> (0, std::llround ((double) d.end * ticksPerFrame));
        if (end - start <= shortest)
            continue;
        lynx::Note n;
        n.start = start;
        n.length = end - start;
        n.channel = channel;
        n.key = d.key;
        n.velocity = std::clamp (d.velocity, 0.0f, 1.0f);
        pattern.notes.push_back (n);
        ++count;
    }
    pattern.sortNotes();
    return count;
}

int detectKey (const Track& track)
{
    static constexpr double major[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double minor[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double h[12] {};
    bool any = false;
    for (const auto& p : track.pitches)
        if (p.voiced() && p.origPitch > 0.0)
        {
            const auto k = (int) std::lround (p.origPitch);
            h[((k % 12) + 12) % 12] += (double) (p.voicedEnd() - p.origStart);
            any = true;
        }
    if (! any)
        return -1;
    auto corr = [&] (const double* prof, int tonic) {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i)
            mx += h[i], my += prof[i];
        mx /= 12.0, my /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const auto x = h[(i + tonic) % 12] - mx, y = prof[i] - my;
            sxy += x * y, sxx += x * x, syy += y * y;
        }
        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : -2.0;
    };
    int best = 0;
    double score = -3.0;
    for (int k = 0; k < 24; ++k)
        if (const auto r = corr (k < 12 ? major : minor, k % 12); r > score)
            score = r, best = k;
    return best;
}

std::uint16_t keyScaleMask (int key) noexcept
{
    if (key < 0)
        return 0xFFF;
    return key < 12 ? (std::uint16_t) 0b101010110101 : (std::uint16_t) 0b010110101101; // (major / natural minor from the tonic)
}

std::pair<std::int64_t, std::int64_t> timeAround (const Track& track, const std::vector<int>& which)
{
    std::int64_t a = -1, b = -1;
    for (const auto k : which)
    {
        if (k < 0 || k >= (int) track.pitches.size())
            continue;
        const auto& p = track.pitches[(std::size_t) k];
        const auto end = p.curStart + (std::int64_t) std::llround ((double) (p.voicedEnd() - p.origStart) / std::max (1e-9, p.stretch));
        a = a < 0 ? p.curStart : std::min (a, p.curStart);
        b = std::max (b, std::min (p.curEnd, end));
    }
    return { a, b };
}

std::vector<int> notesIn (const Track& track, std::int64_t a, std::int64_t b)
{
    std::vector<int> out;
    for (int k = 0; k < (int) track.pitches.size(); ++k)
    {
        const auto& p = track.pitches[(std::size_t) k];
        if (p.voiced() && p.curStart < b && p.curEnd > a)
            out.push_back (k);
    }
    return out;
}

audioedit::Document regionsDocument (const SampleData& audio, const Track& track, bool asMarkers)
{
    audioedit::Document doc;
    doc.audio.sampleRate = audio.sampleRate;
    const auto n = (std::size_t) audio.frames();
    doc.audio.channels.emplace_back (audio.leftData(), audio.leftData() + n);
    if (audio.isStereo())
        doc.audio.channels.emplace_back (audio.rightData(), audio.rightData() + n);
    for (const auto& p : track.pitches)
    {
        if (! p.voiced() || ! p.enabled || p.origPitch <= 0.0 || p.curStart >= (std::int64_t) n)
            continue;
        audioedit::Marker m;
        m.start = p.curStart;
        m.end = asMarkers ? p.curStart : std::min<std::int64_t> ((std::int64_t) n, p.curEnd);
        const auto key = (int) std::lround (p.origPitch + p.shift);
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        m.name = std::string (names[((key % 12) + 12) % 12]) + std::to_string (key / 12);
        m.keyNum = std::clamp (key, 0, 127);
        doc.markers.push_back (m);
    }
    return doc;
}

void quantizeTime (Track& track, const std::vector<int>& which, double grid)
{
    if (! (grid > 0.0))
        return;
    std::vector<int> chosen = which;
    if (chosen.empty())
        for (int k = 0; k < (int) track.pitches.size(); ++k)
            chosen.push_back (k);
    for (const auto k : chosen)
    {
        if (k <= 0 || k >= (int) track.pitches.size() || ! track.pitches[(std::size_t) k].voiced())
            continue; // (the first note keeps the file's start)
        auto& p = track.pitches[(std::size_t) k];
        auto& before = track.pitches[(std::size_t) k - 1];
        const auto to = (std::int64_t) std::llround (std::round ((double) p.curStart / grid) * grid);
        const auto joined = before.curEnd == p.curStart;
        const auto lo = before.curStart + 1, hi = p.curEnd - 1;
        if (to < lo || to > hi)
            continue;
        p.curStart = to;
        if (joined)
            before.curEnd = to;
        for (auto* q : { &before, &p })
        {
            const auto out = std::max<std::int64_t> (1, q->curEnd - q->curStart);
            q->stretch = (double) (q->origEnd - q->origStart) / (double) out;
            if (std::abs (q->stretch - 1.0) < 1e-9)
                q->stretch = 1.0;
        }
    }
    track.edited = std::any_of (track.pitches.begin(), track.pitches.end(), [&track] (const Note& p) { return p.edited (track.globalFormant); });
}

void stretchAll (Track& track, double ratio)
{
    if (! (ratio > 0.0) || ! std::isfinite (ratio))
        return;
    for (auto& p : track.pitches)
    {
        p.curStart = (std::int64_t) std::llround ((double) p.origStart * ratio);
        p.curEnd = std::max (p.curStart + 1, (std::int64_t) std::llround ((double) p.origEnd * ratio));
        p.stretch = std::abs (ratio - 1.0) < 1e-12 ? 1.0 : (double) (p.origEnd - p.origStart) / (double) (p.curEnd - p.curStart);
    }
    track.edited = std::any_of (track.pitches.begin(), track.pitches.end(), [&track] (const Note& p) { return p.edited (track.globalFormant); });
}

std::shared_ptr<SampleData> cropped (const SampleData& s, std::int64_t a, std::int64_t b)
{
    const auto n = (std::int64_t) s.frames();
    a = std::clamp<std::int64_t> (a, 0, n);
    b = std::clamp<std::int64_t> (b, 0, n);
    if (b <= a)
        a = 0, b = n;
    auto out = std::make_shared<SampleData>();
    out->sampleRate = s.sampleRate;
    out->left.assign (s.leftData() + a, s.leftData() + b);
    if (s.isStereo())
        out->right.assign (s.rightData() + a, s.rightData() + b);
    return out;
}

std::vector<std::uint8_t> writeNative (const Track& track)
{
    // (round 190li: a track that uses nothing of version 4 is written as version 3, byte for byte as before; rollup step 8:
    // version 5 only for a fine-split analysis)
    const auto v4 = track.engine != 0 || std::any_of (track.pitches.begin(), track.pitches.end(), [] (const Note& p) {
                        return p.modulation != 1.0 || p.pitchDrift != 1.0;
                    });
    return writeNative (track, track.fineSplit ? 5u : v4 ? 4u : 3u);
}

std::vector<std::uint8_t> writeNative (const Track& track, std::uint32_t version)
{
    // (version 2: what releases before round natives190kj read — rollup 0.1.30 step 6's Save a copy for 0.1.28)
    version = std::clamp (version, 2u, nativeVersion);
    std::vector<std::uint8_t> out { 'L', 'X', 'N', 'T' };
    put (out, version);
    put (out, (std::int64_t) track.frames);
    put (out, track.globalFormant);
    put (out, track.centre);
    put (out, track.variation);
    put (out, track.transition);
    if (version >= 3)
        put (out, (std::int32_t) track.key); // (version 3)
    if (version >= 4)
        put (out, (std::int32_t) track.engine); // (version 4)
    if (version >= 5)
        put (out, (std::uint8_t) (track.fineSplit ? 1 : 0)); // (version 5)
    put (out, (std::uint32_t) track.pitches.size());
    for (const auto& p : track.pitches)
    {
        for (const auto v : { p.origStart, p.origEnd, p.curStart, p.curEnd })
            put (out, v);
        for (const auto v : { p.origPitch, p.shift, p.stretch, p.volume, p.formant, p.drift, p.smoothIn, p.smoothOut, p.fadeInTime, p.fadeOutTime,
                              p.fadeInGain, p.fadeOutGain, p.vibPeriod, p.vibAmpStart, p.vibAmpEnd, p.tremAmp })
            put (out, v);
        put (out, (std::uint8_t) (p.enabled ? 1 : 0));
        put (out, p.rampIn);
        put (out, p.rampOut);
        put (out, p.origSmoothIn);
        put (out, p.origSmoothOut);
        if (version >= 3)
        {
            put (out, p.validEnd);
            put (out, p.velocity);
        }
        if (version >= 4)
        {
            put (out, p.modulation);
            put (out, p.pitchDrift);
            put (out, p.sungPitch);
        }
    }
    return out;
}

std::optional<Track> parseNative (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 8 || std::memcmp (state.data(), "LXNT", 4) != 0)
        return std::nullopt;
    Reader r { state, 4 };
    const auto version = r.get<std::uint32_t>();
    if (version < 1 || version > nativeVersion)
        return std::nullopt;
    Track t;
    t.frames = r.get<std::int64_t>();
    t.globalFormant = r.get<double>();
    if (version >= 2)
    {
        t.centre = r.get<double>();
        t.variation = r.get<double>();
        t.transition = r.get<double>();
    }
    if (version >= 3)
        t.key = r.get<std::int32_t>();
    if (version >= 4)
        t.engine = r.get<std::int32_t>();
    if (version >= 5)
        t.fineSplit = r.get<std::uint8_t>() != 0;
    const auto count = r.get<std::uint32_t>();
    if (! r.ok || count > 100000)
        return std::nullopt;
    for (std::uint32_t i = 0; i < count && r.ok; ++i)
    {
        Note p;
        for (auto* v : { &p.origStart, &p.origEnd, &p.curStart, &p.curEnd })
            *v = r.get<std::int64_t>();
        for (auto* v : { &p.origPitch, &p.shift, &p.stretch, &p.volume, &p.formant, &p.drift, &p.smoothIn, &p.smoothOut, &p.fadeInTime, &p.fadeOutTime,
                         &p.fadeInGain, &p.fadeOutGain, &p.vibPeriod, &p.vibAmpStart, &p.vibAmpEnd, &p.tremAmp })
            *v = r.get<double>();
        p.enabled = r.get<std::uint8_t>() != 0;
        if (version >= 2)
        {
            p.rampIn = r.get<double>();
            p.rampOut = r.get<double>();
            p.origSmoothIn = r.get<double>();
            p.origSmoothOut = r.get<double>();
        }
        else
        {
            p.origSmoothIn = p.smoothIn;
            p.origSmoothOut = p.smoothOut;
        }
        if (version >= 3)
        {
            p.validEnd = r.get<std::int64_t>();
            p.velocity = r.get<double>();
        }
        if (version >= 4)
        {
            p.modulation = r.get<double>();
            p.pitchDrift = r.get<double>();
            p.sungPitch = r.get<double>();
        }
        t.pitches.push_back (p);
    }
    if (! r.ok || r.at != state.size())
        return std::nullopt;
    t.notes = (int) t.pitches.size();
    t.edited = std::any_of (t.pitches.begin(), t.pitches.end(), [&t] (const Note& p) { return p.edited (t.globalFormant); });
    return t;
}

std::optional<Track> trackOf (const std::vector<std::uint8_t>& state)
{
    if (auto t = parseNative (state))
        return t;
    if (const auto s = parse (state); s && ! s->tracks.empty() && ! s->tracks.front().timeWarp)
        return s->tracks.front();
    return std::nullopt;
}

std::vector<std::uint8_t> writeWarp (const WarpMap& map)
{
    std::vector<std::uint8_t> out { 'L', 'X', 'N', 'W' };
    put (out, warpVersion);
    put (out, (std::int64_t) map.frames);
    put (out, (std::uint32_t) map.markers.size());
    for (const auto& m : map.markers)
    {
        put (out, m.source);
        put (out, m.output);
    }
    return out;
}

std::optional<WarpMap> parseWarp (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 8 || std::memcmp (state.data(), "LXNW", 4) != 0)
        return std::nullopt;
    Reader r { state, 4 };
    if (r.get<std::uint32_t>() != warpVersion)
        return std::nullopt;
    WarpMap m;
    m.frames = r.get<std::int64_t>();
    const auto count = r.get<std::uint32_t>();
    if (! r.ok || count > 100000)
        return std::nullopt;
    for (std::uint32_t i = 0; i < count && r.ok; ++i)
    {
        WarpMarker w;
        w.source = r.get<std::int64_t>();
        w.output = r.get<std::int64_t>();
        // (markers must increase on both axes)
        if (w.source <= 0 || w.output <= 0 || (! m.markers.empty() && (w.source <= m.markers.back().source || w.output <= m.markers.back().output)))
            return std::nullopt;
        m.markers.push_back (w);
    }
    if (! r.ok || r.at != state.size())
        return std::nullopt;
    return m;
}

namespace
{
// the markers with the implicit start and end ones: (source, output) points, increasing
std::vector<std::pair<double, double>> warpPoints (const WarpMap& map, std::int64_t frames)
{
    std::vector<std::pair<double, double>> pts { { 0.0, 0.0 } };
    for (const auto& m : map.markers) // (a marker at the very end: FL's last segment end, round 167b)
        if ((double) m.source <= (double) frames && (double) m.source > pts.back().first && (double) m.output > pts.back().second)
            pts.push_back ({ (double) m.source, (double) m.output });
    const auto& last = pts.back();
    const auto slope = pts.size() >= 2 ? (last.second - pts[pts.size() - 2].second) / std::max (1.0, last.first - pts[pts.size() - 2].first) : 1.0;
    if ((double) frames > last.first)
        pts.push_back ({ (double) frames, last.second + slope * ((double) frames - last.first) });
    return pts;
}
} // namespace

std::int64_t warpedLength (const WarpMap& map, std::int64_t frames)
{
    return (std::int64_t) std::llround (warpPoints (map, frames).back().second);
}

double warpToOutput (const WarpMap& map, std::int64_t frames, double source)
{
    const auto pts = warpPoints (map, frames);
    for (std::size_t i = 1; i < pts.size(); ++i)
        if (source <= pts[i].first || i + 1 == pts.size())
        {
            const auto& a = pts[i - 1];
            const auto& b = pts[i];
            return a.second + (source - a.first) * (b.second - a.second) / std::max (1e-9, b.first - a.first);
        }
    return source;
}

double warpToSource (const WarpMap& map, std::int64_t frames, double output)
{
    const auto pts = warpPoints (map, frames);
    for (std::size_t i = 1; i < pts.size(); ++i)
        if (output <= pts[i].second || i + 1 == pts.size())
        {
            const auto& a = pts[i - 1];
            const auto& b = pts[i];
            return a.first + (output - a.second) * (b.first - a.first) / std::max (1e-9, b.second - a.second);
        }
    return output;
}

std::shared_ptr<SampleData> renderWarp (const SampleData& source, const WarpMap& map)
{
    const auto n = (std::int64_t) source.frames();
    if (n <= 0)
        return nullptr;
    const auto pts = warpPoints (map, n);
    bool moved = false; // (FL's own analysis leaves its unmoved markers up to 2 samples off: still the source)
    for (const auto& [a, b] : pts)
        moved = moved || std::abs (a - b) > 2.5;
    if (! moved)
        return nullptr;

    auto out = std::make_shared<SampleData>();
    out->sampleRate = source.sampleRate;
    const auto stereo = source.isStereo();
    for (std::size_t i = 1; i < pts.size(); ++i)
    {
        const auto a = (std::int64_t) std::llround (pts[i - 1].first), b = (std::int64_t) std::llround (pts[i].first);
        const auto wantStart = (std::int64_t) std::llround (pts[i - 1].second), wantEnd = (std::int64_t) std::llround (pts[i].second);
        if (b <= a || wantEnd <= wantStart)
            continue;
        SampleData piece;
        piece.sampleRate = source.sampleRate;
        piece.left.assign (source.leftData() + a, source.leftData() + b);
        if (stereo)
            piece.right.assign (source.rightData() + a, source.rightData() + b);
        const auto ratio = (double) (wantEnd - wantStart) / (double) (b - a);
        std::shared_ptr<SampleData> stretched;
        if (std::abs (ratio - 1.0) > 1e-3)
            stretched = timeStretch (piece, ratio, StretchMethod::mono); // Newtone edits monophonic material
        else if (wantEnd - wantStart != b - a)
        {
            // FL's analysis leaves its segments a sample or two off (ratios like 1.0001): read the piece at that rate
            // (under 2 cents) instead of running the stretcher, so an unmoved segment stays the source
            stretched = std::make_shared<SampleData>();
            stretched->sampleRate = piece.sampleRate;
            const auto len = wantEnd - wantStart;
            const auto step = (double) (b - a - 1) / (double) std::max<std::int64_t> (1, len - 1);
            auto read = [] (const std::vector<float>& x, double at) {
                const auto i = (std::size_t) at;
                const auto f = (float) (at - (double) i);
                return i + 1 < x.size() ? x[i] + (x[i + 1] - x[i]) * f : x.back();
            };
            for (std::int64_t k = 0; k < len; ++k)
            {
                stretched->left.push_back (read (piece.left, (double) k * step));
                if (stereo)
                    stretched->right.push_back (read (piece.right, (double) k * step));
            }
        }
        const auto& src = stretched != nullptr ? *stretched : piece;
        // each piece lands exactly where its markers say (the stretcher's length is trimmed / padded)
        const auto length = (std::size_t) (wantEnd - wantStart);
        out->left.resize ((std::size_t) wantStart, 0.0f);
        if (stereo)
            out->right.resize ((std::size_t) wantStart, 0.0f);
        for (std::size_t k = 0; k < length; ++k)
        {
            const auto inside = k < src.frames();
            out->left.push_back (inside ? src.leftData()[k] : 0.0f);
            if (stereo)
                out->right.push_back (inside ? (src.isStereo() ? src.rightData()[k] : src.leftData()[k]) : 0.0f);
        }
    }
    return out;
}

std::optional<WarpMap> warpFromTrack (const Track& track)
{
    if (! track.timeWarp || track.pitches.empty())
        return std::nullopt;
    WarpMap m;
    m.frames = track.frames > 0 ? track.frames : track.pitches.back().origEnd;
    auto add = [&m] (std::int64_t source, std::int64_t output) {
        if (source <= 0 || output <= 0)
            return;
        if (! m.markers.empty() && (source <= m.markers.back().source || output <= m.markers.back().output))
            return;
        m.markers.push_back ({ source, output });
    };
    // Newtime's segments ("Pitch_N", REA_rodada167b §4): each starts at a warp marker, source iOrigStartSamplePos →
    // output iCurStartSamplePos; the last one's end is the end marker
    for (const auto& p : track.pitches)
        add (p.origStart, p.curStart);
    add (track.pitches.back().origEnd, track.pitches.back().curEnd);
    return m;
}

std::optional<WarpMap> warpOf (const std::vector<std::uint8_t>& state)
{
    if (auto native = parseWarp (state))
        return native;
    if (const auto s = parse (state))
        for (const auto& t : s->tracks)
            if (t.timeWarp)
                return warpFromTrack (t);
    return std::nullopt;
}

std::shared_ptr<const SampleData> renderedSource (std::shared_ptr<const SampleData> source, const std::vector<std::uint8_t>& state)
{
    if (source == nullptr)
        return source;
    if (const auto warp = warpOf (state))
    {
        if (auto warped = renderWarp (*source, *warp))
            return warped;
        return source;
    }
    if (const auto t = trackOf (state))
        if (auto edited = renderEdits (*source, *t))
            return edited;
    return source;
}

std::int64_t snapToBeatGrid (std::int64_t position, double sampleRate, double bpm, int division)
{
    if (division <= 0 || sampleRate <= 0.0 || bpm <= 0.0)
        return position;
    const auto step = sampleRate * 60.0 / bpm / division;
    return (std::int64_t) std::llround (std::round ((double) position / step) * step);
}

} // namespace lynx::newtonefx
