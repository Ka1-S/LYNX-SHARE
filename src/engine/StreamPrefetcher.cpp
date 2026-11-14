#include "engine/StreamPrefetcher.h"

#include "platform/PowerThrottling.h"

namespace lynx
{
StreamPrefetcher::StreamPrefetcher (const Transport& t)
    : juce::Thread ("LYNX prefetch"),
      transport (t)
{
    startThread (juce::Thread::Priority::high);
}

StreamPrefetcher::~StreamPrefetcher()
{
    stopThread (2000);
}

void StreamPrefetcher::update (const Song& song, SampleLibrary& samples)
{
    std::vector<Entry> next;
    const auto secondsPerTick = 60.0 / (juce::jmax (1.0, song.bpm) * ppq);

    for (const auto& c : song.playlist)
    {
        if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size())
            continue;

        // only what the rack already holds: a streamed file is held by its channel; never decode here (round 149)
        auto sample = samples.findLoaded (juce::String::fromUTF8 (song.channels[(std::size_t) c.channel].samplePath.c_str()));

        if (sample != nullptr && sample->isMapped())
            next.push_back ({ std::move (sample), (double) c.start, (double) c.end(), (double) c.offset * secondsPerTick });
    }

    const juce::ScopedLock sl (lock);
    entries = std::move (next);
    songLength = (double) juce::jmax<std::int64_t> (1, song.songLength());
    bpm = song.bpm;
    songMode = song.mode == PlayMode::song;
}

void StreamPrefetcher::run()
{
    power::optOutOfPowerThrottling(); // (it feeds the audio thread: no EcoQoS in the background, round toolchain190gt)
    while (! threadShouldExit())
    {
        {
            const juce::ScopedLock sl (lock);

            if (songMode && ! entries.empty())
            {
                const auto ticksPerSecond = juce::jmax (1.0, bpm) * ppq / 60.0;
                const auto now = std::fmod (juce::jmax (0.0, transport.positionTicks.load()), songLength);
                const auto ahead = now + lookaheadSeconds * ticksPerSecond;

                for (const auto& e : entries)
                {
                    // part of the clip between now and a few seconds from now (the song may wrap around)
                    for (const auto base : { 0.0, songLength })
                    {
                        const auto from = juce::jmax (e.start + base, now);
                        const auto to = juce::jmin (e.end + base, ahead);

                        if (from < to)
                            prefetchSample (*e.sample, e.offsetSeconds + (from - e.start - base) / ticksPerSecond,
                                            e.offsetSeconds + (to - e.start - base) / ticksPerSecond);
                    }
                }
            }
        }

        wait (100);
    }
}
} // namespace lynx
