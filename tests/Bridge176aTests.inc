// Round 176a: 32-bit plugins run in the 32-bit bridge LYNX-bridge32.exe. LYNX tells a 32-bit module from its PE header
// (core/PeArch.h); the bridge itself is exercised by LYNX.exe --bridge32test on Windows.

#include "core/PeArch.h"
#include "core/PluginCatalog.h"

namespace b176a
{
// the first bytes of a PE file: "MZ", e_lfanew at 0x3c, "PE\0\0" and the machine at e_lfanew + 4
inline std::vector<std::uint8_t> peHeader (std::uint16_t machine, std::uint32_t at = 0x80)
{
    std::vector<std::uint8_t> h (at + 64, 0);
    h[0] = 'M';
    h[1] = 'Z';
    std::memcpy (h.data() + 0x3c, &at, 4);
    std::memcpy (h.data() + at, "PE\0\0", 4);
    h[at + 4] = (std::uint8_t) (machine & 0xff);
    h[at + 5] = (std::uint8_t) (machine >> 8);
    return h;
}
} // namespace b176a

TEST (bridge176a_pe_machine_tells_32_bit_modules_apart)
{
    using lynx::pearch::Arch;
    const auto x86 = b176a::peHeader (0x014c), x64 = b176a::peHeader (0x8664), arm = b176a::peHeader (0xaa64);
    CHECK (lynx::pearch::fromHeader (x86.data(), x86.size()) == Arch::x86);
    CHECK (lynx::pearch::fromHeader (x64.data(), x64.size()) == Arch::x64);
    CHECK (lynx::pearch::fromHeader (arm.data(), arm.size()) == Arch::arm64);
    // the PE header far into the file (some linkers put a long DOS stub first)
    const auto far = b176a::peHeader (0x014c, 0xe00);
    CHECK (lynx::pearch::fromHeader (far.data(), far.size()) == Arch::x86);
    // other machines (IA-64 0x0200) are not x86
    const auto ia64 = b176a::peHeader (0x0200);
    CHECK (lynx::pearch::fromHeader (ia64.data(), ia64.size()) == Arch::other);
}

TEST (bridge176a_pe_machine_rejects_what_is_not_a_pe_file)
{
    using lynx::pearch::Arch;
    auto h = b176a::peHeader (0x014c);
    CHECK (lynx::pearch::fromHeader (nullptr, 0) == Arch::unknown);
    CHECK (lynx::pearch::fromHeader (h.data(), 0x30) == Arch::unknown); // cut before e_lfanew
    CHECK (lynx::pearch::fromHeader (h.data(), 0x82) == Arch::unknown); // cut inside the PE signature / machine
    auto bad = h;
    bad[0] = 'Z';
    CHECK (lynx::pearch::fromHeader (bad.data(), bad.size()) == Arch::unknown); // no "MZ"
    auto noPe = h;
    noPe[0x81] = 'X';
    CHECK (lynx::pearch::fromHeader (noPe.data(), noPe.size()) == Arch::unknown); // no "PE\0\0"
    auto huge = h;
    const std::uint32_t past = 0xfffffff0u;
    std::memcpy (huge.data() + 0x3c, &past, 4);
    CHECK (lynx::pearch::fromHeader (huge.data(), huge.size()) == Arch::unknown); // e_lfanew past the end (no overflow)
    const std::uint8_t elf[64] = { 0x7f, 'E', 'L', 'F' };
    CHECK (lynx::pearch::fromHeader (elf, sizeof (elf)) == Arch::unknown);
}

TEST (bridge176a_vst3_bundle_folders_follow_steinberg_names)
{
    using lynx::pearch::Arch;
    CHECK (std::string (lynx::pearch::vst3Folder (Arch::x86)) == "x86-win");
    CHECK (std::string (lynx::pearch::vst3Folder (Arch::x64)) == "x86_64-win");
    CHECK (std::string (lynx::pearch::vst3Folder (Arch::arm64)) == "arm64-win");
    CHECK (std::string (lynx::pearch::vst3Folder (Arch::unknown)).empty());
}

// FL's "Combine 32 and 64 bit versions of a plugin" [UI fl29c]: one entry "32+64" for a pair; a lone build keeps its own
TEST (bridge176a_32_and_64_bit_builds_of_one_plugin_are_one_entry)
{
    const std::vector<std::pair<std::string, bool>> items = {
        { "VST|4c585632|LYNX Test Synth", true },  // the 32-bit build
        { "VST|4c585632|LYNX Test Synth", false }, // the 64-bit build of the same plugin
        { "VST|11111111|Old Delay", true },        // 32-bit only
        { "VST3|22222222|New EQ", false },         // 64-bit only
        { "VST3|4c585632|LYNX Test Synth", true }, // another format: its own entry
    };
    const auto bits = lynx::plugincatalog::combinedBits (items);
    CHECK (bits.size() == items.size());
    CHECK (bits[0].empty());
    CHECK (bits[1] == "32+64");
    CHECK (bits[2] == "32");
    CHECK (bits[3] == "64");
    CHECK (bits[4] == "32");
    CHECK (lynx::plugincatalog::combinedBits ({}).empty());
}
