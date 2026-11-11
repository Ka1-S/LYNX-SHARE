#include "core/ControllerProfile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>

#include "core/ControllerProfilesData.h"

namespace lynx::controller
{
namespace
{
std::string trim (const std::string& s)
{
    auto a = s.find_first_not_of (" \t\r\n");
    if (a == std::string::npos)
        return {};
    auto b = s.find_last_not_of (" \t\r\n");
    return s.substr (a, b - a + 1);
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::vector<std::string> words (const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream in (s);
    std::string w;
    while (in >> w)
        out.push_back (w);
    return out;
}

std::vector<std::string> list (const std::string& s)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size())
    {
        auto p = s.find (',', start);
        if (p == std::string::npos)
            p = s.size();
        if (auto item = trim (s.substr (start, p - start)); ! item.empty())
            out.push_back (item);
        start = p + 1;
    }
    return out;
}

std::optional<int> number (const std::string& s, int base = 10)
{
    if (s.empty())
        return std::nullopt;
    auto text = s;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        text = text.substr (2);
        base = 16;
    }
    char* end = nullptr;
    const auto v = std::strtol (text.c_str(), &end, base);
    if (end == text.c_str() || *end != 0 || v < -100000 || v > 100000)
        return std::nullopt;
    return (int) v;
}

// "5" or "3-10": first and count
bool range (const std::string& s, int low, int high, int& first, int& count)
{
    const auto dash = s.find ('-', 1);
    const auto a = number (dash == std::string::npos ? s : s.substr (0, dash));
    const auto b = dash == std::string::npos ? a : number (s.substr (dash + 1));
    if (! a || ! b || *a < low || *b > high || *b < *a)
        return false;
    first = *a;
    count = *b - *a + 1;
    return true;
}

struct Named
{
    const char* name;
    int value;
    int arg;
};

const Named actions[] = {
    { "transport.play", (int) Action::command, (int) Command::play },
    { "transport.stop", (int) Action::command, (int) Command::stop },
    { "transport.record", (int) Action::command, (int) Command::record },
    { "transport.loop", (int) Action::command, (int) Command::loop },
    { "transport.metronome", (int) Action::command, (int) Command::metronome },
    { "transport.tap", (int) Action::command, (int) Command::tapTempo },
    { "transport.rewind", (int) Action::command, (int) Command::rewind },
    { "transport.forward", (int) Action::command, (int) Command::forward },
    { "edit.undo", (int) Action::command, (int) Command::undo },
    { "pattern.previous", (int) Action::command, (int) Command::previousPattern },
    { "pattern.next", (int) Action::command, (int) Command::nextPattern },
    { "transport.jog", (int) Action::jog, 0 },
    { "mixer.volume", (int) Action::mixerVolume, 0 },
    { "mixer.pan", (int) Action::mixerPan, 0 },
    { "mixer.mute", (int) Action::mixerMute, 0 },
    { "mixer.solo", (int) Action::mixerSolo, 0 },
    { "mixer.arm", (int) Action::mixerArm, 0 },
    { "mixer.select", (int) Action::mixerSelect, 0 },
    { "mixer.touch", (int) Action::mixerTouch, 0 },
    { "master.volume", (int) Action::masterVolume, 0 },
    { "master.touch", (int) Action::masterTouch, 0 },
    { "mixer.bank.left", (int) Action::mixerBankLeft, 0 },
    { "mixer.bank.right", (int) Action::mixerBankRight, 0 },
    { "mixer.strip.left", (int) Action::mixerStripLeft, 0 },
    { "mixer.strip.right", (int) Action::mixerStripRight, 0 },
    { "channel.volume", (int) Action::channelVolume, 0 },
    { "channel.pan", (int) Action::channelPan, 0 },
    { "channel.mute", (int) Action::channelMute, 0 },
    { "channel.select", (int) Action::channelSelect, 0 },
    { "channel.bank.left", (int) Action::channelBankLeft, 0 },
    { "channel.bank.right", (int) Action::channelBankRight, 0 },
    { "channel.previous", (int) Action::channelPrevious, 0 },
    { "channel.next", (int) Action::channelNext, 0 },
    { "plugin.param", (int) Action::pluginParam, 0 },
    { "plugin.page.left", (int) Action::pluginPageLeft, 0 },
    { "plugin.page.right", (int) Action::pluginPageRight, 0 },
    { "pad.note", (int) Action::padNote, 0 },
    { "pad.step", (int) Action::padStep, 0 },
    { "layer", (int) Action::layer, 0 },
};

