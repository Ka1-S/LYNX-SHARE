// Round 190gx: Ableton Live facts from the 190gu screen session applied Live-exact — the Clip Gain law, the Track Panning
// law, clip envelopes only on warped clips (core/ClipEnvelope.h, .als import / export). Uses clipenv190gi's helpers.

namespace livefacts190gx
{
// Live 12.4's Clip Gain renders (1 kHz sine, round 190gu, s/ana/clipenv-g2.txt): value → dB
struct Measured
{
    double value, db;
};
inline const Measured clipGain[] = {
    { 1.0, 0.0 },       { 0.9, -2.0 },      { 0.8, -4.0 },      { 0.7, -6.0 },      { 0.6, -8.0 },      { 0.5, -10.0 },     { 0.4, -12.0 },
    { 0.3, -14.0 },     { 0.2, -16.0 },     { 0.1, -18.0 },     { 0.0, -20.0 },     { -0.1, -22.0 },    { -0.2, -24.0 },    { -0.3, -26.6 },
    { -0.4, -30.2 },    { -0.5, -34.8 },    { -0.6, -40.4 },    { -0.7, -47.0 },    { -0.8, -54.6 },    { -0.82, -56.24 },  { -0.84, -57.92 },
    { -0.86, -59.64 },  { -0.88, -61.4 },   { -0.9, -63.2 },    { -0.92, -65.04 },  { -0.94, -66.92 },  { -0.96, -68.373 }, { -0.98, -69.4 },
};

// the song's volume ClipEnvelope events (Live time in beats, Live value), the "before the song" one left out
inline std::vector<std::pair<double, double>> volumeEvents (const std::string& xmlText)
{
    std::vector<std::pair<double, double>> out;
    lynx::xml::Document doc;
    if (! doc.parse (xmlText))
        return out;
    for (const auto& t : doc.root().child ("LiveSet").child ("Tracks").children ("AudioTrack"))
    {
        const auto vol = t.path ("DeviceChain/MainSequencer/VolumeModulationTarget").attr ("Id");
        for (const auto& clip : t.path ("DeviceChain/MainSequencer/Sample/ArrangerAutomation/Events").children ("AudioClip"))
            for (const auto& env : clip.path ("Envelopes/Envelopes").children ("ClipEnvelope"))
                if (env.path ("EnvelopeTarget/PointeeId").attr ("Value") == vol)
                    for (const auto& ev : env.path ("Automation/Events").children())
                        if (const auto time = std::stod (ev.attr ("Time")); time > -1.0e6)
                            out.push_back ({ time, std::stod (ev.attr ("Value")) });
    }
    return out;
}
// Live's straight line between its events at `beats`
inline double liveAt (const std::vector<std::pair<double, double>>& ev, double beats)
{
    if (ev.empty())
        return 1.0;
    if (beats <= ev.front().first)
        return ev.front().second;
    for (std::size_t k = 1; k < ev.size(); ++k)
        if (beats <= ev[k].first)
        {
            const auto [t0, v0] = ev[k - 1];
            const auto [t1, v1] = ev[k];
            return t1 > t0 ? v0 + (v1 - v0) * (beats - t0) / (t1 - t0) : v1;
        }
    return ev.back().second;
}
inline std::optional<lynx::Song> reimport (const std::string& xmlText, lynx::FlpImportReport* report = nullptr)
{
    lynx::xml::Document doc;
    if (! doc.parse (xmlText))
        return std::nullopt;
    lynx::AlsImportOptions o;
    o.common.projectFolder = "E:/sets";
    o.common.fileExists = [] (const std::string&) { return true; };
    o.common.report = report;
    return lynx::importAlsXml (doc, o);
}
inline const lynx::Clip* firstAudio (const lynx::Song& s)
{
    for (const auto& c : s.playlist)
        if (c.kind == lynx::ClipKind::audio)
            return &c;
    return nullptr;
}
// the volume ClipEnvelope's events replaced by `events` (a Live ramp written by hand)
inline std::string withVolumeEvents (std::string text, const std::string& events)
{
    const auto env = text.find ("<ClipEnvelope "); // (not <ClipEnvelopeChooserViewState>)
    if (env == std::string::npos)
        return {};
    const auto open = text.find ("<Events>", env), close = text.find ("</Events>", env);
    if (open == std::string::npos || close == std::string::npos || close < open)
        return {};
    text.replace (open + 8, close - open - 8, events);
    return text;
}
} // namespace livefacts190gx

