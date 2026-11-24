#pragma once

// Two LYNX extras for saving and exporting (round 190qo), off by default (FL asks where to export every time and saves
// a project where it is):
//  - Issue #251: "Always collect and save" — every save first copies the files the project uses from outside its folder
//    into <project folder>/Samples/Imported and switches the project to the copies (File › Collect all and save, Live's
//    command, run on every save), so a project folder always opens complete on another computer.
//  - Issue #215: "Export to the File name's folder without asking" — the Export window's Export... writes straight to the
//    name its File name field gives (wildcards and folders resolved) inside the export folder, without the file dialog
//    (a file that exists is still confirmed).
// App settings (ui-options.txt "alwaysCollect", "exportNoDialog").

#include <atomic>

namespace lynx::saveextras
{
inline std::atomic<bool>& alwaysCollectFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool alwaysCollect() noexcept { return alwaysCollectFlag().load (std::memory_order_relaxed); }
inline void setAlwaysCollect (bool on) noexcept { alwaysCollectFlag().store (on, std::memory_order_relaxed); }

inline std::atomic<bool>& exportNoDialogFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool exportNoDialog() noexcept { return exportNoDialogFlag().load (std::memory_order_relaxed); }
inline void setExportNoDialog (bool on) noexcept { exportNoDialogFlag().store (on, std::memory_order_relaxed); }
} // namespace lynx::saveextras
