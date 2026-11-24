// Round 190qo: GitHub issues #270 (typing keyboard: chord layouts for more scales, the user's own .scr files) and #271
// (FL's SoundFont Player follows a slide note's velocity and pan).
#include "core/KeyMap.h"
#include "core/SoundFont.h"

namespace issues190qo
{
// a layout's notes for one key at octave 4, root C (FL's numbers: C5 = 60)
std::vector<int> notesOf (const std::string& layout, int key)
{
    using namespace lynx::keys;
    const auto l = typingpiano::layoutIndex (layout);
    CHECK (std::string (typingpiano::layoutName (l)) == layout);
    return typingPianoKey (key, typingpiano::defaultOctave, 0, l).notes;
}
} // namespace issues190qo

// #270: LYNX's chord layouts follow the rule of FL's own four chord files (made again from FL's Major and Minor Natural,
// they give those files back key for key), and exist for FL's other seven-note scales
TEST (issues190qo_typing_chord_layouts_follow_fl_rule)
{
    using namespace lynx::keys;
    using issues190qo::notesOf;
    CHECK (typingpiano::chordRuleCheck() == "OK");
    // Minor Melodic + Chords: the number row plays the scale's triads in root position, the letters the scale's notes
    CHECK (notesOf ("Minor Melodic + Chords", '1') == std::vector<int> ({ 60, 63, 67 }));    // i   (C Eb G)
    CHECK (notesOf ("Minor Melodic + Chords", '3') == std::vector<int> ({ 63, 67, 71 }));    // III+ (Eb G B)
    CHECK (notesOf ("Minor Melodic + Chords", '4') == std::vector<int> ({ 65, 69, 72 }));    // IV  (F A C)
    CHECK (notesOf ("Minor Melodic + Chords", '8') == std::vector<int> ({ 72, 75, 79 }));    // i an octave up
    CHECK (notesOf ("Minor Melodic + Chords", 'Q') == notesOf ("Minor Melodic", 'Q'));
    CHECK (notesOf ("Minor Melodic + Chords", 'Z') == std::vector<int> ({ 48 }));
    CHECK (notesOf ("Minor Melodic + Chords", 'B') == std::vector<int> ({ 55 }));
    // Minor Melodic Chordmap: voice-led triads in the 12 semitones from the scale's 7th (B4 = 59)
    CHECK (notesOf ("Minor Melodic Chordmap", 'Q') == std::vector<int> ({ 60, 63, 67 }));
    CHECK (notesOf ("Minor Melodic Chordmap", 'W') == std::vector<int> ({ 62, 65, 69 }));
    CHECK (notesOf ("Minor Melodic Chordmap", 'E') == std::vector<int> ({ 59, 63, 67 })); // (III+: B in the window's bottom)
    CHECK (notesOf ("Minor Melodic Chordmap", '1') == std::vector<int> ({ 63, 67, 72 }));
    CHECK (notesOf ("Minor Melodic Chordmap", 'A') == std::vector<int> ({ 55, 60, 63 }));
    CHECK (notesOf ("Minor Melodic Chordmap", 'Z') == std::vector<int> ({ 36 }));
    // the root note moves them as FL's own layouts
    const auto l = typingpiano::layoutIndex ("Minor Melodic + Chords");
    CHECK (typingPianoKey ('1', 4, 9, l).notes == std::vector<int> ({ 69, 72, 76 }));        // A minor
    // only seven-note scales get them, and FL's own chord layouts are not doubled
    int lynxCount = 0;
    for (int i = 0; i < typingpiano::layoutCount(); ++i)
        if (typingpiano::layoutSource (i) == typingpiano::Source::lynx)
            ++lynxCount;
    CHECK (lynxCount > 10);
    CHECK (std::string (typingpiano::layoutName (typingpiano::layoutIndex ("Major Pentatonic + Chords"))) == "Layout Piano");
    CHECK (std::string (typingpiano::layoutName (typingpiano::layoutIndex ("Layout Piano + Chords"))) == "Layout Piano"); // (not a scale)
    CHECK (lynxCount == 22); // (11 scales: Minor Harmonic, Hungarian, Melodic, Neopolitan; Arabic, Dorian, Locrian, Lydian, Mixolydian, Neopolitan, Phrygian)
    CHECK (typingpiano::layoutSource (typingpiano::layoutIndex ("Major Chordmap")) == typingpiano::Source::fl);
}

