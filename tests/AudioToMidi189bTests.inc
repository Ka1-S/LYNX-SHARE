// --- Convert to MIDI (round a2m189b): Basic Pitch's windows and note creation, melody, drums, the new pattern -------

#include <random>

#include "core/AudioToMidi.h"

TEST (a2m189b_windows_are_stitched_on_an_exact_time_grid)
{
    using namespace lynx::a2m;
    // a fake model that answers each frame with the input sample at that frame: the stitched output must be the ramp
    // at k * hop (within half a frame: the 141.27-frame stride is not whole)
    for (const std::int64_t samples : { (std::int64_t) 1000, (std::int64_t) windowSamples, (std::int64_t) windowStride * 3 + 517 })
    {
        std::vector<float> ramp ((std::size_t) samples);
        for (std::int64_t i = 0; i < samples; ++i)
            ramp[(std::size_t) i] = (float) i;
        constexpr int perWindow = 172;
        int calls = 0;
        double last = 0.0;
        Posteriorgram pg;
        const auto ok = runModel (ramp, perWindow, [&] (const float* in, float* note, float* onset) {
            ++calls;
            for (int f = 0; f < perWindow; ++f)
                for (int b = 0; b < keys; ++b)
                {
                    const auto at = std::min (f * hop, windowSamples - 1);
                    // (the padding before the audio reads 0; mark it so it can't pass for sample 0)
                    note[f * keys + b] = in[at] == 0.0f && at < overlapSamples / 2 ? -1.0f : in[at];
                    onset[f * keys + b] = (float) b;
                }
            return true;
        }, [&] (double p) { last = p; }, pg);
        CHECK (ok);
        CHECK (calls == numWindows (samples));
        CHECK (last == 1.0);
        CHECK (pg.frames == (int) ((samples + hop - 1) / hop));
        auto worst = 0.0;
        for (int k = 0; k < pg.frames; ++k)
        {
            const auto expected = std::min<double> ((double) k * hop, (double) samples - 1);
            const auto got = (double) pg.note[(std::size_t) k * keys];
            if ((double) k * hop < (double) samples - hop)
                worst = std::max (worst, std::abs (got - expected));
            CHECK (pg.onset[(std::size_t) k * keys + 7] == 7.0f);
        }
        CHECK (worst <= hop / 2.0);
    }
}

TEST (a2m189b_model_stop_ends_the_run)
{
    using namespace lynx::a2m;
    std::vector<float> audio ((std::size_t) windowStride * 5, 0.1f);
    int calls = 0;
    Posteriorgram pg;
    CHECK (! runModel (audio, 172, [&] (const float*, float*, float*) { return ++calls < 2; }, {}, pg));
    CHECK (calls == 2);
}

namespace
{
lynx::a2m::Posteriorgram emptyPg (int frames)
{
    lynx::a2m::Posteriorgram pg;
    pg.frames = frames;
    pg.note.assign ((std::size_t) frames * lynx::a2m::keys, 0.0f);
    pg.onset = pg.note;
    return pg;
}

void paint (lynx::a2m::Posteriorgram& pg, int key, int from, int to, float level, float onset)
{
    const auto b = key - lynx::a2m::lowestKey;
    for (int t = from; t < to; ++t)
        pg.note[(std::size_t) t * lynx::a2m::keys + (std::size_t) b] = level;
    if (onset > 0.0f)
        pg.onset[(std::size_t) from * lynx::a2m::keys + (std::size_t) b] = onset;
}
} // namespace

