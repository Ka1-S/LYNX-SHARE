// Round 190mc: the Piano roll's Chord helper (GitHub discussion #105) — the key a chord implies with its context, the
// related keys for modal interchange, the borrowed chords with their movement, mood and voicing (core/ModalHelper.h),
// and Transistor Bass's program row (FL's PROGRAM / SEQUENCE / PATCH selectors, GitHub discussion #202).
#include "core/ModalHelper.h"

namespace modal190mc
{
using namespace lynx::modalhelp;
inline std::string name (const Borrowed& b) { return spelledName (b.root, lynx::notenames::Style::english) + chordSuffix (b.chord.intervals); }
inline const RelatedKey* find (const std::vector<RelatedKey>& keys, int tonic, Mode mode)
{
    for (const auto& k : keys)
        if (k.scale.tonic == tonic && k.scale.mode == mode)
            return &k;
    return nullptr;
}
} // namespace modal190mc

TEST (modal190mc_scales_spelling_signatures)
{
    using namespace modal190mc;
    using lynx::notenames::Style;
    // C Aeolian is spelled with flats, letters in order
    const Scale cMinor { 0, Mode::aeolian };
    std::string spelled;
    for (int d = 0; d < 7; ++d)
        spelled += spelledName (spellDegree (cMinor, d), Style::english) + " ";
    CHECK (spelled == "C D E\xe2\x99\xad F G A\xe2\x99\xad B\xe2\x99\xad ");
    // key signatures: in signature order, the harmonic minor's raised seventh apart
    auto sig = signatureOf (cMinor);
    CHECK (sig.flats == 3 && sig.sharps == 0 && ! sig.raised);
    CHECK (spelledName (sig.notes[0], Style::english) == "B\xe2\x99\xad" && spelledName (sig.notes[2], Style::english) == "A\xe2\x99\xad");
    CHECK (signatureOf ({ 0, Mode::ionian }).notes.empty());
    CHECK (signatureOf ({ 0, Mode::mixolydian }).flats == 1);  // (as F major)
    CHECK (signatureOf ({ 0, Mode::dorian }).flats == 2);      // (as B♭ major)
    CHECK (signatureOf ({ 0, Mode::phrygian }).flats == 4);    // (as A♭ major)
    CHECK (signatureOf ({ 0, Mode::lydian }).sharps == 1);     // (as G major)
    CHECK (signatureOf ({ 0, Mode::locrian }).flats == 5);     // (as D♭ major)
    sig = signatureOf ({ 9, Mode::harmonicMinor });          // A harmonic minor: no signature, G♯
    CHECK (sig.notes.empty() && sig.raised && spelledName (*sig.raised, Style::english) == "G\xe2\x99\xaf");
    CHECK (signatureOf ({ 6, Mode::ionian }).sharps == 6);     // F♯ major (not G♭)
    CHECK (signatureOf ({ 3, Mode::aeolian }).flats == 6);     // E♭ minor (not D♯)
    CHECK (spelledName (spellDegree ({ 10, Mode::ionian }, 0), Style::english) == "B\xe2\x99\xad");
    // the user's note names: solfège and German B / H
    CHECK (spelledName (spellDegree ({ 10, Mode::ionian }, 0), Style::solfege) == "Si\xe2\x99\xad");
    CHECK (spelledName (spellDegree ({ 10, Mode::ionian }, 0), Style::germanic) == "B");
    CHECK (spelledName (spellDegree ({ 0, Mode::ionian }, 6), Style::germanic) == "H");
    // chord symbols and numerals
    CHECK (chordSuffix ({ 0, 3, 7 }) == "m" && chordSuffix ({ 0, 4, 7, 10 }) == "7" && chordSuffix ({ 0, 4, 7, 11 }) == "maj7");
    CHECK (chordSuffix ({ 0, 3, 6, 10 }) == "m7\xe2\x99\xad" "5" && chordSuffix ({ 0, 3, 6 }) == "\xc2\xb0" && chordSuffix ({ 0, 4, 8 }) == "+");
    CHECK (chordSuffix ({ 0, 3, 7, 10, 14 }) == "m9" && chordSuffix ({ 0, 4, 7, 14 }) == "add9" && chordSuffix ({ 0, 5, 7 }) == "sus4");
    CHECK (numeral ({ 0, Mode::ionian }, 5, Quality::minor) == "iv");
    CHECK (numeral ({ 0, Mode::ionian }, 10, Quality::major) == "\xe2\x99\xad" "VII");
    CHECK (numeral ({ 0, Mode::ionian }, 6, Quality::diminished) == "\xe2\x99\xaf" "iv\xc2\xb0");
    CHECK (numeral ({ 0, Mode::ionian }, 6, Quality::major) == "\xe2\x99\xad" "V");
    // intervals: the shorter way first
    CHECK (movement (0, 5).up == 5 && movement (0, 5).down == 7 && movement (0, 5).upFirst);
    CHECK (movement (0, 10).up == 10 && movement (0, 10).down == 2 && ! movement (0, 10).upFirst);
    CHECK (std::string (intervalName (5)) == "Perfect 4th" && std::string (intervalName (7)) == "Perfect 5th" && std::string (intervalName (6)) == "Tritone");
}

