// --- round 190kk: Live 12 devices that were still dropped — Erosion → Corrode, Looper → Edison / muted Utility, MIDI
// Effect Racks written into the notes. Laws measured on Live 12.4.16's renders (/mnt/e/lynx-work/livedev190kk RESULTS.md).

#include <complex>

#include "core/Corrode.h"
#include "core/Platter.h"
#include "core/Soundboard.h"

namespace livedev190kk
{
using als190cf::flag;
using als190cf::on;
using als190cf::par;
using als190cf::v;

inline std::string erosion2 (double amount, double hz, double width, double blend, double stereo)
{
    return "<Erosion2 Id=\"1\">" + on() + par ("Amount", amount) + par ("Frequency", hz) + par ("FilterWidth", width) + par ("NoiseBlend", blend) +
           par ("StereoWidth", stereo) + "</Erosion2>";
}
inline std::string erosionLegacy (int mode, double hz, double amplitude, double q)
{
    return "<Erosion Id=\"1\">" + on() + par ("Mode", mode) + par ("Freq", hz) + par ("Amplitude", amplitude) + par ("BandQ", q) + "</Erosion>";
}
// the carrier of a sine after the effect, relative to the input (lock-in over `seconds`), and the mean |L − R|
inline double carrierAfter (const lynx::EffectSlot& slot, double hz, double seconds = 1.0)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    const auto lat = fx->latencySamples();
    const auto n = (int) (seconds * 48000.0) + lat;
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.25 * std::sin (2.0 * 3.141592653589793 * hz * i / 48000.0));
    for (int s = 0; s < n; s += 256)
        fx->process (l.data() + s, r.data() + s, std::min (256, n - s), slot.params, 120.0);
    std::complex<double> acc;
    const auto skip = lat + 4800;
    for (int i = skip; i < n; ++i)
        acc += (double) l[(std::size_t) i] * std::exp (std::complex<double> (0.0, -2.0 * 3.141592653589793 * hz * (i - lat) / 48000.0));
    return 2.0 * std::abs (acc) / (double) (n - skip) / 0.25;
}
} // namespace livedev190kk

TEST (livedev190kk_erosion_imports_onto_corrode_with_the_measured_depths)
{
    using namespace lynx;
    namespace c = corrode;
    // Live 12's Erosion: one to one
    auto t = als190cf::translate (livedev190kk::erosion2 (0.5, 1000.0, 0.5, 0.0, 1.0));
    CHECK (t && t->slot.kind == EffectKind::corrode && t->to == "Corrode" && t->links.size() == 5);
    if (! t)
        return;
    CHECK (std::abs (c::frequencyHz (t->slot.params[c::frequency]) - 1000.0) < 0.5 && std::abs (c::widthOct (t->slot.params[c::width]) - 0.5) < 1e-4);
    CHECK (t->slot.params[c::noise] == 0.0f && t->slot.params[c::stereo] == 1.0f && t->slot.params[c::amount] == 0.5f);
    // the sine's depth: Live's carrier at 2 kHz falls to J0 (2π · 2000 · 76.5 µs) = −2.4 dB (−2.37 measured), at 500 Hz −0.14 dB
    const auto at2k = 20.0 * std::log10 (livedev190kk::carrierAfter (t->slot, 2000.0));
    CHECK (std::abs (at2k - (-2.37)) < 0.3);
    // the legacy device: Mode 2 is the sine (0.81 µs per unit of Amplitude), Mode 0 / 1 noise (mono / wide)
    t = als190cf::translate (livedev190kk::erosionLegacy (2, 5000.0, 50.0, 0.3));
    CHECK (t && t->slot.params[c::noise] == 0.0f && std::abs (c::sineDepthUs (t->slot.params[c::amount]) - 40.5) < 0.1);
    t = als190cf::translate (livedev190kk::erosionLegacy (1, 5000.0, 150.0, 0.3)); // 0.34 µs per unit of Amplitude
    CHECK (t && t->slot.params[c::noise] == 1.0f && t->slot.params[c::stereo] == 1.0f
           && std::abs (c::noiseDepthUs (t->slot.params[c::amount], 0.3, 5000.0) - 51.0) < 0.5);
    // the band pass's noise is normalised: the closed-form variance matches a long impulse response
    const double b0 = 0.0119, b2 = -0.0119, a1 = -1.9686, a2 = 0.9762;
    double s1 = 0.0, s2 = 0.0, e = 0.0;
    for (int i = 0; i < 200000; ++i)
    {
        const auto x = i == 0 ? 1.0 : 0.0, y = b0 * x + s1;
        s1 = -a1 * y + s2;
        s2 = b2 * x - a2 * y;
        e += y * y;
    }
    CHECK (std::abs (c::biquadNoiseGain (b0, 0.0, b2, a1, a2) / e - 1.0) < 1e-6);
    // Amount 0 is exactly the input, delayed by the reported latency
    auto slot = makeEffectSlot (EffectKind::corrode);
    slot.params[c::amount] = 0.0f;
    CHECK (std::abs (livedev190kk::carrierAfter (slot, 8000.0) - 1.0) < 1e-4);
    CHECK (effectInfo (EffectKind::corrode).params.size() == (std::size_t) c::count);
}

