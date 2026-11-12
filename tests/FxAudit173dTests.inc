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
        { "Fret Channel", "EQ auto listen", "inert", "editor-only: solos the EQ band whose knob is held (a transient setting the editor makes); the original renders the same with it on (black-box, tp172c8)" },
        { "Kiln Channel", "Link I/O", "inert", "editor-only: an Input change moves Output the other way (kiln::linkedEdit); no DSP of its own" },
        { "Kiln Channel", "Group I/O", "inert", "editor-only: Input / Output edits reach the group's other channels; no DSP of its own" },
        { "Kiln Channel", "Group", "inert", "routing only: which group's Group bypass this channel follows (a lone instance sounds the same in every group, as the original)" },
        { "Ember Saturator", "Classic auto", "inert", "editor-only: with Auto on, editing Classic drive also sets Classic output (linkedEdit); no DSP of its own" },
        { "Ember Saturator", "Classic hot law", "inert", "not-in-programme: it changes styles T and P above Classic drive 5 only, a corner the context search does not reach; tests/Parity190arTests.inc renders both values there" },
        { "Multiband Delay", "Target", "inert", "editor-only: which cells (volume / pan) the band display draws" },
        { "Auto-pan", "Manual trigger", "inert", "momentary: each press while playing fires the trigger source once (RhythmPan.cpp); a value set before the render is the saved one, not a press" },
        { "Fruity Delay 3", "Keep pitch", "inert", "not-in-programme: acts only while the delay time is being changed (automation); a fixed time renders the same" },
        { "Fruity Delay 3", "Time smoothing", "inert", "not-in-programme: the glide time of a delay-time change; a fixed time renders the same" },
        { "Vintage Phaser", "Keep pitch", "inert", "not-in-programme: acts only while the delay time is being changed (automation)" },
        { "Transporter", "Send to", "inert", "routing: picks the insert its loops' send goes to; the send buffer is pulled by that insert, not this one" },
        { "LYNX Tune", "Receive", "inert", "routing: takes the scale another LYNX Tune instance sends; one instance alone has nothing to receive" },
        { "Pitch correction", "MIDI port", "inert", "routing (round 190ee): the MIDI Out port whose notes reach MIDI mode / Harmonize (RackRenderer); the audit hands the notes to the effect itself" },
        { "Pitch correction", "Port routing", "inert", "routing (round 190ee): notes from the port or (older projects) from the Channel Rack; the audit feeds the same chord both ways" },
        { "Newtone", "Slave to host playback", "inert", "no-source: syncs the playback of the loaded source sample; a new instance has none" },
        { "Newtime", "Slave to host playback", "inert", "no-source: syncs the playback of the loaded source sample; a new instance has none" },
        { "Facet EQ", "Target Level", "inert", "no-source: levels the Listen = Source / Target audition of the EQ match; a new instance has no captured target spectrum" },
        { "Fruity Scratcher", "", "inert", "no-source: a turntable over a loaded sample; a new instance has none and passes the input" },
        { "Hardcore", "Box * knob 5", "inert", "hidden-slot: FL's fifth knob slot of a box; no box type has a fifth knob in the editor" },
        { "Hardcore", "(unused *)", "inert", "hidden-slot: FL's unused state slots; no control in the editor" },
        { "Fruity Delay Bank", "B8 Unused", "inert", "hidden-slot: FL's unused slot (bank 8 has no send to next); no control in the editor" },
        { "Fruity WaveShaper", "Unipolar", "inert", "not-in-programme: mirrors a non-symmetric drawn curve; the default curve is a straight line (identical either way)" },
        { "Fruity WaveShaper", "HQ", "inert", "not-in-programme: oversamples the curve; a straight line makes no harmonics to alias" },
        { "Sentry Gate", "Law", "inert", "not-in-programme: the Vocal style's noise reduction acts on hiss-like sound that starts after a pause (60 ms under -60 dBFS); the programme plays without one (tests/Parity190vTests.inc renders it)" },
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
        { "Fruity Love Philter", "Unit 1 on", "silent", "behaviour: FL's Love Philter outputs the sum of its units; its default has unit 1 on only, so unit 1 off is silence (exact engine)" },
        // round 190g: FL Studio Mobile's own behaviours, kept by the Pocket engines (REA specs of round 189h)
        { "Pocket Compressor", "RMS", "inert", "fl-inert: FLM's setter always clears the RMS flag (its RMS mode is unreachable), so the control does nothing there either" },
        { "Pocket Compressor", "Auto gain", "clip", "behaviour: with Auto gain off FLM's Level runs 0…+40 dB instead of −20…+20 dB, so the default Level is +20 dB" },
        { "Pocket Tremolo", "Stereo offset", "inert", "not-in-programme: the audit only tries the knob's end, 360°, a whole cycle = the same phase; 90° and 180° move the right channel (FlmImport189hBTests.inc)" },
        // round 190cm: FLM's Tape Stop held off is silence — the stop ramp ends on a ~11 ms decay to nothing and the input is
        // not heard until Play goes on (spec-tapestop.md §0, §3.3; FL render "Play off": silence, flmeng190by/fx/scores_all.txt)
        { "Pocket Tape Stop", "Play", "silent", "behaviour: FLM's Tape Stop is silent while Play is off (the tape has stopped); the ramps are checked by the Play-edge contexts of tools/FxAudit.h" },
        { "Pocket Wow & Flutter", "Random rate", "inert", "not-in-programme: FLM writes the sync length after the free rate, so a loaded state takes its random interval from the sync length; the free rate acts when it is moved while playing" },
    };
    return a;
}

