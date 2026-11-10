#include "core/XmlLite.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>

namespace lynx::xml
{
namespace
{
bool isSpace (char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isNameChar (char c) noexcept
{
    return ! isSpace (c) && c != '/' && c != '>' && c != '=' && c != '<' && c != '"' && c != '\'' && c != '\0';
}

void appendUtf8 (std::string& out, std::uint32_t cp)
{
    if (cp < 0x80)
        out += (char) cp;
    else if (cp < 0x800)
    {
        out += (char) (0xC0 | (cp >> 6));
        out += (char) (0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += (char) (0xE0 | (cp >> 12));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    }
    else if (cp < 0x110000)
    {
        out += (char) (0xF0 | (cp >> 18));
        out += (char) (0x80 | ((cp >> 12) & 0x3F));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    }
}
} // namespace

std::string Document::decode (std::string_view raw)
{
    if (raw.find ('&') == std::string_view::npos)
        return std::string (raw);
    std::string out;
    out.reserve (raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        if (raw[i] != '&')
        {
            out += raw[i];
            continue;
        }
        const auto semi = raw.find (';', i);
        if (semi == std::string_view::npos || semi - i > 12)
        {
            out += raw[i];
            continue;
        }
        const auto ent = raw.substr (i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (! ent.empty() && ent[0] == '#')
        {
            std::uint32_t cp = 0;
            const auto hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            const auto digits = ent.substr (hex ? 2 : 1);
            if (std::from_chars (digits.data(), digits.data() + digits.size(), cp, hex ? 16 : 10).ec == std::errc())
                appendUtf8 (out, cp);
        }
        else
        {
            out.append (raw.substr (i, semi - i + 1)); // unknown entity: kept as written
        }
        i = semi;
    }
    return out;
}

bool Document::parse (std::string text, std::string* error)
{
    buffer = std::move (text);
    nodes.clear();
    attrs.clear();
    texts.clear();
    const auto fail = [error] (const char* what, std::size_t at) {
        if (error != nullptr)
            *error = std::string (what) + " at byte " + std::to_string (at);
        return false;
    };

    const char* s = buffer.data();
    const auto n = buffer.size();
    std::size_t i = 0;
    if (n >= 3 && (unsigned char) s[0] == 0xEF && (unsigned char) s[1] == 0xBB && (unsigned char) s[2] == 0xBF)
        i = 3; // UTF-8 BOM

    std::vector<std::uint32_t> stack, lastChild;
    // texts are collected per node while it is open, then appended contiguously when it closes
    std::vector<std::vector<std::string_view>> pendingText;
    nodes.reserve (n / 40);

    while (i < n)
    {
        if (s[i] != '<')
        {
            const auto start = i;
            while (i < n && s[i] != '<')
                ++i;
            if (! stack.empty())
            {
                auto run = std::string_view (s + start, i - start);
                bool blank = true;
                for (const auto c : run)
                    blank = blank && isSpace (c);
                if (! blank)
                    pendingText.back().push_back (run);
            }
            continue;
        }
        if (buffer.compare (i, 4, "<!--") == 0)
        {
            const auto end = buffer.find ("-->", i + 4);
            if (end == std::string::npos)
                return fail ("unterminated comment", i);
            i = end + 3;
            continue;
        }
        if (buffer.compare (i, 9, "<![CDATA[") == 0)
        {
            const auto end = buffer.find ("]]>", i + 9);
            if (end == std::string::npos)
                return fail ("unterminated CDATA", i);
            if (! stack.empty())
                pendingText.back().push_back (std::string_view (s + i + 9, end - i - 9)); // (CDATA: decode is a no-op unless it holds '&')
            i = end + 3;
            continue;
        }
        if (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!'))
        {
            const auto end = buffer.find ('>', i);
            if (end == std::string::npos)
                return fail ("unterminated declaration", i);
            i = end + 1;
            continue;
        }
        if (i + 1 < n && s[i + 1] == '/')
        {
            auto j = i + 2;
            while (j < n && isNameChar (s[j]))
                ++j;
            const auto name = std::string_view (s + i + 2, j - i - 2);
            if (stack.empty() || nodes[stack.back()].name != name)
                return fail ("mismatched closing tag", i);
            const auto end = buffer.find ('>', j);
            if (end == std::string::npos)
                return fail ("unterminated closing tag", i);
            auto& node = nodes[stack.back()];
            node.textBegin = (std::uint32_t) texts.size();
            node.textCount = (std::uint32_t) pendingText.back().size();
            texts.insert (texts.end(), pendingText.back().begin(), pendingText.back().end());
            pendingText.pop_back();
            stack.pop_back();
            lastChild.pop_back();
            i = end + 1;
            continue;
        }

        // start tag
        auto j = i + 1;
        while (j < n && isNameChar (s[j]))
            ++j;
        if (j == i + 1)
            return fail ("empty element name", i);
        if (stack.empty() && ! nodes.empty())
            return fail ("more than one root element", i);
        Node node;
        node.name = std::string_view (s + i + 1, j - i - 1);
        node.firstAttr = (std::uint32_t) attrs.size();
        bool selfClosing = false;
        for (;;)
        {
            while (j < n && isSpace (s[j]))
                ++j;
            if (j >= n)
                return fail ("unterminated tag", i);
            if (s[j] == '/' && j + 1 < n && s[j + 1] == '>')
            {
                selfClosing = true;
                j += 2;
                break;
            }
            if (s[j] == '>')
            {
                ++j;
                break;
            }
            const auto an = j;
            while (j < n && isNameChar (s[j]))
                ++j;
            const auto attrName = std::string_view (s + an, j - an);
            while (j < n && isSpace (s[j]))
                ++j;
            if (attrName.empty() || j >= n || s[j] != '=')
                return fail ("bad attribute", j);
            ++j;
            while (j < n && isSpace (s[j]))
                ++j;
            if (j >= n || (s[j] != '"' && s[j] != '\''))
                return fail ("unquoted attribute", j);
            const auto quote = s[j];
            const auto vs = ++j;
            while (j < n && s[j] != quote)
                ++j;
            if (j >= n)
                return fail ("unterminated attribute", vs);
            attrs.push_back ({ attrName, std::string_view (s + vs, j - vs) });
            ++j;
        }
        node.attrCount = (std::uint32_t) attrs.size() - node.firstAttr;
        const auto index = (std::uint32_t) nodes.size();
        nodes.push_back (node);
        if (! stack.empty())
        {
            if (lastChild.back() == 0)
                nodes[stack.back()].firstChild = index;
            else
                nodes[lastChild.back()].nextSibling = index;
            lastChild.back() = index;
        }
        if (! selfClosing)
        {
            stack.push_back (index);
            lastChild.push_back (0);
            pendingText.emplace_back();
        }
        i = j;
    }
    if (! stack.empty())
        return fail ("unclosed element", n);
    if (nodes.empty())
        return fail ("no root element", 0);
    return true;
}

std::string_view Element::name() const noexcept { return doc != nullptr ? doc->nodes[node].name : std::string_view {}; }

bool Element::hasAttr (std::string_view a) const noexcept
{
    if (doc == nullptr)
        return false;
    const auto& nd = doc->nodes[node];
    for (std::uint32_t k = 0; k < nd.attrCount; ++k)
        if (doc->attrs[nd.firstAttr + k].name == a)
            return true;
    return false;
}

std::string Element::attr (std::string_view a) const
{
    if (doc == nullptr)
        return {};
    const auto& nd = doc->nodes[node];
    for (std::uint32_t k = 0; k < nd.attrCount; ++k)
        if (const auto& at = doc->attrs[nd.firstAttr + k]; at.name == a)
            return Document::decode (at.rawValue);
    return {};
}

std::vector<std::pair<std::string, std::string>> Element::attributes() const
{
    std::vector<std::pair<std::string, std::string>> out;
    if (doc == nullptr)
        return out;
    const auto& nd = doc->nodes[node];
    for (std::uint32_t k = 0; k < nd.attrCount; ++k)
    {
        const auto& at = doc->attrs[nd.firstAttr + k];
        out.emplace_back (std::string (at.name), Document::decode (at.rawValue));
    }
    return out;
}

std::string Element::text() const
{
    if (doc == nullptr)
        return {};
    const auto& nd = doc->nodes[node];
    std::string out;
    for (std::uint32_t k = 0; k < nd.textCount; ++k)
        out += Document::decode (doc->texts[nd.textBegin + k]);
    return out;
}

Element Element::child (std::string_view c) const noexcept
{
    if (doc == nullptr)
        return {};
    for (auto k = doc->nodes[node].firstChild; k != 0; k = doc->nodes[k].nextSibling)
        if (doc->nodes[k].name == c)
            return Element (doc, k);
    return {};
}

Element Element::path (std::string_view p) const noexcept
{
    Element e = *this;
    while (e && ! p.empty())
    {
        const auto slash = p.find ('/');
        e = e.child (p.substr (0, slash));
        p = slash == std::string_view::npos ? std::string_view {} : p.substr (slash + 1);
    }
    return e;
}

std::vector<Element> Element::children (std::string_view c) const
{
    std::vector<Element> out;
    if (doc == nullptr)
        return out;
    for (auto k = doc->nodes[node].firstChild; k != 0; k = doc->nodes[k].nextSibling)
        if (c.empty() || doc->nodes[k].name == c)
            out.push_back (Element (doc, k));
    return out;
}

Element Element::find (std::string_view d) const noexcept
{
    if (doc == nullptr)
        return {};
    // nodes are stored in document order: the descendants of `node` are the next indices up to its next sibling's
    // (or an ancestor's next sibling); walk them with an explicit stack of child links
    std::vector<std::uint32_t> todo;
    for (auto k = doc->nodes[node].firstChild; k != 0; k = doc->nodes[k].nextSibling)
        todo.push_back (k);
    std::reverse (todo.begin(), todo.end());
    while (! todo.empty())
    {
        const auto k = todo.back();
        todo.pop_back();
        if (doc->nodes[k].name == d)
            return Element (doc, k);
        std::vector<std::uint32_t> kids;
        for (auto c = doc->nodes[k].firstChild; c != 0; c = doc->nodes[c].nextSibling)
            kids.push_back (c);
        todo.insert (todo.end(), kids.rbegin(), kids.rend());
    }
    return {};
}

std::string Element::value (std::string_view p, std::string_view fallback) const
{
    const auto e = path (p);
    return e && e.hasAttr ("Value") ? e.attr ("Value") : std::string (fallback);
}

double Element::number (std::string_view p, double fallback) const
{
    const auto e = path (p);
    if (! e || ! e.hasAttr ("Value"))
        return fallback;
    const auto v = e.attr ("Value");
    char* end = nullptr;
    const auto d = std::strtod (v.c_str(), &end);
    return end != v.c_str() ? d : fallback;
}

bool Element::boolean (std::string_view p, bool fallback) const
{
    const auto e = path (p);
    if (! e || ! e.hasAttr ("Value"))
        return fallback;
    const auto v = e.attr ("Value");
    return v == "true" || v == "1";
}

std::string escape (std::string_view text)
{
    std::string out;
    out.reserve (text.size());
    for (const auto c : text)
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            case '\n': out += "&#10;"; break;
            case '\r': out += "&#13;"; break;
            case '\t': out += "&#9;"; break;
            default: out += c;
        }
    return out;
}
} // namespace lynx::xml
