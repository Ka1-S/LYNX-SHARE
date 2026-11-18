#include "core/AudioToMidi.h"

#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <queue>
#include <utility>

#include "core/AudioEdit.h"
#include "core/NoteEdit.h"

namespace lynx::a2m
{
// --- Basic Pitch ---------------------------------------------------------------------------------------------------
bool runModel (const std::vector<float>& mono, int framesPerWindow,
               const std::function<bool (const float* in, float* note, float* onset)>& model,
               const std::function<void (double)>& progress, Posteriorgram& out)
{
    return runModelBatched (mono, framesPerWindow, 1, false,
                            [&model] (const float* in, int, float* note, float* onset, float*) { return model (in, note, onset); }, progress, out);
}

bool runModelBatched (const std::vector<float>& mono, int framesPerWindow, int batch, bool withContour,
                      const std::function<bool (const float* in, int count, float* note, float* onset, float* contour)>& model,
                      const std::function<void (double)>& progress, Posteriorgram& out)
{
    const auto samples = (std::int64_t) mono.size();
    const auto windows = numWindows (samples);
    const auto keep = framesPerWindow - overlapFrames; // the frames of a window that are kept (half the overlap off each side)
    batch = std::clamp (batch, 1, 16);
    out.frames = (int) std::max<std::int64_t> (1, (samples + hop - 1) / hop);
    out.note.assign ((std::size_t) out.frames * keys, 0.0f);
    out.onset.assign ((std::size_t) out.frames * keys, 0.0f);
    out.contour.assign (withContour ? (std::size_t) out.frames * contourBins : 0, 0.0f);
    if (keep <= 0)
        return false;

    const auto perWindow = (std::size_t) framesPerWindow * keys, perContour = (std::size_t) framesPerWindow * contourBins;
    std::vector<float> in ((std::size_t) windowSamples * (std::size_t) batch), note (perWindow * (std::size_t) batch), onset (note.size()),
        contour (withContour ? perContour * (std::size_t) batch : 0);
    for (int first = 0; first < windows; first += batch)
    {
        const auto count = std::min (batch, windows - first);
        for (int j = 0; j < count; ++j)
        {
            // window w starts half an overlap before sample w * stride: its frame i is at sample w * stride + (i - 15) * hop
            const auto from = (std::int64_t) (first + j) * windowStride - overlapSamples / 2;
            auto* dest = in.data() + (std::size_t) j * windowSamples;
            for (int i = 0; i < windowSamples; ++i)
            {
                const auto s = from + i;
                dest[i] = s >= 0 && s < samples ? mono[(std::size_t) s] : 0.0f;
            }
        }
        if (! model (in.data(), count, note.data(), onset.data(), withContour ? contour.data() : nullptr))
            return false;

        for (int j = 0; j < count; ++j)
        {
            const auto w = first + j;
            // every output frame k (sample k * hop) from the window whose kept part holds it: an exact time grid (Basic
            // Pitch's own unwrapping concatenates 142 kept frames per 141.27-frame stride and corrects the drift afterwards)
            const auto firstSample = (std::int64_t) w * windowStride;
            const auto lastSample = w + 1 < windows ? firstSample + windowStride : samples + hop;
            for (auto k = (firstSample + hop - 1) / hop; k * hop < lastSample && k < out.frames; ++k)
            {
                const auto local = std::clamp<std::int64_t> (overlapFrames / 2 + std::llround ((double) (k * hop - firstSample) / hop), 0, framesPerWindow - 1);
                std::copy_n (note.data() + (std::size_t) j * perWindow + (std::size_t) local * keys, keys, out.note.data() + k * keys);
                std::copy_n (onset.data() + (std::size_t) j * perWindow + (std::size_t) local * keys, keys, out.onset.data() + k * keys);
                if (withContour)
                    std::copy_n (contour.data() + (std::size_t) j * perContour + (std::size_t) local * contourBins, contourBins, out.contour.data() + k * contourBins);
            }
        }
        if (progress)
            progress ((double) (first + count) / (double) windows);
    }
    return true;
}

namespace
{
double sincOf (double x) noexcept
{
    if (std::abs (x) < 1.0e-9)
        return 1.0;
    const auto px = 3.141592653589793 * x;
    return std::sin (px) / px;
}

double blackmanOf (double x) noexcept // x in [-1, 1]
{
    if (std::abs (x) >= 1.0)
        return 0.0;
    const auto t = (x + 1.0) * 0.5;
    return 0.42 - 0.5 * std::cos (2.0 * 3.141592653589793 * t) + 0.08 * std::cos (4.0 * 3.141592653589793 * t);
}

std::int64_t gcdOf (std::int64_t a, std::int64_t b) noexcept
{
    while (b != 0)
        a = std::exchange (b, a % b);
    return a;
}
} // namespace

std::vector<float> toModelRate (const std::vector<float>& mono, double rate, const std::function<bool()>& stop)
{
    const auto n = (std::int64_t) mono.size();
    if (n == 0 || ! (rate > 0.0))
        return {};
    const auto outFrames = std::max<std::int64_t> (1, std::llround ((double) n * modelRate / rate));
    if (std::abs (rate - modelRate) < 1.0)
        return mono;
    const auto whole = (std::int64_t) std::llround (rate);
    const auto g = gcdOf ((std::int64_t) modelRate, whole);
    const std::int64_t up = (std::int64_t) modelRate / g, down = (std::int64_t) whole / g; // input frames per output frame = down / up
    if (std::abs ((double) whole - rate) > 1.0e-9 || up > 4096)
        return audioedit::resampleChannel (mono, outFrames, stop);
    // (resampleChannel steps n / outFrames, a hair off the rate ratio when n * up / down isn't whole: this keeps the
    // exact ratio, at most half an input frame apart at the end; equal to it when the length divides)

    const auto step = (double) down / (double) up;
    const auto cutoff = std::min (1.0, 1.0 / step);
    const auto halfWidth = 16.0 / cutoff;
    // output o reads around t = (o + 0.5) * step - 0.5 = ((2o + 1) * down - up) / (2 up): its phase is o mod up
    struct Phase
    {
        std::int64_t first = 0; // the first tap's input frame for the phase's first output
        std::vector<double> taps;
        double weights = 0.0;
    };
    std::vector<Phase> phases ((std::size_t) up);
    for (std::int64_t p = 0; p < up; ++p)
    {
        const auto t = ((double) p + 0.5) * step - 0.5;
        auto& ph = phases[(std::size_t) p];
        ph.first = (std::int64_t) std::ceil (t - halfWidth);
        const auto last = (std::int64_t) std::floor (t + halfWidth);
        for (auto k = ph.first; k <= last; ++k)
        {
            const auto x = t - (double) k;
            const auto w = cutoff * sincOf (cutoff * x) * blackmanOf (x / halfWidth);
            ph.taps.push_back (w);
            ph.weights += w;
        }
    }
    std::vector<float> out ((std::size_t) outFrames, 0.0f);
    for (std::int64_t o = 0; o < outFrames; ++o)
    {
        if ((o & 0xffff) == 0 && stop && stop())
            return {};
        const auto& ph = phases[(std::size_t) (o % up)];
        const auto first = ph.first + (o / up) * down; // phase p + up * m sits m * down input frames later
        double sum = 0.0;
        const auto taps = (std::int64_t) ph.taps.size();
        if (first >= 0 && first + taps <= n)
        {
            const auto* x = mono.data() + first;
            for (std::int64_t k = 0; k < taps; ++k)
                sum += ph.taps[(std::size_t) k] * x[k];
        }
        else // edges: the nearest sample continues (no dip to zero)
            for (std::int64_t k = 0; k < taps; ++k)
                sum += ph.taps[(std::size_t) k] * mono[(std::size_t) std::clamp<std::int64_t> (first + k, 0, n - 1)];
        out[(std::size_t) o] = (float) (ph.weights != 0.0 ? sum / ph.weights : 0.0);
    }
    return out;
}

namespace
{
// note_creation.py's get_infered_onsets: the predicted onsets, or the smallest rise of the note posteriorgram over the
// last 1 and 2 frames (rescaled to the onsets' maximum) where that is larger
std::vector<float> inferOnsets (const Posteriorgram& pg)
{
    const auto n = (std::size_t) pg.frames;
    std::vector<float> diff (n * keys, 0.0f);
    float diffMax = 0.0f, onsetMax = 0.0f;
    for (std::size_t t = 2; t < n; ++t)
        for (std::size_t b = 0; b < (std::size_t) keys; ++b)
        {
            const auto v = pg.note[t * keys + b];
            const auto d = std::max (0.0f, std::min (v - pg.note[(t - 1) * keys + b], v - pg.note[(t - 2) * keys + b]));
            diff[t * keys + b] = d;
            diffMax = std::max (diffMax, d);
        }
    for (auto v : pg.onset)
        onsetMax = std::max (onsetMax, v);
    std::vector<float> out (pg.onset);
    if (diffMax > 0.0f)
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = std::max (out[i], onsetMax * diff[i] / diffMax);
    return out;
}

float meanOf (const Posteriorgram& pg, int from, int to, int bin)
{
    if (to <= from)
        return 0.0f;
    double sum = 0.0;
    for (int t = from; t < to; ++t)
        sum += pg.note[(std::size_t) t * keys + (std::size_t) bin];
    return (float) (sum / (to - from));
}
} // namespace

std::vector<Note> notesFromPosteriorgram (const Posteriorgram& pg, const NoteOptions& options, double timeOffset)
{
    const auto n = pg.frames;
    std::vector<Note> notes;
    if (n < 3 || (int) pg.note.size() < n * keys)
        return notes;

    // constrain_frequency: bins outside the key range are silent
    auto frames = pg.note;
    auto onsets = pg.onset;
    const auto lowBin = std::clamp (options.minKey - lowestKey, 0, keys), highBin = std::clamp (options.maxKey - lowestKey + 1, 0, keys);
    for (int t = 0; t < n; ++t)
        for (int b = 0; b < keys; ++b)
            if (b < lowBin || b >= highBin)
                frames[(std::size_t) (t * keys + b)] = onsets[(std::size_t) (t * keys + b)] = 0.0f;
    Posteriorgram constrained { n, frames, onsets, {} };
    if (options.inferOnsets)
        onsets = inferOnsets (constrained);

    const auto minLen = (int) std::lround (options.minNoteSeconds * modelRate / hop);
    const auto tol = options.energyTolerance;
    const auto thr = options.frameThreshold;
    auto at = [] (std::vector<float>& m, int t, int b) -> float& { return m[(std::size_t) t * keys + (std::size_t) b]; };
    auto remaining = frames;
    auto toTime = [&] (int frame) { return (double) frame * hop / modelRate + timeOffset; };
    auto add = [&] (int from, int to, int bin) {
        notes.push_back ({ toTime (from), toTime (to), bin + lowestKey, std::clamp (meanOf (constrained, from, to, bin), 0.0f, 1.0f), {} });
    };

    // onset peaks (scipy's argrelmax along time: strictly above both neighbours), latest first, high keys first at a tie
    for (int t = n - 2; t >= 1; --t)
        for (int b = keys - 1; b >= 0; --b)
        {
            const auto v = at (onsets, t, b);
            if (v < options.onsetThreshold || v <= at (onsets, t - 1, b) || v <= at (onsets, t + 1, b))
                continue;
            if (t >= n - 1)
                continue;
            // where the energy at this key stays below the threshold for `tol` frames
            auto i = t + 1;
            auto k = 0;
            while (i < n - 1 && k < tol)
            {
                k = at (remaining, i, b) < thr ? k + 1 : 0;
                ++i;
            }
            i -= k;
            if (i - t <= minLen)
                continue;
            for (int f = t; f < i; ++f)
                for (int nb = std::max (0, b - 1); nb <= std::min (keys - 1, b + 1); ++nb)
                    at (remaining, f, nb) = 0.0f;
            add (t, i, b);
        }

    if (options.melodiaTrick)
    {
        // the strongest frame left, again and again (numpy's argmax: the first of equal values); values only ever drop
        // to 0, so a queue of the frames above the threshold with stale entries skipped gives the same order
        using Entry = std::pair<float, int>; // value, -(t * keys + b)
        std::priority_queue<Entry> queue;
        for (int i = 0; i < n * keys; ++i)
            if (remaining[(std::size_t) i] > thr)
                queue.push ({ remaining[(std::size_t) i], -i });
        while (! queue.empty())
        {
            const auto [value, negIndex] = queue.top();
            queue.pop();
            const auto index = -negIndex;
            if (remaining[(std::size_t) index] != value)
                continue; // (cleared since)
            const auto mid = index / keys, b = index % keys;
            remaining[(std::size_t) index] = 0.0f;
            auto clear = [&] (int f) {
                for (int nb = std::max (0, b - 1); nb <= std::min (keys - 1, b + 1); ++nb)
                    at (remaining, f, nb) = 0.0f;
            };

            auto i = mid + 1;
            auto k = 0;
            while (i < n - 1 && k < tol)
            {
                k = at (remaining, i, b) < thr ? k + 1 : 0;
                clear (i);
                ++i;
            }
            const auto end = i - 1 - k;

            i = mid - 1;
            k = 0;
            while (i > 0 && k < tol)
            {
                k = at (remaining, i, b) < thr ? k + 1 : 0;
                clear (i);
                --i;
            }
            const auto start = i + 1 + k;
            if (end - start <= minLen)
                continue;
            add (start, end, b);
        }
    }

    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.key < b.key; });
    return notes;
}

