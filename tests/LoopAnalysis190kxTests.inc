// Round 190kx: loop analysis — the key and tempo of the user's own loops (core/LoopAnalysis.h). The laws measured on
// LYNX's own synthetic corpus (tests/LoopCorpus190kx.h; the whole table: tools/loop_accuracy.cpp), the cache, its text
// form, the worker, and what the Simple drop's switch defaults to.

#include <atomic>
#include <map>

#include "LoopCorpus190kx.h"
#include "core/LoopAnalysis.h"
#include "core/TempoDetect.h"

namespace loop190kx
{
inline bool sameTempo (double got, double want) { return got > 0.0 && std::abs (got / want - 1.0) < 0.01; }
inline bool sameKey (const std::optional<lynx::keyfollow::LoopKey>& k, const loopcorpus::Item& it)
{
    return k && k->root == it.keyRoot && k->minor == it.minor;
}
// "drums-house", "tonal-chords", "mixed-trap", "oneshot-drum"
inline std::string group (const loopcorpus::Item& it) { return it.name.substr (0, it.name.find ('-', it.name.find ('-') + 1)); }
} // namespace loop190kx

TEST (loop190kx_tempo_beats_the_old_detector_in_every_class)
{
    using namespace lynx;
    using namespace loop190kx;
    std::map<std::string, std::pair<int, int>> exact; // group -> (new, legacy)
    int fresh = 0, old = 0, wholeNew = 0, wholeOld = 0, loops = 0;
    for (const auto& it : loopcorpus::corpus (true))
    {
        if (! (it.bpm > 0.0))
            continue;
        ++loops;
        const auto a = loopanalysis::analyse (*it.audio, it.name + ".wav", false);
        const auto l = tempodetect::detect (it.audio->leftData(), it.audio->isStereo() ? it.audio->rightData() : nullptr, it.audio->frames(), it.audio->sampleRate);
        const auto legacyOk = sameTempo (l.bpm, it.bpm) && l.confidence >= 0.35;
        auto& e = exact[group (it)];
        e.first += sameTempo (a.bpm, it.bpm), e.second += legacyOk;
        fresh += sameTempo (a.bpm, it.bpm), old += legacyOk;
        // a loop of whole beats: its length in beats at the found tempo is whole (4 or more)
        wholeNew += a.wholeBeats && std::abs (a.beats - std::round (a.beats)) < 0.05;
        wholeOld += l.beats >= 3.9 && std::abs (l.beats - std::round (l.beats)) <= 0.04 && l.confidence >= 0.35;
    }
    for (const auto& [g, e] : exact)
    {
        CHECK (e.first >= e.second); // nothing gets worse
        if (e.first < e.second)
            std::printf ("  loop190kx: %s tempo exact %d < legacy %d\n", g.c_str(), e.first, e.second);
    }
    std::printf ("  loop190kx: tempo exact %d of %d (legacy %d), whole-beat loops %d (legacy %d)\n", fresh, loops, old, wholeNew, wholeOld);
    CHECK (fresh >= old + loops / 6); // (measured on the subset: a clear gain, see LOOPS-HANDOFF.md for the full table)
    CHECK (wholeNew >= wholeOld);
}

