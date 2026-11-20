// Round natives190kj: FL Newtone's note analysis (measured on FL 2026's own DLL in the EXEC harness, 23 files) and the
// editor commands of FL's Newtone window (Select time around / before / after, Export regions, Sync, the score).
// docs/analise/REA_rodada190kj-newtone.md

#include "core/AudioEdit.h"
#include "core/NewtoneFx.h"

namespace natives190kj
{
// a sung line: harmonics of each note's f0 through a vowel-like resonance; `spec` = (MIDI note or 0 for a rest, seconds)
lynx::SampleData line (const std::vector<std::pair<double, double>>& spec)
{
    lynx::SampleData s;
    s.sampleRate = 44100.0;
    double ph = 0.0;
    for (const auto& [midi, seconds] : spec)
    {
        const auto n = (int) (seconds * 44100.0);
        const auto f0 = midi > 0.0 ? 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) : 0.0;
        const auto ramp = 882; // 20 ms fades, as round 95's synthetic voices
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            if (f0 > 0.0)
            {
                ph += 2.0 * M_PI * f0 / 44100.0;
                for (int k = 1; k * f0 < 6000.0; ++k)
                {
                    const auto f = k * f0;
                    const auto res = 1.0 / std::sqrt (1.0 + std::pow ((f - 700.0) / 150.0, 2.0)) + 0.2;
                    v += (float) (0.2 * res / k * std::sin (k * ph));
                }
                v *= (float) std::min ({ 1.0, i / (double) ramp, (n - i) / (double) ramp });
            }
            s.left.push_back (v);
        }
    }
    return s;
}
} // namespace natives190kj

TEST (natives190kj_newtone_analysis_tiles_the_file_like_fl)
{
    using namespace lynx;
    // FL's analysis (EXEC on Newtone_x64.dll, round natives190kj): a lead-in note (unvoiced, the first note's pitch) from 0
    // to the first onset, then each note from its onset to the next one (a rest belongs to the note before), the last
    // one to the end; fOrigPitch from whole-sample periods; volume fades 0.2 x the length; 73 ms smooth-in after silence
    const auto s = natives190kj::line ({ { 0, 0.2 }, { 60, 0.35 }, { 0, 0.1 }, { 62, 0.35 }, { 0, 0.1 }, { 64, 0.35 }, { 0, 0.3 } });
    const auto t = newtonefx::analyseNotes (s);
    CHECK (t.pitches.size() == 4u);
    if (t.pitches.size() != 4u)
        return;
    const auto& lead = t.pitches[0];
    CHECK (lead.origStart == 0 && ! lead.voiced() && lead.validEnd == 0 && lead.velocity == 0.0);
    CHECK (std::abs (lead.origPitch - t.pitches[1].origPitch) < 1e-9);
    for (std::size_t k = 0; k + 1 < t.pitches.size(); ++k)
        CHECK (t.pitches[k].origEnd == t.pitches[k + 1].origStart && t.pitches[k].curEnd == t.pitches[k].origEnd);
    CHECK (t.pitches.back().origEnd == (std::int64_t) s.frames());
    // onsets at 0.2, 0.65, 1.1 s: FL places them within ~150 samples; LYNX within 10 ms
    const double onsets[] = { 0.2, 0.65, 1.1 };
    // whole-sample periods: C4 261.63 Hz -> 44100 / floor (168.56) = 262.5 Hz = 60.0575 (FL: 60.058); D4 -> 62.0889; E4 -> 64.0384
    const double pitch[] = { 69.0 + 12.0 * std::log2 (44100.0 / 168.0 / 440.0), 69.0 + 12.0 * std::log2 (44100.0 / 150.0 / 440.0),
                             69.0 + 12.0 * std::log2 (44100.0 / 133.0 / 440.0) };
    for (int k = 0; k < 3; ++k)
    {
        const auto& p = t.pitches[(std::size_t) k + 1];
        CHECK (std::abs ((double) p.origStart / 44100.0 - onsets[k]) < 0.01);
        CHECK (std::abs (p.origPitch - pitch[k]) < 0.002);
        CHECK (p.voiced() && p.validEnd > p.origStart && p.validEnd < p.origEnd); // (the rest after it is its own silence)
        CHECK (std::abs (p.fadeInTime - 0.2 * (double) (p.origEnd - p.origStart)) < 1e-6 && p.fadeOutTime == p.fadeInTime);
        CHECK (std::abs (p.smoothIn - 0.073 * 44100.0) < 1.0 && p.origSmoothIn == p.smoothIn);
        CHECK (p.smoothOut >= (double) (p.origEnd - p.validEnd)); // (the silence after the voice, as FL's)
        CHECK (! p.edited (1.0));
    }
    // C, D, E: FL's KeyIdx 0 (C major), as FL's analysis of round natives190kj's s1
    CHECK (t.key == 0 && newtonefx::keyScaleMask (0) == 0b101010110101 && newtonefx::keyScaleMask (21) == 0b010110101101);
    // unedited: the source plays as it is (bit-identical)
    CHECK (newtonefx::renderEdits (s, t) == nullptr);

    // a legato step (no silence): the boundary at the pitch change, a short smooth-in (FL: 0-210 samples on steps)
    const auto l = natives190kj::line ({ { 0, 0.1 }, { 72, 0.4 }, { 76, 0.4 }, { 0, 0.2 } });
    const auto tl = newtonefx::analyseNotes (l);
    CHECK (tl.pitches.size() == 3u);
    if (tl.pitches.size() == 3u)
    {
        CHECK (std::abs ((double) tl.pitches[2].origStart / 44100.0 - 0.5) < 0.012);
        CHECK (tl.pitches[2].smoothIn < 0.073 * 44100.0);
    }
}

