// Round 170f (issue #60): FL's legacy Pattern Blocks — one row per pattern, a click places a whole instance of the row's
// pattern on a bar; blocks play, export, save and undo like clips (docs/analise/REA_rodada170f.md)

#include "core/PatternBlocks.h"

namespace pb170f
{
// patterns P1 (1 bar, a note per beat on channel 0), P2 (2 bars), P3 (1 bar, empty)
inline lynx::Song blockSong()
{
    lynx::Song s;
    s.channels.resize (1);
    s.patterns.resize (3);
    for (int i = 0; i < 3; ++i)
        s.patterns[(std::size_t) i].name = "P" + std::to_string (i + 1);
    for (int step : { 0, 4, 8, 12 })
        s.patterns[0].toggleStep (0, step);
    s.patterns[1].lengthTicks = 8 * lynx::ppq;
    s.patterns[1].toggleStep (0, 0);
    s.patternBlocksOn = true;
    s.mode = lynx::PlayMode::song;
    return s;
}
constexpr std::int64_t bar = 4 * lynx::ppq;

inline std::vector<int> noteOns (const lynx::Song& s, int samples)
{
    const auto tps = lynx::ticksPerSample (120.0, 48000.0); // 1 beat = 24000 samples
    lynx::EventBuffer ev;
    lynx::scheduleSong (s, 0.0, samples, tps, ev, false, nullptr, false);
    std::vector<int> ons;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on)
            ons.push_back (e.sampleOffset);
    return ons;
}
} // namespace pb170f

TEST (pb170f_click_places_whole_pattern_on_the_bar)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();

    // a click anywhere in bar 3 places P1 at bar 3's start (no pattern to choose: the row is the pattern)
    CHECK (patternblocks::place (s, 0, 2 * bar + 300));
    CHECK (s.blocks.size() == 1 && s.blocks[0].pattern == 0 && s.blocks[0].start == 2 * bar);
    // the same bar again, or inside the block: nothing (blocks never stack)
    CHECK (! patternblocks::place (s, 0, 2 * bar + 10));
    // a 2-bar pattern: a block on bar 1 covers bars 1–2, so bar 2 is taken, bar 3 is free
    CHECK (patternblocks::place (s, 1, 0));
    CHECK (! patternblocks::place (s, 1, bar + 5));
    CHECK (! patternblocks::fits (s, 1, bar));
    CHECK (patternblocks::place (s, 1, 2 * bar));
    // the length is the pattern's: never stored, never cropped
    CHECK (patternblocks::lengthOf (s, 1) == 2 * bar);
    const auto clips = patternblocks::asClips (s);
    CHECK (clips.size() == 3);
    for (const auto& c : clips)
        CHECK (c.offset == 0 && c.length == patternblocks::lengthOf (s, c.pattern) && c.track == patternblocks::blockTrack);
    // missing patterns can't be painted
    CHECK (! patternblocks::place (s, 7, 0) && ! patternblocks::place (s, -1, 0));
    // blocks are kept sorted by row, then start
    CHECK (std::is_sorted (s.blocks.begin(), s.blocks.end(), [] (const PatternBlock& a, const PatternBlock& b) {
        return a.pattern != b.pattern ? a.pattern < b.pattern : a.start < b.start;
    }));
}

TEST (pb170f_drag_paints_at_pattern_length_and_right_drag_erases)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();

    // dragging a 2-bar row over bars 1–4 paints bars 1 and 3 (each bar where a whole block fits), either direction
    CHECK (patternblocks::paintRange (s, 1, 4 * bar - 1, 0) == 2);
    CHECK (s.blocks.size() == 2 && s.blocks[0].start == 0 && s.blocks[1].start == 2 * bar);
    // a 1-bar row: every bar
    CHECK (patternblocks::paintRange (s, 0, 0, 3 * bar + 20) == 4);

    // right-click erases the block under the mouse (anywhere inside it), a right-drag every block crossed
    CHECK (patternblocks::blockAt (s, 1, 3 * bar + 7) >= 0);
    CHECK (patternblocks::erase (s, 1, 3 * bar + 7));
    CHECK (! patternblocks::erase (s, 1, 3 * bar + 7));
    CHECK (patternblocks::eraseRange (s, 0, bar + 1, 2 * bar + 1) == 2);
    int left = 0;
    for (const auto& b : s.blocks)
        left += b.pattern == 0 ? 1 : 0;
    CHECK (left == 2);
}

