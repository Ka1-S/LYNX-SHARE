// Round 190kv: the Soundtoys factory presets. Every factory preset of the installed Soundtoys plug-ins was loaded into
// the official plug-in in its own hidden host process (public VST3 API only) and read back; LYNX recognises them by a
// fingerprint of the encoded payload (FNV-1a 64 over its length and every character but the volatile fields: the last 24
// and characters 300 … 369, which change with every save) and plays their own settings mapped onto Echoform / Ember /
// LYNX Tune (core/SoundtoysFactory190kv.inc, generated; docs/analise/REA_rodada190kv.md). Any other state keeps the
// round-136 hashes and the substitute's preset bit for bit. These tests use synthetic payloads (no vendor data).
#include <cstdint>
#include <string>
#include <vector>

#include "core/Effects.h"
#include "core/EchoSuite.h"
#include "core/MixSuite.h"
#include "core/PluginSubstitutes.h"

namespace stoys190kv
{
// a synthetic Soundtoys-like state: "WIDGET = <w>;\rVERSION = 4;\rPRESET =<payload>;\r", payload in the 6-bit alphabet
inline std::vector<std::uint8_t> state (const std::string& widget, const std::string& payload)
{
    const std::string s = "WIDGET = " + widget + ";\rVERSION = 4;\rSAVED_BY_VERSION = 5.5.5;\rBUILDNUM = 19885;\rPRESET =" + payload + ";\r";
    return { s.begin(), s.end() };
}
inline std::string payload (std::size_t n, unsigned seed)
{
    std::string p (n, '0');
    for (auto& c : p)
    {
        seed = seed * 1664525u + 1013904223u;
        c = (char) (0x30 + ((seed >> 20) & 63u));
    }
    return p;
}
inline lynx::EffectSlot substitute (const char* name, const std::vector<std::uint8_t>* st, int* translated)
{
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst3;
    id.name = name;
    if (st != nullptr)
        id.state = *st;
    const auto* e = lynx::findPluginSubstitute (id, false);
    return e != nullptr ? lynx::makeSubstituteSlot (*e, st != nullptr ? &id : nullptr, translated) : lynx::EffectSlot {};
}
} // namespace stoys190kv

TEST (stoys190kv_fingerprint_ignores_only_the_volatile_fields)
{
    using namespace lynx;
    using stoys190kv::state;
    const auto p = stoys190kv::payload (900, 7u);
    const auto fp = soundtoysFactoryFingerprint (state ("EchoBoy", p));
    CHECK (fp != 0);
    auto q = p; // the volatile fields: characters 300 … 369 and the last 24
    q[300] = q[300] == '1' ? '2' : '1';
    q[369] = q[369] == '1' ? '2' : '1';
    q[900 - 24] = q[900 - 24] == '1' ? '2' : '1';
    q[899] = q[899] == '1' ? '2' : '1';
    CHECK (soundtoysFactoryFingerprint (state ("EchoBoy", q)) == fp);
    for (const std::size_t at : { (std::size_t) 0, (std::size_t) 10, (std::size_t) 299, (std::size_t) 370, (std::size_t) (900 - 25) })
    {
        auto r = p;
        r[at] = r[at] == '1' ? '2' : '1';
        CHECK (soundtoysFactoryFingerprint (state ("EchoBoy", r)) != fp);
    }
    CHECK (soundtoysFactoryFingerprint (state ("EchoBoy", p + "0")) != fp); // the length counts
    CHECK (soundtoysFactoryFingerprint (state ("EchoBoy", p.substr (0, 20))) == 0); // no character outside the tail
    const std::string text = "<?xml version=\"1.0\"?><state/>";
    CHECK (soundtoysFactoryFingerprint (std::vector<std::uint8_t> (text.begin(), text.end())) == 0);
}