TEST (livefacts190gx_clip_gain_law_matches_lives_renders)
{
    using namespace lynx;
    float worst = 0.0f;
    for (const auto& m : livefacts190gx::clipGain)
        worst = std::max (worst, (float) std::abs (clipenv::liveGainDb (m.value) - m.db));
    CHECK (worst < 0.011f);
    CHECK (clipenv::liveGain (-1.0) == 0.0 && clipenv::liveGain (1.0) == 1.0 && clipenv::liveGain (2.0) == 1.0); // (never above unity)
    // the upper part is exactly 10^(v − 1)
    for (const auto v : { 1.0, 0.55, 0.0, -0.2 })
        CHECK (std::abs (clipenv::liveGain (v) - std::pow (10.0, v - 1.0)) < 1.0e-12);
    // the law rises everywhere and its inverse gives the value back
    double last = -1.0e9, worstBack = 0.0;
    for (int k = -999; k <= 1000; ++k)
    {
        const auto v = k / 1000.0, db = clipenv::liveGainDb (v);
        CHECK (db > last);
        last = db;
        worstBack = std::max (worstBack, std::abs (clipenv::liveValueOfGain (clipenv::liveGain (v)) - v));
    }
    CHECK (worstBack < 1.0e-9);
    CHECK (clipenv::liveValueOfGain (0.0) == -1.0 && clipenv::liveValueOfGain (1.5) == 1.0);
}

TEST (livefacts190gx_lane_values_and_lives_values_are_the_same_gain)
{
    using namespace lynx;
    // Live → lane → the lane's own gain law (Levels adjustment) = Live's gain
    float worstDb = 0.0f;
    for (const auto& m : livefacts190gx::clipGain)
    {
        const auto v = clipenv::fromLive (clipenv::volumeLane, m.value);
        worstDb = std::max (worstDb, (float) std::abs (20.0 * std::log10 ((double) clipenv::gainOf (v)) - clipenv::liveGainDb (m.value)));
    }
    CHECK (worstDb < 0.01f);
    CHECK (clipenv::fromLive (clipenv::volumeLane, 1.0) == 1.0f && clipenv::fromLive (clipenv::volumeLane, -1.0) == 0.0f);
    CHECK (clipenv::toLive (clipenv::volumeLane, 1.0f) == 1.0 && clipenv::toLive (clipenv::volumeLane, 0.0f) == -1.0);
    // −10 dB (Live 0.5) is no longer the lane's middle (the 190gi linear map put 0.5 at 0.75)
    CHECK (std::abs (clipenv::toLive (clipenv::volumeLane, clipenv::fromLive (clipenv::volumeLane, 0.5)) - 0.5) < 1.0e-5);
    CHECK (std::abs (clipenv::fromLive (clipenv::volumeLane, 0.5) - 0.75f) > 0.05f);
    // the pan lane: identity between offset and Track Panning
    CHECK (clipenv::toLive (clipenv::panLane, 0.25f) == -0.5 && clipenv::fromLive (clipenv::panLane, 0.5) == 0.75f);
}

TEST (livefacts190gx_track_panning_law_is_constant_power_plus_3_db)
{
    // Live's Track Panning renders (190gu e3): p → L, R gains; LYNX renders a pan envelope at Live's value p with the same
    // gains (its voice pan law, against the same clip without the envelope)
    using namespace lynx;
    const std::array<std::array<double, 3>, 6> live { { { -1.0, 1.41421, 0.0 }, { -0.5, 1.30656, 0.54120 }, { -0.2, 1.14412, 0.83125 },
                                                        { 0.0, 1.0, 1.0 }, { 0.3, 0.73892, 1.20582 }, { 0.9, 0.11096, 1.40985 } } };
    std::vector<float> plainR;
    const auto plainL = clipenv190gi::render (clipenv190gi::dcSong(), 24000, &plainR);
    for (const auto& row : live)
    {
        auto song = clipenv190gi::dcSong();
        ClipEnvelope e;
        const auto v = clipenv::fromLive (clipenv::panLane, row[0]);
        e.pan = { clipenv190gi::point (0, v), clipenv190gi::point (ppq * 4, v) };
        e.volume = { clipenv190gi::point (0, 0.99f) }; // (active even at the centre)
        song.clipEnvelopes.push_back (e);
        song.playlist[0].envelope = 0;
        std::vector<float> r;
        const auto l = clipenv190gi::render (song, 24000, &r);
        const auto g = (double) clipenv::gainOf (0.99f);
        CHECK (std::abs (l[12000] / plainL[12000] / g - row[1]) < 3.0e-4 && std::abs (r[12000] / plainR[12000] / g - row[2]) < 3.0e-4);
    }
}

TEST (livefacts190gx_als_unwarped_clip_envelopes_are_left_out)
{
    using namespace lynx;
    auto text = exportAls (clipenv190gi::rampSong()).xml;
    // every exported audio clip is warped, so Live plays its envelope
    CHECK (text.find ("<IsWarped Value=\"true\"") != std::string::npos);
    FlpImportReport warpedReport;
    const auto warped = livefacts190gx::reimport (text, &warpedReport);
    CHECK (warped.has_value() && livefacts190gx::firstAudio (*warped) != nullptr && clipenv::activeOf (*warped, *livefacts190gx::firstAudio (*warped)) != nullptr);
    for (std::size_t at = 0; (at = text.find ("<IsWarped Value=\"true\"", at)) != std::string::npos;)
        text.replace (at, 22, "<IsWarped Value=\"false\"");
    FlpImportReport report;
    const auto plain = livefacts190gx::reimport (text, &report);
    CHECK (plain.has_value());
    if (! plain)
        return;
    const auto* audio = livefacts190gx::firstAudio (*plain);
    CHECK (audio != nullptr && clipenv::of (*plain, *audio) == nullptr && plain->clipEnvelopes.empty());
    CHECK (report.unsupported.count ("Clip envelopes of unwarped audio clips (left out: Live does not play them)") == 1);
}

