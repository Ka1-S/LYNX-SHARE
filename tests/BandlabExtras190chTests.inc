// Round 190ch (LYNX's own features in the roles BandLab offers; all local): the project's Lyrics / Notes
// (core/Lyrics.h), reference matching for mastering (core/ReferenceMatch.h) and the "Clean voice" chain
// (core/CleanVoice.h).

#include "core/CleanVoice.h"
#include "core/Lyrics.h"
#include "core/ReferenceMatch.h"

namespace bl190ch
{
constexpr double rate = 44100.0;

// deterministic noise, −1 … 1
struct Lcg
{
    std::uint32_t state;
    explicit Lcg (std::uint32_t seed) : state (seed) {}
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) ((double) (state >> 8) / 8388608.0 - 1.0);
    }
};

// pink noise (Paul Kellet's filter of white noise), about −20 dBFS RMS
inline std::vector<float> pink (std::size_t frames, std::uint32_t seed)
{
    Lcg white (seed);
    std::vector<float> out (frames);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& v : out)
    {
        const double w = white.next();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        v = (float) ((b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.05);
        b6 = w * 0.115926;
    }
    return out;
}

inline void sine (std::vector<float>& out, std::size_t from, std::size_t to, double hz, double peakDb)
{
    const auto a = std::pow (10.0, peakDb / 20.0);
    for (auto i = from; i < to && i < out.size(); ++i)
        out[i] += (float) (a * std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / rate));
}

// a chain of slots run over a stereo buffer, in blocks
inline void run (const std::vector<lynx::EffectSlot>& slots, std::vector<float>& l, std::vector<float>& r)
{
    for (const auto& slot : slots)
    {
        auto fx = lynx::makeEffect (slot);
        fx->prepare (rate);
        fx->reset();
        for (std::size_t at = 0; at < l.size(); at += 512)
            fx->process (l.data() + at, r.data() + at, (int) std::min<std::size_t> (512, l.size() - at), slot.params, 120.0);
    }
}

inline double rmsDb (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (auto i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return 10.0 * std::log10 (sum / (double) (to - from) + 1.0e-20);
}
} // namespace bl190ch

// --- Lyrics / Notes --------------------------------------------------------------------------------------------------
TEST (bl190ch_lyrics_lines_headings_and_positions)
{
    using namespace lynx;
    const std::string text = "[Verse 1]\n[5] Walking down the line\n  [5.3]   second half\nplain words\n[9] [Chorus]\n[12]\n[not closed\n[a] and more";
    const auto lines = lyrics::parse (text);
    CHECK (lines.size() == 8);
    CHECK (lines[0].section && lines[0].title == "Verse 1" && ! lines[0].stamped);
    CHECK (lines[1].stamped && lines[1].bar == 5 && lines[1].beat == 1 && lines[1].body == "Walking down the line" && ! lines[1].section);
    CHECK (lines[2].stamped && lines[2].bar == 5 && lines[2].beat == 3 && lines[2].body == "second half");
    CHECK (! lines[3].stamped && ! lines[3].section && lines[3].body == "plain words");
    CHECK (lines[4].stamped && lines[4].bar == 9 && lines[4].section && lines[4].title == "Chorus");
    CHECK (lines[5].stamped && lines[5].bar == 12 && lines[5].body.empty() && ! lines[5].section); // a position alone is not a heading
    CHECK (! lines[6].section && ! lines[6].stamped);
    CHECK (! lines[7].section && ! lines[7].stamped && lines[7].body == "[a] and more"); // a heading is the whole line
    // the byte ranges are the lines
    for (const auto& line : lines)
        CHECK (text.substr (line.begin, line.end - line.begin).find ('\n') == std::string::npos);
    CHECK (text.substr (lines[1].bodyBegin, lines[1].end - lines[1].bodyBegin) == "Walking down the line");
    CHECK (lyrics::lineAt (lines, 0) == 0 && lyrics::lineAt (lines, lines[2].begin + 3) == 2 && lyrics::lineAt (lines, text.size()) == 7);
    // an empty text is one empty line; Windows line ends don't leak into the words
    CHECK (lyrics::parse ("").size() == 1);
    const auto crlf = lyrics::parse ("[Intro]\r\n[2] hello\r\n");
    CHECK (crlf.size() == 3 && crlf[0].section && crlf[0].title == "Intro" && crlf[1].body == "hello");
    // not positions: bar 0, letters, a negative number
    for (const char* bad : { "[0] x", "[1.0] x", "[1a] x", "[-3] x", "[] x", "[1.2.3] x" })
        CHECK (! lyrics::parse (bad)[0].stamped);
    CHECK (lyrics::wordCount (text) == 13); // headings and positions don't count
}

