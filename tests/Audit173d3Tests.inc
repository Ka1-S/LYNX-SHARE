// Round 173d3 (issue #77, the FL-parity gaps of the control audit): EQUO's band sends reach a mixer insert; Love
// Philter's whole state can be edited (units, filter types, articulators) and written back in FL's layout.

TEST (audit173d3_equo_send_goes_to_the_insert)
{
    std::vector<float> inL (4096), inR (4096);
    for (int n = 0; n < 4096; ++n)
    {
        inL[(std::size_t) n] = (float) (((n * 37) % 200 - 100) / 60.0);
        inR[(std::size_t) n] = (float) (((n * 53) % 170 - 85) / 50.0);
    }
    for (const bool add : { false, true })
        for (const int ovs : { 0, 1 })
        {
            auto state = equoTestState (ovs, 150, add);
            CHECK (lynx::equofx::sendInsert (state) == 0);
            auto routed = state;
            CHECK (lynx::equofx::setSendInsert (routed, 3) && lynx::equofx::sendInsert (routed) == 3 && lynx::equofx::parse (routed)->sendTrack == 2);
            auto run = [&] (const std::vector<std::uint8_t>& st, bool sending, std::vector<float>* send) {
                lynx::equofx::Engine e;
                e.prepare (44100.0, false);
                e.set (*lynx::equofx::parse (st));
                std::vector<float> l (inL), r (inR), sl (4096, 0.0f), sr (4096, 0.0f);
                for (int i = 0; i < 4096; i += 512)
                    e.process (l.data() + i, r.data() + i, 512, sending ? sl.data() + i : nullptr, sending ? sr.data() + i : nullptr);
                if (send != nullptr)
                    *send = sl, send->insert (send->end(), sr.begin(), sr.end());
                l.insert (l.end(), r.begin(), r.end());
                return l;
            };
            std::vector<float> send, sendAgain, sendEdited;
            const auto plain = run (state, false, nullptr);
            CHECK (run (routed, true, &send) == plain); // the send leaves the insert's own output as it is
            double energy = 0.0;
            for (const auto v : send)
                energy += (double) v * v;
            CHECK (energy > 1.0);
            run (routed, false, &sendAgain); // computed but not routed: nothing reaches a buffer
            CHECK (std::all_of (sendAgain.begin(), sendAgain.end(), [] (float v) { return v == 0.0f; }));
            // a band's send level (bank 1, the band at 1 kHz) changes what is sent
            auto edited = *lynx::equofx::parse (routed);
            edited.cell (0, 17, 2) = edited.cell (0, 17, 2) > 1.0f ? 0.1f : 1.9f;
            edited.target[17][2].mode = 0;
            lynx::equofx::Engine e;
            e.prepare (44100.0, false);
            e.set (edited);
            std::vector<float> l (inL), r (inR), sl (4096, 0.0f), sr (4096, 0.0f);
            e.process (l.data(), r.data(), 4096, sl.data(), sr.data());
            CHECK (! std::equal (sl.begin(), sl.end(), send.begin()));
        }

    // the slot: the send's insert is EffectSlot::sendTracks[0], a route of the mixer graph
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::equo);
    slot.state = equoTestState (0, 150, false);
    slot.sendTracks[0] = 2;
    CHECK (lynx::slotSendTarget (slot, 0) == 2 && lynx::slotSendTarget (slot, 1) == -1);
}

