// Round 190fg: follow-ups of 0.1.27 (pocketpy memory stop, VST3 note ids, the song meter's denominator, Pocket knob names)
#include "core/HostNotePitch.h"

// The mixer's knob page draws an effect's control names through the translation table (as the generated editor page
// and the Pocket panels do): every Pocket effect's control name has a translation in every language table, so none of
// them shows in English in another language (site190ex note: ja showed "Stop time" where the site said 停止時間).
TEST (follow190fg_pocket_knob_names_are_translated)
{
    using namespace lynx;
    using namespace i18ntest;
    const EffectKind pocket[] = { EffectKind::pocketDistortion, EffectKind::pocketLimiterClassic, EffectKind::pocketLimiter,
                                  EffectKind::pocketLeveller, EffectKind::pocketGate, EffectKind::pocketTranceDelay,
                                  EffectKind::pocketTapeDelay, EffectKind::pocketMultiFx, EffectKind::pocketAutoduck,
                                  EffectKind::pocketChorus, EffectKind::pocketSpacer, EffectKind::pocketStereoizer,
                                  EffectKind::pocketWowFlutter, EffectKind::pocketPhaser, EffectKind::pocketFlanger,
                                  EffectKind::pocketTremolo, EffectKind::pocketWaveshaper, EffectKind::pocketCompressor,
                                  EffectKind::pocketSpreader, EffectKind::pocketComb, EffectKind::pocketMultiband,
                                  EffectKind::pocketTapeStop, EffectKind::pocketEq, EffectKind::pocketGraphicEq,
                                  EffectKind::pocketFilter, EffectKind::pocketReverbClassic, EffectKind::pocketReverb2,
                                  EffectKind::pocketReverb };
    int tables = 0, missing = 0;
    for (const auto& entry : std::filesystem::directory_iterator ("resources/i18n"))
    {
        if (entry.path().extension() != ".txt")
            continue;
        ++tables;
        std::vector<std::string> duplicates;
        const auto table = parseTable (readFile (entry.path()), duplicates);
        for (const auto kind : pocket)
            for (const auto& param : effectInfo (kind).params)
                if (const auto it = table.find (param.name); it == table.end() || it->second.empty())
                {
                    if (++missing <= 10)
                        std::printf ("  %s: no \"%s\" (%s)\n", entry.path().stem().string().c_str(), param.name, effectInfo (kind).name);
                }
    }
    CHECK (tables == 14);
    CHECK (missing == 0);
    const auto mixer = readFile ("src/ui/MixerComponent.cpp");
    CHECK (mixer.find ("drawFittedText (g, tr (info.params[(std::size_t) p].name)") != std::string::npos);
    CHECK (mixer.find ("drawFittedText (g, juce::String::fromUTF8 (info.params[(std::size_t) p].name)") == std::string::npos);
}

