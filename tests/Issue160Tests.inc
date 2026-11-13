// Issue #160: Fruity Slicer — Piano roll notes all played the same part of the sample. The rack keeps a channel's
// instrument across edits and configures it with the channel again; the Slicer's slices live in its state, so that
// configure emptied the slice table and every key played the sample from its start. Also FL's key table
// (FUN_0076c200): a slice's own key plays it, a later slice wins a shared key, keys without a slice are silent.

#include "core/FruitySlicer.h"
#include "core/RackRenderer.h"

namespace issue160
{
// four slices of 11025 frames, slice i a sine at amplitude 0.1 · (i + 1)
std::shared_ptr<const lynx::SampleData> steps()
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    for (int i = 0; i < 4 * 11025; ++i)
        d->left.push_back (0.1f * (float) (i / 11025 + 1) * (float) std::sin (i * 0.05));
    d->right = d->left;
    return d;
}

lynx::fslicer::State fourSlices()
{
    auto state = lynx::fslicer::fresh ("C:\\loops\\steps.wav", 44100);
    state.slices.clear();
    for (int i = 0; i < 4; ++i)
    {
        lynx::fslicer::Slice s;
        s.name = "s" + std::to_string (i + 1);
        s.position = i * 11025;
        state.slices.push_back (s);
    }
    return state;
}

lynx::Song song (const lynx::fslicer::State& state, float volume)
{
    lynx::Song s;
    lynx::ChannelDef c;
    c.name = "Slicer";
    c.instrument = lynx::InstrumentKind::fruityslicer;
    c.samplePath = "C:\\loops\\steps.wav";
    c.pluginState = lynx::fslicer::write (state);
    c.volume = volume;
    s.channels.push_back (c);
    return s;
}

lynx::RackState::Factories factories()
{
    lynx::RackState::Factories f;
    f.samples = [] (const std::string&) { return steps(); };
    return f;
}

// the peak of the first 2000 frames a key plays (0 = silent), the note then released and rung out
float keyPeak (lynx::Instrument& instrument, int key)
{
    std::vector<float> left (2000, 0.0f), right (2000, 0.0f);
    instrument.noteOn (key, 0.78125f);
    instrument.render (left.data(), right.data(), 2000, 1.0f, 1.0f);
    instrument.noteOff (key);
    std::vector<float> tail (44100, 0.0f), tail2 (44100, 0.0f);
    for (int at = 0; at < 44100; at += 512)
        instrument.render (tail.data() + at, tail2.data() + at, std::min (512, 44100 - at), 1.0f, 1.0f);
    float peak = 0.0f;
    for (auto v : left)
        peak = std::max (peak, std::fabs (v));
    return peak;
}
} // namespace issue160

TEST (issue160_slicer_key_table_is_fls)
{
    using namespace lynx;
    auto state = issue160::fourSlices();
    auto table = fslicer::keyTable (state);
    CHECK (table.size() == 125);
    CHECK (table[60] == 0 && table[61] == 1 && table[62] == 2 && table[63] == 3); // C5 = slice 1, upwards
    CHECK (table[59] == -1 && table[64] == -1 && table[0] == -1 && table[124] == -1); // nothing below or past them

    state.slices[2].key = 72; // the SliceKeyMenu: slice 3 on C6
    state.slices[3].key = 60; // slice 4 on C5: the later slice takes the key
    table = fslicer::keyTable (state);
    CHECK (table[72] == 2 && table[62] == -1 && table[60] == 3 && table[61] == 1);

    // what the sampler voices are given
    ChannelDef def;
    fslicer::configureSampler (def, state, 4 * 11025);
    CHECK (def.sampler.slices.size() == 4 && def.sampler.sliceKeys == table);
}

TEST (issue160_slicer_keys_play_their_slices_after_an_edit)
{
    using namespace lynx;
    const auto state = issue160::fourSlices();
    const auto f = issue160::factories();
    auto first = RackState::build (issue160::song (state, 0.78f), nullptr, 44100.0, f);
    CHECK (first != nullptr && first->instruments.size() == 1);

    auto check = [] (Instrument& instrument) {
        // C5, D5, D#5: slices 1, 3, 4 (0.1, 0.3, 0.4); B4 and E5 have no slice
        const auto c5 = issue160::keyPeak (instrument, 60), d5 = issue160::keyPeak (instrument, 62), ds5 = issue160::keyPeak (instrument, 63);
        CHECK (c5 > 0.01f);
        CHECK (std::fabs (d5 / c5 - 3.0f) < 0.15f);
        CHECK (std::fabs (ds5 / c5 - 4.0f) < 0.2f);
        CHECK (issue160::keyPeak (instrument, 59) == 0.0f);
        CHECK (issue160::keyPeak (instrument, 64) == 0.0f);
    };
    check (*first->instruments[0]);

    // an edit (the channel's volume): the rack keeps the Slicer, and its keys still play their own slices
    auto second = RackState::build (issue160::song (state, 0.5f), first.get(), 44100.0, f);
    CHECK (second != nullptr && second->instruments.size() == 1 && second->instruments[0] == first->instruments[0]);
    check (*second->instruments[0]);

    // a slice's own key (FL's SliceKeyMenu) moves it: slice 3 on C6, and D5 is silent
    auto moved = state;
    moved.slices[2].key = 72;
    auto third = RackState::build (issue160::song (moved, 0.5f), second.get(), 44100.0, f);
    auto& slicer = *third->instruments[0];
    const auto c5 = issue160::keyPeak (slicer, 60);
    CHECK (issue160::keyPeak (slicer, 62) == 0.0f);
    CHECK (std::fabs (issue160::keyPeak (slicer, 72) / c5 - 3.0f) < 0.15f);
}
