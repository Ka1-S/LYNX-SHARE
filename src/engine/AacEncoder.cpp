// M4A export on systems without a platform AAC encoder LYNX can call (Linux, the BSDs): see engine/AacEncoder.h.
// Windows: engine/AacEncoderWindows.cpp; macOS: engine/AacEncoderMac.cpp.

#include "engine/AacEncoder.h"

#if ! defined(_WIN32) && ! defined(__APPLE__)

namespace lynx::aac
{
bool available()
{
    return false;
}

std::string unavailableReason()
{
    // (no system AAC encoder to call: LYNX does not bundle one, engine/AacEncoder.h)
    return "M4A (AAC) export is not available on Linux: it uses the operating system's AAC encoder (Windows, macOS)";
}

bool encode (const std::string&, const float*, const float*, long long, int, int, int, std::string& error)
{
    error = unavailableReason();
    return false;
}
} // namespace lynx::aac

#endif
