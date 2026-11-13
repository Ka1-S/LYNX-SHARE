#include "core/SimplePads.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/SimpleTracks.h"

namespace lynx::simplepads
{
namespace
{
constexpr char keyRows[padCount + 1] = "VBNMFGHJRTYU4567"; // pad 0 … 15, bottom row first

bool validTrack (const Song& song, int track)
{
    return simpletracks::kindOf (song, track) == simpletracks::Kind::pads;
}

ChannelDef* padAt (Song& song, int track, int pad)
{
    const auto c = padChannel (song, track, pad);
    return c >= 0 ? &song.channels[(std::size_t) c] : nullptr;
}
} // namespace

const char* modeName (Mode mode) noexcept
{
    switch (mode)
    {
        case Mode::gate: return "Gate";
        case Mode::oneShot: return "One-shot";
        case Mode::loop: return "Loop";
    }
    return "Gate";
}

char typingKey (int pad) noexcept { return pad >= 0 && pad < padCount ? keyRows[pad] : 0; }

int padForKey (int keyCode) noexcept
{
    if (keyCode >= 'a' && keyCode <= 'z')
        keyCode -= 'a' - 'A';
    for (int i = 0; i < padCount; ++i)
        if (keyRows[i] == keyCode)
            return i;
    return -1;
}

Tone toneFilter (float tone) noexcept
{
    Tone t;
    tone = std::clamp (tone, -1.0f, 1.0f);
    if (std::abs (tone) < 0.005f)
        return t;
    t.on = true;
    if (tone < 0.0f)
    {
        t.filterType = 0;
        t.cutoff = 22050.0f * std::pow (100.0f / 22050.0f, -tone);
    }
    else
    {
        t.filterType = 2;
        t.cutoff = 20.0f * std::pow (8000.0f / 20.0f, tone);
    }
    return t;
}

std::string toneText (float tone)
{
    const auto t = toneFilter (tone);
    if (! t.on)
        return "Off";
    char buf[48];
    if (t.cutoff >= 1000.0f)
        std::snprintf (buf, sizeof (buf), "%.1f kHz", (double) t.cutoff / 1000.0);
    else
        std::snprintf (buf, sizeof (buf), "%d Hz", (int) std::lround (t.cutoff));
    return std::string (t.filterType == 0 ? "Low-pass " : "High-pass ") + buf;
}

void apply (ChannelDef& c, std::int64_t frames)
{
    auto& p = c.simplePad;
    auto& s = c.sampler;
    const auto mode = modeOf (c);
    p.release = std::clamp (p.release, 0.0f, maxRelease);
    s.decay = 0.0f;
    s.sustain = 1.0f;
    // a gate or loop pad stops when its key is let go: at least 5 ms of release (no click); 0 means one-shot to the Sampler
    s.release = mode == Mode::oneShot ? 0.0f : std::max (p.release, 0.005f);
    s.attack = std::clamp (s.attack, 0.0f, maxAttack);
    s.pitch = std::clamp (s.pitch, -maxPitch, maxPitch);

    const auto tone = toneFilter (p.tone);
    s.filterType = tone.filterType;
    s.cutoff = tone.on ? tone.cutoff : 20000.0f;
    if (! tone.on)
        s.filterType = 0;

    // choke group: FL's cut groups are song-wide numbers, so a pad's group is made this insert's own
    p.group = std::clamp (p.group, 0, groupCount);
    const auto cut = p.group > 0 ? 1000 + std::max (0, c.mixerTrack) * 8 + p.group : 0;
    c.cutGroup = cut;
    c.cutBy = cut;

    // loop: one zone over every key, its loop = the played region, repeated while the note is held
    c.zones.clear();
    if (mode == Mode::loop && frames > 32 && ! c.samplePath.empty())
    {
        const auto first = (std::int64_t) std::floor ((double) std::clamp (s.regionStart, 0.0f, 1.0f) * (double) frames);
        const auto last = (std::int64_t) std::floor ((double) std::clamp (s.regionEnd, 0.0f, 1.0f) * (double) frames) - 1;
        if (last > first + 32)
        {
            SampleZone z;
            z.path = c.samplePath;
            z.rootKey = padKey;
            z.loopStart = first;
            z.loopEnd = last;
            z.loopMode = 2;
            c.zones.push_back (std::move (z));
        }
    }
    p.colour = std::clamp (p.colour, 0, colourCount);
}

std::vector<ChannelDef> emptyKit()
{
    std::vector<ChannelDef> kit;
    for (int i = 0; i < padCount; ++i)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.name = "Pad " + std::to_string (i + 1);
        c.simplePad.index = i;
        apply (c, 0);
        kit.push_back (std::move (c));
    }
    return kit;
}