TEST (stoys190kv_factory_tables_cover_the_installed_presets)
{
    using namespace lynx;
    // fingerprints per plug-in (the re-saved state's and the preset file's; ambiguous ones dropped): sorted, unique
    const std::pair<const char*, std::size_t> least[] { { "EchoBoy", 300 }, { "EchoBoy Jr", 50 }, { "PrimalTap", 50 },
                                                        { "Little PrimalTap", 25 }, { "Decapitator", 40 }, { "Little AlterBoy", 10 } };
    for (const auto& [widget, n] : least)
    {
        const auto f = soundtoysFactoryFingerprints (widget);
        CHECK (f.size() >= n);
        if (std::string (widget) != "Little AlterBoy")
            for (std::size_t i = 1; i < f.size(); ++i)
                CHECK (f[i - 1] < f[i]);
    }
    CHECK (soundtoysFactoryFingerprints ("PanMan").empty()); // its patterns are not in the parameter list: not shipped
}

TEST (stoys190kv_a_factory_preset_plays_its_own_settings)
{
    using namespace lynx;
    namespace ef = echosuite::echoform;
    namespace em = mixsuite::ember;
    for (const char* widget : { "EchoBoy", "EchoBoy Jr", "PrimalTap", "Little PrimalTap" })
    {
        int checked = 0;
        for (const auto fp : soundtoysFactoryFingerprints (widget))
        {
            auto slot = stoys190kv::substitute (widget, nullptr, nullptr);
            CHECK (slot.kind == EffectKind::echoform);
            const auto n = applySoundtoysFactoryPreset (widget, fp, slot);
            CHECK (n >= 8 && slot.mix == 1.0f);
            CHECK (slot.params[(std::size_t) ef::revision] == 1.0f); // a new instance's voicings (the value maps' lab)
            for (int i = 0; i < ef::paramCount; ++i)
                CHECK (slot.params[(std::size_t) i] >= 0.0f && slot.params[(std::size_t) i] <= 1.0f);
            if (++checked == 40)
                break;
        }
        CHECK (checked > 0);
    }
    for (const auto fp : soundtoysFactoryFingerprints ("Decapitator"))
    {
        auto slot = stoys190kv::substitute ("Decapitator", nullptr, nullptr);
        CHECK (applySoundtoysFactoryPreset ("Decapitator", fp, slot) == 12);
        CHECK (slot.params[(std::size_t) em::voicing] == 1.0f && slot.mix == 1.0f);
        auto echo = stoys190kv::substitute ("EchoBoy", nullptr, nullptr); // another plug-in's fingerprint, another effect: nothing
        const auto before = echo.params;
        CHECK (applySoundtoysFactoryPreset ("Decapitator", fp, echo) == 0 && echo.params == before);
        CHECK (applySoundtoysFactoryPreset ("EchoBoy", fp, slot) == 0);
    }
    for (const auto fp : soundtoysFactoryFingerprints ("Little AlterBoy"))
    {
        auto slot = stoys190kv::substitute ("Little AlterBoy", nullptr, nullptr);
        CHECK (applySoundtoysFactoryPreset ("Little AlterBoy", fp, slot) == 8 && slot.mix == 1.0f);
    }
    auto slot = stoys190kv::substitute ("EchoBoy", nullptr, nullptr);
    CHECK (applySoundtoysFactoryPreset ("EchoBoy", 0x1234567890abcdefull, slot) == 0);
}

TEST (stoys190kv_any_other_state_keeps_the_previous_translation_bit_for_bit)
{
    using namespace lynx;
    for (const char* name : { "EchoBoy", "EchoBoy Jr", "PrimalTap", "Little PrimalTap", "Decapitator", "Little AlterBoy", "PanMan" })
        for (unsigned seed = 1; seed <= 4; ++seed)
        {
            std::string widget = name;
            const auto st = stoys190kv::state (widget, stoys190kv::payload (600 + 100 * seed, seed));
            int translated = -1;
            const auto with = stoys190kv::substitute (name, &st, &translated);
            const auto without = stoys190kv::substitute (name, nullptr, nullptr);
            CHECK (translated == 0);
            CHECK (with.kind == without.kind && with.params == without.params && with.mix == without.mix);
        }
}
