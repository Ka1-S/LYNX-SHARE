#pragma once

#include "engine/RemoteJsonLimits.h"
#include <juce_core/juce_core.h>
#include <stdexcept>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <aclapi.h>
 #include <bcrypt.h>
 #include <sddl.h>
#else
 #include <cerrno>
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace lynx::remote_files
{
[[noreturn]] inline void fail (const char* message) { throw std::runtime_error (message); }

inline juce::String randomToken()
{
    unsigned char bytes[32];
#if JUCE_WINDOWS
    if (BCryptGenRandom (nullptr, bytes, sizeof (bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        fail ("Cannot read the operating system random source");
#else
    const auto fd = ::open ("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) fail ("Cannot open the operating system random source");
    std::size_t done = 0;
    while (done < sizeof (bytes))
    {
        const auto count = ::read (fd, bytes + done, sizeof (bytes) - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ::close (fd); fail ("Cannot read the operating system random source"); }
        done += (std::size_t) count;
    }
    ::close (fd);
#endif
    return juce::String::toHexString (bytes, sizeof (bytes), 0);
}

#if JUCE_WINDOWS
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle (HANDLE h) : value (h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle (value); }
    Handle (const Handle&) = delete;
    Handle& operator= (const Handle&) = delete;
};
struct LocalMemory
{
    void* value = nullptr;
    ~LocalMemory() { if (value != nullptr) LocalFree (value); }
};
struct Owner
{
    Owner()
    {
        HANDLE raw = nullptr;
        if (! OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &raw)) fail ("Cannot inspect current user");
        Handle token (raw);
        DWORD size = 0;
        GetTokenInformation (raw, TokenUser, nullptr, 0, &size);
        user.resize (size);
        if (size == 0 || ! GetTokenInformation (raw, TokenUser, user.data(), size, &size)) fail ("Cannot inspect current user");
        sid = reinterpret_cast<TOKEN_USER*> (user.data())->User.Sid;
        LPWSTR rawText = nullptr;
        if (! ConvertSidToStringSidW (sid, &rawText)) fail ("Cannot encode current user");
        LocalMemory text { rawText };
        const auto sddl = L"O:" + std::wstring (rawText) + L"D:P(A;;FA;;;" + std::wstring (rawText) + L")";
        PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
        if (! ConvertStringSecurityDescriptorToSecurityDescriptorW (sddl.c_str(), SDDL_REVISION_1, &rawDescriptor, nullptr))
            fail ("Cannot create private remote-control security descriptor");
        descriptor.value = rawDescriptor;
        attributes = { sizeof (SECURITY_ATTRIBUTES), rawDescriptor, FALSE };
    }
    void check (HANDLE file) const
    {
        PSID owner = nullptr;
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR raw = nullptr;
        if (GetSecurityInfo (file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                             &owner, nullptr, &dacl, nullptr, &raw) != ERROR_SUCCESS)
            fail ("Cannot inspect remote-control file permissions");
        LocalMemory security { raw };
        if (owner == nullptr || ! EqualSid (owner, sid) || dacl == nullptr)
            fail ("Remote-control discovery must be owned by this user and private");
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (! GetSecurityDescriptorControl (raw, &control, &revision) || (control & SE_DACL_PROTECTED) == 0)
            fail ("Remote-control discovery must have a protected current-user-only DACL");
        bool ownerAccess = false;
        for (DWORD i = 0; i < dacl->AceCount; ++i)
        {
            void* rawAce = nullptr;
            if (! GetAce (dacl, i, &rawAce)) fail ("Cannot inspect remote-control access entries");
            const auto* header = static_cast<ACE_HEADER*> (rawAce);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) fail ("Unsupported remote-control access entry");
            const auto* ace = static_cast<ACCESS_ALLOWED_ACE*> (rawAce);
            if (! EqualSid (const_cast<DWORD*> (&ace->SidStart), sid))
                fail ("Remote-control discovery grants access to another account");
            ownerAccess = ownerAccess || ace->Mask != 0;
        }
        if (! ownerAccess) fail ("Remote-control discovery has no current-user access");
    }
    std::vector<unsigned char> user;
    PSID sid = nullptr;
    LocalMemory descriptor;
    SECURITY_ATTRIBUTES attributes {};
};

inline void checkPrivateFile (const juce::File& file)
{
    const auto name = file.getFullPathName();
    const auto path = name.toWideCharPointer();
    const auto attributes = GetFileAttributesW (path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
        fail ("Remote-control discovery must be a regular file without a reparse point");
    Handle handle (CreateFileW (path, READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE) fail ("Cannot inspect remote-control discovery");
    Owner().check (handle.value);
}
inline void ensurePrivateDirectory (const juce::File& directory)
{
    Owner owner;
    const auto name = directory.getFullPathName();
    const auto path = name.toWideCharPointer();
    if (! CreateDirectoryW (path, &owner.attributes) && GetLastError() != ERROR_ALREADY_EXISTS)
        fail ("Cannot create private remote-control directory");
    const auto attributes = GetFileAttributesW (path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        fail ("Remote-control directory must not be a reparse point");
    Handle handle (CreateFileW (path, READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE) fail ("Cannot inspect remote-control directory");
    owner.check (handle.value);
}
inline void writeDiscovery (const juce::File& file, const juce::String& json)
{
    ensurePrivateDirectory (file.getParentDirectory());
    if (file.exists()) checkPrivateFile (file);
    Owner owner;
    const auto temporary = file.getSiblingFile (file.getFileName() + "." + randomToken() + ".tmp");
    const auto tempPath = temporary.getFullPathName();
    bool ok = false;
    {
        Handle handle (CreateFileW (tempPath.toWideCharPointer(), GENERIC_WRITE | READ_CONTROL, 0, &owner.attributes, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (handle.value == INVALID_HANDLE_VALUE) fail ("Cannot create private discovery file");
        const auto bytes = json.toStdString();
        DWORD written = 0;
        ok = WriteFile (handle.value, bytes.data(), (DWORD) bytes.size(), &written, nullptr) && written == bytes.size()
             && FlushFileBuffers (handle.value);
    }
    if (ok) ok = MoveFileExW (tempPath.toWideCharPointer(), file.getFullPathName().toWideCharPointer(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (! ok) { temporary.deleteFile(); fail ("Cannot publish private remote-control discovery"); }
}
#else
inline void checkPrivateFile (const juce::File& file)
{
    struct stat status {};
    if (::lstat (file.getFullPathName().toRawUTF8(), &status) != 0 || ! S_ISREG (status.st_mode)
        || status.st_uid != ::geteuid() || (status.st_mode & 0077) != 0)
        fail ("Remote-control discovery must be a regular, current-user-only file");
}
inline void ensurePrivateDirectory (const juce::File& directory)
{
    const auto path = directory.getFullPathName().toStdString();
    if (::mkdir (path.c_str(), 0700) != 0 && errno != EEXIST) fail ("Cannot create private remote-control directory");
    struct stat status {};
    if (::lstat (path.c_str(), &status) != 0 || ! S_ISDIR (status.st_mode) || status.st_uid != ::geteuid() || (status.st_mode & 0077) != 0)
        fail ("Remote-control directory must already be owned by this user and private");
}
inline void writeDiscovery (const juce::File& file, const juce::String& json)
{
    ensurePrivateDirectory (file.getParentDirectory());
    struct stat existing {};
    if (::lstat (file.getFullPathName().toRawUTF8(), &existing) == 0) checkPrivateFile (file);
    else if (errno != ENOENT) fail ("Cannot inspect discovery destination");
    auto pattern = (file.getFullPathName() + ".XXXXXX").toStdString();
    const auto fd = ::mkstemp (pattern.data());
    if (fd < 0) fail ("Cannot create private remote-control discovery file");
    bool ok = ::fchmod (fd, S_IRUSR | S_IWUSR) == 0;
    const auto bytes = json.toStdString();
    std::size_t done = 0;
    while (ok && done < bytes.size())
    {
        const auto count = ::write (fd, bytes.data() + done, bytes.size() - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) ok = false; else done += (std::size_t) count;
    }
    if (ok) ok = ::fsync (fd) == 0;
    ::close (fd);
    if (ok) ok = ::rename (pattern.c_str(), file.getFullPathName().toRawUTF8()) == 0;
    if (! ok) { ::unlink (pattern.c_str()); fail ("Cannot publish private remote-control discovery file"); }
}
#endif

inline void removeDiscoveryIfOwned (const juce::File& file, const juce::String& token)
{
    checkPrivateFile (file);
    auto input = file.createInputStream();
    if (input == nullptr) return;
    // Read a bounded amount even if the file changes after its permissions are
    // checked. Cleanup must not feed untrusted nesting to JUCE's recursive parser.
    constexpr std::size_t maxBytes = 16384;
    juce::MemoryBlock bytes;
    input->readIntoMemoryBlock (bytes, maxBytes + 1);
    input.reset(); // Windows must close the reader before deleting the record.
    if (bytes.getSize() == 0 || bytes.getSize() > maxBytes
        || ! remote_json::withinNestingLimit ({ static_cast<const char*> (bytes.getData()), bytes.getSize() }, 8))
        return;
    const auto record = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), (int) bytes.getSize()));
    if (record["token"].toString() == token) file.deleteFile();
}
}