TEST (bl190ch_lyrics_positions_follow_the_songs_bars)
{
    using namespace lynx;
    Song song;
    song.beatsPerBar = 4;
    const auto bar = (std::int64_t) ppq * 4;
    CHECK (lyrics::tickOf (song, 1, 1) == 0);
    CHECK (lyrics::tickOf (song, 5, 1) == 4 * bar);
    CHECK (lyrics::tickOf (song, 5, 3) == 4 * bar + 2 * ppq);
    CHECK (lyrics::tickOf (song, 2, 9) < 2 * bar); // a beat past the bar stays in the bar
    CHECK (lyrics::barBeatOf (song, 4 * bar + 2 * ppq + 17) == std::make_pair (5, 3));
    CHECK (lyrics::stampText (9, 1) == "[9]" && lyrics::stampText (9, 3) == "[9.3]");
    // a time signature change: from bar 3 on the bars are 3/4
    Marker waltz;
    waltz.tick = 2 * bar;
    waltz.numerator = 3;
    waltz.denominator = 4;
    song.addMarker (waltz);
    for (int b = 1; b <= 12; ++b)
    {
        const auto tick = lyrics::tickOf (song, b, 1);
        const auto at = song.barAt (tick);
        CHECK (at.number == b && at.tick == tick);
        CHECK (lyrics::barBeatOf (song, tick) == std::make_pair (b, 1));
    }
    CHECK (lyrics::tickOf (song, 4, 1) == 2 * bar + 3 * ppq);
}

TEST (bl190ch_lyrics_marker_at_playhead_and_jumps)
{
    using namespace lynx;
    Song song;
    const auto bar = (std::int64_t) ppq * 4;
    song.info.comments = "[Verse]\nFirst line of the verse that is rather long and keeps going on\n\n[Chorus]\nSing it";
    // the heading takes the playhead's place (inside bar 3, beat 2) and a marker named after it is put there
    CHECK (lyrics::markAtPlayhead (song, 0, 2 * bar + ppq + 100));
    CHECK (song.info.comments.rfind ("[3.2] [Verse]\n", 0) == 0);
    CHECK (song.markers().size() == 1 && song.markers()[0].tick == 2 * bar + ppq && song.markers()[0].name == "Verse");
    // a text line: the marker gets its first words (never longer than 40 bytes)
    CHECK (lyrics::markAtPlayhead (song, 1, 4 * bar));
    CHECK (song.markers().size() == 2 && song.markers()[1].tick == 4 * bar);
    CHECK (song.markers()[1].name.size() <= 40 && song.markers()[1].name.rfind ("First line of the verse", 0) == 0);
    // again on the same line and place: the position is replaced, no second marker
    CHECK (lyrics::markAtPlayhead (song, 1, 4 * bar));
    CHECK (song.markers().size() == 2);
    auto lines = lyrics::parse (song.info.comments);
    CHECK (lines[1].stamped && lines[1].bar == 5 && lines[1].beat == 1);
    CHECK (std::count (song.info.comments.begin(), song.info.comments.end(), '[') == 4); // [3.2] [Verse] [5] [Chorus]
    // moving the line elsewhere re-stamps it
    CHECK (lyrics::markAtPlayhead (song, 1, 6 * bar));
    lines = lyrics::parse (song.info.comments);
    CHECK (lines[1].bar == 7 && lines[1].body.rfind ("First line", 0) == 0);
    // an empty line gets a name of its own; a line that does not exist changes nothing
    CHECK (lyrics::markAtPlayhead (song, 2, 8 * bar));
    CHECK (song.markers().back().name == "Line 3");
    const auto before = song;
    CHECK (! lyrics::markAtPlayhead (song, 99, 0));
    CHECK (song.info.comments == before.info.comments && song.markers().size() == before.markers().size());

    // jumps: a stamped line goes to its place; a heading without one goes to the marker of the same name
    lines = lyrics::parse (song.info.comments);
    CHECK (lyrics::jumpTick (song, lines[0]) == std::optional<std::int64_t> (2 * bar + ppq));
    CHECK (! lyrics::jumpTick (song, lines[3])); // [Chorus]: no position, no marker of that name yet
    Marker chorus;
    chorus.tick = 12 * bar;
    chorus.name = "CHORUS";
    song.addMarker (chorus);
    CHECK (lyrics::jumpTick (song, lines[3]) == std::optional<std::int64_t> (12 * bar)); // (the name's case does not matter)
    CHECK (! lyrics::jumpTick (song, lines[4]));
    // the line that is playing: the last place at or before the playhead, in song order
    CHECK (lyrics::currentLine (song, lines, 0) == -1);
    CHECK (lyrics::currentLine (song, lines, 2 * bar + ppq) == 0);
    CHECK (lyrics::currentLine (song, lines, 7 * bar) == 1);
    CHECK (lyrics::currentLine (song, lines, 9 * bar) == 2);
    CHECK (lyrics::currentLine (song, lines, 500 * bar) == 3);
    // taking a position off
    const auto cleared = lyrics::withoutStamp (song.info.comments, 0);
    CHECK (cleared.rfind ("[Verse]\n", 0) == 0);
    CHECK (lyrics::withoutStamp (cleared, 0) == cleared);
    CHECK (lyrics::withStamp ("", 0, 2, 1) == "[2]" && lyrics::withStamp ("a\nb", 1, 3, 2) == "a\n[3.2] b");
}

