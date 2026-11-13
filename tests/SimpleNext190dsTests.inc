// --- Round 190ds: the Simple layout's next block — the keyboard's Scale keys / Chord pad / Lock to key (core/ScaleKeys.h),
// loops that follow the project key (core/KeyFollow.h, ProjectKey retune), and a pattern for every new Instrument track
// (core/SimpleTracks.h). The last test measures the key move's quality on the real render path (pitch in cents, the
// length kept, artefacts against an ideal transposition and LYNX's stretch modes, CPU); LYNX_190DS_REPORT=1 prints the
// tables.

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <memory>

#include "core/AudioEditTools.h"
#include "core/KeyFollow.h"
#include "core/RackRenderer.h"
#include "core/ScaleKeys.h"
#include "core/Scheduler.h"
#include "core/SimpleTracks.h"
#include "core/TimeStretch.h"

namespace simple190ds
{
using namespace lynx;

inline int scaleIndex (const char* name)
{
    const auto& list = notetools::flReferenceScales();
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::string (list[(std::size_t) i].name) == name)
            return i;
    return -1;
}
inline projectkey::Key key (int root, const char* scale) { return { root, scaleIndex (scale) }; }
inline Song songIn (std::optional<projectkey::Key> k)
{
    Song s;
    projectkey::set (s, k);
    return s;
}

// a loop: chords of harmonic tones (each note with six partials), two bars at 120 BPM, 48 kHz, mono
inline std::shared_ptr<SampleData> chordLoop (const std::vector<std::vector<int>>& chords, double seconds = 4.0, double rate = 48000.0)
{
    auto d = std::make_shared<SampleData>();
    d->sampleRate = rate;
    const auto frames = (std::size_t) (seconds * rate);
    d->left.assign (frames, 0.0f);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto& chord = chords[i * chords.size() / frames];
        const auto t = (double) i / rate;
        const auto beat = std::fmod (t, 0.5);
        double v = 0.0;
        for (const auto k : chord)
        {
            const auto f = 440.0 * std::pow (2.0, (k - 69) / 12.0);
            for (int h = 1; h <= 6; ++h)
                v += std::sin (2.0 * 3.141592653589793 * f * h * t) / (h * h);
        }
        d->left[i] = (float) (0.08 * v * (1.0 - std::exp (-beat * 60.0)));
    }
    return d;
}

inline std::shared_ptr<SampleData> tone (double f0, double seconds = 4.0, double rate = 48000.0)
{
    auto d = std::make_shared<SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < d->left.size(); ++i)
    {
        const auto t = (double) i / rate;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin (2.0 * 3.141592653589793 * f0 * h * t) / h;
        d->left[i] = (float) (0.25 * v * std::min (1.0, t * 200.0));
    }
    return d;
}

// The song a dropped loop becomes: one audio clip of `beats` beats on track 0, through insert 1. `stretched`: the loop
// keeps time (its channel stretched to the beats, pitch kept, round 190p), with the stretch method `method`.
struct Render
{
    std::vector<float> out;
    double nsPerSample = 0.0;
};

inline Render renderClip (std::shared_ptr<SampleData> data, float clipPitch, bool stretched, StretchMethod method = StretchMethod::general,
                          float channelPitch = 0.0f, bool keepPitch = false, double seconds = 4.0)
{
    constexpr double rate = 48000.0;
    Song s;
    s.bpm = 120.0;
    s.mixer.resize (2);
    ChannelDef c;
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = "loop.wav";
    c.mixerTrack = 1;
    c.sampler.pitch = channelPitch;
    c.sampler.keepPitch = keepPitch || stretched;
    c.sampler.stretchMethod = method;
    if (stretched)
        c.sampler.stretchBeats = (float) (data->frames() / data->sampleRate * 2.0); // (its own length at 120 BPM)
    s.channels.push_back (c);
    s.patterns.emplace_back();
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.track = 0;
    clip.start = 0;
    clip.length = (std::int64_t) std::llround (seconds * 2.0 * ppq);
    clip.pitch = clipPitch;
    s.playlist.push_back (clip);
    s.mode = PlayMode::song;
    RackState::Factories f;
    f.samples = [data] (const std::string&) { return std::shared_ptr<const SampleData> (data); };
    f.stretchedSamples = [data] (const std::string&, double ratio, StretchMethod m, double semis, const StretchMap&) {
        return std::shared_ptr<const SampleData> (timeStretch (*data, ratio, m, semis));
    };
    auto state = RackState::build (s, nullptr, rate, f);
    RackRenderer renderer;
    constexpr int block = 512;
    renderer.prepare (rate, block);
    const auto tps = ticksPerSample (s.bpm, rate);
    Render r;
    std::vector<float> l (block), rr (block);
    double tick = 0.0;
    std::int64_t ns = 0;
    const auto blocks = (int) ((seconds + 0.5) * rate / block);
    for (int b = 0; b < blocks; ++b, tick += block * tps)
    {
        const auto t0 = std::chrono::steady_clock::now();
        renderer.process (state.get(), true, tick, block, l.data(), rr.data());
        ns += std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
        r.out.insert (r.out.end(), l.begin(), l.end());
    }
    r.nsPerSample = (double) ns / (double) (blocks * block);
    return r;
}

