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
} // namespace lynx::countin