TEST (modal190mc_key_from_chord_and_context)
{
    using namespace modal190mc;
    // the discussion's mockup: Am F G before C → C Ionian, the chord is I
    auto a = analyse ({ 60, 64, 67 }, { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } });
    CHECK (a.valid && a.key.tonic == 0 && a.key.mode == Mode::ionian && a.quality == Quality::major && a.contextChords == 3);
    CHECK (numeral (a.key, a.chord.root, a.quality) == "I");
    // the same C after B♭ and F: the ♭VII says Mixolydian
    a = analyse ({ 60, 64, 67 }, { { 58, 62, 65 }, { 53, 57, 60 } });
    CHECK (a.key.mode == Mode::mixolydian);
    // a minor chord alone is Aeolian; with C, F and G before it D minor is Dorian (B natural), with B♭ before it Aeolian
    CHECK (analyse ({ 57, 60, 64 }).key.mode == Mode::aeolian);
    CHECK (analyse ({ 62, 65, 69 }, { { 60, 64, 67 }, { 53, 57, 60 }, { 55, 59, 62 } }).key.mode == Mode::dorian);
    CHECK (analyse ({ 62, 65, 69 }, { { 58, 62, 65 }, { 57, 61, 64 } }).key.mode == Mode::aeolian);
    // E major after A minor and D minor: Phrygian is out (G♯), Ionian / Mixolydian / Lydian decide on the context
    a = analyse ({ 64, 68, 71 }, { { 57, 60, 64 }, { 62, 65, 69 } });
    CHECK (a.key.tonic == 4 && majorType (a.key.mode));
    // a diminished chord: Locrian; an inversion keeps the chord's root (C/E is still C)
    CHECK (analyse ({ 59, 62, 65 }).key.mode == Mode::locrian);
    a = analyse ({ 52, 55, 60 });
    CHECK (a.chord.root == 0 && a.chord.bass == 4 && a.key.tonic == 0);
    // the user's choice wins (the header's Key and Mode menus)
    a = analyse ({ 60, 64, 67 }, {}, std::nullopt, 7, Mode::mixolydian);
    CHECK (a.key.tonic == 7 && a.key.mode == Mode::mixolydian && a.forcedTonic && a.forcedMode);
    // the project's key breaks a tie: a lone C major in a project in F major reads as C Mixolydian
    CHECK (analyse ({ 60, 64, 67 }, {}, Scale { 5, Mode::ionian }).key.mode == Mode::mixolydian);
    CHECK (! analyse ({}).valid);
}

TEST (modal190mc_context_chords_from_notes)
{
    using namespace modal190mc;
    std::vector<lynx::Note> notes;
    const auto add = [&notes] (int key, std::int64_t start, std::int64_t length, int channel = 0) {
        lynx::Note n;
        n.key = key, n.start = start, n.length = length, n.channel = channel;
        notes.push_back (n);
    };
    const auto bar = 4 * (std::int64_t) lynx::ppq;
    for (const auto k : { 57, 60, 64 }) add (k, 0, bar);        // Am
    for (const auto k : { 53, 57, 60 }) add (k, bar, bar);      // F
    add (79, bar + bar / 2, bar / 4);                           // a single melody note: not a chord
    for (const auto k : { 55, 59, 62 }) add (k, 2 * bar, bar);  // G
    for (const auto k : { 40, 47 }) add (k, 2 * bar, bar, 1);   // another channel: not read
    for (const auto k : { 60, 64, 67 }) add (k, 3 * bar, bar);  // C (the selection)
    const auto ctx = contextChords (notes, 0, 3 * bar);
    CHECK (ctx.size() == 3);
    CHECK ((ctx[0] == std::vector<int> { 57, 60, 64 }) && (ctx[2] == std::vector<int> { 55, 59, 62 }));
    CHECK (contextChords (notes, 0, 3 * bar, 2).size() == 2); // (the two latest)
    CHECK (contextChords (notes, 0, 0).empty());
}