TEST (bl190ch_lyrics_search_and_text_export)
{
    using namespace lynx;
    const std::string text = "[Chorus]\nLove is LOVE, love\nCora\xc3\xa7\xc3\xa3o e amor";
    const auto hits = lyrics::find (text, "love");
    CHECK (hits.size() == 3);
    for (const auto& [a, b] : hits)
        CHECK (b - a == 4 && lynx::lyrics::detail::sameText (text.substr (a, 4), "LOVE"));
    CHECK (lyrics::find (text, "cora\xc3\xa7\xc3\xa3o").size() == 1); // ASCII letters either case, other bytes as written
    CHECK (lyrics::find (text, "").empty() && lyrics::find (text, "nothing here").empty() && lyrics::find ("ab", "abc").empty());
    CHECK (lyrics::find ("aaaa", "aa").size() == 2); // (not overlapping)

    Song song;
    song.bpm = 120.0; // one bar = 2 s
    song.info.title = "Night Drive";
    song.info.author = "Somebody";
    song.info.comments = "[Verse]\n[9] First\r\n[9.3] Second\nno position\n[41]";
    CHECK (lyrics::exportText (song) == "Night Drive\nSomebody\n\n[Verse]\n[9] First\n[9.3] Second\nno position\n[41]\n");
    CHECK (lyrics::exportText (song, lyrics::Stamps::none) == "Night Drive\nSomebody\n\n[Verse]\nFirst\nSecond\nno position\n");
    CHECK (lyrics::exportText (song, lyrics::Stamps::times) == "Night Drive\nSomebody\n\n[Verse]\n[0:16] First\n[0:17] Second\nno position\n[1:20]\n");
    song.info.title.clear();
    song.info.author.clear();
    song.info.comments = "one";
    CHECK (lyrics::exportText (song) == "one\n");
    // the text is the project's Comments: nothing else holds it (one store, the field .lynx and .flp already carry)
    Song other = song;
    CHECK (other.info == song.info);
}

// --- Reference match ---------------------------------------------------------------------------------------------------
TEST (bl190ch_reference_analysis_matches_ebu_r128_and_known_signals)
{
    using namespace lynx;
    const auto second = (std::size_t) bl190ch::rate;
    // EBU Tech 3341 case 1 and 2: a stereo 1 kHz sine at −23 / −33 dBFS reads −23.0 / −33.0 LUFS (±0.1)
    for (const double level : { -23.0, -33.0 })
    {
        std::vector<float> l (20 * second, 0.0f);
        bl190ch::sine (l, 0, l.size(), 1000.0, level);
        const auto a = refmatch::analyse (l, l, bl190ch::rate);
        CHECK (a.valid && std::abs (a.seconds - 20.0) < 0.01);
        CHECK (std::abs (a.lufs - level) < 0.1);
        CHECK (std::abs (a.truePeakDb - level) < 0.2);
        CHECK (a.correlation > 0.999 && a.widthPercent() < 0.01); // both channels the same: mono
        // all of the sine's power (−3.01 dB under its peak) is in the 1 kHz band, and nowhere else
        CHECK (std::abs (refmatch::bandHz (17) - 1000.0) < 1.0e-9);
        CHECK (std::abs (a.spectrumDb[17] - (level - 3.01)) < 0.1);
        CHECK (a.spectrumDb[14] < level - 70.0 && a.spectrumDb[20] < level - 70.0);
    }
    // case 3 (gating): 10 s at −36, 60 s at −23, 10 s at −36 dBFS still reads −23.0 LUFS (±0.1)
    {
        std::vector<float> l (80 * second, 0.0f);
        bl190ch::sine (l, 0, 10 * second, 1000.0, -36.0);
        bl190ch::sine (l, 10 * second, 70 * second, 1000.0, -23.0);
        bl190ch::sine (l, 70 * second, 80 * second, 1000.0, -36.0);
        CHECK (std::abs (refmatch::analyse (l, l, bl190ch::rate).lufs - -23.0) < 0.1);
    }
    // stereo width: unrelated noise left and right is half side energy; one channel inverted is all side
    {
        const auto l = bl190ch::pink (10 * second, 11u), r = bl190ch::pink (10 * second, 977u);
        const auto a = refmatch::analyse (l, r, bl190ch::rate);
        CHECK (std::abs (a.correlation) < 0.05 && std::abs (a.widthPercent() - 50.0) < 3.0 && std::abs (a.sideDb) < 0.6);
        auto inv = l;
        for (auto& v : inv)
            v = -v;
        const auto b = refmatch::analyse (l, inv, bl190ch::rate);
        CHECK (b.correlation < -0.999 && b.widthPercent() > 99.9);
        // pink noise: every third-octave band holds the same power (±1 dB over 50 Hz … 10 kHz on 10 s)
        const auto mono = refmatch::analyse (l, l, bl190ch::rate);
        for (int k = 4; k <= 27; ++k)
            CHECK (std::abs (mono.spectrumDb[(std::size_t) k] - mono.spectrumDb[17]) < 1.5);
    }
    // too short, or nothing: not valid, and a suggestion from it is not either
    std::vector<float> tiny (1000, 0.1f);
    CHECK (! refmatch::analyse (tiny, tiny, bl190ch::rate).valid);
    CHECK (! refmatch::suggest ({}, {}, bl190ch::rate).valid);
    // 48 kHz and 22.05 kHz files: the bands above the Nyquist frequency are not used
    {
        std::vector<float> l (4 * 22050, 0.0f);
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = (float) (0.1 * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * (double) i / 22050.0));
        const auto a = refmatch::analyse (l, l, 22050.0);
        CHECK (a.valid && a.usedBands < refmatch::bands && refmatch::bandHz (a.usedBands - 1) < 11025.0);
        CHECK (std::abs (a.spectrumDb[17] - (-20.0 - 3.01)) < 0.1);
    }
}

