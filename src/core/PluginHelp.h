#pragma once

// Round 190oa (DF-131, FL's plugin wrapper menu › Help: FL opens the plugin's page of its manual): the page of LYNX's
// wiki for a built-in plugin, https://lynx-share.com/wiki/built-in/<fx|gen>-<slug>/ (website/_src/build.py makes one per
// catalogue entry with a wiki body; the slug is the plugin's name in lower case, every run of other characters one
// "-"). The ids below are website/_src/catalog.json's at this round; a name without a page gets the built-in index.

#include <algorithm>
#include <cctype>
#include <string>

namespace lynx::pluginhelp
{
inline std::string slug (const std::string& name)
{
    std::string out;
    bool dash = false;
    for (const auto ch : name)
    {
        const auto c = (unsigned char) ch;
        if (std::isalnum (c) != 0 && c < 128)
        {
            if (dash && ! out.empty())
                out += '-';
            dash = false;
            out += (char) std::tolower (c);
        }
        else
            dash = true;
    }
    return out;
}

inline bool hasPage (const std::string& id)
{
    static const std::string ids = " "
        "fx-aether fx-apex-limiter fx-atrium fx-audio-effect-rack fx-auto-pan fx-chorus fx-compressor "
        "fx-control-surface fx-crest-compressor fx-crossover fx-delay fx-distortion fx-distructor fx-echoform fx-edison "
        "fx-effector fx-ember-saturator fx-emphasis fx-emphasizer fx-epoch-verb fx-eq fx-equo fx-facet-eq fx-filter "
        "fx-foil-plate fx-formula-controller fx-frequency-shifter fx-frequency-shifter-2 fx-frequency-splitter "
        "fx-fret-channel fx-fruity-7-band-eq fx-fruity-balance fx-fruity-bass-boost fx-fruity-big-clock "
        "fx-fruity-blood-overdrive fx-fruity-center fx-fruity-chorus fx-fruity-compressor fx-fruity-convolver "
        "fx-fruity-delay fx-fruity-delay-2 fx-fruity-delay-3 fx-fruity-delay-bank fx-fruity-fast-dist fx-fruity-filter "
        "fx-fruity-flanger fx-fruity-flangus fx-fruity-free-filter fx-fruity-html-notebook fx-fruity-limiter "
        "fx-fruity-love-philter fx-fruity-multiband-compressor fx-fruity-panomatic fx-fruity-phaser fx-fruity-scratcher "
        "fx-fruity-send fx-fruity-soft-clipper fx-fruity-waveshaper fx-gemini-doubler fx-glow-leveler fx-gross-beat "
        "fx-halo fx-hardcore fx-hardware-insert fx-horizon fx-hush-de-esser fx-hyper-chorus fx-kiln-channel "
        "fx-lattice-mod fx-limiter fx-lo-fi fx-low-lifter fx-luxeverb fx-lynx-tune fx-maximus fx-multiband-delay "
        "fx-nebula fx-newtime fx-newtone fx-orbit fx-parametric-eq fx-patcher fx-peak-controller fx-pitch-correction "
        "fx-pitch-shift fx-pitch-shifter fx-pocket-autoduck fx-pocket-chorus fx-pocket-comb-filter fx-pocket-compressor "
        "fx-pocket-distortion fx-pocket-eq fx-pocket-filter fx-pocket-flanger fx-pocket-gate fx-pocket-graphic-eq "
        "fx-pocket-leveller fx-pocket-limiter fx-pocket-limiter-classic fx-pocket-multiband-compressor "
        "fx-pocket-multifx fx-pocket-phaser fx-pocket-reverb fx-pocket-reverb-2 fx-pocket-reverb-classic "
        "fx-pocket-spacer fx-pocket-spreader fx-pocket-stereoizer fx-pocket-tape-delay fx-pocket-tape-stop "
        "fx-pocket-trance-delay fx-pocket-tremolo fx-pocket-waveshaper fx-pocket-wow-flutter fx-prism-multiband "
        "fx-quell-denoiser fx-reeverb fx-reeverb-2 fx-reverb fx-rhythmic-envelope fx-ridge-strip fx-sentry-gate "
        "fx-shell-channel fx-spiral-echo fx-spreader fx-squeeze fx-stereo-enhancer fx-stereo-shaper fx-strike-shaper "
        "fx-transient-processor fx-transmitter fx-transporter fx-tuner fx-utility fx-vintage-chorus fx-vintage-phaser "
        "fx-visualizer fx-vocoder fx-voice-shift fx-volume-gate fx-wave-candy fx-x-y-controller fx-x-y-z-controller "
        "gen-3x-osc gen-bassdrum gen-beepmap gen-boobass gen-dashboard gen-drum-rack gen-drumaxx gen-drumpad "
        "gen-envelope-controller gen-fl-keys gen-fl-slayer gen-fpc gen-fruit-kick gen-fruity-dance gen-fruity-dx10 "
        "gen-fruity-granulizer gen-fruity-slicer gen-fruity-video-player gen-gms gen-gmsynth-flm gen-harmless "
        "gen-harmor gen-instrument-rack gen-kepler gen-kepler-exo gen-keyboard-controller gen-layer gen-lynx-soundfont "
        "gen-midi-effect-rack gen-midi-out gen-minisynth gen-morphine gen-ogun gen-patcher gen-plucked "
        "gen-pocket-supersaw gen-poizone gen-sakura gen-sawer gen-simsynth gen-slicex gen-synth gen-sytrus "
        "gen-toxic-biohazard gen-transistor-bass gen-vfx-color-mapper gen-vfx-envelope gen-vfx-key-mapper "
        "gen-vfx-keyboard-splitter gen-vfx-level-scaler gen-vfx-script gen-vfx-sequencer gen-voltage-controller "
        "gen-wave-traveller ";
    return ! id.empty() && ids.find (" " + id + " ") != std::string::npos;
}

constexpr const char* wikiRoot = "https://lynx-share.com/wiki/";

// the built-in plugin's page (its index when it has none of its own)
inline std::string pageFor (const std::string& name, bool generator)
{
    const auto id = std::string (generator ? "gen-" : "fx-") + slug (name);
    return std::string (wikiRoot) + "built-in/" + (hasPage (id) ? id + "/" : std::string());
}
} // namespace lynx::pluginhelp
