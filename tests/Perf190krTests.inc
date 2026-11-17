// Round 190kr (0.1.30, beat FL on CPU): the FL-native mixer effects' kernels rearranged for speed must render exactly
// what the plain loops rendered. Each engine keeps its old loop as the reference and checks the new kernel against it
// on random filters, states and block sizes (hlfx::kernelsMatchReference: Maximus's biquad and smoother cascades and
// its two-lane half-band stages; peqfl::stagesMatchReference: Parametric EQ 2's stage cascade and its two-lane
// half-band resamplers). The project-level proof (perfbench output hashes, the per-effect hash harness over every
// FL-native slot of the demo songs) is in the round's notes; this pins the kernels on every compiler / CPU the suite
// runs on.

TEST (perf190kr_maximus_kernels_match_the_plain_loops)
{
    int same = 0;
    for (std::uint32_t seed = 1; seed <= 40; ++seed)
        if (lynx::hlfx::kernelsMatchReference (seed * 2654435761u))
            ++same;
        else
            std::printf ("  Maximus kernels differ from the plain loops (seed %u)\n", seed);
    CHECK (same == 40);
}

TEST (perf190kr_parametric_eq_cascade_matches_the_plain_loop)
{
    int same = 0;
    for (std::uint32_t seed = 1; seed <= 40; ++seed)
        if (lynx::peqfl::stagesMatchReference (seed * 2246822519u))
            ++same;
        else
            std::printf ("  Parametric EQ 2 cascade differs from the plain loop (seed %u)\n", seed);
    CHECK (same == 40);
}