// magnitude spectrum (Hann, n = power of two) of x[from, from + n)
inline std::vector<double> spectrum (const std::vector<float>& x, std::size_t from, int n)
{
    std::vector<double> re ((std::size_t) n), im ((std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * i / (n - 1));
        re[(std::size_t) i] = from + (std::size_t) i < x.size() ? (double) x[from + (std::size_t) i] * w : 0.0;
    }
    audioedit::fftInPlace (re, im, false);
    std::vector<double> m ((std::size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
        m[(std::size_t) k] = std::hypot (re[(std::size_t) k], im[(std::size_t) k]);
    return m;
}

// the frequency of the strongest peak near `expected` Hz (±8 %), parabolic interpolation on the log magnitude
inline double peakNear (const std::vector<double>& m, double expected, double rate, int n)
{
    const auto lo = (int) (expected * 0.92 * n / rate), hi = (int) (expected * 1.08 * n / rate);
    int best = lo;
    for (int k = lo; k <= hi; ++k)
        if (m[(std::size_t) k] > m[(std::size_t) best])
            best = k;
    const auto a = std::log (m[(std::size_t) best - 1] + 1e-30), b = std::log (m[(std::size_t) best] + 1e-30), c = std::log (m[(std::size_t) best + 1] + 1e-30);
    const auto d = 0.5 * (a - c) / (a - 2.0 * b + c);
    return ((double) best + d) * rate / n;
}

// energy outside ±4 bins of the harmonics of `f0` (80 Hz – 8 kHz) against the energy on them, dB: what a transposition
// adds that isn't the tone (lower is better)
inline double offHarmonicDb (const std::vector<double>& m, double f0, double rate, int n)
{
    double on = 0.0, off = 0.0;
    const auto lo = (int) (80.0 * n / rate), hi = (int) (8000.0 * n / rate);
    for (int k = lo; k <= hi; ++k)
    {
        const auto f = k * rate / n;
        const auto h = std::round (f / f0);
        const auto near = h >= 1.0 && std::abs (f - h * f0) <= 4.0 * rate / n;
        (near ? on : off) += m[(std::size_t) k] * m[(std::size_t) k];
    }
    return 10.0 * std::log10 ((off + 1e-30) / (on + 1e-30));
}

// spectral difference (dB, RMS over 100 Hz – 6 kHz of the log magnitudes, floor −80 dB below the peak) between two
// signals over the same span: how far a key move is from the ideal transposition
inline double spectralDistanceDb (const std::vector<double>& a, const std::vector<double>& b, double rate, int n)
{
    double peak = 1e-30;
    for (std::size_t k = 0; k < a.size(); ++k)
        peak = std::max ({ peak, a[k], b[k] });
    const auto floor = peak * 1e-4;
    double sum = 0.0;
    int count = 0;
    for (int k = (int) (100.0 * n / rate); k <= (int) (6000.0 * n / rate); ++k)
    {
        const auto x = 20.0 * std::log10 (std::max (a[(std::size_t) k], floor) / std::max (b[(std::size_t) k], floor));
        sum += x * x;
        ++count;
    }
    return std::sqrt (sum / std::max (1, count));
}

// energy away (beyond ±4 bins) from the partials of `keys` (MIDI keys, harmonics 1…6) against the energy on them, dB,
// 100 Hz – 6 kHz: how much of a chord is not the notes it should play (lower is better)
inline double offPartialsDb (const std::vector<double>& m, const std::vector<double>& keys, double rate, int n)
{
    std::vector<double> partials;
    for (const auto k : keys)
        for (int h = 1; h <= 6; ++h)
            partials.push_back (440.0 * std::pow (2.0, (k - 69.0) / 12.0) * h);
    double on = 0.0, off = 0.0;
    for (int b = (int) (100.0 * n / rate); b <= (int) (6000.0 * n / rate); ++b)
    {
        const auto f = b * rate / n;
        bool near = false;
        for (const auto p : partials)
            near = near || std::abs (f - p) <= 4.0 * rate / n;
        (near ? on : off) += m[(std::size_t) b] * m[(std::size_t) b];
    }
    return 10.0 * std::log10 ((off + 1e-30) / (on + 1e-30));
}

inline double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (std::size_t i = from; i < to; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<std::size_t> (1, to - from));
}

inline bool report() { return std::getenv ("LYNX_190DS_REPORT") != nullptr; }
} // namespace simple190ds

TEST (simple190ds_scale_keys_every_scale)
{
    using namespace lynx;
    const auto& scales = notetools::flReferenceScales();
    for (int sc = 0; sc < (int) scales.size(); ++sc)
        for (int root = 0; root < 12; ++root)
        {
            const projectkey::Key k { root, sc };
            const auto n = scalekeys::degreeCount (k);
            if (n == 0)
                continue;
            const auto base = scalekeys::rootAtOrAbove (k, 48);
            CHECK (base >= 48 && base < 60 && scalekeys::wrap12 (base) == root);
            int last = base - 1;
            for (int i = -n; i <= 2 * n; ++i)
            {
                const auto note = scalekeys::scaleNote (k, i, base);
                CHECK (projectkey::contains (k, note)); // every scale key is in the key
                if (i >= 0)
                {
                    CHECK (note > last); // rising
                    last = note;
                }
            }
            CHECK (scalekeys::scaleNote (k, n, base) == base + 12); // one octave of degrees
            CHECK (scalekeys::scaleNote (k, -n, base) == base - 12);
        }
}

