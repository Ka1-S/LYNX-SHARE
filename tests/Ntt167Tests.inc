// Round 167b (REA_rodada167b.md): Transmitter's sends to other mixer tracks, synced time text, envelope view;
// Newtone and Newtime parity

#include "core/Transmitter.h"

namespace ntt167
{
// a Transmitter with both section effects Off and no tilt: transient + sustain = the input delayed by the lookahead
lynx::EffectSlot plainTransmitter()
{
    using namespace lynx::transmitter;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::transmitter);
    slot.params[transientEffect] = paramFromRaw (transientEffect, off);
    slot.params[sustainEffect] = paramFromRaw (sustainEffect, off);
    slot.params[transientTilt] = slot.params[sustainTilt] = 0.5f;
    return slot;
}
} // namespace ntt167

TEST (ntt167_transmitter_synced_text_and_columns)
{
    using namespace lynx::transmitter;
    // FL: 768 positions (16 steps × 48 ticks), shown one tick on
    CHECK (syncedText (10000) == "16:0");
    CHECK (syncedText (0) == "0:1");
    CHECK (syncedText (2500) == "4:1");   // 0.25 · 767 + 0.5 → 192 → 193 = 4 steps 1 tick
    CHECK (syncedText (144) == "0:12");   // FL's Default preset's transient delay
    CHECK (paramText (tDelaySynced, paramFromRaw (tDelaySynced, 10000)) == "16:0");
    CHECK (paramText (sDelaySynced, paramFromRaw (sDelaySynced, 3156)) == "5:5");
    // the envelope view: FL's Default speed 90 → 2.87 ms columns at 44.1 kHz, speed 50 → 12.2 ms, never under 16
    CHECK (columnSamples (44100, 90) == (int) std::lround (44100 * ((std::pow (601.0, 0.1) - 1) * 0.0004125 + 0.0025)));
    CHECK (columnSamples (44100, 90) == 127);
    CHECK (columnSamples (44100, 50) == 539);
    CHECK (columnSamples (100, 100) == 16);
    // FL's state carries the view's speed after the sidechain
    const auto st = parse (fltrans165::flState (fltrans165::defaultRaw(), 0, 0, 0));
    CHECK (st && st->speed == 60);
}

TEST (ntt167_transmitter_parts_are_what_fl_sends)
{
    using namespace lynx::transmitter;
    // FL's engine goldens (REA_rodada167b §1): out = level_T · part_T + level_S · part_S, the parts before the levels and
    // the on switches. LYNX's parts follow the same law.
    Engine e;
    auto raw = fltrans165::defaultRaw();
    raw[transientLevel] = 7000;
    raw[sustainLevel] = 9500;
    raw[transientOn] = 0;
    for (int k = 0; k < count; ++k)
        e.setRaw (k, raw[(std::size_t) k]);
    const int n = 512;
    std::vector<float> l (n), r (n), ol (n), orr (n);
    double worst = 0, energyT = 0, energyS = 0;
    bool envelopeInRange = true;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < n; ++i)
        {
            const int t = b * n + i;
            const float burst = (t % 11025) < 400 ? 0.8f : 0.1f;
            l[(std::size_t) i] = burst * std::sin ((float) t * 0.05f);
            r[(std::size_t) i] = burst * std::sin ((float) t * 0.031f);
        }
        e.process (l.data(), r.data(), ol.data(), orr.data(), n);
        if (b < 100) // (the level gains glide in first)
            continue;
        const float gS = (float) std::pow (10.0, engineValue (sustainLevel, raw[sustainLevel]) / 20.0);
        for (int i = 0; i < n; ++i)
        {
            const double want = gS * e.part (1, 0)[i]; // the transient is switched off on the main output
            worst = std::max (worst, std::abs (want - ol[(std::size_t) i]));
            energyT += e.part (0, 0)[i] * e.part (0, 0)[i];
            energyS += e.part (1, 1)[i] * e.part (1, 1)[i];
        }
        for (int i = 0; i < n; ++i)
            envelopeInRange = envelopeInRange && e.envelope()[i] >= 0.0f && e.envelope()[i] <= 1.0f;
    }
    CHECK (envelopeInRange);
    CHECK (worst < 1e-5);
    CHECK (energyT > 1.0 && energyS > 1.0); // the switched-off transient is still there for the send
}

