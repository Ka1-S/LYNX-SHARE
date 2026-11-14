#pragma once

#include <stdexcept>
#include <string>

namespace lynx
{
class RemoteError final : public std::runtime_error
{
public:
    RemoteError (std::string errorCode, std::string message, bool outcomeUncertain = false)
        : std::runtime_error (std::move (message)), code (std::move (errorCode)), uncertain (outcomeUncertain) {}
    const std::string code;
    const bool uncertain;
};
}