const Named feedbacks[] = {
    { "transport.play", (int) Feedback::command, (int) Command::play },
    { "transport.stop", (int) Feedback::command, (int) Command::stop },
    { "transport.record", (int) Feedback::command, (int) Command::record },
    { "transport.loop", (int) Feedback::command, (int) Command::loop },
    { "transport.metronome", (int) Feedback::command, (int) Command::metronome },
    { "mixer.volume", (int) Feedback::mixerVolume, 0 },
    { "mixer.pan", (int) Feedback::mixerPan, 0 },
    { "mixer.mute", (int) Feedback::mixerMute, 0 },
    { "mixer.solo", (int) Feedback::mixerSolo, 0 },
    { "mixer.arm", (int) Feedback::mixerArm, 0 },
    { "mixer.select", (int) Feedback::mixerSelect, 0 },
    { "master.volume", (int) Feedback::masterVolume, 0 },
    { "channel.volume", (int) Feedback::channelVolume, 0 },
    { "channel.pan", (int) Feedback::channelPan, 0 },
    { "channel.mute", (int) Feedback::channelMute, 0 },
    { "channel.select", (int) Feedback::channelSelect, 0 },
    { "plugin.param", (int) Feedback::pluginParam, 0 },
    { "pad.step", (int) Feedback::padStep, 0 },
    { "layer", (int) Feedback::layer, 0 },
};

const Named displayTexts[] = {
    { "mixer.name", (int) DisplayText::mixerName, 0 },
    { "mixer.volume", (int) DisplayText::mixerVolume, 0 },
    { "mixer.pan", (int) DisplayText::mixerPan, 0 },
    { "channel.name", (int) DisplayText::channelName, 0 },
    { "channel.volume", (int) DisplayText::channelVolume, 0 },
    { "channel.pan", (int) DisplayText::channelPan, 0 },
    { "plugin.param.name", (int) DisplayText::pluginParamName, 0 },
    { "plugin.param.value", (int) DisplayText::pluginParamValue, 0 },
    { "mixer.selected.name", (int) DisplayText::selectedMixerName, 0 },
    { "channel.selected.name", (int) DisplayText::selectedChannelName, 0 },
    { "plugin.name", (int) DisplayText::pluginName, 0 },
    { "layer.name", (int) DisplayText::layerName, 0 },
};

template <std::size_t N>
const Named* findNamed (const Named (&table)[N], const std::string& name)
{
    for (const auto& n : table)
        if (name == n.name)
            return &n;
    return nullptr;
}

bool takesStrip (Action a) noexcept
{
    switch (a)
    {
        case Action::mixerVolume: case Action::mixerPan: case Action::mixerMute: case Action::mixerSolo: case Action::mixerArm:
        case Action::mixerSelect: case Action::mixerTouch: case Action::channelVolume: case Action::channelPan: case Action::channelMute:
        case Action::channelSelect: case Action::pluginParam: case Action::padNote: case Action::padStep:
            return true;
        default:
            return false;
    }
}

bool takesStrip (Feedback f) noexcept { return f != Feedback::command && f != Feedback::masterVolume && f != Feedback::layer; }

struct Parser
{
    Profile& p;
    std::vector<std::string>& errors;
    int line = 0;

    void error (const std::string& what) { errors.push_back ("line " + std::to_string (line) + ": " + what); }

    int layerIndex (const std::string& name)
    {
        for (std::size_t i = 0; i < p.layers.size(); ++i)
            if (lower (p.layers[i]) == lower (name))
                return (int) i;
        error ("unknown layer '" + name + "' (list it in [device] layers = ...)");
        return -1;
    }

