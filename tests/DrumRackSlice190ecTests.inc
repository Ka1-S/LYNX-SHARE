// Round 190ec: Live's Slice to Drum Rack / Slice to New MIDI Track (core/Rack.h): one Drum Rack pad per slice from C1,
// LYNX's two slicing templates (gated with choke group 1 after the facts of Live's Default slicing; one-shot), and the
// helpers behind the Playlist command (slices in the played part, a grid of slices, the notes that replay them, the new
// track under the clip).

namespace drumrackslice190ec
{
using namespace lynx;

bool near (float a, float b, float tol = 1.0e-5f) { return std::abs (a - b) <= tol; }

// the sampler node of chain c of a drum rack (null if there is none)
const ChannelDef* padSampler (const PatcherDoc& doc, int c)
{
    const auto* r = rack::rackOf (doc);
    if (r == nullptr || c < 0 || c >= (int) r->chains.size() || r->chains[(std::size_t) c].nodes.empty())
        return nullptr;
    const auto& n = doc.nodes[(std::size_t) r->chains[(std::size_t) c].nodes.front()];
    return n.generator && n.channel.instrument == InstrumentKind::sampler ? &n.channel : nullptr;
}
} // namespace drumrackslice190ec

TEST (drumrack190ec_slice_to_drum_rack_pads_from_c1_with_their_regions)
{
    using namespace drumrackslice190ec;
    const auto gated = rack::sliceToDrumRack ("/x/loop.wav", { 0.0f, 0.25f, 0.5f }, "Loop", rack::SliceTemplate::gated, 0.75f);
    CHECK (gated.instrument == InstrumentKind::patcher && rack::isDrumRack (gated) && gated.name == "Loop");
    const auto* r = rack::rackOf (gated.patcher);
    CHECK (r != nullptr && r->chains.size() == 3);
    if (r == nullptr || r->chains.size() != 3)
        return;
    const float starts[] { 0.0f, 0.25f, 0.5f }, ends[] { 0.25f, 0.5f, 0.75f };
    for (int c = 0; c < 3; ++c)
    {
        const auto& chain = r->chains[(std::size_t) c];
        CHECK (chain.receive == rack::drumFirstPad + c && chain.play == rack::drumPlayNote && chain.choke == 1);
        CHECK (chain.name == "Loop " + std::to_string (c + 1));
        const auto* s = padSampler (gated.patcher, c);
        CHECK (s != nullptr);
        if (s == nullptr)
            continue;
        CHECK (s->samplePath == "/x/loop.wav" && near (s->sampler.regionStart, starts[c]) && near (s->sampler.regionEnd, ends[c]));
        CHECK (near (s->sampler.release, 0.05f) && s->sampler.cut); // (gated, one voice)
    }
    CHECK (rack::padNames (*r).at (37) == "Loop 2");
    // the one-shot template: no choke group, each slice plays to its end
    const auto shot = rack::sliceToDrumRack ("/x/loop.wav", { 0.0f, 0.5f }, "Loop", rack::SliceTemplate::oneShot);
    const auto* q = rack::rackOf (shot.patcher);
    CHECK (q != nullptr && q->chains.size() == 2 && q->chains[0].choke == 0 && q->chains[1].choke == 0);
    if (const auto* s = padSampler (shot.patcher, 1))
        CHECK (s->sampler.release == 0.0f && ! s->sampler.cut && near (s->sampler.regionEnd, 1.0f));
    else
        CHECK (false);
    // at most 92 pads (C1 … the top key)
    std::vector<float> many;
    for (int i = 0; i < 100; ++i)
        many.push_back ((float) i / 100.0f);
    const auto capped = rack::sliceToDrumRack ("/x/loop.wav", many, "Loop");
    CHECK (rack::rackOf (capped.patcher)->chains.size() == (std::size_t) rack::maxSlicePads && rack::maxSlicePads == 92);
    CHECK (rack::rackOf (capped.patcher)->chains.back().receive == 127);
}

