// --- Round 190lk: the polyphonic note editor (core/PolyEdit.h on PitchNet's engine, third_party/pitchnet-poly): the
// saved model, the analysis of a chord, renders (unedited = untouched, a note moved inside a chord, masks built on
// demand = PitchNet's full analysis), the render cache and the audio clip channel that plays the edited render ----------

#include "core/ClipOps.h"
#include "core/FlClipMenu.h"
#include "core/PolyEdit.h"
#include "core/RackRenderer.h"

#include "../third_party/pitchnet-poly/PolyEngine.h"

namespace pe190lk
{
constexpr double rate = 44100.0;
double hz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

// a chord of harmonic tones (8 partials, 1/k), `seconds` long, 10 ms fades; the notes start `stagger` apart
std::shared_ptr<lynx::SampleData> chord (std::initializer_list<double> keys, double seconds = 1.6, double stagger = 0.0, bool stereo = true)
{
    auto s = std::make_shared<lynx::SampleData>();
    s->sampleRate = rate;
    const auto n = (std::size_t) (seconds * rate);
    s->left.assign (n, 0.0f);
    int v = 0;
    for (const auto key : keys)
    {
        const auto start = (std::size_t) (stagger * v++ * rate);
        for (std::size_t i = start; i < n; ++i)
        {
            const auto t = (double) (i - start) / rate;
            const auto env = std::min ({ 1.0, t / 0.01, (double) (n - i) / (0.01 * rate) });
            double x = 0.0;
            for (int k = 1; k <= 8; ++k)
                x += std::sin (2.0 * 3.141592653589793 * k * hz (key) * t + 0.3 * k) / k;
            s->left[i] += (float) (0.06 * env * x);
        }
    }
    if (stereo)
        s->right = s->left;
    return s;
}

// the level of a frequency in a stretch of a signal (DFT bin, Hann)
double level (const std::vector<float>& x, double f, double from, double to)
{
    const auto a = (std::size_t) (from * rate), b = std::min (x.size(), (std::size_t) (to * rate));
    double re = 0.0, im = 0.0;
    for (auto i = a; i < b; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * (double) (i - a) / (double) (b - a));
        re += w * x[i] * std::cos (2.0 * 3.141592653589793 * f * (double) i / rate);
        im += w * x[i] * std::sin (2.0 * 3.141592653589793 * f * (double) i / rate);
    }
    return std::sqrt (re * re + im * im) / (double) (b - a);
}

int noteNear (const lynx::polyedit::Model& m, double key)
{
    for (int k = 0; k < (int) m.notes.size(); ++k)
        if (m.notes[(std::size_t) k].active && std::abs (m.notes[(std::size_t) k].medianMidi() - key) < 0.4) // (190lo: not a candidate)
            return k;
    return -1;
}

const lynx::polyedit::Model& cMajor()
{
    static const auto model = [] {
        lynx::polyedit::Model m;
        lynx::polyedit::analyse (*chord ({ 60.0, 64.0, 67.0 }), nullptr, m);
        return m;
    }();
    return model;
}
} // namespace pe190lk

TEST (polyedit190lk_saved_model_round_trip)
{
    using namespace lynx::polyedit;
    Model m;
    m.sampleRate = 48000.0;
    m.frames = 123456;
    m.evidence = true;
    m.takeBackground = false;
    Note a;
    a.startFrame = 3;
    a.endFrame = 5;
    a.f0 = { 261.6f, 261.7f, 261.5f };
    a.inharmonicity = 1.5e-4f;
    a.energy = 2.5;
    a.probability = 0.9f;
    m.notes = { a, a };
    m.notes[1].f0 = { 330.0f };
    m.notes[1].startFrame = m.notes[1].endFrame = 9;
    m.edits[1] = { -1.0f, -6.0f, 0.125, 1.5f, false };
    m.edits[0] = {}; // (an identity edit is not kept)
    const auto blob = write (m);
    CHECK (blob.size() > 8 && blob[0] == 'L' && blob[3] == 'E');
    const auto back = parse (blob);
    CHECK (back.has_value());
    auto expected = m;
    expected.edits.erase (0);
    CHECK (back.has_value() && *back == expected);
    CHECK (back.has_value() && back->edited() && std::abs (back->notes[0].medianMidi() - 60.0f) < 0.05f);
    // broken blobs: nothing
    CHECK (! parse ({}).has_value());
    CHECK (! parse ({ 'L', 'X', 'N', 'T', 1, 0, 0, 0 }).has_value());
    auto cut = blob;
    cut.resize (cut.size() - 7);
    CHECK (! parse (cut).has_value());
    // a different edit state, a different key; the same one, the same
    auto m2 = m;
    m2.edits[1].semitones = -2.0f;
    CHECK (stateKey (write (m2)) != stateKey (blob) && stateKey (write (m)) == stateKey (blob) && stateKey ({}).empty());
}

