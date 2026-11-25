#pragma once

// Settings › Files › FL Studio folder (round 190rh, issue #291): whether a folder is an FL Studio installation, told from
// the files alone (a Windows drive mounted on Linux has no registry), read only. The user may pick the folder itself, one
// above it ("Image-Line", "Program Files"), one inside it ("Data") or macOS' application; `folder` is the installation's.

#include <string>
#include <vector>

namespace lynx::flinstall
{
enum class Status
{
    ok,          // an installation LYNX can read
    missing,     // no such folder (the drive is not mounted, the folder was moved)
    notReadable, // there, but this user may not read it (a mount with another owner, no permissions)
    userData,    // FL's user data folder (Documents\Image-Line\FL Studio): the installation is another folder
    several,     // a folder with more than one installation: `candidates`
    notFl        // something else
};

struct Check
{
    Status status = Status::notFl;
    std::string folder;                  // the installation ("" unless ok)
    std::string version;                 // "26.1.7.5654" from FL64.exe's version resource, "" when it can't be read
    std::string missing;                 // ok, but a part is not there or not readable ("Data", "Plugins")
    std::vector<std::string> candidates; // the installations found under `several`
};

// Never throws, never writes; follows the names ignoring case where the file system does not.
Check check (const std::string& folder);

// The version in a Windows program's version resource (VS_FIXEDFILEINFO) as "a.b.c.d"; "" = none
std::string versionOf (const std::vector<unsigned char>& exe);

// Where a Windows drive may be mounted on this system: the installations found there (Linux: the folders of /mnt,
// /media, /run/media; `roots` replaces them in the tests). Only looks at "<mount>/Program Files[ (x86)]/Image-Line".
std::vector<std::string> onMountedDrives (const std::vector<std::string>& roots = {});
} // namespace lynx::flinstall
