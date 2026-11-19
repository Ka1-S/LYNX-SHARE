#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

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
        raise (t.activity[0], left);
        raise (t.activity[1], right);
        if (track == 0) // (round 190jj: the Simple top bar's master meter reads the Master only)
        {
            raise (masterBar[0], left);
            raise (masterBar[1], right);
        }
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

    // A fourth reader (round 190im: the Playlist's activity strips, FL's lit column beside the track headers, whose
    // brightness follows the level), so it takes no peaks from the Mixer, the headers or the Session mixer.
    float takeActivityPeak (int track, int channel) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return 0.0f;

        return tracks[(std::size_t) track].activity[channel & 1].exchange (0.0f, std::memory_order_relaxed);
    }

    // Round 190lr (DL-108, Live's mixer "shows both peak and RMS output levels"): the mean square of the samples since the
    // last read, for the Session mixer's RMS bars. The audio thread adds the blocks' sums of squares only while a reader
    // wants them (`powerWanted`, set while Live's look of the Session mixer shows), so nothing is summed otherwise.
    std::atomic<bool> powerWanted { false };
    // (UI thread) when the reader last took them; the main window's timer clears powerWanted a second after the last
    // read, so a hidden Session mixer leaves nothing summed
    std::uint32_t powerReadMs = 0;
    void expirePower (std::uint32_t nowMs) noexcept
    {
        if (powerWanted.load (std::memory_order_relaxed) && nowMs - powerReadMs > 1000u)
            powerWanted.store (false, std::memory_order_relaxed);
    }
    void accumulatePower (int track, float sumLeft, float sumRight, int frames) noexcept
    {
        if (track < 0 || track >= maxMixerTracks || frames <= 0)
            return;
        auto& t = tracks[(std::size_t) track];
        addTo (t.power[0], sumLeft);
        addTo (t.power[1], sumRight);
        t.powerFrames.fetch_add (frames, std::memory_order_relaxed);
    }
    // the mean square of each side since the last read (false: no samples came)
    bool takeSessionPower (int track, float& left, float& right) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return false;
        auto& t = tracks[(std::size_t) track];
        const auto frames = t.powerFrames.exchange (0, std::memory_order_relaxed);
        const auto l = t.power[0].exchange (0.0f, std::memory_order_relaxed), r = t.power[1].exchange (0.0f, std::memory_order_relaxed);
        if (frames <= 0)
            return false;
        left = std::max (0.0f, l) / (float) frames;
        right = std::max (0.0f, r) / (float) frames;
        return true;
    }
    // the sums themselves, for the render-ahead's replay (it keeps them per block and adds them back in time)
    bool takePowerSums (int track, float& left, float& right, int& frames) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return false;
        auto& t = tracks[(std::size_t) track];
        frames = t.powerFrames.exchange (0, std::memory_order_relaxed);
        left = t.power[0].exchange (0.0f, std::memory_order_relaxed);
        right = t.power[1].exchange (0.0f, std::memory_order_relaxed);
        return frames > 0;
    }

    // A fifth reader, of the Master only (round 190jj: BandLab's master meter behind the master volume in the Simple top
    // bar), so it takes no peaks from the Mixer's Master strip.
    float takeMasterBarPeak (int channel) noexcept { return masterBar[(std::size_t) (channel & 1)].exchange (0.0f, std::memory_order_relaxed); }

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
    static void addTo (std::atomic<float>& slot, float value) noexcept
    {
        auto current = slot.load (std::memory_order_relaxed);
        while (! slot.compare_exchange_weak (current, current + value, std::memory_order_relaxed))
        {
        }
    }
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
        std::array<std::atomic<float>, 2> peak {}, view {}, session {}, activity {};
        std::array<std::atomic<float>, 2> power {}; // (round 190lr: sums of squares, only while powerWanted)
        std::atomic<int> powerFrames { 0 };
    };
    std::array<TrackPeaks, maxMixerTracks> tracks {};
    std::array<std::atomic<float>, 2> masterBar {};
};
} // namespace lynx