// NOT legitimately silent: FL-parity gaps scheduled for a later round (REPORT.md, "Known gaps", with a tracker id).
// Each is a dead control today; listed so build-local stays green until its round lands, and every entry here is an
// open item. Nothing else may be added without a tracker id.
const std::vector<Allowed>& knownGaps()
{
    // round 173d3: gap173d-1 (EQUO sends), gap173d-2 (Love Philter units / articulators), gap173d-3 (Convolver EQ
    // curve) and gap173d-4 (Vocodex maps) are closed; the list stays for the next tracked gap
    static const std::vector<Allowed> a {};
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
// controls whose material only FL's installation has: without FL's Data they have nothing to switch (CI)
bool needsFlFiles (const std::string& effect, const std::string& control)
{
    // Hardcore's cabinet impulses are read from FL's Plugins\Fruity\Effects\Hardcore\Imp (natives::newEffect, never
    // bundled): without them every Cabinet choice is the bypass
    return realFlpDir.empty() && effect == "Hardcore" && control == "Cabinet";
}

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
            if (c.status == fxaudit::Status::dead && ! isAllowed (rep.name, c.control.name, "inert") && ! needsFlFiles (rep.name, c.control.name))
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
            if (f.what == "NaN") // (round 190bb: say where)
                std::printf ("  %s: NaN / inf with \"%s\" = %s (%g)\n", rep.name.c_str(), f.control.c_str(), f.text.c_str(), f.value);
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

// One test per effect kind (round speed185a; it was 4 ranges, one of them 6 minutes long: the parallel shards could not
// spread it). The same kinds and checks; the last test takes every kind after 117 (new kinds land there first).
TEST (fxaudit173d_effect_controls_000) { fxaudit173d::auditRange (0, 0); }
TEST (fxaudit173d_effect_controls_001) { fxaudit173d::auditRange (1, 1); }
TEST (fxaudit173d_effect_controls_002) { fxaudit173d::auditRange (2, 2); }
TEST (fxaudit173d_effect_controls_003) { fxaudit173d::auditRange (3, 3); }
TEST (fxaudit173d_effect_controls_004) { fxaudit173d::auditRange (4, 4); }
TEST (fxaudit173d_effect_controls_005) { fxaudit173d::auditRange (5, 5); }
TEST (fxaudit173d_effect_controls_006) { fxaudit173d::auditRange (6, 6); }
TEST (fxaudit173d_effect_controls_007) { fxaudit173d::auditRange (7, 7); }
TEST (fxaudit173d_effect_controls_008) { fxaudit173d::auditRange (8, 8); }
TEST (fxaudit173d_effect_controls_009) { fxaudit173d::auditRange (9, 9); }
TEST (fxaudit173d_effect_controls_010) { fxaudit173d::auditRange (10, 10); }
TEST (fxaudit173d_effect_controls_011) { fxaudit173d::auditRange (11, 11); }
TEST (fxaudit173d_effect_controls_012) { fxaudit173d::auditRange (12, 12); }
TEST (fxaudit173d_effect_controls_013) { fxaudit173d::auditRange (13, 13); }
TEST (fxaudit173d_effect_controls_014) { fxaudit173d::auditRange (14, 14); }
TEST (fxaudit173d_effect_controls_015) { fxaudit173d::auditRange (15, 15); }
TEST (fxaudit173d_effect_controls_016) { fxaudit173d::auditRange (16, 16); }
TEST (fxaudit173d_effect_controls_017) { fxaudit173d::auditRange (17, 17); }
TEST (fxaudit173d_effect_controls_018) { fxaudit173d::auditRange (18, 18); }
TEST (fxaudit173d_effect_controls_019) { fxaudit173d::auditRange (19, 19); }
TEST (fxaudit173d_effect_controls_020) { fxaudit173d::auditRange (20, 20); }
TEST (fxaudit173d_effect_controls_021) { fxaudit173d::auditRange (21, 21); }
TEST (fxaudit173d_effect_controls_022) { fxaudit173d::auditRange (22, 22); }
TEST (fxaudit173d_effect_controls_023) { fxaudit173d::auditRange (23, 23); }
TEST (fxaudit173d_effect_controls_024) { fxaudit173d::auditRange (24, 24); }
TEST (fxaudit173d_effect_controls_025) { fxaudit173d::auditRange (25, 25); }
TEST (fxaudit173d_effect_controls_026) { fxaudit173d::auditRange (26, 26); }
TEST (fxaudit173d_effect_controls_027) { fxaudit173d::auditRange (27, 27); }
TEST (fxaudit173d_effect_controls_028) { fxaudit173d::auditRange (28, 28); }
TEST (fxaudit173d_effect_controls_029) { fxaudit173d::auditRange (29, 29); }
TEST (fxaudit173d_effect_controls_030) { fxaudit173d::auditRange (30, 30); }
TEST (fxaudit173d_effect_controls_031) { fxaudit173d::auditRange (31, 31); }
TEST (fxaudit173d_effect_controls_032) { fxaudit173d::auditRange (32, 32); }
TEST (fxaudit173d_effect_controls_033) { fxaudit173d::auditRange (33, 33); }
TEST (fxaudit173d_effect_controls_034) { fxaudit173d::auditRange (34, 34); }
TEST (fxaudit173d_effect_controls_035) { fxaudit173d::auditRange (35, 35); }
TEST (fxaudit173d_effect_controls_036) { fxaudit173d::auditRange (36, 36); }
TEST (fxaudit173d_effect_controls_037) { fxaudit173d::auditRange (37, 37); }
TEST (fxaudit173d_effect_controls_038) { fxaudit173d::auditRange (38, 38); }
TEST (fxaudit173d_effect_controls_039) { fxaudit173d::auditRange (39, 39); }
TEST (fxaudit173d_effect_controls_040) { fxaudit173d::auditRange (40, 40); }
TEST (fxaudit173d_effect_controls_041) { fxaudit173d::auditRange (41, 41); }
TEST (fxaudit173d_effect_controls_042) { fxaudit173d::auditRange (42, 42); }
TEST (fxaudit173d_effect_controls_043) { fxaudit173d::auditRange (43, 43); }
TEST (fxaudit173d_effect_controls_044) { fxaudit173d::auditRange (44, 44); }
TEST (fxaudit173d_effect_controls_045) { fxaudit173d::auditRange (45, 45); }
TEST (fxaudit173d_effect_controls_046) { fxaudit173d::auditRange (46, 46); }
TEST (fxaudit173d_effect_controls_047) { fxaudit173d::auditRange (47, 47); }
TEST (fxaudit173d_effect_controls_048) { fxaudit173d::auditRange (48, 48); }
TEST (fxaudit173d_effect_controls_049) { fxaudit173d::auditRange (49, 49); }
TEST (fxaudit173d_effect_controls_050) { fxaudit173d::auditRange (50, 50); }
TEST (fxaudit173d_effect_controls_051) { fxaudit173d::auditRange (51, 51); }
TEST (fxaudit173d_effect_controls_052) { fxaudit173d::auditRange (52, 52); }
TEST (fxaudit173d_effect_controls_053) { fxaudit173d::auditRange (53, 53); }
TEST (fxaudit173d_effect_controls_054) { fxaudit173d::auditRange (54, 54); }
TEST (fxaudit173d_effect_controls_055) { fxaudit173d::auditRange (55, 55); }
TEST (fxaudit173d_effect_controls_056) { fxaudit173d::auditRange (56, 56); }
TEST (fxaudit173d_effect_controls_057) { fxaudit173d::auditRange (57, 57); }
TEST (fxaudit173d_effect_controls_058) { fxaudit173d::auditRange (58, 58); }
TEST (fxaudit173d_effect_controls_059) { fxaudit173d::auditRange (59, 59); }
TEST (fxaudit173d_effect_controls_060) { fxaudit173d::auditRange (60, 60); }
TEST (fxaudit173d_effect_controls_061) { fxaudit173d::auditRange (61, 61); }
TEST (fxaudit173d_effect_controls_062) { fxaudit173d::auditRange (62, 62); }
TEST (fxaudit173d_effect_controls_063) { fxaudit173d::auditRange (63, 63); }
TEST (fxaudit173d_effect_controls_064) { fxaudit173d::auditRange (64, 64); }
TEST (fxaudit173d_effect_controls_065) { fxaudit173d::auditRange (65, 65); }
TEST (fxaudit173d_effect_controls_066) { fxaudit173d::auditRange (66, 66); }
TEST (fxaudit173d_effect_controls_067) { fxaudit173d::auditRange (67, 67); }
TEST (fxaudit173d_effect_controls_068) { fxaudit173d::auditRange (68, 68); }
TEST (fxaudit173d_effect_controls_069) { fxaudit173d::auditRange (69, 69); }
TEST (fxaudit173d_effect_controls_070) { fxaudit173d::auditRange (70, 70); }
TEST (fxaudit173d_effect_controls_071) { fxaudit173d::auditRange (71, 71); }
TEST (fxaudit173d_effect_controls_072) { fxaudit173d::auditRange (72, 72); }
TEST (fxaudit173d_effect_controls_073) { fxaudit173d::auditRange (73, 73); }
TEST (fxaudit173d_effect_controls_074) { fxaudit173d::auditRange (74, 74); }
TEST (fxaudit173d_effect_controls_075) { fxaudit173d::auditRange (75, 75); }
TEST (fxaudit173d_effect_controls_076) { fxaudit173d::auditRange (76, 76); }
TEST (fxaudit173d_effect_controls_077) { fxaudit173d::auditRange (77, 77); }
TEST (fxaudit173d_effect_controls_078) { fxaudit173d::auditRange (78, 78); }
TEST (fxaudit173d_effect_controls_079) { fxaudit173d::auditRange (79, 79); }
TEST (fxaudit173d_effect_controls_080) { fxaudit173d::auditRange (80, 80); }
TEST (fxaudit173d_effect_controls_081) { fxaudit173d::auditRange (81, 81); }
TEST (fxaudit173d_effect_controls_082) { fxaudit173d::auditRange (82, 82); }
TEST (fxaudit173d_effect_controls_083) { fxaudit173d::auditRange (83, 83); }
TEST (fxaudit173d_effect_controls_084) { fxaudit173d::auditRange (84, 84); }
TEST (fxaudit173d_effect_controls_085) { fxaudit173d::auditRange (85, 85); }
TEST (fxaudit173d_effect_controls_086) { fxaudit173d::auditRange (86, 86); }
TEST (fxaudit173d_effect_controls_087) { fxaudit173d::auditRange (87, 87); }
TEST (fxaudit173d_effect_controls_088) { fxaudit173d::auditRange (88, 88); }
TEST (fxaudit173d_effect_controls_089) { fxaudit173d::auditRange (89, 89); }
TEST (fxaudit173d_effect_controls_090) { fxaudit173d::auditRange (90, 90); }
TEST (fxaudit173d_effect_controls_091) { fxaudit173d::auditRange (91, 91); }
TEST (fxaudit173d_effect_controls_092) { fxaudit173d::auditRange (92, 92); }
TEST (fxaudit173d_effect_controls_093) { fxaudit173d::auditRange (93, 93); }
TEST (fxaudit173d_effect_controls_094) { fxaudit173d::auditRange (94, 94); }
TEST (fxaudit173d_effect_controls_095) { fxaudit173d::auditRange (95, 95); }
TEST (fxaudit173d_effect_controls_096) { fxaudit173d::auditRange (96, 96); }
TEST (fxaudit173d_effect_controls_097) { fxaudit173d::auditRange (97, 97); }
TEST (fxaudit173d_effect_controls_098) { fxaudit173d::auditRange (98, 98); }
TEST (fxaudit173d_effect_controls_099) { fxaudit173d::auditRange (99, 99); }
TEST (fxaudit173d_effect_controls_100) { fxaudit173d::auditRange (100, 100); }
TEST (fxaudit173d_effect_controls_101) { fxaudit173d::auditRange (101, 101); }
TEST (fxaudit173d_effect_controls_102) { fxaudit173d::auditRange (102, 102); }
TEST (fxaudit173d_effect_controls_103) { fxaudit173d::auditRange (103, 103); }
TEST (fxaudit173d_effect_controls_104) { fxaudit173d::auditRange (104, 104); }
TEST (fxaudit173d_effect_controls_105) { fxaudit173d::auditRange (105, 105); }
TEST (fxaudit173d_effect_controls_106) { fxaudit173d::auditRange (106, 106); }
TEST (fxaudit173d_effect_controls_107) { fxaudit173d::auditRange (107, 107); }
TEST (fxaudit173d_effect_controls_108) { fxaudit173d::auditRange (108, 108); }
TEST (fxaudit173d_effect_controls_109) { fxaudit173d::auditRange (109, 109); }
TEST (fxaudit173d_effect_controls_110) { fxaudit173d::auditRange (110, 110); }
TEST (fxaudit173d_effect_controls_111) { fxaudit173d::auditRange (111, 111); }
TEST (fxaudit173d_effect_controls_112) { fxaudit173d::auditRange (112, 112); }
TEST (fxaudit173d_effect_controls_113) { fxaudit173d::auditRange (113, 113); }
TEST (fxaudit173d_effect_controls_114) { fxaudit173d::auditRange (114, 114); }
TEST (fxaudit173d_effect_controls_115) { fxaudit173d::auditRange (115, 115); }
TEST (fxaudit173d_effect_controls_116) { fxaudit173d::auditRange (116, 116); }
TEST (fxaudit173d_effect_controls_117) { fxaudit173d::auditRange (117, 117); }
TEST (fxaudit173d_effect_controls_118_up) { fxaudit173d::auditRange (118, 200); }

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

