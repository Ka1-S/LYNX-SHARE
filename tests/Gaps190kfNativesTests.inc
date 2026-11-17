// Round gaps190kf (item 4, FL natives): FL Edison's Sample properties (Format › Edit properties..., F2) — the form's
// arithmetic (Edison_x64.dll 0x7b2910 / 0x7b2a80 / 0x7b30c0 / 0x7b3470 / 0x7b3240 / 0x7b2ed0), Accept (0x878c90) and the
// WAV chunks FL's writer 0x613e00 makes for them (smpl unity note and pitch fraction, inst, acid, LIST INFO) and its
// readers read back (0x6191d0 / 0x619290 / 0x619320 / 0x619d30). core/SampleProps.h, core/AudioEdit.h.

#include "core/AudioEdit.h"
#include "core/SampleProps.h"

namespace gkf_ae = lynx::audioedit;
namespace gkf_sp = lynx::sampleprops;

namespace gaps190kfnatives
{
namespace ae = lynx::audioedit;

inline ae::Document sine (double seconds, double rate, double hz, int channels = 1)
{
    ae::Document d;
    d.audio.sampleRate = rate;
    const auto n = (std::size_t) std::llround (seconds * rate);
    d.audio.channels.assign ((std::size_t) channels, std::vector<float> (n));
    for (std::size_t i = 0; i < n; ++i)
        for (auto& c : d.audio.channels)
            c[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / rate);
    return d;
}

inline const std::uint8_t* chunk (const std::vector<std::uint8_t>& w, const char* tag, std::uint32_t* len = nullptr)
{
    for (std::size_t p = 12; p + 8 <= w.size();)
    {
        const auto l = (std::uint32_t) (w[p + 4] | w[p + 5] << 8 | w[p + 6] << 16 | (std::uint32_t) w[p + 7] << 24);
        if (std::memcmp (w.data() + p, tag, 4) == 0)
        {
            if (len != nullptr)
                *len = l;
            return w.data() + p + 8;
        }
        p += 8 + l + (l & 1);
    }
    return nullptr;
}
inline std::uint32_t u32 (const std::uint8_t* p) { return (std::uint32_t) (p[0] | p[1] << 8 | p[2] << 16 | (std::uint32_t) p[3] << 24); }
} // namespace gaps190kfnatives

TEST (gaps190kf_props_tempo_length_link)
{
    // FL 0x62c120: beats = frames · tempo / (rate · 60); tempo back from beats; no frames → 0
    CHECK (std::abs (gkf_sp::beatsOf (120.0, 88200.0, 44100.0) - 4.0) < 1e-12);
    CHECK (std::abs (gkf_sp::tempoOf (4.0, 88200.0, 44100.0) - 120.0) < 1e-12);
    CHECK (gkf_sp::tempoOf (4.0, 0.0, 44100.0) == 0.0);
    CHECK (gkf_sp::clampTypedTempo (-5.0) == 0.0 && gkf_sp::clampTypedTempo (2.0e5) == 100000.0 && gkf_sp::clampTypedTempo (97.5) == 97.5);
    CHECK (gkf_sp::acceptedTempo (1000.0) == 960.0 && gkf_sp::acceptedTempo (-1.0) == 0.0);
    CHECK (gkf_sp::acceptedRate (0.5) == 1.0 && gkf_sp::acceptedRate (2.0e8) == 1.0e8 && gkf_sp::acceptedRate (48000.0) == 48000.0);
}

