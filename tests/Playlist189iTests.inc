// --- Round 189i: FL's per-clip edge declicker (PLFadeHandleMenu: Manual, Declicker Automatic / Disabled / Enabled) ----
// (FLEngine 2026 [DECOMP] FUN_00d01bc0 / FUN_00cfc020 / FUN_00cfbf60 menu, FUN_00d27cf0 / FUN_00d253d0 / FUN_00d24400
// voice, FUN_00d011a0 seamless joins, FUN_012c6ae0 / FUN_00977870 declicker tables; notes in /mnt/e/lynx-work/flgap189i)

#include "core/ClipOps.h"
#include "core/Instruments.h"
#include "core/Scheduler.h"

namespace
{
// an audio clip voice of a DC 0.5 sample with the channel's declicking `mode`: renders `before` frames, ends the clip,
// renders 1000 more; returns both blocks
struct Edge189i
{
    std::vector<float> head, tail;
};
Edge189i edge189i (int mode, std::uint16_t shape, float fadeIn = 0.0f, float fadeOut = 0.0f, float elapsed = 0.0f)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    data->left.assign (44100 * 2, 0.5f);
    auto clip = lynx::makeInstrument (lynx::InstrumentKind::audioClip, data);
    clip->prepare (44100.0);
    lynx::ChannelDef def;
    def.instrument = lynx::InstrumentKind::audioClip;
    def.sampler.declickMode = mode;
    clip->configure (def, 120.0);
    const auto key = lynx::audioClipKey (3);
    clip->setNextClipFades (elapsed, 20000.0f, fadeIn, fadeOut, shape);
    clip->noteOnAt (key, 1.0f, 0.1);
    Edge189i e;
    e.head.assign (9000, 0.0f);
    std::vector<float> r (9000);
    clip->render (e.head.data(), r.data(), 9000, 1.0f, 1.0f);
    clip->noteOff (key);
    e.tail.assign (1000, 0.0f);
    clip->render (e.tail.data(), r.data(), 1000, 1.0f, 1.0f);
    return e;
}
int sounding189i (const std::vector<float>& v)
{
    int n = 0;
    while (n < (int) v.size() && std::fabs (v[(std::size_t) n]) > 0.0f)
        ++n;
    return n;
}
} // namespace

// The clip's end takes the channel's declicking release (FL: Automatic / Enabled): 10 ms raised cosine for "Out only";
// Disabled (shape bit 6), a manual end (bit 8) or a fade out end with a sharp cut
TEST (playlist189i_clip_end_declick)
{
    const auto automatic = edge189i (0, 0);
    const auto level = automatic.head.back();
    CHECK (level > 0.1f);
    CHECK (std::abs (sounding189i (automatic.tail) - 441) <= 2);
    CHECK (std::abs (automatic.tail[220] / level - 0.5f) < 0.02f);
    CHECK (std::abs (sounding189i (edge189i (2, 0).tail) - 882) <= 2); // Transient (bleeding): 20 ms
    CHECK (sounding189i (edge189i (0, 64).tail) == 0);
    CHECK (sounding189i (edge189i (0, 256).tail) == 0);
    CHECK (sounding189i (edge189i (0, 0, 0.0f, 100.0f).tail) == 0);
}

// The clip's start: modes 3 / 4 / 5 fade in with a raised cosine (20 / 100 / 200 ms), modes 1 / 2 remove the first
// frame's step over 4 ms, mode 0 has no attack; Disabled (bit 5) or a manual start (bit 7) start at full level
TEST (playlist189i_clip_start_declick)
{
    const auto plain = edge189i (0, 0).head;
    CHECK (plain[0] > 0.1f && std::abs (plain[0] - plain[100]) < 1e-6f);
    const auto generic = edge189i (3, 0).head; // 20 ms = 882 frames
    const auto level = generic[5000];
    CHECK (generic[0] < 0.001f * level);
    CHECK (std::abs (generic[441] / level - 0.5f) < 0.01f);
    CHECK (std::abs (generic[900] - level) < 1e-6f);
    const auto transient = edge189i (2, 0).head; // 4 ms = 176 frames: x - x0 (1 + cos((k + 1) pi / 177)) / 2
    CHECK (transient[0] < 0.001f * level);
    CHECK (std::abs (transient[87] / level - 0.5f) < 0.02f);
    CHECK (std::abs (transient[200] - level) < 1e-6f);
    CHECK (std::abs (edge189i (3, 32).head[0] - level) < 1e-6f);
    CHECK (std::abs (edge189i (3, 128).head[0] - level) < 1e-6f);
    CHECK (edge189i (3, 128, 0.0f, 0.0f, 500.0f).head[0] < 0.001f * level); // started past its manual start: declicked
}

// The menu's data: FL's bits (manual 0 / 1, choice >> 2 / >> 14), Manual off zeroes the fade, seamless joins
TEST (playlist189i_declick_choice_bits_and_joins)
{
    using namespace lynx;
    Clip a;
    a.kind = ClipKind::audio;
    a.channel = 2;
    a.track = 4;
    a.start = 0;
    a.length = 96;
    a.offset = 48;
    clipops::setDeclickChoice (a, 0, clipops::declickDisabled);
    clipops::setDeclickChoice (a, 1, clipops::declickEnabled);
    CHECK (a.flFadeFlags == ((3u << 2) | (2u << 14)));
    CHECK (clipops::declickChoice (a, 0) == 3 && clipops::declickChoice (a, 1) == 2);
    a.fadeOut = 10;
    clipops::setManualFade (a, 1, true);
    CHECK (clipops::manualFade (a, 1) && ! clipops::manualFade (a, 0) && (a.flFadeFlags & 2u) != 0);
    clipops::setManualFade (a, 1, false);
    CHECK (! clipops::manualFade (a, 1) && a.fadeOut == 0);

    Clip b = a;
    b.flFadeFlags = 0;
    b.start = 96;
    b.offset = 144; // the source goes on where a stopped
    std::vector<Clip> playlist { a, b };
    CHECK (clipops::seamlessJoin (playlist, 0, 1, 120.0, 96));
    CHECK (clipops::seamlessJoin (playlist, 1, 0, 120.0, 96));
    CHECK (! clipops::seamlessJoin (playlist, 0, 0, 120.0, 96));
    playlist[1].offset = 200; // a jump in the source
    CHECK (! clipops::seamlessJoin (playlist, 0, 1, 120.0, 96));
    playlist[1].offset = 144;
    playlist[1].gain = 0.5f;
    CHECK (! clipops::seamlessJoin (playlist, 0, 1, 120.0, 96));
    playlist[1].gain = a.gain;
    playlist[1].track = 5;
    CHECK (! clipops::seamlessJoin (playlist, 0, 1, 120.0, 96));
}
