// Round 190cd: the audio editor as a mixer-slot plugin (core/EdisonFx.h) — FL's Edison. The record modes' state
// machine, the effect (pass-through, mute input, the take, playback), the session that keeps a slot's audio, the WAV
// file of a project and FL's saved state. The project file's side is in LYNX --edisonslottest (engine/ProjectFile.cpp).

namespace edison190cd
{
namespace ef = lynx::edisonfx;
namespace ae = lynx::audioedit;

inline lynx::EffectParams defaults()
{
    return lynx::makeEffectSlot (lynx::EffectKind::edison).params;
}

// `blocks` blocks of `n` frames of a constant `level` through the processor (returns the last block's left output)
inline std::vector<float> run (ef::Processor& p, const lynx::EffectParams& params, float level, int blocks, int n = 441)
{
    std::vector<float> l, r;
    for (int b = 0; b < blocks; ++b)
    {
        l.assign ((std::size_t) n, level);
        r.assign ((std::size_t) n, -level);
        p.process (l.data(), r.data(), n, params, 120.0);
    }
    return l;
}

inline ae::Document ramp (int frames, double rate = 44100.0)
{
    ae::Document d;
    d.audio.sampleRate = rate;
    d.audio.channels.assign (2, std::vector<float> ((std::size_t) frames));
    for (int i = 0; i < frames; ++i)
    {
        d.audio.channels[0][(std::size_t) i] = (float) (i % 200) / 400.0f;
        d.audio.channels[1][(std::size_t) i] = -(float) (i % 300) / 600.0f;
    }
    return d;
}

inline void put32 (std::vector<std::uint8_t>& b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back ((std::uint8_t) (v >> (8 * i)));
}

inline void putDouble (std::vector<std::uint8_t>& b, double v)
{
    std::uint8_t raw[8];
    std::memcpy (raw, &v, 8);
    b.insert (b.end(), raw, raw + 8);
}

// A state in the layout FL writes for the plugin (REA round 190cd): version, 12 bytes, 64-bit size + WAV image, an
// empty name, selection / view / noise threshold, (envelopes), the 20 bytes of switches.
inline std::vector<std::uint8_t> flState (const std::vector<std::uint8_t>& wav, int mode, bool newProject, bool loop, bool mute,
                                          int maxTime, bool slave, bool ignoreOwn, float threshold)
{
    std::vector<std::uint8_t> b;
    put32 (b, 0x000d0003u);
    b.insert (b.end(), 12, 0);
    put32 (b, (std::uint32_t) wav.size());
    put32 (b, 0);
    b.insert (b.end(), wav.begin(), wav.end());
    b.push_back (0); // name
    putDouble (b, -1.0);
    putDouble (b, -1.0);
    putDouble (b, 0.0);
    putDouble (b, 1.0);
    std::uint8_t t[4];
    std::memcpy (t, &threshold, 4);
    b.insert (b.end(), t, t + 4);
    b.insert (b.end(), 40, 0); // (what lies between is not read)
    put32 (b, (std::uint32_t) mode);
    b.push_back (newProject);
    b.push_back (loop);
    b.push_back (mute);
    b.push_back (0);
    put32 (b, (std::uint32_t) maxTime);
    put32 (b, 0);
    b.push_back (slave);
    b.push_back (0);
    b.push_back (ignoreOwn);
    b.push_back (0);
    return b;
}
} // namespace edison190cd

TEST (edison190cd_the_plugin_is_a_slot_kind_with_fl_defaults)
{
    namespace ef = lynx::edisonfx;
    const auto slot = lynx::makeEffectSlot (lynx::EffectKind::edison);
    CHECK (slot.kind == lynx::EffectKind::edison);
    // FL: the mode list opens on "On input", Append is on, the longest recording is 30 minutes (the sixth choice; live FL
    // check 190cq, shot edison190cd-05e), threshold 0.02
    CHECK (ef::modeOf (slot.params) == ef::Mode::onInput);
    CHECK (slot.params[ef::append] >= 0.5f);
    CHECK (slot.params[ef::muteInput] < 0.5f);
    CHECK (slot.params[ef::slave] < 0.5f);
    CHECK (ef::maxTimeIndexOf (slot.params) == 5);
    CHECK (ef::maxMinutes[5] == 30);
    CHECK (std::abs (slot.params[ef::noiseThreshold] - 0.02f) < 1.0e-6f);
    CHECK (ef::maxFramesOf (slot.params, 48000.0) == 30 * 60 * 48000);
    auto forever = slot.params;
    forever[ef::maxTime] = 0.0f;
    CHECK (ef::maxFramesOf (forever, 48000.0) == 0);
    CHECK (std::string (ef::modeName (ef::Mode::now)) == "Now");
    CHECK (std::string (ef::modeName (ef::Mode::onInput)) == "On input");
    CHECK (std::string (ef::modeName (ef::Mode::input)) == "Input");
    CHECK (std::string (ef::modeName (ef::Mode::onPlay)) == "On play");
    for (int m = 0; m < ef::modeCount; ++m)
    {
        auto p = slot.params;
        p[ef::recordMode] = ef::modeValue ((ef::Mode) m);
        CHECK (ef::modeOf (p) == (ef::Mode) m);
    }
}

