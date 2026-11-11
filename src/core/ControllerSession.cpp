#include "core/ControllerSession.h"

#include <algorithm>
#include <cmath>

namespace lynx::controller
{
namespace
{
float clamp01 (float v) noexcept { return std::isfinite (v) ? std::fmin (1.0f, std::fmax (0.0f, v)) : 0.0f; }

bool isRing (Encoding e) noexcept { return e != Encoding::plain; }

// a held state shown by an LED (not a position)
bool isLed (Feedback f) noexcept
{
    switch (f)
    {
        case Feedback::mixerVolume: case Feedback::mixerPan: case Feedback::masterVolume: case Feedback::channelVolume:
        case Feedback::channelPan: case Feedback::pluginParam:
            return false;
        default:
            return true;
    }
}
} // namespace

// --- Claims -------------------------------------------------------------------------------------------------------
Claims::Claims (const Profile& profile)
{
    for (const auto& b : profile.bindings)
        for (int e = 0; e < b.message.count; ++e)
        {
            const auto& m = b.message;
            const auto number = m.channelRange ? m.number : m.number + e;
            const auto first = m.channel < 0 ? 0 : (m.channelRange ? m.channel + e : m.channel);
            const auto last = m.channel < 0 ? 15 : first;
            for (int ch = first; ch <= last && ch < 16; ++ch)
                switch (m.kind)
                {
                    case MessageKind::note: if (number < 128) notes[(std::size_t) ch].set ((std::size_t) number); break;
                    case MessageKind::cc: if (number < 128) ccs[(std::size_t) ch].set ((std::size_t) number); break;
                    case MessageKind::bend: bend.set ((std::size_t) ch); break;
                }
        }
}

bool Claims::has (const std::uint8_t* bytes, std::size_t size) const noexcept
{
    if (bytes == nullptr || size < 2)
        return false;
    const auto ch = (std::size_t) (bytes[0] & 0x0F);
    const auto d1 = (std::size_t) (bytes[1] & 0x7F);
    switch (bytes[0] & 0xF0)
    {
        case 0x80: case 0x90: return notes[ch].test (d1);
        case 0xB0: return ccs[ch].test (d1);
        case 0xE0: return bend.test (ch);
        default: return false;
    }
}

bool Claims::takes (const std::uint8_t* bytes, std::size_t size, std::uint64_t linkedLow, std::uint64_t linkedHigh) const noexcept
{
    if (! has (bytes, size))
        return false;
    if ((bytes[0] & 0xF0) == 0xB0)
    {
        const auto cc = bytes[1] & 0x7F;
        if ((((cc & 64) != 0 ? linkedHigh : linkedLow) >> (cc & 63) & 1u) != 0)
            return false; // linked by hand: the link has it
    }
    return true;
}

// --- text -----------------------------------------------------------------------------------------------------------
std::string volumeText (float v) { return std::to_string ((int) std::lround (clamp01 (v) * 100.0f)) + "%"; }

std::string panText (float v)
{
    const auto p = (int) std::lround ((clamp01 (v) - 0.5f) * 200.0f);
    return p == 0 ? std::string ("C") : (p < 0 ? "L" + std::to_string (-p) : "R" + std::to_string (p));
}

std::string displayAscii (const std::string& utf8, int width)
{
    // U+00C0…U+00FF without their accents
    static const char latin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    std::string out;
    for (std::size_t i = 0; i < utf8.size() && (int) out.size() < width;)
    {
        const auto c = (unsigned char) utf8[i];
        std::uint32_t cp = c;
        std::size_t len = 1;
        if (c >= 0xF0) { cp = c & 0x07u; len = 4; }
        else if (c >= 0xE0) { cp = c & 0x0Fu; len = 3; }
        else if (c >= 0xC0) { cp = c & 0x1Fu; len = 2; }
        else if (c >= 0x80) { ++i; continue; } // (a stray continuation byte)
        for (std::size_t k = 1; k < len && i + k < utf8.size(); ++k)
            cp = (cp << 6) | ((unsigned char) utf8[i + k] & 0x3Fu);
        i += len;
        if (cp >= 0x20 && cp < 0x7F)
            out.push_back ((char) cp);
        else if (cp >= 0xC0 && cp <= 0xFF)
            out.push_back (latin[cp - 0xC0]);
        else if (cp >= 0x20)
            out.push_back ('?');
    }
    out.resize ((std::size_t) std::max (0, width), ' ');
    return out;
}

// --- Session --------------------------------------------------------------------------------------------------------
Session::Session (std::shared_ptr<const Profile> profile, int unitOffset)
    : prof (std::move (profile)), offset (unitOffset)
{
    for (const auto& b : prof->bindings)
        pickups.emplace_back ((std::size_t) std::max (1, b.message.count));
    for (const auto& f : prof->feedback)
        sent.emplace_back ((std::size_t) std::max (1, f.message.count), -1);
}

int Session::mixerTrack (int strip, const Surface& surface, const Shared& shared) const noexcept
{
    const auto t = strip == selectedStrip ? surface.selectedMixerTrack() : 1 + shared.mixerBank + offset + strip;
    return t >= 0 && t < surface.mixerTracks() ? t : -1;
}

int Session::channelIndex (int strip, const Surface& surface, const Shared& shared) const noexcept
{
    const auto c = strip == selectedStrip ? surface.selectedChannel() : shared.channelBank + offset + strip;
    return c >= 0 && c < surface.channels() ? c : -1;
}

int Session::paramIndex (int strip, const Surface& surface, const Shared& shared) const noexcept
{
    if (strip < 0)
        return -1;
    const auto i = shared.page * prof->strips + offset + strip;
    return i < surface.pluginParams() ? i : -1;
}

void Session::targetsMoved() noexcept
{
    for (auto& line : pickups)
        for (auto& s : line)
        {
            s.caught = false;
            s.lastSet = -1.0f;
        }
}

void Session::open (const Surface& surface, const Shared& shared, const Send& send)
{
    for (const auto& m : prof->init)
        send (m.data(), m.size());
    for (auto& line : sent)
        std::fill (line.begin(), line.end(), -1);
    shown.clear();
    touched.reset();
    masterTouched = false;
    opened = true;
    refresh (surface, shared, send);
}

void Session::close (const Send& send)
{
    if (! opened)
        return;
    for (std::size_t i = 0; i < prof->feedback.size(); ++i)
    {
        const auto& f = prof->feedback[i];
        if (f.message.kind == MessageKind::bend)
            continue; // (a motor fader stays where it is)
        const auto rest = isLed (f.what) ? f.off : 0;
        for (int e = 0; e < f.message.count; ++e)
            if (sent[i][(std::size_t) e] >= 0 && sent[i][(std::size_t) e] != rest)
                sendValue (f, e, rest, send);
    }
    // the display: blank what was written
    if (! prof->display.write.empty())
    {
        const auto& d = prof->display;
        for (const auto& [place, text] : shown)
        {
            const std::string blank (text.size(), ' ');
            if (text == blank)
                continue;
            std::vector<std::uint8_t> bytes;
            for (const auto token : d.write)
                switch (token)
                {
                    case Display::pos: bytes.push_back ((std::uint8_t) ((place.first * d.cells * d.width + place.second * d.width) & 0x7F)); break;
                    case Display::row: bytes.push_back ((std::uint8_t) (place.first & 0x7F)); break;
                    case Display::cell: bytes.push_back ((std::uint8_t) (place.second & 0x7F)); break;
                    case Display::text: bytes.insert (bytes.end(), blank.begin(), blank.end()); break;
                    default: bytes.push_back ((std::uint8_t) token); break;
                }
            send (bytes.data(), bytes.size());
        }
    }
    for (const auto& m : prof->exit)
        send (m.data(), m.size());
    for (auto& line : sent)
        std::fill (line.begin(), line.end(), -1);
    shown.clear();
    opened = false;
}

bool Session::input (const std::uint8_t* bytes, std::size_t size, Surface& surface, Shared& shared)
{
    if (bytes == nullptr || size < 2)
        return false;
    const auto ch = bytes[0] & 0x0F;
    auto kind = MessageKind::note;
    auto num = bytes[1] & 0x7F;
    auto value = size > 2 ? bytes[2] & 0x7F : 0;
    auto is14 = false;
    switch (bytes[0] & 0xF0)
    {
        case 0x80: value = 0; break;
        case 0x90: break;
        case 0xB0: kind = MessageKind::cc; break;
        case 0xE0:
            if (size < 3)
                return false;
            kind = MessageKind::bend;
            value = (bytes[1] & 0x7F) | ((bytes[2] & 0x7F) << 7);
            num = 0;
            is14 = true;
            break;
        default: return false;
    }
    if (kind == MessageKind::cc && surface.linkedByHand (num, ch))
        return false; // the user's own link comes first

    auto known = false;
    for (std::size_t i = 0; i < prof->bindings.size(); ++i)
    {
        const auto& b = prof->bindings[i];
        const auto e = b.message.element (kind, ch, num);
        if (e < 0)
            continue;
        known = true;
        if ((b.layer >= 0 && b.layer != shared.layer) || (b.value >= 0 && b.value != value))
            continue;
        const auto before = shared;
        auto& state = pickups[i][(std::size_t) e];

        // continuous targets: the value now and how to set it
        auto current = -1.0f;
        std::function<void (float)> set;
        const auto strip = b.strip == selectedStrip ? selectedStrip : b.strip + e;
        switch (b.action)
        {
            case Action::mixerVolume: case Action::mixerPan:
                if (const auto t = mixerTrack (strip, surface, shared); t >= 0)
                {
                    const auto p = b.action == Action::mixerVolume ? Param::volume : Param::pan;
                    current = surface.mixerValue (t, p);
                    set = [&surface, t, p] (float v) { surface.setMixerValue (t, p, v); };
                }
                break;
            case Action::masterVolume:
                if (surface.mixerTracks() > 0)
                {
                    current = surface.mixerValue (0, Param::volume);
                    set = [&surface] (float v) { surface.setMixerValue (0, Param::volume, v); };
                }
                break;
            case Action::channelVolume: case Action::channelPan:
                if (const auto c = channelIndex (strip, surface, shared); c >= 0)
                {
                    const auto p = b.action == Action::channelVolume ? Param::volume : Param::pan;
                    current = surface.channelValue (c, p);
                    set = [&surface, c, p] (float v) { surface.setChannelValue (c, p, v); };
                }
                break;
            case Action::pluginParam:
                if (const auto p = paramIndex (strip, surface, shared); p >= 0)
                {
                    current = surface.pluginParam (p);
                    set = [&surface, p] (float v) { surface.setPluginParam (p, v); };
                }
                break;
            default: break;
        }

        auto delta = 0;
        switch (b.mode)
        {
            case ValueMode::absolute: break;
            case ValueMode::relativeSign: delta = (value & 0x40) != 0 ? -(value & 0x3F) : (value & 0x3F); break;
            case ValueMode::relativeTwos: delta = (value & 0x7F) < 64 ? (value & 0x7F) : (value & 0x7F) - 128; break;
            case ValueMode::relativeOffset: delta = (value & 0x7F) - 64; break;
        }
        const auto pressed = value > 0;

        if (set)
        {
            if (b.mode != ValueMode::absolute)
            {
                if (delta != 0)
                    set (clamp01 (current + (float) delta * b.step));
            }
            else
            {
                const auto v = is14 ? (float) value / 16383.0f : (float) value / 127.0f;
                if (! b.pickup || prefs::pickupUpdate (state, current, v))
                    set (v);
            }
            return true;
        }

        switch (b.action)
        {
            case Action::command:
                if (pressed)
                    surface.command ((Command) b.arg);
                break;
            case Action::jog:
                if (delta != 0)
                    surface.jog (delta);
                break;
            case Action::mixerMute: case Action::mixerSolo: case Action::mixerArm:
                if (const auto t = mixerTrack (strip, surface, shared); t >= 0 && pressed)
                {
                    const auto s = b.action == Action::mixerMute ? Switch::mute : b.action == Action::mixerSolo ? Switch::solo : Switch::arm;
                    surface.setMixerSwitch (t, s, ! surface.mixerSwitch (t, s));
                }
                break;
            case Action::mixerSelect:
                if (const auto t = mixerTrack (strip, surface, shared); t >= 0 && pressed)
                    surface.selectMixerTrack (t);
                break;
            case Action::mixerTouch:
                if (strip >= 0 && strip < (int) touched.size())
                {
                    touched.set ((std::size_t) strip, pressed);
                    if (! pressed)
                        invalidateMotor (Feedback::mixerVolume, strip); // the motor goes to LYNX's value on release
                }
                break;
            case Action::masterTouch:
                masterTouched = pressed;
                if (! pressed)
                    invalidateMotor (Feedback::masterVolume, 0);
                break;
            case Action::mixerBankLeft: case Action::mixerStripLeft:
                if (pressed)
                    shared.mixerBank = std::max (0, shared.mixerBank - (b.action == Action::mixerBankLeft ? shared.strips : 1));
                break;
            case Action::mixerBankRight: case Action::mixerStripRight:
                if (const auto next = shared.mixerBank + (b.action == Action::mixerBankRight ? shared.strips : 1);
                    pressed && next < surface.mixerTracks() - 1)
                    shared.mixerBank = next;
                break;
            case Action::channelMute:
                if (const auto c = channelIndex (strip, surface, shared); c >= 0 && pressed)
                    surface.setChannelMuted (c, ! surface.channelMuted (c));
                break;
            case Action::channelSelect:
                if (const auto c = channelIndex (strip, surface, shared); c >= 0 && pressed)
                    surface.selectChannel (c);
                break;
            case Action::channelBankLeft:
                if (pressed)
                    shared.channelBank = std::max (0, shared.channelBank - shared.strips);
                break;
            case Action::channelBankRight:
                if (pressed && shared.channelBank + shared.strips < surface.channels())
                    shared.channelBank += shared.strips;
                break;
            case Action::channelPrevious: case Action::channelNext:
                if (pressed && surface.channels() > 0)
                    surface.selectChannel (std::clamp (surface.selectedChannel() + (b.action == Action::channelNext ? 1 : -1), 0, surface.channels() - 1));
                break;
            case Action::pluginPageLeft:
                if (pressed)
                    shared.page = std::max (0, shared.page - 1);
                break;
            case Action::pluginPageRight:
                if (pressed && (shared.page + 1) * prof->strips < surface.pluginParams())
                    ++shared.page;
                break;
            case Action::padNote:
                surface.playNote (std::clamp (b.key + e, 0, 127), (float) value / 127.0f, pressed);
                break;
            case Action::padStep:
                if (const auto index = b.strip + e; pressed && index >= 0 && index < surface.steps())
                    surface.setStep (index, ! surface.step (index));
                break;
            case Action::layer:
                if (pressed)
                    shared.layer = b.arg;
                break;
            default: break;
        }
        if (before.mixerBank != shared.mixerBank || before.channelBank != shared.channelBank || before.page != shared.page || before.layer != shared.layer)
            targetsMoved();
        return true;
    }
    return known; // (a line of another layer: taken, nothing to do)
}

void Session::invalidateMotor (Feedback what, int strip) noexcept
{
    for (std::size_t i = 0; i < prof->feedback.size(); ++i)
    {
        const auto& f = prof->feedback[i];
        if (f.what != what)
            continue;
        const auto e = what == Feedback::masterVolume ? 0 : strip - f.strip;
        if (e >= 0 && e < f.message.count)
            sent[i][(std::size_t) e] = -1;
    }
}

int Session::feedbackValue (const FeedbackLine& f, int element, const Surface& surface, const Shared& shared) const
{
    const auto strip = f.strip == selectedStrip ? selectedStrip : f.strip + element;
    auto led = [&f] (bool on) { return on ? f.on : f.off; };
    auto value = -1.0f; // a position 0…1 (−1: nothing there)
    switch (f.what)
    {
        case Feedback::command: return led (surface.lit ((Command) f.arg));
        case Feedback::layer: return led (shared.layer == f.arg);
        case Feedback::mixerMute: case Feedback::mixerSolo: case Feedback::mixerArm:
        {
            const auto t = mixerTrack (strip, surface, shared);
            const auto s = f.what == Feedback::mixerMute ? Switch::mute : f.what == Feedback::mixerSolo ? Switch::solo : Switch::arm;
            return led (t >= 0 && surface.mixerSwitch (t, s));
        }
        case Feedback::mixerSelect:
        {
            const auto t = mixerTrack (strip, surface, shared);
            return led (t >= 0 && t == surface.selectedMixerTrack());
        }
        case Feedback::channelMute:
        {
            const auto c = channelIndex (strip, surface, shared);
            return led (c >= 0 && surface.channelMuted (c));
        }
        case Feedback::channelSelect:
        {
            const auto c = channelIndex (strip, surface, shared);
            return led (c >= 0 && c == surface.selectedChannel());
        }
        case Feedback::padStep:
        {
            const auto index = f.strip + element;
            return led (index >= 0 && index < surface.steps() && surface.step (index));
        }
        case Feedback::mixerVolume: case Feedback::mixerPan:
            if (const auto t = mixerTrack (strip, surface, shared); t >= 0)
                value = surface.mixerValue (t, f.what == Feedback::mixerVolume ? Param::volume : Param::pan);
            break;
        case Feedback::masterVolume:
            if (surface.mixerTracks() > 0)
                value = surface.mixerValue (0, Param::volume);
            break;
        case Feedback::channelVolume: case Feedback::channelPan:
            if (const auto c = channelIndex (strip, surface, shared); c >= 0)
                value = surface.channelValue (c, f.what == Feedback::channelVolume ? Param::volume : Param::pan);
            break;
        case Feedback::pluginParam:
            if (const auto p = paramIndex (strip, surface, shared); p >= 0)
                value = surface.pluginParam (p);
            break;
    }

    if (isRing (f.encoding))
    {
        if (value < 0.0f)
            return 0; // ring off
        const auto v = clamp01 (value);
        switch (f.encoding)
        {
            case Encoding::ringDot: return 1 + (int) std::lround (v * 10.0f);
            case Encoding::ringPan: return 0x10 | (1 + (int) std::lround (v * 10.0f));
            case Encoding::ringWrap: return 0x20 | (1 + (int) std::lround (v * 10.0f));
            case Encoding::ringSpread: return 0x30 | (1 + (int) std::lround (v * 5.0f));
            case Encoding::plain: break;
        }
    }
    const auto v = clamp01 (std::max (0.0f, value));
    if (f.message.kind == MessageKind::bend)
        return (int) std::lround (v * 1023.0f) << 4; // 10 bits, as motor faders resolve
    return (int) std::lround (v * 127.0f);
}

void Session::sendValue (const FeedbackLine& f, int element, int value, const Send& send) const
{
    const auto& m = f.message;
    const auto ch = (std::uint8_t) ((m.channelRange ? m.channel + element : m.channel) & 0x0F);
    const auto num = (std::uint8_t) ((m.channelRange ? m.number : m.number + element) & 0x7F);
    std::uint8_t bytes[3] {};
    switch (m.kind)
    {
        case MessageKind::note: bytes[0] = (std::uint8_t) (0x90 | ch); bytes[1] = num; bytes[2] = (std::uint8_t) (value & 0x7F); break;
        case MessageKind::cc: bytes[0] = (std::uint8_t) (0xB0 | ch); bytes[1] = num; bytes[2] = (std::uint8_t) (value & 0x7F); break;
        case MessageKind::bend:
            bytes[0] = (std::uint8_t) (0xE0 | ch);
            bytes[1] = (std::uint8_t) (value & 0x7F);
            bytes[2] = (std::uint8_t) ((value >> 7) & 0x7F);
            break;
    }
    send (bytes, 3);
}

std::string Session::displayText (const DisplayRow& row, int cell, const Surface& surface, const Shared& shared) const
{
    switch (row.what)
    {
        case DisplayText::mixerName: case DisplayText::mixerVolume: case DisplayText::mixerPan:
        {
            const auto t = mixerTrack (cell, surface, shared);
            if (t < 0)
                return {};
            return row.what == DisplayText::mixerName ? surface.mixerName (t)
                 : row.what == DisplayText::mixerVolume ? volumeText (surface.mixerValue (t, Param::volume)) : panText (surface.mixerValue (t, Param::pan));
        }
        case DisplayText::channelName: case DisplayText::channelVolume: case DisplayText::channelPan:
        {
            const auto c = channelIndex (cell, surface, shared);
            if (c < 0)
                return {};
            return row.what == DisplayText::channelName ? surface.channelName (c)
                 : row.what == DisplayText::channelVolume ? volumeText (surface.channelValue (c, Param::volume)) : panText (surface.channelValue (c, Param::pan));
        }
        case DisplayText::pluginParamName: case DisplayText::pluginParamValue:
        {
            const auto p = paramIndex (cell, surface, shared);
            if (p < 0)
                return {};
            return row.what == DisplayText::pluginParamName ? surface.pluginParamName (p) : volumeText (surface.pluginParam (p));
        }
        case DisplayText::selectedMixerName:
        {
            const auto t = surface.selectedMixerTrack();
            return t >= 0 && t < surface.mixerTracks() ? surface.mixerName (t) : std::string();
        }
        case DisplayText::selectedChannelName:
        {
            const auto c = surface.selectedChannel();
            return c >= 0 && c < surface.channels() ? surface.channelName (c) : std::string();
        }
        case DisplayText::pluginName: return surface.pluginName();
        case DisplayText::layerName: return shared.layer >= 0 && shared.layer < (int) prof->layers.size() ? prof->layers[(std::size_t) shared.layer] : std::string();
        case DisplayText::text: return row.text;
    }
    return {};
}

void Session::refresh (const Surface& surface, const Shared& shared, const Send& send)
{
    if (! opened)
        return;
    for (std::size_t i = 0; i < prof->feedback.size(); ++i)
    {
        const auto& f = prof->feedback[i];
        if (f.layer >= 0 && f.layer != shared.layer)
        {
            std::fill (sent[i].begin(), sent[i].end(), -1); // (sent again when its layer comes back)
            continue;
        }
        for (int e = 0; e < f.message.count; ++e)
        {
            if (f.what == Feedback::mixerVolume && f.strip + e >= 0 && f.strip + e < (int) touched.size() && touched.test ((std::size_t) (f.strip + e)))
                continue; // a held fader is the user's
            if (f.what == Feedback::masterVolume && masterTouched)
                continue;
            const auto v = feedbackValue (f, e, surface, shared);
            if (v == sent[i][(std::size_t) e])
                continue;
            sent[i][(std::size_t) e] = v;
            sendValue (f, e, v, send);
        }
    }

    const auto& d = prof->display;
    if (d.write.empty())
        return;
    for (const auto& row : d.rows)
    {
        if (row.layer >= 0 && row.layer != shared.layer)
            continue;
        const auto perCell = row.what <= DisplayText::pluginParamValue;
        const auto cells = perCell ? d.cells : 1;
        const auto width = perCell ? d.width : d.width * d.cells;
        for (int c = 0; c < cells; ++c)
        {
            // (cells side by side keep a gap between their texts)
            auto text = perCell && d.cells > 1 ? displayAscii (displayText (row, c, surface, shared), width - 1) + " "
                                               : displayAscii (displayText (row, c, surface, shared), width);
            auto& last = shown[{ row.row, perCell ? c : 0 }];
            if (! perCell) // a whole row over cells drawn before: forget them
                for (int k = 1; k < d.cells; ++k)
                    shown.erase ({ row.row, k });
            if (text == last)
                continue;
            last = text;
            std::vector<std::uint8_t> bytes;
            for (const auto token : d.write)
                switch (token)
                {
                    case Display::pos: bytes.push_back ((std::uint8_t) ((row.row * d.cells * d.width + c * d.width) & 0x7F)); break;
                    case Display::row: bytes.push_back ((std::uint8_t) (row.row & 0x7F)); break;
                    case Display::cell: bytes.push_back ((std::uint8_t) (c & 0x7F)); break;
                    case Display::text: bytes.insert (bytes.end(), text.begin(), text.end()); break;
                    default: bytes.push_back ((std::uint8_t) token); break;
                }
            send (bytes.data(), bytes.size());
        }
    }
}

// --- Hub ------------------------------------------------------------------------------------------------------------
namespace
{
std::string groupKey (const Hub::Device& d) { return d.profile->group.empty() ? std::string ("\x01") + d.input : d.profile->group; }
} // namespace

void Hub::configure (const std::vector<Device>& devices, const Surface& surface, const DeviceSend& send)
{
    // the wanted sessions: each unit's offset within its group
    struct Wanted
    {
        Device device;
        std::string group;
        int offset = 0;
    };
    std::vector<Wanted> wanted;
    std::map<std::string, int> totals;
    for (const auto& d : devices)
    {
        if (d.profile == nullptr || d.input.empty())
            continue;
        if (std::any_of (wanted.begin(), wanted.end(), [&] (const Wanted& w) { return w.device.input == d.input; }))
            continue;
        const auto key = groupKey (d);
        wanted.push_back ({ d, key, totals[key] });
        totals[key] += d.profile->strips;
    }

    auto sendTo = [&send] (const std::string& input) {
        return Send ([&send, input] (const std::uint8_t* bytes, std::size_t size) {
            if (send)
                send (input, bytes, size);
        });
    };

    std::vector<Entry> next;
    std::vector<std::size_t> fresh;
    for (auto& w : wanted)
    {
        const auto old = std::find_if (entries.begin(), entries.end(), [&] (const Entry& e) {
            return e.session != nullptr && e.input == w.device.input && e.group == w.group && &e.session->profile() == w.device.profile.get()
                   && e.session->unitOffset() == w.offset;
        });
        if (old != entries.end())
            next.push_back (std::move (*old));
        else
        {
            fresh.push_back (next.size());
            next.push_back ({ w.device.input, w.group, std::make_unique<Session> (w.device.profile, w.offset) });
        }
    }
    for (auto& e : entries) // the ones that go
        if (e.session != nullptr)
            e.session->close (sendTo (e.input));
    entries = std::move (next);

    for (auto it = groups.begin(); it != groups.end();)
        it = totals.count (it->first) != 0 ? std::next (it) : groups.erase (it);
    for (const auto& [key, strips] : totals)
        groups[key].strips = strips;
    for (const auto i : fresh)
        entries[i].session->open (surface, groups[entries[i].group], sendTo (entries[i].input));
}

void Hub::closeAll (const DeviceSend& send)
{
    for (auto& e : entries)
        e.session->close ([&] (const std::uint8_t* bytes, std::size_t size) {
            if (send)
                send (e.input, bytes, size);
        });
    entries.clear();
    groups.clear();
}

bool Hub::has (const std::string& input) const noexcept { return session (input) != nullptr; }

const Session* Hub::session (const std::string& input) const noexcept
{
    for (const auto& e : entries)
        if (e.input == input)
            return e.session.get();
    return nullptr;
}

const Shared* Hub::shared (const std::string& group) const noexcept
{
    const auto it = groups.find (group);
    return it != groups.end() ? &it->second : nullptr;
}

bool Hub::input (const std::string& device, const std::uint8_t* bytes, std::size_t size, Surface& surface)
{
    for (auto& e : entries)
        if (e.input == device)
        {
            auto& shared = groups[e.group];
            const auto before = shared;
            const auto handled = e.session->input (bytes, size, surface, shared);
            if (before.mixerBank != shared.mixerBank || before.channelBank != shared.channelBank || before.page != shared.page || before.layer != shared.layer)
                for (auto& other : entries)
                    if (other.group == e.group)
                        other.session->targetsMoved();
            return handled;
        }
    return false;
}

void Hub::refresh (const Surface& surface, const DeviceSend& send)
{
    for (auto& e : entries)
        e.session->refresh (surface, groups[e.group], [&] (const std::uint8_t* bytes, std::size_t size) {
            if (send)
                send (e.input, bytes, size);
        });
}
} // namespace lynx::controller
