// Round 190jv, helper "pr" (BUILD-LIST 20, coverage pianoroll-0183): FL's Piano roll View › Scale highlighting ›
// Automatic — FL's Krumhansl-Schmuckler key guess (REA of FLEngine_x64.dll: FUN_00ea8a80 / FUN_00dd3ed0 / FUN_00e500c0;
// the facts in src/core/ScaleDetect.h and docs/analise/REA_rodada190jv.md, "pr").
#include "core/ScaleDetect.h"

namespace pr190jv
{
inline std::vector<lynx::Note> notesOf (std::initializer_list<int> keys, int channel = 0)
{
    std::vector<lynx::Note> out;
    std::int64_t start = 0;
    for (const auto key : keys)
    {
        lynx::Note n;
        n.key = key;
        n.channel = channel;
        n.start = start;
        start += 24;
        out.push_back (n);
    }
    return out;
}
inline bool anyNote (const lynx::Note&) { return true; }
inline bool noSelection (int) { return false; }
} // namespace pr190jv

TEST (pr190jv_auto_scale_fl_profiles_and_threshold)
{
    using namespace lynx;
    using namespace pr190jv;
    // FL counts 0.25 per note (not its length); fewer than 3 notes: no guess
    CHECK (scaledetect::detect (notesOf ({ 60, 64 }), anyNote, noSelection) == scaledetect::none);
    CHECK (scaledetect::detect (notesOf ({}), anyNote, noSelection) == scaledetect::none);
    // the C major scale and its triad: C major (index 0)
    CHECK (scaledetect::detect (notesOf ({ 60, 62, 64, 65, 67, 69, 71 }), anyNote, noSelection) == 0);
    CHECK (scaledetect::detect (notesOf ({ 60, 64, 67 }), anyNote, noSelection) == 0);
    // an A minor triad and an A minor line with its G#: A minor (12 + 9)
    CHECK (scaledetect::detect (notesOf ({ 57, 60, 64 }), anyNote, noSelection) == 21);
    CHECK (scaledetect::detect (notesOf ({ 57, 57, 57, 60, 64, 64, 59, 62, 68 }), anyNote, noSelection) == 21);
    // D major, F# minor
    CHECK (scaledetect::detect (notesOf ({ 62, 66, 69, 62, 64, 66, 67, 69, 71, 73 }), anyNote, noSelection) == 2);
    CHECK (scaledetect::detect (notesOf ({ 66, 66, 69, 73, 66, 69, 73, 65, 68 }), anyNote, noSelection) == 18);
    // every pitch class once: no correlation anywhere, the first (C major) stays (FL's strict "greater" scan)
    CHECK (scaledetect::detect (notesOf ({ 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71 }), anyNote, noSelection) == 0);
    // only the pitch class counts: the same notes an octave apart guess the same key
    CHECK (scaledetect::detect (notesOf ({ 45, 72, 88 }), anyNote, noSelection) == 21);
}

TEST (pr190jv_auto_scale_counts_the_edited_channel_and_the_selection)
{
    using namespace lynx;
    using namespace pr190jv;
    // channel 0: an A minor triad; channel 1: a D major line
    auto notes = notesOf ({ 57, 60, 64 }, 0);
    for (const auto& n : notesOf ({ 62, 66, 69, 62, 66, 69 }, 1))
        notes.push_back (n);
    const auto channel0 = [] (const Note& n) { return n.channel == 0; };
    const auto channel1 = [] (const Note& n) { return n.channel == 1; };
    CHECK (scaledetect::detect (notes, channel0, noSelection) == 21);
    CHECK (scaledetect::detect (notes, channel1, noSelection) == 2);
    // with some of the edited notes selected only those count (3 selected on channel 1: D F# A)
    const auto firstThreeOfChannel1 = [] (int i) { return i >= 3 && i < 6; };
    CHECK (scaledetect::detect (notes, channel1, firstThreeOfChannel1) == 2);
    // a selection on another channel does not narrow the edited channel's notes
    const auto selectedOnChannel1 = [] (int i) { return i == 4; };
    CHECK (scaledetect::detect (notes, channel0, selectedOnChannel1) == 21);
    // two selected notes of the edited channel: fewer than 3 counted, no guess
    const auto twoOfChannel0 = [] (int i) { return i < 2; };
    CHECK (scaledetect::detect (notes, channel0, twoOfChannel0) == scaledetect::none);
}

TEST (pr190jv_auto_scale_highlighted_rows_are_fl_sets)
{
    using namespace lynx;
    // FL's out-of-scale sets (0xe504ac major 1 3 6 8 10, 0xe504cc minor 1 4 6 9 10): major, and the harmonic minor
    const int major[] = { 0, 2, 4, 5, 7, 9, 11 }, minor[] = { 0, 2, 3, 5, 7, 8, 11 };
    for (int root = 0; root < 12; ++root)
        for (int degree = 0; degree < 12; ++degree)
        {
            const auto inMajor = std::find (std::begin (major), std::end (major), degree) != std::end (major);
            const auto inMinor = std::find (std::begin (minor), std::end (minor), degree) != std::end (minor);
            CHECK (scaledetect::inScale (root, 60 + root + degree) == inMajor);
            CHECK (scaledetect::inScale (12 + root, 48 + root + degree) == inMinor);
        }
    CHECK (scaledetect::inScale (21, 68));   // A minor: G# in (harmonic)
    CHECK (! scaledetect::inScale (21, 67)); // and G out
    for (int key = 0; key < 128; ++key)
        CHECK (! scaledetect::inScale (scaledetect::none, key)); // no guess: FL highlights no row
    CHECK (scaledetect::name (21) == "A Minor");
    CHECK (scaledetect::name (6) == "F# Major");
    CHECK (scaledetect::name (-1).empty());
}

TEST (pr190jv_auto_scale_in_the_piano_roll)
{
    // the Piano roll offers it in the scale box and in View › Scale highlighting, guesses only while it is chosen and
    // again only after an edit / selection / channel / pattern change (no timer: the paint asks)
    const auto roll = i18ntest::readFile ("src/ui/PianoRollComponent.cpp");
    CHECK (roll.find ("scaleBox.addItem (tr (\"Automatic\"), autoScaleId);") != std::string::npos);
    CHECK (roll.find ("menu.addSubMenu (tr (\"Scale highlighting\"), scaleHighlightMenu());") != std::string::npos);
    CHECK (roll.find ("menu.addItem (tr (\"Automatic\"), true, isAutoScale(), [this] { setAutoScale (true); });") != std::string::npos);
    const auto update = roll.find ("void PianoRollComponent::updateAutoKey()");
    CHECK (update != std::string::npos);
    CHECK (roll.find ("if (! isAutoScale())\n        return;", update) != std::string::npos);
    CHECK (roll.find ("updateAutoKey(); // (round 190jv", roll.find ("void PianoRollComponent::paint (juce::Graphics& g)")) != std::string::npos);
    const auto header = i18ntest::readFile ("src/ui/PianoRollComponent.h");
    CHECK (header.find ("static constexpr int autoScaleId = 2;") != std::string::npos);
}