TEST (edison190cd_gate_now_writes_every_block)
{
    namespace ef = lynx::edisonfx;
    ef::Gate g;
    g.prepare (44100.0);
    CHECK (g.phase() == ef::Phase::off);
    CHECK (! g.block (1.0f, 512, true, false, 0.02f).write); // not armed: nothing
    g.arm (ef::Mode::now);
    CHECK (g.phase() == ef::Phase::recording);
    for (int i = 0; i < 4; ++i)
    {
        const auto d = g.block (0.0f, 512, false, false, 0.02f); // silence, song stopped: still written
        CHECK (d.write && ! d.preRoll && ! d.jumpMarker);
    }
    g.disarm();
    CHECK (g.phase() == ef::Phase::off);
    CHECK (! g.block (1.0f, 512, true, false, 0.02f).write);
}

TEST (edison190cd_gate_on_input_starts_with_the_first_sound_and_never_stops)
{
    namespace ef = lynx::edisonfx;
    ef::Gate g;
    g.prepare (44100.0);
    CHECK (g.holdFrames() == 44100);   // 1 s
    CHECK (g.preRollFrames() == 4410); // 0.1 s
    g.arm (ef::Mode::onInput);
    CHECK (g.phase() == ef::Phase::waiting);
    CHECK (! g.block (0.01f, 512, true, false, 0.02f).write);  // below the threshold
    CHECK (! g.block (0.02f, 512, true, false, 0.02f).write);  // at it: not above
    CHECK (g.phase() == ef::Phase::waiting);
    const auto first = g.block (0.5f, 512, false, false, 0.02f);
    CHECK (first.write && first.preRoll);
    CHECK (g.phase() == ef::Phase::recording);
    // minutes of silence later it still records (only "Input" runs out)
    for (int i = 0; i < 20000; ++i)
    {
        const auto d = g.block (0.0f, 512, false, false, 0.02f);
        if (! d.write || d.preRoll)
        {
            CHECK (false);
            break;
        }
    }
    CHECK (g.phase() == ef::Phase::recording);
}

TEST (edison190cd_gate_input_pauses_after_the_hold_and_resumes_with_pre_roll)
{
    namespace ef = lynx::edisonfx;
    ef::Gate g;
    g.prepare (48000.0);
    CHECK (g.holdFrames() == 48000);
    g.arm (ef::Mode::input);
    CHECK (! g.block (0.0f, 480, false, false, 0.02f).write);
    auto d = g.block (0.3f, 480, false, false, 0.02f);
    CHECK (d.write && d.preRoll);
    d = g.block (0.3f, 480, false, false, 0.02f);
    CHECK (d.write && ! d.preRoll); // going on: no second pre-roll
    // silence: 1 s of hold = 100 blocks of 480; the 99 first are still written, the 100th is not
    int writtenBlocks = 0;
    for (int i = 0; i < 100; ++i)
        writtenBlocks += g.block (0.0f, 480, false, false, 0.02f).write ? 1 : 0;
    CHECK (writtenBlocks == 99);
    CHECK (g.phase() == ef::Phase::waiting);
    CHECK (! g.block (0.0f, 480, false, false, 0.02f).write);
    d = g.block (0.3f, 480, false, false, 0.02f);
    CHECK (d.write && d.preRoll); // heard again: a new piece with its pre-roll
    // a sound inside the hold starts the hold again
    for (int i = 0; i < 50; ++i)
        g.block (0.0f, 480, false, false, 0.02f);
    g.block (0.3f, 480, false, false, 0.02f);
    writtenBlocks = 0;
    for (int i = 0; i < 100; ++i)
        writtenBlocks += g.block (0.0f, 480, false, false, 0.02f).write ? 1 : 0;
    CHECK (writtenBlocks == 99);
    // the threshold is the plugin's own
    CHECK (! g.block (0.3f, 480, false, false, 0.5f).write);
    CHECK (g.block (0.6f, 480, false, false, 0.5f).write);
}

TEST (edison190cd_gate_on_play_follows_the_transport_and_marks_song_jumps)
{
    namespace ef = lynx::edisonfx;
    ef::Gate g;
    g.prepare (44100.0);
    g.arm (ef::Mode::onPlay);
    CHECK (g.phase() == ef::Phase::waiting);
    CHECK (! g.block (1.0f, 512, false, false, 0.02f).write); // loud but stopped
    auto d = g.block (0.0f, 512, true, false, 0.02f);
    CHECK (d.write && d.jumpMarker && ! d.preRoll); // silent but playing; FL marks where the take starts (round 190ct)
    CHECK (g.phase() == ef::Phase::recording);
    d = g.block (0.0f, 512, true, false, 0.02f);
    CHECK (d.write && ! d.jumpMarker);
    d = g.block (0.0f, 512, true, true, 0.02f);
    CHECK (d.write && d.jumpMarker); // the song jumped (loop, click on the timeline)
    d = g.block (0.0f, 512, true, false, 0.02f);
    CHECK (d.write && ! d.jumpMarker); // one marker per jump
    CHECK (! g.block (0.0f, 512, false, false, 0.02f).write); // stop pauses
    CHECK (g.phase() == ef::Phase::waiting);
    d = g.block (0.0f, 512, true, false, 0.02f);
    CHECK (d.write && d.jumpMarker); // the song starts again: a marker where it resumes
}

