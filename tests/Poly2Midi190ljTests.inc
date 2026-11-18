// --- Convert to MIDI (polyphonic) with options (round 190lj): batched windows, the polyphase resampler, the dialog's
// settings, Basic Pitch's pitch bends, the chosen channel / quantize / bend curves of the new pattern -------------------

#include "core/AudioEdit.h"
#include "core/AudioToMidi.h"
#include "core/FlClipMenu.h"

namespace poly190lj
{
// (rollup 0.1.30 step 8: g++ 13 hit an internal compiler error, gimplify_var_or_parm_decl, on braced a2m::Note lists with
// an empty `bends` inside a function that takes them by reference; the notes are made here instead)
lynx::a2m::Note note (double start, double end, int key, float velocity)
{
    lynx::a2m::Note n;
    n.start = start;
    n.end = end;
    n.key = key;
    n.velocity = velocity;
    return n;
}
// a fake model: note = the window's input at the frame, onset = the bin, contour = the frame's index in its window
bool fake (const float* in, int count, float* note, float* onset, float* contour)
{
    using namespace lynx::a2m;
    for (int w = 0; w < count; ++w)
        for (int f = 0; f < 172; ++f)
        {
            for (int b = 0; b < keys; ++b)
            {
                note[(w * 172 + f) * keys + b] = in[w * windowSamples + std::min (f * hop, windowSamples - 1)];
                onset[(w * 172 + f) * keys + b] = (float) b;
            }
            if (contour != nullptr)
                for (int b = 0; b < contourBins; ++b)
                    contour[(w * 172 + f) * contourBins + b] = (float) (f * 1000 + b);
        }
    return true;
}

lynx::a2m::Posteriorgram flatPg (int frames)
{
    lynx::a2m::Posteriorgram pg;
    pg.frames = frames;
    pg.note.assign ((std::size_t) frames * lynx::a2m::keys, 0.0f);
    pg.onset = pg.note;
    pg.contour.assign ((std::size_t) frames * lynx::a2m::contourBins, 0.01f);
    return pg;
}
} // namespace poly190lj

TEST (poly190lj_batched_windows_give_the_same_posteriorgram)
{
    using namespace lynx::a2m;
    std::vector<float> audio ((std::size_t) windowStride * 7 + 1234);
    for (std::size_t i = 0; i < audio.size(); ++i)
        audio[i] = (float) std::sin ((double) i * 0.001) + (float) i * 1.0e-6f;
    Posteriorgram one, three;
    CHECK (runModel (audio, 172, [] (const float* in, float* note, float* onset) { return poly190lj::fake (in, 1, note, onset, nullptr); }, {}, one));
    int calls = 0;
    double last = 0.0;
    CHECK (runModelBatched (audio, 172, 3, true, [&] (const float* in, int count, float* note, float* onset, float* contour) {
        ++calls;
        CHECK (count >= 1 && count <= 3 && contour != nullptr);
        return poly190lj::fake (in, count, note, onset, contour);
    }, [&] (double p) { last = p; }, three));
    CHECK (calls == (numWindows ((std::int64_t) audio.size()) + 2) / 3);
    CHECK (last == 1.0);
    CHECK (one.frames == three.frames && one.note == three.note && one.onset == three.onset);
    CHECK (one.contour.empty() && three.contour.size() == (std::size_t) three.frames * contourBins);
    // the contour comes from the same window frame as the note values (frame k of the first window = its frame 15 + k)
    CHECK (three.contour[0] == 15000.0f && three.contour[(std::size_t) 10 * contourBins + 5] == 25005.0f);
    // a stop ends it
    Posteriorgram stopped;
    CHECK (! runModelBatched (audio, 172, 4, false, [] (const float*, int, float*, float*, float*) { return false; }, {}, stopped));
}