std::vector<Note> monophonic (std::vector<Note> notes, double minSeconds)
{
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.velocity > b.velocity; });
    std::vector<Note> kept;
    for (std::size_t i = 0; i < notes.size();)
    {
        // notes that start together (within 50 ms): the loudest one (an overtone is weaker than its fundamental)
        auto best = i;
        auto j = i + 1;
        for (; j < notes.size() && notes[j].start - notes[i].start < 0.05; ++j)
            if (notes[j].velocity > notes[best].velocity)
                best = j;
        const auto n = notes[best];
        i = j;
        if (! kept.empty())
        {
            auto& last = kept.back();
            // a weaker note inside a louder one that goes on for most of it: an overtone or a re-attack of the
            // sounding note, not a new one
            if (n.start < last.end && last.velocity > n.velocity * 1.3f && last.end - n.start > 0.5 * (n.end - n.start))
                continue;
            last.end = std::min (last.end, n.start); // one note at a time: the new one ends the last
            if (last.end - last.start < minSeconds)
                kept.pop_back();
        }
        kept.push_back (n);
    }
    return kept;
}

NoteOptions optionsFor (Mode mode) noexcept
{
    NoteOptions options;
    options.onsetThreshold = 0.7f;
    options.frameThreshold = 0.4f;
    options.melodiaTrick = mode == Mode::melody;
    options.minNoteSeconds = mode == Mode::melody ? 0.05 : 0.1277;
    return options;
}