TEST (edison190cd_idle_passes_the_audio_through_bit_exact)
{
    namespace ef = lynx::edisonfx;
    ef::Processor p;
    p.prepare (48000.0);
    const auto params = edison190cd::defaults();
    std::uint32_t seed = 11;
    bool same = true;
    for (int block = 0; block < 60; ++block)
    {
        const auto n = 1 + (int) (seed % 700u);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            l[(std::size_t) i] = (float) (seed >> 8) / 8388608.0f - 1.0f;
            seed = seed * 1664525u + 1013904223u;
            r[(std::size_t) i] = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 1.0e-30f; // (denormal-sized values too)
        }
        const auto inL = l, inR = r;
        p.setSongPosition (block * 37.0);
        p.setPlaying (block % 3 == 0);
        p.process (l.data(), r.data(), n, params, 128.0);
        same = same && std::memcmp (l.data(), inL.data(), (std::size_t) n * sizeof (float)) == 0
               && std::memcmp (r.data(), inR.data(), (std::size_t) n * sizeof (float)) == 0;
    }
    CHECK (same);
    CHECK (p.phase() == ef::Phase::off);
    CHECK (p.playPosition() < 0.0);

    // …and while it records too (recording only reads the input)
    p.setRecording (true);
    auto now = params;
    now[ef::recordMode] = ef::modeValue (ef::Mode::now);
    std::vector<float> l (256, 0.25f), r (256, -0.5f);
    p.process (l.data(), r.data(), 256, now, 128.0);
    CHECK (l[0] == 0.25f && l[255] == 0.25f && r[0] == -0.5f && r[255] == -0.5f);
    CHECK (p.phase() == ef::Phase::recording);
    p.setRecording (false);
}

TEST (edison190cd_mute_input_silences_the_slot_but_still_records_and_plays)
{
    namespace ef = lynx::edisonfx;
    namespace ae = lynx::audioedit;
    ef::Processor p;
    p.prepare (44100.0);
    auto params = edison190cd::defaults();
    params[ef::muteInput] = 1.0f;
    params[ef::recordMode] = ef::modeValue (ef::Mode::now);
    auto out = edison190cd::run (p, params, 0.5f, 1);
    CHECK (out[0] == 0.0f && out[440] == 0.0f);

    p.setRecording (true);
    out = edison190cd::run (p, params, 0.5f, 3);
    CHECK (out[0] == 0.0f);
    std::vector<float> l, r;
    std::vector<std::int64_t> jumps;
    CHECK (p.drain (l, r, jumps, 0) == 3 * 441);
    CHECK (l.size() == 3 * 441 && l[0] == 0.5f && r[0] == -0.5f && l.back() == 0.5f);
    p.setRecording (false);

    // playback is added after the mute: the clip is heard alone
    const auto doc = std::make_shared<const ae::Document> (edison190cd::ramp (1000));
    p.play (std::shared_ptr<const ae::Audio> (doc, &doc->audio), false);
    std::vector<float> pl (441, 0.5f), pr (441, 0.5f);
    p.process (pl.data(), pr.data(), 441, params, 120.0);
    bool same = true;
    for (int i = 0; i < 441; ++i)
        same = same && pl[(std::size_t) i] == doc->audio.channels[0][(std::size_t) i] && pr[(std::size_t) i] == doc->audio.channels[1][(std::size_t) i];
    CHECK (same);
    CHECK (std::abs (p.playPosition() - 441.0) < 1.0e-9);
    // unmuted: input + clip
    params[ef::muteInput] = 0.0f;
    std::fill (pl.begin(), pl.end(), 0.5f);
    std::fill (pr.begin(), pr.end(), 0.5f);
    p.process (pl.data(), pr.data(), 441, params, 120.0);
    CHECK (std::abs (pl[0] - (0.5f + doc->audio.channels[0][441])) < 1.0e-7f);
    // the clip ends: silence added, the position gone
    edison190cd::run (p, params, 0.0f, 3);
    CHECK (p.playPosition() < 0.0);
    out = edison190cd::run (p, params, 0.125f, 1);
    CHECK (out[0] == 0.125f && out[440] == 0.125f);
    // a looped clip goes round
    p.play (std::shared_ptr<const ae::Audio> (doc, &doc->audio), true);
    edison190cd::run (p, params, 0.0f, 5);
    CHECK (p.playPosition() >= 0.0 && p.playPosition() < 1000.0);
    p.play (nullptr, false);
    out = edison190cd::run (p, params, 0.125f, 1);
    CHECK (out[0] == 0.125f);
}