TEST (livedev190kk_looper_follows_its_monitor_setting)
{
    using namespace lynx;
    const auto looper = [] (int monitor) {
        return "<Looper Id=\"1\">" + livedev190kk::on() + livedev190kk::par ("State", 0) + livedev190kk::par ("Feedback", 1.0) +
               livedev190kk::flag ("Reverse", false) + livedev190kk::par ("Monitor", monitor) + livedev190kk::par ("Pitch", 0.0) + "</Looper>";
    };
    for (int m = 0; m < 4; ++m)
    {
        const auto t = als190cf::translate (looper (m));
        CHECK (t.has_value());
        if (t)
            CHECK (m == 1 || m == 2 ? (t->slot.kind == EffectKind::utility && t->slot.params[0] == 0.0f) : t->slot.kind == EffectKind::edison);
    }
}

TEST (livedev190kk_midi_effect_rack_chains_are_written_into_the_notes)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190cf::range;
    using als190cf::v;
    // a rack of two chains: keys 0…59 up an octave, 60…127 down an octave (Live 12.4's own MIDI Effect Rack shape)
    const auto pitch = [] (int st, const std::string& mapping = {}, double lo = -12.0, double hi = 12.0) {
        return "<MidiPitcher Id=\"0\">" + als190cf::on() + "<Pitch>" + mapping + v ("Manual", std::to_string (st)) + "<MidiControllerRange>" +
               v ("Min", std::to_string (lo)) + v ("Max", std::to_string (hi)) + "</MidiControllerRange></Pitch>" + als190cf::par ("Lowest", 0) +
               als190cf::par ("Range", 127) + "</MidiPitcher>";
    };
    const auto chain = [] (const std::string& devices, int klo, int khi, int vlo = 1, int vhi = 127, bool active = true, int vflo = -1) {
        return "<MidiEffectBranch Id=\"0\"><Name>" + v ("EffectiveName", "c") + "</Name><DeviceChain><MidiToMidiDeviceChain><Devices>" + devices +
               "</Devices></MidiToMidiDeviceChain></DeviceChain>" + range ("BranchSelectorRange", 0, 0, 0, 0) + "<ZoneSettings>" +
               range ("KeyRange", klo, khi, klo, khi) + range ("VelocityRange", vlo, vhi, vflo < 0 ? vlo : vflo, vhi) + "</ZoneSettings><MixerDevice><Speaker>" +
               v ("Manual", active ? "true" : "false") + "</Speaker></MixerDevice></MidiEffectBranch>";
    };
    const auto rack = [] (const std::string& chains, const std::string& macroValues = {}) {
        return "<MidiEffectGroupDevice Id=\"0\">" + als190cf::on() + "<Branches>" + chains + "</Branches>" + macroValues +
               "<ChainSelector><Manual Value=\"0\" /></ChainSelector></MidiEffectGroupDevice>";
    };
    xml::Document doc;
    CHECK (doc.parse (rack (chain (pitch (12), 0, 59) + chain (pitch (-12), 60, 127))));
    Context context;
    Notes notes;
    std::string stopped;
    auto e = midiRackNotes (doc.root(), context, notes, stopped);
    CHECK (e && e->kind == NoteEffect::Kind::rack && e->chains.size() == 2 && ! midiRackIsPlain (doc.root()));
    if (! e)
        return;
    auto out = applyNoteEffect (*e, { 48, 0.5f, 1.0 });
    CHECK (out.size() == 1 && out[0].key == 60);
    out = applyNoteEffect (*e, { 72, 0.5f, 1.0 });
    CHECK (out.size() == 1 && out[0].key == 60 && std::abs (out[0].velocity - 0.5f) < 1e-6f);
    // a velocity zone with a fade-in (64 … 100): below the fade the velocity is scaled (core/Rack.h zoneVelocity), a switched-
    // off chain plays nothing, and two chains with the same result play it once
    CHECK (doc.parse (rack (chain ("", 0, 127) + chain (pitch (12), 0, 127, 64, 127, true, 100) + chain (pitch (5), 0, 127, 1, 127, false))));
    e = midiRackNotes (doc.root(), context, notes, stopped);
    CHECK (e.has_value());
    if (e)
    {
        out = applyNoteEffect (*e, { 60, 82.0f / 127.0f, 1.0 });
        CHECK (out.size() == 2 && out[0].key == 60 && out[1].key == 72);
        if (out.size() == 2)
            CHECK (std::abs (out[1].velocity * 127.0f - 82.0f * 19.0f / 37.0f) < 0.01f); // (82 − 64 + 1) / (100 − 64 + 1)
        CHECK (applyNoteEffect (*e, { 60, 30.0f / 127.0f, 1.0 }).size() == 1);
    }
    // a macro mapped onto Pitch (−12 … +12) at 95.25 → +6 semitones; one plain chain is taken device by device
    std::string macros;
    for (int k = 0; k < 16; ++k)
        macros += "<MacroControls." + std::to_string (k) + "><Manual Value=\"" + (k == 0 ? std::string ("95.25") : std::string ("0")) + "\" /></MacroControls." +
                  std::to_string (k) + ">";
    CHECK (doc.parse (rack (chain (pitch (0, als190cf::keyMidi (0)), 0, 127), macros)));
    CHECK (midiRackIsPlain (doc.root()));
    e = midiRackNotes (doc.root(), context, notes, stopped);
    CHECK (e && applyNoteEffect (*e, { 60, 0.5f, 1.0 }).front().key == 66);
    // a device that cannot be written into notes stops the rack, by name
    CHECK (doc.parse (rack (chain ("<MidiScale Id=\"0\">" + als190cf::on() + als190cf::par ("Base", 0) + "</MidiScale>", 0, 59) + chain ("", 60, 127))));
    CHECK (! midiRackNotes (doc.root(), context, notes, stopped) && stopped == "Scale");

    // through the importer: a split rack in front of a Simpler rewrites the clip's notes
    const auto globals = v ("NumVoices", "5") + als190cf::par ("PortamentoMode", 0) + v ("PlaybackMode", "0");
    const auto simpler = als190bq::simpler (globals, "<VolumeAndPan>" + als190cf::par ("Volume", 0.0) + als190cf::par ("Panorama", 0.0) + "</VolumeAndPan>");
    const auto clip = als190cf::keyTrack (0, 48, als175a::midiNote (0, 0.5, 100)) + als190cf::keyTrack (1, 72, als175a::midiNote (1, 0.5, 100));
    CHECK (doc.parse (als190cf::set (rack (chain (pitch (12), 0, 59) + chain (pitch (-12), 60, 127)) + simpler, {}, clip)));
    FlpImportReport report;
    auto options = als175a::options (&report);
    std::string error;
    const auto song = importAlsXml (doc, options, &error);
    CHECK (song.has_value());
    if (! song)
        return;
    bool found = false;
    for (const auto& p : song->patterns)
        if (p.notes.size() == 2)
            found = p.notes[0].key == 60 && p.notes[1].key == 60;
    CHECK (found && als190cf::reportHas (report.approximated, "MIDI Effect Rack (2 chains)") && report.unsupported.empty() && report.notReproduced.empty());
}