namespace
{
// the Melody mode's decoder after note creation: one note at a time, each start moved back to where its key's
// activation reached half its level (at most 5 frames, never into the previous note)
std::vector<Note> melodyOf (const Posteriorgram& pg, std::vector<Note> notes, double minSeconds)
{
    notes = monophonic (std::move (notes), minSeconds);
    auto previousEnd = -1.0;
    for (auto& n : notes)
    {
        const auto bin = n.key - lowestKey;
        const auto frame = (int) std::lround (n.start * modelRate / hop);
        if (bin < 0 || bin >= keys || frame < 0 || frame >= pg.frames)
            continue;
        double level = 0.0;
        auto count = 0;
        for (auto f = frame; f < std::min (pg.frames, frame + 4); ++f, ++count)
            level += pg.note[(std::size_t) f * keys + (std::size_t) bin];
        level /= std::max (1, count);
        auto start = frame;
        while (start > 0 && frame - start < 5 && pg.note[(std::size_t) (start - 1) * keys + (std::size_t) bin] >= 0.5 * level
               && (double) (start - 1) * hop / modelRate > previousEnd)
            --start;
        previousEnd = n.end;
        n.start = (double) start * hop / modelRate;
    }
    return notes;
}
} // namespace

std::vector<Note> transcribe (const Posteriorgram& pg, Mode mode)
{
    auto notes = notesFromPosteriorgram (pg, optionsFor (mode));
    if (mode != Mode::melody)
        return notes;
    return melodyOf (pg, std::move (notes), 0.05);
}