// #270: the user's own .scr files (FL: "You can create your own files"), read with FL's rules: one line per key in scan
// code order, notes by name or number, chords with commas, FREE and LOCK, missing lines FREE
TEST (issues190qo_typing_user_scr_files)
{
    using namespace lynx::keys;
    std::array<TypingKey, 47> keys;
    CHECK (! typingpiano::parseScr ("FREE\nFREE\n", keys));
    // line 1 = '1', 2 = '2', 3 = '3'; line 15 = 'Q' (after =, Backspace, Tab)
    std::string text = "C5\r\nD#5,G5,A#5\r\nLOCK\r\n61\r\nbad\r\n";
    for (int i = 5; i < 14; ++i)
        text += "FREE\n";
    text += "db4\n";
    CHECK (typingpiano::parseScr (text, keys));
    const auto at = [&keys] (char c) {
        const std::string order = "1234567890-=QWERTYUIOP[]ASDFGHJKL;'`\\ZXCVBNM,./";
        return keys[(std::size_t) order.find (c)];
    };
    CHECK (at ('1').taken && at ('1').notes == std::vector<int> ({ 60 }));
    CHECK (at ('2').notes == std::vector<int> ({ 63, 67, 70 }));
    CHECK (at ('3').taken && at ('3').notes.empty()); // LOCK
    CHECK (at ('4').notes == std::vector<int> ({ 61 }));
    CHECK (! at ('5').taken);                          // (unreadable: FREE)
    CHECK (at ('Q').notes == std::vector<int> ({ 49 }));
    CHECK (! at ('Z').taken);                          // (missing: FREE)

    const auto before = typingpiano::layoutCount();
    typingpiano::setUserLayouts ({ { "My minor melodic", "", text }, { "Major", "", "C5\n" }, { "Deep", "Packs/A", "C4\n" }, { "Empty", "", "\n" } });
    CHECK (typingpiano::layoutCount() == before + 2); // ("Major" is FL's own; "Empty" has no key)
    const auto mine = typingpiano::layoutIndex ("My minor melodic");
    CHECK (typingpiano::layoutSource (mine) == typingpiano::Source::user);
    CHECK (typingPianoKey ('2', 4, 2, mine).notes == std::vector<int> ({ 65, 69, 72 })); // (root D: up a tone)
    CHECK (typingpiano::layoutFolder (typingpiano::layoutIndex ("Deep")) == "Packs/A");
    typingpiano::setUserLayouts ({});
    CHECK (typingpiano::layoutCount() == before);
    CHECK (typingpiano::layoutIndex ("My minor melodic") == typingpiano::pianoLayout());
}