// VST3 note ids for nested same-key notes (fix 26, the 190ej follow-up): the LYNX note of every note on / off sent on
// MIDI channel 1 is queued per key in the order it went out; the host takes them in the same order. A model of the
// patched host's pairing (fix 14 numbering + fix 26 tags) shows the inner note of a LIFO pair ending its own id.
TEST (follow190fg_vst3_note_tags_pair_each_off_with_its_own_on)
{
    using namespace lynx::hostpitch;
    SentKeys k;
    // LIFO: C (note 7) then D (note 9) on key 64; D ends first, then C. A held key's release has no note (0).
    k.tag (true, 64, 7);
    k.tag (true, 64, 9);
    k.tag (false, 64, 9);
    k.tag (false, 64, 7);
    k.tag (false, 60, 0);
    CHECK (k.takeTag (true, 64) == 7 && k.takeTag (true, 64) == 9);
    CHECK (k.takeTag (false, 64) == 9 && k.takeTag (false, 64) == 7);
    CHECK (k.takeTag (false, 60) == 0 && k.takeTag (false, 60) == 0); // (nothing queued: 0, the oldest-note rule)
    CHECK (k.takeTag (true, 61) == 0);
    // a queue nobody reads (no VST3 host took the block) keeps the newest 8
    for (std::uint32_t i = 1; i <= 12; ++i)
        k.tag (true, 70, i);
    CHECK (k.takeTag (true, 70) == 5);
    k.clear();
    CHECK (k.takeTag (true, 70) == 0);

    // the host's side (cmake/LynxPatchJuce.cmake fixes 14 + 26), modelled: ids counted per note on; a tagged off ends
    // the on with its tag, an untagged one the oldest
    struct Key
    {
        std::vector<std::pair<int, std::uint32_t>> sounding; // (VST3 id, LYNX tag), oldest first
    };
    std::map<int, Key> keys;
    int nextId = 0;
    std::vector<int> offIds;
    const auto hostOn = [&] (int key, std::uint32_t tag) { keys[key].sounding.push_back ({ nextId++, tag }); };
    const auto hostOff = [&] (int key, std::uint32_t tag) {
        auto& s = keys[key].sounding;
        std::size_t at = 0;
        for (std::size_t i = 0; tag != 0 && i < s.size(); ++i)
            if (s[i].second == tag) { at = i; break; }
        offIds.push_back (s.empty() ? -1 : s[at].first);
        if (! s.empty())
            s.erase (s.begin() + (std::ptrdiff_t) at);
    };
    SentKeys sent;
    // what PluginInstrument writes for C on, D on, D off, C off (and the order the host converts them)
    sent.tag (true, 64, 7);
    sent.tag (true, 64, 9);
    sent.tag (false, 64, 9);
    sent.tag (false, 64, 7);
    hostOn (64, sent.takeTag (true, 64));  // C: id 0
    hostOn (64, sent.takeTag (true, 64));  // D: id 1
    hostOff (64, sent.takeTag (false, 64)); // D's end
    hostOff (64, sent.takeTag (false, 64)); // C's end
    CHECK ((offIds == std::vector<int> { 1, 0 }));
    // without tags (fix 14 alone) D's end took C's id
    offIds.clear();
    hostOn (64, 0);
    hostOn (64, 0);
    hostOff (64, 0);
    hostOff (64, 0);
    CHECK ((offIds == std::vector<int> { 2, 3 }));
}

// --- the song meter's denominator (round 190fg; 190em follow-up) -------------------------------------------------------
#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/ClipLauncher.h"
#include "core/FlpExport.h"
#include "core/MarkerPlan.h"
#include "core/MetronomeVoice.h"
#include "core/MidiImport.h"
#include "core/PlaylistOps.h"
#include "core/SessionOps.h"

// A bar of n/d lasts 4n/d quarters and its beats are 1/d notes (Live; FL's time signatures: beat ticks = (16 / d)
// steps, bar = n beats, REA_rodada190ax [DECOMP FUN_010628b0 / FUN_01062880]); n/4 is what every project had before.
TEST (follow190fg_meter_bar_and_beat_lengths)
{
    using namespace lynx;
    CHECK (Meter (4, 4).bar() == 4 * ppq && Meter (4, 4).beat() == ppq);
    CHECK (Meter (3, 4).bar() == 3 * ppq);
    CHECK (Meter (6, 8).bar() == 3 * ppq && Meter (6, 8).beat() == ppq / 2);
    CHECK (Meter (7, 8).bar() == 7 * ppq / 2);
    CHECK (Meter (12, 8).bar() == 6 * ppq && Meter (6, 16).bar() == 3 * ppq / 2 && Meter (6, 16).beat() == ticksPerStep);
    CHECK (Meter (2, 2).bar() == 4 * ppq && Meter (2, 2).beat() == 2 * ppq && Meter (1, 1).bar() == 4 * ppq);
    CHECK (meterUnit (8) == 8 && meterUnit (0) == 4 && meterUnit (3) == 4 && meterUnit (7) == 8 && meterUnit (32) == 16 && meterUnit (1) == 1);
    // every n/4 song: the same bar as before (ppq · n), whatever the numerator
    for (int n = 1; n <= 32; ++n)
    {
        Song s;
        s.beatsPerBar = n;
        CHECK (s.beatUnit == 4 && s.barTicks() == (std::int64_t) ppq * n && s.beatTicks() == ppq);
    }
}

