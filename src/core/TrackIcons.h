#pragma once

// The icons of channels, mixer inserts and Playlist tracks (round 168h). The model keeps FL Studio's own icon index
// (ChannelDef::icon, MixerTrack::icon, PlaylistTrack::icon: .flp 0x9B / 0x5F / 0xEE+8, REA round 168j) so a project
// round-trips with FL; LYNX draws its own original icon set (src/ui/TrackIcons.cpp), never FL's artwork.
//
// FL's index i is a glyph of its icon font at U+F400 + i (REA round 168h: the 168 FL demo projects' 3 937 non-zero
// icons against the names of FL's ILGlyphsEx font: 4 Snare_drum on "snare" 35x, 5 Kick_drum on "kick" 42x,
// 20 Speech on "vocals", 239 Hi_Hat on "hi-hats" 27x...). Each LYNX icon carries the FL index it saves; an imported
// index LYNX has no icon of its own for shows the closest one (and is kept as it is until the user picks another).
// No JUCE: tested in isolation.

#include <array>
#include <cstdint>
#include <string_view>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::trackicons
{
enum class Icon : std::uint8_t
{
    none,
    // drums and percussion
    kick, snare, hiHat, cymbal, clap, percussion, shaker, tambourine, cowbell, drumMachine,
    // instruments
    acousticGuitar, electricGuitar, strings, brass, saxophone, flute, organ, piano, keyboard, harp, bell, tuningFork,
    melody, bass, synth, squareSynth, pad, pluck, arpeggio, wobble,
    // voice
    vocals, maleVocals, femaleVocals, vocoder, microphone,
    // audio and samples
    sample, loop, audio, notes, steps, automation, nature, film, vinyl,
    // effects and routing
    effect, reverb, equalizer, compressor, filter, riser, send, bus, master, sidechain, plugin, knob, midi, headphones,
    speaker,
    // numbers 1..9
    number1, number2, number3, number4, number5, number6, number7, number8, number9,
    // misc
    smile, fire, cloud, heart, clock, gear, folder, lock, tag // tag: an FL icon LYNX has no own one for
};

constexpr int iconCount = (int) Icon::tag + 1;

struct Entry
{
    Icon icon;
    int flIndex;           // the index LYNX saves for this icon (FL's glyph U+F400 + flIndex)
    std::string_view name; // English label (translated by the picker)
};

// The picker's icons in its order (no `tag`: that one only stands in for FL icons LYNX has no own one for).
inline constexpr std::array<Entry, iconCount - 1> entries { {
    { Icon::none, 0, TR_NOOP ("None") },
    { Icon::kick, 5, TR_NOOP ("Kick") },
    { Icon::snare, 4, TR_NOOP ("Snare") },
    { Icon::hiHat, 239, TR_NOOP ("Hi-hat") },
    { Icon::cymbal, 6, TR_NOOP ("Cymbal") },
    { Icon::clap, 132, TR_NOOP ("Clap") },
    { Icon::percussion, 7, TR_NOOP ("Percussion") },
    { Icon::shaker, 129, TR_NOOP ("Shaker") },
    { Icon::tambourine, 128, TR_NOOP ("Tambourine") },
    { Icon::cowbell, 121, TR_NOOP ("Cowbell") },
    { Icon::drumMachine, 111, TR_NOOP ("Drum machine") },
    { Icon::acousticGuitar, 1, TR_NOOP ("Acoustic guitar") },
    { Icon::electricGuitar, 2, TR_NOOP ("Electric guitar") },
    { Icon::strings, 3, TR_NOOP ("Strings") },
    { Icon::brass, 8, TR_NOOP ("Brass") },
    { Icon::saxophone, 120, TR_NOOP ("Saxophone") },
    { Icon::flute, 91, TR_NOOP ("Flute") },
    { Icon::organ, 9, TR_NOOP ("Organ") },
    { Icon::piano, 12, TR_NOOP ("Piano") },
    { Icon::keyboard, 237, TR_NOOP ("Keyboard") },
    { Icon::harp, 11, TR_NOOP ("Harp") },
    { Icon::bell, 78, TR_NOOP ("Bell") },
    { Icon::tuningFork, 15, TR_NOOP ("Tuning fork") },
    { Icon::melody, 13, TR_NOOP ("Melody") },
    { Icon::bass, 14, TR_NOOP ("Bass") },
    { Icon::synth, 16, TR_NOOP ("Synth") },
    { Icon::squareSynth, 17, TR_NOOP ("Square synth") },
    { Icon::pad, 86, TR_NOOP ("Pad") },
    { Icon::pluck, 87, TR_NOOP ("Pluck") },
    { Icon::arpeggio, 109, TR_NOOP ("Arpeggio") },
    { Icon::wobble, 122, TR_NOOP ("Wobble") },
    { Icon::vocals, 20, TR_NOOP ("Vocals") },
    { Icon::maleVocals, 21, TR_NOOP ("Male vocals") },
    { Icon::femaleVocals, 22, TR_NOOP ("Female vocals") },
    { Icon::vocoder, 23, TR_NOOP ("Vocoder") },
    { Icon::microphone, 30, TR_NOOP ("Microphone") },
    { Icon::sample, 18, TR_NOOP ("Sample") },
    { Icon::loop, 19, TR_NOOP ("Loop") },
    { Icon::audio, 75, TR_NOOP ("Audio") },
    { Icon::notes, 70, TR_NOOP ("Notes") },
    { Icon::steps, 72, TR_NOOP ("Steps") },
    { Icon::automation, 73, TR_NOOP ("Automation") },
    { Icon::nature, 24, TR_NOOP ("Nature") },
    { Icon::film, 25, TR_NOOP ("Film") },
    { Icon::vinyl, 88, TR_NOOP ("Vinyl") },
    { Icon::effect, 26, TR_NOOP ("Effect") },
    { Icon::reverb, 119, TR_NOOP ("Reverb") },
    { Icon::equalizer, 117, TR_NOOP ("EQ") },
    { Icon::compressor, 82, TR_NOOP ("Compressor") },
    { Icon::filter, 114, TR_NOOP ("Filter") },
    { Icon::riser, 115, TR_NOOP ("Riser") },
    { Icon::send, 118, TR_NOOP ("Send") },
    { Icon::bus, 39, TR_NOOP ("Bus") },
    { Icon::master, 116, TR_NOOP ("Master") },
    { Icon::sidechain, 93, TR_NOOP ("Sidechain") },
    { Icon::plugin, 27, TR_NOOP ("Plugin") },
    { Icon::knob, 36, TR_NOOP ("Knob") },
    { Icon::midi, 96, TR_NOOP ("MIDI") },
    { Icon::headphones, 28, TR_NOOP ("Headphones") },
    { Icon::speaker, 29, TR_NOOP ("Speaker") },
    { Icon::number1, 59, "1" },
    { Icon::number2, 60, "2" },
    { Icon::number3, 61, "3" },
    { Icon::number4, 62, "4" },
    { Icon::number5, 63, "5" },
    { Icon::number6, 64, "6" },
    { Icon::number7, 65, "7" },
    { Icon::number8, 66, "8" },
    { Icon::number9, 67, "9" },
    { Icon::smile, 97, TR_NOOP ("Smile") },
    { Icon::fire, 126, TR_NOOP ("Fire") },
    { Icon::cloud, 133, TR_NOOP ("Cloud") },
    { Icon::heart, 43, TR_NOOP ("Good") },
    { Icon::clock, 99, TR_NOOP ("Clock") },
    { Icon::gear, 104, TR_NOOP ("Gear") },
    { Icon::folder, 110, TR_NOOP ("Folder") },
    { Icon::lock, 92, TR_NOOP ("Lock") },
} };

// The FL index LYNX saves for `icon` (`tag` and unknown: 0).
constexpr int flIndexOf (Icon icon) noexcept
{
    for (const auto& e : entries)
        if (e.icon == icon)
            return e.flIndex;
    return 0;
}

constexpr std::string_view nameOf (Icon icon) noexcept
{
    for (const auto& e : entries)
        if (e.icon == icon)
            return e.name;
    return TR_NOOP ("Other");
}

// The LYNX icon shown for FL's index `flIndex`: its own icon, else the closest one, else `tag` (any other non-zero
// index); 0 and negatives: none.
constexpr Icon iconFor (int flIndex) noexcept
{
    if (flIndex <= 0)
        return Icon::none;
    for (const auto& e : entries)
        if (e.flIndex == flIndex)
            return e.icon;
    switch (flIndex) // FL glyphs (U+F400 + index) LYNX draws with a near icon
    {
        case 10: return Icon::flute;           // pan flute
        case 31: return Icon::send;            // jack plug
        case 32: return Icon::vinyl;           // CD / DVD
        case 33: return Icon::microphone;      // in
        case 34: return Icon::speaker;         // out
        case 35: return Icon::knob;            // joystick (remote control)
        case 38: return Icon::film;            // visual
        case 44: return Icon::tag;             // bad
        case 46: case 47: case 48: case 49: case 50: case 51: case 52: // (unnamed speaker positions)
        case 53: case 54: case 55: case 56: case 57: case 58:          // quad front / rear, mono, stereo, left, right
            return Icon::speaker;
        case 68: case 69: return Icon::bus;
        case 71: return Icon::notes;           // melodic score
        case 74: return Icon::automation;
        case 79: case 81: case 89: return Icon::bell; // triangle, glockenspiel, chimes
        case 80: return Icon::cymbal;          // gong
        case 83: case 85: case 127: return Icon::synth; // additive, FM, Harmor
        case 84: return Icon::filter;          // subtractive
        case 90: return Icon::acousticGuitar;  // banjo
        case 94: case 125: return Icon::plugin; // patch, building block
        case 95: return Icon::gear;            // wrench
        case 100: return Icon::notes;          // pencil
        case 103: return Icon::loop;           // scissors (sliced)
        case 106: case 113: case 130: return Icon::folder; // package, project, library
        case 107: return Icon::sidechain;      // magnet
        case 112: return Icon::flute;          // whistle
        case 123: return Icon::drumMachine;    // pads
        case 124: return Icon::knob;           // touchscreen
        case 131: return Icon::clap;           // snap
        case 240: return Icon::clock;          // small clock
        default: return Icon::tag;
    }
}
} // namespace lynx::trackicons
