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
                              "velocityRange", "chanceGroup", "chanceOne", "expression" };
// MPE stage 2 (issue #108, core/NoteCurves.h): "expression" = { "pitch": [[tick, cents], ...], "timbre": [[tick, 0..1], ...],
// "pressure": [[tick, 0..1], ...] }, ticks from the note's start; every lane present (empty = none), editable
constexpr std::array curveLanes { "pitch", "timbre", "pressure" };
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
inline juce::var curvesValue (const Note& n)
{
    auto* result = new juce::DynamicObject();
    for (int lane = 0; lane < noteCurveLaneCount; ++lane)
    {
        juce::Array<juce::var> points;
        for (const auto& q : n.curves->lane ((NoteCurveLane) lane))
            points.add (juce::Array<juce::var> { (double) q.tick, (double) q.value });
        result->setProperty (curveLanes[(std::size_t) lane], points);
    }
    return result;
}

inline NoteCurvesRef curvesFrom (const juce::var& value)
{
    const auto& input = value["expression"];
    const auto* object = input.getDynamicObject();
    if (object == nullptr)
        invalid ("expression must be an object");
    if (object->getProperties().size() != (int) curveLanes.size())
        invalid ("expression must contain exactly pitch, timbre and pressure");
    NoteCurves curves;
    for (int lane = 0; lane < noteCurveLaneCount; ++lane)
    {
        const auto* name = curveLanes[(std::size_t) lane];
        if (! input.hasProperty (name))
            invalid (juce::String ("expression must include ") + name);
        const auto* points = input[name].getArray();
        if (points == nullptr)
            invalid (juce::String ("expression.") + name + " must be an array");
        if ((std::size_t) points->size() > notecurves::maxPoints)
            invalid (juce::String ("expression.") + name + " has too many points");
        const auto pitch = lane == (int) NoteCurveLane::pitch;
        const auto low = pitch ? -(double) notecurves::maxPitchCents : 0.0, high = pitch ? (double) notecurves::maxPitchCents : 1.0;
        auto previous = -1.0;
        for (const auto& point : *points)
        {
            const auto* pair = point.getArray();
            if (pair == nullptr || pair->size() != 2)
                invalid (juce::String ("expression.") + name + " points must be [tick, value]");
            for (int i = 0; i < 2; ++i)
                if (! ((*pair)[i].isInt() || (*pair)[i].isInt64() || (*pair)[i].isDouble()))
                    invalid (juce::String ("expression.") + name + " points must be numbers");
            const auto tick = (double) (*pair)[0], v = (double) (*pair)[1];
            if (! std::isfinite (tick) || tick < 0.0 || tick > (double) maxTick)
                invalid (juce::String ("expression.") + name + " tick is outside its allowed range");
            if (tick <= previous)
                invalid (juce::String ("expression.") + name + " ticks must increase");
            if (! std::isfinite (v) || v < low || v > high)
                invalid (juce::String ("expression.") + name + " value is outside its allowed range");
            previous = tick;
            curves.lanes[(std::size_t) lane].push_back ({ (float) tick, (float) v });
        }
    }
    NoteCurvesRef result;
    result = std::move (curves);
    return result;
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
             { "chanceGroup", n.chanceGroup }, { "chanceOne", n.chanceOne }, { "expression", detail::curvesValue (n) },
             { "repeat", n.repeat } })
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
    // "repeat" (issue #208, FL's note repeat rate 0..14) is optional: clients written before it may leave it out
    const auto withRepeat = value.hasProperty ("repeat");
    if (object->getProperties().size() != (int) detail::fields.size() + (withRepeat ? 1 : 0))
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
    n.curves = detail::curvesFrom (value);
    if (withRepeat)
        n.repeat = (int) detail::integer (value, "repeat", 0, 14);
    return n;
}
} // namespace lynx::remote_notes
