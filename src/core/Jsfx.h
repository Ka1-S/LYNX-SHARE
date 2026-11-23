#pragma once

// JSFX support (issue #241, round 190pj; docs/design/JSFX.md): what LYNX keeps about a JSFX script apart from the
// runtime (ysfx, engine/JsfxFormat.h). No JUCE here (tests/Jsfx190pjTests.inc).
//
// - Identity. A script is found in one of the JSFX roots (the user's Documents/LYNX/JSFX, the bundled pack, the REAPER
//   Effects folder when REAPER is installed, the folders added in Settings › File). Its id is its path relative to that
//   root ("Utility/volume_trim.jsfx"), so a project made on another computer finds the same script in any root there.
//   The plugin list keeps the absolute path; the unique id is a hash of the relative path (plus the role: a script
//   listed both as an effect and as a generator has two ids).
// - File policy (owner decision 1, recommended option): a script reads files (file_open, file sliders) only inside the
//   JSFX roots, their Data folders and the folder of the open project. Scripts have no write, exec or network API.
// - Saved state (owner decision 3, recommended option): the relative id, a hash of the script's source (to warn when
//   the script changed since the project was saved), the slider values and the script's @serialize data. The script's
//   source itself is never stored in the project.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lynx::jsfx
{
inline constexpr const char* formatName = "JSFX";

// A path with '/' separators, no "." / ".." parts and no trailing '/' (lexically; "C:\\a\\..\\b" -> "C:/b")
std::string normalise (std::string_view path);

// `path` lies inside `root` (or is it). Both are normalised first; caseInsensitive for Windows' file systems.
bool isInside (std::string_view path, std::string_view root, bool caseInsensitive);

// The root that holds `path` (the deepest one when roots nest) and the path relative to it ("Utility/volume.jsfx");
// root = -1 when none does
struct Located
{
    int root = -1;
    std::string relative;
};
Located locate (std::string_view path, const std::vector<std::string>& roots, bool caseInsensitive);

// The plugin list's unique id of a script: a hash of its relative path (case-folded, '/' separators) and its role
std::int32_t uniqueIdOf (std::string_view relative, bool instrument);

// A hash of a script's source bytes (FNV-1a 64), saved with its state
std::uint64_t sourceHash (std::string_view bytes);

// The first bytes of an extensionless file read as a JSFX header: a "desc:" line among its first lines (REAPER's own
// effects have no extension). Comment lines and blank lines before it are allowed.
bool looksLikeJsfx (std::string_view head);

// A script is also listed as a generator (a Channel rack instrument) when it has no audio input, or one of its tags or
// its folder says it is an instrument, a synth or a MIDI tool (the MIDI ones then send their notes to the channel's
// MIDI Out port, as instruments do)
bool listAsGenerator (int audioInputs, const std::vector<std::string>& tags, std::string_view relative);
// ... and as an effect unless it has no audio output at all and no input either (a pure generator)
bool listAsEffect (int audioInputs, int audioOutputs);

// What a JSFX slot saves (PluginRef::state, base64 of these bytes)
struct SavedState
{
    std::string relative;            // the script's id
    std::uint64_t source = 0;        // sourceHash of the script when it was saved (0: unknown)
    std::vector<std::pair<std::uint32_t, double>> sliders; // slider index (0-based) and value
    std::string serialized;          // the @serialize data
};
std::string encodeState (const SavedState& state);
// false: not a LYNX JSFX state (or a damaged one); `state` is then left as it was
bool decodeState (const void* data, std::size_t size, SavedState& state);

// The default folder of the bundled pack inside LYNX's settings folder, relative to it
inline constexpr const char* packFolderName = "JSFX pack";
// The user's own folder, relative to Documents/LYNX
inline constexpr const char* userFolderName = "JSFX";
} // namespace lynx::jsfx
