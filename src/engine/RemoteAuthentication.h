#pragma once

#include "core/Sha256.h"
#include <array>
#include <string_view>

namespace lynx::remote_auth
{
inline std::array<std::uint8_t, 32> digestBytes (const std::string& hex)
{
    const auto nibble = [] (char c) { return (std::uint8_t) (c <= '9' ? c - '0' : c - 'a' + 10); };
    std::array<std::uint8_t, 32> result {};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = (std::uint8_t) ((nibble (hex[2 * i]) << 4) | nibble (hex[2 * i + 1]));
    return result;
}
inline std::string hmacSha256 (std::string_view key, std::string_view message)
{
    std::array<std::uint8_t, 64> block {};
    if (key.size() > block.size())
    {
        const auto digest = digestBytes (sha256Hex (reinterpret_cast<const std::uint8_t*> (key.data()), key.size()));
        std::copy (digest.begin(), digest.end(), block.begin());
    }
    else std::copy (key.begin(), key.end(), block.begin());
    std::array<std::uint8_t, 64> innerPad {}, outerPad {};
    for (std::size_t i = 0; i < block.size(); ++i)
    {
        innerPad[i] = block[i] ^ 0x36;
        outerPad[i] = block[i] ^ 0x5c;
    }
    Sha256Stream inner;
    inner.update (innerPad.data(), innerPad.size());
    inner.update (reinterpret_cast<const std::uint8_t*> (message.data()), message.size());
    const auto digest = digestBytes (inner.hex());
    Sha256Stream outer;
    outer.update (outerPad.data(), outerPad.size());
    outer.update (digest.data(), digest.size());
    return outer.hex();
}
inline bool isLowerHex (std::string_view value, std::size_t size) noexcept
{
    return value.size() == size && std::all_of (value.begin(), value.end(), [] (char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
inline bool constantTimeEqual (std::string_view supplied, std::string_view expected) noexcept
{
    if (supplied.size() != expected.size()) return false;
    unsigned difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        difference |= (unsigned char) supplied[i] ^ (unsigned char) expected[i];
    return difference == 0;
}
inline std::string proof (std::string_view token, std::string_view role, std::string_view clientNonce, std::string_view serverNonce)
{
    return hmacSha256 (token, "lynx-control/1\n" + std::string (role) + "\n" + std::string (clientNonce) + "\n" + std::string (serverNonce));
}
}