TEST (bl190ch_reference_match_pink_noise_with_a_known_tilt)
{
    using namespace lynx;
    const auto frames = (std::size_t) (30.0 * bl190ch::rate);
    const auto songL = bl190ch::pink (frames, 1u), songR = bl190ch::pink (frames, 2u);
    // the reference: the same noise tilted by 0.6 dB per octave around 1 kHz (Facet's Flat Tilt: its gain is the level
    // five octaves above the band frequency) and 4 dB quieter
    constexpr double tiltDbPerOctave = 0.6, quieterDb = -4.0;
    auto tilt = makeEffectSlot (EffectKind::facetEq);
    tilt.params[(std::size_t) facet::param (0, 0)] = facet::shapeValue (facet::Shape::flatTilt);
    tilt.params[(std::size_t) facet::param (0, 1)] = facet::hzValue (1000.0);
    tilt.params[(std::size_t) facet::param (0, 2)] = facet::gainValue (tiltDbPerOctave * 5.0);
    tilt.params[(std::size_t) facet::outputParam] = refmatch::detail::valueFor ([] (float v) { return facet::outputDb (v); }, quieterDb);
    auto refL = songL, refR = songR;
    bl190ch::run ({ tilt }, refL, refR);

    const auto song = refmatch::analyse (songL, songR, bl190ch::rate);
    const auto reference = refmatch::analyse (refL, refR, bl190ch::rate);
    CHECK (song.valid && reference.valid);
    // the analyser sees the tilt that was put in (the mean level aside)
    const auto s = refmatch::suggest (song, reference, bl190ch::rate);
    CHECK (s.valid);
    for (int k = 2; k <= 29; ++k) // 31.5 Hz … 16 kHz
    {
        const auto expected = tiltDbPerOctave * std::log2 (refmatch::bandHz (k) / 1000.0);
        CHECK (std::abs (s.differenceDb[(std::size_t) k] - expected) < 0.3);
        CHECK (std::abs (s.targetDb[(std::size_t) k] - expected) < 0.3);
    }
    CHECK (s.fitErrorDb < 0.35);
    // the level: what is left after the EQ's own change of loudness (the tilt turns around 1 kHz: next to nothing)
    CHECK (std::abs (s.gainDb + s.eqLoudnessDb - (reference.lufs - song.lufs)) < 1.0e-9 && std::abs (s.gainDb - quieterDb) < 0.5);
    CHECK (std::abs (refmatch::kWeightingDb (1000.0, 48000.0)) < 0.75 && refmatch::kWeightingDb (10000.0, 48000.0) > 3.5
           && refmatch::kWeightingDb (30.0, 48000.0) < -3.0);
    CHECK (std::abs (s.targetLufs - reference.lufs) < 1.0e-9);

    // one pass: the song through the two Master slots Match adds, measured again
    Song project;
    project.mixer = Song::defaultMixer (2);
    project.mixer[0].effects.clear();
    CHECK (refmatch::apply (project, s));
    CHECK (project.mixer[0].effects.size() == 2);
    auto outL = songL, outR = songR;
    bl190ch::run (project.mixer[0].effects, outL, outR);
    const auto after = refmatch::analyse (outL, outR, bl190ch::rate);
    double worst = 0.0;
    for (int k = 0; k < refmatch::bands; ++k)
        worst = std::max (worst, std::abs (after.spectrumDb[(std::size_t) k] - reference.spectrumDb[(std::size_t) k]));
    if (worst >= 0.5 || std::abs (after.lufs - reference.lufs) >= 0.5)
    {
        std::printf ("  worst band error after one pass: %.3f dB; LUFS song %.2f reference %.2f after %.2f; gain %.2f, EQ loudness %.2f, fit error %.2f\n", worst,
                     song.lufs, reference.lufs, after.lufs, s.gainDb, s.eqLoudnessDb, s.fitErrorDb);
        for (int k = 0; k < refmatch::bands; ++k)
            std::printf ("    %7.0f Hz  after-reference %+.2f  target %+.2f  eq %+.2f\n", refmatch::bandHz (k),
                         after.spectrumDb[(std::size_t) k] - reference.spectrumDb[(std::size_t) k], s.targetDb[(std::size_t) k], s.eqDb[(std::size_t) k]);
    }
    for (int k = 0; k < refmatch::bands; ++k) // every band, 20 Hz … 20 kHz, in absolute level
        CHECK (std::abs (after.spectrumDb[(std::size_t) k] - reference.spectrumDb[(std::size_t) k]) < 0.5);
    CHECK (std::abs (after.lufs - reference.lufs) < 0.5);
    CHECK (after.truePeakDb <= -1.0 + 0.1); // under the limiter's ceiling
}

