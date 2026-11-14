// Rollup 0.1.28 (round 190fv): what two of the merged branches do together.
// - follow190fg gave the song a meter with a denominator (Song::beatUnit, Meter; Pattern::effectiveLength takes a
//   Meter, a bar of 6/8 = 3 quarters). livefreeze190ff's Consolidate and reabl190fo's Simple Sampler track were written
//   against the numerator alone (a bar = beatsPerBar quarters); the rollup moved both to the song's meter.
// - livefreeze190ff's track freeze holds an insert number (FreezeState::insert); reafl190fl's "Increase number of
//   tracks to..." adds inserts: they go at the end, so a frozen track keeps its insert and stays frozen.

#include <tuple>

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

// Step 2 (realive190fm): Playlist clips play through grooves of the pool, and Live's MIDI Generate tab writes bars.
// - livefreeze190ff's Consolidate makes one new clip without a groove: the clips' grooves are written into its notes
//   (moved and accented as the Scheduler plays them), so the consolidated clip plays what the clips played.
// - MIDI Generate's Stacks durations are bars: of the song's meter (follow190fg), not of its numerator.
TEST (rollup028_consolidate_writes_a_clips_groove_into_the_notes)
{
    using namespace lynx;
    auto s = groove190fm::song();
    s.playlist[1].track = 0; // both clips on one track: bar 1 (with the groove) and bar 3 (straight)
    auto g = groove190fm::offbeat (60);
    g.velocity = 1.0f;
    const auto id = groove::add (s.grooves, g);
    CHECK (groove::setPlaylistClipGroove (s, { 0 }, id));
    const auto before = groove190fm::play (s);
    freeze::ConsolidatePlan plan;
    CHECK (freeze::planConsolidate (s, { 0, 1 }, plan) == nullptr && ! plan.audio);
    const auto merged = freeze::consolidatePatternClips (s, plan);
    CHECK (merged >= 0 && s.playlist.size() == 1 && s.playlist[0].groove == 0);
    const auto after = groove190fm::play (s);
    CHECK (before.size() == after.size() && before.size() == 16);
    if (before.size() != after.size())
        return;
    auto sorted = [] (std::vector<groove190fm::Heard> h) {
        std::stable_sort (h.begin(), h.end(), [] (const auto& a, const auto& b) { return std::tie (a.tick, a.type, a.key) < std::tie (b.tick, b.type, b.key); });
        return h;
    };
    const auto a = sorted (before), b = sorted (after);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHECK (a[i].key == b[i].key && a[i].type == b[i].type);
        CHECK (std::abs (a[i].tick - b[i].tick) < 1.0);
        CHECK (std::abs (a[i].velocity - b[i].velocity) < 1.0e-6f);
    }
    // the grooved off-beat 16th of bar 1: 60 ticks late and at half the velocity, now in the notes themselves
    const auto on61 = groove190fm::onsOf (after, 61);
    CHECK (on61.size() == 2 && std::abs (on61[0] - 300.0) < 1.0 && std::abs (on61[1] - (2.0 * groove190fm::bar + 240.0)) < 1.0);
}

TEST (rollup028_midi_generate_stacks_count_bars_of_the_song_meter)
{
    using namespace lynx::midigen;
    const auto& bank = lynxChordBank();
    StacksOptions o; // one chord of one bar
    auto c = mg190fm::bar (2);
    c.beatsPerBar = 6;
    c.beatUnit = 8;
    c.to = 2 * 3 * lynx::ppq; // two bars of 6/8
    const auto out = stacks (o, c, bank);
    CHECK (out.size() == 6);
    if (out.size() == 6)
        CHECK (out[0].length == 3 * lynx::ppq && out[3].start == 3 * lynx::ppq);
    // n/4 as before
    const auto four = stacks (o, mg190fm::bar (2), bank);
    CHECK (four.size() == 6 && four[0].length == 4 * lynx::ppq);
}

// Step 2 (ember190fj): Ember's Tape runs a measured magnetic core in new instances (Law = Measured). The Live imports
// that build Ember stages on fits made on the Classic tape keep it there, as 190fj did for Saturator: realive190fm's
// Roar (Polynomial = the tape style) and drumbus190fk's round-190ek Drum Buss rack.
TEST (rollup028_live_imports_keep_embers_classic_tape)
{
    using namespace lynx;
    using namespace lynx::alsdev;
    using als190fm::par;
    namespace em = mixsuite::ember;
    const auto t = als190fm::translate (als190fm::roar (1, par ("Stage1_Shaper_Type", 7) + par ("Stage2_Shaper_Type", 7)));
    const auto* r = t ? als190fm::rackOf (*t) : nullptr;
    CHECK (r != nullptr);
    int embers = 0;
    if (r != nullptr)
        for (const auto& node : t->slot.patcher.nodes)
            if (node.effect.kind == EffectKind::ember)
            {
                ++embers;
                CHECK (em::styleOf (node.effect.params[em::style]) == em::Style::tape);
                CHECK (node.effect.params[(std::size_t) em::law] == 0.0f && ! em::measuredTape (node.effect.params));
            }
    CHECK (embers >= 1);
    // a new Ember in the Tape style is the measured one (190fj's default), so the pin above is what keeps the import
    auto fresh = makeEffectSlot (EffectKind::ember);
    fresh.params[em::style] = 1.0f / (float) (em::styles - 1);
    CHECK (em::measuredTape (fresh.params));
}