TEST (gaps190kf_props_quick_estimation)
{
    // FL 0x7b30c0 at project tempo 120 (one bar = 2 s): a 2-bar loop of 4 s → 8 beats at 120 BPM
    double tempo = 0.0;
    CHECK (gkf_sp::quickBeats (4.0 * 44100.0, 44100.0, 120.0, tempo) == 8 && std::abs (tempo - 120.0) < 1e-9);
    // 2.1 s (1.05 bars) → 4 beats, tempo 114.28…
    CHECK (gkf_sp::quickBeats (2.1 * 44100.0, 44100.0, 120.0, tempo) == 4 && std::abs (tempo - 4.0 * 60.0 / 2.1) < 1e-9);
    // under 0.75 bar: 2 beats (1 s → 120 BPM)
    CHECK (gkf_sp::quickBeats (1.0 * 44100.0, 44100.0, 120.0, tempo) == 2 && std::abs (tempo - 120.0) < 1e-9);
    // a tempo outside 30…240 gives none: 0.3 s → 2 beats = 400 BPM
    CHECK (gkf_sp::quickBeats (0.3 * 44100.0, 44100.0, 120.0, tempo) == 0);
    // capped at 256 beats (a long song: 300 s = 150 bars → 600 → 256 → tempo 51.2)
    CHECK (gkf_sp::quickBeats (300.0 * 44100.0, 44100.0, 120.0, tempo) == 256 && std::abs (tempo - 51.2) < 1e-9);
    // no selection length → 4 beats
    CHECK (gkf_sp::quickBeats (0.0, 44100.0, 120.0, tempo) == 0); // (its tempo is 0: out of range)
}

TEST (gaps190kf_props_limit_round_pitch)
{
    // FL 0x7b3470: into [low, 2·low] by halving / doubling; ≤ 1 untouched
    CHECK (gkf_sp::limitTempo (280.0, 50) == 70.0);
    CHECK (gkf_sp::limitTempo (45.0, 100) == 180.0);
    CHECK (gkf_sp::limitTempo (140.0, 100) == 140.0);
    CHECK (gkf_sp::limitTempo (200.0, 100) == 200.0);
    CHECK (gkf_sp::limitTempo (0.5, 100) == 0.5);
    // FL 0x7b3240: the fine tune's rounded semitones into the middle note
    int middle = 60;
    double fine = 151.0;
    gkf_sp::roundPitch (middle, fine);
    CHECK (middle == 62 && fine == 0.0);
    middle = 1, fine = -250.0; // (−2.5 → −2 with banker's rounding)
    gkf_sp::roundPitch (middle, fine);
    CHECK (middle == 0);
    middle = 126, fine = 400.0;
    gkf_sp::roundPitch (middle, fine);
    CHECK (middle == 127);
    // FL 0x7b2ed0: 69.37 st → A5 (69) + 37 cents; none below 0
    int m = 0, c = 0;
    CHECK (gkf_sp::fromDetectedPitch (69.37, m, c) && m == 69 && c == 37);
    CHECK (! gkf_sp::fromDetectedPitch (-1.0, m, c));
    CHECK (gkf_sp::noteName (60) == "C5" && gkf_sp::noteName (69) == "A5" && gkf_sp::noteName (61) == "C#5");
}