TEST (livefacts190gx_als_import_follows_lives_ramp)
{
    // a Live ramp from 1 to −1 over 4 beats (straight in Live's value): LYNX's imported curve keeps Live's gain within
    // 0.1 dB wherever it is above −60 dB, and a step at 0.5 lands on −10 dB
    using namespace lynx;
    const auto base = exportAls (clipenv190gi::rampSong()).xml;
    const auto text = livefacts190gx::withVolumeEvents (base, "<FloatEvent Id=\"0\" Time=\"-63072000\" Value=\"1\" />"
                                                              "<FloatEvent Id=\"1\" Time=\"0\" Value=\"1\" />"
                                                              "<FloatEvent Id=\"2\" Time=\"4\" Value=\"-1\" />");
    CHECK (! text.empty());
    const auto song = livefacts190gx::reimport (text);
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* audio = livefacts190gx::firstAudio (*song);
    const auto* e = audio != nullptr ? clipenv::of (*song, *audio) : nullptr;
    CHECK (e != nullptr);
    if (e == nullptr)
        return;
    double worst = 0.0;
    int compared = 0;
    for (int k = 0; k <= 400; ++k)
    {
        const auto beats = k / 100.0, live = clipenv::liveGainDb (1.0 - beats / 2.0);
        if (! (live > -60.0))
            continue;
        const auto lynxDb = clipenv::laneDb (clipenv::valueAt (*e, clipenv::volumeLane, clipenv::clipTick (*audio, beats * ppq)));
        worst = std::max (worst, std::abs (lynxDb - live));
        ++compared;
    }
    CHECK (compared > 300);
    CHECK (worst < 0.1);
    CHECK (e->volume.size() > 3 && e->volume.size() < 200); // (cut into pieces, not one per tick)
}

TEST (livefacts190gx_als_export_keeps_lynxs_gain_curve)
{
    // LYNX ramps (straight in the lane's value) → Live events: Live's straight lines in its own value on Live's law stay
    // within 0.1 dB of LYNX's gain wherever it is above −60 dB; then back again
    using namespace lynx;
    for (const auto end : { 0.4f, 0.0f })
    {
        auto song = clipenv190gi::rampSong();
        song.clipEnvelopes[0].volume.back().value = end;
        const auto out = exportAls (song);
        const auto ev = livefacts190gx::volumeEvents (out.xml);
        CHECK (ev.size() >= 3);
        double worst = 0.0;
        for (int k = 0; k <= 400; ++k)
        {
            const auto beats = k / 100.0;
            const auto lynxDb = clipenv::laneDb (clipenv::valueAt (song.clipEnvelopes[0], clipenv::volumeLane, beats * ppq));
            if (lynxDb < -60.0)
                continue;
            worst = std::max (worst, std::abs (clipenv::liveDb (livefacts190gx::liveAt (ev, beats)) - lynxDb));
        }
        if (! (worst < 0.1))
            std::printf ("  export to %.1f: worst %.3f dB\n", (double) end, worst);
        CHECK (worst < 0.1);
        const auto back = livefacts190gx::reimport (out.xml);
        const auto* audio = back ? livefacts190gx::firstAudio (*back) : nullptr;
        const auto* e = audio != nullptr ? clipenv::of (*back, *audio) : nullptr;
        CHECK (e != nullptr);
        if (e == nullptr)
            continue;
        double trip = 0.0;
        for (int k = 0; k <= 400; ++k)
        {
            const auto beats = k / 100.0;
            const auto a = clipenv::laneDb (clipenv::valueAt (song.clipEnvelopes[0], clipenv::volumeLane, beats * ppq));
            if (a < -60.0)
                continue;
            trip = std::max (trip, std::abs (clipenv::laneDb (clipenv::valueAt (*e, clipenv::volumeLane, clipenv::clipTick (*audio, beats * ppq))) - a));
        }
        if (! (trip < 0.2))
            std::printf ("  round trip to %.1f: worst %.3f dB\n", (double) end, trip);
        CHECK (trip < 0.2);
    }
}

TEST (livefacts190gx_render_of_a_live_clip_gain_step)
{
    // Live's 0.5 renders at −10.0 dB (190gu e1): the imported value renders there in LYNX too
    using namespace lynx;
    const auto plain = clipenv190gi::render (clipenv190gi::dcSong(), 24000);
    auto song = clipenv190gi::dcSong();
    ClipEnvelope e;
    const auto v = clipenv::fromLive (clipenv::volumeLane, 0.5);
    e.volume = { clipenv190gi::point (0, v), clipenv190gi::point (ppq * 4, v) };
    song.clipEnvelopes.push_back (e);
    song.playlist[0].envelope = 0;
    const auto shaped = clipenv190gi::render (song, 24000);
    CHECK (std::abs (plain[12000]) > 0.01f);
    CHECK (std::abs (20.0 * std::log10 ((double) shaped[12000] / (double) plain[12000]) + 10.0) < 0.01);
}