int padChannel (const Song& song, int track, int pad)
{
    if (pad < 0 || pad >= padCount || ! validTrack (song, track))
        return -1;
    const auto insert = simpletracks::insertOf (song, track);
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (const auto& c = song.channels[(std::size_t) i]; c.mixerTrack == insert && c.simplePad.index == pad)
            return i;
    return -1;
}

std::vector<int> padChannels (const Song& song, int track)
{
    std::vector<int> out ((std::size_t) padCount, -1);
    if (! validTrack (song, track))
        return out;
    const auto insert = simpletracks::insertOf (song, track);
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (const auto& c = song.channels[(std::size_t) i]; c.mixerTrack == insert && c.simplePad.index >= 0 && c.simplePad.index < padCount
                                                            && out[(std::size_t) c.simplePad.index] < 0)
            out[(std::size_t) c.simplePad.index] = i;
    return out;
}

bool hasSample (const Song& song, int track, int pad)
{
    const auto c = padChannel (song, track, pad);
    return c >= 0 && ! song.channels[(std::size_t) c].samplePath.empty();
}

bool loadSample (Song& song, int track, int pad, const std::string& path, const std::string& name, std::int64_t frames, double seconds)
{
    auto* c = padAt (song, track, pad);
    if (c == nullptr || path.empty())
        return false;
    c->samplePath = path;
    c->name = name.empty() ? "Pad " + std::to_string (pad + 1) : name;
    c->sampler.regionStart = 0.0f;
    c->sampler.regionEnd = 1.0f;
    c->sampler.reverse = false;
    c->sampler.gainDb = 0.0f;
    c->sampler.pitch = 0.0f;
    c->simplePad.tone = 0.0f;
    if (seconds > 0.0)
        c->simplePad.mode = (std::uint8_t) (seconds <= oneShotSeconds ? Mode::oneShot : Mode::gate);
    apply (*c, frames);
    return true;
}

bool clearPad (Song& song, int track, int pad)
{
    auto* c = padAt (song, track, pad);
    if (c == nullptr)
        return false;
    const auto keep = c->simplePad.index;
    const auto insert = c->mixerTrack, group = c->group;
    const auto colour = c->colour;
    ChannelDef fresh;
    fresh.instrument = InstrumentKind::sampler;
    fresh.name = "Pad " + std::to_string (pad + 1);
    fresh.mixerTrack = insert;
    fresh.group = group;
    fresh.colour = colour;
    fresh.uid = c->uid;
    fresh.simplePad.index = keep;
    *c = std::move (fresh);
    apply (*c, 0);
    return true;
}

bool copyPad (Song& song, int track, int from, int to, std::int64_t frames)
{
    const auto a = padChannel (song, track, from), b = padChannel (song, track, to);
    if (a < 0 || b < 0 || a == b)
        return false;
    auto& dst = song.channels[(std::size_t) b];
    const auto& src = song.channels[(std::size_t) a];
    const auto uid = dst.uid;
    dst.samplePath = src.samplePath;
    dst.name = src.name;
    dst.sampler = src.sampler;
    dst.volume = src.volume;
    dst.pan = src.pan;
    dst.simplePad = src.simplePad;
    dst.simplePad.index = to;
    dst.uid = uid;
    apply (dst, frames);
    return true;
}

bool swapPads (Song& song, int track, int a, int b)
{
    const auto ca = padChannel (song, track, a), cb = padChannel (song, track, b);
    if (ca < 0 || cb < 0 || ca == cb)
        return false;
    std::swap (song.channels[(std::size_t) ca].simplePad.index, song.channels[(std::size_t) cb].simplePad.index);
    return true;
}

float normalizeGainDb (float peak) noexcept
{
    if (! (peak > 1.0e-6f))
        return 0.0f;
    return std::clamp (-20.0f * std::log10 (peak), -24.0f, 24.0f);
}
} // namespace lynx::simplepads
