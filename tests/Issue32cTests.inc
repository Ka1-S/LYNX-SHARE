// Round 169c (issue #32 follow-up, medievilmusic on 0.1.22): docs/analise/REA_rodada169c.md
#define I32C_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

namespace i32c
{
// A hosted generator stand-in: keeps the keys it was sent like PluginInstrument (HostedKeys) and, for every block it
// is asked to render, the position its play head would report (HostBlockClock + HostTimeline::tickAt).
struct HostedSpy final : lynx::Instrument
{
    lynx::HostedKeys keys;
    lynx::HostBlockClock clock;
    std::vector<int> offs;                          // keys released by allNotesOff (the note offs sent)
    std::vector<std::pair<double, int>> blocks;     // (play head tick, samples) per block
    void prepare (double) noexcept override {}
    void noteOn (int key, float) noexcept override { keys.set (0, key, true); }
    void noteOff (int key) noexcept override { keys.set (0, key, false); }
    void allNotesOff() noexcept override
    {
        keys.releaseAll ([this] (int, int key) { offs.push_back (key); });
    }
    void render (float*, float*, int n, float, float) noexcept override
    {
        const auto& t = lynx::hostTimeline();
        const auto at = clock.start (t.chunk.load());
        clock.advance (n);
        if (t.playing.load())
            blocks.emplace_back (t.tickAt (at), n);
    }
};
} // namespace i32c

TEST (issue32c_hosted_keys_release_each_held_key_once)
{
    // a VST3 synth only stops a note on its own note off (no CC 123): every held key on every MIDI channel gets one
    lynx::HostedKeys keys;
    keys.set (0, 60, true);
    keys.set (0, 64, true);
    keys.set (3, 127, true);
    keys.set (0, 64, false);
    keys.set (16, 10, true); // (out of range: ignored)
    keys.set (0, 128, true);
    CHECK (keys.held (0, 60) && ! keys.held (0, 64) && keys.held (3, 127) && keys.any());
    std::vector<std::pair<int, int>> offs;
    keys.releaseAll ([&] (int ch, int key) { offs.emplace_back (ch, key); });
    I32C_REQUIRE (offs.size() == 2);
    CHECK (offs[0] == std::make_pair (0, 60));
    CHECK (offs[1] == std::make_pair (3, 127));
    CHECK (! keys.any());
    offs.clear();
    keys.releaseAll ([&] (int ch, int key) { offs.emplace_back (ch, key); });
    CHECK (offs.empty()); // nothing twice
}

TEST (issue32c_a_jump_while_playing_sends_the_sounding_notes_their_note_offs)
{
    // #1 "when moving inside the sequencer while the playback is on, some synths double their sound": a long note of a
    // hosted generator is sounding, the user clicks the ruler; the old note must get its note off (the chased copy of
    // it starts again at the new position, as FL's "Play truncated notes")
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    I32C_REQUIRE (song.channels.size() > 1 && ! song.patterns.empty());
    song.patterns[0].notes.clear();
    lynx::Note n;
    n.start = 0;
    n.length = 16 * lynx::ppq;
    n.key = 55;
    n.channel = 1;
    song.patterns[0].notes.push_back (n);
    song.currentPattern = 0;
    const double rate = 48000.0, tps = lynx::ticksPerSample (song.bpm, rate);
    auto state = lynx::RackState::build (song, nullptr, rate);
    auto spy = std::make_shared<i32c::HostedSpy>();
    const_cast<lynx::RackState&> (*state).instruments[1] = spy;
    lynx::RackRenderer renderer;
    renderer.prepare (rate, 512);
    unsigned jumps = 0;
    renderer.setTransportOptions (true, false, jumps);
    std::vector<float> l (512), r (512);
    double tick = 0.0;
    for (int b = 0; b < 4; ++b, tick += 512 * tps)
        renderer.process (state.get(), true, tick, 512, l.data(), r.data());
    CHECK (spy->keys.held (0, 55));
    renderer.setTransportOptions (true, false, ++jumps);
    renderer.process (state.get(), true, 4.0 * lynx::ppq, 512, l.data(), r.data());
    I32C_REQUIRE (spy->offs.size() == 1);
    CHECK (spy->offs[0] == 55);       // the old note was stopped...
    CHECK (spy->keys.held (0, 55));   // ...and the chased one plays (held once, not twice)
}

