#pragma once

// When LYNX starts a full background plugin scan by itself (round 190fp). A scan loads every plugin, and on a PC with
// expired licences each of those can open its own licence box (QA 190fd: 20+ in a row), so LYNX only scans by itself
// when the user asked for it:
//   - at start-up: the setup wizard was finished or skipped once, and "Look for new plugins in the background at every
//     start" is on (the default);
//   - when the setup wizard is finished: that same box is ticked there, and "Scan now" has not already started one.
//     With the box unticked, Finish saves the folders and scans nothing; "Scan now" in the wizard, the plugin picker's
//     Scan button and the plugin menus' "Scan for plugins" still scan at once. Skip never scans.
// (Our REA notes have no record of FL Studio's own first-run scan, so this follows the box's wording.)

namespace lynx::autoscan
{
constexpr bool atStartup (bool persistentWindow, bool setupDone, bool scanAtEveryStart) noexcept
{
    return persistentWindow && setupDone && scanAtEveryStart;
}

constexpr bool onWizardFinish (bool scanAtEveryStartTicked, bool scanAlreadyStarted) noexcept
{
    return scanAtEveryStartTicked && ! scanAlreadyStarted;
}
} // namespace lynx::autoscan
