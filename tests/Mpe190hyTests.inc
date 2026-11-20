// --- Round 190hy (issue #108): MPE stage 2 — each note's own pitch bend / timbre / pressure curves (core/NoteCurves.h):
// the model, editing, recording from an MPE controller, playback to the instruments ------------------------------------
#include "core/NoteCurves.h"

namespace mpe190hy
{
inline bool near (float a, float b, float tolerance = 0.01f) { return std::abs (a - b) <= tolerance; }
inline lynx::NoteCurves curves (std::vector<lynx::NoteCurvePoint> pitch, std::vector<lynx::NoteCurvePoint> timbre = {},
                                std::vector<lynx::NoteCurvePoint> pressure = {})
{
    lynx::NoteCurves c;
    c.lane (lynx::NoteCurveLane::pitch) = std::move (pitch);
    c.lane (lynx::NoteCurveLane::timbre) = std::move (timbre);
    c.lane (lynx::NoteCurveLane::pressure) = std::move (pressure);
    return c;
}
} // namespace mpe190hy

TEST (mpe190hy_curves_value_and_handle)
{
    using namespace lynx;
    using mpe190hy::near;
    NoteCurves c = mpe190hy::curves ({ { 0.0f, 0.0f }, { 100.0f, 200.0f }, { 300.0f, -100.0f } });
    const auto pitch = NoteCurveLane::pitch;
    CHECK (c.valueAt (pitch, -5.0, 9.0f) == 0.0f && c.valueAt (pitch, 50.0, 9.0f) == 100.0f && c.valueAt (pitch, 200.0, 9.0f) == 50.0f);
    CHECK (c.valueAt (pitch, 999.0, 9.0f) == -100.0f);                      // (held after the last point)
    CHECK (c.valueAt (NoteCurveLane::timbre, 50.0, 0.25f) == 0.25f);        // (an empty lane: the fallback)
    CHECK (c.moves (pitch) && ! c.moves (NoteCurveLane::pressure));
    // normalise: sorted, one point per tick (the later), values in range, no negative ticks
    NoteCurves messy = mpe190hy::curves ({ { 50.0f, 9999.0f }, { -3.0f, 1.0f }, { 50.0f, 7.0f } }, { { 1.0f, 2.0f } }, { { 0.0f, std::nanf ("") } });
    messy.normalise();
    CHECK (messy.lane (pitch).size() == 2 && messy.lane (pitch)[0].tick == 0.0f && messy.lane (pitch)[1].value == 7.0f);
    CHECK (messy.lane (NoteCurveLane::timbre)[0].value == 1.0f && messy.lane (NoteCurveLane::pressure)[0].value == 0.0f);

    // the handle: none by default, a deep copy, compared by content, empty curves = none
    Note a;
    CHECK (! a.curves.has() && a.curves->empty() && a.curves.get() == nullptr);
    a.curves = c;
    auto b = a;
    CHECK (a == b && b.curves.has() && b.curves.get() != a.curves.get());
    b.curves.edit().lane (pitch)[1].value = 201.0f;
    CHECK (! (a == b) && a.curves->lane (pitch)[1].value == 200.0f);
    b.curves = NoteCurves {};
    CHECK (! b.curves.has() && b.curves.get() == nullptr);
    Note plain;
    CHECK (plain == Note {});
    auto moved = std::move (a);
    CHECK (moved.curves.has() && near (moved.curves->valueAt (pitch, 100.0, 0.0f), 200.0f));
}

