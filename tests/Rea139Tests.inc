// --- REA round 139: FL Studio technologies (export entries, polyphony, record quantize / markers, smart disable) ---

TEST (rea139_last_exported_formats_like_fl)
{
    using namespace lynx::exportopts;
    // FL's mask (FLEngine FUN_011dd840 / the render window's close): bit 3 WAV, 4 MP3, 5 OGG, 6 FLAC, 7 MIDI, 9 M4A
    CHECK (flFormatBit (Format::wav) == 0x8 && flFormatBit (Format::mp3) == 0x10 && flFormatBit (Format::ogg) == 0x20
           && flFormatBit (Format::flac) == 0x40);
    Options o;
    CHECK (flLastExportedMask (o) == 8 && formatsOf (o).size() == 1);
    auto both = withFlLastExported (o, 0x8 | 0x10 | 0x40); // WAV + MP3 + FLAC
    CHECK (both.format == Format::wav && flLastExportedMask (both) == 0x58);
    const auto f = formatsOf (both);
    CHECK (f.size() == 3 && f[0] == Format::wav && f[1] == Format::mp3 && f[2] == Format::flac);
    // invalid masks fall back to WAV (FL: < 8 or > 0x3FF); MIDI / M4A only: WAV for the audio window
    CHECK (withFlLastExported (o, 3).format == Format::wav && withFlLastExported (o, 0x800).moreFormats == 0);
    CHECK (withFlLastExported (o, 0x80 | 0x200).format == Format::wav);
    CHECK (withFlLastExported (o, 0x20).format == Format::ogg && withFlLastExported (o, 0x20).moreFormats == 0);
    // a "<format> file..." entry: that format alone
    CHECK (withSingleFormat (both, Format::mp3).format == Format::mp3 && withSingleFormat (both, Format::mp3).moreFormats == 0);
    // remembered; the main format never counts as "more"; the disk space counts every file
    auto saved = fromText (toText (both));
    CHECK (saved.moreFormats == both.moreFormats && saved.disableMaxPolyphony);
    auto self = o;
    self.moreFormats = 1u << (int) Format::wav;
    CHECK (clamped (self).moreFormats == 0);
    o.sampleRate = 44100.0;
    o.wavBits = 16;
    auto two = o;
    two.moreFormats = 1u << (int) Format::mp3;
    CHECK (estimatedBytes (two, 60.0) == estimatedBytes (o, 60.0) + (std::int64_t) (60.0 * o.mp3Kbps * 1000.0 / 8.0));
}

TEST (rea139_max_polyphony_releases_the_oldest_and_export_lifts_it)
{
    lynx::VoiceOrder order;
    CHECK (order.noteOn (60, 2) == -1 && order.noteOn (64, 2) == -1);
    CHECK (order.noteOn (67, 2) == 60 && order.sounding() == 2 && order.oldest() == 64); // the oldest goes
    order.noteOff (64);
    CHECK (order.noteOn (70, 2) == -1 && order.oldest() == 67);
    CHECK (order.noteOn (67, 2) == -1 && order.oldest() == 70); // played again: the newest
    // (round 168l: FL's limiter does not run on a Mono channel at all, FLEngine 0xd293d0)
    CHECK (lynx::voiceLimitFor (0, false) == 0 && lynx::voiceLimitFor (1, true) == 0 && lynx::voiceLimitFor (1, false) == 1
           && lynx::voiceLimitFor (4, true) == 0);

    // in the renderer: a chord of 4 on a channel limited to 2 keeps 2 voices
    auto song = lynx::Song::makeDefault();
    song.channels.resize (1);
    song.channels[0].instrument = lynx::InstrumentKind::synth;
    song.channels[0].maxPolyphony = 2;
    song.patterns[0].notes.clear();
    for (int k = 0; k < 4; ++k)
        song.patterns[0].notes.push_back ({ (std::int64_t) k * 24, 960, 0, 60 + k * 3, 0.8f });
    auto render = [] (const lynx::Song& s) {
        auto state = lynx::RackState::build (s, nullptr, 48000.0);
        lynx::RackRenderer r;
        r.prepare (48000.0, 512);
        std::vector<float> l (512), rr (512);
        const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
        double energy = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            r.process (state.get(), true, b * 512 * tps, 512, l.data(), rr.data());
            if (b >= 20)
                for (auto v : l)
                    energy += (double) v * v;
        }
        return energy;
    };
    const auto limited = render (song);
    auto free = song;
    free.channels[0].maxPolyphony = 0;
    const auto unlimited = render (free);
    CHECK (limited > 0.0 && unlimited > limited * 1.3); // 2 voices sound instead of 4

    // FL's "Disable maximum polyphony" (on by default) lifts every limit in the exported song
    lynx::exportopts::Options o;
    o.range = lynx::exportopts::Range::pattern;
    std::int64_t music = 0, loop = 0;
    CHECK (lynx::exportopts::makeRenderSong (song, o, 0, music, loop).channels[0].maxPolyphony == 0);
    o.disableMaxPolyphony = false;
    CHECK (lynx::exportopts::makeRenderSong (song, o, 0, music, loop).channels[0].maxPolyphony == 2);
}

