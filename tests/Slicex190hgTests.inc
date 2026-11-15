// Round 190hg: Slicex follows slide notes and the channel's porta (#157), as FL's Slicex reads its voices' Final.Pitch
// at every render call (docs/analise/REA_rodada190hg.md §1)
#include "core/HostSlide.h"
#include "core/SlicexInstrument.h"

namespace slicex190hg
{
// a 1 s mono 441 Hz sine at 44.1 kHz with four cue points (four regions of 11025 frames)
inline std::shared_ptr<lynx::SampleData> sine()
{
    auto s = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 44100; ++i)
        s->left.push_back (0.5f * (float) std::sin (2.0 * 3.141592653589793 * 441.0 * (double) i / 44100.0));
    s->sampleRate = 44100.0;
    s->markerStarts = { 0, 11025, 22050, 33075 };
    return s;
}

// sign changes of the left channel in [from, to)
inline int crossings (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    int n = 0;
    for (std::size_t i = from + 1; i < to && i < x.size(); ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++n;
    return n;
}
} // namespace slicex190hg

TEST (slicex190hg_a_slide_note_bends_the_sounding_slice)
{
    using namespace lynx;
    const auto a = slicex190hg::sine();
    auto def = slicex190l::channel();
    auto inst = makeSlicexInstrument (a, nullptr);
    CHECK (inst->followsSlides());
    CHECK (inst->followsChannelPorta());
    inst->prepare (44100.0);
    inst->configure (def, 120.0);
    inst->settleNew();

    const int key = slicex::autoBaseKey (4); // the first region, at its own pitch
    std::vector<float> l (6144, 0.0f), r (6144, 0.0f);
    inst->setNextNoteExpression (0.0f, 0.0f, false);
    inst->noteOn (key, 0.8f);
    inst->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    // a slide note an octave up over 512 frames: the slice keeps playing (no new voice) and ends an octave higher
    inst->slideTo (key + 12, 0.0f, 512.0f);
    inst->render (l.data() + 2048, r.data() + 2048, 4096, 1.0f, 1.0f);

    const auto before = slicex190hg::crossings (l, 200, 2048);  // 441 Hz: about 37 sign changes in 1848 frames
    const auto after = slicex190hg::crossings (l, 2048 + 600, 2048 + 600 + 1848);
    CHECK (before >= 35 && before <= 39);
    CHECK (after >= 72 && after <= 76);                         // 882 Hz
    const auto during = slicex190hg::crossings (l, 2048, 2048 + 512);
    CHECK (during > 10 && during < 21);                         // gliding between the two

    // the next note starts at its own pitch again (the slide belongs to the voice it moved)
    inst->noteOff (key);
    std::vector<float> l2 (4096, 0.0f), r2 (4096, 0.0f);
    inst->render (l2.data(), r2.data(), 4096, 1.0f, 1.0f);
    std::fill (l2.begin(), l2.end(), 0.0f);
    inst->noteOn (key, 0.8f);
    inst->render (l2.data(), r2.data(), 2048, 1.0f, 1.0f);
    const auto again = slicex190hg::crossings (l2, 200, 2048);
    CHECK (again >= 35 && again <= 39);
}

TEST (slicex190hg_channel_porta_glides_a_new_slice_in)
{
    using namespace lynx;
    const auto a = slicex190hg::sine();
    auto inst = makeSlicexInstrument (a, nullptr);
    inst->prepare (44100.0);
    inst->configure (slicex190l::channel(), 120.0);
    inst->settleNew();

    const int key = slicex::autoBaseKey (4);
    // the rack's porta: the note starts an octave below its pitch and glides back over 1024 frames
    inst->setNextNoteExpression (0.0f, -1200.0f, true);
    HostSlide::pendingGlide() = { -1200.0f, 1024.0f, false };
    inst->noteOn (key, 0.8f);
    CHECK (HostSlide::pendingGlide().taken);
    HostSlide::pendingGlide() = {};
    std::vector<float> l (6144, 0.0f), r (6144, 0.0f);
    inst->render (l.data(), r.data(), 6144, 1.0f, 1.0f);
    const auto start = slicex190hg::crossings (l, 0, 256);     // about 220 Hz at first
    const auto end = slicex190hg::crossings (l, 2048, 2048 + 1848);
    CHECK (start <= 4);
    CHECK (end >= 35 && end <= 39);                            // its own 441 Hz once the glide is over
}
