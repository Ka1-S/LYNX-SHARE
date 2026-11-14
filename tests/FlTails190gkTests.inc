// Round 190gk: FL value-3+ tails of Gross Beat, Edison and the Sampler waveform menu, and the tlst bug.
// Evidence: /mnt/e/lynx-work/fltails190gk/HANDOFF.md.

namespace fltails190gk
{
// a 16-bit mono WAV of `frames` frames with `starts.size()` regions (cue ids 1…n, ltxt lengths) and an optional tlst
inline std::vector<std::uint8_t> regionsWav (const std::vector<std::uint32_t>& starts, std::uint32_t frames,
                                             const std::vector<std::uint8_t>& tlstBody)
{
    using flfix190ez::put32;
    using flfix190ez::tag;
    std::vector<std::uint8_t> o;
    tag (o, "RIFF");
    put32 (o, 0);
    tag (o, "WAVE");
    tag (o, "fmt ");
    put32 (o, 16);
    o.insert (o.end(), { 1, 0, 1, 0 });
    put32 (o, 44100);
    put32 (o, 88200);
    o.insert (o.end(), { 2, 0, 16, 0 });
    tag (o, "data");
    put32 (o, frames * 2);
    o.insert (o.end(), frames * 2, 0);
    tag (o, "cue ");
    put32 (o, (std::uint32_t) (4 + 24 * starts.size()));
    put32 (o, (std::uint32_t) starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i)
    {
        put32 (o, (std::uint32_t) i + 1);
        put32 (o, starts[i]);
        tag (o, "data");
        put32 (o, 0);
        put32 (o, 0);
        put32 (o, starts[i]);
    }
    tag (o, "LIST");
    put32 (o, (std::uint32_t) (4 + 28 * starts.size()));
    tag (o, "adtl");
    for (std::size_t i = 0; i < starts.size(); ++i)
    {
        tag (o, "ltxt");
        put32 (o, 20);
        put32 (o, (std::uint32_t) i + 1);
        put32 (o, 50);
        tag (o, "rgn ");
        put32 (o, 0);
        put32 (o, 0);
    }
    if (! tlstBody.empty())
    {
        tag (o, "tlst");
        put32 (o, (std::uint32_t) tlstBody.size());
        o.insert (o.end(), tlstBody.begin(), tlstBody.end());
    }
    const auto riff = (std::uint32_t) (o.size() - 8);
    for (int i = 0; i < 4; ++i)
        o[4 + (std::size_t) i] = (std::uint8_t) (riff >> (8 * i));
    return o;
}

// a tlst entry with the two words as given
inline void entry (std::vector<std::uint8_t>& b, std::uint32_t w4, std::uint32_t w8, int key)
{
    flfix190ez::tag (b, "cue ");
    flfix190ez::put32 (b, w4);
    flfix190ez::put32 (b, w8);
    b.insert (b.end(), { 0xff, (std::uint8_t) key, 0x90, 0 });
    flfix190ez::put32 (b, 0);
    flfix190ez::put32 (b, 0);
}
} // namespace fltails190gk