// The song's bars, pattern lengths, song length and launch quantization follow the meter: a 6/8 song has 3-quarter
// bars (the scene launch of 190em gave the song a 3/4 bar: the same length, but three beats).
TEST (follow190fg_song_bars_follow_the_meter)
{
    using namespace lynx;
    Song s = Song::makeDefault();
    s.beatsPerBar = 6;
    s.beatUnit = 8;
    const auto bars = s.barsBetween (0, 9 * ppq);
    CHECK (bars.size() == 3);
    if (bars.size() == 3)
        CHECK (bars[0].length == 3 * ppq && bars[1].tick == 3 * ppq && bars[2].tick == 6 * ppq && bars[2].number == 3);
    CHECK (s.barAt (7 * ppq).number == 3 && s.barAt (7 * ppq).tick == 6 * ppq);
    Pattern p;
    p.notes.push_back ({ 0, ppq, 0, 60, 0.8f });
    CHECK (p.effectiveLength (s.meter()) == 3 * ppq);
    p.notes.push_back ({ 4 * ppq, ppq, 0, 60, 0.8f });
    CHECK (p.effectiveLength (s.meter()) == 6 * ppq && p.effectiveLength (Meter (4, 4)) == 8 * ppq);
    CHECK (launcher::quantTicks (LaunchQuant::bar, s.meter()) == 3.0 * ppq && launcher::quantTicks (LaunchQuant::bars2, s.meter()) == 6.0 * ppq);
    CHECK (launcher::quantTicks (LaunchQuant::quarter, s.meter()) == (double) ppq); // (Live's 1/4: a quarter in any meter)
    // a time signature marker after the start keeps its own bars
    Marker m;
    m.tick = 6 * ppq;
    m.numerator = 4;
    m.denominator = 4;
    m.action = Marker::timeSignature;
    s.addMarker (m);
    const auto mixed = s.barsBetween (0, 14 * ppq);
    CHECK (mixed.size() == 4 && mixed[2].tick == 6 * ppq && mixed[2].length == 4 * ppq && mixed[3].tick == 10 * ppq);
    CHECK (markers::signatureAt (s, 0) == std::make_pair (6, 8) && markers::signatureAt (s, 7 * ppq) == std::make_pair (4, 4));
    // the bar : beat : tick fields count the meter's beats (eighths)
    CHECK (clipprops::formatPosition (3 * ppq + ppq / 2 + 5, s.meter()) == "2:2:005");
    CHECK (clipprops::parsePosition ("2:2:005", s.meter()) == 3 * ppq + ppq / 2 + 5 && clipprops::parsePosition ("1:7:000", s.meter()) == -1);
    CHECK (session::lengthCounter (3.0 * ppq + ppq, s.meter()) == "1.2.0");
    CHECK (clipprops::formatPosition (4 * ppq + ppq + 5, Meter (4, 4)) == "2:2:005"); // (4/4 as before)
}

