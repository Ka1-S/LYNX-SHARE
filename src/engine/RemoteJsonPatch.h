#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace lynx::remote_json
{
[[noreturn]] inline void invalid (const char* message) { throw std::runtime_error (message); }

inline bool number (const juce::var& value) { return value.isInt() || value.isInt64() || value.isDouble(); }
// Native model floats are rounded by the project writer. Integer values retain exact semantics.
inline bool equal (const juce::var& a, const juce::var& b, bool modelFloats = false)
{
    if (number (a) && number (b))
    {
        const auto x = (double) a, y = (double) b;
        if (! std::isfinite (x) || ! std::isfinite (y)) return false;
        if (! modelFloats || ! a.isDouble() || ! b.isDouble()) return x == y;
        return std::abs (x - y) <= 2.0e-7 * std::max ({ 1.0, std::abs (x), std::abs (y) });
    }
    if (const auto* x = a.getArray())
    {
        const auto* y = b.getArray();
        if (y == nullptr || x->size() != y->size()) return false;
        for (int i = 0; i < x->size(); ++i) if (! equal ((*x)[i], (*y)[i], modelFloats)) return false;
        return true;
    }
    if (const auto* x = a.getDynamicObject())
    {
        const auto* y = b.getDynamicObject();
        if (y == nullptr || x->getProperties().size() != y->getProperties().size()) return false;
        for (const auto& p : x->getProperties())
            if (! y->hasProperty (p.name) || ! equal (p.value, y->getProperty (p.name), modelFloats)) return false;
        return true;
    }
    if (a.isVoid() || a.isUndefined()) return b.isVoid() || b.isUndefined();
    return a.isBool() == b.isBool() && a.isString() == b.isString() && a == b;
}

inline void bounds (const juce::var& value, int depth, std::size_t& nodes)
{
    if (depth > 64 || ++nodes > 4000000) invalid ("Project JSON exceeds its depth or node limit");
    if (number (value) && ! std::isfinite ((double) value)) invalid ("Project numbers must be finite");
    if (const auto* array = value.getArray())
    {
        if (array->size() > 1000000) invalid ("A project array exceeds its item limit");
        for (const auto& child : *array) bounds (child, depth + 1, nodes);
    }
    if (const auto* object = value.getDynamicObject())
        for (const auto& child : object->getProperties()) bounds (child.value, depth + 1, nodes);
}

inline std::vector<juce::String> pointer (const juce::var& value)
{
    if (! value.isString()) invalid ("Patch path must be a JSON Pointer string");
    const auto path = value.toString();
    if (path.isEmpty()) return {};
    if (! path.startsWithChar ('/') || path.length() > 4096) invalid ("Patch path must start with / and be at most 4096 characters");
    std::vector<juce::String> parts;
    juce::String part;
    for (int i = 1; i <= path.length(); ++i)
    {
        if (i == path.length() || path[i] == '/')
        {
            parts.push_back (part);
            part.clear();
            if (parts.size() > 64) invalid ("Patch path exceeds its depth limit");
        }
        else if (path[i] == '~')
        {
            if (++i >= path.length() || (path[i] != '0' && path[i] != '1')) invalid ("Invalid JSON Pointer escape");
            part += path[i] == '0' ? '~' : '/';
        }
        else part += path[i];
    }
    return parts;
}

inline int index (const juce::String& text, int size, bool insertion)
{
    if (insertion && text == "-") return size;
    if (text.isEmpty() || (text.length() > 1 && text[0] == '0')) invalid ("Invalid JSON Pointer array index");
    std::uint64_t result = 0;
    for (auto c : text)
    {
        if (c < '0' || c > '9') invalid ("Invalid JSON Pointer array index");
        result = result * 10 + (std::uint64_t) (c - '0');
        if (result > (std::uint64_t) std::numeric_limits<int>::max()) invalid ("JSON Pointer array index is too large");
    }
    if (result > (std::uint64_t) size || (! insertion && result == (std::uint64_t) size)) invalid ("JSON Pointer array index is out of range");
    return (int) result;
}

inline juce::var member (const juce::var& value, const juce::String& key)
{
    if (const auto* object = value.getDynamicObject())
    {
        const juce::Identifier id (key);
        if (! object->hasProperty (id)) invalid ("Patch path does not exist");
        return object->getProperty (id);
    }
    if (const auto* array = value.getArray()) return (*array)[index (key, array->size(), false)];
    invalid ("Patch path crosses a scalar value");
}

inline void edit (juce::var& value, const std::vector<juce::String>& parts, std::size_t at,
                  const juce::String& operation, const juce::var& replacement)
{
    if (at == parts.size())
    {
        if (operation == "test") { if (! equal (value, replacement)) invalid ("JSON Patch test failed"); }
        else if (operation == "remove") invalid ("Removing the project root is not supported");
        else value = replacement.clone();
        return;
    }
    const auto& key = parts[at];
    if (auto* object = value.getDynamicObject())
    {
        const juce::Identifier id (key);
        if (at + 1 == parts.size())
        {
            if (operation != "add" && ! object->hasProperty (id)) invalid ("Patch path does not exist");
            if (operation == "test") { if (! equal (object->getProperty (id), replacement)) invalid ("JSON Patch test failed"); }
            else if (operation == "remove") object->removeProperty (id);
            else object->setProperty (id, replacement.clone());
        }
        else
        {
            auto child = member (value, key);
            edit (child, parts, at + 1, operation, replacement);
            object->setProperty (id, child);
        }
        return;
    }
    if (auto* array = value.getArray())
    {
        const auto i = index (key, array->size(), at + 1 == parts.size() && operation == "add");
        if (at + 1 == parts.size())
        {
            if (operation == "test") { if (! equal ((*array)[i], replacement)) invalid ("JSON Patch test failed"); }
            else if (operation == "remove") array->remove (i);
            else if (operation == "add") array->insert (i, replacement.clone());
            else array->set (i, replacement.clone());
        }
        else
        {
            auto child = (*array)[i];
            edit (child, parts, at + 1, operation, replacement);
            array->set (i, child);
        }
        return;
    }
    invalid ("Patch path crosses a scalar value");
}

inline juce::var apply (const juce::var& source, const juce::var& patch)
{
    const auto* operations = patch.getArray();
    if (operations == nullptr || operations->isEmpty() || operations->size() > 128) invalid ("patch must contain 1..128 operations");
    auto result = source.clone();
    for (const auto& operation : *operations)
    {
        const auto* fields = operation.getDynamicObject();
        if (fields == nullptr || ! operation["op"].isString()) invalid ("Each patch operation must be an object with op and path");
        for (const auto& field : fields->getProperties())
            if (field.name.toString() != "op" && field.name.toString() != "path" && field.name.toString() != "value")
                invalid ("Unknown JSON Patch field");
        const auto op = operation["op"].toString();
        if (op != "test" && op != "add" && op != "replace" && op != "remove") invalid ("Supported JSON Patch operations are test, add, replace and remove");
        if ((op != "remove") != operation.hasProperty ("value")) invalid ("Patch value is required except for remove");
        edit (result, pointer (operation["path"]), 0, op, operation["value"]);
    }
    std::size_t nodes = 0;
    bounds (result, 0, nodes);
    return result;
}
} // namespace lynx::remote_json
