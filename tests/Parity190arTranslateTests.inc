// Round 190ar part 2: fitted setting maps as state translators (core/FittedMaps.inc). The data are captured component
// states (saved through the public parameter API at the bank's settings) and the LYNX parameters the map gives there.
namespace p190artr
{
struct Case
{
    const char* label;
    const char* plugin;
    const char* effect;
    const char* xml;
    std::vector<std::pair<int, float>> expected;
};
inline const std::vector<Case>& cases()
{
    static const std::vector<Case> c {
#include "Parity190arTranslateData.inc"
    };
    return c;
}
inline lynx::EffectSlot slotFor (const std::string& plugin, const std::string& xml, int* translated = nullptr)
{
    using namespace lynx;
    ForeignPlugin fp;
    fp.format = ForeignPlugin::Format::vst3;
    fp.name = plugin;
    fp.state = substtest::flVst3State (substtest::juceXmlComponent (xml));
    const auto id = pluginIdentity (fp);
    const auto* entry = findPluginSubstitute (id, false);
    if (entry == nullptr)
        return {};
    int n = 0;
    auto slot = makeSubstituteSlot (*entry, &id, &n);
    if (translated != nullptr)
        *translated = n;
    return slot;
}
} // namespace p190artr

TEST (p190ar_fitted_maps_translate_the_captured_states)
{
    using namespace lynx;
    CHECK (p190artr::cases().size() >= 18);
    for (const auto& c : p190artr::cases())
    {
        int n = 0;
        const auto slot = p190artr::slotFor (c.plugin, c.xml, &n);
        CHECK (std::string (effectInfo (slot.kind).name) == c.effect);
        CHECK (n >= 3); // the state was read (links applied)
        float worst = 0.0f;
        for (const auto& [index, value] : c.expected)
            worst = std::max (worst, std::abs (slot.params[(std::size_t) index] - value));
        if (worst > 2.0e-4f)
            std::printf ("p190ar translate %s: worst %.5f\n", c.label, (double) worst);
        CHECK (worst <= 2.0e-4f);
    }
}

TEST (p190ar_fitted_maps_leave_unknown_states_alone)
{
    using namespace lynx;
    // a reference of the same role without a map, and a mapped one whose state carries none of the mapped entries:
    // the substitute stays at its defaults (the entry's preset), nothing is half applied
    const auto plain = makeEffectSlot (EffectKind::crestCompressor);
    int n = -1;
    auto s = p190artr::slotFor ("VBC FG-Red", "<package version=\"1.0\"><entry id=\"Threshold\" type=\"float64\" value=\"0.3\"/></package>", &n);
    CHECK (s.kind == EffectKind::crestCompressor && n == 0 && s.params == plain.params);
    s = p190artr::slotFor ("SSL Native X-Comp v6", "<SSL_PLUGIN_STATE PluginName=\"SSL Native X-Comp v6\" StateASelected=\"1\"><A x=\"1\"><PROCESSOR_STATE>"
                                                   "<PARAM id=\"somethingElse\" value=\"1.0\"/></PROCESSOR_STATE></A></SSL_PLUGIN_STATE>", &n);
    CHECK (s.kind == EffectKind::crestCompressor && n == 0 && s.params == plain.params);
    // the B state is read when it is the selected one
    const std::string a = "<PARAM id=\"threshold\" value=\"12.0\"/><PARAM id=\"ratio\" value=\"4.0\"/>";
    const std::string b = "<PARAM id=\"threshold\" value=\"-10.0\"/><PARAM id=\"ratio\" value=\"10.0\"/>";
    auto two = [&] (const char* selected) {
        return p190artr::slotFor ("SSL Native X-Comp v6", std::string ("<SSL_PLUGIN_STATE PluginName=\"SSL Native X-Comp v6\" StateASelected=\"") + selected
                                                          + "\"><A x=\"1\"><PROCESSOR_STATE>" + a + "</PROCESSOR_STATE></A><B x=\"1\"><PROCESSOR_STATE>" + b
                                                          + "</PROCESSOR_STATE></B></SSL_PLUGIN_STATE>");
    };
    const auto sa = two ("1"), sb = two ("0");
    CHECK (sa.params != sb.params);
    // a transfer never leaves 0 .. 1, whatever the state says
    s = p190artr::slotFor ("SSL Native X-Comp v6", "<SSL_PLUGIN_STATE PluginName=\"SSL Native X-Comp v6\" StateASelected=\"1\"><A x=\"1\"><PROCESSOR_STATE>"
                                                   "<PARAM id=\"threshold\" value=\"-900.0\"/><PARAM id=\"makeup\" value=\"900.0\"/></PROCESSOR_STATE></A></SSL_PLUGIN_STATE>");
    for (const auto v : s.params)
        CHECK (v >= 0.0f && v <= 1.0f);
}
