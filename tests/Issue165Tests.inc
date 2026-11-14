// Round 190gm, issue #165 (karatmep232, Linux, video): a slide note that starts on the same step as a note. FL 2026
// glides that note to the slide note's key over the slide note's length (the user's video, 0:04-0:08: B4 and a C#5
// slide note on one step, the guitar bends up); LYNX sorted the slide before the note's "on", found no sounding voice
// and dropped the slide, so the note stayed flat.

TEST (issue165_a_slide_comes_after_the_notes_that_start_on_its_sample)
{
    using namespace lynx;
    EventBuffer b;
    auto add = [&b] (NoteEvent::Type type, int key, int offset) {
        NoteEvent e;
        e.type = type;
        e.channel = 0;
        e.key = key;
        e.sampleOffset = offset;
        b.add (e);
    };
    add (NoteEvent::Type::slide, 73, 100); // (the order the scheduler emits them in: the slide note first)
    add (NoteEvent::Type::on, 71, 100);
    add (NoteEvent::Type::off, 69, 100);
    add (NoteEvent::Type::on, 60, 50);
    b.sortByOffset();
    CHECK (b.size() == 4);
    CHECK (b[0].sampleOffset == 50 && b[0].type == NoteEvent::Type::on);
    // on the same sample: the note that ends, the note that starts, then the slide that bends it
    CHECK (b[1].type == NoteEvent::Type::off && b[1].key == 69);
    CHECK (b[2].type == NoteEvent::Type::on && b[2].key == 71);
    CHECK (b[3].type == NoteEvent::Type::slide && b[3].key == 73);
}

namespace issue165
{
// an instrument that writes down what the rack asks of it, in order
struct Recorder final : lynx::Instrument
{
    std::vector<std::string> calls;
    void prepare (double) noexcept override {}
    void noteOn (int key, float) noexcept override { calls.push_back ("on " + std::to_string (key)); }
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    void slideTo (int key, float, float) noexcept override { calls.push_back ("slide " + std::to_string (key)); }
};
} // namespace issue165

TEST (issue165_a_slide_note_on_the_step_of_a_note_reaches_the_instrument_after_it)
{
    using namespace lynx;
    Song song;
    song.mixer = Song::defaultMixer (2);
    ChannelDef c;
    c.instrument = InstrumentKind::plugin;
    c.plugin.id = "recorder";
    song.channels.push_back (c);
    song.patterns.emplace_back();
    Note b4;
    b4.key = 71;
    b4.start = ppq;
    b4.length = ppq * 2;
    Note slide; // the user's C#5 slide note, on B4's step
    slide.key = 73;
    slide.start = ppq;
    slide.length = ppq / 4;
    slide.slide = true;
    song.patterns[0].notes = { slide, b4 };
    song.patterns[0].lengthTicks = (std::int64_t) (ppq * 4);
    song.mode = PlayMode::pattern;
    auto rec = std::make_shared<issue165::Recorder>();
    RackState::Factories f;
    f.pluginInstruments = [rec] (const PluginRef&, double) { return std::shared_ptr<Instrument> (rec); };
    auto state = RackState::build (song, nullptr, 44100.0, f);
    RackRenderer renderer;
    renderer.prepare (44100.0, 256);
    const auto tps = song.bpm / 60.0 * ppq / 44100.0;
    std::vector<float> l (256), r (256);
    for (int done = 0; done < 44100; done += 256)
        renderer.process (state.get(), true, done * tps, 256, l.data(), r.data(), nullptr);
    CHECK (rec->calls.size() >= 2);
    if (rec->calls.size() >= 2)
    {
        CHECK (rec->calls[0] == "on 71");
        CHECK (rec->calls[1] == "slide 73");
    }
}

// What it sounds like: a note with a slide note a whole tone up on its own step ends a whole tone up (+200 cents), as a
// slide note that starts inside the note always did
TEST (issue165_a_note_glides_to_a_slide_note_that_starts_with_it)
{
    using namespace lynx;
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    Note held;
    held.key = 60;
    held.length = ppq * 8;
    held.velocity = 0.8f;
    Note slide;
    slide.key = 62;
    slide.start = 0; // the note's own step
    slide.length = ppq / 4;
    slide.slide = true;
    const auto base = issue14::pitchAt (issue14::renderPattern (synth, { held }, 0.0f, 1.5), 1.0);
    const auto slid = issue14::pitchAt (issue14::renderPattern (synth, { slide, held }, 0.0f, 1.5), 1.0);
    CHECK (base > 0.0);
    CHECK (std::abs (issue14::cents (base, slid) - 200.0) < 15.0);
    // a slide note that starts inside the note (the case that already worked) still lands on the same pitch
    slide.start = ppq / 2;
    const auto later = issue14::pitchAt (issue14::renderPattern (synth, { held, slide }, 0.0f, 1.5), 1.0);
    CHECK (std::abs (issue14::cents (base, later) - 200.0) < 15.0);
}
