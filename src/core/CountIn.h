#pragma once

#include <string_view>

// FL's "Countdown before recording" (Ctrl+P, the toolbar's PrecountBtn, Options › Recording precount) and its
// right-click "Precount duration" (PrecountPopup: 1 bar / 2 bars, radio items; LYNX also offers 4). The switch and the
// length are two settings: turning the countdown off keeps its length, and both are kept between sessions. FL stores
// them in its settings (HKCU\Software\Image-Line\FL Studio 26\General\ToolbarForm "PrecountBtn" = on/off and
// General "Precount time" = the length, read 2026-11-12; DFM TFruityLoopsMainForm PrecountPopup). Issue #152: LYNX
// saved neither, and Ctrl+P / the button turned a 2- or 4-bar count-in into 1 bar.
namespace lynx::countin
{
constexpr int maxBars = 4;

inline int clampLength (int bars) noexcept { return bars < 1 ? 1 : (bars > maxBars ? maxBars : bars); }

// Ctrl+P / the toolbar button: off <-> on at the kept length. `bars` = the count-in now (0 = off).
inline int toggled (int bars, int length) noexcept { return bars > 0 ? 0 : clampLength (length); }

// The length kept while the count-in is `bars` (a count-in that is on sets it; off keeps `length`).
inline int lengthFor (int bars, int length) noexcept { return clampLength (bars > 0 ? bars : length); }

// What the count-in starts as before the settings are read: a new install as FL's factory form does (the action
// OptionsRecordingPrecountAction is "Checked" in FL's DFM, REA_rodada177c; 1 bar). A settings file from an older LYNX,
// which never saved the count-in, keeps LYNX's old start (off) until the user sets it.
constexpr int startingBars (bool settingsFileExists) noexcept { return settingsFileExists ? 0 : 1; }

// The settings file's two keys (MainComponent's ui settings): "recordCountIn" = 0/1, "recordCountInBars" = the
// length. Read in any order: returns false for another key. `bars` = the count-in (0 = off), `length` = the kept one.
constexpr std::string_view onKey = "recordCountIn";
constexpr std::string_view lengthKey = "recordCountInBars";

inline bool readSetting (std::string_view key, int value, int& bars, int& length) noexcept
{
    if (key == lengthKey)
    {
        length = clampLength (value);
        if (bars > 0)
            bars = length;
        return true;
    }
    if (key == onKey)
    {
        length = clampLength (length);
        bars = value != 0 ? length : 0;
        return true;
    }
    return false;
}
// FL's time display during the count-in reads "REC IN n", n = the beats left, one per beat (live check 190iw Q7: REC IN 4,
// 3, 2, 1 over one bar of 4/4 at 150 BPM, ~400 ms each). `ticksLeft` = Transport::countIn, `beatUnit` = the meter's note
// value (a beat = 4 / beatUnit quarter notes of `ppqn` ticks). 0 when not counting in.
inline int beatsLeft (double ticksLeft, int beatUnit, int ppqn) noexcept
{
    if (! (ticksLeft > 0.0) || ppqn <= 0)
        return 0;
    const auto beat = (double) ppqn * 4.0 / (double) (beatUnit > 0 ? beatUnit : 4);
    const auto n = (int) (ticksLeft / beat - 1.0e-6) + 1; // the beat being counted: 4 at the very start, 1 on the last
    return n < 1 ? 1 : n;
}
// Live's count-in in the Arrangement Position field (round 190kn, DL-44; Live 12 manual 19.6: "The count-in runs from
// negative bars-beats-sixteenths (beginning at -2.1.1., for example, with a Count-In setting of 2 bars) up to 1.1.1., at
// which point recording commences"): the bar counts up from -bars to -1 while the beat and sixteenth count forward
// inside it. `ticksLeft` = Transport::countIn, `beatsPerBar` / `beatUnit` = the meter. bar = 0 when not counting in.
struct LivePosition
{
    int bar = 0, beat = 1, sixteenth = 1;
};
inline LivePosition livePosition (double ticksLeft, int beatsPerBar, int beatUnit, int ppqn) noexcept
{
    if (! (ticksLeft > 0.0) || ppqn <= 0)
        return {};
    const auto beat = (double) ppqn * 4.0 / (double) (beatUnit > 0 ? beatUnit : 4);
    const auto bar = beat * (double) (beatsPerBar > 0 ? beatsPerBar : 4);
    const auto barsLeft = (int) (ticksLeft / bar - 1.0e-9) + 1;          // the bar being counted: -2, then -1
    const auto into = ((double) barsLeft * bar - ticksLeft > 0.0 ? (double) barsLeft * bar - ticksLeft : 0.0); // ticks into that bar
    LivePosition p;
    p.bar = -barsLeft;
    p.beat = (int) (into / beat) + 1;
    p.sixteenth = (int) ((into - (double) (p.beat - 1) * beat) / ((double) ppqn / 4.0)) + 1;
    return p;
}
} // namespace lynx::countin
