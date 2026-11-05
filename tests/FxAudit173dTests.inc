// Round 173d (issue #77): "no built-in effect has a dead control or a broken mode". Every effect kind (tools/FxAudit.h)
// renders a short programme (drums, a vocal-like pad, a full mix) with each control moved — the far end of a knob,
// every option of a choice — and must change the output; a control that needs another setting first (a band's gain
// before its Q, a section switched on, a side-chain input) is tried with the prerequisite the last survey found
// (FxAudit173dKnown.inc, scripts/fxaudit-known.py), then searched. allowed() lists the controls that cannot change a
// rendered insert here (editor-only, momentary, FL's own inert ones, routing, no source loaded, hidden slots,
// material the programme lacks) and the outputs that are the mode's job (a mute, a freeze); knownGaps() lists the
// FL-parity gaps (dead controls) scheduled for 0.1.25, each with a tracker id. No NaN / inf ever. The FL engines that start from FL's presets are skipped
// without FL's Data.
// The full survey (0, ½ and 1 of every knob, longer programme): scripts/fxaudit.sh, REPORT in /mnt/e/lynx-work/audit173d.

#include "../tools/FxAudit.h"

namespace fxaudit173d
{
struct Known
{
    const char* effect;
    const char* control;
    const char* key;
};

const std::vector<Known>& known()
{
    static const std::vector<Known> k {
#include "FxAudit173dKnown.inc"
    };
    return k;
}

struct Allowed
{
    const char* effect;
    const char* control; // "" = any control; '*' = any run of characters
    const char* what;    // a finding ("silent", "clip", "dc", "click"), or "inert" (the control changes nothing here)
    const char* reason;
};

// Legitimately silent in this harness. Each reason names why the control cannot change a rendered insert here:
// editor-only (a link or display setting), momentary (a push button that acts when pressed while playing), fl-inert
// (FL's own control does nothing either, kept for parity, with the REA evidence), routing
// (its effect lands outside the insert's own output), no-source (the plugin plays a loaded sample a new instance lacks),
// hidden-slot (a state slot with no control in the editor), not-in-programme (it acts on material the programme
// lacks), or behaviour (a broken-looking output that is the mode's job: a mute, a freeze, an audition).
const std::vector<Allowed>& allowed()
{
    static const std::vector<Allowed> a {
        { "Echoform", "Filter link", "inert", "editor-only: moving one cut moves the other (core/EchoSuite.cpp linkedEdit); no DSP of its own" },
        { "Ember Saturator", "Classic auto", "inert", "editor-only: with Auto on, editing Classic drive also sets Classic output (linkedEdit); no DSP of its own" },
        { "Multiband Delay", "Target", "inert", "editor-only: which cells (volume / pan) the band display draws" },
        { "Auto-pan", "Manual trigger", "inert", "momentary: each press while playing fires the trigger source once (RhythmPan.cpp); a value set before the render is the saved one, not a press" },
        { "Fruity Delay 3", "Keep pitch", "inert", "not-in-programme: acts only while the delay time is being changed (automation); a fixed time renders the same" },
        { "Fruity Delay 3", "Time smoothing", "inert", "not-in-programme: the glide time of a delay-time change; a fixed time renders the same" },
        { "Vintage Phaser", "Keep pitch", "inert", "not-in-programme: acts only while the delay time is being changed (automation)" },
        { "Transporter", "Send to", "inert", "routing: picks the insert its loops' send goes to; the send buffer is pulled by that insert, not this one" },
        { "LYNX Tune", "Receive", "inert", "routing: takes the scale another LYNX Tune instance sends; one instance alone has nothing to receive" },
        { "Newtone", "Slave to host playback", "inert", "no-source: syncs the playback of the loaded source sample; a new instance has none" },
        { "Newtime", "Slave to host playback", "inert", "no-source: syncs the playback of the loaded source sample; a new instance has none" },
        { "Facet EQ", "Target Level", "inert", "no-source: levels the Listen = Source / Target audition of the EQ match; a new instance has no captured target spectrum" },
        { "Fruity Scratcher", "", "inert", "no-source: a turntable over a loaded sample; a new instance has none and passes the input" },
        { "Hardcore", "Box * knob 5", "inert", "hidden-slot: FL's fifth knob slot of a box; no box type has a fifth knob in the editor" },
        { "Hardcore", "(unused *)", "inert", "hidden-slot: FL's unused state slots; no control in the editor" },
        { "Fruity Delay Bank", "B8 Unused", "inert", "hidden-slot: FL's unused slot (bank 8 has no send to next); no control in the editor" },
        { "Fruity WaveShaper", "Unipolar", "inert", "not-in-programme: mirrors a non-symmetric drawn curve; the default curve is a straight line (identical either way)" },
        { "Fruity WaveShaper", "HQ", "inert", "not-in-programme: oversamples the curve; a straight line makes no harmonics to alias" },
        { "LYNX Tune", "Humanize", "inert", "not-in-programme: slows the retune only on a steady note held 0.3 s that then moves; the pad has constant vibrato" },
        { "LYNX Tune", "C# bypass", "inert", "not-in-programme: leaves notes sung near C# alone; the programme sings none there" },
        { "LYNX Tune", "Key map *", "inert", "not-in-programme: a per-key override (8 keys each, C1…B8); the programme sings only a few keys" },
        { "Rhythmic envelope", "P1 tension", "inert", "fl-inert: a segment takes the curve of its arrival point, and the first point has no segment before it (REA_rodada20 §1.3); FL's knob does nothing either" },
        { "Transmitter", "Stereo link", "inert", "fl-inert: FL's engine stores it and ignores it (REA_rodada165g-fltrans165, param 34)" },
        { "Ridge Strip", "Gate hold", "inert", "not-in-programme: holds the gate over gaps longer than 0.25 s; the programme's drums are 16ths" },
        // behaviour: the output the mode is for
        { "Vocoder", "Hold", "silent", "behaviour: Hold freezes the bands as they are; nothing played yet = silence (as FL)" },
        { "Vocodex", "Freeze", "silent", "behaviour: Freeze holds the first frame; silence (as FL)" },
        { "Maximus", "Master mode", "silent", "behaviour: the Muted option" },
        { "Fruity Mute 2", "Mute", "silent", "behaviour: the mute switch" },
        { "Ember Saturator", "B* state", "silent", "behaviour: a band's mute option" },
        { "Apex Limiter", "Audition", "silent", "behaviour: plays what the limiter removes; nothing at −18 dBFS" },
        { "Fruity Flanger", "Invert wet", "click", "behaviour: flips the wet's polarity at once, as FL's switch does" },
        { "Echoform", "Note *", "click", "behaviour: the tape glide; a jump to an 8-bar time sweeps the read head over 0.12 s (a pitch dive)" },
        { "Lattice Mod", "Delay sync", "click", "behaviour: the taps glide to a new time over Smoothing (100 ms by default); a jump to a 1–2 bar time sweeps the read heads (a pitch sweep, not a step)" },
        { "Lattice Mod", "Sync", "click", "behaviour: the same tap glide (Smoothing) on a jump to a long synced time" },
        { "Vocoder", "Bands", "clip", "behaviour: FL's Fruity Vocoder, 4 wide bands sum hotter (exact engine)" },
        { "Transmitter", "", "clip", "behaviour: FL's Transmitter, Bounce sums 32 taps and the delay its feedback (exact engine)" },
        { "Transmitter", "", "click", "behaviour: FL's Transmitter, a new section effect starts on the running part (exact engine)" },
        { "Transmitter", "", "dc", "behaviour: FL's Transmitter, Bounce's 32 taps of the kicks' tails (exact engine)" },
    };
    return a;
}

// NOT legitimately silent: FL-parity gaps scheduled for the 0.1.25 rounds (REPORT.md, "Known gaps", ids gap173d-1 …
// gap173d-4). Each is a dead control today; listed so build-local stays green until its round lands, and every entry
// here is an open item. Nothing else may be added without a tracker id.
const std::vector<Allowed>& knownGaps()
{
    static const char* const philter = "gap173d-2: a new Love Philter has unit 1 only and no articulators; LYNX's editor can't set unit types / draw articulators";
    static const std::vector<Allowed> a {
        { "EQUO", "Master send *", "inert", "gap173d-1: EQUO's send levels feed a mixer track in FL; LYNX computes the send gains but routes nothing" },
        { "EQUO", "Bank * send *", "inert", "gap173d-1: EQUO's send levels feed a mixer track in FL; LYNX computes the send gains but routes nothing" },
        { "Fruity Love Philter", "Unit * envelope amount", "inert", philter },
        { "Fruity Love Philter", "Unit 2 *", "inert", philter }, { "Fruity Love Philter", "Unit 3 *", "inert", philter },
        { "Fruity Love Philter", "Unit 4 *", "inert", philter }, { "Fruity Love Philter", "Unit 5 *", "inert", philter },
        { "Fruity Love Philter", "Unit 6 *", "inert", philter }, { "Fruity Love Philter", "Unit 7 *", "inert", philter },
        { "Fruity Love Philter", "Unit 8 *", "inert", philter }, { "Fruity Love Philter", "Follower *", "inert", philter },
        { "Fruity Love Philter", "Master LFO", "inert", philter }, { "Fruity Love Philter", "Mod X", "inert", philter },
        { "Fruity Love Philter", "Mod Y", "inert", philter }, { "Fruity Love Philter", "Pattern", "inert", philter },
        { "Fruity Convolver", "EQ", "inert", "gap173d-3: the amount of a drawn EQ curve; FL's default curve is flat at 0 dB (×1, REA_rodada60 §6) and LYNX's editor can't draw one" },
        { "Vocodex", "Subtractive gate", "inert", "gap173d-4: how the gate map applies its threshold (both paths reproduced, REA_rodada35 §4.2); a new instance's gate map is flat at 0 and LYNX's editor can't draw Vocodex maps" },
    };
    return a;
}

// `pattern` with '*' = any run of characters ("Bank * send *")
bool matches (const char* pattern, const char* text)
{
    if (*pattern == 0)
        return *text == 0;
    if (*pattern == '*')
        return matches (pattern + 1, text) || (*text != 0 && matches (pattern, text + 1));
    return *pattern == *text && matches (pattern + 1, text + 1);
}

bool isAllowed (const std::string& effect, const std::string& control, const std::string& what)
{
    for (const auto* list : { &allowed(), &knownGaps() })
        for (const auto& a : *list)
            if (effect == a.effect && (*a.control == 0 || matches (a.control, control.c_str())) && what == a.what)
                return true;
    return false;
}

// the kinds [from, to]: each control moves the output (or is listed), no broken output
void auditRange (int from, int to)
{
    using namespace lynx;
    static const auto fl = natives142::flAccess();
    static const auto prog = fxaudit::programme (0.25, 0.15);
    fxaudit::Options opt;
    opt.quick = true;
    opt.contexts = 4;
    opt.searchBudget = 60;
    opt.inert = [] (const std::string& effect, const std::string& control) { return isAllowed (effect, control, "inert"); };
    opt.known = [] (const std::string& effect, const std::string& control) -> std::string {
        for (const auto& k : known())
            if (effect == k.effect && control == k.control)
                return k.key;
        return {};
    };
    for (int k = from; k <= std::min (to, (int) lastEffectKind); ++k)
    {
        const auto kind = (EffectKind) k;
        if (fxaudit::skipReason (kind) != nullptr)
            continue;
        const auto slot = natives::newEffect (kind, fl);
        if (! slot)
            continue;
        const auto rep = fxaudit::audit (*slot, prog, opt);
        CHECK (! rep.controls.empty() || effectInfo (kind).params.empty());
        // a control silent on the short programme (a slow release, a long pattern) gets the survey's programme
        auto second = opt;
        for (const auto& c : rep.controls)
            if (c.status == fxaudit::Status::dead && ! isAllowed (rep.name, c.control.name, "inert"))
                second.only.push_back (c.control.name);
        if (second.only.empty())
            ; // all moved
        else
        {
            static const auto full = fxaudit::programme (0.6, 0.25);
            second.contexts = 16;
            const auto again = fxaudit::audit (*slot, full, second);
            for (const auto& c : again.controls)
                if (c.status == fxaudit::Status::dead)
                {
                    std::printf ("  %s: \"%s\" changes nothing (best %.0f dB)%s\n", rep.name.c_str(), c.control.name.c_str(), c.best,
                                 c.onlyNewInstance ? ", only in a new instance" : "");
                    CHECK (c.status != fxaudit::Status::dead);
                }
        }
        // a broken output on the short programme is checked again on the survey's (a delay longer than 0.25 s is
        // silent there, a switch lands on an attack); NaN / inf fail at once
        auto third = opt;
        third.only.clear();
        for (const auto& f : rep.broken)
        {
            CHECK (f.what != "NaN");
            if (f.what != "NaN" && ! isAllowed (rep.name, f.control, f.what))
                third.only.push_back (f.control);
        }
        if (! third.only.empty())
        {
            static const auto full = fxaudit::programme (0.6, 0.25);
            const auto again = fxaudit::audit (*slot, full, third);
            for (const auto& f : again.broken)
                if (std::find (third.only.begin(), third.only.end(), f.control) != third.only.end())
                {
                    const bool ok = f.what != "NaN" && isAllowed (rep.name, f.control, f.what);
                    if (! ok)
                        std::printf ("  %s: %s with %s = %s (%.3g)\n", rep.name.c_str(), f.what.c_str(), f.control.c_str(), f.text.c_str(), f.value);
                    CHECK (ok);
                }
        }
    }
}
} // namespace fxaudit173d

