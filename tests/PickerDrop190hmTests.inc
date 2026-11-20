// Round 190hm (issue #177): Picker panel entries dragged onto the Playlist, placed as FL Studio 2026 places them
// (core/PickerDrop.h; FLEngine FUN_00e4daa0 / FUN_00e7a470 / FUN_012f30b0 / FUN_00e3a710).

#include "core/PickerDrop.h"

namespace pickerdrop190hm
{
inline lynx::Clip clip (lynx::ClipKind kind, int index, std::int64_t length)
{
    lynx::Clip c;
    c.kind = kind;
    c.pattern = kind == lynx::ClipKind::pattern ? index : 0;
    c.channel = kind == lynx::ClipKind::audio ? index : -1;
    c.automation = kind == lynx::ClipKind::automation ? index : -1;
    c.length = length;
    return c;
}

// tracks 0..7; track 3 an audio track, track 5 an instrument track
inline lynx::Song song()
{
    lynx::Song s;
    s.playlistTracks.resize (8);
    s.playlistTracks[3].mode = lynx::PlaylistTrackMode::audio;
    s.playlistTracks[5].mode = lynx::PlaylistTrackMode::instrument;
    return s;
}
} // namespace pickerdrop190hm

TEST (pickerdrop190hm_dragged_entries_follow_the_picker_selection)
{
    using lynx::pickerdrop::draggedEntries;
    // a press on an entry that is not picked: that entry alone (FL clears the selection and picks it)
    CHECK (draggedEntries ({ 1, 4 }, 2) == std::vector<int> { 2 });
    // a press on a picked entry: every picked one, in the order they were picked
    CHECK (draggedEntries ({ 4, 1, 6 }, 1) == std::vector<int> ({ 4, 1, 6 }));
    CHECK (draggedEntries ({}, 0) == std::vector<int> { 0 });
    CHECK (draggedEntries ({ 1 }, -1).empty());
}

TEST (pickerdrop190hm_plain_drop_goes_down_one_track_per_entry)
{
    using namespace pickerdrop190hm;
    using lynx::ClipKind;
    const auto s = song();
    const std::vector<lynx::Clip> patterns { clip (ClipKind::pattern, 0, 384), clip (ClipKind::pattern, 2, 768) };
    CHECK (! lynx::pickerdrop::asksAboutInstrumentTrack (s, patterns, 0, false));
    const auto placed = lynx::pickerdrop::place (s, patterns, 0, 1536, false, false);
    CHECK (placed.size() == 2);
    CHECK (placed[0].track == 0 && placed[0].start == 1536 && placed[0].pattern == 0 && placed[0].length == 384);
    CHECK (placed[1].track == 1 && placed[1].start == 1536 && placed[1].pattern == 2 && placed[1].length == 768);
    // a negative time starts at 0
    CHECK (lynx::pickerdrop::place (s, patterns, 0, -50, false, false)[0].start == 0);
    // the current pattern becomes the last dropped one; Shift keeps it
    CHECK (lynx::pickerdrop::currentPatternAfter (placed, false, 7) == 2);
    CHECK (lynx::pickerdrop::currentPatternAfter (placed, true, 7) == 7);
    CHECK (lynx::pickerdrop::currentPatternAfter ({ clip (ClipKind::audio, 3, 96) }, false, 7) == 7);
}

TEST (pickerdrop190hm_audio_track_takes_only_audio_clips)
{
    using namespace pickerdrop190hm;
    using lynx::ClipKind;
    const auto s = song();
    // three patterns from track 2: the one meant for audio track 3 is skipped, the third still goes to track 4
    const std::vector<lynx::Clip> patterns { clip (ClipKind::pattern, 0, 384), clip (ClipKind::pattern, 1, 384), clip (ClipKind::pattern, 2, 384) };
    const auto placed = lynx::pickerdrop::place (s, patterns, 2, 0, false, false);
    CHECK (placed.size() == 2);
    CHECK (placed[0].track == 2 && placed[0].pattern == 0);
    CHECK (placed[1].track == 4 && placed[1].pattern == 2);
    // an audio clip goes on the audio track
    const auto audio = lynx::pickerdrop::place (s, { clip (ClipKind::audio, 9, 192) }, 3, 96, false, false);
    CHECK (audio.size() == 1 && audio[0].track == 3 && audio[0].channel == 9 && audio[0].start == 96);
    // a pattern goes on an instrument track without a question
    CHECK (! lynx::pickerdrop::asksAboutInstrumentTrack (s, { clip (ClipKind::pattern, 0, 384) }, 5, false));
    CHECK (lynx::pickerdrop::place (s, { clip (ClipKind::pattern, 0, 384) }, 5, 0, false, false).at (0).track == 5);
}