TEST (edison190cd_on_input_keeps_the_pre_roll_and_the_take_is_the_input)
{
    namespace ef = lynx::edisonfx;
    ef::Processor p;
    p.prepare (44100.0);
    const auto params = edison190cd::defaults(); // On input, threshold 0.02
    p.setRecording (true);
    edison190cd::run (p, params, 0.001f, 30); // 0.3 s of near-silence: waiting
    CHECK (p.phase() == ef::Phase::waiting);
    std::vector<float> l, r;
    std::vector<std::int64_t> jumps;
    CHECK (p.drain (l, r, jumps, 0) == 0);
    edison190cd::run (p, params, 0.5f, 2);
    CHECK (p.phase() == ef::Phase::recording);
    edison190cd::run (p, params, 0.0f, 4); // silence does not stop it
    CHECK (p.phase() == ef::Phase::recording);
    p.drain (l, r, jumps, 0);
    // 0.1 s of pre-roll (4410 frames of the near-silence), the two loud blocks, the four silent ones
    CHECK (l.size() == 4410 + 6 * 441);
    CHECK (l[0] == 0.001f && l[4409] == 0.001f && l[4410] == 0.5f && r[4410] == -0.5f);
    CHECK (l[4410 + 2 * 441] == 0.0f);
    CHECK (jumps.empty());
    CHECK (p.droppedFrames() == 0);
    p.setRecording (false);
    CHECK (p.phase() == ef::Phase::off);
}

TEST (edison190cd_on_play_records_only_while_the_song_plays_and_notes_jumps)
{
    namespace ef = lynx::edisonfx;
    ef::Processor p;
    p.prepare (44100.0);
    auto params = edison190cd::defaults();
    params[ef::recordMode] = ef::modeValue (ef::Mode::onPlay);
    p.setRecording (true);
    const auto ticksPerBlock = 441.0 / 44100.0 * (120.0 / 60.0) * (double) lynx::ppq;
    double tick = 0.0;
    auto block = [&] (bool playing, float level) {
        p.setSongPosition (tick);
        p.setPlaying (playing);
        edison190cd::run (p, params, level, 1);
        if (playing)
            tick += ticksPerBlock;
    };
    for (int i = 0; i < 5; ++i)
        block (false, 0.5f);
    CHECK (p.phase() == ef::Phase::waiting);
    for (int i = 0; i < 10; ++i)
        block (true, 0.25f);
    CHECK (p.phase() == ef::Phase::recording);
    tick = 4.0 * lynx::ppq * 8; // the song jumps eight bars on
    for (int i = 0; i < 10; ++i)
        block (true, 0.75f);
    for (int i = 0; i < 5; ++i)
        block (false, 0.5f);
    CHECK (p.phase() == ef::Phase::waiting);
    std::vector<float> l, r;
    std::vector<std::int64_t> jumps;
    p.drain (l, r, jumps, 0);
    CHECK (l.size() == 20 * 441);
    CHECK (l[0] == 0.25f && l[10 * 441 - 1] == 0.25f && l[10 * 441] == 0.75f);
    CHECK (jumps.size() == 2); // the take's start (FL's "Song jump" at 0, round 190ct) and the jump
    if (jumps.size() == 2)
        CHECK (jumps[0] == 0 && jumps[1] == 10 * 441);
    p.setRecording (false);
}

TEST (edison190cd_the_maximum_recording_time_ends_the_take)
{
    namespace ef = lynx::edisonfx;
    ef::Processor p;
    p.prepare (1000.0); // (a low rate: one minute is 60000 frames)
    auto params = edison190cd::defaults();
    params[ef::recordMode] = ef::modeValue (ef::Mode::now);
    params[ef::maxTime] = 1.0f / 6.0f; // 1 minute
    CHECK (ef::maxFramesOf (params, 1000.0) == 60000);
    p.setRecording (true);
    edison190cd::run (p, params, 0.5f, 59, 1000);
    CHECK (p.isRecording() && ! p.reachedLimit());
    edison190cd::run (p, params, 0.5f, 3, 1000);
    CHECK (! p.isRecording());
    CHECK (p.reachedLimit());
    std::vector<float> l, r;
    std::vector<std::int64_t> jumps;
    p.drain (l, r, jumps, 0);
    CHECK (l.size() == 60000);
    // forever: goes on
    params[ef::maxTime] = 0.0f;
    p.setRecording (true);
    CHECK (! p.reachedLimit());
    edison190cd::run (p, params, 0.5f, 70, 1000);
    CHECK (p.isRecording());
    p.setRecording (false);
}

