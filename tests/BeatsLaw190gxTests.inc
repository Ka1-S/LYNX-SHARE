// Round 190gx: Beats' law switch — Classic (projects saved before 190gx, bit for bit as before) and Measured (Live 12.4's
// renders: new channels, imports). The .lynx side (the "beatsLaw190gx" mark) is checked by the app's --selftest
// (beatslaw190gx_saved); the Classic renders by als175a's tests run on both laws.

#include "core/TimeStretch.h"
#include "core/WarpModes.h"

TEST (beatslaw190gx_defaults_lists_and_cache_keys)
{
    using namespace lynx;
    const WarpModeSettings fresh;
    CHECK (fresh.beatsLaw == WarpModeSettings::beatsLawPitchAware); // (round 190ik's default; Measured before it)
    // the .lynx list: a 190fm list had nine numbers (Classic), the tenth is the law (round 190ik: the eleventh the
    // Texture / Complex Pro law)
    const std::array<double, 9> nine { 6, 2, 100, 30, 65, 25, 100, 128, 0 };
    CHECK (warpmodes::fromList (nine, nine.size()).beatsLaw == WarpModeSettings::beatsLawClassic);
    auto list = warpmodes::toList (fresh);
    CHECK (list.size() == 11 && list[9] == 2.0 && list[10] == 1.0 && warpmodes::fromList (list, list.size()) == fresh);
    list[9] = 1.0;
    CHECK (warpmodes::fromList (list, list.size()).beatsLaw == WarpModeSettings::beatsLawMeasured);
    list[9] = 0.0;
    CHECK (warpmodes::fromList (list, list.size()).beatsLaw == WarpModeSettings::beatsLawClassic);
    // the stretch caches tell the laws apart (Beats only), the Measured law keeps the empty key it had as the default
    StretchOptions measured, classic;
    measured.modes.beatsLaw = WarpModeSettings::beatsLawMeasured;
    classic.modes.beatsLaw = WarpModeSettings::beatsLawClassic;
    CHECK (stretchOptionsKey (StretchOptions {}, StretchMethod::beats) == "|pa");
    CHECK (stretchOptionsKey (measured, StretchMethod::beats).empty());
    CHECK (stretchOptionsKey (classic, StretchMethod::beats) == "|classic");
    CHECK (stretchOptionsKey (classic, StretchMethod::complex).empty());
    // a Classic channel switched by the Clip View
    Song song;
    song.channels.emplace_back();
    song.channels[0].sampler.warpModes.beatsLaw = WarpModeSettings::beatsLawClassic;
    CHECK (warpmodes::setBeatsLaw (song, 0, 1) && song.channels[0].sampler.warpModes.beatsLaw == WarpModeSettings::beatsLawMeasured);
}

TEST (beatslaw190gx_other_methods_ignore_the_law)
{
    using namespace lynx;
    SampleData in;
    in.sampleRate = 44100.0;
    for (int i = 0; i < 44100; ++i)
        in.left.push_back ((float) (0.5 * std::sin (2.0 * 3.141592653589793 * 330.0 * i / 44100.0) * std::exp (-(i % 11025) / 3000.0)));
    in.right = in.left;
    StretchOptions classic;
    classic.modes.beatsLaw = WarpModeSettings::beatsLawClassic;
    for (const auto m : { StretchMethod::tones, StretchMethod::complex })
        CHECK (timeStretch (in, 1.25, m, 0.0, classic)->left == timeStretch (in, 1.25, m)->left);
}

TEST (beatslaw190gx_als_import_gets_the_newest_law)
{
    using namespace lynx;
    auto song = clipenv190gi::dcSong();
    song.channels[0].sampler.keepPitch = true;
    song.channels[0].sampler.stretchMethod = StretchMethod::beats;
    song.channels[0].sampler.warpModes.beatsLaw = WarpModeSettings::beatsLawClassic; // (whatever LYNX had: Live has no law)
    const auto back = livefacts190gx::reimport (exportAls (song).xml);
    CHECK (back.has_value());
    bool any = false;
    if (back)
        for (const auto& c : back->channels)
            if (c.instrument == InstrumentKind::audioClip)
            {
                any = true;
                CHECK (c.sampler.warpModes.beatsLaw == WarpModeSettings::beatsLawPitchAware); // (round 190ik: the pitch-aware law)
            }
    CHECK (any);
}