TEST (gaps190kf_props_wav_chunks_fl_layout)
{
    using namespace gaps190kfnatives;
    auto d = sine (2.0, 44100.0, 440.0);
    // without properties the file is the same as before (no new chunk)
    const auto plain = gkf_ae::encodeWav (d, gkf_ae::WavFormat::int16);
    CHECK (chunk (plain, "smpl") == nullptr && chunk (plain, "inst") == nullptr && chunk (plain, "acid") == nullptr && chunk (plain, "LIST") == nullptr);

    d.props.name = "Lead vox";
    d.props.comments = "take 3";
    d.props.middleNote = 69;
    d.props.fineTune = -30.0;
    d.props.lowNote = 40;
    d.props.highNote = 90;
    d.props.tempo = 120.0;
    d.props.tempoSync = true;
    const auto w = gkf_ae::encodeWav (d, gkf_ae::WavFormat::int16);
    std::uint32_t len = 0;
    // smpl (FL: unity = middle − floor (fine / 100), fraction = the rest × 2^32 / 100), no loop: 36 bytes
    const auto* smpl = chunk (w, "smpl", &len);
    CHECK (smpl != nullptr && len == 36);
    if (smpl != nullptr)
    {
        CHECK (u32 (smpl + 8) == 22675);  // 1e9 / 44100
        CHECK (u32 (smpl + 12) == 70);    // 69 − (−1)
        CHECK (u32 (smpl + 16) == (std::uint32_t) std::nearbyint (70.0 * 42949672.96));
        CHECK (u32 (smpl + 28) == 0);     // loops
    }
    // inst: unshifted note 69 − round (−0.3) = 69, fine −30, gain 0, 40 / 90, velocities 0 / 127
    const auto* inst = chunk (w, "inst", &len);
    CHECK (inst != nullptr && len == 7);
    if (inst != nullptr)
        CHECK (inst[0] == 69 && (std::int8_t) inst[1] == -30 && inst[2] == 0 && inst[3] == 40 && inst[4] == 90 && inst[5] == 0 && inst[6] == 127);
    // acid: whole beats (4) at 120 → flags 0 (tempo-sync, no stretch flag), root 60 + 0x80, beats 4, 4/4, 120.0f
    const auto* acid = chunk (w, "acid", &len);
    CHECK (acid != nullptr && len == 24);
    if (acid != nullptr)
    {
        float bpm;
        std::memcpy (&bpm, acid + 20, 4);
        CHECK (u32 (acid) == 0 && u32 (acid + 4) == 0x80003c && u32 (acid + 12) == 4 && u32 (acid + 16) == 0x40004 && bpm == 120.0f);
    }
    // LIST INFO: INAM then ICMT, zero-terminated
    const auto* info = chunk (w, "LIST", &len);
    CHECK (info != nullptr && std::memcmp (info, "INFO", 4) == 0 && std::memcmp (info + 4, "INAM", 4) == 0 && u32 (info + 8) == 9
           && std::memcmp (info + 12, "Lead vox\0", 9) == 0);

    // FL's readers give FL's values back: the middle note is smpl's unity note, the fine tune its fraction (as FL)
    gkf_ae::Document back;
    CHECK (gkf_ae::decodeWav (w.data(), w.size(), back));
    CHECK (back.props.name == "Lead vox" && back.props.comments == "take 3");
    CHECK (back.props.middleNote == 70 && std::abs (back.props.fineTune - 70.0) < 1e-6);
    CHECK (back.props.lowNote == 40 && back.props.highNote == 90 && back.props.highVelocity == 127);
    CHECK (std::abs (back.props.tempo - 120.0) < 1e-9 && back.props.tempoSync);
}

TEST (gaps190kf_props_acid_flags)
{
    using namespace gaps190kfnatives;
    // 2.1 s at 120: beats round to 4, which is not the length → flag 4; not tempo-sync → one-shot (1)
    auto d = sine (2.1, 44100.0, 220.0);
    d.props.tempo = 120.0;
    auto a = gkf_ae::acidOf (d.props, d.audio.frames(), 44100.0);
    CHECK (u32 (a.data()) == 5 && u32 (a.data() + 12) == 4);
    // a sample with no tempo but tempo-sync: FL writes tempo 0 and counts the beats at 120; length not whole → no flag 4
    d.props.tempo = 0.0;
    d.props.tempoSync = true;
    a = gkf_ae::acidOf (d.props, d.audio.frames(), 44100.0);
    float bpm;
    std::memcpy (&bpm, a.data() + 20, 4);
    CHECK (u32 (a.data()) == 0 && bpm == 0.0f);
    // reading an acid chunk without flag 4 and no one-shot: the tempo from its beats and the length (FL 0x62c560)
    const auto w = gkf_ae::encodeWav (d, gkf_ae::WavFormat::int16);
    gkf_ae::Document back;
    CHECK (gkf_ae::decodeWav (w.data(), w.size(), back) && std::abs (back.props.tempo - 4.0 * 60.0 / 2.1) < 1e-6 && back.props.tempoSync);
    // a loop of the file keeps its smpl loop next to the pitch (60 / 0 without pitch info, as before)
    auto l = sine (1.0, 44100.0, 220.0);
    l.loopStart = 100;
    l.loopEnd = 4000;
    const auto lw = gkf_ae::encodeWav (l, gkf_ae::WavFormat::int16);
    std::uint32_t len = 0;
    const auto* smpl = chunk (lw, "smpl", &len);
    CHECK (smpl != nullptr && len == 60 && u32 (smpl + 12) == 60 && u32 (smpl + 16) == 0 && u32 (smpl + 28) == 1 && u32 (smpl + 44) == 100 && u32 (smpl + 48) == 3999);
}