TEST (poly190lj_polyphase_resampler_matches_the_sinc_resampler)
{
    using namespace lynx;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        // a length that divides (the per-sample resampler then steps the exact ratio too)
        const auto up = rate == 44100.0 ? 1 : rate == 48000.0 ? 147 : 147, down = rate == 44100.0 ? 2 : rate == 48000.0 ? 320 : 640;
        const auto n = (std::size_t) down * (rate == 44100.0 ? 9000 : 60);
        std::vector<float> x (n);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = (float) (0.5 * std::sin ((double) i * 2.0 * 3.141592653589793 * 440.0 / rate) + 0.25 * std::sin ((double) i * 0.37));
        const auto fast = a2m::toModelRate (x, rate);
        const auto slow = audioedit::resampleChannel (x, (std::int64_t) (n * (std::size_t) up / (std::size_t) down));
        CHECK (fast.size() == slow.size());
        auto worst = 0.0;
        for (std::size_t i = 0; i < std::min (fast.size(), slow.size()); ++i)
            worst = std::max (worst, (double) std::abs (fast[i] - slow[i]));
        CHECK (worst < 1.0e-5);
        // a length that doesn't divide: the exact ratio, within a hair of the per-sample one
        std::vector<float> odd (x.begin(), x.end() - 7);
        const auto a = a2m::toModelRate (odd, rate);
        CHECK ((std::int64_t) a.size() == std::llround ((double) odd.size() * a2m::modelRate / rate));
    }
    // the model's own rate passes through; a rate that isn't whole takes the per-sample resampler
    std::vector<float> y (5000, 0.25f);
    CHECK (a2m::toModelRate (y, 22050.0) == y);
    const auto z = a2m::toModelRate (y, 44100.5);
    CHECK (z.size() == (std::size_t) std::llround (5000.0 * 22050.0 / 44100.5) && std::abs (z[1000] - 0.25f) < 1.0e-4f);
    CHECK (a2m::toModelRate ({}, 44100.0).empty());
    bool asked = false;
    CHECK (a2m::toModelRate (std::vector<float> (100000, 0.1f), 44100.0, [&] { asked = true; return true; }).empty() && asked);
}

TEST (poly190lj_dialog_settings)
{
    using namespace lynx::a2m;
    // the defaults are the Harmony item's settings; the sensitivities turn round into Basic Pitch's thresholds
    const ConvertOptions defaults;
    const auto n = noteOptionsOf (defaults);
    const auto h = optionsFor (Mode::harmony);
    CHECK (std::abs (n.frameThreshold - h.frameThreshold) < 1e-6f && std::abs (n.onsetThreshold - h.onsetThreshold) < 1e-6f);
    CHECK (n.melodiaTrick == h.melodiaTrick && std::abs (n.minNoteSeconds - h.minNoteSeconds) < 1e-6);
    ConvertOptions o;
    o.noteSensitivity = 0.7f; // Basic Pitch's defaults
    o.splitSensitivity = 0.5f;
    o.polyphonic = false;
    o.minKey = 90;
    o.maxKey = 40; // (swapped)
    const auto b = noteOptionsOf (o);
    CHECK (std::abs (b.frameThreshold - 0.3f) < 1e-6f && std::abs (b.onsetThreshold - 0.5f) < 1e-6f && b.melodiaTrick);
    CHECK (b.minKey == 40 && b.maxKey == 90);
    o.noteSensitivity = 1.0f;
    o.splitSensitivity = -3.0f;
    o.minKey = 0;
    o.maxKey = 200;
    const auto c = noteOptionsOf (o);
    CHECK (c.frameThreshold == 0.05f && c.onsetThreshold == 0.95f && c.minKey == 21 && c.maxKey == 108);

    // the dialog's default notes = the Harmony item's; one at a time with 50 ms = the Melody item's
    auto pg = poly190lj::flatPg (300);
    auto paint = [&pg] (int key, int from, int to, float level, float onset) {
        for (int t = from; t < to; ++t)
            pg.note[(std::size_t) t * keys + (std::size_t) (key - lowestKey)] = level;
        pg.onset[(std::size_t) from * keys + (std::size_t) (key - lowestKey)] = onset;
    };
    paint (60, 10, 80, 0.8f, 0.9f);
    paint (64, 12, 80, 0.7f, 0.85f);
    paint (67, 100, 200, 0.6f, 0.0f); // no onset: the melodia trick only
    paint (43, 120, 140, 0.9f, 0.95f);
    const auto harmony = transcribe (pg, Mode::harmony), dialog = transcribe (pg, ConvertOptions {});
    CHECK (harmony.size() == dialog.size() && harmony.size() == 3);
    for (std::size_t i = 0; i < std::min (harmony.size(), dialog.size()); ++i)
        CHECK (harmony[i].key == dialog[i].key && harmony[i].start == dialog[i].start && harmony[i].end == dialog[i].end && dialog[i].bends.empty());
    ConvertOptions mono;
    mono.polyphonic = false;
    mono.minNoteMs = 50.0;
    const auto melody = transcribe (pg, Mode::melody), one = transcribe (pg, mono);
    CHECK (melody.size() == one.size() && ! one.empty());
    for (std::size_t i = 0; i < std::min (melody.size(), one.size()); ++i)
        CHECK (melody[i].key == one[i].key && melody[i].start == one[i].start && melody[i].end == one[i].end);
    // the key range leaves the low note out
    ConvertOptions high;
    high.minKey = 48;
    const auto ranged = transcribe (pg, high);
    CHECK (std::none_of (ranged.begin(), ranged.end(), [] (const Note& x) { return x.key == 43; }) && ranged.size() == 2);
}