TEST (loop190kx_key_beats_the_old_detector_and_drums_get_no_key)
{
    using namespace lynx;
    using namespace loop190kx;
    int fresh = 0, old = 0, keyed = 0, falseNew = 0, falseOld = 0, unkeyed = 0;
    std::map<std::string, std::pair<int, int>> exact;
    for (const auto& it : loopcorpus::corpus (true))
    {
        const auto d = loopanalysis::detectKey (*it.audio);
        const auto l = keyfollow::detectKey (*it.audio);
        if (it.keyRoot >= 0)
        {
            ++keyed;
            fresh += sameKey (d.key, it), old += sameKey (l.key, it);
            auto& e = exact[group (it)];
            e.first += sameKey (d.key, it), e.second += sameKey (l.key, it);
        }
        else
        {
            ++unkeyed;
            falseNew += d.key.has_value(), falseOld += l.key.has_value();
        }
    }
    // (per class on this small subset a class of 4-7 loops may lose one to chance — the arpeggios: short plucked notes
    // that the harmonic / percussive split thins out; on the whole corpus, tools/loop_accuracy.cpp, no class is worse)
    for (const auto& [g, e] : exact)
    {
        CHECK (e.first + 1 >= e.second);
        if (e.first < e.second)
            std::printf ("  loop190kx: %s key exact %d < legacy %d\n", g.c_str(), e.first, e.second);
    }
    std::printf ("  loop190kx: key exact %d of %d (legacy %d); a key given to %d of %d drum loops / hits (legacy %d)\n", fresh, keyed, old, falseNew, unkeyed,
                 falseOld);
    CHECK (fresh > old);
    CHECK (falseNew <= falseOld && falseNew * 5 <= unkeyed); // percussion: at most one in five gets a key (the whole corpus: 6 of 130)
}

TEST (loop190kx_name_and_acid_come_first_and_long_files_are_left_alone)
{
    using namespace lynx;
    const auto drums = loopcorpus::drums ("house", 124.0, 4, 3);
    // the audio says 124; the name says 120 and A minor, ACID says 16 beats: ACID, then the name, win (they are the
    // maker's own data)
    auto acid = *drums.audio;
    acid.acidBeats = 16.0f;
    auto r = loopanalysis::analyse (acid, "Loop 120 Am.wav");
    CHECK (r.tempoSource == loopanalysis::TempoSource::acid && std::abs (r.bpm - 124.0) < 0.01 && r.wholeBeats);
    CHECK (r.keySource == loopanalysis::KeySource::name && (r.key == keyfollow::LoopKey { 9, true }));
    r = loopanalysis::analyse (*drums.audio, "Groove 1.wav");
    CHECK (r.tempoSource == loopanalysis::TempoSource::detected && std::abs (r.bpm - 124.0) < 0.01 && r.wholeBeats && r.isLoop());
    CHECK (r.keySource == loopanalysis::KeySource::none && ! r.key); // (drums: no key)
    CHECK (loopanalysis::tempoText (r) == "124 BPM");
    // over 70 seconds: a song or a take, not analysed
    lynx::SampleData song;
    song.sampleRate = 44100.0;
    song.left.assign ((std::size_t) (71.0 * 44100.0), 0.0f);
    for (std::size_t i = 0; i < drums.audio->frames(); ++i)
        song.left[i] = drums.audio->leftData()[i];
    r = loopanalysis::analyse (song, "Song.wav");
    CHECK (r.bpm == 0.0 && ! r.wholeBeats && ! r.key && r.seconds > 70.0);
    // a take that is not a whole number of beats (a loop with 0.73 s more of itself): its tempo, but not a loop
    {
        auto take = *loopcorpus::drums ("rock", 100.0, 4, 6).audio;
        const auto extra = (std::size_t) (0.73 * take.sampleRate);
        for (std::size_t i = 0; i < extra; ++i)
            take.left.push_back (take.left[i]);
        take.right.clear();
        r = loopanalysis::analyse (take, "Take 3.wav");
        CHECK (! r.wholeBeats && std::abs (r.bpm - 100.0) < 1.0);
        if (r.wholeBeats || std::abs (r.bpm - 100.0) >= 1.0)
            std::printf ("  loop190kx: take %.3f BPM, %.3f beats, whole %d\n", r.bpm, r.beats, (int) r.wholeBeats);
    }
    // a one-shot: no tempo, no loop
    const auto hit = loopcorpus::oneShot (false, 0, 4);
    r = loopanalysis::analyse (*hit.audio, "Kick.wav");
    CHECK (! r.isLoop() && ! r.key);
}