TEST (natives190kj_newtone_native_v3_and_old_states)
{
    using namespace lynx;
    const auto s = natives190kj::line ({ { 0, 0.15 }, { 57, 0.4 }, { 60, 0.4 }, { 0, 0.1 } });
    auto t = newtonefx::analyseNotes (s);
    CHECK (t.pitches.size() == 3u);
    if (t.pitches.size() < 3u)
        return;
    t.pitches[2].shift = 1.5;
    // version 3 keeps the voiced end and the velocity
    const auto v3 = newtonefx::writeNative (t);
    CHECK (v3.size() > 8 && v3[4] == 3);
    const auto back = newtonefx::parseNative (v3);
    CHECK (back && back->pitches.size() == t.pitches.size() && back->key == t.key);
    if (back && back->pitches.size() == t.pitches.size())
        for (std::size_t k = 0; k < t.pitches.size(); ++k)
            CHECK (back->pitches[k].validEnd == t.pitches[k].validEnd && back->pitches[k].velocity == t.pitches[k].velocity);

    // an old version-2 state (as round 165g wrote it: no voiced end) still loads, its notes whole, and renders exactly
    // as the same notes in version 3 (the render does not read the new fields: old projects play bit-identically)
    auto v2 = v3;
    v2[4] = 2;
    // (version 3 adds the key after the knobs and two fields per note)
    std::vector<std::uint8_t> trimmed (v2.begin(), v2.begin() + 4 + 4 + 8 + 8 * 4);
    trimmed.insert (trimmed.end(), v2.begin() + 4 + 4 + 8 + 8 * 4 + 4, v2.begin() + 4 + 4 + 8 + 8 * 4 + 8);
    std::size_t at = 4 + 4 + 8 + 8 * 4 + 8;
    for (std::size_t k = 0; k < t.pitches.size(); ++k)
    {
        const auto note = 4 * 8 + 16 * 8 + 1 + 4 * 8; // the version-2 note
        trimmed.insert (trimmed.end(), v2.begin() + (std::ptrdiff_t) at, v2.begin() + (std::ptrdiff_t) (at + note));
        at += note + 16; // (skip the version-3 fields)
    }
    const auto old = newtonefx::parseNative (trimmed);
    CHECK (old && old->pitches.size() == t.pitches.size());
    if (old && back)
    {
        CHECK (old->pitches[1].validEnd == -1 && old->pitches[1].voiced() && old->pitches[1].voicedEnd() == old->pitches[1].origEnd);
        const auto a = newtonefx::renderEdits (s, *old), b = newtonefx::renderEdits (s, *back);
        CHECK (a != nullptr && b != nullptr && a->left == b->left);
    }
}