// --- "Convert to MIDI (polyphonic)" with options (round 190lj) ---------------------------------------------------------
NoteOptions noteOptionsOf (const ConvertOptions& o) noexcept
{
    NoteOptions n;
    n.frameThreshold = std::clamp (1.0f - o.noteSensitivity, 0.05f, 0.95f);
    n.onsetThreshold = std::clamp (1.0f - o.splitSensitivity, 0.05f, 0.95f);
    n.minNoteSeconds = std::clamp (o.minNoteMs, 0.0, 5000.0) / 1000.0;
    n.melodiaTrick = ! o.polyphonic;
    n.minKey = std::clamp (std::min (o.minKey, o.maxKey), lowestKey, lowestKey + keys - 1);
    n.maxKey = std::clamp (std::max (o.minKey, o.maxKey), lowestKey, lowestKey + keys - 1);
    return n;
}

std::vector<Note> transcribe (const Posteriorgram& pg, const ConvertOptions& options)
{
    const auto settings = noteOptionsOf (options);
    auto notes = notesFromPosteriorgram (pg, settings);
    if (! options.polyphonic)
        notes = melodyOf (pg, std::move (notes), settings.minNoteSeconds);
    if (options.pitchBends)
        addPitchBends (pg, notes);
    return notes;
}

void addPitchBends (const Posteriorgram& pg, std::vector<Note>& notes, BendCentre where)
{
    if (pg.frames <= 0 || pg.contour.size() < (std::size_t) pg.frames * contourBins)
        return;
    constexpr int tolerance = 25, window = 2 * tolerance + 1;
    // scipy.signal.windows.gaussian (51, std=5)
    std::array<double, window> gaussian {};
    for (int i = 0; i < window; ++i)
        gaussian[(std::size_t) i] = std::exp (-0.5 * std::pow ((double) (i - tolerance) / 5.0, 2.0));
    for (auto& n : notes)
    {
        n.bends.clear();
        const auto from = std::clamp ((int) std::lround (n.start * modelRate / hop), 0, pg.frames);
        const auto to = std::clamp ((int) std::lround (n.end * modelRate / hop), from, pg.frames);
        if (to <= from)
            continue;
        // midi_pitch_to_contour_bin: 36 log2 (hz / 27.5); the model's own answer for the key: one bin up
        const auto centre = contourPerKey * (n.key - lowestKey) + (where == BendCentre::measured ? 1 : 0);
        const auto low = std::max (centre - tolerance, 0), high = std::min (contourBins, centre + tolerance + 1);
        std::vector<int> values;
        values.reserve ((std::size_t) (to - from));
        for (int t = from; t < to; ++t)
        {
            const auto* row = pg.contour.data() + (std::size_t) t * contourBins;
            auto best = low;
            auto bestValue = -1.0;
            for (int b = low; b < high; ++b)
            {
                const auto v = (double) row[b] * gaussian[(std::size_t) (b - centre + tolerance)];
                if (v > bestValue) // (numpy's argmax: the first of equal values)
                {
                    bestValue = v;
                    best = b;
                }
            }
            values.push_back (best - centre); // 1/3 semitones
        }
        if (std::all_of (values.begin(), values.end(), [] (int v) { return v == 0; }))
            continue;
        // note_events_to_midi spreads a note's bends evenly from its start to its end (np.linspace)
        const auto span = n.end - n.start;
        const auto count = (int) values.size();
        auto timeOf = [&] (int i) { return count > 1 ? span * (double) i / (double) (count - 1) : 0.0; };
        auto centsOf = [] (int v) { return (float) v * 100.0f / (float) contourPerKey; };
        n.bends.push_back ({ 0.0, centsOf (values[0]) });
        for (int i = 1; i < count; ++i)
            if (values[(std::size_t) i] != values[(std::size_t) (i - 1)])
            {
                // a step: the value held up to the frame before, then the new one (the curve is linear in between)
                if (n.bends.back().seconds < timeOf (i - 1))
                    n.bends.push_back ({ timeOf (i - 1), centsOf (values[(std::size_t) (i - 1)]) });
                n.bends.push_back ({ timeOf (i), centsOf (values[(std::size_t) i]) });
            }
    }
}