TEST (bl190ch_reference_match_limits_and_smoothing)
{
    using namespace lynx;
    // smoothing keeps a straight tilt as it is, up to both ends, and takes a zigzag out
    std::array<double, refmatch::bands> line {}, zigzag {};
    for (int k = 0; k < refmatch::bands; ++k)
    {
        line[(std::size_t) k] = 0.37 * k - 4.0;
        zigzag[(std::size_t) k] = k % 2 == 0 ? 2.0 : -2.0;
    }
    const auto smoothLine = refmatch::smoothed (line, refmatch::bands), smoothZig = refmatch::smoothed (zigzag, refmatch::bands);
    for (int k = 0; k < refmatch::bands; ++k)
    {
        CHECK (std::abs (smoothLine[(std::size_t) k] - line[(std::size_t) k]) < 1.0e-6);
        if (k >= 2 && k < refmatch::bands - 2)
            CHECK (std::abs (smoothZig[(std::size_t) k]) < 0.5);
    }

    // a reference that is 15 dB brighter above 2 kHz: no band is asked for more than ±6 dB, and Amount scales it
    refmatch::Analysis song, bright;
    song.valid = bright.valid = true;
    song.lufs = -20.0;
    bright.lufs = -9.0;
    for (int k = 0; k < refmatch::bands; ++k)
    {
        song.spectrumDb[(std::size_t) k] = -30.0;
        bright.spectrumDb[(std::size_t) k] = refmatch::bandHz (k) > 2000.0 ? -15.0 : -30.0;
    }
    const auto full = refmatch::suggest (song, bright, bl190ch::rate);
    double top = -100.0, bottom = 100.0;
    for (int k = 0; k < refmatch::bands; ++k)
    {
        CHECK (std::abs (full.targetDb[(std::size_t) k]) <= refmatch::maxBandDb + 1.0e-9);
        top = std::max (top, full.targetDb[(std::size_t) k]);
        bottom = std::min (bottom, full.targetDb[(std::size_t) k]);
    }
    CHECK (top == refmatch::maxBandDb && bottom < -3.0); // (the mean level is taken out first: the lows go down)
    CHECK (std::abs (full.gainDb + full.eqLoudnessDb - 11.0) < 1.0e-9 && full.eqLoudnessDb > 0.5); // (brighter is louder: less gain is needed)
    bright.lufs = 3.0;
    CHECK (refmatch::suggest (song, bright, bl190ch::rate).gainDb == refmatch::maxLoudnessDb); // the level moves 12 dB at most
    // a measured result that came out 2 LU short is asked for again as a trim; the level still moves 12 dB at most
    bright.lufs = -9.0;
    refmatch::Options trim;
    trim.trimDb = 2.0;
    CHECK (std::abs (refmatch::suggest (song, bright, bl190ch::rate, trim).gainDb - (full.gainDb + 2.0)) < 1.0e-9);
    trim.trimDb = 50.0;
    CHECK (refmatch::suggest (song, bright, bl190ch::rate, trim).gainDb == refmatch::maxLoudnessDb);
    bright.lufs = 3.0;
    refmatch::Options half;
    half.amount = 0.25;
    half.matchLoudness = false;
    const auto quarter = refmatch::suggest (song, bright, bl190ch::rate, half);
    CHECK (quarter.gainDb == 0.0);
    for (int k = 0; k < refmatch::bands; ++k)
        CHECK (std::abs (quarter.targetDb[(std::size_t) k]) <= 0.25 * 15.0 + 1.0e-9);
    refmatch::Options none;
    none.amount = 0.0;
    const auto flat = refmatch::suggest (song, bright, bl190ch::rate, none);
    for (int k = 0; k < refmatch::bands; ++k)
        CHECK (flat.targetDb[(std::size_t) k] == 0.0 && std::abs (flat.eqDb[(std::size_t) k]) < 1.0e-6);
    for (int b = 0; b < refmatch::eqBands; ++b)
        CHECK (facet::gainDb (flat.eq[(std::size_t) facet::param (b, 2)]) == 0.0);
    // the fitted EQ is the layout the header describes, and its output gain is the loudness gain
    CHECK (facet::shape (full.eq[(std::size_t) facet::param (0, 0)]) == facet::Shape::lowShelf);
    CHECK (facet::shape (full.eq[(std::size_t) facet::param (7, 0)]) == facet::Shape::highShelf);
    for (int b = 1; b < 7; ++b)
        CHECK (facet::shape (full.eq[(std::size_t) facet::param (b, 0)]) == facet::Shape::bell);
    CHECK (std::abs (facet::outputDb (full.eq[(std::size_t) facet::outputParam]) - full.gainDb) < 0.01);
    // the same loudness and tone: nothing to do
    const auto same = refmatch::suggest (song, song, bl190ch::rate);
    CHECK (same.valid && std::abs (same.gainDb) < 1.0e-4 && same.fitErrorDb < 1.0e-6);
}