TEST (simple190ds_diatonic_chords_and_numerals)
{
    using namespace lynx;
    using simple190ds::key;
    const auto cMajor = key (0, "Major (Ionian)");
    CHECK (cMajor.flScale >= 0);
    const char* names[] { "I", "ii", "iii", "IV", "V", "vi", "vii\xc2\xb0" };
    const std::vector<int> triads[] { { 48, 52, 55 }, { 50, 53, 57 }, { 52, 55, 59 }, { 53, 57, 60 }, { 55, 59, 62 }, { 57, 60, 64 }, { 59, 62, 65 } };
    for (int d = 0; d < 7; ++d)
    {
        const auto notes = scalekeys::diatonicChord (cMajor, d, 48, scalekeys::Complexity::triad, scalekeys::Spread::close);
        CHECK (notes == triads[d]);
        CHECK (scalekeys::numeral (d, notes) == names[d]);
    }
    CHECK (chordread::nameKeys (scalekeys::diatonicChord (cMajor, 5, 48, scalekeys::Complexity::triad, scalekeys::Spread::close)).text.rfind ("Am", 0) == 0);
    // richer chords
    CHECK ((scalekeys::diatonicChord (cMajor, 4, 48, scalekeys::Complexity::seventh, scalekeys::Spread::close) == std::vector<int> { 55, 59, 62, 65 })); // G7
    CHECK ((scalekeys::diatonicChord (cMajor, 0, 48, scalekeys::Complexity::ninth, scalekeys::Spread::close) == std::vector<int> { 48, 52, 55, 59, 62 }));
    CHECK ((scalekeys::diatonicChord (cMajor, 1, 48, scalekeys::Complexity::sus4, scalekeys::Spread::close) == std::vector<int> { 50, 55, 57 }));
    CHECK ((scalekeys::diatonicChord (cMajor, 0, 48, scalekeys::Complexity::add9, scalekeys::Spread::close) == std::vector<int> { 48, 52, 55, 62 }));
    // spread: open moves the third up an octave; wide also doubles the root below
    CHECK ((scalekeys::diatonicChord (cMajor, 0, 48, scalekeys::Complexity::triad, scalekeys::Spread::open) == std::vector<int> { 48, 55, 64 }));
    CHECK ((scalekeys::diatonicChord (cMajor, 0, 48, scalekeys::Complexity::triad, scalekeys::Spread::wide) == std::vector<int> { 36, 48, 55, 64 }));
    // A minor: i ii° III iv v VI VII; a pentatonic key uses its parent
    const auto aMinor = key (9, "Minor Natural (Aeolian)");
    CHECK (scalekeys::numeral (1, scalekeys::diatonicChord (aMinor, 1, 48, scalekeys::Complexity::triad, scalekeys::Spread::close)) == "ii\xc2\xb0");
    CHECK (scalekeys::numeral (0, scalekeys::diatonicChord (aMinor, 0, 48, scalekeys::Complexity::triad, scalekeys::Spread::close)) == "i");
    const auto aPenta = key (9, "Minor Pentatonic");
    CHECK (aPenta.flScale >= 0);
    CHECK (scalekeys::diatonicChord (aPenta, 0, 48, scalekeys::Complexity::triad, scalekeys::Spread::close)
           == scalekeys::diatonicChord (aMinor, 0, 48, scalekeys::Complexity::triad, scalekeys::Spread::close));
    // the key's diatonic set: C Dm Em F G Am B° (qualities as FL Studio Mobile's default set)
    const auto set = scalekeys::diatonicSet (cMajor);
    CHECK (set.size() == 7);
    const int qualities[] { 0, 1, 1, 0, 0, 1, 3 };
    for (int d = 0; d < 7 && d < (int) set.size(); ++d)
        CHECK (set[(std::size_t) d].quality == qualities[d] && set[(std::size_t) d].root == triads[d][0] % 12);
    // every chord stays inside MIDI
    for (int d = 0; d < 14; ++d)
        for (const auto n : scalekeys::diatonicChord (cMajor, d, 120, scalekeys::Complexity::ninth, scalekeys::Spread::wide))
            CHECK (n >= 0 && n <= 127);
}