TEST (rea139_record_quantize_like_fl)
{
    using namespace lynx;
    const auto g = ticksPerStep;
    auto n = [] (std::int64_t start, std::int64_t length) { return Note { start, length, 0, 60, 0.8f }; };
    // played: start 3/4 step late, end 1/3 step before a grid line
    const auto start = g + g * 3 / 4, end = 4 * g - g / 3;
    auto run = [&] (TakeQuantize q) {
        std::vector<Note> v { n (start, end - start) };
        quantizeTake (v, q);
        return v[0];
    };
    TakeQuantize q;
    q.grid = g;
    // "Note start time" + "Leave note duration" (LYNX's default): start to the grid, the played length
    auto a = run (q);
    CHECK (a.start == 2 * g && a.length == end - start);
    // + "Note end time": the end to the nearest grid line too
    q.end = NoteEnd::quantize;
    a = run (q);
    CHECK (a.start == 2 * g && a.start + a.length == 4 * g);
    // neither end item: the end stays where it was played
    q.end = NoteEnd::keepEnd;
    a = run (q);
    CHECK (a.start == 2 * g && a.start + a.length == end);
    // start off, end quantized
    q.starts = false;
    q.end = NoteEnd::quantize;
    a = run (q);
    CHECK (a.start == start && a.start + a.length == 4 * g);
    // "Record to step sequencer": steps, one step long
    q.stepSequencer = true;
    q.grid = 0;
    a = run (q);
    CHECK (a.start == 2 * g && a.length == ticksPerStep);
    // no grid: as played
    TakeQuantize none;
    a = run (none);
    CHECK (a.start == start && a.length == end - start);
}

TEST (rea139_recording_markers_punch_in_out)
{
    using namespace lynx;
    auto m = [] (std::int64_t tick, int action) {
        Marker k;
        k.tick = tick;
        k.action = action;
        return k;
    };
    CHECK (punchRanges ({}).empty() && inPunchRanges ({}, 123.0));
    CHECK (punchRanges ({ m (500, 0) }).empty()); // plain markers do nothing
    auto r = punchRanges ({ m (1000, Marker::punchOut), m (200, Marker::punchIn), m (3000, Marker::punchIn) });
    CHECK (r.size() == 2 && r[0].in == 200 && r[0].out == 1000 && r[1].in == 3000 && r[1].out > 1000000);
    CHECK (! inPunchRanges (r, 100.0) && inPunchRanges (r, 200.0) && ! inPunchRanges (r, 1000.0) && inPunchRanges (r, 5000.0));
    auto stopOnly = punchRanges ({ m (800, Marker::punchOut) });
    CHECK (stopOnly.size() == 1 && stopOnly[0].in == 0 && stopOnly[0].out == 800);

    // an audio take over [0, 4000) offset 10 keeps the parts inside the ranges
    const auto parts = punchParts (r, 0, 4000, 10);
    CHECK (parts.size() == 2 && parts[0].start == 200 && parts[0].length == 800 && parts[0].offset == 210);
    CHECK (parts[1].start == 3000 && parts[1].length == 1000 && parts[1].offset == 3010);
    CHECK (punchParts ({}, 5, 6, 7).size() == 1 && punchParts (r, 1100, 500, 0).empty());

    // FL's marker types from an .flp (9 Start recording, 10 Stop recording) and the project file keep the action
    Marker kept;
    kept.action = Marker::punchIn;
    CHECK (kept.action == 9 && ! kept.isSignature());
}