TEST (edison190cd_a_take_is_appended_or_replaces_the_audio)
{
    namespace ef = lynx::edisonfx;
    namespace ae = lynx::audioedit;
    auto base = edison190cd::ramp (1000);
    base.title = "Take";
    ae::addMarker (base, 100, "here");
    ae::Audio take;
    take.sampleRate = 44100.0;
    take.channels.assign (2, std::vector<float> (500, 0.5f));

    const auto appended = ef::withTake (base, take, { 250 }, true);
    CHECK (appended.audio.frames() == 1500);
    CHECK (appended.audio.channels[0][999] == base.audio.channels[0][999]);
    CHECK (appended.audio.channels[0][1000] == 0.5f && appended.audio.channels[1][1499] == 0.5f);
    CHECK (appended.markers.size() == 2);
    if (appended.markers.size() == 2)
    {
        CHECK (appended.markers[0].start == 100 && appended.markers[0].name == "here");
        CHECK (appended.markers[1].start == 1250 && appended.markers[1].name == "Song jump");
    }
    CHECK (appended.title == "Take");

    const auto replaced = ef::withTake (base, take, { 250 }, false);
    CHECK (replaced.audio.frames() == 500);
    CHECK (replaced.audio.channels[0][0] == 0.5f);
    CHECK (replaced.markers.size() == 1 && replaced.markers[0].start == 250);

    // nothing there yet: Append makes the take the audio
    ae::Document empty;
    const auto first = ef::withTake (empty, take, {}, true);
    CHECK (first.audio.frames() == 500 && first.audio.sampleRate == 44100.0 && first.markers.empty());

    // a take at another rate is converted to the audio's rate, its markers with it
    ae::Audio half = take;
    half.sampleRate = 22050.0;
    const auto mixed = ef::withTake (base, half, { 250 }, true);
    CHECK (mixed.audio.sampleRate == 44100.0);
    CHECK (std::abs ((int) mixed.audio.frames() - 2000) <= 2);
    CHECK (mixed.markers.size() == 2 && mixed.markers[1].start == 1500);
}

TEST (edison190cd_a_session_records_through_the_slot_effect_with_the_window_closed)
{
    namespace ef = lynx::edisonfx;
    namespace ae = lynx::audioedit;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::edison);
    slot.params[ef::recordMode] = ef::modeValue (ef::Mode::now);
    const auto fx = ef::make (slot, 44100.0);
    CHECK (fx != nullptr);
    const auto p = std::dynamic_pointer_cast<ef::Processor> (fx);
    CHECK (p != nullptr);
    auto session = ef::findSession (slot.instance.id);
    CHECK (session != nullptr);
    if (p == nullptr || session == nullptr)
        return;
    CHECK (ef::sessionFor (slot) == session); // one per slot instance
    CHECK (session->document().audio.empty() && ! session->dirty() && session->path().empty());

    const auto revision = session->revision();
    CHECK (session->start (p, true));
    CHECK (session->recording());
    edison190cd::run (*p, slot.params, 0.5f, 10);
    CHECK (session->pump());
    CHECK (session->takeFrames() == 4410);
    CHECK (session->preview().audio.frames() == 4410);
    CHECK (session->document().audio.empty()); // the document changes when the take ends
    edison190cd::run (*p, slot.params, 0.25f, 10);
    session->stop();
    CHECK (! session->recording() && ! p->isRecording());
    CHECK (session->document().audio.frames() == 8820);
    CHECK (session->document().audio.channels[0][0] == 0.5f && session->document().audio.channels[0][8819] == 0.25f);
    CHECK (session->dirty());
    CHECK (session->revision() != revision);

    // a second take with Append: after the first; without: instead of it
    CHECK (session->start (p, true));
    CHECK (session->takeStart() == 8820);
    edison190cd::run (*p, slot.params, 0.75f, 5);
    session->stop();
    CHECK (session->document().audio.frames() == 8820 + 2205);
    CHECK (session->document().audio.channels[0][8820] == 0.75f);
    CHECK (session->start (p, false));
    CHECK (session->takeStart() == 0);
    edison190cd::run (*p, slot.params, 0.125f, 5);
    session->stop();
    CHECK (session->document().audio.frames() == 2205);
    CHECK (session->document().audio.channels[0][0] == 0.125f);

    // a recording with nothing in it leaves the audio alone
    CHECK (session->start (p, false));
    session->stop();
    CHECK (session->document().audio.frames() == 2205);

    // a copy of the song (same instance) finds the same audio; a duplicate slot is a new, empty one
    auto copy = slot;
    CHECK (ef::sessionFor (copy) == session);
    copy.instance.renew();
    CHECK (ef::findSession (copy.instance.id) == nullptr);

    // the session lives as long as the effect or a window holds it
    const auto id = slot.instance.id;
    session.reset();
    CHECK (ef::findSession (id) != nullptr);
}