// The metronome clicks the meter's beats: six eighths a 6/8 bar, the first one the downbeat; n/4 as before (a click
// every quarter, the downbeat every n); a signature marker's bars with its own beats; a count-in runs backwards.
TEST (follow190fg_metronome_clicks_the_meters_beats)
{
    using namespace lynx;
    const auto clicks = [] (Meter meter, std::vector<metronome::Span> spans, double from, double to) {
        std::vector<std::pair<std::int64_t, bool>> out;
        bool has = false;
        std::int64_t last = 0;
        for (double t = from; t < to; t += 7.0)
        {
            const auto b = metronome::beatAt (t, spans.data(), (int) spans.size(), { 0, meter.bar(), meter.beat() });
            if (! has || b.start != last)
                out.push_back ({ b.start, b.downbeat });
            has = true;
            last = b.start;
        }
        return out;
    };
    const auto six = clicks (Meter (6, 8), {}, 0.0, 6.0 * ppq);
    CHECK (six.size() == 12);
    for (std::size_t i = 0; i < six.size(); ++i)
        CHECK (six[i].first == (std::int64_t) i * ppq / 2 && six[i].second == (i % 6 == 0));
    const auto four = clicks (Meter (4, 4), {}, 0.0, 8.0 * ppq);
    CHECK (four.size() == 8);
    for (std::size_t i = 0; i < four.size(); ++i)
        CHECK (four[i].first == (std::int64_t) i * ppq && four[i].second == (i % 4 == 0));
    // song mode: 6/8 from 0, 4/4 from beat 6 (spans as the engine's signature table)
    const auto mixed = clicks (Meter (4, 4), { { 0, 3 * ppq, ppq / 2 }, { 6 * ppq, 4 * ppq, ppq } }, 0.0, 10.0 * ppq);
    CHECK (mixed.size() == 16);
    if (mixed.size() == 16)
        CHECK (mixed[12].first == 6 * ppq && mixed[12].second && mixed[13].first == 7 * ppq && ! mixed[13].second && mixed[6].second);
    // a one-bar count-in before 0 in 6/8: six clicks, the first the downbeat
    const auto countIn = clicks (Meter (6, 8), {}, -3.0 * ppq, 0.0);
    CHECK (countIn.size() == 6 && countIn.front().first == -3 * ppq && countIn.front().second && ! countIn[1].second);
}

// .flp, .als and MIDI keep the denominator both ways; an n/4 song writes and reads exactly as before.
TEST (follow190fg_meter_round_trips_through_flp_als_and_midi)
{
    using namespace lynx;
    Song six = Song::makeDefault();
    six.beatsPerBar = 6;
    six.beatUnit = 8;
    flpexport::Options fo;
    flpexport::Report fr;
    const auto flpProject = flp::parseProject (flpexport::exportFlp (six, fo, fr));
    CHECK (flpProject.beatsPerBar == 6 && flpProject.beatUnit == 8);
    const auto fromFlp = importFlp (flpProject, {});
    CHECK (fromFlp.beatsPerBar == 6 && fromFlp.beatUnit == 8 && fromFlp.barTicks() == 3 * ppq);
    Song four = Song::makeDefault();
    const auto flp4 = flp::parseProject (flpexport::exportFlp (four, fo, fr));
    CHECK (flp4.beatsPerBar == 4 && flp4.beatUnit == 4 && importFlp (flp4, {}).beatUnit == 4);

    six.loopStart = 0;
    six.loopEnd = 3 * ppq;
    const auto als = exportAls (six, {});
    CHECK (als.xml.find ("<Manual Value=\"302\"") != std::string::npos); // (Live's code for 6/8)
    const auto fromAls = importAls (als.bytes, AlsImportOptions {});
    CHECK (fromAls.has_value());
    if (fromAls)
        CHECK (fromAls->beatsPerBar == 6 && fromAls->beatUnit == 8 && fromAls->markers().empty());
    const auto als4 = exportAls (four, {});
    CHECK (als4.xml.find ("<Manual Value=\"201\"") != std::string::npos);

    midiimport::ParsedMidi midi;
    midi.numerator = 6;
    midi.denominator = 8;
    midi.events.push_back ({ 0, 0, 0x90, 60, 100 });
    midi.events.push_back ({ 0, ppq, 0x80, 60, 0 });
    midiimport::Options mo;
    mo.newProject = true;
    Song fromMidi;
    std::string error;
    CHECK (midiimport::importInto (fromMidi, midi, mo, "MIDI", error));
    CHECK (fromMidi.beatsPerBar == 6 && fromMidi.beatUnit == 8);
}