// #271: FL's SoundFont Player follows a slide note's velocity (FL 2026's renders, REA_rodada190qo: velocity 100 sliding to
// 10 ends at the velocity-10 level, 10 to 100 at the velocity-100 level); a channel of a project saved before (engine
// revision 4) keeps its level, bit for bit
TEST (issues190qo_soundfont_follows_slide_velocity)
{
    using namespace lynx;
    const auto folder = std::filesystem::temp_directory_path() / ("lynx-190qo-sf-" + processTempTag());
    std::filesystem::create_directories (folder);
    const auto path = (folder / "font.sf2").string();
    {
        const auto bytes = sf::makeTestFont ({ { "Tone", 0, 0 } }, 1);
        std::ofstream out (path, std::ios::binary);
        out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    }
    const auto keep = sf::FontLibrary::instance().getNow (path);
    flp::Project project;
    flp::Channel fc;
    fc.index = 0;
    fc.type = flp::ChannelType::instrument;
    fc.name = "SF";
    fc.pluginName = "Fruity soundfont player";
    fc.pluginState = i173a::flSoundfontState6 ({ 1, 0, 0, 0, 0, -1, -1, -1, -1, -1, -128, -1, -1 }, path);
    project.channels = { fc };
    const auto imported = importFlp (project, {}).channels.at (0);
    CHECK (imported.instrument == InstrumentKind::soundfont);
    CHECK (imported.engineRev >= 5);

    const auto render = [] (ChannelDef c, float vel, float slideVel, bool withSlide) {
        Song s;
        s.mixer = Song::defaultMixer (2);
        s.bpm = 120.0;
        s.slideNoteProps = true;
        c.mixerTrack = 1;
        s.channels.push_back (c);
        s.patterns.emplace_back();
        Note held;
        held.key = 60;
        held.length = ppq * 8;
        held.velocity = vel;
        Note slide;
        slide.key = 60;
        slide.start = ppq;
        slide.length = ppq;
        slide.slide = true;
        slide.velocity = slideVel;
        s.patterns[0].notes = { held };
        if (withSlide)
            s.patterns[0].notes.push_back (slide);
        s.patterns[0].lengthTicks = (std::int64_t) (ppq * 16);
        s.mode = PlayMode::pattern;
        auto state = RackState::build (s, nullptr, 48000.0, [] (const std::string&) -> std::shared_ptr<const SampleData> { return {}; });
        RackRenderer r;
        r.prepare (48000.0, 512);
        std::vector<float> out;
        const auto tps = 120.0 / 60.0 * ppq / 48000.0;
        for (int done = 0; done < 48000 * 2; done += 512)
        {
            std::vector<float> l (512), rr (512);
            r.process (state.get(), true, done * tps, 512, l.data(), rr.data(), nullptr);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto rms = [] (const std::vector<float>& x, double from, double to) {
        double sum = 0.0;
        const auto a = (std::size_t) (from * 48000.0), b = std::min (x.size(), (std::size_t) (to * 48000.0));
        for (auto i = a; i < b; ++i)
            sum += (double) x[i] * (double) x[i];
        return b > a ? std::sqrt (sum / (double) (b - a)) : 0.0;
    };
    const auto loud = render (imported, 100.0f / 128.0f, 0.0f, false);
    const auto quiet = render (imported, 10.0f / 128.0f, 0.0f, false);
    const auto down = render (imported, 100.0f / 128.0f, 10.0f / 128.0f, true);
    const auto up = render (imported, 10.0f / 128.0f, 100.0f / 128.0f, true);
    // (the slide: beat 2, one beat long = 0.5 .. 1.0 s at 120 BPM)
    const auto L = rms (loud, 1.2, 1.8), Q = rms (quiet, 1.2, 1.8);
    std::printf ("  soundfont slide velocity: loud %.4f quiet %.4f down %.4f up %.4f\n", L, Q, rms (down, 1.2, 1.8), rms (up, 1.2, 1.8));
    CHECK (L > 0.0 && Q > 0.0 && Q < L * 0.5);
    CHECK (std::abs (rms (down, 0.1, 0.45) / rms (loud, 0.1, 0.45) - 1.0) < 0.01); // (before the slide: the note's own)
    CHECK (std::abs (rms (down, 1.2, 1.8) / Q - 1.0) < 0.03);
    CHECK (std::abs (rms (up, 1.2, 1.8) / L - 1.0) < 0.03);
    CHECK (rms (down, 0.7, 0.8) < rms (loud, 0.7, 0.8) && rms (down, 0.7, 0.8) > rms (quiet, 0.7, 0.8)); // (on the way)
    // engine revision 4 (a project saved before): the slide leaves the level as it was
    auto old = imported;
    old.engineRev = 4;
    CHECK (render (old, 100.0f / 128.0f, 10.0f / 128.0f, true) == render (old, 100.0f / 128.0f, 100.0f / 128.0f, true));
    std::filesystem::remove_all (folder);
}

// #271: the FL generators FL's host moves on a slide note's velocity (FL 2026 /R renders, REA_rodada190qo: DX10, Granulizer,
// BeepMap, Fruit Kick, BooBass, Wave Traveller, SoundFont Player follow; GMS, PoiZone, Sakura, Sawer don't) — LYNX's
// ports through their Final.Vol, from engine revision 5 on
TEST (issues190qo_fl_ports_follow_slide_velocity)
{
    using namespace issues190od;
    using lynx::InstrumentKind;
    for (const auto kind : { InstrumentKind::granulizer }) // (DX10, BeepMap, Fruit Kick, Wave Traveller, BooBass need FL's state or files:
                                                           // checked against FL's renders in REA_rodada190qo)
    {
        const auto loud = render (kind, true, 100.0f / 128.0f, 100.0f / 128.0f, 0.0f, 8, 256, false);
        const auto quiet = render (kind, true, 10.0f / 128.0f, 10.0f / 128.0f, 0.0f, 8, 256, false);
        const auto down = render (kind, true, 100.0f / 128.0f, 10.0f / 128.0f, 0.0f, 8, 256);
        const auto L = rms (loud.l, 70 * tick, 90 * tick), Q = rms (quiet.l, 70 * tick, 90 * tick), D = rms (down.l, 70 * tick, 90 * tick);
        std::printf ("  %s slide velocity: loud %.5f quiet %.5f slid %.5f\n", "granulizer", L, Q, D);
        CHECK (L > 0.0 && Q < L * 0.1);
        CHECK (std::abs (D / Q - 1.0) < 0.05); // (after the slide: the slide note's level, 31 dB down)
        CHECK (std::abs (rms (down.l, 20 * tick, 46 * tick) / rms (loud.l, 20 * tick, 46 * tick) - 1.0) < 0.01); // (before it)
    }
}
