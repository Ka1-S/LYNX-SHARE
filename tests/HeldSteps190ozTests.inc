// Round 190oz: FL's step notes on generators are held (REA_rodada190oz §5): a length-0 note on a plugin / native
// generator channel sounds until that channel's next note starts (FL sends its note-off then, on that note's sample);
// on a Sampler a step note stays one step long.

TEST (flp_import_step_notes_on_generators_are_held)
{
    FlpBuilder b (96);
    b.ascii (0xC7, "21.2.3.4004");
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (2); // a generator (plugin) channel
    b.u8 (0x40); b.u16 (1);
    b.u8 (0x15); b.u8 (0); // a Sampler
    b.u8 (0x41); b.u16 (1);
    std::vector<std::uint8_t> notes;
    for (const auto& r : { noteRecord (0, 0, 0, 60, 100), noteRecord (96, 0, 48, 60, 100), noteRecord (0, 1, 0, 60, 100) })
        notes.insert (notes.end(), r.begin(), r.end());
    b.data (0xE0, notes);

    const auto song = lynx::importFlp (flp::parseProject (b.bytes));
    CHECK (! song.patterns.empty());
    if (song.patterns.empty())
        return;
    int heldOn0 = 0, plainOn0 = 0, heldOn1 = 0, plainOn1 = 0;
    for (const auto& n : song.patterns[0].notes)
    {
        CHECK (n.length > 0);
        if (n.channel == 0)
            (n.heldStep() ? heldOn0 : plainOn0) += 1;
        else
            (n.heldStep() ? heldOn1 : plainOn1) += 1;
    }
    // the generator's step note is held, its real-length note is not; the Sampler's step note is an ordinary one-step note
    CHECK (heldOn0 == 1 && plainOn0 == 1);
    CHECK (heldOn1 == 0 && plainOn1 == 1);
}

TEST (scheduler_held_step_note_has_no_off)
{
    lynx::Pattern p;
    lynx::Note held, plain;
    held.start = 0, held.channel = 0, held.held = true;
    plain.start = 0, plain.channel = 1;
    p.notes = { held, plain };
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::EventBuffer ev;
    lynx::schedulePatternLoop (p, p.effectiveLength (lynx::Meter (4, 4)), 0.0, 24000, tps, ev);
    int on0 = 0, off0 = 0, on1 = 0, off1 = 0;
    for (const auto& e : ev)
    {
        if (e.type == lynx::NoteEvent::Type::on)
            (e.channel == 0 ? on0 : on1) += 1;
        if (e.type == lynx::NoteEvent::Type::off)
            (e.channel == 0 ? off0 : off1) += 1;
    }
    CHECK (on0 == 1 && off0 == 0);
    CHECK (on1 == 1 && off1 == 1);

    // the safety net that ends notes whose "off" did not come leaves the held one alone
    lynx::ScheduledNotes sounding;
    sounding.process (ev, 24000, tps);
    CHECK (sounding.heldSteps() == 1);
    lynx::EventBuffer later;
    sounding.process (later, 48000 * 4, tps);
    int offs = 0;
    for (const auto& e : later)
        offs += e.type == lynx::NoteEvent::Type::off && e.channel == 0;
    CHECK (offs == 0);

    // the channel's next note (any key) ends it: its "off" on that note's sample, before the "on", with the held note's id
    std::uint32_t heldId = 0;
    for (const auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on && e.channel == 0)
            heldId = e.noteId;
    lynx::EventBuffer next;
    lynx::NoteEvent on;
    on.type = lynx::NoteEvent::Type::on;
    on.channel = 0, on.key = 67, on.velocity = 0.8f, on.sampleOffset = 100;
    next.add (on);
    lynx::NoteEvent other = on; // another channel's note leaves it alone
    other.channel = 1, other.sampleOffset = 50;
    next.add (other);
    next.sortByOffset();
    sounding.process (next, 512, tps);
    int heldOff = -1, nextOn = -1;
    for (int i = 0; i < next.size(); ++i)
    {
        if (next[i].type == lynx::NoteEvent::Type::off && next[i].channel == 0 && next[i].key == 60 && next[i].noteId == heldId && heldId != 0)
            heldOff = i;
        if (next[i].type == lynx::NoteEvent::Type::on && next[i].channel == 0)
            nextOn = i;
    }
    CHECK (heldOff >= 0 && nextOn > heldOff && next[heldOff].sampleOffset == 100);
    CHECK (sounding.heldSteps() == 0);

    // once its length is edited it is an ordinary note again
    p.notes[0].length = lynx::ticksPerStep * 2;
    lynx::EventBuffer ev2;
    lynx::schedulePatternLoop (p, p.effectiveLength (lynx::Meter (4, 4)), 0.0, 24000, tps, ev2);
    int offAfter = 0;
    for (const auto& e : ev2)
        offAfter += e.type == lynx::NoteEvent::Type::off && e.channel == 0;
    CHECK (offAfter == 1);
}
