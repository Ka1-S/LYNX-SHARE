#pragma once

// Online Sessions, phase 2 (docs/design/ONLINE_SESSIONS.md §6.4 as built in round 169b): the invite code and the
// end-to-end encryption of a live session.
//
// The invite code LYNX-ABCDE-FGHIJ-KLMN-OPQR-STUV-WXYZ is the relay room (10 characters the relay chose) and an 80-bit
// secret the host's LYNX SHARE made (16 characters, never sent to any server). Argon2id (Monocypher) turns the whole
// code into two values with different BLAKE2b domains: the session key (XChaCha20-Poly1305, every frame) and the relay
// room token (what the relay checks before it lets a peer into the room). The relay sees the token, never the key, and
// cannot get one from the other. Every frame carries the sender's random id and a counter: a frame replayed by anyone
// on the way is dropped.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lynx::livecrypto
{
struct Invite
{
    std::string room;   // 10 characters a-z 2-7 (the relay's room id)
    std::string secret; // 16 characters a-z 2-7 (80 bits)
};
std::string newSecret();
std::string formatCode (const Invite& invite);                // "LYNX-ABCDE-FGHIJ-KLMN-OPQR-STUV-WXYZ"
std::optional<Invite> parseCode (const std::string& text);   // spaces, dashes, case, a pasted link: forgiven

struct Keys
{
    std::array<std::uint8_t, 32> key {};
    std::string relayToken; // 64 hex digits
};
Keys derive (const Invite& invite);

void randomBytes (std::uint8_t* out, std::size_t size);

// One computer's sending side: frames sealed with the session key, numbered.
class Sealer
{
public:
    explicit Sealer (const std::array<std::uint8_t, 32>& key);
    std::vector<std::uint8_t> seal (const std::vector<std::uint8_t>& plain);
    std::uint64_t id() const noexcept { return sender; }

private:
    std::array<std::uint8_t, 32> key;
    std::uint64_t sender = 0, counter = 0;
};

// The receiving side: a frame that was changed, made with another key or seen before is refused.
class Opener
{
public:
    explicit Opener (const std::array<std::uint8_t, 32>& key);
    std::optional<std::vector<std::uint8_t>> open (const std::vector<std::uint8_t>& frame, std::uint64_t* sender = nullptr);

private:
    std::array<std::uint8_t, 32> key;
    std::map<std::uint64_t, std::uint64_t> last; // sender -> highest counter
};

constexpr std::size_t overhead = 2 + 24 + 16 + 16; // magic + version, nonce, MAC, sender + counter
} // namespace lynx::livecrypto