TEST (issue32c_each_block_of_a_generator_reports_where_it_starts)
{
    // #1 "...or go out of tempo" / #3: a generator's chunk is split at its notes, and each piece is one plugin block.
    // Every block reported the chunk's start, so a synced sequencer saw the song stand still and jump. Now the play
    // head moves on with each block: the positions are continuous across notes, chunks and blocks.
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.mode = lynx::PlayMode::pattern;
    I32C_REQUIRE (song.channels.size() > 1 && ! song.patterns.empty());
    song.patterns[0].notes.clear();
    for (int i = 0; i < 16; ++i) // sixteenth notes: several notes inside each block
    {
        lynx::Note n;
        n.start = i * lynx::ppq / 4;
        n.length = lynx::ppq / 8;
        n.key = 48 + i;
        n.channel = 1;
        song.patterns[0].notes.push_back (n);
    }
    song.currentPattern = 0;
    const double rate = 44100.0, tps = lynx::ticksPerSample (song.bpm, rate);
    auto state = lynx::RackState::build (song, nullptr, rate);
    auto spy = std::make_shared<i32c::HostedSpy>();
    const_cast<lynx::RackState&> (*state).instruments[1] = spy;
    lynx::RackRenderer renderer;
    renderer.prepare (rate, 1024);
    std::vector<float> l (1024), r (1024);
    double tick = 0.0;
    for (int b = 0; b < 40; ++b, tick += 1024 * tps)
        renderer.process (state.get(), true, tick, 1024, l.data(), r.data());
    I32C_REQUIRE (spy->blocks.size() > 60); // (split at the notes: more blocks than device blocks)
    int splitInsideChunk = 0;
    double worst = 0.0;
    for (std::size_t i = 1; i < spy->blocks.size(); ++i)
    {
        const auto expected = spy->blocks[i - 1].first + spy->blocks[i - 1].second * tps;
        const auto at = spy->blocks[i].first;
        if (at < 4.0 * lynx::ppq - 1.0 && expected < 4.0 * lynx::ppq - 1.0) // (the pattern's wrap is a jump)
            worst = std::max (worst, std::abs (at - expected));
        if (spy->blocks[i].first != spy->blocks[i - 1].first && spy->blocks[i - 1].second < 1024)
            ++splitInsideChunk;
    }
    CHECK (worst < 1.0e-6);
    CHECK (splitInsideChunk > 0);
    // stopped: no tempo, the play head stays where it is
    renderer.process (state.get(), false, tick, 1024, l.data(), r.data());
    CHECK (lynx::hostTimeline().ticksPerSample.load() == 0.0);
}

TEST (issue32c_a_wheel_scrolling_the_mixer_keeps_scrolling_over_its_controls)
{
    // #5 "all the faders on the mixer can react accidentally on the mouse wheel": the spin that scrolls the strips goes
    // on scrolling while the wheel keeps turning; after a pause the wheel over a fader moves it again (FL)
    lynx::controls::WheelScrollLatch latch;
    CHECK (! latch.scrolling (1000.0)); // nothing scrolled yet: the wheel over a fader changes it
    latch.scrolled (1000.0);
    CHECK (latch.scrolling (1000.0));
    CHECK (latch.scrolling (1060.0));   // the next notch (~60 ms later) is still the scroll
    latch.scrolled (1060.0);            // (it scrolled again: the hold starts over)
    CHECK (latch.scrolling (1400.0));
    CHECK (! latch.scrolling (1460.0)); // a pause of 400 ms ends it
    CHECK (! latch.scrolling (900.0));  // (a clock that went back is no scroll)
}