TEST (gaps190kf_props_accept)
{
    using namespace gaps190kfnatives;
    auto d = sine (1.0, 44100.0, 440.0, 2);
    d.audio.channels[1].assign (d.audio.channels[1].size(), 0.0f);
    auto f = gkf_sp::formOf (d, "loop");
    CHECK (f.channels == gkf_sp::Channels::stereo && f.sampleRate == 44100.0 && f.middleNote == 60 && ! f.int16);
    // merged mono = (L + R) / 2; the rate changed without Resample keeps the frames
    f.channels = gkf_sp::Channels::monoMerged;
    f.sampleRate = 48000.0;
    f.tempo = 1200.0;
    f.middleNote = 200;
    CHECK (gkf_sp::accept (d, f));
    CHECK (d.audio.numChannels() == 1 && d.audio.frames() == 44100 && d.audio.sampleRate == 48000.0);
    CHECK (std::abs (d.audio.channels[0][100] - 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * 100.0 / 44100.0)) < 1e-6f);
    CHECK (d.props.tempo == 960.0 && d.props.middleNote == 127);
    // with Resample the length follows the rate
    f = gkf_sp::formOf (d, "loop");
    f.sampleRate = 24000.0;
    f.resample = true;
    CHECK (gkf_sp::accept (d, f) && d.audio.sampleRate == 24000.0 && d.audio.frames() == 22050);
    // 16-bit: a peak over 0 dB brought to 0 dB first, then every sample on the 16-bit grid
    auto h = sine (0.1, 44100.0, 1000.0);
    for (auto& v : h.audio.channels[0])
        v *= 4.0f; // peak 2.0
    gkf_sp::toInt16 (h);
    float peak = 0.0f;
    bool onGrid = true;
    for (const auto v : h.audio.channels[0])
    {
        peak = std::max (peak, std::abs (v));
        const auto q = (double) v * 32767.0;
        onGrid = onGrid && std::abs (q - std::nearbyint (q)) < 1e-3;
    }
    CHECK (peak <= 1.0f && peak > 0.999f && onGrid && h.props.int16);
    // a mono sample made stereo doubles it; the Mono items leave a mono sample as it is
    auto m = sine (0.1, 44100.0, 300.0);
    gkf_sp::setChannels (m, gkf_sp::Channels::monoLeft);
    CHECK (m.audio.numChannels() == 1);
    gkf_sp::setChannels (m, gkf_sp::Channels::stereo);
    CHECK (m.audio.numChannels() == 2 && m.audio.channels[0] == m.audio.channels[1]);
}

