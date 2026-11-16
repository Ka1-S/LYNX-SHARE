// Round 190hq, slice B11 (community complaint #22 "tempo changes break audio clips; no tempo map"): tempo markers on the
// Playlist ruler write one tempo automation clip (core/TempoMarkers.h); a channel stretched to its beats in Resample mode
// stays on the beat under them; a launched scene's tempo holds them (Live's scene tempo over its tempo automation).

#include "core/TempoMarkers.h"

TEST (tempomarkers190hq_edit_the_tempo_map)
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    CHECK (tempomarkers::list (s).empty() && tempomarkers::automationIndex (s) < 0);

    // a marker at bar 2 (90 BPM), one at bar 3 gliding up to 140: one tempo automation, one clip from the song's start
    CHECK (tempomarkers::add (s, 4 * ppq, 90.0) == 0);
    CHECK (tempomarkers::add (s, 8 * ppq, 140.0, true) == 1);
    const auto a = tempomarkers::automationIndex (s);
    CHECK (a == 0 && s.automations.size() == 1 && s.automations[0].tempoMarkers && s.automations[0].points.size() == 3);
    const auto* clip = tempomarkers::clipOf (s, a);
    CHECK (clip != nullptr && clip->start == 0 && clip->length == 8 * ppq + s.barTicks());
    auto marks = tempomarkers::list (s);
    CHECK (marks.size() == 2 && marks[0] == (tempomarkers::Mark { 4 * ppq, marks[0].bpm, false }) && marks[1].glide);
    CHECK (std::abs (marks[0].bpm - 90.0) < 0.01 && std::abs (marks[1].bpm - 140.0) < 0.01);
    CHECK (std::abs (tempomarkers::startTempo (s) - 120.0) < 0.01);

    // the tempo map: 120 to bar 2, then a straight ramp from 90 into the gliding 140 at bar 3, then 140 for ever
    CHECK (std::abs (s.tempoAt (2.0 * ppq) - 120.0) < 0.01);
    CHECK (std::abs (s.tempoAt (4.0 * ppq) - 90.0) < 0.01);
    CHECK (std::abs (s.tempoAt (6.0 * ppq) - 115.0) < 0.5);
    CHECK (std::abs (s.tempoAt (100.0 * ppq) - 140.0) < 0.01);
    CHECK (tempomap::active (s));

    // the same tick again: that marker takes the new tempo; tick 0 is the start tempo
    CHECK (tempomarkers::add (s, 4 * ppq, 100.0) == 0 && tempomarkers::list (s).size() == 2);
    CHECK (tempomarkers::add (s, 0, 110.0) == -1 && std::abs (tempomarkers::startTempo (s) - 110.0) < 0.01);

    // moving the 140 marker before the first one: the list stays sorted and it keeps its glide; the one it passed keeps its own
    CHECK (tempomarkers::move (s, 1, 2 * ppq) == 0);
    marks = tempomarkers::list (s);
    CHECK (marks.size() == 2 && marks[0].tick == 2 * ppq && marks[0].glide && std::abs (marks[0].bpm - 140.0) < 0.01);
    CHECK (marks[1].tick == 4 * ppq && ! marks[1].glide && std::abs (marks[1].bpm - 100.0) < 0.01);
    CHECK (tempomarkers::clipOf (s, a)->length == 4 * ppq + s.barTicks());

    // glide on / off, a tempo typed in (clamped to FL's 10…522)
    tempomarkers::setGlide (s, 0, false);
    tempomarkers::setTempo (s, 1, 9000.0);
    marks = tempomarkers::list (s);
    CHECK (! marks[0].glide && std::abs (marks[1].bpm - maxTempo) < 0.01);
    CHECK (tempomarkers::near (s, 4 * ppq + 10, 20) == 1 && tempomarkers::near (s, 3 * ppq, 20) == -1);

    // removing every marker takes the automation and its clip away; the song keeps the start tempo
    tempomarkers::remove (s, 0);
    tempomarkers::remove (s, 0);
    CHECK (tempomarkers::automationIndex (s) < 0 && s.automations.empty() && s.playlist.empty());
    CHECK (std::abs (s.bpm - 110.0) < 0.01 && ! tempomap::active (s));

    // another automation before the markers' one is renumbered with it when it goes
    Automation other;
    other.target = { AutomationTargetKind::mixerVolume, 1 };
    s.automations.push_back (other);
    tempomarkers::add (s, ppq, 80.0);
    CHECK (tempomarkers::automationIndex (s) == 1 && tempomarkers::clipOf (s, 1) != nullptr);
    tempomarkers::remove (s, 0);
    CHECK (s.automations.size() == 1 && s.playlist.empty());

    // a deleted clip comes back with the next marker
    tempomarkers::add (s, ppq, 80.0);
    s.playlist.clear();
    CHECK (tempomarkers::list (s).empty());
    tempomarkers::add (s, 2 * ppq, 70.0);
    CHECK (tempomarkers::list (s).size() == 2);

    CHECK (tempomarkers::tempoText (128.0) == "128 BPM" && tempomarkers::tempoText (92.5) == "92.5 BPM" && tempomarkers::tempoText (99.126) == "99.13 BPM");
}

