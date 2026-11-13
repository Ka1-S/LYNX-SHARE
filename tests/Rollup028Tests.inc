// Rollup 0.1.28 (round 190fv): what two of the merged branches do together.
// - follow190fg gave the song a meter with a denominator (Song::beatUnit, Meter; Pattern::effectiveLength takes a
//   Meter, a bar of 6/8 = 3 quarters). livefreeze190ff's Consolidate and reabl190fo's Simple Sampler track were written
//   against the numerator alone (a bar = beatsPerBar quarters); the rollup moved both to the song's meter.
// - livefreeze190ff's track freeze holds an insert number (FreezeState::insert); reafl190fl's "Increase number of
//   tracks to..." adds inserts: they go at the end, so a frozen track keeps its insert and stays frozen.

#include "core/MixerOps.h"
#include "core/SimplePads.h"
#include "core/SimpleTracks.h"
#include "core/StandIns.h"

namespace rollup028
{
// 6/8: six eighths, a bar of three quarters
inline lynx::Song sixEight()
{
    auto s = lynx::Song::makeDefault();
    s.beatsPerBar = 6;
    s.beatUnit = 8;
    return s;
}

struct LoopFlag
{
    explicit LoopFlag (bool on) : was (lynx::stretchedClipsLoop()) { lynx::setStretchedClipsLoop (on); }
    ~LoopFlag() { lynx::setStretchedClipsLoop (was); }
    bool was;
};
} // namespace rollup028

TEST (rollup028_consolidate_loops_patterns_in_bars_of_the_song_meter)
{
    using lynx::ppq;
    const rollup028::LoopFlag loops (true);
    auto s = rollup028::sixEight();
    CHECK (s.barTicks() == 3 * ppq);
    s.playlist.clear();
    s.playTruncatedNotes = false;
    // a pattern without its own length: two notes in its first quarter notes, so it lasts one bar of the meter
    lynx::Pattern p;
    p.lengthTicks = 0;
    for (const auto at : { (std::int64_t) 0, (std::int64_t) ppq })
    {
        lynx::Note n;
        n.start = at;
        n.length = ppq / 2;
        n.channel = 0;
        p.notes.push_back (n);
    }
    s.patterns.push_back (p);
    const auto pattern = (int) s.patterns.size() - 1;
    CHECK (s.patterns[(std::size_t) pattern].effectiveLength (s.meter()) == 3 * ppq);
    // clip A stretched over two bars (the pattern loops twice), clip B one bar later on the same track
    using Span = std::pair<std::int64_t, std::int64_t>;
    for (const auto& [start, length] : { Span { 0, 6 * ppq }, Span { 8 * ppq, 3 * ppq } })
    {
        lynx::Clip c;
        c.kind = lynx::ClipKind::pattern;
        c.pattern = pattern;
        c.track = 0;
        c.start = start;
        c.length = length;
        s.playlist.push_back (c);
    }
    lynx::freeze::ConsolidatePlan plan;
    CHECK (lynx::freeze::planConsolidate (s, { 0, 1 }, plan) == nullptr);
    CHECK (! plan.audio);
    const auto merged = lynx::freeze::consolidatePatternClips (s, plan);
    CHECK (merged >= 0 && merged < (int) s.patterns.size());
    if (merged < 0 || merged >= (int) s.patterns.size())
        return;
    const auto& m = s.patterns[(std::size_t) merged];
    CHECK (m.lengthTicks == 11 * ppq);
    // as played: the loop of 3 quarters twice (0, 1, 3, 4), then clip B (8, 9). With the numerator alone (a bar of
    // 6 quarters) clip A would have played its pattern once: 0 and 1 only.
    std::vector<std::int64_t> starts;
    for (const auto& n : m.notes)
        starts.push_back (n.start);
    const std::vector<std::int64_t> want { 0, ppq, 3 * ppq, 4 * ppq, 8 * ppq, 9 * ppq };
    CHECK (starts == want);
    CHECK (s.playlist.size() == 1 && s.playlist[0].length == 11 * ppq);
}

TEST (rollup028_simple_sampler_track_records_into_four_bars_of_the_song_meter)
{
    using lynx::ppq;
    // SimpleWorkflow::addPads (UI side) builds the 4-bar pattern from the song's bar, not from the numerator
    const auto ui = i18ntest::readFile ("src/ui/SimpleWorkflow.cpp");
    const auto at = ui.find ("bool SimpleWorkflow::addPads()");
    CHECK (at != std::string::npos);
    if (at == std::string::npos)
        return;
    const auto body = ui.substr (at, ui.find ("\n}\n", at) - at);
    CHECK (body.find ("4 * engine.getSong().barTicks()") != std::string::npos);
    CHECK (body.find ("beatsPerBar") == std::string::npos);
    // ... and the core places that pattern as one clip of four 6/8 bars
    auto s = rollup028::sixEight();
    s.playlist.clear();
    lynx::simpletracks::Request q;
    q.kind = lynx::simpletracks::Kind::pads;
    q.kit = lynx::simplepads::emptyKit();
    lynx::Pattern p;
    p.lengthTicks = 4 * s.barTicks();
    q.pattern = p;
    const auto r = lynx::simpletracks::addTrack (s, q);
    CHECK (r.pattern >= 0 && r.pattern < (int) s.patterns.size());
    CHECK (s.patterns[(std::size_t) r.pattern].effectiveLength (s.meter()) == 12 * ppq);
    bool found = false;
    for (const auto& c : s.playlist)
        if (c.kind == lynx::ClipKind::pattern && c.pattern == r.pattern)
        {
            found = true;
            CHECK (c.length == 4 * s.barTicks());
        }
    CHECK (found);
}

TEST (rollup028_more_mixer_tracks_keep_a_track_freeze)
{
    auto s = lynx::Song::makeDefault();
    CHECK (! s.channels.empty() && s.mixer.size() > 4);
    if (s.channels.empty() || s.mixer.size() <= 4)
        return;
    for (auto& c : s.channels)
        c.mixerTrack = 0;
    auto& lead = s.channels[0];
    lead.mixerTrack = 3;
    lynx::StandIn whole;
    whole.path = "lead-frozen.wav";
    whole.lengthTicks = 16 * lynx::ppq;
    lead.freeze.stems = { whole };
    lead.freeze.insert = 3;
    CHECK (lynx::freeze::trackFreezeHolds (s, 0));
    const auto before = lynx::mixerops::insertCount (s);
    CHECK (lynx::mixerops::increaseInsertsTo (s, before + 5) == 5);
    CHECK (lynx::mixerops::insertCount (s) == before + 5);
    CHECK (s.channels[0].mixerTrack == 3 && s.channels[0].freeze.insert == 3);
    CHECK (lynx::freeze::trackFreezeHolds (s, 0));
    CHECK (lynx::freeze::frozenByInsert (s, 3) == 0);
    // "Plugin delay compensation > Set for all tracks" leaves the freeze alone too
    lynx::mixerops::setPdcForAllTracks (s);
    CHECK (lynx::freeze::trackFreezeHolds (s, 0));
}