TEST (simple190ds_strum_and_chord_sets)
{
    using namespace lynx;
    scalekeys::Settings s;
    s.strumMs = 60;
    const auto up = scalekeys::strummed ({ 64, 60, 67 }, s);
    CHECK (up.size() == 3 && up[0] == std::make_pair (60, 0) && up[1] == std::make_pair (64, 30) && up[2] == std::make_pair (67, 60));
    s.strumDown = true;
    const auto down = scalekeys::strummed ({ 64, 60, 67 }, s);
    CHECK (down[0].first == 67 && down[2].first == 60 && down[2].second == 60);
    s.strumMs = 500; // clamped
    CHECK (scalekeys::strummed ({ 60, 64 }, s).back().second == scalekeys::maxStrumMs);

    // FL Studio Mobile's KEYB (the text of a real file's default set) and LYNX's chords from it
    const std::string keyb = "{\n    \"chords\": [\n        {\n            \"baseNote\": 0,\n            \"extensions\": [],\n            \"octave\": 3,\n"
                             "            \"quality\": 0\n        },\n        {\n            \"baseNote\": 2,\n            \"extensions\": [],\n"
                             "            \"octave\": 3,\n            \"quality\": 1\n        },\n        {\n            \"baseNote\": 11,\n"
                             "            \"extensions\": [ 3 ],\n            \"octave\": 3,\n            \"quality\": 3\n        },\n        {\n"
                             "            \"baseNote\": 0,\n            \"extensions\": [],\n            \"octave\": 4,\n            \"quality\": 0\n        }\n    ],\n"
                             "    \"rootNote\": 0\n}";
    int dropped = -1;
    const auto set = scalekeys::parseFlmChordSet (keyb, &dropped);
    CHECK (set.size() == 4);
    CHECK (dropped == 0);
    if (set.size() == 4)
    {
        CHECK (set[0].root == 0 && set[0].quality == 0 && set[0].octave == 3);
        CHECK (set[1].root == 2 && set[1].quality == 1);
        CHECK (set[2].root == 11 && set[2].quality == 3 && set[2].extensions == std::vector<int> { 3 });
        CHECK (set[3].octave == 4);
        CHECK ((scalekeys::setChord (set, 0, 48, scalekeys::Spread::close) == std::vector<int> { 48, 52, 55 }));
        CHECK ((scalekeys::setChord (set, 2, 48, scalekeys::Spread::close) == std::vector<int> { 59, 62, 65 })); // B°
        CHECK ((scalekeys::setChord (set, 3, 48, scalekeys::Spread::close) == std::vector<int> { 60, 64, 67 }));  // C, an octave up
    }
    // KEYB extensions are semitones (REA 190ds: 10 = "7", 14 = "9"); power chords; out-of-range values are counted
    const auto rich = scalekeys::parseFlmChordSet ("{\"chords\":[{\"baseNote\":7,\"extensions\":[10,14],\"octave\":3,\"quality\":0},"
                                                   "{\"baseNote\":4,\"extensions\":[],\"octave\":3,\"quality\":6},{\"baseNote\":0,\"extensions\":[99],\"octave\":3,\"quality\":0}]}",
                                                   &dropped);
    CHECK (rich.size() == 3 && dropped == 1);
    if (rich.size() == 3)
    {
        CHECK ((scalekeys::setChord (rich, 0, 48, scalekeys::Spread::close) == std::vector<int> { 55, 59, 62, 65, 69 })); // G9
        CHECK ((scalekeys::setChord (rich, 1, 48, scalekeys::Spread::close) == std::vector<int> { 52, 59 }));             // E5
    }
    CHECK (scalekeys::parseFlmChordSet ("not json").empty());
    CHECK (scalekeys::parseFlmChordSet ("{\"chords\": []}").empty());
    // a project chord set replaces the diatonic pads when chosen
    Song song;
    song.keyboardChords = set;
    scalekeys::Settings pads;
    pads.projectChords = true;
    CHECK (scalekeys::padCount (song, pads) == 4);
    pads.projectChords = false;
    CHECK (scalekeys::padCount (song, pads) == 7);
    CHECK (scalekeys::padNotes (song, pads, 0, 48).empty()); // (no key: the diatonic pads have nothing to play)
    // extensions are semitones over the root (LYNX's own edits)
    std::vector<KeyboardChord> own { { 7, 0, 4, { 10 } } };
    CHECK ((scalekeys::setChord (own, 0, 48, scalekeys::Spread::close) == std::vector<int> { 55, 59, 62, 65 })); // G7
}

TEST (simple190ds_flm_key_locker)
{
    using namespace lynx;
    // FLM's HEAD root + scale index (REA 190ds), as the files on disk hold them
    auto named = [] (std::uint32_t root, std::uint32_t scale) {
        const auto k = scalekeys::flmKeyLocker (root, scale);
        return k ? projectkey::name (*k) : std::string ("none");
    };
    CHECK (named (0, 0) == "none");                              // Chromatic: no lock
    CHECK (named (2, 30) == "D Other Phrygian");                 // "Permich - Arabic"
    CHECK (named (7, 10) == "G Minor Natural (Aeolian)");
    CHECK (named (10, 1) == "A# Major (Ionian)");
    CHECK (named (2, 4) == "D Major Pentatonic");
    CHECK (named (9, 12) == "A Minor Pentatonic");
    CHECK (named (0, 99) == "none" && named (12, 1) == "none");
    // every FLM scale that maps lands on FL's scale with the same degrees
    int mapped = 0;
    for (std::uint32_t sc = 1; sc < 33; ++sc)
        if (const auto k = scalekeys::flmKeyLocker (0, sc))
        {
            ++mapped;
            CHECK (projectkey::contains (*k, 0));
        }
    if (simple190ds::report())
        std::printf ("  FLM key locker scales FL's list has: %d of 32\n", mapped);
    CHECK (mapped >= 12);
}

