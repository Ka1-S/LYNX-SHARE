// --- Round 190lo: the polyphonic note editor, round 2 (core/PolyEdit.h): rejected candidates kept for note assignment,
// the modulation / drift / drawn pitch curve of a note, LXPE version 2 (version 1 blobs still read), the engine's note
// order for an analysis, the guided detector as a per-region fallback ---------------------------------------------------

namespace pe190lo
{
// a version-1 (round 190lk) blob of one note and one edit, byte for byte
std::vector<std::uint8_t> version1Blob()
{
    std::vector<std::uint8_t> b { 'L', 'X', 'P', 'E' };
    auto put = [&b] (auto v) {
        std::uint8_t raw[sizeof (v)];
        std::memcpy (raw, &v, sizeof (v));
        b.insert (b.end(), raw, raw + sizeof (v));
    };
    put ((std::uint32_t) 1);
    put (48000.0);
    put ((std::int64_t) 96000);
    put ((std::int32_t) 4096);
    put ((std::int32_t) 512);
    put ((std::uint8_t) 3);
    put ((std::uint32_t) 1);
    put ((std::int32_t) 10);
    put ((std::int32_t) 12);
    put (0.0f);
    put (1.0);
    put (0.9f);
    put ((std::uint32_t) 3);
    for (int i = 0; i < 3; ++i)
        put (440.0f);
    put ((std::uint32_t) 1);
    put ((std::int32_t) 0);
    put (2.0f);
    put (-3.0f);
    put (0.0);
    put (1.0f);
    put ((std::uint8_t) 0);
    return b;
}

// a note of `frames` frames at `midi` with a 5.5 Hz vibrato of `depth` semitones and a slow rise of `rise` semitones
lynx::polyedit::Note wobbly (double midi, int frames, double frameSeconds, double depth, double rise)
{
    lynx::polyedit::Note n;
    n.startFrame = 0;
    n.endFrame = frames - 1;
    for (int i = 0; i < frames; ++i)
    {
        const auto t = i * frameSeconds;
        const auto m = midi + depth * std::sin (2.0 * 3.141592653589793 * 5.5 * t) + rise * i / (double) frames;
        n.f0.push_back ((float) (440.0 * std::pow (2.0, (m - 69.0) / 12.0)));
    }
    return n;
}

double rms (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double s = 0.0;
    for (auto i = from; i < to; ++i)
        s += (double) v[i] * v[i];
    return std::sqrt (s / (double) std::max<std::size_t> (1, to - from));
}

// a harmonic tone (8 partials, 1/k) with a linear attack of `attack` s and a 0.1 s release, playing [from, to) s, into x;
// `vibrato` semitones at 5.5 Hz
void tone (std::vector<float>& x, double rate, double key, double from, double to, double attack, double vibrato = 0.0)
{
    double phase = 0.0;
    const auto a = (std::size_t) (from * rate), b = std::min (x.size(), (std::size_t) ((to + 0.5) * rate));
    for (auto i = a; i < b; ++i)
    {
        const auto t = (double) (i - a) / rate, tEnd = to - from;
        const auto env = std::min (1.0, t / attack) * (t < tEnd ? 1.0 : std::exp (-(t - tEnd) / 0.1));
        const auto f = 440.0 * std::pow (2.0, (key + vibrato * std::sin (2.0 * 3.141592653589793 * 5.5 * t) - 69.0) / 12.0);
        phase += 2.0 * 3.141592653589793 * f / rate;
        double v = 0.0;
        for (int k = 1; k <= 8; ++k)
            v += std::sin (k * phase + 0.3 * k) / k;
        x[i] += (float) (0.06 * env * v);
    }
}

// the frequency of the strongest component between lo and hi Hz in each 2048-sample frame (hop 1024) of [from, to) s
std::vector<double> peakTrack (const std::vector<float>& x, double rate, double lo, double hi, double from, double to)
{
    std::vector<double> out;
    for (auto a = (std::size_t) (from * rate); a + 2048 <= std::min (x.size(), (std::size_t) (to * rate)); a += 1024)
    {
        double best = 0.0, bestF = lo;
        for (double f = lo; f <= hi; f += 0.5)
        {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < 2048; ++i)
            {
                const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) i / 2048.0);
                re += w * x[a + i] * std::cos (2.0 * 3.141592653589793 * f * (double) i / rate);
                im += w * x[a + i] * std::sin (2.0 * 3.141592653589793 * f * (double) i / rate);
            }
            if (re * re + im * im > best)
                best = re * re + im * im, bestF = f;
        }
        out.push_back (bestF);
    }
    return out;
}