TEST (modal190mc_related_keys_and_borrowed_chords)
{
    using namespace modal190mc;
    // C Ionian with its context: the parallel C minor ranks first
    const auto a = analyse ({ 60, 64, 67 }, { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } });
    const auto keys = relatedKeys (a);
    CHECK (! keys.empty() && keys[0].scale.tonic == 0 && keys[0].scale.mode == Mode::aeolian && keys[0].rank == 1);
    CHECK (std::string (relationName (keys[0].relation, a.key.mode, keys[0].scale.mode)) == "Parallel minor");
    CHECK (keys[1].scale.mode == Mode::mixolydian && keys[2].scale.mode == Mode::dorian);
    // the closely related keys follow (relative minor as a harmonic minor, dominant, subdominant), the rare modes last
    const auto* g = find (keys, 7, Mode::ionian);
    const auto* f = find (keys, 5, Mode::ionian);
    const auto* am = find (keys, 9, Mode::harmonicMinor);
    CHECK (g != nullptr && g->relation == Relation::dominant && f != nullptr && f->relation == Relation::subdominant);
    CHECK (am != nullptr && am->relation == Relation::relative && am->rank < g->rank);
    CHECK (keys.back().scale.mode == Mode::locrian);
    CHECK (find (keys, 0, Mode::ionian) == nullptr); // (the home itself is not listed)
    // panel 2 from C minor: the discussion's mockup order — Fm, B♭, A♭, E♭, Cm, Gm — then D°
    const auto chords = borrowedChords (a, keys[0]);
    CHECK (chords.size() == 7);
    if (chords.size() == 7)
    {
        CHECK (name (chords[0]) == "Fm" && name (chords[1]) == "B\xe2\x99\xad" && name (chords[2]) == "A\xe2\x99\xad");
        CHECK (name (chords[3]) == "E\xe2\x99\xad" && name (chords[4]) == "Cm" && name (chords[5]) == "Gm" && name (chords[6]) == "D\xc2\xb0");
        // F minor: up a Perfect 4th (or down a Perfect 5th), one common tone (C), "bittersweet"
        CHECK (chords[0].move.up == 5 && chords[0].move.down == 7 && chords[0].move.upFirst);
        CHECK (std::string (intervalName (chords[0].move.up)) == "Perfect 4th" && std::string (intervalName (chords[0].move.down)) == "Perfect 5th");
        CHECK (chords[0].numeral == "iv" && chords[0].commonTones == 1 && std::string (chords[0].mood.front()) == "bittersweet");
        // B♭: down a Major 2nd (or up a Minor 7th)
        CHECK (! chords[1].move.upFirst && std::string (intervalName (chords[1].move.down)) == "Major 2nd");
        CHECK (chords[4].move.up == 0 && chords[4].commonTones == 2); // C minor: the same root
        for (const auto& c : chords)
            CHECK (c.rank >= 1 && ! c.mood.empty());
    }
    // a related key lends its secondary dominant first: G major → D (V/V), then F♯° (vii°/V)
    if (g != nullptr)
    {
        const auto fromG = borrowedChords (a, *g);
        CHECK (! fromG.empty() && name (fromG[0]) == "D" && fromG[0].secondaryDominant && fromG[0].numeral == "V/V");
        CHECK (fromG.size() > 1 && name (fromG[1]) == "F\xe2\x99\xaf\xc2\xb0");
    }
    if (am != nullptr)
    {
        const auto fromAm = borrowedChords (a, *am);
        CHECK (! fromAm.empty() && name (fromAm[0]) == "E" && fromAm[0].numeral == "V/vi");
    }
    // a minor home: A minor borrows the parallel major's E (V), D (IV), A (I) first
    const auto m = analyse ({ 57, 60, 64 });
    const auto mk = relatedKeys (m);
    CHECK (! mk.empty() && mk[0].scale.tonic == 9 && mk[0].scale.mode == Mode::ionian);
    CHECK (std::string (relationName (mk[0].relation, m.key.mode, mk[0].scale.mode)) == "Parallel major");
    const auto mc = borrowedChords (m, mk[0]);
    CHECK (mc.size() >= 3 && name (mc[0]) == "E" && name (mc[1]) == "D" && name (mc[2]) == "A");
    // extensions: the source scale's own 7th and 9th
    const auto sevenths = borrowedChords (a, keys[0], Extension::seventh);
    CHECK (! sevenths.empty() && name (sevenths[0]) == "Fm7" && name (sevenths[1]) == "B\xe2\x99\xad" "7");
    CHECK (name (borrowedChords (a, keys[0], Extension::ninth)[0]) == "Fm9");
    CHECK (name (borrowedChords (a, keys[0], Extension::sus4)[1]) == "B\xe2\x99\xad" "sus4");
    // the same call twice gives the same list (deterministic)
    CHECK (borrowedChords (a, keys[1]).size() == borrowedChords (a, keys[1]).size());
}