TEST (simple190ds_typing_keys_follow_the_mode)
{
    using namespace lynx;
    const auto aMinor = simple190ds::songIn (simple190ds::key (9, "Minor Natural (Aeolian)"));
    scalekeys::Settings s;
    // Keys mode: as typed; Lock to key: out of key plays nothing
    CHECK ((scalekeys::typedNotes (aMinor, s, 61, 60) == std::vector<int> { 61 }));
    s.lockToKey = true;
    CHECK (scalekeys::typedNotes (aMinor, s, 61, 60).empty());
    CHECK ((scalekeys::typedNotes (aMinor, s, 60, 60) == std::vector<int> { 60 }));
    // Scale keys: Z X C V B N M (C D E F G A B) = the degrees from the root at or above the typing C; black keys silent
    s = {};
    s.mode = scalekeys::Mode::scaleKeys;
    const int typed[] { 60, 62, 64, 65, 67, 69, 71, 72, 74 };
    const int expect[] { 69, 71, 72, 74, 76, 77, 79, 81, 83 };
    for (int i = 0; i < 9; ++i)
        CHECK ((scalekeys::typedNotes (aMinor, s, typed[i], 60) == std::vector<int> { expect[i] }));
    CHECK (scalekeys::typedNotes (aMinor, s, 63, 60).empty());
    // an octave lower on the keyboard: an octave lower
    CHECK ((scalekeys::typedNotes (aMinor, s, 48, 60) == std::vector<int> { 57 }));
    // Chord pad: the degrees' chords; the 8th white key starts the pads again an octave up
    s.mode = scalekeys::Mode::chordPad;
    CHECK ((scalekeys::typedNotes (aMinor, s, 60, 60) == std::vector<int> { 69, 72, 76 }));
    CHECK ((scalekeys::typedNotes (aMinor, s, 65, 60) == std::vector<int> { 74, 77, 81 }));
    CHECK ((scalekeys::typedNotes (aMinor, s, 72, 60) == std::vector<int> { 81, 84, 88 }));
    // no project key: every mode plays what is typed
    const Song none;
    for (const auto mode : { scalekeys::Mode::keys, scalekeys::Mode::scaleKeys, scalekeys::Mode::chordPad })
    {
        s.mode = mode;
        s.lockToKey = true;
        CHECK ((scalekeys::typedNotes (none, s, 61, 60) == std::vector<int> { 61 }));
    }
    // pentatonic: five degrees per octave on seven white keys
    const auto penta = simple190ds::songIn (simple190ds::key (9, "Minor Pentatonic"));
    s = {};
    s.mode = scalekeys::Mode::scaleKeys;
    CHECK ((scalekeys::typedNotes (penta, s, 69, 60) == std::vector<int> { 81 })); // A = the 6th white key = the octave's root
    CHECK (scalekeys::typingLabel (0) == std::string ("Z") && scalekeys::typingLabel (7) == std::string ("Q"));
}

TEST (simple190ds_key_in_the_file_name)
{
    using namespace lynx;
    using keyfollow::LoopKey;
    const std::pair<const char*, std::optional<LoopKey>> cases[] {
        { "Night Pad 120 Gm.wav", LoopKey { 7, true } },
        { "DL_102_Dm_Stack.wav", LoopKey { 2, true } },
        { "Pluck_120_D_minor.wav", LoopKey { 2, true } },
        { "Guitar Loop - F# Major - 95 BPM.wav", LoopKey { 6, false } },
        { "Keys_Ebmaj_128.wav", LoopKey { 3, false } },
        { "bass_c#min_140.wav", LoopKey { 1, true } },
        { "Lead 140bpm Bbm.wav", LoopKey { 10, true } },
        { "120Am_vox.wav", LoopKey { 9, true } },
        { "Synth Loop 128 C.wav", LoopKey { 0, false } },
        { "Brass Csharp minor.aif", LoopKey { 1, true } },
        { "Strings A minor.wav", LoopKey { 9, true } },
        // no key
        { "Kick 01.wav", std::nullopt },
        { "Drum Loop 120.wav", std::nullopt },
        { "I am here.wav", std::nullopt },
        { "Bass Groove 98.wav", std::nullopt },
        { "Ambient Texture.wav", std::nullopt },
        { "Piano C.wav", std::nullopt },     // a bare note needs a tempo beside it
        { "Cmaj7 stab 120.wav", std::nullopt }, // a chord name, not a key
        { "Vocal take 3.wav", std::nullopt },
    };
    for (const auto& [name, expected] : cases)
    {
        const auto got = keyfollow::keyInName (name);
        CHECK (got == expected);
        if (got != expected)
            std::printf ("  key in name \"%s\": %s\n", name, got ? keyfollow::keyName (*got).c_str() : "none");
    }
}

TEST (simple190ds_key_detected_from_the_audio)
{
    using namespace lynx;
    // progressions in a key (i VI VII i / I IV V I and friends); the move needs the right notes, so the relative major
    // root must be right; the key itself (major or minor) too for these clear cadences
    struct Case
    {
        int root;
        bool minor;
        std::vector<std::vector<int>> chords;
    };
    const Case cases[] {
        { 7, true, { { 55, 58, 62 }, { 51, 55, 58 }, { 53, 57, 60 }, { 55, 58, 62 } } },          // G minor
        { 4, true, { { 52, 55, 59, 64 }, { 48, 52, 55, 60 }, { 50, 54, 57, 62 }, { 52, 55, 59, 64 } } }, // E minor
        { 0, false, { { 48, 52, 55 }, { 53, 57, 60 }, { 55, 59, 62 }, { 48, 52, 55 } } },          // C major
        { 2, false, { { 50, 54, 57 }, { 55, 59, 62 }, { 57, 61, 64 }, { 50, 54, 57 } } },          // D major
        { 9, true, { { 57, 60, 64 }, { 53, 57, 60 }, { 52, 56, 59 }, { 57, 60, 64 } } },           // A minor (harmonic V)
        { 3, false, { { 51, 55, 58 }, { 56, 60, 63 }, { 58, 62, 65 }, { 51, 55, 58 } } },          // Eb major
    };
    int exact = 0;
    for (const auto& c : cases)
    {
        const auto d = keyfollow::detectKey (*simple190ds::chordLoop (c.chords));
        const auto relative = [] (const keyfollow::LoopKey& k) { return (k.root + (k.minor ? 3 : 0)) % 12; };
        CHECK (d.key.has_value());
        if (! d.key)
            continue;
        CHECK (relative (*d.key) == relative ({ c.root, c.minor }));
        exact += d.key->root == c.root && d.key->minor == c.minor ? 1 : 0;
        if (simple190ds::report())
            std::printf ("  detect %s: %s (r %.3f, margin %.3f)\n", keyfollow::keyName ({ c.root, c.minor }).c_str(), keyfollow::keyName (*d.key).c_str(),
                         d.correlation, d.margin);
    }
    CHECK (exact >= 5);
    // no key: noise, a drum loop, a single pure tone, silence, a file too short
    auto noise = std::make_shared<SampleData>();
    noise->sampleRate = 48000.0;
    noise->left.resize (192000);
    std::uint32_t seed = 190;
    for (auto& v : noise->left)
        v = (float) ((seed = seed * 1664525u + 1013904223u) >> 8 & 0xffff) / 65536.0f - 0.5f;
    CHECK (! keyfollow::detectKey (*noise).key);
    auto drums = std::make_shared<SampleData>();
    drums->sampleRate = 48000.0;
    drums->left.resize (192000);
    for (std::size_t i = 0; i < drums->left.size(); ++i)
    {
        const auto t = (double) (i % 24000) / 48000.0;
        const auto w = (float) ((seed = seed * 1664525u + 1013904223u) >> 8 & 0xffff) / 65536.0f - 0.5f;
        drums->left[i] = (float) (std::sin (2.0 * 3.141592653589793 * (50.0 * t + 1.5 * (1.0 - std::exp (-t * 40.0)))) * std::exp (-t * 9.0)
                                  + (i / 24000 % 2 == 1 ? w * std::exp (-t * 20.0) : 0.0));
    }
    CHECK (! keyfollow::detectKey (*drums).key);
    CHECK (! keyfollow::detectKey (*simple190ds::tone (440.0)).key);
    auto silence = std::make_shared<SampleData>();
    silence->left.assign (96000, 0.0f);
    CHECK (! keyfollow::detectKey (*silence).key);
    auto shortOne = std::make_shared<SampleData>();
    shortOne->left.assign (1000, 0.1f);
    CHECK (! keyfollow::detectKey (*shortOne).key);
}