double spread (const std::vector<double>& v)
{
    double m = 0.0, s = 0.0;
    for (auto x : v)
        m += x / (double) v.size();
    for (auto x : v)
        s += (x - m) * (x - m) / (double) v.size();
    return std::sqrt (s);
}
} // namespace pe190lo

TEST (polyedit190lo_version2_round_trip_and_version1_still_reads)
{
    using namespace lynx::polyedit;
    Model m;
    m.sampleRate = 44100.0;
    m.frames = 44100;
    Note a;
    a.startFrame = 2;
    a.endFrame = 4;
    a.f0 = { 220.0f, 220.5f, 219.5f };
    Note candidate = a;
    candidate.active = false;
    candidate.probability = 0.2f;
    m.notes = { a, candidate };
    Edit e;
    e.modulation = 0.0f;
    e.drift = 1.5f;
    e.curve = { 0.0f, 0.25f, 0.5f };
    m.edits[0] = e;
    const auto back = parse (write (m));
    CHECK (back.has_value() && *back == m);
    CHECK (back.has_value() && back->notes[0].active && ! back->notes[1].active && back->edits.at (0).curve.size() == 3);
    // an edit of the pitch movement only is an edit
    CHECK (! e.identity() && e.pitchMoves() && Edit {}.identity() && ! Edit {}.pitchMoves());
    Edit flatCurve;
    flatCurve.curve = { 0.0f, 0.0f };
    CHECK (flatCurve.identity());
    // round 190lk's version 1: every note is a note, the edit has no pitch movement
    const auto v1 = parse (pe190lo::version1Blob());
    CHECK (v1.has_value() && v1->notes.size() == 1 && v1->notes[0].active && v1->notes[0].f0.size() == 3);
    CHECK (v1.has_value() && v1->edits.count (0) == 1 && v1->edits.at (0).semitones == 2.0f && v1->edits.at (0).gainDb == -3.0f
           && v1->edits.at (0).modulation == 1.0f && v1->edits.at (0).drift == 1.0f && v1->edits.at (0).curve.empty());
    // cut short: nothing
    auto cut = write (m);
    cut.resize (cut.size() - 3);
    CHECK (! parse (cut).has_value());
}

TEST (polyedit190lo_candidates_come_after_the_notes_for_the_engine)
{
    using namespace lynx::polyedit;
    Model m;
    m.sampleRate = 44100.0;
    m.frames = 44100;
    Note n;
    n.f0 = { 220.0f };
    m.notes = { n, n, n, n };
    m.notes[1].active = false;
    m.notes[3].active = false;
    CHECK (analysisIndex (m, 0) == 0 && analysisIndex (m, 2) == 1 && analysisIndex (m, 1) == 2 && analysisIndex (m, 3) == 3);
    CHECK (analysisIndex (m, -1) == -1 && analysisIndex (m, 4) == -1);
    CHECK ((activeNotes (m) == std::vector<int> { 0, 2 }));
    const auto a = toAnalysis (m);
    CHECK (a->notes.size() == 4 && a->notes[0].active && a->notes[1].active && ! a->notes[2].active && ! a->notes[3].active);
    // an edit of a candidate does nothing until it is made a note
    m.edits[1].semitones = 2.0f;
    CHECK (! m.edited());
    m.notes[1].active = true;
    CHECK (m.edited() && analysisIndex (m, 1) == 1 && analysisIndex (m, 2) == 2);
}

