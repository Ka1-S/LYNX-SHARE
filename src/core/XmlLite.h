#pragma once

// A small read-only XML DOM for project files written by other programs (round 175a: Live sets, which are gzipped XML
// of up to tens of MB). No dependencies, no JUCE. One pass over the text; element names, attribute values and text
// point into the document's own buffer (decoded lazily), so a 27 MB set parses in well under a second with one
// allocation per element. Supports elements, attributes, text, CDATA, comments, the XML declaration and the five
// named entities plus numeric character references. No DTDs, namespaces or validation.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lynx::xml
{
struct Attribute
{
    std::string_view name, rawValue; // rawValue: as written, entities not decoded
};

class Document;

// A view of one element. Cheap to copy; valid while its Document lives.
class Element
{
public:
    Element() = default;
    explicit operator bool() const noexcept { return doc != nullptr; }

    std::string_view name() const noexcept;
    // The decoded value of attribute `attr` ("" when missing).
    std::string attr (std::string_view attr) const;
    bool hasAttr (std::string_view attr) const noexcept;
    // Every attribute, decoded, in document order.
    std::vector<std::pair<std::string, std::string>> attributes() const;
    // The decoded text directly inside this element (concatenated runs; "" when none).
    std::string text() const;

    // The first child named `childName` (invalid Element when none).
    Element child (std::string_view childName) const noexcept;
    // A path of child names separated by '/' ("DeviceChain/Mixer/Volume").
    Element path (std::string_view childPath) const noexcept;
    // Every child (named `childName`, or all with an empty name), in document order.
    std::vector<Element> children (std::string_view childName = {}) const;
    // The first element anywhere below (depth first) named `descendantName`.
    Element find (std::string_view descendantName) const noexcept;

    // Live-style scalars: the "Value" attribute of the child at `childPath` (`fallback` when missing or unparsable).
    std::string value (std::string_view childPath, std::string_view fallback = {}) const;
    double number (std::string_view childPath, double fallback = 0.0) const;
    bool boolean (std::string_view childPath, bool fallback = false) const;

    std::size_t index() const noexcept { return node; }

private:
    friend class Document;
    Element (const Document* d, std::size_t n) noexcept : doc (d), node (n) {}
    const Document* doc = nullptr;
    std::size_t node = 0;
};

class Document
{
public:
    // Parses `text` (kept by the document). False and `error` on malformed XML.
    bool parse (std::string text, std::string* error = nullptr);
    Element root() const noexcept { return nodes.empty() ? Element() : Element (this, 0); }

    static std::string decode (std::string_view raw); // entities → characters (UTF-8)

private:
    friend class Element;
    struct Node
    {
        std::string_view name;
        std::uint32_t firstAttr = 0, attrCount = 0;
        std::uint32_t firstChild = 0, nextSibling = 0; // 0 = none (node 0 is the root, never a child)
        std::uint32_t textBegin = 0, textCount = 0;    // runs in `texts`
    };
    std::string buffer;
    std::vector<Node> nodes;
    std::vector<Attribute> attrs;
    std::vector<std::string_view> texts;
};

// Writing: escapes text for an attribute value or element text.
std::string escape (std::string_view text);
} // namespace lynx::xml
