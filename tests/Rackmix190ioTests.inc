// Round 190io (design190ia slices 6 and 11, the Mixer details, issue #154 part 2): FL's Channel rack and Mixer landmarks.
// The paint is checked by --rackmixshot190io / the --snapshot pairs; these are the parts without JUCE.

#include "core/FlpExport.h"
#include "core/MiddleEllipsis.h"
#include "core/MixerRouteArrow.h"
#include "core/StripInserts.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>

namespace r190io
{
constexpr double rate = 48000.0;

// a long sine (0.25, 440 Hz) the audio clip channel plays
std::shared_ptr<lynx::SampleData> sine (double seconds)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < d->left.size(); ++i)
        d->left[i] = 0.25f * (float) std::sin (6.283185307179586 * 440.0 * (double) i / rate);
    d->right = d->left;
    return d;
}

// Song mode, 120 BPM: channel 0 = an audio clip channel (or a Sampler) on the sine, one step at the pattern's start,
// the pattern once on the Playlist at bar 1
lynx::Song stepSong (lynx::InstrumentKind kind, const std::string& path)
{
    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    lynx::ChannelDef c;
    c.instrument = kind;
    c.name = kind == lynx::InstrumentKind::audioClip ? "Sine clip" : "Sine sampler";
    c.samplePath = path;
    c.volume = lynx::audioClipUnityVolume;
    s.channels = { c };
    lynx::Pattern p;
    p.name = "Steps";
    p.toggleStep (0, 0);
    s.patterns = { p };
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::pattern;
    clip.pattern = 0;
    clip.length = 4 * lynx::ppq;
    s.playlist = { clip };
    return s;
}

