// Round gaps190kf: the remaining 0.1.30 coverage gaps (/mnt/e/lynx-work/gaps190kf/HANDOFF.md).
// Item 1b (detail-072): an audio clip's Transpose reaches Live's ±48 st in the real-time warp modes (Complex, Tones,
// Beats in the Clip View), where the voice shifter used to stop at ±24. Uses round 190ik's song / render helpers.

#include "core/ClipBrace.h"

namespace gaps190kf
{
constexpr double twoPi = 2.0 * 3.141592653589793;
inline std::shared_ptr<lynx::SampleData> tone440()
{
    auto tone = std::make_shared<lynx::SampleData>();
    tone->sampleRate = 44100.0;
    for (int i = 0; i < 44100 * 4; ++i)
        tone->left.push_back ((float) (0.4 * std::sin (twoPi * 440.0 * i / 44100.0)));
    tone->right = tone->left;
    return tone;
}
// the 8-beat warped clip of rampSong (fixed 120 BPM) with its own transposition
inline lynx::Song transposedSong (float semitones, bool keepPitch = true, lynx::StretchMethod method = lynx::StretchMethod::complex)
{
    auto song = warp190ik::rampSong (8.0, keepPitch, false);
    song.channels.back().sampler.stretchMethod = method;
    for (auto& c : song.playlist)
        if (c.kind == lynx::ClipKind::audio)
            c.pitch = semitones;
    return song;
}
} // namespace gaps190kf

TEST (gaps190kf_clip_transpose_reaches_plus_minus_48_semitones_in_real_time)
{
    using namespace lynx;
    using namespace gaps190kf;
    const auto tone = tone440();
    for (const auto method : { StretchMethod::complex, StretchMethod::tones })
        for (const auto st : { -48.0f, -36.0f, -25.0f, 25.0f, 36.0f, 48.0f })
        {
            const auto r = warp190ik::render (transposedSong (st, true, method), tone, 3.0);
            const auto want = 440.0 * std::exp2 (st / 12.0);
            const auto got = warp190ik::peakFrequency (r.left, 44100, 44100 + 16384, 44100.0, want * 0.97, want * 1.03);
            const auto off = warp190ik::cents (got, want);
            CHECK (std::abs (off) < 15.0);
            if (std::abs (off) >= 15.0)
                std::printf ("  gaps190kf: method %d %+.0f st: %.2f Hz (want %.2f, %+.1f c)\n", (int) method, (double) st, got, want, off);
            // the clip keeps its length (its 8 beats at 120 BPM = 4 s): sound at 3 s, as untransposed
            double e = 0.0;
            for (long i = 44100 * 2; i < 44100 * 2 + 4410; ++i)
                e += (double) r.left[(std::size_t) i] * r.left[(std::size_t) i];
            CHECK (e > 1.0);
        }
}

TEST (gaps190kf_clip_transpose_within_24_keeps_the_old_shifter_path)
{
    using namespace lynx;
    using namespace gaps190kf;
    const auto tone = tone440();
    // ±24 is reached exactly as before (the shifter's ±2 octaves); 25 goes one semitone further now (it stopped at 24)
    const auto at24 = warp190ik::render (transposedSong (24.0f), tone, 2.0);
    const auto at25 = warp190ik::render (transposedSong (25.0f), tone, 2.0);
    CHECK (std::abs (warp190ik::cents (warp190ik::peakFrequency (at24.left, 44100, 44100 + 16384, 44100.0, 1700.0, 1820.0), 1760.0)) < 15.0);
    CHECK (std::abs (warp190ik::cents (warp190ik::peakFrequency (at25.left, 44100, 44100 + 16384, 44100.0, 1810.0, 1920.0), 1864.66)) < 15.0);
    // a channel transposed by its own pitch beyond ±24 (not a clip's Live Transpose) keeps the ±24 clamp: old projects
    // play as they did
    auto song = transposedSong (12.0f);
    song.channels.back().sampler.pitch = 20.0f;
    const auto both = warp190ik::render (song, tone, 2.0);
    auto clamped = transposedSong (0.0f);
    clamped.channels.back().sampler.pitch = 24.0f;
    const auto ref = warp190ik::render (clamped, tone, 2.0);
    CHECK (both.left == ref.left);
}

// Item 1a (tempo_warp-0062, tech-0001): the Clip View's warp time line — Live's loop brace and Start / End markers dragged
// in content ticks (core/ClipBrace.h)
TEST (gaps190kf_clip_brace_marks_follow_loop_on_and_off)
{
    using namespace lynx;
    Clip c;
    c.kind = ClipKind::audio;
    c.offset = 2 * ppq;
    c.length = 8 * ppq;
    ClipGridSlot slot;
    slot.loop = true;
    slot.startMarker = ppq;
    auto m = clipbrace::of (c, slot);
    CHECK (m.loop && m.loopStart == 2 * ppq && m.loopEnd == 10 * ppq && m.start == 3 * ppq && m.end == 10 * ppq);
    slot.loop = false; // (Loop off: the brace spans Start…End, the Start marker on the clip's start)
    m = clipbrace::of (c, slot);
    CHECK (! m.loop && m.loopStart == 2 * ppq && m.start == 2 * ppq && m.end == 10 * ppq);
}