    // "note 0-7 ch 1", "cc 16", "bend ch 1-8"; leaves the words it did not use in `rest`
    bool message (const std::vector<std::string>& w, MessageRange& m, std::vector<std::string>& rest, bool needChannel)
    {
        if (w.empty())
        {
            error ("a message is missing (note, cc or bend)");
            return false;
        }
        const auto kind = lower (w[0]);
        std::size_t i = 1;
        m = {};
        if (kind == "note")
            m.kind = MessageKind::note;
        else if (kind == "cc")
            m.kind = MessageKind::cc;
        else if (kind == "bend")
            m.kind = MessageKind::bend;
        else
        {
            error ("unknown message '" + w[0] + "' (note, cc or bend)");
            return false;
        }
        if (m.kind != MessageKind::bend)
        {
            if (i >= w.size() || ! range (w[i], 0, 127, m.number, m.count))
            {
                error ("'" + kind + "' needs a number 0-127 or a range like 16-23");
                return false;
            }
            ++i;
        }
        if (i + 1 < w.size() && lower (w[i]) == "ch")
        {
            int first = 0, count = 1;
            if (! range (w[i + 1], 1, 16, first, count))
            {
                error ("'ch' needs a MIDI channel 1-16 or a range like 1-8");
                return false;
            }
            m.channel = first - 1;
            if (count > 1)
            {
                if (m.count > 1)
                {
                    error ("a line can have a range of numbers or of channels, not both");
                    return false;
                }
                m.count = count;
                m.channelRange = true;
            }
            i += 2;
        }
        else if (needChannel)
        {
            error ("the MIDI channel is missing (ch 1-16)");
            return false;
        }
        rest.assign (w.begin() + (std::ptrdiff_t) i, w.end());
        return true;
    }

    // "1", "selected" (strips are 1-based in the file)
    bool strip (const std::string& s, int& out)
    {
        if (lower (s) == "selected")
        {
            out = selectedStrip;
            return true;
        }
        if (const auto n = number (s); n && *n >= 1 && *n <= 256)
        {
            out = *n - 1;
            return true;
        }
        error ("'" + s + "' is not a strip (a number from 1, or 'selected')");
        return false;
    }

    static bool option (const std::string& word, std::string& key, std::string& value)
    {
        const auto eq = word.find ('=');
        if (eq == std::string::npos || eq == 0)
            return false;
        key = lower (word.substr (0, eq));
        value = word.substr (eq + 1);
        return true;
    }

    void input (const std::string& lhs, const std::string& rhs)
    {
        Binding b;
        b.line = line;
        std::vector<std::string> rest;
        if (! message (words (lhs), b.message, rest, false))
            return;
        if (rest.size() == 2 && lower (rest[0]) == "value")
        {
            const auto v = number (rest[1]);
            if (! v || *v < 0 || *v > 127)
                return error ("'value' needs a number 0-127");
            b.value = *v;
        }
        else if (! rest.empty())
            return error ("unexpected '" + rest[0] + "'");

        const auto w = words (rhs);
        if (w.empty())
            return error ("the action is missing");
        const auto* named = findNamed (actions, lower (w[0]));
        if (named == nullptr)
            return error ("unknown action '" + w[0] + "'");
        b.action = (Action) named->value;
        b.arg = named->arg;
        if (b.message.kind == MessageKind::bend)
            b.pickup = false; // (motor faders; "pickup=on" turns it on)
        std::size_t i = 1;
        std::string key, value;
        if (b.action == Action::layer)
        {
            if (i >= w.size())
                return error ("'layer' needs a layer name");
            b.arg = layerIndex (w[i++]);
            if (b.arg < 0)
                return;
        }
        else if (i < w.size() && ! option (w[i], key, value))
        {
            if (! takesStrip (b.action))
                return error ("'" + w[0] + "' takes no strip");
            if (! strip (w[i++], b.strip))
                return;
        }
        for (; i < w.size(); ++i)
        {
            if (! option (w[i], key, value))
                return error ("unexpected '" + w[i] + "'");
            const auto v = lower (value);
            if (key == "mode")
            {
                if (v == "abs" || v == "absolute") b.mode = ValueMode::absolute;
                else if (v == "sign") b.mode = ValueMode::relativeSign;
                else if (v == "twos") b.mode = ValueMode::relativeTwos;
                else if (v == "offset") b.mode = ValueMode::relativeOffset;
                else return error ("unknown mode '" + value + "' (abs, sign, twos, offset)");
            }
            else if (key == "pickup")
            {
                if (v != "on" && v != "off")
                    return error ("pickup is on or off");
                b.pickup = v == "on";
            }
            else if (key == "step")
            {
                char* end = nullptr;
                const auto s = std::strtod (value.c_str(), &end);
                if (end == value.c_str() || *end != 0 || ! (s > 0.0) || s > 1.0)
                    return error ("step is a number above 0, up to 1");
                b.step = (float) s;
            }
            else if (key == "layer")
            {
                b.layer = layerIndex (value);
                if (b.layer < 0)
                    return;
            }
            else if (key == "key")
            {
                const auto k = number (value);
                if (! k || *k < 0 || *k > 127)
                    return error ("key is a note number 0-127");
                b.key = *k;
            }
            else
                return error ("unknown option '" + key + "'");
        }
        if (b.strip == selectedStrip && b.message.count > 1)
            return error ("'selected' cannot be used with a range");
        p.bindings.push_back (b);
    }