TEST (ntt167_transmitter_sends_route_to_inserts)
{
    using namespace lynx;
    using namespace lynx::transmitter;
    // insert 1: a Transmitter with both parts switched off on its own output; its transient goes to insert 3 and its
    // sustain to insert 2. The parts add up to the input (delayed by the lookahead), so the Master gets the input back.
    auto both = [] (Song& s) {
        auto t = ntt167::plainTransmitter();
        t.params[transientOn] = t.params[sustainOn] = 0.0f;
        t.sendTracks = { 3, 2 };
        s.mixer[1].effects.push_back (t);
    };
    const auto [l, r] = send105::render (both, 1, 4000);
    CHECK (std::abs (l[3999] - 0.25f) < 1e-4f && std::abs (r[3999] - 0.25f) < 1e-4f);
    // nothing sent: the switched-off parts leave silence
    const auto [l0, r0] = send105::render ([] (Song& s) {
        auto t = ntt167::plainTransmitter();
        t.params[transientOn] = t.params[sustainOn] = 0.0f;
        s.mixer[1].effects.push_back (t);
    }, 1, 4000);
    CHECK (std::abs (l0[3999]) < 1e-6f);
    // only the sustain sent: a steady DC is all sustain once the onset has passed
    const auto [l1, r1] = send105::render ([] (Song& s) {
        auto t = ntt167::plainTransmitter();
        t.params[transientOn] = t.params[sustainOn] = 0.0f;
        t.sendTracks = { 0, 2 };
        s.mixer[1].effects.push_back (t);
    }, 1, 4000);
    CHECK (std::abs (l1[3999] - 0.25f) < 1e-3f);

    // a send is a route: the destination after the source, no route back, renumbered / dropped with the inserts
    Song song;
    song.mixer = Song::defaultMixer (5);
    both (song);
    const auto order = song.mixerOrder();
    CHECK (std::find (order.begin(), order.end(), 1) < std::find (order.begin(), order.end(), 3));
    CHECK (std::find (order.begin(), order.end(), 1) < std::find (order.begin(), order.end(), 2));
    CHECK (! song.canRoute (3, 1) && ! song.canRoute (2, 1));
    CHECK (slotSendTarget (song.mixer[1].effects[0], 0) == 3 && slotSendTarget (song.mixer[1].effects[0], 1) == 2);
    const auto taps = fruitySendsOf (song, 1);
    CHECK (taps.size() == 2 && taps[0].output == 0 && taps[0].target == 3 && taps[1].output == 1 && taps[1].target == 2 && taps[0].afterSlot);

}

TEST (ntt167_transmitter_send_pdc_after_its_latency)
{
    using namespace lynx;
    // insert 1: [latent 100][Transmitter (88, lookahead) → 2]: the send leaves after both (188), unlike a Fruity Send
    Song s;
    s.mixer = Song::defaultMixer (3);
    s.channels.resize (2);
    s.channels[0].mixerTrack = 1;
    s.channels[1].mixerTrack = 2;
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::eq));
    auto t = ntt167::plainTransmitter();
    t.sendTracks = { 2, 0 };
    s.mixer[1].effects.push_back (t);
    const auto plan = computePdc (s, { 0, 0 }, { {}, { 100, 88 } });
    CHECK (plan.inAlign[2] == 188 && plan.channelDelay[1] == 188);
}

TEST (ntt167_transmitter_envelope_view)
{
    using namespace lynx;
    // a running Transmitter keeps FL's envelope columns: a burst raises the transient envelope, the tail the sustain
    auto fx = transmitter::make();
    fx->prepare (44100);
    auto slot = ntt167::plainTransmitter();
    const auto* view = transmitter::envelopeOf (fx.get());
    CHECK (view != nullptr && transmitter::envelopeOf (nullptr) == nullptr);
    if (view == nullptr)
        return;
    CHECK (view->columnLength.load() == 127);
    std::vector<float> l (512), r (512);
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const int t = b * 512 + i;
            l[(std::size_t) i] = r[(std::size_t) i] = (t >= 8820 && t < 8900 ? 0.9f : 0.05f) * std::sin ((float) t * 0.2f);
        }
        fx->process (l.data(), r.data(), 512, slot.params, 120.0);
    }
    const auto written = view->written.load();
    CHECK (written == 40 * 512 / 127);
    float peakT = 0, lastS = 0, peakOut = 0;
    for (std::uint32_t k = 0; k < written; ++k)
    {
        peakT = std::max (peakT, view->columns[k].transient);
        peakOut = std::max (peakOut, view->columns[k].output);
    }
    lastS = view->columns[written - 1].sustain;
    CHECK (peakT > 0.5f && lastS > 0.5f && peakOut > 0.5f);
}

