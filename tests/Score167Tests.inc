// --- Edison's Convert to score and dump to piano roll (core/NewtoneFx.h detectScore / dumpScore) - round 167c (§4)

#include "core/NewtoneFx.h"

TEST (score167_detects_the_melody)
{
    using namespace lynx;
    // C4, E4, G4 (0.5 s each, 0.1 s apart), all sung 0.2 semitone sharp; E4 at half the level
    const double rate = 44100.0, sharp = 0.2;
    SampleData s;
    s.sampleRate = rate;
    s.left.assign ((std::size_t) (rate * 2.0), 0.0f);
    const int keys[] = { 60, 64, 67 };
    const float amps[] = { 0.5f, 0.25f, 0.5f };
    for (int k = 0; k < 3; ++k)
    {
        const auto from = (std::size_t) (rate * (0.1 + 0.6 * k)), to = from + (std::size_t) (rate * 0.5);
        const auto hz = 440.0 * std::pow (2.0, (keys[k] + sharp - 69) / 12.0);
        for (auto i = from; i < to; ++i)
        {
            // a few harmonics, like a voice
            const auto t = (double) (i - from) / rate;
            double x = 0.0;
            for (int h = 1; h <= 4; ++h)
                x += std::sin (2.0 * 3.141592653589793 * hz * h * t) / h;
            s.left[i] = (float) (amps[k] * 0.6 * x);
        }
    }
    double detune = 0.0;
    const auto notes = newtonefx::detectScore (s, &detune);
    CHECK (notes.size() == 3u);
    CHECK (std::abs (detune - sharp) < 0.05);
    if (notes.size() == 3u)
    {
        for (int k = 0; k < 3; ++k)
        {
            CHECK (notes[(std::size_t) k].key == keys[k]); // the detune is taken out: the written keys
            CHECK (std::abs ((double) notes[(std::size_t) k].start / rate - (0.1 + 0.6 * k)) < 0.03);
            CHECK (std::abs ((double) notes[(std::size_t) k].end / rate - (0.6 + 0.6 * k)) < 0.03);
        }
        // volume = 0.5 + 0.5 x level: the loudest notes full, the quieter one about 0.75
        CHECK (notes[0].velocity > 0.95f && notes[2].velocity > 0.95f && std::abs (notes[1].velocity - 0.75f) < 0.05f);
    }

    // into the current pattern, the selected channel's notes replaced, the other channels' kept
    Song song;
    song.bpm = 120.0;
    song.patterns.resize (2);
    song.currentPattern = 1;
    Note old;
    old.channel = 2;
    old.key = 30;
    song.patterns[1].notes.push_back (old);
    old.channel = 0;
    song.patterns[1].notes.push_back (old);
    auto withShort = notes;
    newtonefx::DetectedNote tiny;
    tiny.start = 0;
    tiny.end = (std::int64_t) (rate * 0.01); // 0.01 s at 120 BPM = 19 ticks: left out (3 ticks at FL's PPQ 96)
    withShort.push_back (tiny);
    const auto count = newtonefx::dumpScore (song, 2, withShort, rate);
    CHECK (count == (int) notes.size());
    int ofTwo = 0, ofZero = 0;
    for (const auto& n : song.patterns[1].notes)
        (n.channel == 2 ? ofTwo : ofZero) += 1;
    CHECK (ofTwo == (int) notes.size() && ofZero == 1 && song.patterns[0].notes.empty());
    if (! notes.empty())
    {
        const auto& first = song.patterns[1].notes[0].channel == 2 ? song.patterns[1].notes[0] : song.patterns[1].notes[1];
        CHECK (first.start == std::llround ((double) notes[0].start / rate * 2.0 * ppq) && first.key == 60);
    }
}