// --- drums ---------------------------------------------------------------------------------------------------------
// Semi-adaptive NMF drum transcription (after Dittmar & Gärtner, "Real-time transcription and separation of drum
// recordings based on NMF decomposition", DAFx 2014): the magnitude spectrogram on 1/6-octave bands is factored into
// three spectral templates (kick, snare, hi-hat; generic shapes that adapt halfway to the clip) times their activations;
// a hit is a peak of an activation's rise.
namespace
{
void fft (std::vector<std::complex<float>>& a)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto angle = -2.0 * 3.141592653589793 / (double) len;
        const std::complex<float> step ((float) std::cos (angle), (float) std::sin (angle));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}
} // namespace

std::vector<Note> drumHits (const float* mono, std::int64_t count, double rate, const DrumOptions& options)
{
    std::vector<Note> hits;
    if (mono == nullptr || count <= 0 || rate <= 0.0)
        return hits;

    // ~46 ms windows every ~5.8 ms (2048 / 256 at 44.1 kHz)
    std::size_t size = 256;
    while ((double) size < rate * 0.046)
        size <<= 1;
    const auto step = std::max (1, (int) std::lround (rate * 256.0 / 44100.0));
    const auto frames = (int) (count / step) + 1;
    const auto fps = rate / step;

    // 1/6-octave bands from 30 Hz to 16 kHz (or 0.45 x the rate)
    const auto top = std::min (16000.0, rate * 0.45);
    std::vector<double> edges;
    for (double f = 30.0; f < top * 1.0001; f *= std::exp2 (1.0 / 6.0))
        edges.push_back (f);
    const auto numBands = (int) edges.size() - 1;
    if (numBands < 6)
        return hits;
    std::vector<int> bandOf (size / 2 + 1, -1);
    for (std::size_t bin = 0; bin <= size / 2; ++bin)
    {
        const auto hz = (double) bin * rate / (double) size;
        for (int b = 0; b < numBands; ++b)
            if (hz >= edges[(std::size_t) b] && hz < edges[(std::size_t) b + 1])
                bandOf[bin] = b;
    }
    // a band narrower than a bin takes the nearest bin
    std::vector<int> nearest ((std::size_t) numBands, -1);
    for (int b = 0; b < numBands; ++b)
        if (std::find (bandOf.begin(), bandOf.end(), b) == bandOf.end())
            nearest[(std::size_t) b] = (int) std::lround (std::sqrt (edges[(std::size_t) b] * edges[(std::size_t) b + 1]) * (double) size / rate);

    // V: bands x frames (frame t centred on sample t * step)
    const auto nb = (std::size_t) numBands, nt = (std::size_t) frames;
    std::vector<float> v (nb * nt, 1e-9f);
    std::vector<float> window (size);
    for (std::size_t i = 0; i < size; ++i)
        window[i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) i / (double) (size - 1)));
    std::vector<std::complex<float>> buffer (size);
    std::vector<float> magnitude (size / 2 + 1);
    for (std::size_t t = 0; t < nt; ++t)
    {
        const auto from = (std::int64_t) t * step - (std::int64_t) size / 2;
        for (std::size_t i = 0; i < size; ++i)
        {
            const auto s = from + (std::int64_t) i;
            buffer[i] = { s >= 0 && s < count ? mono[s] * window[i] : 0.0f, 0.0f };
        }
        fft (buffer);
        for (std::size_t bin = 0; bin <= size / 2; ++bin)
            magnitude[bin] = std::abs (buffer[bin]);
        for (std::size_t bin = 0; bin <= size / 2; ++bin)
            if (bandOf[bin] >= 0)
                v[(std::size_t) bandOf[bin] * nt + t] += magnitude[bin];
        for (std::size_t b = 0; b < nb; ++b)
            if (nearest[b] >= 0 && nearest[b] <= (int) (size / 2))
                v[b * nt + t] += magnitude[(std::size_t) nearest[b]];
    }

    // templates: kick around 60 Hz (with a little click), snare body ~200 Hz plus noise around 2.5 kHz, hi-hat ~10 kHz
    constexpr int numParts = 3;
    std::vector<float> w0 (nb * numParts);
    auto bump = [] (double logHz, double hz, double width) { const auto d = (logHz - std::log2 (hz)) / width; return std::exp (-0.5 * d * d); };
    for (std::size_t b = 0; b < nb; ++b)
    {
        const auto l = std::log2 (std::sqrt (edges[b] * edges[b + 1]));
        w0[b * numParts + 0] = (float) (bump (l, 60.0, 0.7) + 0.05 * bump (l, 3000.0, 1.0));
        w0[b * numParts + 1] = (float) (0.8 * bump (l, 200.0, 0.6) + 0.5 * bump (l, 2500.0, 1.6));
        w0[b * numParts + 2] = (float) bump (l, 10000.0, 0.8);
    }
    auto normalise = [nb] (std::vector<float>& w) {
        for (int k = 0; k < numParts; ++k)
        {
            double sum = 0.0;
            for (std::size_t b = 0; b < nb; ++b)
                sum += w[b * numParts + (std::size_t) k];
            for (std::size_t b = 0; b < nb; ++b)
                w[b * numParts + (std::size_t) k] = (float) (w[b * numParts + (std::size_t) k] / std::max (1e-12, sum));
        }
    };
    normalise (w0);
    auto w = w0;
    double mean = 0.0;
    for (auto x : v)
        mean += x;
    mean /= (double) v.size();
    std::vector<float> h ((std::size_t) numParts * nt, (float) (mean / numParts));

    // KL-divergence multiplicative updates; the templates adapt (half way back to the generic ones) after 20 rounds
    std::vector<float> ratio (nb * nt);
    auto computeRatio = [&] {
        for (std::size_t b = 0; b < nb; ++b)
            for (std::size_t t = 0; t < nt; ++t)
            {
                double wh = 1e-9;
                for (int k = 0; k < numParts; ++k)
                    wh += (double) w[b * numParts + (std::size_t) k] * h[(std::size_t) k * nt + t];
                ratio[b * nt + t] = (float) (v[b * nt + t] / wh);
            }
    };
    for (int round = 0; round < 60; ++round)
    {
        computeRatio();
        for (int k = 0; k < numParts; ++k)
        {
            double wsum = 0.0;
            for (std::size_t b = 0; b < nb; ++b)
                wsum += w[b * numParts + (std::size_t) k];
            for (std::size_t t = 0; t < nt; ++t)
            {
                double num = 0.0;
                for (std::size_t b = 0; b < nb; ++b)
                    num += (double) w[b * numParts + (std::size_t) k] * ratio[b * nt + t];
                h[(std::size_t) k * nt + t] = (float) (h[(std::size_t) k * nt + t] * num / (wsum + 1e-9));
            }
        }
        if (round < 20)
            continue;
        computeRatio();
        auto adapted = w;
        for (int k = 0; k < numParts; ++k)
        {
            double hsum = 0.0;
            for (std::size_t t = 0; t < nt; ++t)
                hsum += h[(std::size_t) k * nt + t];
            for (std::size_t b = 0; b < nb; ++b)
            {
                double num = 0.0;
                for (std::size_t t = 0; t < nt; ++t)
                    num += (double) h[(std::size_t) k * nt + t] * ratio[b * nt + t];
                adapted[b * numParts + (std::size_t) k] = (float) (w[b * numParts + (std::size_t) k] * num / (hsum + 1e-9));
            }
        }
        normalise (adapted);
        for (std::size_t i = 0; i < w.size(); ++i)
            w[i] = 0.5f * w0[i] + 0.5f * adapted[i];
    }

    // hits: peaks of each activation's rise above a share of its largest rise (lower with more sensitivity), at least
    // 30 ms apart. A snare also has to ring (40 ms after its peak its activation still holds 30 %: a kick's click lights
    // the snare's template for a few ms only) or be about as loud as the loudest snares
    const auto threshold = 0.25f - 0.2f * std::clamp (options.sensitivity, 0.0f, 1.0f);
    const auto gap = std::max (1, (int) std::lround (0.03 * fps));
    const auto ring = std::max (1, (int) std::lround (0.04 * fps));
    const int keysOf[numParts] { kickKey, snareKey, hatKey };
    std::vector<float> rise (nt);
    for (int k = 0; k < numParts; ++k)
    {
        const auto* act = h.data() + (std::size_t) k * nt;
        const auto peak = std::max (1e-12f, *std::max_element (act, act + nt));
        for (std::size_t t = 0; t < nt; ++t)
            rise[t] = std::max (0.0f, (act[t] - (t > 0 ? act[t - 1] : act[0])) / peak);
        // (smoothed over two frames)
        for (std::size_t t = nt; t-- > 1;)
            rise[t] = 0.5f * (rise[t] + rise[t - 1]);
        const auto riseMax = *std::max_element (rise.begin(), rise.end());
        if (riseMax <= 0.0f)
            continue;
        auto last = -gap;
        for (int t = 1; t + 1 < frames; ++t)
        {
            const auto r = rise[(std::size_t) t];
            if (r < threshold * riseMax || t - last < gap)
                continue;
            auto isPeak = true;
            for (int g = std::max (0, t - gap); g <= std::min (frames - 1, t + gap) && isPeak; ++g)
                isPeak = rise[(std::size_t) g] <= r;
            if (! isPeak)
                continue;
            const auto top = (int) (std::max_element (act + t, act + std::min (frames, t + gap / 2 + 1)) - act);
            if (keysOf[k] == snareKey && act[std::min (frames - 1, top + ring)] < 0.3f * act[top] && act[top] < 0.92f * peak)
                continue; // (unless it is as loud as the loudest snares: a short clap-like snare layered on a kick)
            last = t;
            const auto time = std::max (0.0, ((double) t - 0.5) * step / rate);
            hits.push_back ({ time, time + 0.0625, keysOf[k], std::clamp (act[std::min (frames - 1, t + 3)] / peak, 0.1f, 1.0f), {} });
        }
    }

    // the exact time: the ~46 ms analysis window places a kick late; each hit moves to the sharpest rise of the signal's
    // level (1.5 ms steps over 3 ms) from 20 ms before to 5 ms after where the activation rose, if it rises by 3 dB
    const auto fine = std::max (1, (int) std::lround (rate * 0.0015));
    const auto fineFrames = (std::size_t) (count / fine) + 1;
    std::vector<float> level (fineFrames);
    for (std::size_t f = 0; f < fineFrames; ++f)
    {
        double sum = 0.0;
        const auto from = (std::int64_t) f * fine;
        for (auto i = from; i < std::min<std::int64_t> (count, from + 2 * fine); ++i)
            sum += (double) mono[i] * mono[i];
        level[f] = (float) (10.0 * std::log10 (sum / (2.0 * fine) + 1e-12));
    }
    for (auto& hit : hits)
    {
        const auto at = (std::int64_t) std::llround (hit.start * rate / fine);
        auto best = at;
        auto bestRise = -1.0e9f;
        for (auto f = std::max<std::int64_t> (2, at - (std::int64_t) std::lround (0.02 * rate / fine));
             f <= std::min<std::int64_t> ((std::int64_t) fineFrames - 1, at + (std::int64_t) std::lround (0.005 * rate / fine)); ++f)
        {
            const auto rise = level[(std::size_t) f] - level[(std::size_t) f - 2];
            if (rise > bestRise)
            {
                bestRise = rise;
                best = f;
            }
        }
        if (bestRise < 3.0f)
            continue;
        hit.start = std::max (0.0, (double) (best - 1) * fine / rate);
        hit.end = hit.start + 0.0625;
    }
    std::sort (hits.begin(), hits.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.key < b.key; });
    return hits;
}