TEST (mpe190hy_curves_slice_scale_thin)
{
    using namespace lynx;
    NoteCurvesRef r;
    r = mpe190hy::curves ({ { 0.0f, 0.0f }, { 100.0f, 100.0f }, { 200.0f, 0.0f } }, { { 0.0f, 0.7f } });
    // the second half of a note cut at 50 ticks: starts at the value there, keeps the points after it, ends where it ended
    const auto tail = notecurves::slice (r, 50.0, 200.0);
    const auto& p = tail->lane (NoteCurveLane::pitch);
    CHECK (p.size() == 3 && p[0].tick == 0.0f && p[0].value == 50.0f && p[1].tick == 50.0f && p[1].value == 100.0f && p[2].tick == 150.0f);
    CHECK (tail->lane (NoteCurveLane::timbre).size() == 1 && tail->lane (NoteCurveLane::timbre)[0].value == 0.7f);
    const auto head = notecurves::slice (r, 0.0, 50.0);
    CHECK (head->lane (NoteCurveLane::pitch).back().tick == 50.0f && head->lane (NoteCurveLane::pitch).back().value == 50.0f);
    CHECK (! notecurves::slice (NoteCurvesRef {}, 0.0, 10.0).has());
    auto scaled = r;
    notecurves::scale (scaled, 2.0);
    CHECK (scaled->lane (NoteCurveLane::pitch)[2].tick == 400.0f && r->lane (NoteCurveLane::pitch)[2].tick == 200.0f);
    // thinning a straight ramp keeps its ends; a corner stays
    std::vector<NoteCurvePoint> ramp;
    for (int i = 0; i <= 100; ++i)
        ramp.push_back ({ (float) i, (float) i * 2.0f });
    CHECK (notecurves::thin (ramp, 1.0f).size() == 2);
    for (int i = 101; i <= 200; ++i)
        ramp.push_back ({ (float) i, 200.0f - (float) (i - 100) * 2.0f });
    const auto thinned = notecurves::thin (ramp, 1.0f);
    CHECK (thinned.size() == 3 && thinned[1].tick == 100.0f);
}

TEST (mpe190hy_curve_editing)
{
    using namespace lynx;
    const auto pitch = NoteCurveLane::pitch;
    NoteCurvesRef r;
    // the first point on an empty lane starts it from the note's own value at the note's start
    CHECK (notecurves::addPoint (r, pitch, 240.0, 700.0f, 0.0f) == 1);
    CHECK (r->lane (pitch).size() == 2 && r->lane (pitch)[0].tick == 0.0f && r->lane (pitch)[0].value == 0.0f);
    CHECK (notecurves::addPoint (r, pitch, 120.0, 99999.0f, 0.0f) == 1 && r->lane (pitch)[1].value == notecurves::maxPitchCents);
    // a point moves between its neighbours only
    notecurves::movePoint (r, pitch, 1, 500.0, -300.0f);
    CHECK (r->lane (pitch)[1].tick == 239.0f && r->lane (pitch)[1].value == -300.0f);
    notecurves::removePoint (r, pitch, 1);
    CHECK (r->lane (pitch).size() == 2);
    // drawing replaces what lies under the stroke
    notecurves::drawLine (r, pitch, 300.0, 100.0f, 100.0, 300.0f, 50.0, 0.0f);
    const auto& p = r->lane (pitch);
    CHECK (p.size() == 6 && p[1].tick == 100.0f && p[1].value == 300.0f && p.back().tick == 300.0f && p.back().value == 100.0f);
    notecurves::erase (r, pitch, 90.0, 210.0);
    CHECK (r->lane (pitch).size() == 3);
    notecurves::clearLane (r, pitch);
    CHECK (! r.has());
    // timbre / pressure in 0..1
    notecurves::addPoint (r, NoteCurveLane::pressure, 0.0, 3.0f, 0.0f);
    CHECK (r->lane (NoteCurveLane::pressure).size() == 1 && r->lane (NoteCurveLane::pressure)[0].value == 1.0f);
}

