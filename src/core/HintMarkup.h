#pragma once

// Markup of the hint bar texts (docs/WORKFLOW_FL.md, G12). A hint is plain text with optional tokens in
// square brackets that the hint bar draws as glyphs instead of words:
//   [LMB] left button (click / drag)   [2xLMB] double-click   [RMB] right button   [MMB] middle button
//   [Wheel] mouse wheel
//   [Ctrl] [Shift] [Alt] [Space] [F5] [Del] [L] ...   any other short key name is drawn as a key cap
// Example: "Volume: [LMB] drag  [Ctrl] fine  [Wheel] step  [Alt][LMB] reset  [RMB] menu".
// Only tokens of 1..10 letters, digits, '+' or '-' are markup; anything else in brackets stays text.
// No JUCE here, so the parser is unit-tested with the core.

#include <string>
#include <vector>

namespace lynx::hintmarkup
{
struct Segment
{
    enum class Kind
    {
        text,  // plain words
        mouse, // value: "LMB", "2xLMB", "RMB", "MMB" or "Wheel"
        key    // value: the key name as written ("Ctrl", "F5"...)
    };

    Kind kind = Kind::text;
    std::string value;

    bool operator== (const Segment&) const = default;
};

inline bool isMouseToken (const std::string& t)
{
    return t == "LMB" || t == "2xLMB" || t == "RMB" || t == "MMB" || t == "Wheel";
}

inline bool isToken (const std::string& t)
{
    if (t.empty() || t.size() > 10)
        return false;

    for (char c : t)
        if (! ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-'))
            return false;

    return true;
}

// Splits `text` into runs of text and glyph tokens (adjacent text is merged).
inline std::vector<Segment> parse (const std::string& text)
{
    std::vector<Segment> out;
    auto addText = [&out] (const std::string& s) {
        if (s.empty())
            return;
        if (! out.empty() && out.back().kind == Segment::Kind::text)
            out.back().value += s;
        else
            out.push_back ({ Segment::Kind::text, s });
    };

    std::size_t i = 0;

    while (i < text.size())
    {
        const auto open = text.find ('[', i);

        if (open == std::string::npos)
        {
            addText (text.substr (i));
            break;
        }

        const auto close = text.find (']', open + 1);

        if (close == std::string::npos)
        {
            addText (text.substr (i));
            break;
        }

        const auto token = text.substr (open + 1, close - open - 1);

        if (! isToken (token))
        {
            addText (text.substr (i, open + 1 - i)); // keep the '[' and look for the next token after it
            i = open + 1;
            continue;
        }

        addText (text.substr (i, open - i));
        out.push_back ({ isMouseToken (token) ? Segment::Kind::mouse : Segment::Kind::key, token });
        i = close + 1;
    }

    return out;
}

// The text without markup, for places that cannot draw glyphs (tooltips, logs): [LMB] -> "click",
// [Ctrl][LMB] -> "Ctrl+click"... `words` gives the words for LMB, 2xLMB, RMB, MMB and Wheel in that order.
// Round 190sc (RC sweep, the #296 bubble showed "Alt++click"): a key gets its "+" only when the next token follows it
// directly ([Ctrl][LMB], [Ctrl][Shift][LMB]); a text that writes the "+" itself ([Alt]+[LMB]) keeps its one "+", and a key
// before a word ("[Ctrl] fine") gets none.
inline std::string plain (const std::string& text, const std::vector<std::string>& words = { "click", "double-click", "right-click", "middle-click", "wheel" })
{
    std::string out;
    const char* mice[] = { "LMB", "2xLMB", "RMB", "MMB", "Wheel" };

    const auto segments = parse (text);
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        const auto& s = segments[i];
        if (s.kind == Segment::Kind::text)
            out += s.value;
        else if (s.kind == Segment::Kind::key)
            out += s.value + (i + 1 < segments.size() && segments[i + 1].kind != Segment::Kind::text ? "+" : "");
        else
            for (std::size_t m = 0; m < 5; ++m)
                if (s.value == mice[m])
                    out += m < words.size() ? words[m] : s.value;
    }

    return out;
}
} // namespace lynx::hintmarkup