// ---- (c) Edison's tlst writer / reader and Slicex's reader agree on 2+ keyed regions --------------------------------
// FL's own layout, from FL-made files with several entries (r167l: FL /R render with 3 markers, cue ids 0x10000…,
// entries {0xffff, 1}, {0x10000, 1}, {0x10001, 1}) and the Edison writer (REA 190h): {'cue ', cue id − 1, 1, …}.
TEST (fltails190gk_edison_tlst_is_fls_layout_for_several_regions)
{
    using namespace lynx::audioedit;
    Document doc;
    doc.audio.sampleRate = 44100.0;
    doc.audio.channels = { std::vector<float> (4000, 0.1f) };
    doc.markers.push_back ({ 0, 1000, "a", "", 0.0, 38 });
    doc.markers.push_back ({ 1000, 2000, "b", "", 0.0, -1 });
    doc.markers.push_back ({ 2000, 3000, "c", "", 0.0, 61 });
    doc.markers.push_back ({ 3000, 3999, "d", "", 0.0, 70 });
    const auto wav = encodeWav (doc, WavFormat::int16);
    const auto t = flfix190ez::chunk (wav, "tlst");
    CHECK (t.size() == 4 + 3 * 24 && t[0] == 3);
    if (t.size() != 4 + 3 * 24)
        return;
    // entry k: second word = cue id − 1 (ids 1, 3, 4), third word = 1
    const std::uint32_t ids[] = { 1, 3, 4 };
    const int keys[] = { 38, 61, 70 };
    for (int k = 0; k < 3; ++k)
    {
        const auto* e = t.data() + 4 + 24 * k;
        CHECK (std::memcmp (e, "cue ", 4) == 0);
        CHECK ((std::uint32_t) (e[4] | e[5] << 8) == ids[k] - 1 && e[6] == 0 && e[7] == 0);
        CHECK (e[8] == 1 && e[9] == 0 && e[10] == 0 && e[11] == 0);
        CHECK (e[12] == 0xff && e[13] == keys[k] && e[14] == 0x90 && e[15] == 0);
    }

    // Edison reads its own file back
    Document back;
    CHECK (decodeWav (wav.data(), wav.size(), back) && back.markers.size() == 4);
    if (back.markers.size() == 4)
        CHECK (back.markers[0].keyNum == 38 && back.markers[1].keyNum == -1 && back.markers[2].keyNum == 61 && back.markers[3].keyNum == 70);

    // and Slicex (FL's reader layout, 190r) reads the same keys from Edison's file: the bug was {0, id} vs {id − 1, 1}
    std::int64_t frames = 0;
    const auto regions = lynx::slicex::wavRegions (wav, frames);
    CHECK (regions.size() == 4);
    if (regions.size() == 4)
        CHECK (regions[0].key == 38 && regions[1].key == -1 && regions[2].key == 61 && regions[3].key == 70);

    // Slicex's writer (Assign current region to this key) and Edison's reader agree too
    const auto keyed = lynx::slicex::withRegionKeys (wav, { -1, 40, -1, 72 });
    Document fromSlicex;
    CHECK (! keyed.empty() && decodeWav (keyed.data(), keyed.size(), fromSlicex) && fromSlicex.markers.size() == 4);
    if (fromSlicex.markers.size() == 4)
        CHECK (fromSlicex.markers[0].keyNum == -1 && fromSlicex.markers[1].keyNum == 40 && fromSlicex.markers[2].keyNum == -1
               && fromSlicex.markers[3].keyNum == 72);
}

TEST (fltails190gk_tlst_readers_take_fls_entries_and_027s_old_ones)
{
    using namespace lynx::audioedit;
    {
        // FL's layout (as in FL's multi-entry render): regions 2 and 3 keyed
        std::vector<std::uint8_t> b;
        flfix190ez::put32 (b, 2);
        fltails190gk::entry (b, 1, 1, 50);
        fltails190gk::entry (b, 2, 1, 51);
        const auto wav = fltails190gk::regionsWav ({ 0, 100, 200 }, 400, b);
        Document doc;
        CHECK (decodeWav (wav.data(), wav.size(), doc) && doc.markers.size() == 3);
        if (doc.markers.size() == 3)
            CHECK (doc.markers[0].keyNum == -1 && doc.markers[1].keyNum == 50 && doc.markers[2].keyNum == 51);
        std::int64_t frames = 0;
        const auto r = lynx::slicex::wavRegions (wav, frames);
        CHECK (r.size() == 3);
        if (r.size() == 3)
            CHECK (r[0].key == -1 && r[1].key == 50 && r[2].key == 51);
        // saved again by Edison: byte for byte the same entries
        CHECK (flfix190ez::chunk (encodeWav (doc, WavFormat::int16), "tlst") == b);
    }
    {
        // a WAV LYNX 0.1.27's Edison saved: {0, cue id}; both readers still find the right regions
        std::vector<std::uint8_t> b;
        flfix190ez::put32 (b, 2);
        fltails190gk::entry (b, 0, 2, 50);
        fltails190gk::entry (b, 0, 3, 51);
        const auto wav = fltails190gk::regionsWav ({ 0, 100, 200 }, 400, b);
        Document doc;
        CHECK (decodeWav (wav.data(), wav.size(), doc) && doc.markers.size() == 3);
        if (doc.markers.size() == 3)
            CHECK (doc.markers[0].keyNum == -1 && doc.markers[1].keyNum == 50 && doc.markers[2].keyNum == 51);
        std::int64_t frames = 0;
        const auto r = lynx::slicex::wavRegions (wav, frames);
        CHECK (r.size() == 3);
        if (r.size() == 3)
            CHECK (r[0].key == -1 && r[1].key == 50 && r[2].key == 51);
    }
}