TEST (edison190cd_slave_playback_plays_the_content_at_the_song_time)
{
    namespace ef = lynx::edisonfx;
    namespace ae = lynx::audioedit;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::edison);
    slot.params[ef::slave] = 1.0f;
    const auto fx = ef::make (slot, 44100.0);
    const auto p = std::dynamic_pointer_cast<ef::Processor> (fx);
    const auto session = ef::findSession (slot.instance.id);
    CHECK (p != nullptr && session != nullptr);
    if (p == nullptr || session == nullptr)
        return;
    const auto doc = edison190cd::ramp (44100);
    session->setDocument (doc);

    std::vector<float> l (441, 0.0f), r (441, 0.0f);
    // stopped: nothing is added
    p->setPlaying (false);
    p->setSongPosition (0.0);
    p->process (l.data(), r.data(), 441, slot.params, 120.0);
    CHECK (l[5] == 0.0f);
    // playing from the start of the song: the content from its first frame
    p->setPlaying (true);
    p->process (l.data(), r.data(), 441, slot.params, 120.0);
    bool same = true;
    for (int i = 0; i < 441; ++i)
        same = same && l[(std::size_t) i] == doc.audio.channels[0][(std::size_t) i];
    CHECK (same);
    // the song jumps to 0.5 s (one beat at 120): the content follows
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    p->setSongPosition ((double) lynx::ppq);
    p->process (l.data(), r.data(), 441, slot.params, 120.0);
    CHECK (std::abs (l[0] - doc.audio.channels[0][22050]) < 1.0e-6f);
    CHECK (std::abs (r[440] - doc.audio.channels[1][22050 + 440]) < 1.0e-6f);
    // with a selection the content starts at it (FL: song time + selection start); "ignore own selection" undoes that
    session->setSlaveOffset (1000);
    std::fill (l.begin(), l.end(), 0.0f);
    p->setSongPosition (0.0);
    p->process (l.data(), r.data(), 441, slot.params, 120.0);
    CHECK (std::abs (l[0] - doc.audio.channels[0][1000]) < 1.0e-6f);
    auto ignoring = slot.params;
    ignoring[ef::ignoreOwnSelection] = 1.0f;
    std::fill (l.begin(), l.end(), 0.0f);
    p->setSongPosition (0.0);
    p->process (l.data(), r.data(), 441, ignoring, 120.0);
    CHECK (std::abs (l[0] - doc.audio.channels[0][0]) < 1.0e-6f);
    CHECK (std::abs (l[7] - doc.audio.channels[0][7]) < 1.0e-6f);
    // past the end of the content: silence
    std::fill (l.begin(), l.end(), 0.0f);
    p->setSongPosition (16.0 * lynx::ppq);
    p->process (l.data(), r.data(), 441, slot.params, 120.0);
    CHECK (l[0] == 0.0f && l[440] == 0.0f);
    // Slave off: the song plays, the content does not
    auto off = slot.params;
    off[ef::slave] = 0.0f;
    p->setSongPosition (0.0);
    p->process (l.data(), r.data(), 441, off, 120.0);
    CHECK (l[0] == 0.0f && l[440] == 0.0f);
}

TEST (edison190cd_the_audio_of_a_slot_round_trips_through_its_project_file)
{
    namespace ef = lynx::edisonfx;
    namespace ae = lynx::audioedit;
    const auto path = (std::filesystem::temp_directory_path() / "lynx-edison190cd-take.wav").string();
    std::remove (path.c_str());

    auto slot = lynx::makeEffectSlot (lynx::EffectKind::edison);
    {
        const auto session = ef::sessionFor (slot);
        auto doc = edison190cd::ramp (5000, 48000.0);
        ae::addMarker (doc, 1200, "verse");
        ae::addMarker (doc, 3000);
        ae::setLoop (doc, { 500, 4000 });
        session->setDocument (doc);
        CHECK (session->dirty());
        CHECK (session->save (path));
        CHECK (! session->dirty() && session->path() == path);
        slot.samplePath = path;
    }
    CHECK (ef::findSession (slot.instance.id) == nullptr); // nothing holds it any more

    // "the project is opened again": a new slot instance with the saved path
    auto loaded = slot;
    loaded.instance.renew();
    const auto fx = ef::make (loaded, 44100.0);
    const auto session = ef::findSession (loaded.instance.id);
    CHECK (session != nullptr);
    if (session == nullptr)
        return;
    const auto want = edison190cd::ramp (5000, 48000.0);
    const auto& got = session->document();
    CHECK (got.audio.sampleRate == 48000.0 && got.audio.numChannels() == 2 && got.audio.frames() == 5000);
    CHECK (got.audio.channels == want.audio.channels); // 32-bit float: bit for bit
    CHECK (got.markers.size() == 2);
    if (got.markers.size() == 2)
        CHECK (got.markers[0].start == 1200 && got.markers[0].name == "verse" && got.markers[1].start == 3000);
    CHECK (got.loopStart == 500 && got.loopEnd == 4000);
    CHECK (! session->dirty() && session->path() == path);

    // the path changes under a running effect (a song undo past the recording, a relocation): an editor that holds
    // audio keeps it; an empty one reads what the slot names
    const auto other = (std::filesystem::temp_directory_path() / "lynx-edison190cd-other.wav").string();
    CHECK (ef::writeWavFile (edison190cd::ramp (700), other));
    loaded.samplePath = other;
    ef::followPath (loaded);
    CHECK (session->document().audio.frames() == 5000 && session->path() == path);
    loaded.samplePath.clear();
    ef::followPath (loaded);
    CHECK (session->document().audio.frames() == 5000);
    session->setDocument ({});
    CHECK (session->save ({}));
    loaded.samplePath = other;
    ef::followPath (loaded);
    CHECK (session->document().audio.frames() == 700 && session->path() == other && ! session->dirty());
    loaded.samplePath = (std::filesystem::temp_directory_path() / "lynx-edison190cd-missing.wav").string();
    ef::followPath (loaded);
    CHECK (session->document().audio.frames() == 700);

    // an emptied editor forgets its file
    session->setDocument ({});
    CHECK (session->save (path));
    CHECK (session->path().empty() && ! session->dirty());
    std::remove (path.c_str());
    std::remove (other.c_str());
}