TEST (tempomarkers190hq_resampled_audio_stays_on_the_beat)
{
    using namespace lynx;
    // an audio clip of eight clicks, one per beat at 120 BPM (4 s at 48 kHz), stretched to its 8 beats in Resample mode;
    // tempo markers: 120, then 150 from beat 2, then 100 from beat 5. Each click must sound when the song reaches its beat.
    const double rate = 48000.0;
    auto make = [&] (bool markers) {
        Song s;
        s.bpm = 120.0;
        s.mode = PlayMode::song;
        s.mixer = Song::defaultMixer (1);
        ChannelDef ch;
        ch.instrument = InstrumentKind::audioClip;
        ch.samplePath = "clicks";
        ch.sampler.stretchBeats = 8.0f;
        ch.sampler.keepPitch = false;
        ch.mixerTrack = 1;
        s.channels.push_back (ch);
        s.patterns.emplace_back();
        Clip c;
        c.kind = ClipKind::audio;
        c.channel = 0;
        c.start = 0;
        c.length = 8 * ppq;
        s.playlist.push_back (c);
        if (markers)
        {
            tempomarkers::add (s, 2 * ppq, 150.0);
            tempomarkers::add (s, 5 * ppq, 100.0);
        }
        return s;
    };
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (4 * 48000, 0.0f);
        for (int k = 0; k < 8; ++k)
            d->left[(std::size_t) k * 24000] = 1.0f;
        d->right = d->left;
        return std::shared_ptr<const SampleData> (d);
    };
    for (const auto markers : { false, true })
    {
        const auto song = make (markers);
        auto state = RackState::build (song, nullptr, rate, provider);
        RackRenderer r;
        r.prepare (rate, 256);
        std::vector<float> l (256), rr (256);
        double tick = 0.0;
        std::vector<long> heard, expected;
        long last = -100000;
        int beat = 0;
        for (long b = 0; b * 256 < (long) (6.0 * rate) && beat < 8; ++b)
        {
            r.process (state.get(), true, tick, 256, l.data(), rr.data());
            const auto tps = ticksPerSample (r.currentBpm(), rate);
            for (int i = 0; i < 256; ++i)
            {
                const auto at = b * 256 + i;
                if (std::abs (l[(std::size_t) i]) > 0.05f && at - last > 4000)
                {
                    heard.push_back (at);
                    last = at;
                }
                // the song reaches beat k at this sample (the transport moves with the block's tempo, as the engine's)
                while (beat < 8 && tick + (i + 1) * tps > (double) beat * ppq)
                {
                    expected.push_back (at);
                    ++beat;
                }
            }
            tick += 256 * tps;
        }
        CHECK (heard.size() == 8 && expected.size() == 8);
        long worst = 0;
        for (std::size_t k = 0; k < std::min (heard.size(), expected.size()); ++k)
            worst = std::max (worst, std::labs (heard[k] - expected[k]));
        std::printf ("  resampled clicks vs beats (%s): worst %ld samples\n", markers ? "tempo markers" : "steady", worst);
        CHECK (worst <= 8); // (< 0.2 ms: the resampler's interpolation and the block's tempo step)
    }
}

TEST (tempomarkers190hq_a_launched_scene_tempo_holds_the_markers)
{
    using namespace lynx;
    // Live 12.4: a scene with its own tempo, launched, overrides the Arrangement's tempo automation until "Back to
    // Arrangement" — the song plays at its tempo (`bpm`) while sceneTempoHold is set
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    tempomarkers::add (s, 0, 120.0);
    tempomarkers::add (s, ppq, 90.0);
    auto bpmAt = [] (Song song, bool hold) {
        song.bpm = 133.0; // (the scene's tempo, as AudioEngine::launcherCommand sets it)
        song.sceneTempoHold = hold;
        auto state = RackState::build (song, nullptr, 48000.0);
        RackRenderer r;
        r.prepare (48000.0, 256);
        std::vector<float> l (256), rr (256);
        r.process (state.get(), true, 2.0 * ppq, 256, l.data(), rr.data());
        return r.currentBpm();
    };
    CHECK (std::abs (bpmAt (s, false) - 90.0) < 0.01);
    CHECK (std::abs (bpmAt (s, true) - 133.0) < 0.01);
    CHECK (! Song {}.sceneTempoHold);
}