// FL's own 3-entry render (not in the repo: FL's audio). LYNX_FL_TLST3_WAV=/mnt/e/lynx-work/r167l/fl_take1_l_all.wav
TEST (fltails190gk_fl_three_marker_render_reads_in_both)
{
    const char* path = std::getenv ("LYNX_FL_TLST3_WAV");
    if (path == nullptr)
        return;
    std::ifstream in (path, std::ios::binary);
    const std::vector<std::uint8_t> fl ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    std::int64_t frames = 0;
    const auto r = lynx::slicex::wavRegions (fl, frames);
    CHECK (r.size() == 3);
    if (r.size() == 3)
        CHECK (r[0].key == 60 && r[1].key == 61 && r[2].key == 62);
    lynx::audioedit::Document doc;
    CHECK (lynx::audioedit::decodeWav (fl.data(), fl.size(), doc) && doc.markers.size() == 3);
    if (doc.markers.size() == 3)
        CHECK (doc.markers[0].keyNum == 60 && doc.markers[1].keyNum == 61 && doc.markers[2].keyNum == 62);
    std::printf ("fltails190gk: FL render %zu bytes, %zu regions, keys %d %d %d\n", fl.size(), r.size(),
                 r.size() > 0 ? r[0].key : -9, r.size() > 1 ? r[1].key : -9, r.size() > 2 ? r[2].key : -9);
}

// ---- (a) Gross Beat: a slot picked by a MIDI note becomes the slot's parameter (the grid follows) -------------------
namespace fltails190gk
{
// the Gross Beat slot over 60 blocks; `beforeBlock` may send notes and change the parameters the block sees
inline std::vector<float> gbRender (const lynx::EffectSlot& slot, const std::function<void (lynx::Effect&, int, lynx::EffectParams&)>& beforeBlock,
                                    lynx::grossbeat::MidiPicks** picksOut = nullptr, std::unique_ptr<lynx::Effect>* keep = nullptr)
{
    auto fx = lynx::makeEffect (slot);
    std::vector<float> out;
    if (fx == nullptr)
        return out;
    fx->prepare (44100.0);
    auto params = slot.params;
    std::vector<float> l (512), r (512);
    const double bpm = 120.0, ticksPerSample = bpm / 60.0 * lynx::ppq / 44100.0;
    double ticks = 0.0;
    std::int64_t n = 0;
    for (int b = 0; b < 60; ++b)
    {
        for (int i = 0; i < 512; ++i, ++n)
            l[(std::size_t) i] = r[(std::size_t) i] = 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (double) n / 44100.0);
        fx->setSongPosition (ticks);
        fx->setPlaying (true);
        if (beforeBlock)
            beforeBlock (*fx, b, params);
        fx->process (l.data(), r.data(), 512, params, bpm);
        out.insert (out.end(), l.begin(), l.end());
        ticks += 512.0 * ticksPerSample;
    }
    if (picksOut != nullptr)
        *picksOut = lynx::grossbeat::midiPicksOf (fx.get());
    if (keep != nullptr)
        *keep = std::move (fx);
    return out;
}
} // namespace fltails190gk