TEST (fxaudit173d_effect_controls_a) { fxaudit173d::auditRange (0, 29); }
TEST (fxaudit173d_effect_controls_b) { fxaudit173d::auditRange (30, 59); }
TEST (fxaudit173d_effect_controls_c) { fxaudit173d::auditRange (60, 89); }
TEST (fxaudit173d_effect_controls_d) { fxaudit173d::auditRange (90, 200); }

// a new Fruity Delay Bank / Multiband Delay has no state yet: its bank controls still edit it (they did nothing)
TEST (fxaudit173d_empty_state_controls_edit_the_defaults)
{
    using namespace lynx;
    for (const auto kind : { EffectKind::fruityDelayBank, EffectKind::multibandDelay })
    {
        auto slot = makeEffectSlot (kind);
        CHECK (slot.state.empty());
        const auto* t = stateparams::tableFor (kind);
        CHECK (t != nullptr);
        if (t == nullptr)
            continue;
        CHECK (t->get (slot.state, 0).has_value());
        const auto& p = t->params[0];
        const auto edited = t->with (slot.state, 0, p.defaultValue == p.maximum ? p.minimum : p.maximum);
        CHECK (! edited.empty());
        CHECK (t->get (edited, 0) != t->get (slot.state, 0));
    }
}

// FL's Pitcher (Pitch correction) follows the notes held on the Channel Rack: MIDI mode and Harmonize voices
TEST (fxaudit173d_pitch_correction_hears_the_held_notes)
{
    using namespace lynx;
    const auto prog = fxaudit::programme (0.3, 0.1);
    auto slot = makeEffectSlot (EffectKind::pitchCorrect);
    for (const auto* n : { "Harmonize", "MIDI", "Mix" })
        slot.params[(std::size_t) plugui::paramIndex (EffectKind::pitchCorrect, n)] = 1.0f;
    auto panned = slot;
    panned.params[(std::size_t) plugui::paramIndex (EffectKind::pitchCorrect, "Voice 1 pan")] = 0.0f;
    const auto a = fxaudit::run (slot, slot, prog), b = fxaudit::run (panned, panned, prog);
    CHECK (fxaudit::audioDiffDb (b, a) > -40.0);
}