TEST (drumrack190ec_slice_helpers_region_grid_and_notes)
{
    using namespace drumrackslice190ec;
    // the starts inside the played part, sorted, the part's own start first, duplicates dropped
    const auto in = rack::slicesInRegion ({ 0.9f, 0.3f, 0.1f, 0.5f, 0.5f, 0.2f }, 0.2f, 0.8f);
    CHECK (in.size() == 3 && near (in[0], 0.2f) && near (in[1], 0.3f) && near (in[2], 0.5f));
    // a slice every second of a 4 s file, and every half second of its second half; unknown division: one slice
    const auto grid = rack::gridSlices (4.0, 0.0f, 1.0f, 1.0);
    CHECK (grid.size() == 4 && near (grid[1], 0.25f) && near (grid[3], 0.75f));
    const auto half = rack::gridSlices (4.0, 0.5f, 1.0f, 0.5);
    CHECK (half.size() == 4 && near (half[0], 0.5f) && near (half[1], 0.625f) && near (half[3], 0.875f));
    CHECK (rack::gridSlices (4.0, 0.25f, 1.0f, 0.0).size() == 1 && rack::gridSlices (0.0, 0.0f, 1.0f, 1.0).size() == 1);
    // the notes: slice i on C1 + i from its start to the next one's, the clip's ticks (here a file of 1000 ticks,
    // the clip starting 100 ticks in: what plays before the clip is cut at 0)
    const auto tickOf = [] (float f) { return (double) f * 1000.0 - 100.0; };
    const auto notes = rack::sliceNotes ({ 0.0f, 0.25f, 0.5f }, 0.75f, tickOf, 7);
    CHECK (notes.size() == 3);
    if (notes.size() == 3)
    {
        CHECK (notes[0].start == 0 && notes[0].length == 150 && notes[0].key == 36 && notes[0].channel == 7);
        CHECK (notes[1].start == 150 && notes[1].length == 250 && notes[1].key == 37);
        CHECK (notes[2].start == 400 && notes[2].length == 250 && notes[2].key == 38);
    }
    // a slice that ends before the clip starts plays nothing
    CHECK (rack::sliceNotes ({ 0.0f, 0.05f, 0.5f }, 1.0f, tickOf, 0).size() == 2);
}

TEST (drumrack190ec_slice_to_new_midi_track_lands_under_the_clip)
{
    using namespace drumrackslice190ec;
    Song song;
    song.mixer.resize (8); // (Master and inserts: the new channel claims a free one)
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
    song.playlist.push_back (clip);
    Clip other;
    other.track = 3;
    song.playlist.push_back (other);

    const auto drum = rack::sliceToDrumRack ("/x/loop.wav", { 0.0f, 0.5f }, "loop");
    const auto notes = rack::sliceNotes ({ 0.0f, 0.5f }, 1.0f, [] (float f) { return (double) f * 8.0 * ppq; }, 0);
    // not an audio clip, or no notes: nothing changes
    auto copy = song;
    CHECK (rack::addSliceTrack (copy, 1, drum, notes, "loop") == -1 && copy.channels.size() == 1 && copy.playlist.size() == 2);
    CHECK (rack::addSliceTrack (copy, 0, drum, {}, "loop") == -1 && copy.patterns.size() == song.patterns.size());

    const auto patterns = song.patterns.size();
    const auto channel = rack::addSliceTrack (song, 0, drum, notes, "loop");
    CHECK (channel == 1 && song.channels.size() == 2 && rack::isDrumRack (song.channels[1]));
    CHECK (song.channels[1].mixerTrack > 0); // (its own insert)
    CHECK (song.patterns.size() == patterns + 1);
    const auto& p = song.patterns.back();
    CHECK (p.name == "loop" && p.notes.size() == 2 && p.notes[0].channel == 1 && p.notes[1].key == 37 && p.notes[1].start == 4 * ppq);
    // the pattern clip: under the audio clip, as long; the track that was below moves down
    CHECK (song.playlist[1].track == 4);
    const auto& added = song.playlist.back();
    CHECK (added.kind == ClipKind::pattern && added.pattern == (int) song.patterns.size() - 1 && added.track == 3 && added.start == clip.start
           && added.length == clip.length);
}
