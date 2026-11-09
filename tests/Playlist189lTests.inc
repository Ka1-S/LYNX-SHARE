// --- Round 189l: Make unique as sample renders the clip's own pitch, stretch and reverse into the file (core
// renderClipAudio) and resets them on the clip (clipops::resetRenderedClip): the clip sounds the same afterwards

#include "core/ClipOps.h"
#include "core/RackRenderer.h"

namespace
{
std::shared_ptr<lynx::SampleData> tone189l()
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    data->left.resize (44100 * 2);
    for (std::size_t i = 0; i < data->left.size(); ++i)
        data->left[i] = (float) (0.4 * std::sin (2.0 * 3.141592653589793 * 330.0 * (double) i / 44100.0) * (0.3 + 0.7 * (double) i / 88200.0));
    return data;
}
lynx::Song song189l()
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    ChannelDef ch;
    ch.instrument = InstrumentKind::audioClip;
    ch.samplePath = "tone.wav";
    s.channels.push_back (ch);
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = 0;
    c.start = 0;
    c.length = ppq;      // 0.5 s at 120 BPM
    c.offset = ppq / 2;  // from 0.25 s into the file
    s.playlist.push_back (c);
    return s;
}
// renders the clip, makes it unique (the rendered file on a copied channel, the clip reset) and renders it again
struct Unique189l
{
    lynx::ClipAudio before, after;
};
Unique189l unique189l (const lynx::Song& original)
{
    using namespace lynx;
    const auto tone = tone189l();
    RackState::Factories f;
    Unique189l u;
    std::shared_ptr<SampleData> part;
    f.samples = [&] (const std::string& path) -> std::shared_ptr<const SampleData> { return path == "part.wav" ? part : tone; };
    u.before = renderClipAudio (original, 0, 44100.0, f);
    part = std::make_shared<SampleData>();
    part->left = u.before.left;
    if (u.before.right != u.before.left)
        part->right = u.before.right;
    auto song = original;
    auto copy = song.channels[0];
    copy.samplePath = "part.wav";
    song.channels.push_back (copy);
    clipops::resetRenderedClip (song.playlist[0], 1);
    u.after = renderClipAudio (song, 0, 44100.0, f);
    return u;
}
float residual189l (const Unique189l& u)
{
    if (u.before.left.size() != u.after.left.size() || u.before.left.empty())
        return 1.0f;
    float worst = 0.0f;
    for (std::size_t i = 0; i < u.before.left.size(); ++i)
        worst = std::max ({ worst, std::fabs (u.before.left[i] - u.after.left[i]), std::fabs (u.before.right[i] - u.after.right[i]) });
    return worst;
}
} // namespace

TEST (playlist189l_unique_sample_plain_is_the_clips_part)
{
    using namespace lynx;
    const auto tone = tone189l();
    const auto u = unique189l (song189l());
    CHECK (u.before.left.size() == 22050);
    // a plain clip: the file is the part of the sample it plays, at its own level
    float worst = 0.0f;
    for (std::size_t i = 0; i < u.before.left.size(); ++i)
        worst = std::max (worst, std::fabs (u.before.left[i] - tone->left[11025 + i]));
    CHECK (worst < 1e-5f);
    CHECK (residual189l (u) < 1e-5f);
    // not an audio clip, or out of range: nothing
    RackState::Factories f;
    f.samples = [tone] (const std::string&) -> std::shared_ptr<const SampleData> { return tone; };
    CHECK (renderClipAudio (song189l(), 3, 44100.0, f).left.empty());
}

TEST (playlist189l_unique_sample_renders_pitch_stretch_reverse)
{
    using namespace lynx;
    auto song = song189l();
    const auto plain = unique189l (song).before;
    song.playlist[0].reversed = true;
    auto u = unique189l (song);
    CHECK (residual189l (u) < 1e-5f);
    const auto differs = [&plain] (const ClipAudio& a) {
        float d = 0.0f;
        for (std::size_t i = 0; i < std::min (a.left.size(), plain.left.size()); ++i)
            d = std::max (d, std::fabs (a.left[i] - plain.left[i]));
        return d > 0.01f;
    };
    CHECK (differs (u.before)); // the reverse is in the file
    song.playlist[0].reversed = false;
    song.playlist[0].stretch = 2.0;
    song.playlist[0].length = 2 * ppq;
    u = unique189l (song);
    CHECK (u.before.left.size() == 44100);
    CHECK (residual189l (u) < 1e-5f);
    CHECK (differs (u.before));
    song.playlist[0].stretch = 1.0;
    song.playlist[0].length = ppq;
    song.playlist[0].pitch = 3.0f;
    u = unique189l (song);
    CHECK (residual189l (u) < 1e-4f);
    CHECK (differs (u.before));

    Clip c;
    c.channel = 0;
    c.offset = 50;
    c.offsetFraction = 0.25f;
    c.pitch = -2.0f;
    c.stretch = 1.5;
    c.reversed = true;
    clipops::resetRenderedClip (c, 4);
    CHECK (c.channel == 4 && c.offset == 0 && c.offsetFraction == 0.0f && c.pitch == 0.0f && c.stretch == 1.0 && ! c.reversed);
}