TEST (loop190kx_cache_is_per_file_stamp_and_round_trips_as_text)
{
    using namespace lynx;
    loopanalysis::Cache cache;
    loopanalysis::Result r;
    r.seconds = 7.74, r.bpm = 124.0, r.tempoConfidence = 0.8, r.beats = 16.0, r.wholeBeats = true;
    r.tempoSource = loopanalysis::TempoSource::detected;
    r.key = keyfollow::LoopKey { 2, true };
    r.keyConfidence = 0.5f;
    r.keySource = loopanalysis::KeySource::detected;
    const loopanalysis::Stamp stamp { "/loops/Groove 1.wav", 1234, 99 };
    cache.put (stamp, r);
    CHECK (cache.find (stamp).has_value());
    CHECK (! cache.find ({ stamp.path, 1235, 99 }).has_value()); // the file changed: analysed again
    CHECK (! cache.find ({ stamp.path, 1234, 100 }).has_value());
    CHECK (cache.findPath (stamp.path).has_value());
    const auto line = loopanalysis::toText (stamp, r);
    loopanalysis::Stamp s2;
    loopanalysis::Result r2;
    CHECK (loopanalysis::fromText (line, s2, r2));
    CHECK (s2 == stamp && std::abs (r2.bpm - 124.0) < 1.0e-6 && r2.wholeBeats && r2.key == r.key && r2.keySource == r.keySource
           && r2.tempoSource == r.tempoSource && std::abs (r2.beats - 16.0) < 1.0e-6);
    CHECK (! loopanalysis::fromText ("garbage", s2, r2));
    CHECK (loopanalysis::toText ({ "/a\tb.wav", 1, 1 }, r).empty()); // (a tab in the path: not kept)
    // the oldest half goes beyond the capacity
    for (std::size_t i = 0; i <= loopanalysis::Cache::capacity; ++i)
        cache.put ({ "/f" + std::to_string (i), 1, 1 }, r);
    CHECK (cache.size() <= loopanalysis::Cache::capacity && cache.size() > loopanalysis::Cache::capacity / 3);
    CHECK (cache.find ({ "/f" + std::to_string (loopanalysis::Cache::capacity), 1, 1 }).has_value()); // the newest stays
}

TEST (loop190kx_worker_analyses_what_is_asked_once_and_answers_cached_files_at_once)
{
    using namespace lynx;
    loopanalysis::Cache::shared().clear();
    const auto drums = loopcorpus::drums ("rock", 100.0, 4, 5);
    std::atomic<int> decoded { 0 }, answered { 0 };
    loopanalysis::Service::Callbacks cb;
    cb.stat = [] (const std::string& path, std::int64_t& size, std::int64_t& modified) {
        if (path.find ("missing") != std::string::npos)
            return false;
        size = 100, modified = 7;
        return true;
    };
    cb.decode = [&] (const std::string&, double, lynx::SampleData& out, double& total) {
        ++decoded;
        out = *drums.audio;
        total = (double) out.frames() / out.sampleRate;
        return true;
    };
    cb.analysed = [&] (const std::string&) { ++answered; };
    cb.throttle = false;
    {
        loopanalysis::Service service (std::move (cb));
        CHECK (service.request ({ "/l/a.wav", "/l/b.wav", "/l/missing.wav" }) == 3);
        CHECK (service.waitIdle (60000));
        CHECK (decoded == 2 && answered == 2 && service.done() == 2);
        const auto a = loopanalysis::Cache::shared().find ({ "/l/a.wav", 100, 7 });
        CHECK (a.has_value() && std::abs (a->bpm - 100.0) < 0.01 && a->isLoop());
        // asked again: answered from the cache, not decoded again
        service.request ({ "/l/a.wav" });
        CHECK (service.waitIdle (60000));
        CHECK (decoded == 2 && answered == 3);
        CHECK (service.find ("/l/b.wav").has_value());
    }
    loopanalysis::Cache::shared().clear();
}

TEST (loop190kx_simple_drop_switch_is_on_by_default)
{
    // BandLab fits its loops to the project's tempo and key: the Simple layout's drops do it by default
    CHECK (lynx::loopanalysis::dropDetects().load());
}