TEST (natives190kj_newtone_editor_commands)
{
    using namespace lynx;
    const auto s = natives190kj::line ({ { 0, 0.2 }, { 60, 0.4 }, { 0, 0.1 }, { 64, 0.4 }, { 0, 0.1 }, { 67, 0.4 }, { 0, 0.2 } });
    auto t = newtonefx::analyseNotes (s);
    CHECK (t.pitches.size() == 4u);
    if (t.pitches.size() != 4u)
        return;
    // Select time around selection: from the first chosen note's start to the last one's voice end
    const auto [a, b] = newtonefx::timeAround (t, { 1, 2 });
    CHECK (a == t.pitches[1].curStart && b == t.pitches[2].validEnd);
    // the notes in a time range (Select before / after current selection): voiced notes only (not the lead-in)
    CHECK ((newtonefx::notesIn (t, 0, t.pitches[2].curStart) == std::vector<int> { 1 }));
    CHECK ((newtonefx::notesIn (t, t.pitches[2].curStart, (std::int64_t) s.frames()) == std::vector<int> { 2, 3 }));

    // Export regions: one region per voiced note, named after the key it sounds (moved notes: their new key)
    t.pitches[3].shift = 2.0;
    const auto doc = newtonefx::regionsDocument (s, t);
    CHECK (doc.markers.size() == 3u && doc.audio.frames() == (std::int64_t) s.frames());
    if (doc.markers.size() == 3u)
    {
        CHECK (doc.markers[0].name == "C5" && doc.markers[0].keyNum == 60 && doc.markers[0].isRegion());
        CHECK (doc.markers[1].name == "E5" && doc.markers[2].name == "A5" && doc.markers[2].keyNum == 69);
        CHECK (doc.markers[1].start == t.pitches[2].curStart && doc.markers[1].end == t.pitches[2].curEnd);
    }
    // ... written as cue + adtl regions and read back
    const auto bytes = audioedit::encodeWav (doc);
    audioedit::Document backDoc;
    CHECK (audioedit::decodeWav (bytes.data(), bytes.size(), backDoc) && backDoc.markers.size() == 3u);
    // Send to Playlist: the note starts as markers
    const auto marks = newtonefx::regionsDocument (s, t, true);
    CHECK (marks.markers.size() == 3u && ! marks.markers[0].isRegion());

    // the score: voiced notes from their start to their voice's end (no silence, no lead-in)
    const auto sc = newtonefx::score (t, 44100.0);
    CHECK (sc.size() == 3u);
    if (sc.size() == 3u)
    {
        CHECK (std::abs (sc[0].start - (double) t.pitches[1].curStart / 44100.0) < 1e-9);
        CHECK (std::abs (sc[0].length - (double) (t.pitches[1].validEnd - t.pitches[1].origStart) / 44100.0) < 1e-9);
        CHECK (sc[2].key == 69);
    }

    // Sync: every note placed and stretched by the ratio; back to 1 restores the recorded timing
    auto u = t;
    newtonefx::stretchAll (u, 1.25);
    CHECK (u.pitches[2].curStart == std::llround ((double) t.pitches[2].origStart * 1.25) && std::abs (u.pitches[2].stretch - 0.8) < 0.001 && u.edited);
    newtonefx::stretchAll (u, 1.0);
    CHECK (u.pitches[2].curStart == t.pitches[2].origStart && u.pitches[2].stretch == 1.0);

    // Quantize time: note starts to the grid, the joined note before ends there, both stretched
    auto qz = t;
    const auto grid = 11025.0; // (a 1/4 beat at 60 BPM)
    newtonefx::quantizeTime (qz, { 2 }, grid);
    CHECK (qz.pitches[2].curStart % 11025 == 0 && qz.pitches[1].curEnd == qz.pitches[2].curStart);
    CHECK (std::abs (qz.pitches[2].stretch * (double) (qz.pitches[2].curEnd - qz.pitches[2].curStart) - (double) (qz.pitches[2].origEnd - qz.pitches[2].origStart)) < 1e-6);
    CHECK (qz.pitches[0].curStart == 0 && qz.edited);

    // the selection as its own sample
    const auto part = newtonefx::cropped (s, 1000, 5000);
    CHECK (part->frames() == 4000u && part->left[0] == s.left[1000]);
    CHECK (newtonefx::cropped (s, 10, 10)->frames() == s.frames());
}

TEST (natives190kj_newtone_analysis_cpu_report)
{
    using namespace lynx;
    // the analysis of a 10 s sung line (offline, on Analyse / a new sample): its time
    std::vector<std::pair<double, double>> spec;
    for (int k = 0; k < 20; ++k)
        spec.push_back ({ 57.0 + (k * 5) % 12, 0.4 }), spec.push_back ({ 0, 0.1 });
    const auto s = natives190kj::line (spec);
    const auto t0 = std::chrono::steady_clock::now();
    const auto t = newtonefx::analyseNotes (s);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("natives190kj_newtone_analysis_cpu_report: %d notes in %.1f ms for %.1f s of audio\n", (int) t.pitches.size(), ms, (double) s.frames() / 44100.0);
    CHECK (t.pitches.size() == 20u); // (the line starts at 0: no lead-in note)
}