TEST (edison190cd_fl_state_keeps_the_slot_as_the_plugin_with_settings_and_audio)
{
    namespace ef = lynx::edisonfx;
    namespace ae = lynx::audioedit;
    auto source = edison190cd::ramp (3000);
    ae::addMarker (source, 1500, "drop");
    const auto wav = ae::encodeWav (source, ae::WavFormat::float32);
    const auto state = edison190cd::flState (wav, 3, true, true, true, 5, true, true, 0.05f);

    const auto parsed = ef::parseFlState (state);
    CHECK (parsed.has_value());
    if (! parsed)
        return;
    CHECK (parsed->version == 13);
    CHECK (parsed->mode == ef::Mode::onPlay);
    CHECK (parsed->newProject && parsed->loop && parsed->muteInput && parsed->slave && parsed->ignoreOwnSelection);
    CHECK (parsed->maxTime == 5);
    CHECK (std::abs (parsed->noiseThreshold - 0.05f) < 1.0e-7f);
    CHECK (parsed->wavSize == wav.size());

    const auto slot = lynx::effectFromFl ("Edison", state, 140.0);
    CHECK (slot.has_value());
    if (! slot)
        return;
    CHECK (slot->kind == lynx::EffectKind::edison);
    CHECK (ef::modeOf (slot->params) == ef::Mode::onPlay);
    CHECK (slot->params[ef::append] < 0.5f);
    CHECK (slot->params[ef::loopPlay] >= 0.5f);
    CHECK (slot->params[ef::muteInput] >= 0.5f);
    CHECK (slot->params[ef::slave] >= 0.5f);
    CHECK (slot->params[ef::ignoreOwnSelection] >= 0.5f);
    CHECK (ef::maxTimeIndexOf (slot->params) == 5);
    CHECK (std::abs (slot->params[ef::noiseThreshold] - 0.05f) < 1.0e-7f);
    CHECK (slot->state == state); // FL's bytes stay with the slot until its audio changes
    CHECK (slot->samplePath.empty());

    // the audio in the state is the slot's content
    const auto fx = ef::make (*slot, 44100.0);
    const auto session = ef::findSession (slot->instance.id);
    CHECK (session != nullptr);
    if (session != nullptr)
    {
        CHECK (session->document().audio.frames() == 3000);
        CHECK (session->document().audio.channels == source.audio.channels);
        CHECK (session->document().markers.size() == 1);
        CHECK (! session->dirty());
    }

    // FL's defaults (an empty plugin as FL saves it): On input, Append on, 30 minutes (index 5)
    const auto fresh = lynx::effectFromFl ("Edison", edison190cd::flState ({}, 1, false, false, false, 5, false, false, 0.02f), 140.0);
    CHECK (fresh.has_value());
    if (fresh)
    {
        const auto plain = lynx::makeEffectSlot (lynx::EffectKind::edison);
        CHECK (fresh->params == plain.params);
        const auto fx2 = ef::make (*fresh, 44100.0);
        const auto s2 = ef::findSession (fresh->instance.id);
        CHECK (s2 != nullptr && s2->document().audio.empty());
    }

    // garbage is not a reason to lose the slot
    CHECK (! ef::parseFlState ({}).has_value());
    const auto broken = lynx::effectFromFl ("Edison", { 1, 2, 3 }, 140.0);
    CHECK (broken.has_value() && broken->kind == lynx::EffectKind::edison);
    std::vector<std::uint8_t> cut (state.begin(), state.begin() + 40);
    const auto fxCut = lynx::effectFromFl ("Edison", cut, 140.0);
    CHECK (fxCut.has_value() && fxCut->kind == lynx::EffectKind::edison);
}

TEST (edison190cd_other_slots_and_old_projects_are_untouched)
{
    // the new kind is the last one: every earlier kind keeps its number (project files name kinds, FL import maps to them)
    CHECK ((int) lynx::EffectKind::edison == (int) lynx::EffectKind::rackMixer + 1);
    // no session appears for other kinds
    const auto reverb = lynx::makeEffectSlot (lynx::EffectKind::newtone);
    CHECK (lynx::edisonfx::findSession (reverb.instance.id) == nullptr);
    // the generic factory makes the effect too (a slot without the renderer: thumbnails, tools)
    const auto fx = lynx::makeEffect (lynx::EffectKind::edison);
    CHECK (fx != nullptr);
    if (fx != nullptr)
    {
        fx->prepare (44100.0);
        std::vector<float> l (64, 0.5f), r (64, 0.25f);
        fx->process (l.data(), r.data(), 64, lynx::makeEffectSlot (lynx::EffectKind::edison).params, 120.0);
        CHECK (l[0] == 0.5f && r[63] == 0.25f);
    }
}