TEST (pb170f_blocks_play_in_song_mode_and_set_the_song_length)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();
    CHECK (patternblocks::place (s, 0, bar)); // P1 on bar 2
    CHECK (patternblocks::place (s, 1, 2 * bar)); // P2 on bars 3–4

    // the song ends at the last block's end (FL: Song mode loops at the end of the last item)
    CHECK (s.songLength() == 4 * bar);

    // what the audio engine plays: the rack's song copy holds them as plain clips
    auto state = RackState::build (s, nullptr, 48000.0, [] (const std::string&) { return std::shared_ptr<const SampleData>(); });
    CHECK (state != nullptr);
    if (state == nullptr)
        return;
    CHECK (state->song.blocks.empty() && ! state->song.patternBlocksOn);
    CHECK (state->song.playlist.size() == 2);
    const auto ons = pb170f::noteOns (state->song, 16 * 24000);
    // P1's 4 beats in bar 2 (beats 4–7), P2's single note at bar 3 (beat 8)
    CHECK (ons.size() == 5);
    CHECK (ons.size() == 5 && ons[0] == 4 * 24000 && ons[3] == 7 * 24000 && ons[4] == 8 * 24000);

    // the option off: the blocks stay in the project but don't play (and don't lengthen the song)
    s.patternBlocksOn = false;
    CHECK (! patternblocks::active (s));
    auto off = RackState::build (s, nullptr, 48000.0, [] (const std::string&) { return std::shared_ptr<const SampleData>(); });
    CHECK (off != nullptr && off->song.playlist.empty());
    CHECK (s.songLength() == bar); // an empty arrangement: the current pattern's length
}

TEST (pb170f_blocks_and_clips_play_together)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();
    Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = bar;
    s.playlist.push_back (c); // P1 as a clip on bar 1
    CHECK (patternblocks::place (s, 0, bar)); // and as a block on bar 2
    auto copy = s;
    patternblocks::expandSong (copy);
    CHECK (copy.playlist.size() == 2 && copy.blocks.empty());
    CHECK (pb170f::noteOns (copy, 8 * 24000).size() == 8);
    patternblocks::expandSong (copy); // twice is harmless
    CHECK (copy.playlist.size() == 2);
}

TEST (pb170f_a_grown_pattern_is_cut_at_its_next_block)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();
    CHECK (patternblocks::paintRange (s, 0, 0, bar + 1) == 2); // P1 on bars 1 and 2
    s.patterns[0].lengthTicks = 2 * bar;                       // P1 grows to 2 bars
    CHECK (patternblocks::playedEnd (s, s.blocks, 0) == bar);  // the first stops where the second starts
    CHECK (patternblocks::playedEnd (s, s.blocks, 1) == 3 * bar);
    const auto clips = patternblocks::asClips (s);
    CHECK (clips.size() == 2 && clips[0].length == bar && clips[1].length == 2 * bar);
    CHECK (patternblocks::blockAt (s, 0, bar + 3) == 1);
    CHECK (s.songLength() == 3 * bar);
}

TEST (pb170f_bars_follow_time_signature_changes)
{
    using namespace lynx;
    auto s = pb170f::blockSong();
    // 3/4 from bar 2 (tick 4 beats): bar 2 = [4, 7) beats, bar 3 = [7, 10)
    Marker m;
    m.tick = 4 * ppq;
    m.numerator = 3;
    m.denominator = 4;
    s.addMarker (m);
    s.patterns[2].lengthTicks = 3 * ppq; // P3: a 3-beat pattern
    CHECK (patternblocks::place (s, 2, 8 * ppq)); // inside bar 3
    CHECK (s.blocks.back().start == 7 * ppq);
    CHECK (patternblocks::paintRange (s, 2, 4 * ppq, 6 * ppq) == 1);
    CHECK (s.blocks.size() == 2 && s.blocks[0].start == 4 * ppq);
}

TEST (pb170f_pattern_list_edits_keep_blocks_on_their_pattern)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();
    s.addArrangement ("Arrangement 2");
    CHECK (patternblocks::place (s, 2, 0)); // P3 in arrangement 2
    s.selectArrangement (0);
    CHECK (s.blocks.empty() && s.arrangements[1].blocks.size() == 1);
    CHECK (patternblocks::place (s, 1, 0)); // P2 in arrangement 1
    CHECK (patternblocks::place (s, 2, bar));

    // a pattern inserted before them: the rows follow their pattern (every arrangement)
    insertPattern (s, 0, "New");
    CHECK (s.blocks.size() == 2 && s.blocks[0].pattern == 2 && s.blocks[1].pattern == 3);
    CHECK (s.arrangements[1].blocks[0].pattern == 3);
    // moved: swapped with its neighbour
    CHECK (movePattern (s, 2, 1));
    CHECK (s.blocks[0].pattern == 3 || s.blocks[1].pattern == 3);
    // deleted: its blocks go, later rows move up
    s.removePattern (3); // P2 (now at 3)
    CHECK (s.blocks.size() == 1 && s.blocks[0].pattern == 2 && s.blocks[0].start == bar);
    CHECK (s.arrangements[1].blocks.size() == 1 && s.arrangements[1].blocks[0].pattern == 2);
    // a cloned pattern lands after its source and shifts the rows below it
    clonePattern (s, 0);
    CHECK (s.blocks[0].pattern == 3);

    // switching arrangements swaps the blocks like the clips
    s.selectArrangement (1);
    CHECK (s.blocks.size() == 1 && s.blocks[0].start == 0);
    s.selectArrangement (0);
    CHECK (s.blocks.size() == 1 && s.blocks[0].start == bar);
}

