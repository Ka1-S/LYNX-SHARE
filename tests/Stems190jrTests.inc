// --- Extract stems options, round 190jr (BUILD-LIST 14: FL's form + the model choice; core/StemSplit.h) -------------

#include "core/StemSplit.h"

TEST (stems190jr_passes_and_downloads)
{
    using namespace lynx::stemsplit;
    Options o;
    // standard: one pass of the 4-stem graph, every stem; guitar / piano are ignored without the 6-stem model
    auto passes = passesFor (o);
    CHECK (passes.size() == 1 && passes[0].model == &modelFile (Model::standard) && passes[0].take == std::vector<int> ({ 0, 1, 2, 3 }));
    CHECK (passes[0].model->outputs == 4 && passes[0].model->bytes == 165612636);
    o.stems = { false, false, true, true, true, true };
    CHECK (passesFor (o)[0].take == std::vector<int> ({ 2, 3 }));
    // 6 stems: one pass, guitar and piano too
    o.model = Model::sixStems;
    passes = passesFor (o);
    CHECK (passes.size() == 1 && passes[0].model->outputs == 6 && passes[0].take == std::vector<int> ({ 2, 3, 4, 5 }));
    // fine-tuned: one pass per wanted stem, each graph giving its own stem
    o.model = Model::fineTuned;
    o.stems = { true, false, false, true, true, true };
    passes = passesFor (o);
    CHECK (passes.size() == 2 && passes[0].take == std::vector<int> ({ 0 }) && passes[1].take == std::vector<int> ({ 3 }));
    CHECK (std::string (passes[0].model->file) == "htdemucs_ft_drums_fp16weights.onnx" && std::string (passes[1].model->file) == "htdemucs_ft_vocals_fp16weights.onnx");
    // nothing ticked: no pass
    o.stems = {};
    CHECK (passesFor (o).empty());
    // what is downloaded: each graph once, the installed ones not
    o.stems = { true, true, true, true, false, false };
    passes = passesFor (o);
    CHECK (downloadBytes (passes, {}) == 4 * 165612636LL);
    CHECK (downloadBytes (passes, [] (const ModelFile& m) { return std::string (m.file).find ("drums") != std::string::npos; }) == 3 * 165612636LL);
    CHECK (downloadBytes (passesFor (Options {}), [] (const ModelFile&) { return true; }) == 0);
    // every pinned graph: a revision, a 64-hex hash, a size
    for (const auto* m : { &modelFile (Model::standard), &modelFile (Model::sixStems), &modelFile (Model::fineTuned, 0), &modelFile (Model::fineTuned, 3) })
        CHECK (std::string (m->url).find ("/resolve/") != std::string::npos && std::string (m->sha256).size() == 64 && m->bytes > 100000000);
}

TEST (stems190jr_options_text_and_threads)
{
    using namespace lynx::stemsplit;
    Options o;
    CHECK (optionsFromText (toText (o)) == o);
    o.stems = { true, false, true, false, true, false };
    o.model = Model::fineTuned;
    o.limitCpu = o.safeMode = true;
    o.whenDone = WhenDone::nothing;
    o.groupTracks = o.newInserts = false;
    CHECK (toText (o) == "stems=101010 model=2 cpu=1 safe=1 done=2 group=0 inserts=0");
    CHECK (optionsFromText (toText (o)) == o);
    CHECK (optionsFromText ("") == Options {} && optionsFromText ("model=9 done=x junk stems=") == Options {});
    // FL's defaults (the form's DFM): every stem ticked, group and new mixer tracks on, Limit CPU and Safe mode off
    const Options fresh;
    CHECK (fresh.stems[0] && fresh.stems[3] && fresh.groupTracks && fresh.newInserts && ! fresh.limitCpu && ! fresh.safeMode);
    // Limit CPU usage: half the threads; Safe mode: one
    Options limit;
    limit.limitCpu = true;
    CHECK (separationThreads (6, 12, Options {}) == 5 && separationThreads (6, 12, limit) == 2 && separationThreads (2, 4, limit) == 1);
    Options safe;
    safe.safeMode = true;
    CHECK (separationThreads (16, 32, safe) == 1);
}

TEST (stems190jr_six_outputs_and_placement)
{
    using namespace lynx;
    using namespace lynx::stemsplit;
    // a fake 6-stem graph: stem s = input × (s + 1)
    std::vector<float> left ((std::size_t) segmentFrames + 5, 0.25f), right ((std::size_t) segmentFrames + 5, -0.5f);
    std::vector<std::array<std::vector<float>, 2>> stems (6);
    CHECK (separateInto (left, right, 6, [] (const float* in, float* out) {
        for (int s = 0; s < 6; ++s)
            for (std::int64_t i = 0; i < 2 * segmentFrames; ++i)
                out[s * 2 * segmentFrames + i] = in[i] * (float) (s + 1);
        return true;
    }, {}, stems));
    CHECK (stems[5][0].size() == left.size() && std::abs (stems[5][0][100] - 1.5f) < 1e-4f && std::abs (stems[4][1][segmentFrames] + 2.5f) < 1e-4f);

    // placement: only the stems given, grouped under the source's track, on the source's insert, the source clip muted
    Song song;
    ChannelDef channel;
    channel.name = "song";
    channel.instrument = InstrumentKind::audioClip;
    channel.samplePath = "/a/song.wav";
    channel.mixerTrack = 3;
    song.channels.push_back (channel);
    Clip source;
    source.kind = ClipKind::audio;
    source.channel = (int) song.channels.size() - 1;
    source.track = 1;
    source.length = 960;
    song.playlist.push_back (source);
    Placement place;
    place.newInserts = false;
    place.groupTracks = true;
    place.whenDone = WhenDone::muteClip;
    CHECK (! addStemClips (song, 0, "/a/song.wav", {}, place));
    CHECK (addStemClips (song, 0, "/a/song.wav", { { "vocals", "/v.wav" }, { "piano", "/p.wav" } }, place));
    CHECK (song.playlist.size() == 3 && song.playlist[0].muted && ! song.playlistTracks[1].muted);
    CHECK (song.playlist[1].track == 2 && song.playlist[2].track == 3 && song.playlistTracks[2].grouped && song.playlistTracks[3].grouped);
    CHECK (song.channels[(std::size_t) song.playlist[2].channel].name == "song - piano" && song.channels[(std::size_t) song.playlist[2].channel].mixerTrack == 3);
    // Mute source track; new inserts
    Song other = song;
    other.playlist.resize (1);
    other.playlist[0].muted = false;
    Placement track;
    track.whenDone = WhenDone::muteTrack;
    CHECK (addStemClips (other, 0, "/a/song.wav", { { "drums", "/d.wav" } }, track));
    CHECK (! other.playlist[0].muted && other.playlistTracks[1].muted && ! other.playlistTracks[2].grouped
           && other.channels[(std::size_t) other.playlist[1].channel].mixerTrack != 3);
}
