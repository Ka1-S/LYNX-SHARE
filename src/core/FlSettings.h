#pragma once

// FL Studio's Settings options that LYNX honours since round 190bx (FLEngine 2026, the Settings form's resource and
// its option handlers; docs/analise/REA_rodada190bx.md). App-wide, as in FL: kept in ui-options.txt, not in a
// project. Each default is FL's. The values are atomics: the audio thread reads some of them.
//
//   Audio › Mixer    interpolation           "Resampling quality" (IPBox): the sampler channels' interpolation
//   Audio › CPU      multithreadGenerators   "Multithreaded generator processing"
//                    multithreadMixer        "Multithreaded mixer processing"
//                    smartDisable            "Smart disable"
//   Audio › Output   autoCloseDevice         "Auto close": the audio device is free for other programs while LYNX is
//                                            not the program in front
//   General › Misc   restoreStateAfterSolo   "Restore previous state after solo"
//                    autoNameChannels        "Auto name channels"
//   General › Adv.   readSampleTempo         "Read sample tempo information"
//   General › Undo   historyNewestFirst      "Show recent changes at the top"
//
// Header-only, no JUCE.

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace lynx::flsettings
{
// FL's interpolation index (core/FlResampler.h): 1 linear, 2 Hermite, 3…9 sinc with 16, 24, 32, 64, 128, 256, 512 points
inline constexpr int interpolationMin = 1, interpolationMax = 9, interpolationDefault = 4;
// from this index on FL warns that the method is heavy in real time (its Settings handler: item index above 4)
inline constexpr int interpolationHeavyFrom = 6;

constexpr int clampInterpolation (int index) noexcept
{
    return index < interpolationMin || index > interpolationMax ? interpolationDefault : index;
}

// the number of points of an interpolation index (2 linear, 6 Hermite, then the sinc sizes)
constexpr int interpolationPoints (int index) noexcept
{
    constexpr int points[] = { 2, 6, 16, 24, 32, 64, 128, 256, 512 };
    return points[clampInterpolation (index) - 1];
}

struct Options
{
    std::atomic<int> interpolation { interpolationDefault };
    std::atomic<bool> multithreadGenerators { true };
    std::atomic<bool> multithreadMixer { true };
    std::atomic<bool> smartDisable { true };
    std::atomic<bool> autoCloseDevice { false };
    std::atomic<bool> restoreStateAfterSolo { false };
    std::atomic<bool> autoNameChannels { true };
    std::atomic<bool> readSampleTempo { true };
    std::atomic<bool> historyNewestFirst { false };
};

inline Options& options() noexcept
{
    static Options o;
    return o;
}

// FL's defaults again (the tests; a fresh profile)
inline void reset() noexcept
{
    auto& o = options();
    o.interpolation = interpolationDefault;
    o.multithreadGenerators = true;
    o.multithreadMixer = true;
    o.smartDisable = true;
    o.autoCloseDevice = false;
    o.restoreStateAfterSolo = false;
    o.autoNameChannels = true;
    o.readSampleTempo = true;
    o.historyNewestFirst = false;
}

// --- ui-options.txt ---------------------------------------------------------------------------------------------
// One "key=value" line of the options file: true when the key is one of these options (the value is then applied;
// anything unreadable keeps FL's default).
inline bool parse (std::string_view key, std::string_view value) noexcept
{
    auto& o = options();
    const auto on = value == "1";
    const auto off = value == "0";
    auto flag = [&] (std::atomic<bool>& target, bool fallback) {
        target = on ? true : off ? false : fallback;
        return true;
    };
    if (key == "flInterpolation")
    {
        auto n = 0;
        for (const auto c : value)
        {
            if (c < '0' || c > '9' || n > 99)
            {
                n = 0;
                break;
            }
            n = n * 10 + (c - '0');
        }
        o.interpolation = clampInterpolation (n);
        return true;
    }
    if (key == "flMultithreadGenerators") return flag (o.multithreadGenerators, true);
    if (key == "flMultithreadMixer") return flag (o.multithreadMixer, true);
    if (key == "flSmartDisable") return flag (o.smartDisable, true);
    if (key == "flAutoCloseDevice") return flag (o.autoCloseDevice, false);
    if (key == "flRestoreStateAfterSolo") return flag (o.restoreStateAfterSolo, false);
    if (key == "flAutoNameChannels") return flag (o.autoNameChannels, true);
    if (key == "flReadSampleTempo") return flag (o.readSampleTempo, true);
    if (key == "flHistoryNewestFirst") return flag (o.historyNewestFirst, false);
    return false;
}

// The lines for the options file, each starting with "\n" (appended to the other options).
inline std::string serialise()
{
    const auto& o = options();
    auto flag = [] (const char* key, bool on) { return std::string ("\n") + key + "=" + (on ? "1" : "0"); };
    return "\nflInterpolation=" + std::to_string (o.interpolation.load())
           + flag ("flMultithreadGenerators", o.multithreadGenerators.load())
           + flag ("flMultithreadMixer", o.multithreadMixer.load())
           + flag ("flSmartDisable", o.smartDisable.load())
           + flag ("flAutoCloseDevice", o.autoCloseDevice.load())
           + flag ("flRestoreStateAfterSolo", o.restoreStateAfterSolo.load())
           + flag ("flAutoNameChannels", o.autoNameChannels.load())
           + flag ("flReadSampleTempo", o.readSampleTempo.load())
           + flag ("flHistoryNewestFirst", o.historyNewestFirst.load());
}

// --- Audio › Output › "Auto close" --------------------------------------------------------------------------------
// FL frees the audio device for other programs (the box's hint). LYNX lets go of it while it is not the program in
// front and has no use for it (nothing playing, recording, exporting or armed), and opens it again when it comes back
// to the front or needs it.
struct DeviceUse
{
    bool foreground = true;
    bool busy = false; // playing, recording, counting in, bouncing, measuring, an input armed
};

enum class DeviceAction { none, close, reopen };

// What to do now. `closedByUs`: the device is closed because of this option (a device the user closed, or one that
// failed, is never reopened from here).
constexpr DeviceAction autoCloseAction (bool option, const DeviceUse& use, bool deviceOpen, bool closedByUs) noexcept
{
    if (closedByUs)
        return ! deviceOpen && (use.foreground || use.busy || ! option) ? DeviceAction::reopen : DeviceAction::none;
    return option && deviceOpen && ! use.foreground && ! use.busy ? DeviceAction::close : DeviceAction::none;
}

// --- General › Misc › "Restore previous state after solo" ---------------------------------------------------------
// What a solo remembers: the mute state of every row (channels or Playlist tracks) from before the solo, and which row
// was soloed. Runtime only, never saved (a project loaded with a solo on has nothing to restore: un-solo then
// switches everything on, as with the option off).
struct SoloMemory
{
    std::vector<bool> muted; // [row] before the solo
    int row = -1;            // the soloed row (−1: nothing remembered)

    void clear() noexcept
    {
        muted.clear();
        row = -1;
    }
    bool valid (int soloRow, std::size_t rows) const noexcept { return row == soloRow && row >= 0 && muted.size() == rows; }
};

// The memories of the Channel rack's and the Playlist's solo (UI thread only), forgotten when another project loads.
inline SoloMemory& channelSoloMemory() noexcept
{
    static SoloMemory m;
    return m;
}
inline SoloMemory& trackSoloMemory() noexcept
{
    static SoloMemory m;
    return m;
}
inline void forgetSolos() noexcept
{
    channelSoloMemory().clear();
    trackSoloMemory().clear();
}

// --- General › Advanced › "Read sample tempo information" ---------------------------------------------------------
// The beat count a sample file carries (its ACID chunk), as the rest of LYNX sees it: 0 = none, which is also the
// answer while the option is off (FL then does not treat the sample as tempo-synced; FUN_006764f0).
inline float sampleTempoBeats (float storedBeats) noexcept
{
    return options().readSampleTempo.load (std::memory_order_relaxed) ? storedBeats : 0.0f;
}

// --- General › Undo history › "Show recent changes at the top" ----------------------------------------------------
// The history step shown on a list row, and back (`count` steps; oldest first unless `newestFirst`).
constexpr int historyStepForRow (int row, int count, bool newestFirst) noexcept { return newestFirst ? count - 1 - row : row; }
constexpr int historyRowForStep (int step, int count, bool newestFirst) noexcept { return newestFirst ? count - 1 - step : step; }
} // namespace lynx::flsettings