TEST (mpe190hy_split_and_chop_keep_their_part)
{
    using namespace lynx;
    Pattern p;
    Note n;
    n.start = 960;
    n.length = 960;
    n.curves = mpe190hy::curves ({ { 0.0f, 0.0f }, { 960.0f, 1200.0f } });
    p.notes.push_back (n);
    const auto tail = noteedit::split (p, 0, 960 + 480);
    CHECK (tail == 1);
    const auto& head = p.notes[0].curves->lane (NoteCurveLane::pitch);
    const auto& rest = p.notes[1].curves->lane (NoteCurveLane::pitch);
    CHECK (head.size() == 2 && mpe190hy::near (head.back().value, 600.0f) && head.back().tick == 480.0f);
    CHECK (rest.size() == 2 && mpe190hy::near (rest.front().value, 600.0f) && rest.back().tick == 480.0f && rest.back().value == 1200.0f);
}

TEST (mpe190hy_join_keeps_both_notes_curves)
{
    using namespace lynx;
    Pattern p;
    Note a;
    a.start = 0;
    a.length = 480;
    a.curves = mpe190hy::curves ({ { 0.0f, 0.0f }, { 480.0f, 200.0f } });
    Note b = a;
    b.start = 480;
    b.modX = 0.8f;
    b.curves = mpe190hy::curves ({ { 0.0f, -100.0f } });
    p.notes = { a, b };
    CHECK (noteedit::join (p, { 0, 1 }) == 1 && p.notes.size() == 1);
    const auto& n = p.notes[0];
    CHECK (n.length == 960 && n.curves.has());
    // the first note's curve up to the joint, the second's after it; lanes neither note had stay empty (the joined note
    // keeps the first note's values, as a join without curves does)
    CHECK (mpe190hy::near (n.curves->valueAt (NoteCurveLane::pitch, 240.0, 0.0f), 100.0f, 0.5f));
    CHECK (n.curves->valueAt (NoteCurveLane::pitch, 700.0, 0.0f) == -100.0f);
    CHECK (n.curves->lane (NoteCurveLane::timbre).empty());
}

TEST (mpe190hy_scheduler_sends_a_notes_curves)
{
    using namespace lynx;
    Pattern p;
    Note n;
    n.start = 0;
    n.length = 960; // one beat: 22050 samples at 120 BPM, 44.1 kHz
    n.key = 62;
    n.finePitch = 20.0f;
    n.modX = 0.3f;
    n.curves = mpe190hy::curves ({ { 0.0f, 0.0f }, { 960.0f, 1200.0f } }, {}, { { 0.0f, 0.5f } });
    p.notes.push_back (n);
    const auto tps = 960.0 * 2.0 / 44100.0; // ticks per sample at 120 BPM
    EventBuffer out;
    schedulePatternLoop (p, 3840, 0.0, 512, tps, out);
    int ons = 0, pitchProps = 0, others = 0;
    for (const auto& e : out)
    {
        if (e.type == NoteEvent::Type::on)
        {
            ++ons;
            // the note starts at its curves' first values: fine pitch + bend, its own Mod X (no timbre curve), Mod Y from pressure
            CHECK (e.cents == 20.0f && e.modX == 0.3f && e.modY == 0.75f && e.sampleOffset == 0);
        }
        else if (e.type == NoteEvent::Type::voiceProp)
        {
            CHECK (e.expression && e.key == 62 && e.port == -1 && e.sampleOffset > 0 && e.sampleOffset % 64 == 0);
            if (e.prop == 1)
            {
                ++pitchProps;
                // the bend `sampleOffset` samples in: 1200 cents over 22050 samples, plus the fine pitch
                CHECK (mpe190hy::near (e.velocity, 20.0f + 1200.0f * (float) e.sampleOffset / 22050.0f, 0.05f));
            }
            else
                ++others; // (the pressure lane has one point: constant, carried by the "on")
        }
    }
    CHECK (ons == 1 && pitchProps == 7 && others == 0);
    // half-way through, in a later block: the bend is at 600 + 20
    EventBuffer mid;
    schedulePatternLoop (p, 3840, 480.0, 256, tps, mid);
    auto found = false;
    for (const auto& e : mid)
        if (e.type == NoteEvent::Type::voiceProp && e.prop == 1)
        {
            found = true;
            CHECK (mpe190hy::near (e.velocity, 20.0f + 1200.0f * (float) ((480.0 + (double) e.sampleOffset * tps) / 960.0), 0.5f));
        }
    CHECK (found);
    // after the note's end: nothing; and a muted note sends none
    EventBuffer after;
    schedulePatternLoop (p, 3840, 1000.0, 512, tps, after);
    CHECK (after.size() == 0);
    p.notes[0].muted = true;
    EventBuffer muted;
    schedulePatternLoop (p, 3840, 0.0, 512, tps, muted);
    CHECK (muted.size() == 0);
    // the next pass of the loop sends them again
    p.notes[0].muted = false;
    EventBuffer second;
    schedulePatternLoop (p, 3840, 3840.0, 512, tps, second);
    auto props = 0;
    for (const auto& e : second)
        props += e.type == NoteEvent::Type::voiceProp ? 1 : 0;
    CHECK (props == 8); // (on the song's 64-sample grid: 88200 = 1378 × 64 + 8, so offsets 56, 120, … 504)
    // a note without curves: exactly as before (no voice properties)
    p.notes[0].curves.clear();
    EventBuffer plainOut;
    schedulePatternLoop (p, 3840, 0.0, 512, tps, plainOut);
    CHECK (plainOut.size() == 1 && plainOut[0].type == NoteEvent::Type::on && plainOut[0].cents == 20.0f && plainOut[0].modY == 0.5f);
}