TEST (poly190lj_pitch_bends_follow_the_contour)
{
    using namespace lynx::a2m;
    auto pg = poly190lj::flatPg (100);
    const auto frame = hop / modelRate;
    // a C5 (60) note over frames 10..50: on its own bin for 20 frames, then 3 bins (a semitone) up (the model's own
    // bin of a key: one above Basic Pitch's 3 (key - 21))
    const auto centre = contourPerKey * (60 - lowestKey) + 1;
    for (int t = 10; t < 30; ++t)
        pg.contour[(std::size_t) t * contourBins + (std::size_t) centre] = 0.9f;
    for (int t = 30; t < 50; ++t)
        pg.contour[(std::size_t) t * contourBins + (std::size_t) (centre + 3)] = 0.9f;
    // a far-off peak (beyond the 25-bin window) doesn't count; a plain note gets no bends
    for (int t = 10; t < 50; ++t)
        pg.contour[(std::size_t) t * contourBins + (std::size_t) (centre + 30)] = 1.0f;
    for (int t = 60; t < 90; ++t)
        pg.contour[(std::size_t) t * contourBins + (std::size_t) (contourPerKey * (72 - lowestKey) + 1)] = 0.9f;
    std::vector<Note> notes { poly190lj::note (10 * frame, 50 * frame, 60, 0.8f), poly190lj::note (60 * frame, 90 * frame, 72, 0.8f),
                              poly190lj::note (0.0, 0.0, 21, 0.5f) };
    addPitchBends (pg, notes);
    const auto& b = notes[0].bends;
    CHECK (b.size() == 3);
    if (b.size() == 3)
    {
        const auto step = (40 * frame) / 39.0; // np.linspace over the note's 40 frames
        CHECK (b[0].seconds == 0.0 && b[0].cents == 0.0f);
        CHECK (std::abs (b[1].seconds - 19 * step) < 1e-9 && b[1].cents == 0.0f);  // held to the frame before the step
        CHECK (std::abs (b[2].seconds - 20 * step) < 1e-9 && std::abs (b[2].cents - 100.0f) < 1e-4f);
    }
    CHECK (notes[1].bends.empty() && notes[2].bends.empty());
    // Basic Pitch's own centre: the same contour reads a third of a semitone sharp
    auto reference = notes;
    addPitchBends (pg, reference, BendCentre::basicPitch);
    CHECK (reference[0].bends.size() == 3 && std::abs (reference[0].bends.front().cents - 33.333f) < 0.01f
           && std::abs (reference[0].bends.back().cents - 133.333f) < 0.01f);
    CHECK (reference[1].bends.size() == 1 && std::abs (reference[1].bends[0].cents - 33.333f) < 0.01f);
    // the lowest key (bin 0): the window starts at the edge; no contour = nothing to do
    auto low = poly190lj::flatPg (40);
    for (int t = 0; t < 40; ++t)
        low.contour[(std::size_t) t * contourBins + 2] = 0.9f;
    std::vector<Note> edge { poly190lj::note (0.0, 20 * frame, 21, 0.5f) };
    addPitchBends (low, edge);
    CHECK (edge[0].bends.size() == 1 && std::abs (edge[0].bends[0].cents - 33.333f) < 0.01f);
    auto edgeReference = edge;
    addPitchBends (low, edgeReference, BendCentre::basicPitch);
    CHECK (edgeReference[0].bends.size() == 1 && std::abs (edgeReference[0].bends[0].cents - 66.666f) < 0.01f);
    low.contour.clear();
    edge[0].bends.clear();
    addPitchBends (low, edge);
    CHECK (edge[0].bends.empty());
    // the dialog asks for them
    ConvertOptions withBends;
    withBends.pitchBends = true;
    for (int t = 10; t < 50; ++t)
        pg.note[(std::size_t) t * keys + (std::size_t) (60 - lowestKey)] = 0.8f;
    pg.onset[(std::size_t) 10 * keys + (std::size_t) (60 - lowestKey)] = 0.9f;
    const auto found = transcribe (pg, withBends);
    CHECK (found.size() == 1 && found[0].bends.size() == 3);
}

