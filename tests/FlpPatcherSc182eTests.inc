// --- Round 182e: the Patcher's side-chain pin on FL import (issue #112) -------------------------------------------------

TEST (flp182e_patcher_sidechain_pin_is_an_index_into_the_inputs)
{
    // FL's Patcher side-chain pin carries the bus k of the insert's input list [itself, the tracks routed to it in
    // ascending order], as the other effects' side-chain index (REA_rodada22 §1.2). Its name is only the source track's
    // name when saved: in FL's "Astes - Bien Duro" the pin "7 Sidechain" on bus 1 (insert 8) and bus 4 (insert 17) is
    // track 81, named "7 Sidechain"; issue #112's pin is named "Master". REA round 182e
    using namespace patcher141;
    auto importWithPin = [] (const std::string& name, int bus) {
        FlBlob blob;
        blob.node (2, 101, "Fruity Peak Controller", std::vector<std::uint8_t> (64, 0), { { 0, 0, 1011, true }, { 2, 0x10000000, 1012, false } });
        blob.pin (2001, 0, true, 0, "Audio input");
        blob.pin (2002, 0, false, 0, "Audio output");
        blob.pin (2003, 0, true, bus, name);
        blob.cable (2002, 2001, 1.0f, 0);
        blob.cable (1011, 2003, 1.0f, 0);
        flp::Project project;
        project.tempo = 120.0;
        for (int i = 0; i <= 7; ++i)
        {
            flp::MixerInsert insert;
            insert.index = i;
            insert.name = "Insert " + std::to_string (i);
            insert.hasRoutes = true;
            if (i == 2)
                insert.routes = { 0, 5 }; // audio + side-chain into insert 5
            else if (i == 4)
                insert.routes = { 5 };    // side-chain only
            else if (i > 0)
                insert.routes = { 0 };
            if (i == 5)
            {
                flp::MixerInsert::Slot s;
                s.plugin = "Patcher";
                s.state = blob.finish();
                insert.slots.push_back (s);
            }
            project.mixer.push_back (insert);
        }
        const auto song = lynx::importFlp (project);
        const bool ok = song.mixer.size() > 5 && song.mixer[5].effects.size() == 1 && song.mixer[5].effects[0].kind == lynx::EffectKind::patcher;
        return ok ? song.mixer[5].effects[0].sidechain : -100;
    };
    // the inputs of insert 5 are [5, 2, 4]
    CHECK (importWithPin ("Master", 1) == 2);      // (issue #112: before, no source)
    CHECK (importWithPin ("Master", 2) == 4);
    CHECK (importWithPin ("7 Sidechain", 1) == 2); // (before: insert 7, from the name)
    CHECK (importWithPin ("4 Sidechain", 3) == -1); // past the list: FL's host gives the plugin no buffer
}