TEST (mpe190hy_built_in_sampler_plays_a_notes_pitch_curve)
{
    // a song note whose pitch curve steps up an octave half-way: the Sampler's voice follows it while it sounds
    auto sample = s162::sine (440.0, 4.0);
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    s.bpm = 120.0;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "sine.wav";
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::Note n;
    n.start = 0;
    n.length = 4 * lynx::ppq; // two seconds
    n.key = 60;
    n.curves = mpe190hy::curves ({ { 0.0f, 0.0f }, { 1919.0f, 0.0f }, { 1920.0f, 1200.0f } });
    s.patterns[0].notes.push_back (n);
    s.patterns[0].lengthTicks = 16 * lynx::ppq;
    lynx::RackState::Factories f;
    f.samples = [sample] (const std::string&) { return std::shared_ptr<const lynx::SampleData> (sample); };
    f.stretchedSamples = [sample] (const std::string&, double ratio, lynx::StretchMethod m, double, const lynx::StretchMap&, const lynx::StretchOptions&) {
        return std::shared_ptr<const lynx::SampleData> (lynx::timeStretch (*sample, ratio, m));
    };
    constexpr double rate = 44100.0;
    auto state = lynx::RackState::build (s, nullptr, rate, f);
    lynx::RackRenderer r;
    r.prepare (rate, 441);
    std::vector<float> out, l (441), rr (441);
    const auto tps = lynx::ppq * s.bpm / 60.0 / rate;
    for (int b = 0; b < 200; ++b)
    {
        r.process (state.get(), true, b * 441 * tps, 441, l.data(), rr.data(), nullptr, nullptr);
        out.insert (out.end(), l.begin(), l.end());
    }
    const auto first = mpe190bb::frequency (out, 4410, 39690, rate);
    const auto second = mpe190bb::frequency (out, 48510, 83790, rate);
    CHECK (first > 100.0);
    CHECK (std::abs (second / first - 2.0) < 0.004);
}