    void feedback (const std::string& lhs, const std::string& rhs)
    {
        FeedbackLine f;
        f.line = line;
        const auto l = words (lhs);
        if (l.empty())
            return error ("the state is missing");
        const auto* named = findNamed (feedbacks, lower (l[0]));
        if (named == nullptr)
            return error ("unknown state '" + l[0] + "'");
        f.what = (Feedback) named->value;
        f.arg = named->arg;
        if (f.what == Feedback::layer)
        {
            if (l.size() != 2)
                return error ("'layer' needs a layer name");
            f.arg = layerIndex (l[1]);
            if (f.arg < 0)
                return;
        }
        else if (l.size() == 2)
        {
            if (! takesStrip (f.what))
                return error ("'" + l[0] + "' takes no strip");
            if (! strip (l[1], f.strip))
                return;
        }
        else if (l.size() > 2)
            return error ("unexpected '" + l[2] + "'");

        std::vector<std::string> rest;
        if (! message (words (rhs), f.message, rest, true))
            return;
        for (const auto& word : rest)
        {
            std::string key, value;
            if (! option (word, key, value))
                return error ("unexpected '" + word + "'");
            const auto v = lower (value);
            if (key == "on" || key == "off")
            {
                const auto n = number (value);
                if (! n || *n < 0 || *n > 127)
                    return error (key + " is a value 0-127");
                (key == "on" ? f.on : f.off) = *n;
            }
            else if (key == "ring")
            {
                if (v == "dot") f.encoding = Encoding::ringDot;
                else if (v == "pan") f.encoding = Encoding::ringPan;
                else if (v == "wrap") f.encoding = Encoding::ringWrap;
                else if (v == "spread") f.encoding = Encoding::ringSpread;
                else return error ("unknown ring '" + value + "' (dot, pan, wrap, spread)");
            }
            else if (key == "layer")
            {
                f.layer = layerIndex (value);
                if (f.layer < 0)
                    return;
            }
            else
                return error ("unknown option '" + key + "'");
        }
        if (f.strip == selectedStrip && f.message.count > 1)
            return error ("'selected' cannot be used with a range");
        p.feedback.push_back (f);
    }

    bool bytes (const std::string& s, std::vector<std::uint8_t>& out)
    {
        for (const auto& w : words (s))
        {
            const auto v = number (w, 16);
            if (! v || *v < 0 || *v > 255)
            {
                error ("'" + w + "' is not a byte (two hex digits)");
                return false;
            }
            out.push_back ((std::uint8_t) *v);
        }
        if (out.empty())
            error ("the bytes are missing");
        return ! out.empty();
    }

    void display (const std::string& lhs, const std::string& rhs)
    {
        const auto l = words (lower (lhs));
        if (l.empty())
            return;
        if (l[0] == "write")
        {
            p.display.write.clear();
            for (const auto& w : words (rhs))
            {
                const auto t = lower (w);
                if (t == "{pos}") p.display.write.push_back (Display::pos);
                else if (t == "{row}") p.display.write.push_back (Display::row);
                else if (t == "{cell}") p.display.write.push_back (Display::cell);
                else if (t == "{text}") p.display.write.push_back (Display::text);
                else if (const auto v = number (w, 16); v && *v >= 0 && *v <= 255) p.display.write.push_back (*v);
                else return error ("'" + w + "' is not a byte or {pos}, {row}, {cell}, {text}");
            }
        }
        else if (l[0] == "width" || l[0] == "cells")
        {
            const auto n = number (trim (rhs));
            if (! n || *n < 1 || *n > 64)
                return error (l[0] + " is a number 1-64");
            (l[0] == "width" ? p.display.width : p.display.cells) = *n;
        }
        else if (l[0] == "row" && l.size() == 2)
        {
            const auto n = number (l[1]);
            if (! n || *n < 1 || *n > 16)
                return error ("'row' needs a row number from 1");
            DisplayRow row;
            row.row = *n - 1;
            auto value = trim (rhs);
            // an option at the end: layer=<name>
            if (const auto at = value.rfind (" layer="); at != std::string::npos)
            {
                row.layer = layerIndex (trim (value.substr (at + 7)));
                if (row.layer < 0)
                    return;
                value = trim (value.substr (0, at));
            }
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            {
                row.what = DisplayText::text;
                row.text = value.substr (1, value.size() - 2);
            }
            else if (const auto* named = findNamed (displayTexts, lower (value)))
                row.what = (DisplayText) named->value;
            else
                return error ("unknown display text '" + value + "'");
            p.display.rows.push_back (row);
        }
        else
            error ("unknown display setting '" + lhs + "'");
    }
};
} // namespace

