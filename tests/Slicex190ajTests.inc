// Round 190aj: the deck editor's selection and zoom as FL saves them, what FL makes of a selection (a region, the
// loop), and the beat count of a deck by reference (REA_rodada190aj.md; numbers from FL's own saves aj_2…aj_29.flp)
#include "core/Slicex.h"

TEST (slicex190aj_selection_and_view_are_saved_in_beats)
{
    using namespace lynx;
    auto s = slicex::fresh();
    CHECK (! s.decks[0].hasSelection() && s.decks[0].selectionStart() == -1.0 && s.decks[0].selectionEnd() == -1.0);
    // FL's save after a drag over the wave: 1.78309…3.31693 beats, the view 0.57633…7.42109
    slicex::setDeckSelection (s, 0, 1.783088793019204, 3.3169286149712076);
    slicex::setDeckBeats (s, 0, 0.5763301107581897, 7.421090316219005);
    const std::uint8_t fl[32] { 0x03, 0x1b, 0x3e, 0x1d, 0x88, 0x87, 0xfc, 0x3f, 0x49, 0xbe, 0xa3, 0xde, 0x11, 0x89, 0x0a, 0x40,
                                0xe8, 0x01, 0x2d, 0xd8, 0x4b, 0x71, 0xe2, 0x3f, 0xad, 0x4a, 0xc3, 0x4c, 0x32, 0xaf, 0x1d, 0x40 };
    CHECK (std::memcmp (s.decks[0].params.data(), fl, 32) == 0);
    const auto back = slicex::parse (slicex::write (s));
    CHECK (back && back->decks[0].hasSelection() && back->decks[0].selectionStart() == 1.783088793019204 && back->decks[0].selectionEnd() == 3.3169286149712076);
    CHECK (back && back->decks[0].beatStart() == 0.5763301107581897 && back->decks[0].beatEnd() == 7.421090316219005);
    // sorted; an empty one is none
    slicex::setDeckSelection (s, 0, 3.0, 1.0);
    CHECK (s.decks[0].selectionStart() == 1.0 && s.decks[0].selectionEnd() == 3.0);
    slicex::setDeckSelection (s, 0, 2.0, 2.0);
    CHECK (! s.decks[0].hasSelection() && s.decks[0].selectionStart() == -1.0 && s.decks[0].selectionEnd() == -1.0);
    slicex::setDeckSelection (s, 1, -1.0, -1.0);
    CHECK (! s.decks[1].hasSelection());
    CHECK (! s.decks[0].showsLoop());
}

TEST (slicex190aj_a_selection_becomes_frames_as_in_fl)
{
    using namespace lynx;
    // "Add region" on 1.4117647…2.1960784 beats (44 100 frames a beat): FL wrote the region 62259…96846
    auto r = slicex::framesOf (1.411764705882353, 2.196078431372549, 44100.0, 352800);
    CHECK (r.first == 62259 && r.end == 96847);
    // "Set loop" on 2.5322129…3.6526611: FL wrote the loop 111671…161081
    r = slicex::framesOf (2.532212885154062, 3.65266106442577, 44100.0, 352800);
    CHECK (r.first == 111671 && r.end - 1 == 161081);
    // "Select next region" on the region 308700…320643: FL saved 7…7.27084 (and back)
    r = slicex::framesOf (308700.0 / 44100.0, 320644.0 / 44100.0, 44100.0, 352800);
    CHECK (r.first == 308700 && r.end == 320644);
    // swapped ends, the sample's ends, nonsense
    r = slicex::framesOf (9.0, -1.0, 44100.0, 352800);
    CHECK (r.first == 0 && r.end == 352800);
    r = slicex::framesOf (1.0, 2.0, 0.0, 352800);
    CHECK (r.first == 0 && r.end == 0);
    // two "Zoom in" steps from 0…8 beats: FL saved 1.0718286081087496…6.928171391891251
    const auto length = 8.0 * slicex::zoomStep * slicex::zoomStep;
    CHECK (std::fabs (length - 5.856342783782502) < 1.0e-9);
    CHECK (std::fabs ((4.0 - length * 0.5) - 1.0718286081087496) < 1.0e-9);
    // "Zoom on right of selection": 357 frames across FL's 714 pixels
    CHECK (714.0 * slicex::zoomEdgeFramesPerPixel == 357.0);
}

TEST (slicex190aj_set_loop_writes_the_sampler_chunk)
{
    using namespace lynx;
    const auto wav = slicex190h::wavWith (1000, { { 1, 0, 0, "Kick", -1 }, { 2, 250, 100, "Snare", 61 } });
    std::int64_t a = -1, b = -1, frames = 0;
    CHECK (! slicex::wavLoop (wav, a, b));
    // no sampler chunk: one with a single loop goes in after the audio; the regions and the audio stay
    const auto looped = slicex::withLoop (wav, 250, 349);
    CHECK (looped.size() == wav.size() + 68 && slicex::wavLoop (looped, a, b) && a == 250 && b == 349);
    CHECK ((std::size_t) (looped[4] | (looped[5] << 8) | (looped[6] << 16) | (looped[7] << 24)) == looped.size() - 8);
    const auto regions = slicex::wavRegions (looped, frames);
    CHECK (frames == 1000 && regions.size() == 2 && regions[1].name == "Snare" && regions[1].key == 61 && regions[1].end == 349);
    // a chunk with a loop: only the two points change (FL's save: the same chunk sizes before and after "Set loop")
    const auto again = slicex::withLoop (looped, 10, 999);
    CHECK (again.size() == looped.size() && slicex::wavLoop (again, a, b) && a == 10 && b == 999);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < again.size(); ++i)
        differing += again[i] != looped[i] ? 1u : 0u;
    CHECK (differing <= 8);
    // the region list rewritten keeps the loop
    CHECK (slicex::wavLoop (slicex::withRegions (again, regions), a, b) && a == 10 && b == 999);
    CHECK (slicex::withLoop ({}, 0, 10).empty() && slicex::withLoop (wav, 5, 4).empty() && slicex::withLoop (wav, -1, 4).empty());
}

// The saved range of a deck is the editor's zoom (FL writes it zoomed and scrolled), so a deck by reference takes its
// beat count from its file; LYNX's own states have the file's count there, so nothing changes for them
TEST (slicex190aj_deck_by_reference_takes_its_beats_from_the_file)
{
    using namespace lynx;
    auto s = slicex::fresh();
    slicex::setDeckFile (s, 0, "loop.wav", 4);
    slicex::setDeckBeats (s, 0, 0.0, 8.0);             // as the editor writes it on load: the file's beat count
    CHECK (slicex::deckBeats (s.decks[0], 8.0) == 8.0);
    CHECK (slicex::deckBeats (s.decks[0], 0.0) == 8.0); // (the file is not known: the saved range, as before)
    slicex::setDeckBeats (s, 0, 0.5763301107581897, 7.421090316219005); // zoomed and scrolled
    CHECK (slicex::deckBeats (s.decks[0], 8.0) == 8.0);
    CHECK (std::fabs (slicex::deckBeats (s.decks[0], 0.0) - 6.844760205460815) < 1.0e-12);
    // an embedded WAV: its own count when it has one, else the saved range (not the file's guess)
    s.decks[0].wav = slicex190h::wavWith (1000, { { 1, 0, 0, "Kick", -1 } });
    slicex::setDeckBeats (s, 0, 0.0, 4.0);
    CHECK (slicex::deckBeats (s.decks[0], 2.0) == 4.0);
}
