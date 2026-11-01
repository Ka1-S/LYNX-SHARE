// Round 167j: FL's Options › Project › "Panning law" and the mixer's pan fold (docs/analise/REA_rodada167j.md §1)

TEST (rea167j_panning_law_flp)
{
    // .flp 0x17 = 2 × FL's pan-law mode (FLEngine's project writer FUN_00f40840): 2 = triangular, 0 = circular
    FlpBuilder b;
    b.u8 (0x17);
    b.u8 (2);
    CHECK (lynx::importFlp (flp::parseProject (b.bytes)).panTriangular);
    CHECK (! lynx::importFlp (flp::parseProject (FlpBuilder().bytes)).panTriangular);
}

TEST (rea167j_mixer_pan_folds_the_other_side_through_the_renderer)
{
    // a stereo input (L 0.5, R −0.25) through insert 3 panned hard right: FL puts both inputs on the right at half
    // the side gain (circular: √2·½ each; triangular: ½ each), the left output is silent
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    for (auto& p : song.patterns)
        p.notes.clear();
    song.mixer = lynx::Song::defaultMixer (4);
    song.mixer[3].pan = 1.0f;
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    std::vector<float> inL (256, 0.5f), inR (256, -0.25f), l (256), r (256);
    auto run = [&] (const lynx::RackState* st) {
        for (int i = 0; i < 20; ++i) // past the glides (0.001 per sample)
        {
            renderer.setMetronomeInput (inL.data(), inR.data(), 3);
            renderer.process (st, false, 0.0, 256, l.data(), r.data());
        }
    };
    auto circular = lynx::RackState::build (song, nullptr, 48000.0);
    run (circular.get());
    CHECK (std::abs (l[200]) < 1e-5f && std::abs (r[200] - 0.70710677f * 0.25f) < 1e-4f);

    song.panTriangular = true;
    auto triangular = lynx::RackState::build (song, circular.get(), 48000.0);
    run (triangular.get());
    CHECK (std::abs (l[200]) < 1e-5f && std::abs (r[200] - 0.5f * 0.25f) < 1e-4f);

    // swap after the pan (FL swaps the matrix rows): hard right + swap = everything on the left
    song.mixer[3].swapChannels = true;
    auto swapped = lynx::RackState::build (song, triangular.get(), 48000.0);
    run (swapped.get());
    CHECK (std::abs (r[200]) < 1e-5f && std::abs (l[200] - 0.125f) < 1e-4f);
}
