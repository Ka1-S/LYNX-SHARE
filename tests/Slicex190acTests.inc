// Round 190ac: Slicex region list edits (the deck's WAV rewritten as FL's writer lays it out)
#include "core/Slicex.h"

// rename / delete / clone / move / add all go through slicex::withRegions: cue points, labels, lengths and keys of
// the list given, where the old chunks were
TEST (slicex190ac_region_list_is_rewritten_in_the_wav)
{
    using namespace lynx;
    const auto wav = slicex190h::wavWith (1000, { { 1, 0, 0, "Kick", -1 }, { 2, 250, 100, "Snare", 61 }, { 3, 500, 0, nullptr, -1 } });
    std::int64_t frames = 0;
    const auto regions = slicex::wavRegions (wav, frames);
    CHECK (frames == 1000 && regions.size() == 3);
    CHECK (regions[1].purpose == "rgn " && ! regions[1].marker && regions[1].end == 349 && regions[0].marker && regions[0].purpose.empty());
    // unchanged list: the same file
    CHECK (slicex::withRegions (wav, regions) == wav);

    // rename: only the label changes (an odd text length is padded)
    auto list = regions;
    list[0].name = "Bass drum";
    auto out = slicex::withRegions (wav, list);
    auto back = slicex::wavRegions (out, frames);
    CHECK (frames == 1000 && back.size() == 3 && back[0].name == "Bass drum" && back[1].name == "Snare" && back[1].key == 61 && back[1].end == 349);
    CHECK (out.size() % 2 == 0 && (std::size_t) (out[4] | (out[5] << 8) | (out[6] << 16) | (out[7] << 24)) == out.size() - 8);
    list[2].name = "Hat";
    back = slicex::wavRegions (slicex::withRegions (wav, list), frames);
    CHECK (back[2].name == "Hat");

    // delete the first: the others keep name, length and key (the key list follows the new numbering)
    list = regions;
    list.erase (list.begin());
    out = slicex::withRegions (wav, list);
    back = slicex::wavRegions (out, frames);
    CHECK (back.size() == 2 && back[0].start == 250 && back[0].name == "Snare" && back[0].key == 61 && back[0].end == 349 && back[1].start == 500 && back[1].key == -1);
    CHECK (slicex::keyMap (0, back, {}, true)[61].size() == 1);

    // a new marker and a moved start
    list = regions;
    slicex::WavRegion marker;
    marker.start = 750;
    list.push_back (marker);
    list[2].start = 600;
    back = slicex::wavRegions (slicex::withRegions (wav, list), frames);
    CHECK (back.size() == 4 && back[2].start == 600 && back[2].end == 749 && back[3].start == 750 && back[3].end == 999 && back[3].marker);

    // a clone: a region of its own over the same stretch
    list = regions;
    auto copy = list[0];
    copy.marker = false;
    list.insert (list.begin() + 1, copy);
    back = slicex::wavRegions (slicex::withRegions (wav, list), frames);
    CHECK (back.size() == 4 && back[1].start == 0 && back[1].end == 249 && ! back[1].marker && back[1].name == "Kick" && back[2].key == 61);

    // the text of FL's "Set info" is kept
    list = regions;
    list[1].info = "ghost";
    back = slicex::wavRegions (slicex::withRegions (wav, list), frames);
    CHECK (back[1].info == "ghost" && back[1].name == "Snare");

    // no regions: the three chunks go, the audio stays; and back again
    out = slicex::withRegions (wav, {});
    CHECK (slicex::wavRegions (out, frames).empty() && frames == 1000 && out.size() < wav.size());
    CHECK (slicex::withRegions (out, regions) == wav);
    CHECK (slicex::withRegions ({ 1, 2, 3 }, regions).empty());
}

// FL's tempo for a sample without one: the power-of-two beat count with 80 < BPM <= 160 (what FL showed for the files
// of this round: length in frames at 44.1 kHz -> BPM shown)
TEST (slicex190ac_untagged_sample_tempo_is_fls_guess)
{
    using namespace lynx;
    struct Seen { double frames; double beats; int shown; };
    const Seen seen[] = { { 132300, 8, 160 }, { 60417, 2, 88 }, { 126000, 4, 84 }, { 123070, 4, 86 }, { 129073, 4, 82 },
                          { 130667, 4, 81 }, { 127518, 4, 83 }, { 131478, 4, 81 }, { 129865, 4, 82 } };
    for (const auto& s : seen)
    {
        const auto beats = slicex::guessBeats (s.frames / 44100.0);
        CHECK (beats == s.beats);
        CHECK ((int) std::lround (beats * 60.0 * 44100.0 / s.frames) == s.shown);
    }
    CHECK (slicex::guessBeats (0.0) == 0.0);
    CHECK (slicex::guessBeats (0.1) == 0.25); // (shorter than a beat at 160: halved [not seen in FL])
}

// The tempo of a deck is its sample's own beat count; the saved view range (which FL writes zoomed: 0..6.845 beats of
// an 8-beat loop that still showed 60BPM) only stands in for a deck without an embedded WAV
TEST (slicex190ac_deck_tempo_survives_a_zoomed_view)
{
    using namespace lynx;
    auto wav = slicex190h::wavWith (1000, { { 1, 0, 0, "Kick", -1 } });
    CHECK (slicex::wavBeats (wav) == 0.0);
    // an ACID chunk: flags, root note, 0x80, 0.0, beats, meter, tempo
    const std::uint8_t acid[] = { 'a', 'c', 'i', 'd', 24, 0, 0, 0, 0, 0, 0, 0, 0x3c, 0, 0x80, 0, 0, 0, 0, 0, 8, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0x70, 0x42 };
    wav.insert (wav.end(), std::begin (acid), std::end (acid));
    const auto size = (std::uint32_t) (wav.size() - 8);
    std::memcpy (wav.data() + 4, &size, 4);
    CHECK (slicex::wavBeats (wav) == 8.0);
    std::int64_t frames = 0;
    CHECK (slicex::wavBeats (slicex::withRegions (wav, slicex::wavRegions (wav, frames))) == 8.0);

    auto s = slicex::fresh();
    slicex::setDeckBeats (s, 0, 0.0, 6.84476021);
    CHECK (s.decks[0].beats() == 6.84476021); // by reference: the range is all there is
    s.decks[0].wav = wav;
    CHECK (s.decks[0].beats() == 8.0);
    s.decks[1].wav = slicex190h::wavWith (1000, { { 1, 0, 0, "Kick", -1 } });
    slicex::setDeckBeats (s, 1, 0.0, 4.0);
    CHECK (s.decks[1].beats() == 4.0);
    CHECK (slicex::wavBeats ({}) == 0.0);
}