TEST (fltails190gk_gb_slot_parameter_round_trips)
{
    using namespace lynx;
    for (int kind = 0; kind < 2; ++kind)
        for (int s = 0; s < grossbeat::slotsPerKind; ++s)
            CHECK (grossbeat::rawFromParam (kind, grossbeat::paramFromRaw (kind, s)) == s);
}

TEST (fltails190gk_gb_midi_pick_is_published_for_the_parameter)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::grossBeat);
    grossbeat::MidiPicks* picks = nullptr;
    std::unique_ptr<Effect> fx;
    fltails190gk::gbRender (slot, [] (Effect& e, int b, EffectParams&) {
        if (b == 0)
            e.midiNote (60 + 7, 100, 0);   // time slot 8
        if (b == 1)
            e.midiNote (24 + 4, 100, 0);   // volume slot 5
    }, &picks, &fx);
    CHECK (picks != nullptr);
    if (picks == nullptr)
        return;
    CHECK (picks->timeSlot.load() == 7 && picks->volumeSlot.load() == 4 && picks->serial.load() == 2);
    // the parameter changes that make the Song show (and save) the picked slots, as FL's click would
    const auto changes = grossbeat::paramsForPicks (*picks, slot.params);
    CHECK (changes.size() == 2);
    if (changes.size() == 2)
        CHECK (changes[0].first == 0 && grossbeat::rawFromParam (0, changes[0].second) == 7
               && changes[1].first == 1 && grossbeat::rawFromParam (1, changes[1].second) == 4);
    auto after = slot.params;
    after[0] = grossbeat::paramFromRaw (0, 7);
    after[1] = grossbeat::paramFromRaw (1, 4);
    CHECK (grossbeat::paramsForPicks (*picks, after).empty());
    // not a Gross Beat: nothing to publish
    auto other = makeEffect (makeEffectSlot (EffectKind::delay));
    CHECK (other == nullptr || grossbeat::midiPicksOf (other.get()) == nullptr);
}

TEST (fltails190gk_gb_parameter_echo_of_a_pick_changes_nothing)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::grossBeat);
    // reference: the notes alone, the parameter never written back (0.1.27's behaviour)
    const auto notesOnly = fltails190gk::gbRender (slot, [] (Effect& e, int b, EffectParams&) {
        if (b == 0)
            e.midiNote (60 + 5, 100, 0);
        if (b == 2)
            e.midiNote (60 + 7, 100, 0);
    });
    // the message thread writes the picks back late and out of date: 5 arrives after 7 was picked, then 7
    const auto echoed = fltails190gk::gbRender (slot, [] (Effect& e, int b, EffectParams& p) {
        if (b == 0)
            e.midiNote (60 + 5, 100, 0);
        if (b == 2)
            e.midiNote (60 + 7, 100, 0);
        if (b == 10)
            p[0] = grossbeat::paramFromRaw (0, 5);
        if (b == 12)
            p[0] = grossbeat::paramFromRaw (0, 7);
    });
    CHECK (fled190ee::diff (notesOnly, echoed) == 0.0);
    // a real change of the parameter after a pick (a click, automation) still applies, as a note at that block would
    const auto clicked = fltails190gk::gbRender (slot, [] (Effect& e, int b, EffectParams& p) {
        if (b == 0)
            e.midiNote (60 + 7, 100, 0);
        if (b == 20)
            p[0] = grossbeat::paramFromRaw (0, 3);
    });
    const auto noted = fltails190gk::gbRender (slot, [] (Effect& e, int b, EffectParams&) {
        if (b == 0)
            e.midiNote (60 + 7, 100, 0);
        if (b == 20)
            e.midiNote (60 + 3, 100, 0);
    });
    CHECK (fled190ee::diff (clicked, noted) == 0.0);
    CHECK (fled190ee::diff (clicked, notesOnly) > 1.0e-3);
}