TEST (poly190lj_chosen_channel_quantize_and_bend_curves)
{
    using namespace lynx;
    Song song;
    ChannelDef synth;
    synth.name = "Lead";
    synth.instrument = InstrumentKind::synth;
    song.channels.push_back (synth);
    ChannelDef audio;
    audio.instrument = InstrumentKind::audioClip;
    song.channels.push_back (audio);
    CHECK (a2m::canPlayNotes (song.channels[0]) && ! a2m::canPlayNotes (song.channels[1]));
    const auto ticksPerSecond = song.bpm / 60.0 * ppq;
    const auto tickOf = [ticksPerSecond] (double s) { return s * ticksPerSecond; };
    a2m::Names names { "x - Harmony", "x - Harmony", "Kick", "Snare", "Hat" };
    std::vector<a2m::Note> notes { { 0.03, 0.49, 60, 0.7f, {} }, { 0.5, 1.0, 64, 0.7f, { { 0.0, 0.0f }, { 0.25, 0.0f }, { 0.3, 100.0f } } } };
    // on the chosen channel: no channel added
    a2m::Placement onLead;
    onLead.channel = 0;
    const auto p = a2m::addPattern (song, a2m::Mode::harmony, notes, tickOf, 0, names, onLead);
    CHECK (p >= 0 && song.channels.size() == 2);
    const auto& pat = song.patterns[(std::size_t) p];
    CHECK (pat.notes.size() == 2 && pat.notes[0].channel == 0 && pat.notes[1].channel == 0);
    // the bends: the note's pitch curve, ticks from its start
    CHECK (! pat.notes[0].curves.has() && pat.notes[1].curves.has());
    if (pat.notes[1].curves.has())
    {
        const auto& lane = pat.notes[1].curves->lane (NoteCurveLane::pitch);
        CHECK (lane.size() == 3 && lane[2].value == 100.0f && std::abs (lane[2].tick - (float) (0.3 * ticksPerSecond)) < 0.01f);
        CHECK (pat.notes[1].curves->valueAt (NoteCurveLane::pitch, 0.4 * ticksPerSecond, 0.0f) == 100.0f);
    }
    // quantized to 1/16: both ends on the grid, one step at least
    a2m::Placement grid;
    grid.quantizeTicks = ticksPerStep;
    std::vector<a2m::Note> loose { { 0.03, 0.06, 62, 0.7f, {} }, { 0.52, 0.99, 65, 0.7f, {} } };
    const auto q = a2m::addPattern (song, a2m::Mode::harmony, loose, tickOf, 0, names, grid);
    CHECK (q >= 0 && song.channels.size() == 3 && song.channels[2].instrument == InstrumentKind::synth); // (a new channel)
    for (const auto& n : song.patterns[(std::size_t) q].notes)
        CHECK (n.start % ticksPerStep == 0 && n.length % ticksPerStep == 0 && n.length >= ticksPerStep && n.channel == 2);
    // an audio channel or a channel that isn't there: a new Synth channel instead; drums never take the placement
    a2m::Placement bad;
    bad.channel = 1;
    CHECK (a2m::addPattern (song, a2m::Mode::harmony, loose, tickOf, 0, names, bad) >= 0 && song.channels.size() == 4);
    bad.channel = 99;
    CHECK (a2m::addPattern (song, a2m::Mode::harmony, loose, tickOf, 0, names, bad) >= 0 && song.channels.size() == 5);
    std::vector<a2m::Note> hits { { 0.0, 0.06, a2m::kickKey, 1.0f, {} } };
    CHECK (a2m::addPattern (song, a2m::Mode::drums, hits, tickOf, 0, names, onLead) >= 0 && song.channels.size() == 8);
}

TEST (poly190lj_clip_menu_lists_the_polyphonic_item)
{
    using namespace lynx;
    const auto extras = flclipmenu::lynxExtras();
    const auto at = std::find (extras.begin(), extras.end(), std::string ("Convert to MIDI (polyphonic)..."));
    CHECK (at != extras.end() && at != extras.begin() && *(at - 1) == "Convert to MIDI");
    for (const auto& e : flclipmenu::fl())
        CHECK (e.text != "Convert to MIDI (polyphonic)...");
}