TEST (gaps190kf_clickfree_fade_and_delete)
{
    using namespace gaps190kfnatives;
    // FL's fade type 4: (1 − cos (π x)) / 2 with x = (i − first + 1) / (n + 1)
    std::vector<std::vector<float>> ones (1, std::vector<float> (5, 1.0f));
    gkf_ae::clickfree::raisedCosineFade (ones, 0, 2, 0.0f, 1.0f);
    CHECK (std::abs (ones[0][0] - 0.1464466f) < 1e-6f && std::abs (ones[0][1] - 0.5f) < 1e-6f && std::abs (ones[0][2] - 0.8535534f) < 1e-6f && ones[0][3] == 1.0f);

    // Delete in the middle: 24 ms (1058 frames at 44.1 kHz) after the seam = the deleted part's head fading out over the
    // following audio fading in; nothing else moves
    gkf_ae::Document d;
    d.audio.sampleRate = 44100.0;
    d.audio.channels.assign (1, std::vector<float> (20000));
    for (std::size_t i = 0; i < 20000; ++i)
        d.audio.channels[0][i] = (float) (i % 100) / 100.0f; // a ramp, so every frame says where it came from
    const auto orig = d.audio.channels[0];
    auto plain = d;
    gkf_ae::erase (plain, { 5000, 8000 });
    gkf_ae::clickfree::eraseClickFree (d, { 5000, 8000 });
    CHECK (d.audio.frames() == 17000);
    const auto n = (std::int64_t) std::nearbyint (24.0 * 44100.0 * 0.001);
    CHECK (n == 1058);
    bool before = true, seam = true, after = true;
    for (std::int64_t i = 0; i < 5000; ++i)
        before = before && d.audio.channels[0][(std::size_t) i] == orig[(std::size_t) i];
    for (std::int64_t k = 0; k < n; ++k)
    {
        const auto x = (double) (k + 1) / (double) (n + 1);
        const auto g = (std::cos ((1.0 - x) * 3.141592653589793) + 1.0) * 0.5;
        const auto fadeOut = (float) ((double) orig[(std::size_t) (5000 + k)] * (g * (0.0 - 1.0) + 1.0));
        const auto fadeIn = (float) ((double) plain.audio.channels[0][(std::size_t) (5000 + k)] * g);
        seam = seam && std::abs (d.audio.channels[0][(std::size_t) (5000 + k)] - (fadeOut + fadeIn)) < 1e-6f;
    }
    for (std::int64_t i = 5000 + n; i < 17000; ++i)
        after = after && d.audio.channels[0][(std::size_t) i] == plain.audio.channels[0][(std::size_t) i];
    CHECK (before && seam && after);

    // a short range: the overlap is the range's length (0x800)
    auto s = sine (1.0, 44100.0, 300.0);
    const auto s0 = s.audio.channels[0];
    gkf_ae::clickfree::eraseClickFree (s, { 1000, 1100 });
    CHECK (s.audio.frames() == 44000 && s.audio.channels[0][1200] == s0[1300] && s.audio.channels[0][1050] != s0[1150]);

    // from the file's start: the new start fades in; to its end: the new end fades out
    auto a = sine (1.0, 44100.0, 300.0);
    const auto a0 = a.audio.channels[0];
    gkf_ae::clickfree::eraseClickFree (a, { 0, 4410 });
    CHECK (std::abs (a.audio.channels[0][0]) < std::abs (a0[4410]) + 1e-9f && a.audio.channels[0][2000] == a0[6410]);
    auto b = sine (1.0, 44100.0, 300.0);
    const auto b0 = b.audio.channels[0];
    gkf_ae::clickfree::eraseClickFree (b, { 40000, 44100 });
    CHECK (b.audio.frames() == 40000 && b.audio.channels[0][38000] == b0[38000]);
    const auto x = (double) n / (double) (n + 1); // (the fade's last frame)
    CHECK (std::abs (b.audio.channels[0][39999] - (float) ((double) b0[39999] * (1.0 - (std::cos ((1.0 - x) * 3.141592653589793) + 1.0) * 0.5))) < 1e-6f);
}