int MessageRange::element (MessageKind k, int ch, int num) const noexcept
{
    if (k != kind)
        return -1;
    if (channelRange)
    {
        if (kind != MessageKind::bend && num != number)
            return -1;
        const auto i = ch - channel;
        return i >= 0 && i < count ? i : -1;
    }
    if (channel >= 0 && ch != channel)
        return -1;
    if (kind == MessageKind::bend)
        return 0;
    const auto i = num - number;
    return i >= 0 && i < count ? i : -1;
}

ParseResult parseProfile (const std::string& text, const std::string& id, const Library* base)
{
    ParseResult result;
    auto& p = result.profile;
    p.id = id;
    Parser parser { p, result.errors };
    std::string section;
    auto any = false;

    std::istringstream in (text);
    std::string raw;
    while (std::getline (in, raw))
    {
        ++parser.line;
        if (parser.line == 1 && raw.size() >= 3 && (unsigned char) raw[0] == 0xEF && (unsigned char) raw[1] == 0xBB && (unsigned char) raw[2] == 0xBF)
            raw = raw.substr (3); // (a UTF-8 byte order mark)
        auto line = trim (raw);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[')
        {
            if (line.back() != ']')
            {
                parser.error ("a section name ends with ']'");
                continue;
            }
            section = lower (trim (line.substr (1, line.size() - 2)));
            if (section != "device" && section != "init" && section != "exit" && section != "input" && section != "feedback" && section != "display")
                parser.error ("unknown section [" + section + "]");
            continue;
        }
        if (section != "device") // (a source or a name may hold a '#')
            if (const auto hash = line.find (" #"); hash != std::string::npos)
                line = trim (line.substr (0, hash));
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
        {
            parser.error ("'=' is missing");
            continue;
        }
        const auto lhs = trim (line.substr (0, eq)), rhs = trim (line.substr (eq + 1));
        any = true;

        if (section == "device")
        {
            const auto key = lower (lhs);
            if (key == "extends")
            {
                const auto parent = base != nullptr ? base->find (rhs) : nullptr;
                if (parent == nullptr)
                {
                    parser.error ("extends: no profile '" + rhs + "'");
                    continue;
                }
                const auto name = p.name, maker = p.maker;
                const auto match = p.match, exclude = p.exclude;
                auto sources = p.sources;
                p = *parent;
                p.id = id;
                p.name = name;
                p.maker = maker;
                p.match = match;
                p.exclude = exclude;
                sources.insert (sources.end(), parent->sources.begin(), parent->sources.end());
                p.sources = sources;
                p.user = false;
            }
            else if (key == "name") p.name = rhs;
            else if (key == "maker") p.maker = rhs;
            else if (key == "source") p.sources.push_back (rhs);
            else if (key == "group") p.group = rhs;
            else if (key == "match" || key == "exclude")
            {
                auto& target = key == "match" ? p.match : p.exclude;
                for (auto& item : list (rhs))
                    target.push_back (item);
            }
            else if (key == "layers")
            {
                if (! p.bindings.empty() || ! p.feedback.empty() || ! p.display.rows.empty())
                    parser.error ("layers must come before the lines that use them");
                else
                    p.layers = list (rhs);
            }
            else if (key == "strips")
            {
                const auto n = number (rhs);
                if (! n || *n < 1 || *n > 64)
                    parser.error ("strips is a number 1-64");
                else
                    p.strips = *n;
            }
            else
                parser.error ("unknown device setting '" + lhs + "'");
        }
        else if (section == "init" || section == "exit")
        {
            if (lower (lhs) != "send")
            {
                parser.error ("expected 'send = <hex bytes>'");
                continue;
            }
            std::vector<std::uint8_t> bytes;
            if (parser.bytes (rhs, bytes))
                (section == "init" ? p.init : p.exit).push_back (std::move (bytes));
        }
        else if (section == "input")
            parser.input (lhs, rhs);
        else if (section == "feedback")
            parser.feedback (lhs, rhs);
        else if (section == "display")
            parser.display (lhs, rhs);
        else
            parser.error ("a line before the first section");
    }

    if (! any)
        result.errors.push_back ("the file is empty");
    else if (p.name.empty())
        result.errors.push_back ("[device] name is missing");
    if (! p.display.rows.empty() && p.display.write.empty())
        result.errors.push_back ("[display] write is missing");
    return result;
}

namespace
{
// '*' matches any run of characters; the rest compares without case
bool wild (const char* pattern, const char* text) noexcept
{
    for (; *pattern != 0; ++pattern, ++text)
    {
        if (*pattern == '*')
        {
            while (*pattern == '*')
                ++pattern;
            if (*pattern == 0)
                return true;
            for (; *text != 0; ++text)
                if (wild (pattern, text))
                    return true;
            return false;
        }
        if (*text == 0 || std::tolower ((unsigned char) *pattern) != std::tolower ((unsigned char) *text))
            return false;
    }
    return *text == 0;
}
} // namespace

int matchScore (const std::vector<std::string>& patterns, const std::string& portName)
{
    auto best = 0;
    for (const auto& pattern : patterns)
        if (wild (pattern.c_str(), portName.c_str()))
            best = std::max (best, 1 + (int) std::count_if (pattern.begin(), pattern.end(), [] (char c) { return c != '*'; }));
    return best;
}

bool Library::add (const std::string& text, const std::string& id, bool user)
{
    auto parsed = parseProfile (text, id, this);
    if (! parsed.ok())
    {
        for (const auto& e : parsed.errors)
            errors.push_back (id + ": " + e);
        return false;
    }
    parsed.profile.user = user;
    auto made = std::make_shared<const Profile> (std::move (parsed.profile));
    for (auto& existing : profiles)
        if (existing->id == id)
        {
            existing = made;
            return true;
        }
    profiles.push_back (std::move (made));
    return true;
}

std::shared_ptr<const Profile> Library::find (const std::string& id) const
{
    for (const auto& p : profiles)
        if (p->id == id)
            return p;
    return nullptr;
}

std::shared_ptr<const Profile> Library::detect (const std::string& portName) const
{
    std::shared_ptr<const Profile> best;
    auto bestScore = 0;
    for (const auto& p : profiles)
    {
        if (matchScore (p->exclude, portName) > 0)
            continue;
        const auto score = matchScore (p->match, portName) * 2 + (p->user ? 1 : 0);
        if (score > bestScore && score >= 2)
        {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

std::shared_ptr<const Profile> Library::resolve (const std::string& choice, const std::string& portName) const
{
    if (choice.empty())
        return detect (portName);
    if (choice == genericChoice)
        return nullptr;
    return find (choice); // (a profile that is gone: a generic controller)
}

Library Library::builtIn()
{
    Library lib;
    for (const auto* file = builtInProfiles; file->id != nullptr; ++file)
        lib.add (file->text, file->id, false);
    return lib;
}

std::string pairedOutput (const std::string& inputName, const std::vector<std::string>& outputNames)
{
    auto has = [&] (const std::string& name) { return std::find (outputNames.begin(), outputNames.end(), name) != outputNames.end(); };
    if (has (inputName))
        return inputName;
    // the system's usual pairs of names for the two directions of one port
    static const std::pair<const char*, const char*> pairs[] = {
        { "MIDIIN", "MIDIOUT" }, { " DAW Out", " DAW In" }, { " MIDI Out", " MIDI In" }, { " Out", " In" }, { " In", " Out" }, { " Input", " Output" },
    };
    for (const auto& [from, to] : pairs)
    {
        const std::string f (from);
        const auto at = f[0] == ' ' ? inputName.rfind (f) : inputName.find (f);
        if (at == std::string::npos)
            continue;
        if (f[0] == ' ' && at + f.size() != inputName.size())
            continue; // (the direction words are the name's end)
        auto candidate = inputName;
        candidate.replace (at, f.size(), to);
        if (has (candidate))
            return candidate;
    }
    return {};
}
} // namespace lynx::controller
