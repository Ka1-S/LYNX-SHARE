// Round details11_190oa (0.1.31, details pass 11): the last open rows of the details audit.

// FL's toolbar main volume (DF-21, owner decision F1; REA 190oa R2: control 0x40000000, 0…16000, 12800 = 0 dB, gain
// (11^v − 1) / 10 on the output, skipped while rendering): the song keeps it as a fader position; .flp in and out.
TEST (details190oa_main_volume_flp)
{
    using namespace lynx;
    CHECK (Song::makeDefault().mainVolume == unityFader);
    auto song = Song::makeDefault();
    song.mainVolume = 0.5f;
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    std::int32_t raw = -1;
    for (const auto& c : project.controls)
        if (c.id.raw == flp::ControlId::mainVolume)
            raw = c.value;
    CHECK (raw == 8000);
    const auto back = importFlp (project);
    CHECK (std::abs (back.mainVolume - 0.5f) < 1.0e-6f);
    // FL's default writes 12800 = unity, and the Master insert's fader is left alone
    const auto plain = importFlp (flp::parseProject (flpexport::exportFlp (Song::makeDefault(), options, report)));
    CHECK (plain.mainVolume == unityFader);
    CHECK (std::abs (faderGain (unityFader) - 1.0f) < 1.0e-5f && std::abs (faderGain (1.0f) - 1.9028f) < 1.0e-3f);
}

// FL's wrapper menu › Help (DF-131): LYNX's wiki page of the built-in plugin (website/_src/build.py's ids)
TEST (details190oa_plugin_help_pages)
{
    using namespace lynx;
    CHECK (pluginhelp::slug ("3x Osc") == "3x-osc" && pluginhelp::slug ("Plucked!") == "plucked" && pluginhelp::slug ("X-Y Controller") == "x-y-controller");
    CHECK (pluginhelp::pageFor ("Sytrus", true) == "https://lynx-share.com/wiki/built-in/gen-sytrus/");
    CHECK (pluginhelp::pageFor ("Fruity Limiter", false) == "https://lynx-share.com/wiki/built-in/fx-fruity-limiter/");
    CHECK (pluginhelp::pageFor ("Toxic Biohazard", true) == "https://lynx-share.com/wiki/built-in/gen-toxic-biohazard/");
    CHECK (pluginhelp::pageFor ("No such plugin", true) == "https://lynx-share.com/wiki/built-in/"); // (the index)
    CHECK (pluginhelp::hasPage ("fx-aether") && pluginhelp::hasPage ("gen-wave-traveller") && ! pluginhelp::hasPage ("fx-"));
}

// Gross Beat's scratch dial (DP-8; REA 190oa R5): parameter 8, FL raw 65536 = no offset, s = raw / 65536 − 1 added to
// the time envelope after three smoothers; LYNX stores 1 − raw / 65536 so the old projects' 0 is no offset.
TEST (details190oa_gross_beat_scratch_offset)
{
    using namespace lynx;
    CHECK (grossbeat::paramFromRaw (grossbeat::scratchParam, 65536) == 0.0f && grossbeat::rawFromParam (grossbeat::scratchParam, 0.0f) == 65536);
    CHECK (grossbeat::rawFromParam (grossbeat::scratchParam, 0.5f) == 32768 && grossbeat::paramFromRaw (grossbeat::scratchParam, 0) == 1.0f);
    const auto state = grossBeatState ({ 0, 0, 256, 256, 0, 0, 0, 0, 0x10000 }, { 0, 0, 0, 0, 0, 0, 0, 1, 1 }, {});
    auto slot = grossbeat::slotFromFl (state);
    CHECK (slot.has_value() && slot->params[(std::size_t) grossbeat::scratchParam] == 0.0f);
    if (! slot)
        return;
    // a click every 7001 samples (no divisor of the bar at 120 BPM, 48 kHz: 96000 samples)
    std::vector<float> in (240000, 0.0f);
    for (std::size_t i = 0; i < in.size(); i += 7001)
        in[i] = 1.0f;
    const auto plain = renderGrossBeat (*slot, in, 48000.0, 120.0);
    // no offset: the dry signal 15 samples late (unchanged by the 9th parameter)
    CHECK (plain[15] == 1.0f && plain[7016] == 1.0f);
    auto scratched = *slot;
    scratched.params[(std::size_t) grossbeat::scratchParam] = 0.25f; // half a bar back once the smoothers settle
    const auto out = renderGrossBeat (scratched, in, 48000.0, 120.0);
    double diff = 0.0;
    for (std::size_t i = 100000; i < out.size(); ++i)
        diff += std::abs ((double) out[i] - (double) plain[i]);
    CHECK (diff > 1.0); // it plays something else
}