TEST (gaps190kf_clickfree_paste_and_clear)
{
    using namespace gaps190kfnatives;
    // Paste insert: the audio after the point fades out over the pasted head; the audio before it fades in over the
    // pasted tail (each seam overlaps what was its neighbour)
    auto d = sine (1.0, 44100.0, 200.0);
    const auto d0 = d.audio.channels[0];
    gkf_ae::Audio clip;
    clip.sampleRate = 44100.0;
    clip.channels.assign (1, std::vector<float> (4000, 0.0f));
    gkf_ae::clickfree::insertClickFree (d, 10000, clip);
    CHECK (d.audio.frames() == 48100);
    const auto n = (std::int64_t) 1058;
    // inside the silent clip, frame 10000 + k = d0[10000 + k] faded out (k < n); its tail = d0[10000 − n + j] faded in
    const auto g = [n] (std::int64_t k) { return (std::cos ((1.0 - (double) (k + 1) / (double) (n + 1)) * 3.141592653589793) + 1.0) * 0.5; };
    CHECK (std::abs (d.audio.channels[0][10000] - (float) ((double) d0[10000] * (1.0 - g (0)))) < 1e-6f);
    CHECK (std::abs (d.audio.channels[0][14000 - 1] - (float) ((double) d0[9999] * g (n - 1))) < 1e-6f);
    CHECK (d.audio.channels[0][12000] == 0.0f && d.audio.channels[0][14000] == d0[10000] && d.audio.channels[0][9999] == d0[9999]);
    // Clear: the cleared range starts with the old audio fading out and ends with it fading in
    auto c = sine (1.0, 44100.0, 200.0);
    const auto c0 = c.audio.channels[0];
    gkf_ae::clickfree::clearClickFree (c, { 10000, 20000 });
    CHECK (c.audio.frames() == 44100 && c.audio.channels[0][15000] == 0.0f);
    CHECK (std::abs (c.audio.channels[0][10000] - (float) ((double) c0[10000] * (1.0 - g (0)))) < 1e-6f);
    CHECK (std::abs (c.audio.channels[0][19999] - (float) ((double) c0[19999] * g (n - 1))) < 1e-6f);
    // Trim: the kept part's ends fade (in at the start, out at the end)
    auto t = sine (1.0, 44100.0, 200.0);
    const auto t0 = t.audio.channels[0];
    gkf_ae::clickfree::trimClickFree (t, { 1000, 30000 });
    CHECK (t.audio.frames() == 29000 && t.audio.channels[0][5000] == t0[6000]);
    CHECK (std::abs (t.audio.channels[0][0] - (float) ((double) t0[1000] * g (0))) < 1e-6f);
}

// Bit-identity for files and projects saved before this round: a document without sample properties writes the same
// bytes as before (smpl only with a loop, unity note 60, fraction 0, one loop; no inst / acid / LIST), and decode → encode
// of such a file gives the same bytes back (the readers find no property, so nothing new is written)
TEST (gaps190kf_props_old_files_stay_byte_identical)
{
    using namespace gaps190kfnatives;
    auto d = sine (1.5, 48000.0, 330.0, 2);
    d.loopStart = 4800;
    d.loopEnd = 52800;
    for (const auto format : { gkf_ae::WavFormat::int16, gkf_ae::WavFormat::float32 })
    {
        const auto w = gkf_ae::encodeWav (d, format);
        std::uint32_t len = 0;
        const auto* smpl = chunk (w, "smpl", &len);
        CHECK (smpl != nullptr && len == 60);
        if (smpl != nullptr)
            CHECK (u32 (smpl + 12) == 60 && u32 (smpl + 16) == 0 && u32 (smpl + 28) == 1 && u32 (smpl + 44) == 4800 && u32 (smpl + 48) == 52799);
        CHECK (chunk (w, "inst") == nullptr && chunk (w, "acid") == nullptr && chunk (w, "LIST") == nullptr);
        gkf_ae::Document back;
        CHECK (gkf_ae::decodeWav (w.data(), w.size(), back));
        CHECK (! back.props.hasPitchInfo() && ! back.props.hasTempoInfo() && back.props.name.empty() && back.props.comments.empty());
        CHECK (back.props.int16 == (format == gkf_ae::WavFormat::int16)); // (FL's Precision as the file's)
        CHECK (gkf_ae::encodeWav (back, format) == w);
    }
    // the plain edits (click-free off) are untouched by the click-free ones: a plain delete is the exact splice
    auto plain = sine (1.0, 44100.0, 440.0);
    const auto orig = plain.audio.channels[0];
    gkf_ae::erase (plain, { 1000, 3000 });
    bool exact = plain.audio.frames() == (std::int64_t) orig.size() - 2000;
    for (std::size_t i = 0; exact && i < plain.audio.channels[0].size(); ++i)
        exact = plain.audio.channels[0][i] == orig[i < 1000 ? i : i + 2000];
    CHECK (exact);
}