TEST (simple190ds_loops_follow_the_project_key)
{
    using namespace lynx;
    using simple190ds::key;
    // the move between relative-major roots, −6 … +5
    const auto gMinor = key (7, "Minor Natural (Aeolian)"), cMajor = key (0, "Major (Ionian)");
    CHECK (projectkey::loopShift (2, true, gMinor) == 5);   // D minor → G minor
    CHECK (projectkey::loopShift (9, true, cMajor) == 0);   // A minor in C major: the same notes
    CHECK (projectkey::loopShift (7, false, cMajor) == 5);  // G → C: +5 (not −7)
    CHECK (projectkey::loopShift (6, false, cMajor) == -6); // the tritone goes down
    for (int from = 0; from < 12; ++from)
        for (int to = 0; to < 12; ++to)
        {
            const auto s = projectkey::loopShift (from, false, projectkey::Key { to, cMajor.flScale });
            CHECK (s >= -6 && s <= 5 && ((from + s) % 12 + 12) % 12 == to);
        }

    // a song with two loop clips (one following) and the user's own pitch on one
    Song s;
    s.channels.resize (2);
    for (auto& c : s.channels)
        c.instrument = InstrumentKind::audioClip;
    keyfollow::setLoopKey (s, 0, keyfollow::LoopKey { 2, true }, 1); // D minor
    keyfollow::setLoopKey (s, 1, keyfollow::LoopKey { 7, true }, 2); // G minor
    Clip a, b, pat;
    a.kind = b.kind = ClipKind::audio;
    a.channel = 0;
    b.channel = 1;
    b.pitch = 1.0f; // the user's own pitch
    pat.kind = ClipKind::pattern;
    s.playlist = { a, b, pat };
    // nothing follows yet: nothing moves when the key is set
    projectkey::set (s, gMinor);
    CHECK (s.playlist[0].pitch == 0.0f && s.playlist[1].pitch == 1.0f);
    keyfollow::setFollow (s, { 0, 1, 2 }, true);
    CHECK (s.playlist[0].followKey && s.playlist[1].followKey && ! s.playlist[2].followKey); // (a pattern clip: no)
    CHECK (s.playlist[0].keyShift == 5 && s.playlist[0].pitch == 5.0f);
    CHECK (s.playlist[1].keyShift == 0 && s.playlist[1].pitch == 1.0f);
    CHECK (keyfollow::shiftOf (s, s.playlist[0]) == 5);
    // the key moves: every follower moves, the user's pitch stays on top
    projectkey::set (s, key (9, "Minor Natural (Aeolian)"));
    CHECK (s.playlist[0].keyShift == -5 && s.playlist[0].pitch == -5.0f);
    CHECK (s.playlist[1].keyShift == 2 && s.playlist[1].pitch == 3.0f);
    // no key: back to their own keys
    projectkey::set (s, std::nullopt);
    CHECK (s.playlist[0].keyShift == 0 && s.playlist[0].pitch == 0.0f && s.playlist[1].pitch == 1.0f);
    projectkey::set (s, key (9, "Minor Natural (Aeolian)"));
    // switched off: its own key again; the loop's key corrected by hand: it moves from there
    keyfollow::setFollow (s, { 1 }, false);
    CHECK (s.playlist[1].keyShift == 0 && s.playlist[1].pitch == 1.0f && ! s.playlist[1].followKey);
    keyfollow::setLoopKey (s, 0, keyfollow::LoopKey { 4, true }, 3); // E minor (the user's)
    CHECK (s.playlist[0].keyShift == 5 && s.channels[0].sampler.loopKeySource == 3);
    keyfollow::setLoopKey (s, 0, std::nullopt, 0); // not known: it stays where it is in its own key
    CHECK (s.playlist[0].keyShift == 0 && s.playlist[0].pitch == 0.0f && s.playlist[0].followKey);
    // the clip pitch's ±12 limit: the user's +10 and a +5 move stop at +12; switching off gives back exactly +10
    keyfollow::setLoopKey (s, 0, keyfollow::LoopKey { 4, true }, 3);
    s.playlist[0].pitch = 10.0f + s.playlist[0].keyShift;
    projectkey::set (s, key (9, "Minor Natural (Aeolian)"));
    projectkey::set (s, gMinor); // E minor → G minor: +3
    CHECK (s.playlist[0].pitch == 12.0f && s.playlist[0].keyShift == 2);
    keyfollow::setFollow (s, { 0 }, false);
    CHECK (s.playlist[0].pitch == 10.0f);
    // the other clip lists move too (arrangements, the clip grid)
    Song g;
    g.channels.resize (1);
    keyfollow::setLoopKey (g, 0, keyfollow::LoopKey { 2, true }, 1);
    Clip gridClip;
    gridClip.kind = ClipKind::audio;
    gridClip.channel = 0;
    gridClip.followKey = true;
    g.clipGrid.emplace();
    g.clipGrid->clips.push_back (gridClip);
    projectkey::set (g, gMinor);
    CHECK (g.clipGrid->clips[0].keyShift == 5);
    // old projects: nothing follows, a key change moves nothing (the same sound)
    Song old;
    old.channels.resize (1);
    Clip plain;
    plain.kind = ClipKind::audio;
    plain.channel = 0;
    plain.pitch = 2.0f;
    old.playlist = { plain };
    const auto before = old.playlist;
    projectkey::set (old, gMinor);
    CHECK (old.playlist == before);
    // the Clip keeps its size (the two fields sit in its padding)
    CHECK (sizeof (Clip) == 144 || sizeof (void*) != 8);
}