TEST (a2m189b_basic_pitch_note_creation)
{
    using namespace lynx::a2m;
    const auto frameSeconds = hop / modelRate;
    auto pg = emptyPg (400);
    paint (pg, 60, 20, 80, 0.8f, 0.9f);   // a clear note with an onset
    paint (pg, 64, 100, 105, 0.8f, 0.9f); // too short (5 frames < 11)
    paint (pg, 67, 150, 220, 0.6f, 0.0f); // no onset: only the melodia trick finds it
    paint (pg, 48, 250, 300, 0.2f, 0.9f); // below the frame threshold: no note
    NoteOptions options;
    options.inferOnsets = false;
    auto notes = notesFromPosteriorgram (pg, options);
    CHECK (notes.size() == 2);
    CHECK (notes[0].key == 60 && std::abs (notes[0].start - 20 * frameSeconds) < 1e-9 && std::abs (notes[0].end - 80 * frameSeconds) < 1e-9);
    CHECK (std::abs (notes[0].velocity - 0.8f) < 1e-6f);
    CHECK (notes[1].key == 67 && std::abs (notes[1].start - 150 * frameSeconds) < 1e-9);

    options.melodiaTrick = false;
    notes = notesFromPosteriorgram (pg, options);
    CHECK (notes.size() == 1 && notes[0].key == 60);

    // a key range leaves the rest out; the mode settings: melody keeps the melodia trick, harmony doesn't
    options.minKey = 61;
    CHECK (notesFromPosteriorgram (pg, options).empty());
    CHECK (optionsFor (Mode::melody).melodiaTrick && ! optionsFor (Mode::harmony).melodiaTrick);
    CHECK (optionsFor (Mode::melody).onsetThreshold == 0.7f && optionsFor (Mode::harmony).frameThreshold == 0.4f);
}

TEST (a2m189b_melody_keeps_one_note_at_a_time)
{
    using namespace lynx::a2m;
    // a fundamental with a weaker octave above it, then a legato step to the next note while the first still sounds
    std::vector<Note> notes { { 0.00, 0.60, 40, 0.8f }, { 0.01, 0.55, 52, 0.4f }, { 0.50, 1.00, 43, 0.7f },
                              { 0.70, 0.80, 55, 0.3f } };
    const auto mono = monophonic (notes, 0.05);
    CHECK (mono.size() == 2);
    CHECK (mono[0].key == 40 && std::abs (mono[0].end - 0.50) < 1e-9); // cut where the next one starts
    CHECK (mono[1].key == 43 && std::abs (mono[1].end - 1.00) < 1e-9); // the weak overtone inside it is left out
    for (std::size_t i = 1; i < mono.size(); ++i)
        CHECK (mono[i].start >= mono[i - 1].end);
}

TEST (a2m189b_drum_hits_are_found_and_told_apart)
{
    using namespace lynx::a2m;
    constexpr double rate = 44100.0;
    std::vector<float> audio ((std::size_t) (rate * 8.0), 0.0f);
    std::mt19937 rng (189);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    struct Hit { double time; int key; };
    std::vector<Hit> hits;
    const auto beat = 0.5;
    for (int i = 0; i < 16; ++i)
    {
        const auto t = 0.25 + i * beat;
        hits.push_back ({ t, i % 2 == 0 ? kickKey : snareKey });
        hits.push_back ({ t + beat / 2, hatKey });
    }
    for (const auto& h : hits)
    {
        const auto start = (std::size_t) (h.time * rate);
        float dull = 0.0f, duller = 0.0f, dullest = 0.0f; // the snare's noise, low-passed 3 times around 3 kHz (wires, not a cymbal)
        for (std::size_t i = 0; i < (std::size_t) (rate * 0.25) && start + i < audio.size(); ++i)
        {
            const auto s = (double) i / rate;
            float v = 0.0f;
            if (h.key == kickKey) // a falling 60 Hz sine
                v = (float) (std::sin (2.0 * 3.141592653589793 * 60.0 * s) * std::exp (-s * 12.0));
            else if (h.key == snareKey) // 200 Hz body and noise, ringing ~150 ms
            {
                dull += 0.35f * (noise (rng) - dull);
                duller += 0.35f * (dull - duller);
                dullest += 0.35f * (duller - dullest);
                v = (float) ((0.5 * std::sin (2.0 * 3.141592653589793 * 200.0 * s) + 2.0 * dullest) * std::exp (-s * 18.0) * std::min (1.0, s / 0.002));
            }
            else // a short burst of differentiated (bright) noise
                v = (float) (0.4 * (noise (rng) - noise (rng)) * std::exp (-s * 60.0));
            audio[start + i] += 0.5f * v;
        }
    }
    const auto found = drumHits (audio.data(), (std::int64_t) audio.size(), rate);
    for (const int key : { kickKey, snareKey, hatKey })
    {
        int expected = 0, matched = 0, got = 0;
        for (const auto& h : hits)
            if (h.key == key)
            {
                ++expected;
                matched += std::any_of (found.begin(), found.end(), [&] (const Note& n) { return n.key == key && std::abs (n.start - h.time) < 0.03; }) ? 1 : 0;
            }
        for (const auto& n : found)
            got += n.key == key ? 1 : 0;
        CHECK (matched >= expected - 1); // recall
        CHECK (got <= expected + 2);     // precision
    }
    CHECK (drumHits (nullptr, 0, rate).empty());
}