TEST (polyedit190lo_modulation_and_drift_split_the_pitch_track)
{
    using namespace lynx::polyedit;
    const double fs = 512.0 / 44100.0;
    const auto n = pe190lo::wobbly (60.0, 260, fs, 0.4, 0.6);
    const auto parts = pitchParts (n, fs);
    CHECK (parts.drift.size() == n.f0.size() && parts.modulation.size() == n.f0.size());
    // (away from the ends: the vibrato is in the modulation, the slow rise in the drift)
    const auto mod = pe190lo::rms (parts.modulation, 40, 220);
    CHECK (mod > 0.2 && mod < 0.35);
    CHECK (parts.drift[220] - parts.drift[40] > 0.3 && parts.drift[220] - parts.drift[40] < 0.5);
    // modulation 0 takes the vibrato out, drift 0 the rise; 1 / 1 is nothing
    Edit flat;
    flat.modulation = 0.0f;
    const auto c = pitchCurveOf (n, flat, fs);
    CHECK (c.size() == n.f0.size());
    std::vector<float> left (c.size());
    for (std::size_t i = 0; i < c.size(); ++i)
        left[i] = parts.modulation[i] + c[i];
    CHECK (pe190lo::rms (left, 40, 220) < 0.01);
    CHECK (pitchCurveOf (n, Edit {}, fs).empty());
    Edit drawn;
    drawn.curve.assign (10, 1.0f);
    const auto d = pitchCurveOf (n, drawn, fs);
    CHECK (d.size() == n.f0.size() && d[5] == 1.0f && d[20] == 0.0f);
}

TEST (polyedit190lo_fallback_detector_without_evidence_is_the_harmonic_one)
{
    using namespace lynx::polyedit;
    const auto source = pe190lk::chord ({ 57.0, 64.0 }, 1.0);
    Model harmonic, fallback;
    AnalyseOptions h;
    h.detector = Detector::harmonic;
    CHECK (analyse (*source, nullptr, harmonic, nullptr, {}, h));
    CHECK (analyse (*source, nullptr, fallback));
    CHECK (harmonic == fallback && pe190lk::noteNear (fallback, 57.0) >= 0 && pe190lk::noteNear (fallback, 64.0) >= 0);
    // without candidates kept: the active notes only
    AnalyseOptions lean;
    lean.keepCandidates = false;
    Model notesOnly;
    CHECK (analyse (*source, nullptr, notesOnly, nullptr, {}, lean));
    CHECK (std::all_of (notesOnly.notes.begin(), notesOnly.notes.end(), [] (const Note& n) { return n.active; }));
    CHECK (activeNotes (notesOnly).size() == activeNotes (fallback).size());
}

TEST (polyedit190lo_slow_reattacks_split_a_held_pitch)
{
    using namespace lynx::polyedit;
    // a pad-like C4 (0.25 s attacks, 0.1 s release) held through a chord change 0.3 s apart: G3 + C4, then A3 + C4
    auto source = std::make_shared<lynx::SampleData>();
    source->sampleRate = pe190lk::rate;
    source->left.assign ((std::size_t) (2.8 * pe190lk::rate), 0.0f);
    pe190lo::tone (source->left, pe190lk::rate, 60.0, 0.0, 0.9, 0.25);
    pe190lo::tone (source->left, pe190lk::rate, 55.0, 0.0, 0.9, 0.25);
    pe190lo::tone (source->left, pe190lk::rate, 60.0, 1.2, 2.2, 0.25);
    pe190lo::tone (source->left, pe190lk::rate, 57.0, 1.2, 2.2, 0.25);
    auto notesAt = [] (const Model& m, double key) {
        int n = 0;
        for (const auto& note : m.notes)
            n += note.active && std::abs (note.medianMidi() - key) < 0.4 ? 1 : 0;
        return n;
    };
    AnalyseOptions split, joined;
    joined.slowReattacks = false;
    Model a, b;
    CHECK (analyse (*source, nullptr, a, nullptr, {}, split));
    CHECK (analyse (*source, nullptr, b, nullptr, {}, joined));
    CHECK (notesAt (a, 60.0) == 2 && notesAt (a, 55.0) == 1 && notesAt (a, 57.0) == 1);
    CHECK (notesAt (b, 60.0) <= 1);
}

