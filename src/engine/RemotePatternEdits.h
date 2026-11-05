#pragma once

#include "core/RackOps.h"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace lynx::remote_patterns
{
constexpr std::int64_t maxLength = 1000000000;
constexpr int maxPatterns = 100000;
struct Result { Song song; int index; };

inline void shiftLoopStarterPatterns (Song& song, int index)
{
    // RackOps already shifts clips and Pattern Blocks and selects the insertion.
    // Loop Starter ownership is the remaining pattern reference (SessionIds.cpp).
    for (auto& channel : song.channels)
        if (channel.loopStarterPattern >= index)
            ++channel.loopStarterPattern;
}

inline void validateName (const std::optional<std::string>& name)
{
    if (! name) return;
    if (name->empty() || std::all_of (name->begin(), name->end(), [] (unsigned char c) { return c <= 32; })
        || std::any_of (name->begin(), name->end(), [] (unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error ("name must contain visible text without control characters");
}
inline void validateLength (std::int64_t length)
{
    if (length < 0 || length > maxLength)
        throw std::runtime_error ("lengthTicks must be in 0..1000000000 (0 is automatic)");
}
inline void validateCreate (const Song& song, int index, const std::optional<std::string>& name, std::int64_t length)
{
    if (song.patterns.size() >= (std::size_t) maxPatterns || index < 0 || index > (int) song.patterns.size())
        throw std::runtime_error ("index must be an insertion position below the pattern limit");
    validateName (name);
    validateLength (length);
}
inline void validateClone (const Song& song, int index, const std::optional<std::string>& name)
{
    if (song.patterns.size() >= (std::size_t) maxPatterns || index < 0 || index >= (int) song.patterns.size())
        throw std::runtime_error ("index must identify an existing pattern below the pattern limit");
    validateName (name);
}
inline bool metadataChanged (const Song& song, int index, const std::optional<std::string>& name,
                             const std::optional<std::int64_t>& length)
{
    if (index < 0 || index >= (int) song.patterns.size())
        throw std::runtime_error ("index must identify an existing pattern");
    if (! name && ! length) throw std::runtime_error ("Supply name or lengthTicks");
    validateName (name);
    const auto& pattern = song.patterns[(std::size_t) index];
    if (length)
    {
        validateLength (*length);
        if (*length > 0)
            for (const auto& note : pattern.notes)
                if (note.start < 0 || note.length < 0 || note.start > *length || note.length > *length - note.start)
                    throw std::runtime_error ("lengthTicks would truncate an existing note; use automatic length or retain its full end");
    }
    return (name && *name != pattern.name) || (length && *length != pattern.lengthTicks);
}
// Stage after the engine has captured live hosted state, never from an older model.
inline Result create (const Song& source, std::optional<int> at = {}, std::optional<std::string> name = {}, std::int64_t length = 0)
{
    const auto index = at.value_or ((int) source.patterns.size());
    validateCreate (source, index, name, length);
    auto next = source;
    insertPattern (next, index, name.value_or (newPatternName (source)));
    shiftLoopStarterPatterns (next, index);
    next.patterns[(std::size_t) index].lengthTicks = length;
    return { std::move (next), index };
}
inline Result clone (const Song& source, int index, std::optional<std::string> name = {})
{
    validateClone (source, index, name);
    auto next = source;
    const auto added = clonePattern (next, index);
    shiftLoopStarterPatterns (next, added);
    if (name) next.patterns[(std::size_t) added].name = *name;
    // Publication assigns a fresh id; the source keeps its identity.
    next.patterns[(std::size_t) added].uid = 0;
    return { std::move (next), added };
}
inline Result set (const Song& source, int index, const std::optional<std::string>& name = {},
                   const std::optional<std::int64_t>& length = {})
{
    metadataChanged (source, index, name, length);
    auto next = source;
    auto& pattern = next.patterns[(std::size_t) index];
    if (name) pattern.name = *name;
    if (length) pattern.lengthTicks = *length;
    return { std::move (next), index };
}
}