TEST (bl190ch_reference_match_is_two_editable_master_slots)
{
    using namespace lynx;
    refmatch::Analysis a, b;
    a.valid = b.valid = true;
    a.lufs = -18.0;
    b.lufs = -14.0;
    for (int k = 0; k < refmatch::bands; ++k)
        b.spectrumDb[(std::size_t) k] = 0.2 * (k - 15);
    const auto s = refmatch::suggest (a, b, bl190ch::rate);

    Song song;
    song.mixer = Song::defaultMixer (2);
    song.mixer[0].effects.clear();
    auto first = makeEffectSlot (EffectKind::compressor);
    song.mixer[0].effects.push_back (first);
    CHECK (! refmatch::present (song));
    CHECK (refmatch::apply (song, s));
    const auto& fx = song.mixer[0].effects;
    CHECK (fx.size() == 3 && fx[0] == first); // what was on the Master stays, in front
    CHECK (fx[1].kind == EffectKind::facetEq && fx[2].kind == EffectKind::apexLimiter);
    CHECK (fx[1].presetName == refmatch::slotName && fx[2].presetName == refmatch::slotName && fx[1].enabled && fx[2].enabled);
    CHECK (fx[1].look == 0 && fx[2].look == 0); // ordinary slots, not a Master look
    CHECK (fx[1].params == s.eq);
    CHECK (std::abs (apex::ceilingDb (fx[2].params[apex::pCeiling]) - -1.0) < 0.01 && fx[2].params[apex::pTruePeak] == 1.0f
           && apex::gainDb (fx[2].params[apex::pGain]) == 0.0);
    CHECK (refmatch::present (song));
    // "Hear without" switches both, and a second Match replaces them where they are, keeping that state
    refmatch::setBypassed (song, true);
    CHECK (refmatch::bypassed (song) && ! fx[1].enabled && ! fx[2].enabled && fx[0].enabled);
    refmatch::Options o;
    o.amount = 0.5;
    const auto s2 = refmatch::suggest (a, b, bl190ch::rate, o);
    CHECK (refmatch::apply (song, s2));
    CHECK (song.mixer[0].effects.size() == 3 && song.mixer[0].effects[1].params == s2.eq && refmatch::bypassed (song));
    refmatch::setBypassed (song, false);
    CHECK (! refmatch::bypassed (song));
    // a slot the user renamed away is theirs: the next Match adds a fresh pair after cleaning up the half that is left
    song.mixer[0].effects[2].presetName = "My limiter";
    CHECK (refmatch::apply (song, s));
    CHECK (song.mixer[0].effects.size() == 4 && refmatch::matchSlots (song) == std::vector<int> ({ 2, 3 }));
    CHECK (song.mixer[0].effects[1].presetName == "My limiter");
    refmatch::remove (song);
    CHECK (song.mixer[0].effects.size() == 2 && ! refmatch::present (song) && song.mixer[0].effects[0] == first);
    // no room for two more slots: nothing changes
    while ((int) song.mixer[0].effects.size() < maxEffectSlots - 1)
        song.mixer[0].effects.push_back (first);
    const auto full = song;
    CHECK (! refmatch::apply (song, s));
    CHECK (song.mixer[0].effects == full.mixer[0].effects);
    CHECK (! refmatch::apply (song, refmatch::Suggestion {})); // nothing measured: nothing applied
}