TEST (modal190mc_voicing)
{
    using namespace modal190mc;
    lynx::prtools::Chord fm;
    fm.root = 5, fm.intervals = { 0, 3, 7 };
    // C E G → F minor voice-led: C stays, E → F, G → A♭ (C4 F4 A♭4)
    CHECK ((voice (fm, { 60, 64, 67 }, Inversion::voiceLed) == std::vector<int> { 60, 65, 68 }));
    // fixed inversions start near the previous chord's lowest note
    CHECK ((voice (fm, { 60, 64, 67 }, Inversion::root) == std::vector<int> { 65, 68, 72 }));
    CHECK ((voice (fm, { 60, 64, 67 }, Inversion::first) == std::vector<int> { 56, 60, 65 }));
    CHECK ((voice (fm, { 60, 64, 67 }, Inversion::second) == std::vector<int> { 60, 65, 68 }));
    // no previous chord: root position from C4; a 7th chord's third inversion has the 7th in the bass
    CHECK ((voice (fm, {}, Inversion::voiceLed) == std::vector<int> { 65, 68, 72 }));
    lynx::prtools::Chord g7;
    g7.root = 7, g7.intervals = { 0, 4, 7, 10 };
    const auto third = voice (g7, { 60, 64, 67 }, Inversion::third);
    CHECK (third.size() == 4 && third.front() % 12 == 5);
    // A minor → E major voice-led: G♯3 B3 E4
    lynx::prtools::Chord e;
    e.root = 4, e.intervals = { 0, 4, 7 };
    CHECK ((voice (e, { 57, 60, 64 }, Inversion::voiceLed) == std::vector<int> { 56, 59, 64 }));
    // never outside the MIDI range
    for (const auto k : voice (e, { 124, 126, 127 }, Inversion::voiceLed))
        CHECK (k >= 0 && k <= 127);
}

// --- Transistor Bass's program row (discussion #202) ------------------------------------------------------------------
#include "core/ModelParams.h"
#include "core/NativeEngines.h"
#include "core/TransistorBass.h"

namespace modal190mc
{
inline std::optional<lynx::ChannelDef> tbChannel (const char* preset)
{
    if (realFlpDir.empty())
        return std::nullopt;
    const auto path = (std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "Transistor Bass" / preset).string();
    return lynx::natives::channelFromPresetFile (lynx::InstrumentKind::transistorbass, path, natives142::flAccess());
}
} // namespace modal190mc