TEST (polyedit190lk_analysis_finds_the_chord)
{
    using namespace lynx::polyedit;
    const auto& m = pe190lk::cMajor();
    CHECK (m.sampleRate == pe190lk::rate && m.frames == (std::int64_t) (1.6 * pe190lk::rate) && m.hop == 512 && m.fftSize == 4096);
    CHECK (pe190lk::noteNear (m, 60.0) >= 0 && pe190lk::noteNear (m, 64.0) >= 0 && pe190lk::noteNear (m, 67.0) >= 0);
    CHECK (! m.edited());
    // notes in order of their start (round 190lo: the active notes; the rejected candidates follow them)
    for (std::size_t k = 1; k < m.notes.size(); ++k)
        CHECK (! m.notes[k].active || (m.notes[k - 1].active && m.notes[k - 1].startFrame <= m.notes[k].startFrame));
}

TEST (polyedit190lk_unedited_plays_the_source_itself)
{
    using namespace lynx;
    const auto source = pe190lk::chord ({ 60.0, 64.0, 67.0 });
    auto m = pe190lk::cMajor();
    CHECK (polyedit::render (*source, m) == nullptr);
    std::shared_ptr<const SampleData> shared = source;
    CHECK (polyedit::renderedFor ("chord.wav", shared, polyedit::write (m)) == shared);
    CHECK (polyedit::renderedFor ("chord.wav", shared, {}) == shared);
    // a model of another sample (length): the source plays
    m.edits[0].semitones = 1.0f;
    m.frames += 1;
    CHECK (polyedit::renderedFor ("chord.wav", shared, polyedit::write (m)) == shared);
}

TEST (polyedit190lk_a_note_moves_inside_the_chord)
{
    using namespace lynx;
    const auto source = pe190lk::chord ({ 60.0, 64.0, 67.0 });
    auto m = pe190lk::cMajor();
    const auto e = pe190lk::noteNear (m, 64.0);
    CHECK (e >= 0);
    if (e < 0)
        return;
    m.edits[e].semitones = -1.0f; // C major -> C minor
    const auto out = polyedit::render (*source, m);
    CHECK (out != nullptr && out->left.size() == source->left.size() && out->right.size() == source->right.size());
    if (out == nullptr)
        return;
    const auto e4 = pe190lk::hz (64.0), eb4 = pe190lk::hz (63.0), c4 = pe190lk::hz (60.0), g4 = pe190lk::hz (67.0);
    const auto before = [&] (double f) { return pe190lk::level (source->left, f, 0.3, 1.3); };
    const auto after = [&] (double f) { return pe190lk::level (out->left, f, 0.3, 1.3); };
    // the E is gone (more than 20 dB down), the Eb is there at about the E's level, C and G stay within 1 dB
    CHECK (after (e4) < 0.1 * before (e4));
    CHECK (after (eb4) > 0.5 * before (e4) && after (eb4) < 1.5 * before (e4));
    CHECK (std::abs (20.0 * std::log10 (after (c4) / before (c4))) < 1.0);
    CHECK (std::abs (20.0 * std::log10 (after (g4) / before (g4))) < 1.0);
}

