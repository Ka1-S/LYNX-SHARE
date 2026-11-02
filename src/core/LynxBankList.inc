// LYNX's own preset banks (core/LynxBanks.h): one line per engine, LYNX_BANK (<InstrumentKind name>, <factory>).
// The factory is defined in lynx::banks by the engine's bank file (add that file to CMakeLists.txt,
// scripts/build-local.sh CORE and scripts/build-windows-zig.sh next to src/core/LynxBanks.cpp).
LYNX_BANK (toxic, toxicBank)       // core/ToxicBank.cpp (round 168n)
LYNX_BANK (poizone, poizoneBank)   // core/PoiZoneBank.cpp (round 168n)
LYNX_BANK (fruitkick, fruitKickBank)   // core/DrumBanks.cpp (round 168p)
LYNX_BANK (plucked, pluckedBank)       // core/DrumBanks.cpp (round 168p)
LYNX_BANK (drumpad, drumpadBank)       // core/DrumBanks.cpp (round 168p)
LYNX_BANK (sakura, sakuraBank)         // core/DrumBanks.cpp (round 168p)
LYNX_BANK (drumaxx, drumaxxBank)       // core/DrumBanks.cpp (round 168p)
LYNX_BANK (drumsynth, drumSynthBank)   // core/DrumBanks.cpp (round 168p)
