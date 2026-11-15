#pragma once

// LYNX SHARE's outside addresses that the app opens in the browser. One constant per address: change it here and
// every menu item, window and button that opens it follows.
namespace lynx::links
{
// The donation page (round 190fq): Help › Support LYNX SHARE..., the About window and the start-up note open it.
// Donating is optional and never unlocks anything. (Also in .github/FUNDING.yml, README.md and website/_src/build.py.)
inline constexpr const char* donate = "https://ko-fi.com/lynxshare";
} // namespace lynx::links