TEST (gaps190kf_clip_brace_drags_as_live)
{
    using namespace lynx;
    Clip c;
    c.kind = ClipKind::audio;
    c.offset = 4 * ppq;
    c.length = 8 * ppq;
    c.fadeIn = c.fadeOut = 4 * ppq;
    ClipGridSlot slot;
    slot.loop = true;
    slot.startMarker = 2 * ppq; // (the Start marker at beat 6)
    // the loop's start moves later; the Start marker stays at beat 6 (still inside the loop)
    CHECK (clipbrace::move (c, slot, clipbrace::Part::loopStart, 5 * ppq, ppq));
    CHECK (c.offset == 5 * ppq && c.length == 7 * ppq && slot.startMarker == ppq);
    CHECK (c.fadeIn + c.fadeOut <= c.length);
    // past the Start marker: it goes back to the loop's start (LYNX keeps it inside the loop)
    CHECK (clipbrace::move (c, slot, clipbrace::Part::loopStart, 7 * ppq, ppq));
    CHECK (c.offset == 7 * ppq && slot.startMarker == 0);
    // the loop's end, never shorter than the minimum
    CHECK (clipbrace::move (c, slot, clipbrace::Part::loopEnd, 7 * ppq + 10, ppq));
    CHECK (c.length == ppq);
    CHECK (clipbrace::move (c, slot, clipbrace::Part::loopEnd, 15 * ppq, ppq));
    CHECK (c.length == 8 * ppq);
    // the body: moved, its length kept; the Start marker placed inside the loop keeps its place when it can
    CHECK (clipbrace::move (c, slot, clipbrace::Part::start, 9 * ppq, ppq));
    CHECK (slot.startMarker == 2 * ppq);
    CHECK (clipbrace::move (c, slot, clipbrace::Part::loopBody, 6 * ppq, ppq));
    CHECK (c.offset == 6 * ppq && c.length == 8 * ppq && slot.startMarker == 3 * ppq);
    CHECK (! clipbrace::move (c, slot, clipbrace::Part::loopBody, 6 * ppq, ppq)); // (no change)
    CHECK (clipbrace::move (c, slot, clipbrace::Part::loopBody, -3 * ppq, ppq));
    CHECK (c.offset == 0);
    // keys: a grid step, then its length
    CHECK (clipbrace::nudge (c, slot, ppq / 4, 1, false) && c.offset == ppq / 4);
    CHECK (clipbrace::nudge (c, slot, ppq / 4, 1, true) && c.offset == ppq / 4 + 8 * ppq);
    CHECK (clipbrace::nudge (c, slot, ppq / 4, -1, true) && c.offset == ppq / 4);
    // Loop off: Start and End markers are the clip's own
    slot.loop = false;
    CHECK (clipbrace::move (c, slot, clipbrace::Part::start, 2 * ppq, ppq));
    CHECK (c.offset == 2 * ppq && c.length == ppq / 4 + 6 * ppq);
    CHECK (clipbrace::move (c, slot, clipbrace::Part::end, 12 * ppq, ppq));
    CHECK (c.offset == 2 * ppq && c.length == 10 * ppq);
    CHECK (clipbrace::move (c, slot, clipbrace::Part::start, 20 * ppq, ppq)); // (never past the End marker)
    CHECK (c.length == ppq && c.offset == 11 * ppq);
    CHECK (clipbrace::snap (1000.4, 240) == 960 && clipbrace::snap (1100.0, 240) == 1200 && clipbrace::snap (1100.4, 0) == 1100);
}

// CPU of the wider shifter range (report only; the perf gate reads the numbers from the log): a 3 s render of the 8-beat
// clip at ±24 (unchanged path) and at ±36 / ±48 (four-part blocks below −24, a longer prime)
TEST (gaps190kf_clip_transpose_cpu_report)
{
    using namespace gaps190kf;
    const auto tone = tone440();
    for (const auto st : { 0.0f, 24.0f, -24.0f, 36.0f, -36.0f, 48.0f, -48.0f })
    {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = warp190ik::render (transposedSong (st), tone, 3.0);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  gaps190kf cpu: clip transpose %+.0f st: %.1f ms for 3 s (RTF %.4f)\n", (double) st, ms, ms / 3000.0);
        CHECK (! r.left.empty());
    }
}