TEST (simple190ds_simple_tracks_patterns_and_keyed_loops)
{
    using namespace lynx;
    namespace st = simpletracks;
    Song s = Song::makeDefault();
    s.playlist.clear();
    st::Request q;
    q.kind = st::Kind::instrument;
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    q.channel = synth;
    q.emptyBars = 4;
    const auto r = st::addTrack (s, q);
    CHECK (r.ok() && r.pattern >= 0);
    if (! r.ok() || r.pattern < 0)
        return;
    CHECK (s.patterns[(std::size_t) r.pattern].notes.empty());
    CHECK (s.patterns[(std::size_t) r.pattern].effectiveLength (s.meter()) == 4 * s.beatsPerBar * ppq);
    CHECK (st::patternOf (s, r.track) == r.pattern);
    bool made = true;
    CHECK (st::ensurePattern (s, r.track, 4, &made) == r.pattern && ! made); // (it has one)
    // a track without a pattern gets one; a second call gives the same
    q.emptyBars = 0;
    const auto r2 = st::addTrack (s, q);
    CHECK (r2.ok() && r2.pattern < 0 && st::patternOf (s, r2.track) < 0);
    const auto p = st::ensurePattern (s, r2.track, 4, &made);
    CHECK (p >= 0 && made && st::patternOf (s, r2.track) == p && st::ensurePattern (s, r2.track) == p);
    // an audio track has none
    st::Request voice;
    voice.kind = st::Kind::voice;
    const auto r3 = st::addTrack (s, voice);
    CHECK (r3.ok() && st::ensurePattern (s, r3.track) == -1);
    // a keyed loop: its key on the channel, the clip following and moved to the project key in the same edit
    projectkey::set (s, simple190ds::key (7, "Minor Natural (Aeolian)"));
    st::Request loop;
    loop.kind = st::Kind::audio;
    loop.audioFile = "/loops/Pluck_120_D_minor.wav";
    loop.audioLength = 8 * ppq;
    loop.audioBeats = 8.0f;
    loop.audioKeyRoot = 2;
    loop.audioKeyMinor = true;
    loop.audioKeySource = 1;
    loop.followKey = true;
    const auto r4 = st::addTrack (s, loop);
    CHECK (r4.ok() && ! r4.channels.empty());
    if (r4.ok() && ! r4.channels.empty())
    {
        const auto& c = s.playlist.back();
        CHECK (c.followKey && c.keyShift == 5 && c.pitch == 5.0f);
        CHECK (s.channels[(std::size_t) r4.channels[0]].sampler.loopKeyRoot == 2 && s.channels[(std::size_t) r4.channels[0]].sampler.loopKeySource == 1);
        CHECK (s.channels[(std::size_t) r4.channels[0]].sampler.keepPitch); // (a loop: stretched to the tempo, pitch kept)
    }
    // the project key for a loop key: its root, Major or Minor
    const auto k = keyfollow::projectKeyFor ({ 2, true });
    CHECK (k.root == 2 && projectkey::isMinor (k) && notetools::flReferenceScales()[(std::size_t) k.flScale].name == std::string ("Minor Natural (Aeolian)"));
}