TEST (rea139_smart_disable_per_slot_and_allow_smart_disable)
{
    // FL's per-plugin "Smart disable" and the wrapper's "Allow smart disable": with either off the slot keeps being
    // processed in silence
    auto run = [] (bool slotSmart, bool allow) {
        lynx::Song s;
        s.mixer = lynx::Song::defaultMixer (1);
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::sampler;
        c.samplePath = "dc";
        c.mixerTrack = 1;
        s.channels.push_back (c);
        s.patterns.emplace_back();
        lynx::EffectSlot fx;
        fx.kind = lynx::EffectKind::plugin;
        fx.plugin.id = "inversor";
        fx.smartDisable = slotSmart;
        fx.plugin.allowSmartDisable = allow;
        s.mixer[1].effects.push_back (fx);
        auto inverter = std::make_shared<CountingInverter>();
        lynx::RackState::Factories f;
        f.samples = [] (const std::string&) {
            auto d = std::make_shared<lynx::SampleData>();
            d->left.assign (4800, 0.5f);
            return std::shared_ptr<const lynx::SampleData> (d);
        };
        f.pluginEffects = [inverter] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Effect> { return inverter; };
        auto state = lynx::RackState::build (s, nullptr, 48000.0, f);
        lynx::RackRenderer r;
        r.prepare (48000.0, 512);
        std::vector<float> l (480), rr (480);
        for (int i = 0; i < 100 * 6; ++i) // 6 s of silence
            r.process (state.get(), false, 0.0, 480, l.data(), rr.data());
        return inverter->calls;
    };
    CHECK (run (true, true) < 100 * 6);
    CHECK (run (false, true) == 100 * 6);
    CHECK (run (true, false) == 100 * 6);

    // the wrapper flag (version >= 10) and "Use maximum buffer size from host" (fixed-buffer options bit 1)
    lynx::ForeignPlugin w;
    w.hasWrapperOptions = true;
    w.wrapperVersion = 12;
    w.wrapperFlags = 0xA4007D8Cu & ~0x20000000u;
    lynx::PluginRef ref;
    lynx::applyFlWrapperOptions (w, ref);
    CHECK (! ref.allowSmartDisable && ! ref.hostMaxBuffer);
    w.wrapperFlags = 0xA4007D8Cu | 0x2u;
    w.fixedOptions = 0x2u;
    lynx::PluginRef ref2;
    lynx::applyFlWrapperOptions (w, ref2);
    CHECK (ref2.allowSmartDisable && ref2.fixedBuffers && ref2.hostMaxBuffer && ! ref2.maxSizeBuffers);
}

TEST (rea139_flp_wrapper_flags_and_saved_version)
{
    // the plugin record 0xD4: u16 flags at +16 (0x20 = Smart disable); and the version for the Browser's details
    FlpBuilder b;
    b.ascii (0xC7, "21.2.3.4060");
    b.data (0xEC, std::vector<std::uint8_t> (12, 0));
    for (const auto flags : { 0x160, 0x140 })
    {
        b.utf16 (0xC9, u"Fruity Reeverb 2");
        std::vector<std::uint8_t> rec (52, 0);
        rec[8] = 2;
        rec[16] = (std::uint8_t) flags;
        rec[17] = (std::uint8_t) (flags >> 8);
        b.data (0xD4, rec);
        b.data (0xD5, std::vector<std::uint8_t> (8, 0));
        b.u8 (0x62);
        b.u16 (flags == 0x160 ? 0 : 1);
    }
    b.data (0xEC, std::vector<std::uint8_t> (12, 0));
    const auto p = flp::parseProject (b.bytes);
    CHECK (! p.mixer.empty() && p.mixer[0].slots.size() == 2);
    if (! p.mixer.empty() && p.mixer[0].slots.size() == 2)
        CHECK (p.mixer[0].slots[0].wrapperFlags == 0x160 && p.mixer[0].slots[1].wrapperFlags == 0x140);
    CHECK (flp::readSavedVersion (b.bytes) == "21.2.3.4060");
    CHECK (flp::readSavedVersion (std::vector<std::uint8_t> (b.bytes.begin(), b.bytes.begin() + 30)).empty());
    CHECK (flp::readSavedVersion (std::vector<std::uint8_t> (40, 0)).empty());
}

TEST (rea139_midi_export_notes_follow_the_playlist)
{
    auto song = lynx::Song::makeDefault();
    song.patterns.resize (2);
    song.patterns[0].notes = { { 0, 96, 0, 60, 0.8f }, { 384, 96, 0, 62, 0.8f }, { 768, 96, 0, 64, 0.8f } };
    song.patterns[1].notes = { { 0, 2000, 0, 48, 0.8f } };
    song.mode = lynx::PlayMode::song;
    song.playlist.clear();
    lynx::Clip a;
    a.pattern = 0;
    a.start = 1536;
    a.length = 768;   // the first two notes
    song.playlist.push_back (a);
    lynx::Clip b = a;
    b.start = 4000;
    b.offset = 384;   // from the second note
    b.length = 384;
    song.playlist.push_back (b);
    lynx::Clip c = a;
    c.pattern = 1;
    c.start = 0;
    c.length = 500;   // cut at the clip's end
    song.playlist.push_back (c);
    lynx::Clip d = a;
    d.start = 9000;
    d.muted = true;
    song.playlist.push_back (d);
    const auto notes = lynx::exportopts::midiExportNotes (song);
    CHECK (notes.size() == 4);
    if (notes.size() == 4)
    {
        CHECK (notes[0].start == 0 && notes[0].key == 48 && notes[0].length == 500);
        CHECK (notes[1].start == 1536 && notes[2].start == 1920 && notes[3].start == 4000 && notes[3].key == 62);
    }
    song.mode = lynx::PlayMode::pattern;
    song.currentPattern = 0;
    CHECK (lynx::exportopts::midiExportNotes (song).size() == 3);
}