TEST (livedev190kk_vinyl_distortion_imports_onto_platter)
{
    using namespace lynx;
    namespace v = platter;
    const auto vinyl = [] (double drive, bool hard, double gain1) {
        return "<Vinyl Id=\"1\">" + livedev190kk::on() + livedev190kk::flag ("Band1On", true) + livedev190kk::par ("Gain1", gain1) +
               livedev190kk::par ("BandFreq1", 1000.0) + livedev190kk::par ("BandQ1", 3.0) + livedev190kk::flag ("Band2On", false) +
               livedev190kk::par ("Gain2", 0.5) + livedev190kk::par ("BandFreq2", 684.0) + livedev190kk::par ("BandQ2", 0.32) +
               livedev190kk::par ("Drive", drive) + livedev190kk::par ("SoftHard", hard ? 1.0 : 0.0) + livedev190kk::par ("NormalMono", 0.0) +
               livedev190kk::par ("CracleDensity", 10.0) + livedev190kk::par ("CracleVolume", 0.0) + "</Vinyl>";
    };
    auto t = als190cf::translate (vinyl (0.0, false, 0.5));
    CHECK (t && t->slot.kind == EffectKind::platter && t->to == "Platter" && t->links.size() == 13);
    if (! t)
        return;
    // Drive 0: the input passes bit for bit [MEASURED]
    auto run = [] (const EffectSlot& slot, float level, std::vector<float>& l, std::vector<float>& r) {
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        l.assign (48000, 0.0f);
        r.assign (48000, 0.0f);
        for (int i = 0; i < 48000; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = level * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
        for (int s = 0; s < 48000; s += 256)
            fx->process (l.data() + s, r.data() + s, std::min (256, 48000 - s), slot.params, 120.0);
    };
    std::vector<float> l, r;
    run (t->slot, 0.5f, l, r);
    bool exact = true;
    for (int i = 0; i < 48000; ++i)
        exact = exact && l[(std::size_t) i] == 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
    CHECK (exact);
    // soft Tracing at G 1: the left gets + 1.95 |band|, the right − (their sum is the input again)
    t = als190cf::translate (vinyl (1.0, false, 0.5));
    CHECK (t.has_value());
    if (t)
    {
        run (t->slot, 0.25f, l, r);
        double sumErr = 0.0, dcL = 0.0;
        for (int i = 24000; i < 48000; ++i)
        {
            const auto x = 0.25 * std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
            sumErr = std::max (sumErr, std::abs ((l[(std::size_t) i] + r[(std::size_t) i]) * 0.5 - x));
            dcL += l[(std::size_t) i] - x;
        }
        dcL /= 24000.0;
        CHECK (sumErr < 1.0e-5 && std::abs (dcL - 1.95 * 0.25 * 2.0 / 3.141592653589793) < 0.02); // the mean of |sin| is 2 / π
    }
}

TEST (livedev190kk_corpus_imports_onto_soundboard_with_its_modes)
{
    using namespace lynx;
    namespace s = soundboard;
    const auto corpus = [] (int type, double tune) {
        std::string d = "<Corpus Id=\"1\">" + livedev190kk::on();
        const std::pair<const char*, double> pars[] { { "ResonanceType", (double) type }, { "ResonatorQuality", 3 }, { "Frequency", tune },
                                                      { "Decay", 0.5 }, { "FreqDamping", 0 }, { "AmpFreq", 0 }, { "Inharmonics", 0 },
                                                      { "Radius", 0.5 }, { "TubeOpening", 1 }, { "Ratio", 0.5 }, { "ExcitationX", 0.5 },
                                                      { "ListeningXL", 0.3 }, { "ListeningXR", 0.3 }, { "Detune", 0 }, { "Drive", 0 },
                                                      { "Bleed", 0 }, { "DryWet", 1 }, { "StereoWidth", 1 }, { "FilterMidFreq", 1000 },
                                                      { "FilterBandWidth", 4 } };
        for (const auto& [k, v] : pars)
            d += livedev190kk::par (k, v);
        return d + livedev190kk::flag ("FilterOn", false) + livedev190kk::flag ("LfoOnOff", false) + "</Corpus>";
    };
    const auto t = als190cf::translate (corpus (2, 0.5));
    CHECK (t && t->slot.kind == EffectKind::soundboard && t->to == "Soundboard" && s::kindOf (t->slot.params[s::type]) == s::Kind::string);
    if (! t)
        return;
    CHECK (std::abs (s::tuneHz (t->slot.params[s::tune]) - 261.7) < 0.1 && std::abs (s::tuneHz (0.25f) - 66.1) < 0.1);
    // the string rings at its tuning: a long impulse response's strongest low peak is the first mode
    auto fx = makeEffect (t->slot);
    fx->prepare (48000.0);
    std::vector<float> l (1 << 16, 0.0f), r (1 << 16, 0.0f);
    l[0] = r[0] = 1.0f;
    for (int i = 0; i < (1 << 16); i += 256)
        fx->process (l.data() + i, r.data() + i, 256, t->slot.params, 120.0);
    double best = 0.0, bestHz = 0.0;
    for (double hz = 200.0; hz < 320.0; hz += 0.25)
    {
        std::complex<double> acc;
        for (int i = 0; i < (1 << 16); ++i)
            acc += (double) l[(std::size_t) i] * std::exp (std::complex<double> (0.0, -2.0 * 3.141592653589793 * hz * i / 48000.0));
        if (std::abs (acc) > best)
        {
            best = std::abs (acc);
            bestHz = hz;
        }
    }
    CHECK (std::abs (bestHz - 261.7) < 1.0);
    // the membrane's modes: √(m² + (r n)²) with r = 1 / (1 + 7 · Ratio) [MEASURED 1, 1.069, 1.172, 1.305 at Ratio 0.5]
    std::array<double, s::maxModes> ratios {};
    std::array<std::pair<int, int>, s::maxModes> index {};
    s::baseRatios (s::Kind::membrane, 0.5, false, ratios, index);
    CHECK (std::abs (ratios[1] - 1.069) < 0.004 && std::abs (ratios[2] - 1.172) < 0.004 && std::abs (ratios[3] - 1.305) < 0.006);
    s::baseRatios (s::Kind::beam, 0.5, false, ratios, index);
    CHECK (std::abs (ratios[1] - 2.758) < 0.003 && std::abs (ratios[2] - 5.406) < 0.004);
    CHECK (effectInfo (EffectKind::soundboard).params.size() == (std::size_t) s::count && effectInfo (EffectKind::platter).params.size() == (std::size_t) platter::count);
}
