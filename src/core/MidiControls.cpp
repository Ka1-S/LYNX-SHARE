#include "core/MidiControls.h"

#include "core/MidiOut.h"

#include <algorithm>
#include <cstring>

namespace lynx::midictl
{
const char* ccName (int cc) noexcept
{
    switch (cc)
    {
        case 0: return "Bank select MSB";
        case 1: return "Modulation wheel";
        case 2: return "Breath controller";
        case 4: return "Foot controller";
        case 5: return "Portamento time";
        case 6: return "Data entry MSB";
        case 7: return "Main volume";
        case 8: return "Balance";
        case 10: return "Pan";
        case 11: return "Expression controller";
        case 12: return "Control 1";
        case 13: return "Control 2";
        case 16: return "General purpose controller 1";
        case 17: return "General purpose controller 2";
        case 18: return "General purpose controller 3";
        case 19: return "General purpose controller 4";
        case 32: return "Bank select LSB";
        case 33: return "Modulation wheel LSB";
        case 34: return "Breath controller LSB";
        case 36: return "Foot controller LSB";
        case 37: return "Portamento time LSB";
        case 38: return "Data entry LSB";
        case 39: return "Main volume LSB";
        case 40: return "Balance LSB";
        case 42: return "Pan LSB";
        case 43: return "Expression controller LSB";
        case 44: return "Control 1 LSB";
        case 45: return "Control 2 LSB";
        case 48: return "General purpose controller 1 LSB";
        case 49: return "General purpose controller 2 LSB";
        case 50: return "General purpose controller 3 LSB";
        case 51: return "General purpose controller 4 LSB";
        case 64: return "Damper pedal (sustain)";
        case 65: return "Portamento";
        case 66: return "Sostenuto";
        case 67: return "Soft pedal";
        case 68: return "Legato footswitch";
        case 69: return "Hold 2";
        case 70: return "Sound variation";
        case 71: return "Harmonic content";
        case 72: return "Release time";
        case 73: return "Attack time";
        case 74: return "Brightness";
        case 75: return "Sound controller 6";
        case 76: return "Sound controller 7";
        case 77: return "Sound controller 8";
        case 78: return "Sound controller 9";
        case 79: return "Sound controller 10";
        case 80: return "General purpose controller 5";
        case 81: return "General purpose controller 6";
        case 82: return "General purpose controller 7";
        case 83: return "General purpose controller 8";
        case 84: return "Portamento control";
        case 91: return "Reverb depth";
        case 92: return "Tremolo depth";
        case 93: return "Chorus depth";
        case 94: return "Celeste depth";
        case 95: return "Phaser depth";
        case 96: return "Data increment";
        case 97: return "Data decrement";
        case 98: return "NRPN LSB";
        case 99: return "NRPN MSB";
        case 100: return "RPN LSB";
        case 101: return "RPN MSB";
        case 120: return "All sound off";
        case 121: return "Reset all controllers";
        case 122: return "Local control on/off";
        case 123: return "All notes off";
        case 124: return "Omni mode off";
        case 125: return "Omni mode on";
        case 126: return "Mono mode on";
        case 127: return "Poly mode on";
        default: return "";
    }
}

std::string paramName (int param)
{
    if (isCc (param))
    {
        const auto cc = param - ccBase;
        std::string name = "MIDI CC #" + std::to_string (cc);
        if (const std::string standard = ccName (cc); ! standard.empty())
            name += " (" + standard + ")";
        return name;
    }
    if (isPressure (param))
        return "MIDI Channel " + std::to_string (param - pressureBase + 1) + " Aftertouch";
    if (isPitchBend (param))
        return "MIDI Channel " + std::to_string (param - pitchBendBase + 1) + " Pitch Bend";
    return {};
}
MidiOutSettings parseMidiOut (const std::vector<std::uint8_t>& d) noexcept
{
    MidiOutSettings s;
    try
    {
        if (const auto state = midiout::parse (d))
        {
            s.channel = state->channel;
            s.port = state->port;
            s.program = state->program;
            s.bankMsb = state->bankMsb;
            s.bankLsb = state->bankLsb;
        }
    }
    catch (...)
    {
    }
    return s;
}

void writeMidiOut (std::vector<std::uint8_t>& d, const MidiOutSettings& s)
{
    // (the whole FL state is kept: controls, pages, options; round 167i)
    auto state = midiout::stateOf (d);
    state.channel = std::clamp (s.channel, 0, 15);
    state.port = std::clamp (s.port, 0, 255);
    state.program = std::clamp (s.program, 0, 128);
    state.bankMsb = std::clamp (s.bankMsb, -1, 127);
    state.bankLsb = std::clamp (s.bankLsb, -1, 127);
    d = midiout::write (state);
}
} // namespace lynx::midictl
