#pragma once

#include <array>
#include <atomic>

#include "core/AudioCapture.h"
#include "core/MasterLoudness.h"
#include "core/RollingAudioLog.h"
#include "core/Song.h"

namespace lynx
{
// Peaks per mixer track, written by the audio thread and read by the UI.
// Audio accumulates the maximum; the UI reads and resets (takePeak), so no peak is lost
// between two screen frames.
class MeterBank
{
public:
    void accumulate (int track, float left, float right) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return;

        auto& t = tracks[(std::size_t) track];
        raise (t.peak[0], left);
        raise (t.peak[1], right);
        raise (t.view[0], left);
        raise (t.view[1], right);
        raise (t.session[0], left);
        raise (t.session[1], right);
    }

    // UI: peak since the last read. channel 0 = left, 1 = right.
    float takePeak (int track, int channel) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return 0.0f;

        return tracks[(std::size_t) track].peak[channel & 1].exchange (0.0f, std::memory_order_relaxed);
    }

    // The same peaks for a second reader (round 190w: the Playlist track headers' levels, FL's "Show levels on audio
    // tracks"), so the Mixer's meters and the headers never take each other's peaks.
    float takeViewPeak (int track, int channel) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return 0.0f;

        return tracks[(std::size_t) track].view[channel & 1].exchange (0.0f, std::memory_order_relaxed);
    }

    // A third reader (round 190dr: the Session mixer's meters under the clip grid), so it takes no peaks from the Mixer
    // or the Playlist headers.
    float takeSessionPeak (int track, int channel) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return 0.0f;

        return tracks[(std::size_t) track].session[channel & 1].exchange (0.0f, std::memory_order_relaxed);
    }

    // the audio editor's recording tap (round 86): input, Master (0) or a mixer insert
    AudioCapture capture;
    // the audio logger's tap (REA round 153): the Master, while Tools > Audio logger is on
    AudioCapture logTap;
    // the Audio logger's memory (round 181b): the last minutes of the Master, pre or post effects
    RollingAudioLog rollingLog;
    // the real-time bounce's tap (round 165c): the Master while the song plays, during Export › "Render through the
    // hardware in real time"
    AudioCapture bounceTap;
    // the Master's loudness readout (round 190am): Mixer menu > "Loudness meter beside the Master", off by default
    MasterLoudness loudness;

private:
    static void raise (std::atomic<float>& slot, float value) noexcept
    {
        auto current = slot.load (std::memory_order_relaxed);

        while (value > current && ! slot.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }

    // one cache line per track (round 190gq, 190gs Q5): the inserts of a mixer layer are metered from different
    // workers, and 8 tracks shared a line before; the three readers' peaks of a track are written together
    struct alignas (64) TrackPeaks
    {
        std::array<std::atomic<float>, 2> peak {}, view {}, session {};
    };
    std::array<TrackPeaks, maxMixerTracks> tracks {};
};
} // namespace lynx