// --- Clean voice -------------------------------------------------------------------------------------------------------
TEST (bl190ch_clean_voice_chain_settings)
{
    using namespace lynx;
    namespace cv = cleanvoice;
    const auto plain = cv::chain();
    CHECK (plain.size() == 4);
    CHECK (plain[0].kind == EffectKind::facetEq && plain[1].kind == EffectKind::sentryGate && plain[2].kind == EffectKind::hushDeEsser
           && plain[3].kind == EffectKind::crestCompressor);
    cv::Options withNoise;
    withNoise.noiseReduction = true;
    const auto noisy = cv::chain (withNoise);
    CHECK (noisy.size() == 5 && noisy[0].kind == EffectKind::quellDenoiser);
    CHECK (std::abs (noisy[0].params[voicetools::quell::amount] - cv::noiseAmount) < 1.0e-6f);
    for (std::size_t i = 0; i < plain.size(); ++i)
        CHECK (noisy[i + 1] == plain[i]); // the same four behind it
    for (const auto& slot : noisy)
        CHECK (slot.presetName == cv::slotName && slot.enabled && slot.mix == 1.0f && cv::isCleanSlot (slot));

    // each setting read back through the effect's own law
    const auto& eq = plain[0].params;
    CHECK (facet::shape (eq[(std::size_t) facet::param (0, 0)]) == facet::Shape::lowCut);
    CHECK (std::abs (facet::hz (eq[(std::size_t) facet::param (0, 1)]) - cv::highPassHz) < 0.5);
    CHECK (facet::slopeDb (eq[(std::size_t) facet::param (0, 4)]) == cv::highPassSlope);
    for (int b = 1; b < facet::bands; ++b)
        CHECK (facet::shape (eq[(std::size_t) facet::param (b, 0)]) == facet::Shape::off);
    // an octave under the cut: about 12 dB down (Facet's low cut measures 14); the voice's range untouched
    CHECK (facet::responseDb (eq, bl190ch::rate, 40.0) < -10.0 && facet::responseDb (eq, bl190ch::rate, 40.0) > -16.0);
    CHECK (std::abs (facet::responseDb (eq, bl190ch::rate, 1000.0)) < 0.1 && facet::responseDb (eq, bl190ch::rate, 250.0) > -1.0);
    namespace s = dyn::sentry;
    const auto& gate = plain[1].params;
    CHECK (s::styleOf (gate[s::style]) == s::Style::clean);
    CHECK (std::abs (s::thresholdDb (gate[s::threshold]) - cv::expanderThresholdDb) < 0.01);
    CHECK (std::abs (s::ratioOf (gate[s::ratio]) - cv::expanderRatio) < 0.01);
    CHECK (std::abs (s::rangeDb (gate[s::range]) - cv::expanderRangeDb) < 0.01);
    CHECK (std::abs (s::attackMs (gate[s::attack]) - cv::expanderAttackMs) < 0.01 && std::abs (s::holdMs (gate[s::hold]) - cv::expanderHoldMs) < 0.05
           && std::abs (s::releaseMs (gate[s::release]) - cv::expanderReleaseMs) < 0.05);
    namespace h = mixsuite::hush;
    const auto& deEss = plain[2].params;
    CHECK (std::abs (h::hpHz (deEss[h::frequency], h::extended (deEss)) - cv::deEssHz) < 5.0);
    CHECK (std::abs (h::thresholdDb (deEss[h::threshold]) - cv::deEssThresholdDb) < 0.01 && std::abs (h::rangeDb (deEss[h::range]) - cv::deEssRangeDb) < 0.01);
    namespace c = dyn::crest;
    const auto& comp = plain[3].params;
    CHECK (c::styleOf (comp) == c::Style::clean);
    CHECK (std::abs (c::thresholdDb (comp[c::threshold]) - cv::compThresholdDb) < 0.01 && std::abs (c::ratioOf (comp[c::ratio]) - cv::compRatio) < 0.01);
    CHECK (std::abs (c::kneeDb (comp[c::knee]) - cv::compKneeDb) < 0.01 && std::abs (c::attackMs (comp[c::attack]) - cv::compAttackMs) < 0.05
           && std::abs (c::releaseMs (comp[c::release]) - cv::compReleaseMs) < 0.5);
    // gentle: a loud sine is turned down a little, a quiet one not at all
    CHECK (std::abs (c::trimDb (comp[c::output]) - cv::compMakeUpDb) < 0.01);
    if (! (c::staticGainDb (comp, -40.0) > -0.5 && c::staticGainDb (comp, -6.0) < -3.0 && c::staticGainDb (comp, -6.0) > -12.0))
        std::printf ("  compressor static gain: %.2f dB at -40, %.2f dB at -6\n", c::staticGainDb (comp, -40.0), c::staticGainDb (comp, -6.0));
    CHECK (c::staticGainDb (comp, -40.0) > -0.5);
    CHECK (c::staticGainDb (comp, -6.0) < -3.0 && c::staticGainDb (comp, -6.0) > -12.0);
}

TEST (bl190ch_clean_voice_goes_first_on_the_insert_and_comes_off_cleanly)
{
    using namespace lynx;
    namespace cv = cleanvoice;
    Song song;
    song.mixer = Song::defaultMixer (3);
    auto reverb = makeEffectSlot (EffectKind::reverb), delay = makeEffectSlot (EffectKind::delay);
    reverb.row = 2; // (FL's slot places: the user's effects sit in slots 3 and 6)
    delay.row = 5;
    song.mixer[2].effects = { reverb, delay };
    CHECK (! cv::present (song, 2) && cv::freeSlots (song, 2) == maxEffectSlots - 2);
    CHECK (cv::apply (song, 2));
    {
        const auto& fx = song.mixer[2].effects;
        CHECK (fx.size() == 6 && cv::slotsOf (song, 2) == std::vector<int> ({ 0, 1, 2, 3 }));
        const auto want = cv::chain();
        for (std::size_t i = 0; i < 4; ++i)
            CHECK (fx[i].kind == want[i].kind && fx[i].params == want[i].params);
        CHECK (fx[4].kind == EffectKind::reverb && fx[5].kind == EffectKind::delay); // the user's effects after it, in order
        const auto rows = mixerops::effectRows (fx);
        for (std::size_t i = 1; i < rows.size(); ++i)
            CHECK (rows[i] > rows[i - 1] && rows[i] < maxEffectSlots);
        CHECK (cv::present (song, 2) && ! cv::hasNoiseReduction (song, 2));
    }
    // pressing it again with noise reduction replaces the chain: nothing doubles
    cv::Options noise;
    noise.noiseReduction = true;
    CHECK (cv::apply (song, 2, noise));
    CHECK (song.mixer[2].effects.size() == 7 && song.mixer[2].effects[0].kind == EffectKind::quellDenoiser && cv::hasNoiseReduction (song, 2));
    CHECK (song.mixer[2].effects[5].kind == EffectKind::reverb && song.mixer[2].effects[6].kind == EffectKind::delay);
    // a slot edited by hand is still part of it (it is found by its name, not by its values)
    song.mixer[2].effects[1].params[(std::size_t) facet::param (0, 1)] = facet::hzValue (120.0);
    CHECK (cv::slotsOf (song, 2).size() == 5);
    cv::remove (song, 2);
    CHECK (song.mixer[2].effects.size() == 2 && song.mixer[2].effects[0].kind == EffectKind::reverb && song.mixer[2].effects[1].kind == EffectKind::delay);
    CHECK (! cv::present (song, 2));
    // other inserts were never touched; the Master and a missing insert refuse
    CHECK (song.mixer[1].effects.empty());
    const auto before = song;
    CHECK (! cv::apply (song, 0) && ! cv::apply (song, 99) && ! cv::apply (song, -1));
    CHECK (song.mixer[0].effects == before.mixer[0].effects);
    // no room: nothing changes
    while ((int) song.mixer[1].effects.size() < maxEffectSlots - 3)
        song.mixer[1].effects.push_back (reverb);
    const auto full = song.mixer[1].effects;
    CHECK (! cv::apply (song, 1));
    CHECK (song.mixer[1].effects == full);
    song.mixer[1].effects.pop_back();
    CHECK (cv::apply (song, 1) && (int) song.mixer[1].effects.size() == maxEffectSlots);
    CHECK (! cv::apply (song, 1, noise)); // one more slot than there is room for: the chain that is there stays
    CHECK ((int) song.mixer[1].effects.size() == maxEffectSlots && cv::slotsOf (song, 1).size() == 4);
}

