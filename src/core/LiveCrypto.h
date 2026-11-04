#pragma once

// Online Sessions, phase 2 (docs/design/ONLINE_SESSIONS.md §6.4 as built in round 169b): the invite code and the
// end-to-end encryption of a live session.
//
// The invite code LYNX-ABCDE-FGHIJ-KLMN-OPQR-STUV-WXYZ is the relay room (10 characters the relay chose) and an 80-bit
// secret the host's LYNX SHARE made (16 characters, never sent to any server). Argon2id (Monocypher, fixed salt) turns the
// secret into two values with different BLAKE2b domains: the session key (bound to the room too; XChaCha20-Poly1305, every frame) and the relay
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

// The host's identity in a session (round 169j): when a host comes back after a drop or a restart, the guests check it
// is the same host — anyone with the invite code has the session key, so the key alone cannot tell. The host keeps a
// random seed (in its journal); its Ed25519 public key travels in the snapshot; a returning host signs the room and a
// nonce the guest chose.
using Seed = std::array<std::uint8_t, 32>;
using PublicKey = std::array<std::uint8_t, 32>;
using Signature = std::array<std::uint8_t, 64>;
Seed newSeed();
PublicKey publicKeyOf (const Seed& seed);
Signature signHost (const Seed& seed, const std::string& room, const std::string& nonce);
bool checkHost (const PublicKey& key, const std::string& room, const std::string& nonce, const Signature& signature);

std::string toHex (const std::uint8_t* data, std::size_t size);
template <std::size_t N>
std::string toHex (const std::array<std::uint8_t, N>& a) { return toHex (a.data(), N); }
// false when `hex` is not exactly 2 * N hex digits
bool fromHex (const std::string& hex, std::uint8_t* out, std::size_t size);
template <std::size_t N>
bool fromHex (const std::string& hex, std::array<std::uint8_t, N>& out) { return fromHex (hex, out.data(), N); }

constexpr std::size_t overhead = 2 + 24 + 16 + 16; // magic + version, nonce, MAC, sender + counter
} // namespace lynx::livecrypto