TEST (pickerdrop190hm_instrument_track_question)
{
    using namespace pickerdrop190hm;
    using lynx::ClipKind;
    auto s = song();
    lynx::Clip used;
    used.track = 6; // track 6 holds a clip, so the next empty track after 5 is 7
    s.playlist.push_back (used);
    const std::vector<lynx::Clip> audio { clip (ClipKind::audio, 1, 96), clip (ClipKind::audio, 2, 96), clip (ClipKind::audio, 3, 96) };
    // from track 4: the second one would land on instrument track 5 -> FL asks
    CHECK (lynx::pickerdrop::asksAboutInstrumentTrack (s, audio, 4, false));
    CHECK (! lynx::pickerdrop::asksAboutInstrumentTrack (s, audio, 6, false));
    // No: the drop stops there (the first one stays placed)
    const auto no = lynx::pickerdrop::place (s, audio, 4, 0, false, false);
    CHECK (no.size() == 1 && no[0].track == 4);
    // Yes: it moves to the next empty track (7) and the later ones follow from there
    const auto yes = lynx::pickerdrop::place (s, audio, 4, 0, false, true);
    CHECK (yes.size() == 3);
    CHECK (yes[0].track == 4 && yes[1].track == 7 && yes[2].track == 8);
    // automation clips are asked about too
    CHECK (lynx::pickerdrop::asksAboutInstrumentTrack (s, { clip (ClipKind::automation, 0, 1536) }, 5, false));
    // the next empty track skips used, audio and instrument tracks
    CHECK (lynx::pickerdrop::nextEmptyTrack (s, 3) == 4);
    CHECK (lynx::pickerdrop::nextEmptyTrack (s, 5) == 7);
}

TEST (pickerdrop190hm_shift_drop_chains_on_one_track)
{
    using namespace pickerdrop190hm;
    using lynx::ClipKind;
    auto s = song();
    const std::vector<lynx::Clip> patterns { clip (ClipKind::pattern, 0, 384), clip (ClipKind::pattern, 1, 1536), clip (ClipKind::pattern, 2, 96) };
    const auto placed = lynx::pickerdrop::place (s, patterns, 1, 768, true, false);
    CHECK (placed.size() == 3);
    CHECK (placed[0].track == 1 && placed[0].start == 768);
    CHECK (placed[1].track == 1 && placed[1].start == 768 + 384);
    CHECK (placed[2].track == 1 && placed[2].start == 768 + 384 + 1536);
    // with Shift FL checks no audio track: patterns land on it
    CHECK (lynx::pickerdrop::place (s, patterns, 3, 0, true, false).size() == 3);
    // audio clips on an instrument track: the question; No = nothing, Yes = all on the next empty track
    const std::vector<lynx::Clip> audio { clip (ClipKind::audio, 1, 96), clip (ClipKind::audio, 2, 192) };
    CHECK (lynx::pickerdrop::asksAboutInstrumentTrack (s, audio, 5, true));
    CHECK (lynx::pickerdrop::place (s, audio, 5, 0, true, false).empty());
    const auto yes = lynx::pickerdrop::place (s, audio, 5, 0, true, true);
    CHECK (yes.size() == 2 && yes[0].track == 6 && yes[1].track == 6 && yes[1].start == 96);
}

TEST (pickerdrop190hm_track_limit)
{
    using namespace pickerdrop190hm;
    using lynx::ClipKind;
    const auto s = song();
    // FL has 500 tracks: entries past the last one are dropped
    const auto placed = lynx::pickerdrop::place (s, { clip (ClipKind::pattern, 0, 96), clip (ClipKind::pattern, 1, 96) }, 499, 0, false, false);
    CHECK (placed.size() == 1 && placed[0].track == 499);
    CHECK (lynx::pickerdrop::place (s, { clip (ClipKind::pattern, 0, 96) }, 500, 0, false, false).empty());
}