TEST (bl190ch_clean_voice_cleans_a_noisy_take)
{
    using namespace lynx;
    const auto second = (std::size_t) bl190ch::rate;
    // a "take": 1 s of room noise, 2 s of voice (a 220 Hz tone with harmonics) over the noise, 2 s of noise again; and
    // rumble at 35 Hz all the way through
    auto make = [second] (std::vector<float>& l, std::vector<float>& r) {
        bl190ch::Lcg hiss (5u);
        l.assign (5 * second, 0.0f);
        for (auto& v : l)
            v = hiss.next() * 0.0016f; // about −60 dBFS RMS
        bl190ch::sine (l, 0, l.size(), 35.0, -40.0);
        for (int harmonic = 1; harmonic <= 6; ++harmonic)
            bl190ch::sine (l, second, 3 * second, 220.0 * harmonic, -14.0 - 4.0 * harmonic);
        r = l;
    };
    std::vector<float> dryL, dryR, wetL, wetR, nrL, nrR;
    make (dryL, dryR);
    wetL = dryL;
    wetR = dryR;
    bl190ch::run (cleanvoice::chain(), wetL, wetR);
    cleanvoice::Options noise;
    noise.noiseReduction = true;
    nrL = dryL;
    nrR = dryR;
    bl190ch::run (cleanvoice::chain (noise), nrL, nrR);
    for (const auto* out : { &wetL, &wetR, &nrL, &nrR })
        for (const auto v : *out)
            if (! std::isfinite (v) || std::abs (v) > 2.0f)
            {
                CHECK (std::isfinite (v) && std::abs (v) <= 2.0f);
                break;
            }
    // the pause after the voice (its last second, the expander closed): rumble and hiss are well down
    const auto pauseDry = bl190ch::rmsDb (dryL, 4 * second, 5 * second), pauseWet = bl190ch::rmsDb (wetL, 4 * second, 5 * second);
    if (! (pauseWet < pauseDry - 15.0))
        std::printf ("  pause dry %.1f wet %.1f | voice dry %.1f wet %.1f\n", pauseDry, pauseWet, bl190ch::rmsDb (dryL, second + second / 2, 3 * second - second / 2),
                 bl190ch::rmsDb (wetL, second + second / 2, 3 * second - second / 2));
    CHECK (pauseWet < pauseDry - 15.0);
    // the voice itself keeps its level within a few dB (gentle compression, no gating of the words)
    const auto voiceDry = bl190ch::rmsDb (dryL, second + second / 2, 3 * second - second / 2);
    const auto voiceWet = bl190ch::rmsDb (wetL, second + second / 2, 3 * second - second / 2);
    CHECK (std::abs (voiceWet - voiceDry) < 4.0);
    // so the voice stands further above the pause than before
    CHECK ((voiceWet - pauseWet) > (voiceDry - pauseDry) + 12.0);
    // with the noise-reduction slot the pause is at least as quiet, and the voice is still there
    const auto pauseNr = bl190ch::rmsDb (nrL, 4 * second, 5 * second), voiceNr = bl190ch::rmsDb (nrL, second + second / 2, 3 * second - second / 2);
    if (! (pauseNr <= pauseWet + 0.5 && std::abs (voiceNr - voiceDry) < 5.0))
        std::printf ("  with noise reduction: pause %.1f voice %.1f (plain chain: pause %.1f voice %.1f, dry voice %.1f)\n", pauseNr, voiceNr, pauseWet, voiceWet, voiceDry);
    CHECK (pauseNr <= pauseWet + 0.5);
    CHECK (std::abs (voiceNr - voiceDry) < 5.0);
}