// how long the render is above -60 dBFS, in seconds (10 ms windows)
double soundingSeconds (const lynx::Song& song, std::shared_ptr<lynx::SampleData> data, double seconds)
{
    auto state = lynx::RackState::build (song, nullptr, rate, [data] (const std::string&) -> std::shared_ptr<const lynx::SampleData> { return data; });
    lynx::RackRenderer rr;
    rr.prepare (rate, 480);
    const auto total = (int) (seconds * rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    double tick = 0.0;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    for (int at = 0; at < total; at += 480)
    {
        const auto n = std::min (480, total - at);
        rr.process (state.get(), true, tick, n, l.data() + at, r.data() + at);
        tick += n * tps;
    }
    auto last = 0;
    for (int w = 0; w + 480 <= total; w += 480)
    {
        float peak = 0.0f;
        for (int i = w; i < w + 480; ++i)
            peak = std::max (peak, std::abs (l[(std::size_t) i]));
        if (peak > 0.001f)
            last = w + 480;
    }
    return (double) last / rate;
}

void writeWav16 (const std::string& path, const lynx::SampleData& d)
{
    std::ofstream f (path, std::ios::binary);
    const auto frames = (std::uint32_t) d.left.size();
    const auto put32 = [&f] (std::uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    const auto put16 = [&f] (std::uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
    f.write ("RIFF", 4); put32 (36 + frames * 4); f.write ("WAVEfmt ", 8); put32 (16); put16 (1); put16 (2);
    put32 ((std::uint32_t) rate); put32 ((std::uint32_t) rate * 4); put16 (4); put16 (16); f.write ("data", 4); put32 (frames * 4);
    for (std::uint32_t i = 0; i < frames; ++i)
        for (auto v : { d.left[i], d.right[i] })
            put16 ((std::uint16_t) (std::int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f));
}
} // namespace r190io

// FM-14: FL's middle ellipsis keeps the start and the end ("Titey..m #2"); the two parts take about half the room each
TEST (rackmix190io_middle_ellipsis)
{
    using namespace lynx;
    const auto fixed = [] (int, int count) { return (float) count * 7.0f; }; // 7 px per character
    // fits: unchanged
    CHECK ((middleellipsis::split (12, 84.0f, 8.0f, fixed) == std::pair<int, int> { 12, 0 }));
    // "Titey Tom #2" (12 characters, 84 px) in 70 px: 62 px for the parts, 4 at the start (28), the end the rest (4)
    const auto [head, tail] = middleellipsis::split (12, 70.0f, 8.0f, fixed);
    CHECK (head == 4 && tail == 4);
    CHECK ((float) (head + tail) * 7.0f + 8.0f <= 70.0f);
    CHECK (head + tail < 12);
    // every width: the result fits, the end is never shorter than the start, at least one character is dropped
    for (int w = 30; w < 84; ++w)
    {
        const auto [h, t] = middleellipsis::split (12, (float) w, 8.0f, fixed);
        CHECK (h > 0 && t >= h);
        CHECK ((float) (h + t) * 7.0f + 8.0f <= (float) w);
        CHECK (h + t <= 11);
    }
    // no room: nothing
    CHECK ((middleellipsis::split (12, 12.0f, 8.0f, fixed) == std::pair<int, int> { 0, 0 }));
    // a wide start and narrow end letters (by width, not by letters): "WWWW iiii"
    const auto mixed = [] (int first, int count) {
        float w = 0.0f;
        for (int i = first; i < first + count; ++i)
            w += i < 4 ? 12.0f : 3.0f;
        return w;
    };
    const auto [h2, t2] = middleellipsis::split (9, 40.0f, 6.0f, mixed);
    CHECK (h2 == 1 && t2 >= 3); // one W (12 px of 17) then the narrow end
}

// FM-12: an audio clip channel plays from its steps, as FL's manual says ("If desired, Audio Clips can be triggered from
// the Step Sequencer or the Piano roll"); FL's own render of the same project settles how long (env LYNX_190IO_FLDIR
// writes the .flp and the sample for FL's command-line render, round 190io)
TEST (rackmix190io_audio_clip_channel_plays_from_a_step)
{
    using namespace lynx;
    const auto data = r190io::sine (2.0);
    const auto clipSong = r190io::stepSong (InstrumentKind::audioClip, "sine");
    const auto samplerSong = r190io::stepSong (InstrumentKind::sampler, "sine");
    // (round 190jd: a window shorter than the 2 s pattern loop. With 3 s the loop's second trigger at 2.0 s sounded until
    // 2.13 s, which passed for "the whole clip" while the step really cut the clip at 0.13 s)
    const auto clipSecs = r190io::soundingSeconds (clipSong, data, 1.9);
    const auto samplerSecs = r190io::soundingSeconds (samplerSong, data, 1.9);
    std::printf ("rackmix190io: one step plays the audio clip channel %.3f s, the Sampler %.3f s (sample 2 s)\n", clipSecs, samplerSecs);
    // the step plays the whole clip once, at its own pitch, not just the step's 1/16 (a playlist clip's end stops its
    // voice, a step's note-off does not); FL's render of the same project: docs/analise/REA_rodada190io.md §2
    CHECK (clipSecs > 1.85);
    CHECK (samplerSecs > 1.85); // (a Sampler step plays its sample out too)

    if (const char* dir = std::getenv ("LYNX_190IO_FLDIR"); dir != nullptr && *dir != 0)
    {
        const std::string base (dir);
        r190io::writeWav16 (base + "/sine2s.wav", *data);
        const auto winBase = std::getenv ("LYNX_190IO_FLDIR_WIN") != nullptr ? std::string (std::getenv ("LYNX_190IO_FLDIR_WIN")) : base;
        for (const auto& [name, song] : std::map<std::string, Song> { { "clipstep", clipSong }, { "samplerstep", samplerSong } })
        {
            auto s = song;
            s.channels[0].samplePath = winBase + "\\sine2s.wav";
            flpexport::Report report;
            const auto bytes = flpexport::exportFlp (s, {}, report);
            std::ofstream (base + "/" + name + ".flp", std::ios::binary).write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
            CHECK (! bytes.empty());
        }
    }
}

// FM-32: FL's route arrows (Routing Iconography): the selected track's ▼, an up-arrow on every other track (lit when the
// selected track sends there), greyed where routing would loop; "Send in use" on tracks others send into
TEST (rackmix190io_route_arrow_states)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    while (s.mixer.size() < 6)
        s.mixer.emplace_back();
    s.setRoute (1, 3, true); // 1 → 3
    s.setRoute (3, 4, true); // 3 → 4
    using routearrow::State;
    CHECK (routearrow::of (s, 3, 3) == State::out);
    CHECK (routearrow::of (s, 3, 4) == State::routed);
    CHECK (routearrow::of (s, 3, 1) == State::blocked);  // 1 feeds 3: back to 1 would loop
    CHECK (routearrow::of (s, 3, 2) == State::available);
    CHECK (routearrow::of (s, 3, 0) == (s.mixer[3].toMaster ? State::routed : State::available));
    CHECK (routearrow::of (s, 4, 1) == State::blocked);  // 1 → 3 → 4: a loop through 3
    CHECK (routearrow::of (s, 0, 2) == State::blocked);  // the Master sends nowhere inside the Mixer
    CHECK (routearrow::of (s, 0, 0) == State::out);
    CHECK (routearrow::of (s, 3, 99) == State::none);
    CHECK (routearrow::receives (s, 3) && routearrow::receives (s, 4) && ! routearrow::receives (s, 2));
}

// #154 part 2: the Mixer view that lists each strip's insert effects (a LYNX extra, off by default): its rows and hits
TEST (rackmix190io_strip_inserts_layout)
{
    using namespace lynx;
    CHECK (! stripinserts::defaultOn);
    CHECK (stripinserts::shownIn (mixerlayout::Layout::wide) && stripinserts::shownIn (mixerlayout::Layout::extraLarge));
    CHECK (! stripinserts::shownIn (mixerlayout::Layout::compact) && ! stripinserts::shownIn (mixerlayout::Layout::compactAlt));
    // the list's height: rows that fit the strip, between 3 and 6, plus its margins
    CHECK (stripinserts::rowsFor (300) == 3 && stripinserts::rowsFor (2000) == 6);
    CHECK (stripinserts::height (stripinserts::rowsFor (900)) == stripinserts::rowsFor (900) * trackinserts::rowHeight + 4);
    // hits as the track header list's (core/TrackInserts.h): effects, then "add" while a slot is free
    CHECK (trackinserts::itemAt (2, 0, 4, 0) == 0 && trackinserts::itemAt (2, 0, 4, 2 * trackinserts::rowHeight) == trackinserts::addRow);
}
