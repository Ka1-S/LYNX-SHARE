// Round 178a: GitHub issues #87, #63, #86 (docs/analise/REA_rodada178a.md)

#include "core/ClipOps.h"

// #87: an audio clip dropped at 130 BPM, then the tempo lowered to 108: the clip kept its length in ticks while its
// audio (played at the file's own speed) got shorter in bars, so the clip showed an empty tail. FL keeps such a clip's
// region in milliseconds and resizes it with the tempo; a clip whose channel follows the tempo keeps its beats.
TEST (issues178a_audio_clips_follow_a_tempo_change)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.bpm = 130.0;
    song.playlist.clear();
    song.arrangements.resize (2);
    song.currentArrangement = 0;

    ChannelDef plain;
    plain.instrument = InstrumentKind::audioClip;
    plain.samplePath = "drill.wav";
    ChannelDef synced = plain; // Time stretch set: the whole file lasts 16 beats, whatever the tempo
    synced.sampler.stretchBeats = 16.0f;
    song.channels.push_back (plain);
    const auto plainIndex = (int) song.channels.size() - 1;
    song.channels.push_back (synced);
    const auto syncedIndex = (int) song.channels.size() - 1;

    // a 60 s file at 44.1 kHz, as the Playlist sizes a dropped clip
    const double frames = 60.0 * 44100.0, rate = 44100.0;
    Clip whole;
    whole.kind = ClipKind::audio;
    whole.channel = plainIndex;
    whole.start = 4 * ppq;
    whole.length = clipops::fileTicks (song.channels[(std::size_t) plainIndex], frames, rate, song.bpm);
    CHECK (whole.length == 130 * ppq); // 60 s at 130 BPM = 130 beats

    Clip trimmed = whole; // a part of it: skips 10 s, plays 20 s, with fades
    trimmed.track = 1;
    trimmed.offset = (std::int64_t) (10.0 * 130.0 / 60.0 * ppq);
    trimmed.length = (std::int64_t) (20.0 * 130.0 / 60.0 * ppq);
    trimmed.fadeIn = ppq;
    trimmed.fadeOut = 2 * ppq;

    Clip stretched = whole;
    stretched.track = 2;
    stretched.channel = syncedIndex;
    stretched.length = 16 * ppq;

    Clip pattern;
    pattern.kind = ClipKind::pattern;
    pattern.track = 3;
    pattern.length = 8 * ppq;

    song.playlist = { whole, trimmed, stretched, pattern };
    song.arrangements[1].clips = { whole };

    clipops::followTempo (song, 108.0);
    CHECK (song.bpm == 108.0);

    // the whole clip ends where its audio does at the new tempo: no empty tail
    const auto& w = song.playlist[0];
    const auto audio = clipops::fileTicks (song.channels[(std::size_t) plainIndex], frames, rate, 108.0);
    std::printf ("  whole clip at 108 BPM: %lld ticks, audio %lld ticks\n", (long long) w.length, (long long) audio);
    CHECK (std::llabs (w.length - audio) <= 1);
    CHECK (w.start == 4 * ppq); // the position stays

    // the trimmed clip plays the same seconds of the same file
    const auto& t = song.playlist[1];
    const auto seconds = [] (double ticks, double bpm) { return ticks / ppq * 60.0 / bpm; };
    CHECK (std::abs (seconds ((double) t.offset + t.offsetFraction, 108.0) - 10.0) < 1e-6);
    CHECK (std::abs (seconds ((double) t.length, 108.0) - 20.0) < 0.001);
    CHECK (std::abs (seconds ((double) t.fadeIn, 108.0) - seconds (ppq, 130.0)) < 0.001);
    CHECK (std::abs (seconds ((double) t.fadeOut, 108.0) - seconds (2 * ppq, 130.0)) < 0.001);

    // tempo-synced audio and pattern clips keep their beats
    CHECK (song.playlist[2].length == 16 * ppq);
    CHECK (song.playlist[3].length == 8 * ppq);

    // the stored arrangement follows too
    CHECK (std::llabs (song.arrangements[1].clips[0].length - audio) <= 1);

    // back to 130: the clips are as they were (to the tick), even after many small steps (a drag on the tempo)
    for (int i = 1; i <= 220; ++i)
        clipops::followTempo (song, 108.0 + 0.1 * i);
    clipops::followTempo (song, 130.0);
    CHECK (std::llabs (song.playlist[0].length - whole.length) <= 2);
    CHECK (std::llabs (song.playlist[1].offset - trimmed.offset) <= 2);
    CHECK (std::llabs (song.playlist[1].length - trimmed.length) <= 2);

    // the same tempo again changes nothing
    const auto before = song.playlist;
    clipops::followTempo (song, 130.0);
    CHECK (song.playlist == before);
}

#include "core/Arpeggiator.h"

