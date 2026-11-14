#pragma once

#include "core/Song.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

namespace lynx::remote_notes
{
namespace detail
{
constexpr std::array fields { "start", "length", "channel", "key", "velocity", "pan", "muted", "finePitch",
                              "slide", "porta", "release", "modX", "modY", "colour", "group", "chance",
                              "velocityRange", "chanceGroup", "chanceOne" };
constexpr juce::int64 maxTick = 1000000000;

[[noreturn]] inline void invalid (const juce::String& why) { throw std::runtime_error (why.toStdString()); }

inline double number (const juce::var& value, const char* field, double low, double high)
{
    const auto& input = value[field];
    if (! (input.isInt() || input.isInt64() || input.isDouble()))
        invalid (juce::String (field) + " must be a number");
    const auto result = (double) input;
    if (! std::isfinite (result) || result < low || result > high)
        invalid (juce::String (field) + " is outside its allowed range");
    return result;
}

inline juce::int64 integer (const juce::var& value, const char* field, juce::int64 low, juce::int64 high)
{
    const auto result = number (value, field, (double) low, (double) high);
    if (std::floor (result) != result)
        invalid (juce::String (field) + " must be an integer");
    return (juce::int64) result;
}

inline bool boolean (const juce::var& value, const char* field)
{
    if (! value[field].isBool())
        invalid (juce::String (field) + " must be true or false");
    return (bool) value[field];
}
} // namespace detail

// The read and complete-replacement operations share one codec so an unchanged
// read/write cycle cannot reset expression fields to their model defaults.
inline juce::var toValue (const Note& n)
{
    auto* result = new juce::DynamicObject();
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, juce::var>> {
             { "start", (juce::int64) n.start }, { "length", (juce::int64) n.length },
             { "channel", n.channel }, { "key", n.key }, { "velocity", (double) n.velocity },
             { "pan", (double) n.pan }, { "muted", n.muted }, { "finePitch", (double) n.finePitch },
             { "slide", n.slide }, { "porta", n.porta }, { "release", (double) n.release },
             { "modX", (double) n.modX }, { "modY", (double) n.modY }, { "colour", n.colour }, { "group", n.group },
             { "chance", (double) n.chance }, { "velocityRange", (double) n.velocityRange },
             { "chanceGroup", n.chanceGroup }, { "chanceOne", n.chanceOne } })
        result->setProperty (name, value);
    return result;
}

inline Note fromValue (const juce::var& value, std::size_t channels)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        detail::invalid ("Every note must be an object");
    for (const auto* field : detail::fields)
        if (! value.hasProperty (field))
            detail::invalid (juce::String ("Every replacement note must include ") + field + "; read the complete notes first");
    if (object->getProperties().size() != (int) detail::fields.size())
        detail::invalid ("Replacement note contains an unsupported field");
    if (channels == 0)
        detail::invalid ("Cannot add a note without a channel");
    Note n;
    n.start = detail::integer (value, "start", 0, detail::maxTick);
    n.length = detail::integer (value, "length", 1, detail::maxTick);
    if (n.start > detail::maxTick - n.length)
        detail::invalid ("Note end is outside its allowed range");
    n.channel = (int) detail::integer (value, "channel", 0, (juce::int64) channels - 1);
    n.key = (int) detail::integer (value, "key", 0, 127);
    n.velocity = (float) detail::number (value, "velocity", 0, 1);
    n.pan = (float) detail::number (value, "pan", -1, 1);
    n.muted = detail::boolean (value, "muted");
    n.finePitch = (float) detail::number (value, "finePitch", -120, 120);
    n.slide = detail::boolean (value, "slide");
    n.porta = detail::boolean (value, "porta");
    n.release = (float) detail::number (value, "release", 0, 1);
    n.modX = (float) detail::number (value, "modX", 0, 1);
    n.modY = (float) detail::number (value, "modY", 0, 1);
    n.colour = (int) detail::integer (value, "colour", 0, 15);
    n.group = (int) detail::integer (value, "group", 0, std::numeric_limits<int>::max());
    n.chance = (float) detail::number (value, "chance", 0, 1);
    n.velocityRange = (float) detail::number (value, "velocityRange", -1, 1);
    n.chanceGroup = (int) detail::integer (value, "chanceGroup", 0, std::numeric_limits<int>::max());
    n.chanceOne = detail::boolean (value, "chanceOne");
    return n;
}
} // namespace lynx::remote_notes
