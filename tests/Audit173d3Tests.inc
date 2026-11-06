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