TEST (pb170f_turn_blocks_into_clips_one_locked_track_per_pattern)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();
    Clip c;
    c.pattern = 2;
    c.track = 1;
    c.length = bar;
    s.playlist.push_back (c); // a clip on track 2 already
    CHECK (patternblocks::paintRange (s, 0, 0, bar + 1) == 2);
    CHECK (patternblocks::place (s, 1, 2 * bar));
    const auto before = s;

    CHECK (patternblocks::toClips (s) == 3);
    CHECK (s.blocks.empty() && s.patternBlocksOn); // the option stays
    int onTrack2 = 0, onTrack3 = 0;
    for (const auto& clip : s.playlist)
    {
        if (clip.track == 2)
            ++onTrack2, CHECK (clip.pattern == 0 && clip.length == bar);
        if (clip.track == 3)
            ++onTrack3, CHECK (clip.pattern == 1 && clip.start == 2 * bar && clip.length == 2 * bar);
    }
    CHECK (onTrack2 == 2 && onTrack3 == 1);
    CHECK (s.playlistTracks.size() >= 4 && s.playlistTracks[2].name == "P1" && s.playlistTracks[3].name == "P2");
    CHECK (s.playlistTracks[2].lockedToContent && s.playlistTracks[3].lockedToContent);
    // it sounds the same
    auto a = before, b = s;
    patternblocks::expandSong (a);
    patternblocks::expandSong (b);
    CHECK (pb170f::noteOns (a, 16 * 24000) == pb170f::noteOns (b, 16 * 24000));
    CHECK (std::string (describeChange (before, s).text) == "Turn pattern blocks into clips");
    // the option going off with blocks in two arrangements: each arrangement's blocks become its own clips
    auto two = pb170f::blockSong();
    CHECK (patternblocks::place (two, 0, 0));
    two.addArrangement ("B");
    CHECK (patternblocks::place (two, 1, bar));
    CHECK (patternblocks::toClipsInEveryArrangement (two) == 2);
    CHECK (two.currentArrangement == 1 && two.blocks.empty() && two.playlist.size() == 1 && two.playlist[0].pattern == 1);
    two.selectArrangement (0);
    CHECK (two.blocks.empty() && two.playlist.size() == 1 && two.playlist[0].pattern == 0 && two.playlist[0].track == 0);
}

TEST (pb170f_export_renders_blocks)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();
    CHECK (patternblocks::place (s, 1, bar));
    std::int64_t music = 0;
    const auto out = makeExportSong (s, true, 0, music);
    CHECK (music == 3 * bar); // the whole song: up to the block's end
    CHECK (out.playlist.size() == 1 && out.playlist[0].pattern == 1 && out.playlist[0].start == bar && out.blocks.empty());
    // the MIDI export and the stems see them too
    const auto notes = exportopts::midiExportNotes (s);
    CHECK (notes.size() == 1 && notes.size() == 1 && notes[0].start == bar);
    CHECK (exportopts::stemChannels (s, exportopts::Range::song) == std::vector<int> { 0 });
}

TEST (pb170f_blocks_undo_labels_and_arrangement_copies)
{
    using namespace lynx;
    using pb170f::bar;
    auto s = pb170f::blockSong();
    s.patterns[2].toggleStep (0, 0);
    const auto empty = s;
    CHECK (patternblocks::paintRange (s, 1, 0, 3 * bar) == 2);
    CHECK (std::string (describeChange (empty, s).text) == "Paint pattern blocks");
    auto erased = s;
    CHECK (patternblocks::erase (erased, 1, 0));
    CHECK (std::string (describeChange (s, erased).text) == "Erase pattern blocks");
    auto off = s;
    off.patternBlocksOn = false;
    CHECK (std::string (describeChange (s, off).text) == "Pattern blocks");
    s.addArrangement ("Second", true); // a copy of the current arrangement holds the blocks too
    CHECK (s.blocks.size() == 2);
    CHECK (patternblocks::place (s, 2, 5 * bar));
    s.selectArrangement (0);

    // the option and every arrangement's blocks are saved in .lynx (LYNX.exe --selftest: pattern_blocks=OK)
    CHECK (s.arrangements[1].blocks.size() == 3);
}

TEST (pb170f_key_map_actions)
{
    using namespace lynx;
    CHECK (keys::findAction ("playlist.patternBlocks") != nullptr);
    CHECK (keys::findAction ("playlist.blocksToClips") != nullptr);
    keys::KeyMap map;
    CHECK (map.conflicts().empty());
}
