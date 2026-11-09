// --- Round 182e: Peak Controller outputs in an imported Patcher (issue #111) --------------------------------------------

TEST (flp182e_patcher_peak_controller_outputs_keep_fl_numbers)
{
    // FL's Peak Controller names its outputs from the table {"Peak+LFO", "Peak", "LFO"} and its tick computes them in that
    // order (REA_rodada17 §5.1 [DECOMP], same DLL hash in FL 2026 today). A Patcher cable from FL output k (port
    // 0x10000000 + k) must leave LYNX's output k, as the mixer links do. Before, 0/1/2 became 1/2/0, so issue #111's
    // wires from "Peak" read the unused LFO and the driven EQ bands stayed at their minimum.
    using namespace patcher141;
    FlBlob blob;
    std::vector<std::uint8_t> balance;
    FlBlob::put (balance, (std::int32_t) 1);
    FlBlob::put (balance, (std::int32_t) 0);
    FlBlob::put (balance, (std::int32_t) 256);
    for (std::uint64_t k = 0; k < 3; ++k)
        blob.node (2, 100 + k, "Fruity Balance", balance, { { 0, 0, 1000 + 10 * k, true }, { 0, 0, 1001 + 10 * k, false }, { 2, 1, 1002 + 10 * k, true } });
    blob.node (2, 200, "Fruity Peak Controller", std::vector<std::uint8_t> (64, 0),
               { { 0, 0, 2000, true }, { 2, 0x10000000, 2001, false }, { 2, 0x10000001, 2002, false }, { 2, 0x10000002, 2003, false } });
    blob.pin (3001, 0, true, 0, "Audio input");
    blob.pin (3002, 0, false, 0, "Audio output");
    blob.cable (1000, 3001, 1.0f, 0);
    blob.cable (3002, 1001, 1.0f, 0);
    blob.cable (2000, 3001, 1.0f, 0);
    for (std::uint64_t k = 0; k < 3; ++k)
        blob.cable (1002 + 10 * k, 2001 + k, 1.0f, 2); // FL output k → Balance k's volume

    flp::Project project;
    project.tempo = 120.0;
    flp::MixerInsert insert;
    insert.index = 1;
    flp::MixerInsert::Slot s;
    s.plugin = "Patcher";
    s.state = blob.finish();
    insert.slots.push_back (s);
    project.mixer.push_back (insert);
    const auto song = lynx::importFlp (project);
    CHECK (song.mixer.size() > 1 && song.mixer[1].effects.size() == 1);
    if (song.mixer.size() < 2 || song.mixer[1].effects.empty())
        return;
    const auto& doc = song.mixer[1].effects[0].patcher;
    CHECK (doc.nodes.size() == 4 && doc.nodes[3].effect.kind == lynx::EffectKind::peakController);
    int found = 0;
    for (const auto& w : doc.wires)
        if (w.kind == lynx::PatchWire::Kind::control && w.from == 3)
        {
            CHECK (w.fromPort == w.to); // Balance k is node k
            ++found;
        }
    CHECK (found == 3);
}