TEST (mpe190hy_recorder_takes_an_mpe_notes_curves)
{
    using namespace lynx;
    Song s;
    ChannelDef c;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    s.patterns[0].lengthTicks = 4 * ppq * 4;
    NoteRecorder rec;
    rec.begin (s.patterns[0].effectiveLength (s.meter()));
    // an MPE note: its "on" brings its whole bend (here 300 cents: the fine pitch keeps 120), its timbre and pressure
    RecordedEvent on { 0.0, 0, 60, 0.8f, true, true, 120.0f, 0.5f, 0.5f, -1, 300.0f };
    rec.add (s, on);
    // the bend glides up to 700 cents over one beat, the timbre goes to 0.9, the pressure rises and falls
    for (int i = 1; i <= 96; ++i)
    {
        const auto t = (double) i * 10.0;
        rec.add (s, { t, 0, 60, 0.0f, false, false, 0.0f, 0.5f, 0.5f, (std::int8_t) NoteCurveLane::pitch, 300.0f + 400.0f * (float) i / 96.0f });
        rec.add (s, { t, 0, 60, 0.0f, false, false, 0.0f, 0.5f, 0.5f, (std::int8_t) NoteCurveLane::timbre, i < 48 ? 0.5f : 0.9f });
        rec.add (s, { t, 0, 60, 0.0f, false, false, 0.0f, 0.5f, 0.5f, (std::int8_t) NoteCurveLane::pressure, i < 48 ? (float) i / 48.0f : (float) (96 - i) / 48.0f });
    }
    // another channel's expression (no held note there) is ignored
    rec.add (s, { 500.0, 3, 60, 0.0f, false, false, 0.0f, 0.5f, 0.5f, (std::int8_t) NoteCurveLane::pitch, 4800.0f });
    rec.add (s, { (double) ppq, 0, 60, 0.0f, false, true, 0.0f, 0.5f, 1.0f });
    auto notes = rec.takeFinished();
    CHECK (notes.size() == 1);
    if (notes.size() != 1)
        return;
    const auto& n = notes[0];
    CHECK (n.finePitch == 120.0f && n.modX == 0.5f && n.modY == 1.0f && n.curves.has());
    const auto& p = n.curves->lane (NoteCurveLane::pitch);
    // the pitch lane is the bend over the fine pitch: 180 at the start, 580 at the end; a straight ramp thins to its ends
    CHECK (p.size() == 2 && p.front().tick == 0.0f && p.front().value == 180.0f && mpe190hy::near (p.back().value, 580.0f) && p.back().tick == 960.0f);
    const auto& t = n.curves->lane (NoteCurveLane::timbre);
    CHECK (t.size() >= 3 && t.front().value == 0.5f && t.back().value == 0.9f);
    const auto& z = n.curves->lane (NoteCurveLane::pressure);
    CHECK (z.size() >= 3 && z.size() < 20 && z.front().value == 0.0f && mpe190hy::near (n.curves->valueAt (NoteCurveLane::pressure, 480.0, 0.0f), 1.0f));
    // a note whose expression never moved: no curves (its fine pitch, Mod X and Mod Y say it all), as stage 1
    rec.add (s, { 2000.0, 0, 64, 0.8f, true, true, 50.0f, 0.6f, 0.5f, -1, 50.0f });
    rec.add (s, { 2100.0, 0, 64, 0.0f, false, false, 0.0f, 0.5f, 0.5f, (std::int8_t) NoteCurveLane::pitch, 50.0f });
    rec.add (s, { 2400.0, 0, 64, 0.0f, false, true, 0.0f, 0.5f, 0.5f });
    const auto still = rec.takeFinished();
    CHECK (still.size() == 1 && ! still[0].curves.has() && still[0].finePitch == 50.0f && still[0].modX == 0.6f);
    // a plain (not MPE) note: no curves either
    rec.add (s, { 3000.0, 0, 65, 0.8f, true });
    rec.add (s, { 3100.0, 0, 65, 0.0f, false, false, 0.0f, 0.5f, 0.5f, (std::int8_t) NoteCurveLane::pitch, 700.0f });
    rec.add (s, { 3200.0, 0, 65, 0.0f, false });
    const auto plain = rec.takeFinished();
    CHECK (plain.size() == 1 && ! plain[0].curves.has() && plain[0].finePitch == 0.0f);
}