TEST (a2m189b_the_pattern_lands_under_the_clip)
{
    using namespace lynx;
    Song song;
    ChannelDef audioChannel;
    audioChannel.name = "loop";
    audioChannel.instrument = InstrumentKind::audioClip;
    audioChannel.samplePath = "/x/loop.wav";
    song.channels.push_back (audioChannel);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.track = 2;
    clip.start = 4 * ppq;
    clip.length = 8 * ppq;
    clip.offset = ppq; // the clip starts one beat into the file
    song.playlist.push_back (clip);
    Clip other;
    other.track = 3;
    song.playlist.push_back (other);

    // 120 BPM, 44.1 kHz: one beat = 22050 frames
    a2m::ClipTiming timing;
    timing.sampleRate = 44100.0;
    timing.framesPerTick = 22050.0 / ppq;
    timing.spanFrames = 22050.0 * 16;
    const auto first = (std::int64_t) 22050; // the analysed audio starts at the clip's first frame
    const std::vector<a2m::Note> notes { { 0.0, 0.25, 60, 0.5f }, { 1.0, 1.5, 64, 0.9f }, { -0.6, -0.55, 70, 0.5f }, { 3.9, 9.0, 67, 0.5f } };
    a2m::Names names { "loop - Melody", "loop - Melody", "Kick", "Snare", "Hat" };
    CHECK (a2m::addClipPattern (song, 0, "/x/other.wav", a2m::Mode::melody, notes, first, timing, names) == -1);
    const auto pattern = a2m::addClipPattern (song, 0, "/x/loop.wav", a2m::Mode::melody, notes, first, timing, names);
    CHECK (pattern == (int) song.patterns.size() - 1);
    const auto& p = song.patterns[(std::size_t) pattern];
    CHECK (p.name == "loop - Melody");
    CHECK (p.notes.size() == 3); // (the one before the clip is left out)
    CHECK (p.notes[0].start == 0 && p.notes[0].length == ppq / 2 && p.notes[0].key == 60);
    CHECK (p.notes[1].start == 2 * ppq && p.notes[1].length == ppq);
    CHECK (p.notes[2].start == (std::int64_t) std::llround (3.9 * 2 * ppq) && p.notes[2].start + p.notes[2].length == 8 * ppq); // cut at the clip's end
    CHECK (song.channels.size() == 2 && song.channels[1].instrument == InstrumentKind::synth && song.channels[1].name == "loop - Melody");
    CHECK (p.notes[0].channel == 1);
    // a new track right under the audio clip; the clips below move down
    CHECK (song.playlist[1].track == 4);
    const auto& added = song.playlist.back();
    CHECK (added.kind == ClipKind::pattern && added.pattern == pattern && added.track == 3 && added.start == clip.start && added.length == clip.length);

    // drums: Kick, Snare and Hat channels, one step-long note per hit on the step grid's key
    const std::vector<a2m::Note> hits { { 0.0, 0.06, a2m::kickKey, 1.0f }, { 0.5, 0.56, a2m::snareKey, 0.8f }, { 0.75, 0.81, a2m::hatKey, 0.5f } };
    const auto drums = a2m::addClipPattern (song, 0, "/x/loop.wav", a2m::Mode::drums, hits, first, timing, names);
    CHECK (drums >= 0 && song.channels.size() == 5);
    CHECK (song.channels[2].instrument == InstrumentKind::kick && song.channels[3].instrument == InstrumentKind::snare && song.channels[4].instrument == InstrumentKind::hat);
    const auto& d = song.patterns[(std::size_t) drums];
    CHECK (d.notes.size() == 3 && d.notes[0].channel == 2 && d.notes[1].channel == 3 && d.notes[2].channel == 4);
    CHECK (d.notes[1].key == 60 && d.notes[1].length == ticksPerStep && d.notes[1].start == ppq);

    // a clip that plays backwards mirrors the times
    timing.backwards = true;
    const auto clipFirst = a2m::clipFrames (clip, timing, 22050 * 16);
    CHECK (clipFirst.first == 22050 * 16 - 9 * 22050 && clipFirst.count == 8 * 22050);
}