// --- into the song --------------------------------------------------------------------------------------------------
FrameRange clipFrames (const Clip& clip, const ClipTiming& timing, std::int64_t fileFrames) noexcept
{
    const auto a = (double) clip.offset * timing.framesPerTick, b = (double) (clip.offset + clip.length) * timing.framesPerTick;
    // forwards: zero + [a, b); backwards: the span read from its end
    const auto from = timing.backwards ? timing.zeroFrame + timing.spanFrames - b : timing.zeroFrame + a;
    const auto to = timing.backwards ? timing.zeroFrame + timing.spanFrames - a : timing.zeroFrame + b;
    const auto first = std::clamp<std::int64_t> ((std::int64_t) std::floor (from), 0, fileFrames);
    const auto last = std::clamp<std::int64_t> ((std::int64_t) std::ceil (to), first, fileFrames);
    return { first, last - first };
}

bool canPlayNotes (const ChannelDef& channel) noexcept
{
    switch (channel.instrument)
    {
        case InstrumentKind::audioClip:
        case InstrumentKind::fruitydance:
        case InstrumentKind::videoplayer:
        case InstrumentKind::dashboard:
            return false;
        default:
            return true;
    }
}

int addPattern (Song& song, Mode mode, const std::vector<Note>& notes, const std::function<double (double)>& tickOf,
                std::int64_t lengthTicks, const Names& names, const Placement& place)
{
    const auto drums = mode == Mode::drums;
    // round 190lj: an existing channel plays them (melody / harmony), else new channels at the end of the rack
    const auto existing = ! drums && place.channel >= 0 && place.channel < (int) song.channels.size()
                       && canPlayNotes (song.channels[(std::size_t) place.channel]);
    const auto firstChannel = existing ? place.channel : (int) song.channels.size();
    const auto grid = drums ? (std::int64_t) 0 : std::max<std::int64_t> (0, place.quantizeTicks);
    Pattern pattern;
    pattern.name = names.pattern;
    const auto limit = lengthTicks > 0 ? (double) lengthTicks : std::numeric_limits<double>::max();
    for (const auto& n : notes)
    {
        auto from = tickOf (n.start), to = tickOf (n.end);
        if (from > to)
            std::swap (from, to); // (a clip that plays backwards)
        if (to <= 0.0 || from >= limit)
            continue;
        lynx::Note note;
        note.start = (std::int64_t) std::llround (std::max (0.0, from));
        note.channel = drums ? firstChannel + (n.key == kickKey ? 0 : n.key == snareKey ? 1 : 2) : firstChannel;
        note.key = drums ? 60 : n.key;
        note.length = drums ? (std::int64_t) ticksPerStep : std::max<std::int64_t> (1, (std::int64_t) std::llround (std::min (limit, to)) - note.start);
        note.velocity = std::clamp (n.velocity, 0.05f, 1.0f);
        if (grid > 0)
        {
            // to the nearest grid line at both ends, one grid step at least, still inside the clip
            const auto end = (std::int64_t) std::llround (std::min (limit, to) / (double) grid) * grid;
            note.start = (std::int64_t) std::llround (std::max (0.0, from) / (double) grid) * grid;
            if ((double) note.start >= limit)
                note.start = std::max<std::int64_t> (0, (std::int64_t) std::ceil (limit / (double) grid) * grid - grid);
            note.length = std::max (grid, end - note.start);
            if ((double) (note.start + note.length) > limit && limit < std::numeric_limits<double>::max())
                note.length = std::max<std::int64_t> (1, (std::int64_t) std::llround (limit) - note.start);
        }
        if (! drums && ! n.bends.empty())
        {
            // the bends in the note's pitch curve, ticks from where it sounds (before any quantizing: the bend keeps its
            // place in the note)
            NoteCurves curves;
            auto& lane = curves.lane (NoteCurveLane::pitch);
            const auto origin = std::max (0.0, from);
            for (const auto& b : n.bends)
            {
                // (the song tick of the bend: in a reversed clip the note's last bends come first, sorted below)
                const auto at = tickOf (n.start + b.seconds);
                if (at < 0.0 || at >= limit)
                    continue;
                lane.push_back ({ (float) std::max (0.0, at - origin), b.cents });
            }
            if (! lane.empty())
            {
                curves.normalise();
                if (lane.size() > 1 || lane.front().value != 0.0f)
                    note.curves = std::move (curves);
            }
        }
        pattern.notes.push_back (note);
    }
    if (pattern.notes.empty())
        return -1;
    pattern.sortNotes();

    auto addChannel = [&] (const std::string& name, InstrumentKind kind) {
        ChannelDef channel;
        channel.name = name;
        channel.instrument = kind;
        channel.mixerTrack = song.claimFreeInsert();
        song.channels.push_back (channel);
    };
    if (drums)
    {
        addChannel (names.kick, InstrumentKind::kick);
        addChannel (names.snare, InstrumentKind::snare);
        addChannel (names.hat, InstrumentKind::hat);
    }
    else if (! existing)
        addChannel (names.channel, InstrumentKind::synth);
    song.patterns.push_back (std::move (pattern));
    return (int) song.patterns.size() - 1;
}