TEST (audit173d3_love_philter_doc_round_trip_and_edits)
{
    using namespace lynx;
    // a version-12 state of our own making: FL's default as LYNX writes it is not in the repository, so build one by
    // editing the round-66 test state (tests.cpp, love_philter_*) through the document itself
    const auto stateA = hex (
        "03000c00789cedda316ed44018c5f1cfd90b5050234a0e81702a1a6e4045c11d68e7005c00c40928e96810254590380225e552538471ec"
        "d97c314c841e0259cfff91463bf66e7e3b9ac42fdf582e1151961e776abf1f314ce3b3a5d7f6e4fb8bf1b2b6693c5cf56118e6b79ed5fe"
        "b2f6b7b53fadfd41ed8f1379378d9fd77e88a50d57c3c3f597dcd28ee3fcfaadbe9e8fcb24e272d56238fd4019a73e2c5f3074c1ebf732"
        "745a8ee51507670bce99e89cbff9e5076eb9f8be8e378f9b7df3fcfae2dbba37afcb9c0b391b0eb16ec795ff657cfd6a6aefc7cf17178f"
        "f2f9f6f9fafe8769342c7dfabe58e65e62db7f5338fb74fe4f96acafa5e3f8bbf3eb6b75eb1e75068eb3d3b241b95ef2ffc0d67ebc8bb8"
        "b792b65e2ffc9bfa636a64068e9fc3de44f77a7b93adfc6e7170fec661afa17b64038eb34336e81ed980e3ec900dba4736e0383b6483ee"
        "957aee61ed9fe27a4d63194ffd63fdccf44cc7f972fef42408cf6ce0e06c30a7b6711f93fba238387fee50c3e81e75038eb343dda07b64"
        "038eb34336e81e7b0a1c67873d85ee910d38ce0ed9a07b64038eb34336e81ed980e3ec900dba57625ec3d65bcbc7799cd6b83f95ded478"
        "d60267c70ef745758f1a06c7d9a186d13dea061c6787ba41f7c8061c67876cd03df61438ce0e7b0add231b709c1db241f7c8061c67876c"
        "d03db201c7d9211b74afc4bc86adb7968ff338ad717f2abda9f1ac05ce8e1dee8bea1e350c8eb3430da37bd40d38ce0e7583ee910d38ce"
        "0ed9a07bec29709c1df614ba4736e0383b6483ee910d38ce0ed9a07b64038eb34336e85e89790d5b6f2d1fe7715ae3fe547a53e3590b9c"
        "1d3bdc17d53d6a181c67871a46f7a81b709c1dea06dd231b709c1db241f7d853e0383bec29748f6cc07176c806dd231b709c1db241f7c8"
        "061c67876cd0bd12f31ab6de5a3ecee3b4c6fda9f4a6c6b316383b76b82faa7bd43038ce0e358cee5137e0383bd40dba4736e0383b6483"
        "eeb1a7c07176d853e81ed980e3ec900dba4736e0383b6483ee910d38ce0ed9a07b25e6356cbdb57c9cc7698dfb53e94d8d672d7076ec70"
        "5f54f7a861709c1d6a18dda36ec07176a81b748f6cc07176c806dd634f81e3ecb0a7d03db201c7d9211b748f6cc07176c806dd231b709c"
        "1db241f74acc6bd87a6bf9388fd31af7a7d29b1acf5ae0ecd8e1bea8ee51c3e0383bd430ba47dd80e3ec5037e81ed980e3ec900dbac79e"
        "02c7d9614fa17b64038eb34336e81ed980e3ec900dba4736e0383b6483eec54ff7970e90");
    const auto doc = philter::readDoc (stateA);
    CHECK (doc.has_value());
    if (! doc)
        return;
    // the round trip keeps every byte of the body (written with stored zlib blocks)
    const auto back = philter::writeDoc (*doc);
    const auto bodyA = inflateZlib (stateA.data() + 4, stateA.size() - 4);
    const auto bodyB = inflateZlib (back.data() + 4, back.size() - 4);
    CHECK (bodyA && bodyB && *bodyA == *bodyB && std::equal (back.begin(), back.begin() + 4, stateA.begin()));
    CHECK (philter::parse (back) != nullptr);

    std::vector<float> inL (8192), inR (8192);
    for (int n = 0; n < 8192; ++n)
    {
        inL[(std::size_t) n] = (float) (((n * 37) % 200 - 100) / 160.0);
        inR[(std::size_t) n] = (float) (((n * 53) % 170 - 85) / 150.0);
    }
    auto render = [&] (const std::vector<std::uint8_t>& st) {
        auto slot = philter::slotFromFl (st);
        if (! slot)
            return std::vector<float> {};
        auto fx = makeEffect (*slot);
        fx->prepare (44100.0);
        std::vector<float> l (inL), r (inR);
        for (int s = 0; s < 8192; s += 512)
        {
            fx->setSongPosition (s * 2.0 * 960.0 / 44100.0);
            fx->process (l.data() + s, r.data() + s, 512, slot->params, 120.0);
        }
        l.insert (l.end(), r.begin(), r.end());
        return l;
    };
    const auto base = render (stateA);
    CHECK (! base.empty() && render (back) == base);

    // the controls the editor writes into the state each change the sound
    auto edit = [&] (auto&& change) {
        auto d = *doc;
        change (d);
        return render (philter::writeDoc (d));
    };
    int unit = -1; // an enabled unit and a disabled one
    for (int i = 0; i < 8 && unit < 0; ++i)
        if (doc->u[(std::size_t) i].enabled)
            unit = i;
    CHECK (unit >= 0);
    if (unit < 0)
        return;
    const auto U = (std::size_t) unit;
    CHECK (edit ([&] (philter::Doc& d) { d.u[U].type = (d.u[U].type + 3) % 12; }) != base);
    CHECK (edit ([&] (philter::Doc& d) { d.u[U].mode = d.u[U].mode == 2 ? 3 : 2; }) != base);
    CHECK (edit ([&] (philter::Doc& d) { d.u[U].enabled = 0; }) != base);
    // a pattern envelope drawn on the unit's volume (pattern 1), switched on
    CHECK (edit ([&] (philter::Doc& d) {
               auto& e = d.u[U].art[1].pat[(std::size_t) (d.global (5) - 1)];
               e.pts = { { 0.0, 1.0, 0.0, 0, {} }, { 0.05, 0.0, 0.0, 0, {} }, { 0.05, 1.0, 0.0, 0, {} } };
               e.on = 1, e.loop = 0, e.sus = 2;
           })
           != base);
    // the Mod X map on the unit's cutoff
    CHECK (edit ([&] (philter::Doc& d) {
               auto& m = d.u[U].art[2].maps[0];
               m.pts = { { 0.0, 0.0, 0.0, 0, {} }, { 1.0, 1.0, 0.0, 0, {} } };
           })
           != base);
    // the globals in the head: the oversampling factor, CENTER
    CHECK (doc->global (5) >= 1 && doc->global (5) <= 10);
    {
        auto d = *doc;
        d.setOption (6, d.option (6) == 0 ? 1 : 0);
        CHECK (philter::readDoc (philter::writeDoc (d))->option (6) == d.option (6));
    }
}

TEST (audit173d3_love_philter_unit_parameters)
{
    using namespace lynx;
    CHECK (philter::paramCount == 103 && effectInfo (EffectKind::lovePhilter).params.size() == 103u);
    for (int i = 0; i < philter::paramCount; ++i)
        CHECK (philter::paramOfFlIndex (philter::flIndexOf (i)) == i);
    CHECK (philter::flIndexOf (48) == 8 + 3 && philter::flIndexOf (48 + 6) == 8 + 11 && philter::flIndexOf (102) == 8 + 44 * 7 + 10);
    CHECK (std::string (effectInfo (EffectKind::lovePhilter).params[48].name) == "Unit 1 low");
    // a project saved before these parameters existed takes them from the state, not from 0
    auto slot = makeEffectSlot (EffectKind::lovePhilter);
    for (int i = 48; i < philter::paramCount; ++i)
        slot.params[(std::size_t) i] = 0.0f;
    philter::upgradeParams (slot, 48);
    CHECK (slot.params[48 + 5] > 0.0f); // IN (raw 128 of −160…160) is not 0
}