// Round 190ct (live FL check 190cq): the mixer's "Open audio editor and record" loads an Edison set to On play, 5 min,
// Append off, Slave on (shot edison190cd-05b); a plain open and a fresh slot keep On input, 30 min, Append on, Slave off
// (05c / 05d). Old slots keep what they saved.
TEST (edison190ct_open_and_record_preset_and_fresh_defaults)
{
    namespace ef = lynx::edisonfx;
    const auto fresh = lynx::makeEffectSlot (lynx::EffectKind::edison);
    auto armed = fresh;
    ef::setOpenAndRecord (armed.params);
    CHECK (ef::modeOf (armed.params) == ef::Mode::onPlay);
    CHECK (ef::maxTimeIndexOf (armed.params) == 3 && ef::maxMinutes[3] == 5);
    CHECK (armed.params[ef::append] < 0.5f && armed.params[ef::slave] >= 0.5f);
    // the rest stays the fresh plugin's
    CHECK (armed.params[ef::muteInput] == fresh.params[ef::muteInput] && armed.params[ef::loopPlay] == fresh.params[ef::loopPlay]
           && armed.params[ef::noiseThreshold] == fresh.params[ef::noiseThreshold]
           && armed.params[ef::ignoreOwnSelection] == fresh.params[ef::ignoreOwnSelection]);
    CHECK (ef::modeOf (fresh.params) == ef::Mode::onInput && ef::maxTimeIndexOf (fresh.params) == 5 && fresh.params[ef::append] >= 0.5f
           && fresh.params[ef::slave] < 0.5f);
    CHECK (ef::paramText (ef::maxTime, fresh.params[ef::maxTime]) == "30 min");

    // a slot saved before (5 min, the earlier default) keeps its value: the value is in the saved parameters
    auto old = fresh;
    old.params[ef::maxTime] = 3.0f / 6.0f;
    CHECK (ef::maxTimeIndexOf (old.params) == 3);
    // an FL state too old to hold the time gets FL's default
    CHECK (ef::FlState {}.maxTime == 5);
}

// Round 190ct (live FL check 190cq, shot edison190cd-01-02): in On play FL writes "Song jump" at the start of every take
// — frame 0 of a replacing take, the join of an appended one — and where the song starts again while armed.
TEST (edison190ct_on_play_marks_the_start_of_each_take)
{
    namespace ef = lynx::edisonfx;
    namespace ae = lynx::audioedit;
    ef::Processor p;
    p.prepare (44100.0);
    auto params = edison190cd::defaults();
    params[ef::recordMode] = ef::modeValue (ef::Mode::onPlay);
    p.setRecording (true);
    double tick = 0.0;
    const auto ticksPerBlock = 441.0 / 44100.0 * (120.0 / 60.0) * (double) lynx::ppq;
    auto block = [&] (bool playing) {
        p.setSongPosition (tick);
        p.setPlaying (playing);
        edison190cd::run (p, params, 0.3f, 1);
        if (playing)
            tick += ticksPerBlock;
    };
    for (int i = 0; i < 3; ++i)
        block (false);
    for (int i = 0; i < 4; ++i)
        block (true);
    for (int i = 0; i < 3; ++i)
        block (false); // stop: the take pauses
    tick = 0.0;            // play again from the start
    for (int i = 0; i < 4; ++i)
        block (true);
    std::vector<float> l, r;
    std::vector<std::int64_t> jumps;
    p.drain (l, r, jumps, 0);
    p.setRecording (false);
    CHECK (l.size() == 8 * 441);
    CHECK (jumps == (std::vector<std::int64_t> { 0, 4 * 441 }));

    // replacing: the first marker sits at 0; appending: at the join
    ae::Audio take;
    take.sampleRate = 44100.0;
    take.channels.assign (2, std::vector<float> (800, 0.1f));
    const auto base = edison190cd::ramp (1000);
    const auto replaced = ef::withTake (base, take, { 0 }, false);
    CHECK (replaced.markers.size() == 1 && replaced.markers[0].start == 0 && replaced.markers[0].name == "Song jump");
    const auto appended = ef::withTake (base, take, { 0 }, true);
    CHECK (appended.audio.frames() == 1800 && appended.markers.size() == 1 && appended.markers[0].start == 1000
           && appended.markers[0].name == "Song jump");

    // Input mode still writes no marker (live check: none at the start or a resume)
    ef::Gate g;
    g.prepare (44100.0);
    g.arm (ef::Mode::input);
    CHECK (! g.block (0.5f, 512, true, false, 0.02f).jumpMarker);
}

// Round 190ct (live FL check 190cq, shot edison190cd-07b): "Remove all Edison instances" empties every Edison slot (the
// question comes first, in the UI); the other effects keep their rows.
TEST (edison190ct_remove_all_edison_instances)
{
    lynx::Song song;
    song.mixer.resize (3);
    song.mixer[1].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::edison));
    song.mixer[1].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::newtone));
    song.mixer[1].effects.back().row = 3;
    song.mixer[2].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::edison));
    song.mixer[2].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::edison));
    CHECK (lynx::mixerops::removeEffectsOfKind (song, lynx::EffectKind::edison) == 3);
    CHECK (song.mixer[1].effects.size() == 1 && song.mixer[1].effects[0].kind == lynx::EffectKind::newtone && song.mixer[1].effects[0].row == 3);
    CHECK (song.mixer[2].effects.empty());
    CHECK (lynx::mixerops::removeEffectsOfKind (song, lynx::EffectKind::edison) == 0); // none left: nothing happens
}