TEST (polyedit190lk_masks_on_demand_render_as_the_full_analysis)
{
    // third_party/pitchnet-poly/PolyLazy.cpp (LYNX): the masks of the blocks an edit reaches, built when rendering,
    // give PitchNet's render with every mask built at analysis time, sample for sample (pitch, level, move, mute)
    using namespace lynx;
    const auto source = pe190lk::chord ({ 48.0, 60.0, 64.0, 67.0 }, 9.5, 0.05);
    std::vector<float> mono (source->left.size());
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (source->left[i] + source->right[i]);
    auto full = polyphonic::Params::forSampleRate (pe190lk::rate);
    const auto eager = polyphonic::analyze (mono.data(), (int) mono.size(), full);
    auto deferred = full;
    deferred.deferMasks = true;
    auto lazy = polyphonic::analyze (mono.data(), (int) mono.size(), deferred);
    CHECK (polyphonic::maskBytes (lazy) == 0 && polyphonic::maskBytes (eager) > 0);
    CHECK (lazy.notes.size() == eager.notes.size() && lazy.maskBlocksBuilt.size() == 3);
    int moved = 0;
    for (int k = 0; k < (int) eager.notes.size(); ++k)
        if (eager.notes[(std::size_t) k].active && std::abs (eager.notes[(std::size_t) k].medianMidi() - 64.0f) < 0.4f)
            moved = k;
    for (int variant = 0; variant < 4; ++variant)
    {
        std::map<int, polyphonic::NoteEdit> edits;
        auto& e = edits[moved];
        e.takeBackground = true;
        if (variant == 0)
            e.semitones = -1.0f;
        if (variant == 1)
            e.gainDb = -6.0f;
        if (variant == 2)
            e.timeOffsetSeconds = 0.125;
        if (variant == 3)
            e.mute = true;
        auto mine = lazy;
        for (const auto& [lo, hi] : polyphonic::framesForEdits (mine, edits))
            CHECK (polyphonic::ensureMasks (mine, mono.data(), (int) mono.size(), lo, hi));
        const std::vector<std::vector<float>> channels { source->left, source->right };
        const auto a = polyphonic::render (eager, channels, edits);
        const auto b = polyphonic::render (mine, channels, edits);
        float worst = 0.0f;
        for (std::size_t c = 0; c < a.size(); ++c)
            for (std::size_t i = 0; i < a[c].size(); ++i)
                worst = std::max (worst, std::fabs (a[c][i] - b[c][i]));
        CHECK (worst < 1e-6f);
    }
    // released masks are built again on demand
    polyphonic::releaseMasks (lazy);
    CHECK (polyphonic::maskBytes (lazy) == 0);
}

TEST (polyedit190lk_renders_are_cached)
{
    using namespace lynx;
    polyedit::clearCache();
    const std::shared_ptr<const SampleData> source = pe190lk::chord ({ 60.0, 64.0, 67.0 });
    auto m = pe190lk::cMajor();
    m.edits[0].gainDb = -6.0f;
    const auto blob = polyedit::write (m);
    const auto first = polyedit::renderedFor ("chord.wav", source, blob);
    CHECK (first != nullptr && first != source);
    CHECK (polyedit::renderedFor ("chord.wav", source, blob) == first); // (no second render)
    // what the editor already rendered is played at once
    auto made = std::make_shared<SampleData> (*source);
    m.edits[0].gainDb = -3.0f;
    const auto blob2 = polyedit::write (m);
    polyedit::remember ("chord.wav", blob2, made);
    CHECK (polyedit::renderedFor ("chord.wav", source, blob2) == made);
    polyedit::clearCache();
}

TEST (polyedit190lk_the_clip_channel_plays_the_edited_render)
{
    using namespace lynx;
    polyedit::clearCache();
    const std::shared_ptr<const SampleData> source = pe190lk::chord ({ 60.0, 64.0, 67.0 });
    Song s;
    s.bpm = 120.0;
    ChannelDef ch;
    ch.instrument = InstrumentKind::audioClip;
    ch.samplePath = "chord.wav";
    s.channels.push_back (ch);
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = 0;
    c.start = 0;
    c.length = 3 * 960; // 1.5 s
    s.playlist.push_back (c);
    RackState::Factories f;
    f.samples = [source] (const std::string&) { return source; };
    const auto plain = renderClipAudio (s, 0, pe190lk::rate, f);
    auto m = pe190lk::cMajor();
    const auto e = pe190lk::noteNear (m, 64.0);
    CHECK (e >= 0);
    m.edits[e].semitones = -1.0f;
    s.channels[0].polyEdit = polyedit::write (m);
    const auto edited = renderClipAudio (s, 0, pe190lk::rate, f);
    CHECK (plain.left.size() == edited.left.size() && ! plain.left.empty());
    const auto e4 = pe190lk::hz (64.0), eb4 = pe190lk::hz (63.0);
    CHECK (pe190lk::level (edited.left, e4, 0.3, 1.2) < 0.2 * pe190lk::level (plain.left, e4, 0.3, 1.2));
    CHECK (pe190lk::level (edited.left, eb4, 0.3, 1.2) > 3.0 * pe190lk::level (plain.left, eb4, 0.3, 1.2));
    // the edit state is part of the channel's equality (another edit: another instrument)
    auto other = s.channels[0];
    other.polyEdit.clear();
    CHECK (! (other == s.channels[0]));
    // the clip menu's LYNX extras list the editor (FL has none: not one of FL's items)
    const auto extras = flclipmenu::lynxExtras();
    CHECK (std::find (extras.begin(), extras.end(), std::string ("Edit notes (polyphonic)...")) != extras.end());
    for (const auto& item : flclipmenu::fl())
        CHECK (item.text != "Edit notes (polyphonic)...");
    polyedit::clearCache();
}