namespace ntt167
{
// FL's Newtime XML (REA_rodada167b §4): three segments of a 3000-sample file, the second marker moved 300 later
std::string newtimeXml (std::int64_t moved)
{
    auto seg = [] (int k, std::int64_t os, std::int64_t oe, std::int64_t cs, std::int64_t ce) {
        return "<Pitch_" + std::to_string (k) + " bIsEnabled=\"1\" fOrigPitch=\"60.0\" iOrigStartSamplePos=\"" + std::to_string (os) + "\" iOrigEndSamplePos=\""
               + std::to_string (oe) + "\" iCurStartSamplePos=\"" + std::to_string (cs) + "\" iCurEndSamplePos=\"" + std::to_string (ce)
               + "\" fCurInvStretchFactor=\"" + std::to_string ((double) (oe - os) / (double) (ce - cs)) + "\"/>";
    };
    return "<NewToneProject NumOfTracks=\"1\"><Tracks><Trk_0 TrackNum=\"0\" Type=\"class CzplJTimelineWarpSource\" FilePath=\"x.wav\">"
           "<AudioObject SelfBPM=\"120.0\" OriginalFileLength=\"3000\" NumOfPitches=\"3\">"
           + seg (0, 0, 1000, 0, 1000) + seg (1, 1000, 2000, 1000, 2000 + moved) + seg (2, 2000, 3000, 2000 + moved, 3000)
           + "</AudioObject></Trk_0></Tracks></NewToneProject>";
}
} // namespace ntt167

TEST (ntt167_newtime_imports_fl_warp_markers)
{
    using namespace lynx;
    using namespace lynx::newtonefx;
    // FL's segments become LYNX's markers: each segment's source start → output start, and the end
    const auto st = newtonetest::state (1, 4, ntt167::newtimeXml (300), "C:\\x.wav");
    const auto map = warpOf (st);
    CHECK (map && map->frames == 3000);
    if (! map)
        return;
    CHECK (map->markers.size() == 3);
    CHECK (map->markers.size() == 3 && map->markers[0] == (WarpMarker { 1000, 1000 }) && map->markers[1] == (WarpMarker { 2000, 2300 })
           && map->markers[2] == (WarpMarker { 3000, 3000 }));
    CHECK (warpedLength (*map, 3000) == 3000);
    CHECK (std::abs (warpToOutput (*map, 3000, 1500.0) - 1650.0) < 1e-9); // segment 1 stretched 1.3×
    CHECK (std::abs (warpToOutput (*map, 3000, 2500.0) - 2650.0) < 1e-9); // segment 2 squeezed to 700
    // a Newtone state is no warp; LYNX's own markers still read
    CHECK (! warpOf (newtonetest::state (0, 4, newtonetest::xmlWithTrack (true), "")));
    CHECK (warpOf (writeWarp (*map)) == map);

    // the render: a click at source 1500 lands at 1650 (FL's piecewise-linear map), the length is kept
    SampleData src;
    src.sampleRate = 44100;
    src.left.assign (3000, 0.0f);
    for (int i = 0; i < 3000; ++i)
        src.left[(std::size_t) i] = 0.05f * std::sin ((float) i * 0.07f);
    for (int i = 0; i < 8; ++i)
        src.left[(std::size_t) (1500 + i)] = 0.9f;
    auto shared = std::make_shared<SampleData> (src);
    const auto out = renderedSource (shared, st);
    CHECK (out != nullptr && out->frames() == 3000);
    if (out != nullptr && out->frames() == 3000)
    {
        int peak = 0;
        for (int i = 1; i < 3000; ++i)
            if (std::abs (out->left[(std::size_t) i]) > std::abs (out->left[(std::size_t) peak]))
                peak = i;
        CHECK (std::abs (peak - 1652) < 40);
    }
    // FL's unmoved analysis (stretch ratios 1.0001 from its rounding) plays the source unchanged
    const auto plain = newtonetest::state (1, 4, ntt167::newtimeXml (0), "C:\\x.wav");
    CHECK (renderedSource (shared, plain) == shared);
}

TEST (ntt167_newtone_snap_to_beat_grid)
{
    using namespace lynx::newtonefx;
    // FL's Snap to grid menu (Beat, 1/2 ... 1/8 beat, Off): at 120 BPM / 44.1 kHz a beat is 22050 samples
    CHECK (snapToBeatGrid (12000, 44100.0, 120.0, 1) == 22050);
    CHECK (snapToBeatGrid (10000, 44100.0, 120.0, 1) == 0);
    CHECK (snapToBeatGrid (6000, 44100.0, 120.0, 4) == 5513);    // 5512.5 rounds up
    CHECK (snapToBeatGrid (2000, 44100.0, 120.0, 8) == 2756);
    CHECK (snapToBeatGrid (7400, 44100.0, 120.0, 3) == 7350);
    CHECK (snapToBeatGrid (7400, 44100.0, 120.0, 0) == 7400);    // Off
    CHECK (snapToBeatGrid (7400, 44100.0, 0.0, 4) == 7400);
}