int addClipPattern (Song& song, int clipIndex, const std::string& sourcePath, Mode mode, const std::vector<Note>& notes,
                    std::int64_t analysedFirst, const ClipTiming& timing, const Names& names, const Placement& place)
{
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size() || timing.framesPerTick <= 0.0)
        return -1;
    const auto source = song.playlist[(std::size_t) clipIndex];
    if (source.kind != ClipKind::audio || source.channel < 0 || source.channel >= (int) song.channels.size()
        || song.channels[(std::size_t) source.channel].samplePath != sourcePath)
        return -1;

    // clip ticks of a time in the analysed audio
    const auto tickOf = [&] (double seconds) {
        const auto frame = (double) analysedFirst + seconds * timing.sampleRate;
        const auto fromZero = timing.backwards ? timing.spanFrames - (frame - timing.zeroFrame) : frame - timing.zeroFrame;
        return fromZero / timing.framesPerTick - (double) source.offset;
    };
    const auto patternIndex = addPattern (song, mode, notes, tickOf, source.length, names, place);
    if (patternIndex < 0)
        return -1;

    const auto track = source.track + 1;
    noteedit::insertTrack (song, track);
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = patternIndex;
    clip.track = track;
    clip.start = source.start;
    clip.length = source.length;
    song.playlist.push_back (clip);
    return patternIndex;
}
} // namespace lynx::a2m