TEST (simple190ds_key_move_quality)
{
    // The key move is the clip's pitch on LYNX's own voice pitch shifter (core/TimeStretch VoicePitchShifter): measured
    // on the real render path for every move a loop can get (−6 … +5), and against LYNX's stretch modes.
    using namespace simple190ds;
    using lynx::StretchMethod;
    constexpr double rate = 48000.0;
    constexpr int n = 65536;
    const std::size_t from = (std::size_t) (0.6 * rate); // (past the start)
    const auto source = tone (220.0);
    double worstCents = 0.0, worstLength = 0.0, worstOffDb = -200.0, shiftedNs = 0.0;
    const auto plain = renderClip (source, 0.0f, false);
    const auto plainOff = offHarmonicDb (spectrum (plain.out, from, n), 220.0, rate, n);
    if (report())
        std::printf ("  key move on a 220 Hz harmonic tone (clip pitch, length kept): shift, measured f0 error (cents), level 3.4..3.9 s "
                     "vs unshifted (dB), off-harmonic energy (dB), ns/sample\n  plain: off-harmonic %.1f dB, %.0f ns/sample\n",
                     plainOff, plain.nsPerSample);
    for (int shift = -6; shift <= 5; ++shift)
    {
        if (shift == 0)
            continue;
        const auto r = renderClip (source, (float) shift, false);
        const auto expected = 220.0 * std::pow (2.0, shift / 12.0);
        const auto m = spectrum (r.out, from, n);
        const auto f = peakNear (m, expected, rate, n);
        const auto cents = 1200.0 * std::log2 (f / expected);
        // the length stays: the end of the clip (3.4 … 3.9 s) as loud as without the move, silence after it
        const auto endLevel = 20.0 * std::log10 (rms (r.out, (std::size_t) (3.4 * rate), (std::size_t) (3.9 * rate))
                                                 / std::max (1e-12, rms (plain.out, (std::size_t) (3.4 * rate), (std::size_t) (3.9 * rate))));
        // (after the clip the song loops back to its start: the same as without the move)
        const auto after = rms (r.out, (std::size_t) (4.1 * rate), (std::size_t) (4.4 * rate));
        const auto off = offHarmonicDb (m, expected, rate, n);
        worstCents = std::max (worstCents, std::abs (cents));
        worstLength = std::max (worstLength, std::abs (endLevel));
        worstOffDb = std::max (worstOffDb, off);
        shiftedNs = std::max (shiftedNs, r.nsPerSample);
        if (report())
            std::printf ("  %+d: %+.2f cents, end %+.2f dB, off-harmonic %.1f dB, %.0f ns/sample, after the end %.6f (plain %.6f)\n", shift, cents, endLevel, off,
                         r.nsPerSample, after, rms (plain.out, (std::size_t) (4.1 * rate), (std::size_t) (4.4 * rate)));
    }
    CHECK (worstCents < 3.0);   // pitch-exact within 3 cents for every move
    CHECK (worstLength < 1.5);  // the clip plays to its end, at its level
    CHECK (worstOffDb < -25.0); // artefacts at least 25 dB under the tone
    if (report())
        std::printf ("  worst: %.2f cents, %.2f dB at the end, off-harmonic %.1f dB, CPU %.0f ns/sample (plain %.0f)\n", worstCents, worstLength, worstOffDb,
                     shiftedNs, plain.nsPerSample);

    // a chord loop (G minor: Gm Eb F Gm) moved +3 / −5: how much of its first chord (0.12 … 0.46 s, 16384 points) is not
    // the moved chord's partials — for the ideal transposition (the loop resampled by the channel: the same pitches, no
    // stretch), the clip's pitch on a loop that keeps time (a dropped loop) in each of LYNX's stretch modes, the clip's
    // pitch on a loop at its own speed, and the loop not moved at all
    const auto loop = chordLoop ({ { 55, 58, 62 }, { 51, 55, 58 }, { 53, 57, 60 }, { 55, 58, 62 } });
    const struct
    {
        StretchMethod method;
        const char* name;
    } modes[] { { StretchMethod::general, "general (default)" }, { StretchMethod::transients, "transients" }, { StretchMethod::mono, "mono" },
                { StretchMethod::tones, "tones" }, { StretchMethod::texture, "texture" }, { StretchMethod::complex, "complex" } };
    constexpr int cn = 16384;
    const auto at = (std::size_t) (0.12 * rate);
    for (const int shift : { 3, -5 })
    {
        const std::vector<double> moved { 55.0 + shift, 58.0 + shift, 62.0 + shift };
        const auto ideal = offPartialsDb (spectrum (renderClip (loop, 0.0f, false, StretchMethod::general, (float) shift, false).out, at, cn), moved, rate, cn);
        const auto unmoved = offPartialsDb (spectrum (renderClip (loop, 0.0f, false).out, at, cn), moved, rate, cn);
        double general = 0.0, best = 1e9;
        const char* bestName = "";
        for (const auto& mode : modes)
        {
            const auto r = renderClip (loop, (float) shift, true, mode.method);
            const auto d = offPartialsDb (spectrum (r.out, at, cn), moved, rate, cn);
            if (mode.method == StretchMethod::general)
                general = d;
            if (d < best)
                best = d, bestName = mode.name;
            if (report())
                std::printf ("  chord loop %+d, kept in time, %s: off the chord's partials %.1f dB, %.0f ns/sample\n", shift, mode.name, d, r.nsPerSample);
        }
        const auto own = offPartialsDb (spectrum (renderClip (loop, (float) shift, false).out, at, cn), moved, rate, cn);
        if (report())
            std::printf ("  chord loop %+d: ideal (resampled) %.1f dB, at its own speed (clip pitch only) %.1f dB, not moved %.1f dB; best mode: %s\n", shift,
                         ideal, own, unmoved, bestName);
        CHECK (general < unmoved - 15.0); // the moved loop plays the moved chord
        CHECK (own < unmoved - 15.0);
        CHECK (general < -20.0);          // the rest at least 20 dB under the chord (measured: -42.8 dB at +3, -27.5 dB at -5)
    }
}