// FL's program row: PROGRAM (the shown and played program), AF, SEQUENCE / TRANSP. / PATCH of that program, the names
// and "shared" indicators, READ / WRITE, renaming
TEST (modal190mc_tb_program_row)
{
    using namespace lynx;
    auto c = modal190mc::tbChannel ("Factory programs.fst");
    if (! c)
        return;
    const auto value = [&c] (int index) { return modelparams::rawValue (*c, index).value_or (-999.0f); };
    // Factory programs: program 65 shown (FL's "F5"), AF on, program k = patch k + sequence k
    CHECK (value (tbprog::program) == 65.0f && value (tbprog::follow) == 1.0f);
    CHECK (value (tbprog::patch) == 65.0f && value (tbprog::sequence) == 65.0f && value (tbprog::transpose) == 0.0f);
    auto bank = transistorBassBank (c->pluginState);
    CHECK (bank && bank->program == 65 && bank->patch == 65 && ! bank->edited);
    if (! bank)
        return;
    int named = 0;
    for (const auto& n : bank->patches)
        named += n.empty() ? 0 : 1;
    CHECK (named >= 90); // (FL's 96 named factory patches)
    CHECK (bank->patchUsers[65] == 1);
    // choose another program: the page's knobs read its patch, its sequence comes with it
    const auto cutoff65 = value (2);
    CHECK (modelparams::setRawValue (*c, tbprog::program, 3.0f));
    CHECK (value (tbprog::program) == 3.0f && value (tbprog::patch) == 3.0f);
    CHECK (transistorBassProgram (c->pluginState)->program == 3);
    const auto cutoff3 = value (2);
    // another patch for program 3: the knobs read patch 10; patch 10 is now shared by programs 3 and 10
    CHECK (modelparams::setRawValue (*c, tbprog::patch, 10.0f));
    bank = transistorBassBank (c->pluginState);
    CHECK (bank && bank->patch == 10 && bank->patchUsers[10] == 2 && bank->patchUsers[3] == 0);
    CHECK (value (tbprog::program) == 3.0f);
    (void) cutoff65, (void) cutoff3;
    // the sequence number and transpose of the shown program
    CHECK (modelparams::setRawValue (*c, tbprog::sequence, 20.0f) && value (tbprog::sequence) == 20.0f);
    CHECK (modelparams::setRawValue (*c, tbprog::transpose, -12.0f) && value (tbprog::transpose) == -12.0f);
    CHECK (modelparams::setRawValue (*c, tbprog::follow, 0.0f) && value (tbprog::follow) == 0.0f);
    // READ / WRITE: a knob edits the working copy (WRITE lights); READ brings the stored values back, WRITE keeps them
    const auto before = value (2);
    CHECK (modelparams::setRawValue (*c, 2, before > 0.5f ? 0.1f : 0.9f));
    CHECK (transistorBassBank (c->pluginState)->edited);
    auto read = c->pluginState;
    CHECK (transistorBassReadPatch (read));
    auto readChannel = *c;
    readChannel.pluginState = read;
    CHECK (modelparams::rawValue (readChannel, 2).value_or (-1.0f) == before && ! transistorBassBank (read)->edited);
    auto written = c->pluginState;
    CHECK (transistorBassWritePatch (written) && ! transistorBassBank (written)->edited);
    // renaming a patch and a sequence: the state stays readable, every value the same
    auto renamed = c->pluginState;
    CHECK (transistorBassRename (renamed, true, 10, "My acid line"));
    CHECK (transistorBassRename (renamed, false, 20, "Seq"));
    CHECK (transistorBassStateUsable (renamed));
    bank = transistorBassBank (renamed);
    CHECK (bank && bank->patches[10] == "My acid line" && bank->sequences[20] == "Seq");
    auto renamedChannel = *c;
    renamedChannel.pluginState = renamed;
    for (const auto& p : transistorBassParams())
        CHECK (modelparams::rawValue (renamedChannel, p.index) == modelparams::rawValue (*c, p.index));
}

// The program chosen on the page is what the instrument plays, and the instrument reports it at once (the page's follow
// timer read the old engine's program until the audio thread took the new one, and wrote it back into the state)
TEST (modal190mc_tb_program_change_reaches_the_engine)
{
    using namespace lynx;
    auto c = modal190mc::tbChannel ("Sequencer off.fst");
    if (! c)
        return;
    std::vector<float> l (512), r (512);
    const auto block = [&l, &r] (Instrument& i) { i.render (l.data(), r.data(), 512, 1.0f, 1.0f); };
    auto inst = makeTransistorBassInstrument();
    inst->prepare (44100.0);
    inst->configure (*c, 140.0);
    block (*inst);
    CHECK (inst->selectedProgram() == 60);
    auto other = *c;
    CHECK (modelparams::setRawValue (other, tbprog::program, 3.0f));
    inst->configure (other, 140.0);
    // until the audio thread takes the new engine the instrument names no program (the page's follow timer waits instead
    // of writing the old engine's 60 into the new state); then it is the new one
    CHECK (inst->selectedProgram() == -1 || inst->selectedProgram() == 3);
    block (*inst);
    CHECK (inst->selectedProgram() == 3);
    // with the sequencer off, programs 60 and 3 play their own patches: the sound differs
    const auto play = [] (const ChannelDef& d) {
        auto i = makeTransistorBassInstrument();
        i->prepare (44100.0);
        i->configure (d, 140.0);
        i->noteOn (48, 0.8f);
        std::vector<float> l (512), r (512), out;
        for (int b = 0; b < 40; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            i->render (l.data(), r.data(), 512, 1.0f, 1.0f);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    auto factory = modal190mc::tbChannel ("Factory programs.fst");
    if (! factory)
        return;
    auto seqOff = *factory;
    CHECK (modelparams::setRawValue (seqOff, tbseq::sequencer, 0.0f));
    auto seqOffOther = seqOff;
    CHECK (modelparams::setRawValue (seqOffOther, tbprog::program, 3.0f));
    CHECK (play (seqOff) != play (seqOffOther));
    // another patch for the playing program: the engine takes it (a new engine: FL resets the voice on a patch change too)
    // and the program stays the one shown
    auto live = makeTransistorBassInstrument();
    live->prepare (44100.0);
    live->configure (seqOff, 140.0);
    block (*live);
    auto patched = seqOff;
    CHECK (modelparams::setRawValue (patched, tbprog::patch, 3.0f));
    live->configure (patched, 140.0);
    block (*live);
    CHECK (live->selectedProgram() == 65);
    CHECK (play (patched) != play (seqOff));
}