TEST (polyedit190lo_overlap_aware_moves_keep_the_shared_partials)
{
    using namespace lynx::polyedit;
    // C4 + G4 (G4's 2nd partial meets C4's 3rd) held; G4 moved 0.1 s later. The true result: G4 starting 0.1 s later
    const double rate = pe190lk::rate;
    auto make = [rate] (double gStart) {
        auto s = std::make_shared<lynx::SampleData>();
        s->sampleRate = rate;
        s->left.assign ((std::size_t) (2.0 * rate), 0.0f);
        pe190lo::tone (s->left, rate, 60.0, 0.0, 1.5, 0.01);
        pe190lo::tone (s->left, rate, 67.0, gStart, 1.5, 0.01);
        return s;
    };
    const auto source = make (0.0), truth = make (0.1);
    Model m;
    CHECK (analyse (*source, nullptr, m));
    const auto g = pe190lk::noteNear (m, 67.0);
    CHECK (g >= 0 && m.overlapAwareMoves);
    if (g < 0)
        return;
    m.edits[g].moveSeconds = 0.1;
    auto error = [&] (const lynx::SampleData& out) {
        // the level error of the shared partial (784 Hz) over the held part, in 0.1 s steps
        double e = 0.0;
        for (double t = 0.4; t < 1.3; t += 0.1)
            e += std::abs (std::log (pe190lk::level (out.left, 784.0, t, t + 0.1) / pe190lk::level (truth->left, 784.0, t, t + 0.1)));
        return e;
    };
    const auto aware = render (*source, m);
    auto plainModel = m;
    plainModel.overlapAwareMoves = false;
    const auto plain = render (*source, plainModel);
    CHECK (aware != nullptr && plain != nullptr);
    if (aware != nullptr && plain != nullptr)
        CHECK (error (*aware) < error (*plain));
    // round 190lk's blobs play as they did; the flag is saved
    CHECK (! parse (pe190lo::version1Blob())->overlapAwareMoves && parse (write (m))->overlapAwareMoves && ! parse (write (plainModel))->overlapAwareMoves);
}

TEST (polyedit190lo_modulation_zero_flattens_a_vibrato)
{
    using namespace lynx::polyedit;
    auto source = std::make_shared<lynx::SampleData>();
    source->sampleRate = pe190lk::rate;
    source->left.assign ((std::size_t) (2.0 * pe190lk::rate), 0.0f);
    pe190lo::tone (source->left, pe190lk::rate, 57.0, 0.0, 1.6, 0.02, 0.4);
    Model m;
    CHECK (analyse (*source, nullptr, m));
    const auto k = pe190lk::noteNear (m, 57.0);
    CHECK (k >= 0);
    if (k < 0)
        return;
    m.edits[k].modulation = 0.0f;
    const auto out = render (*source, m);
    CHECK (out != nullptr);
    if (out == nullptr)
        return;
    const auto before = pe190lo::spread (pe190lo::peakTrack (source->left, pe190lk::rate, 200.0, 240.0, 0.3, 1.4));
    const auto after = pe190lo::spread (pe190lo::peakTrack (out->left, pe190lk::rate, 200.0, 240.0, 0.3, 1.4));
    CHECK (before > 3.0 && after < 0.5 * before);
}