// #63 (the rest of it): the Arpeggiator played every note centred, with no fine pitch or Mod X / Y: it kept only the
// held notes' keys and velocities. Each arpeggiated note now takes the expression of the held note it plays.
TEST (issues178a_arpeggiator_keeps_the_note_pan)
{
    using namespace lynx;
    ArpSettings s;
    s.direction = 1; // up
    s.stepBeats = 0.25f;
    Arpeggiator arp;
    const auto tps = ticksPerSample (120.0, 48000.0);

    std::array<NoteEvent, 2> in {};
    const float pans[] = { -1.0f, 0.75f };
    const int keys[] = { 60, 67 };
    for (int k = 0; k < 2; ++k)
    {
        auto& e = in[(std::size_t) k];
        e.type = NoteEvent::Type::on;
        e.channel = 0;
        e.key = keys[k];
        e.velocity = 0.8f;
        e.pan = pans[k];
        e.cents = k == 0 ? -30.0f : 40.0f;
        e.modX = k == 0 ? 0.1f : 0.9f;
        e.modY = k == 0 ? 0.2f : 0.7f;
        e.colour = (std::int8_t) (k + 3);
    }

    std::vector<NoteEvent> played;
    auto first = true;
    for (double tick = 0.0; tick < ppq * 2.0; tick += 512 * tps)
    {
        int out = 0;
        arp.process (0, first ? in.data() : nullptr, first ? 2 : 0, tick, tps, 512, s, arp.output.data(), out);
        first = false;
        for (int i = 0; i < out; ++i)
            if (arp.output[(std::size_t) i].type == NoteEvent::Type::on)
                played.push_back (arp.output[(std::size_t) i]);
    }
    CHECK (played.size() >= 6);
    for (const auto& e : played)
    {
        const auto k = e.key == 60 ? 0 : 1;
        CHECK (e.key == keys[k]);
        CHECK (e.pan == pans[k]);
        CHECK (e.cents == (k == 0 ? -30.0f : 40.0f));
        CHECK (e.modX == (k == 0 ? 0.1f : 0.9f) && e.modY == (k == 0 ? 0.2f : 0.7f));
        CHECK (e.colour == (std::int8_t) (k + 3));
    }

    // FL's Repeat 0 (the whole chord on each step) keeps each note's own pan too
    s.octaveRepeat = 0;
    arp.reset();
    played.clear();
    first = true;
    for (double tick = 0.0; tick < ppq; tick += 512 * tps)
    {
        int out = 0;
        arp.process (0, first ? in.data() : nullptr, first ? 2 : 0, tick, tps, 512, s, arp.output.data(), out);
        first = false;
        for (int i = 0; i < out; ++i)
            if (arp.output[(std::size_t) i].type == NoteEvent::Type::on)
                played.push_back (arp.output[(std::size_t) i]);
    }
    CHECK (played.size() >= 2);
    for (const auto& e : played)
        CHECK (e.pan == ((e.key % 12) == 0 ? -1.0f : 0.75f));
}

// #63: a note's Mod X / Mod Y on FL's native sampler voice: FL adds the note's Init.FCut / FRes ((b − 128) / 128 of its
// Mod X / Y bytes, REA_rodada76 §1) to the channel's filter X / Y. So a note at Mod X 64 (0.25) on a channel at filter X
// 228 sounds exactly like a centred note on a channel at filter X 100; a centred note is unchanged.
TEST (issues178a_note_mod_x_moves_the_fl_voice_filter)
{
    using namespace lynx;
    auto noise = std::make_shared<SampleData>();
    noise->sampleRate = 44100.0;
    std::uint32_t seed = 7;
    for (int i = 0; i < 22050; ++i)
    {
        seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
        noise->left.push_back ((float) (((double) seed / 2147483647.0 * 2.0 - 1.0) * 0.3));
    }
    const std::vector<std::int32_t> plain = { 0, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, 0 };
    const std::vector<std::int32_t> vol = { 4, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, -101 };
    auto render = [&] (int filterX, int filterY, std::optional<std::pair<float, float>> mod) {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        for (const auto* r : { &plain, &vol, &plain, &plain, &plain })
            c.sampler.flChannel.insert (c.sampler.flChannel.end(), r->begin(), r->end());
        c.sampler.flChannel.insert (c.sampler.flChannel.end(), { filterX, filterY, 0, 96 });
        auto inst = makeInstrument (InstrumentKind::sampler, noise);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        inst->setSongPosition (0.0);
        if (mod)
            inst->setNextNoteModulation (mod->first, mod->second);
        inst->noteOn (60, 100.0f / 128.0f);
        std::vector<float> l (8000), r (8000);
        inst->render (l.data(), r.data(), 8000, 1.0f, 1.0f);
        return l;
    };
    const auto centred = render (100, 64, std::nullopt);
    CHECK (render (100, 64, std::pair { 0.5f, 0.5f }) == centred);              // Mod X / Y at centre: nothing changes
    const auto darker = render (100, 64, std::pair { 0.25f, 0.5f });
    CHECK (darker != centred);                                                 // the note's Mod X moves the cutoff
    const auto same = render (228, 64, std::pair { 0.25f, 0.5f });             // 228/256 − 0.5 = 100/256
    auto diff = 0.0;
    for (std::size_t i = 0; i < same.size(); ++i)
        diff = std::max (diff, (double) std::abs (same[i] - centred[i]));
    std::printf ("  channel X 228 + note Mod X 0.25 vs channel X 100: max diff %.3g\n", diff);
    CHECK (diff < 1e-6);
    CHECK (render (100, 64, std::pair { 0.5f, 0.9f }) != centred);             // Mod Y: the resonance
}
