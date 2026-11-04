#include "core/LiveCrypto.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <random>

extern "C" {
#include "monocypher.h"
}

namespace lynx::livecrypto
{
namespace
{
constexpr const char* alphabet = "abcdefghijklmnopqrstuvwxyz234567";
constexpr std::uint8_t magic = 0x4C, version = 1;
const std::string ad = "LYNX SHARE live session v1";

void put64 (std::uint8_t* p, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = (std::uint8_t) (v >> (8 * i));
}

std::uint64_t get64 (const std::uint8_t* p)
{
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = v << 8 | p[i];
    return v;
}
} // namespace

void randomBytes (std::uint8_t* out, std::size_t size)
{
    // (std::random_device is the OS generator on every platform LYNX SHARE builds for: rand_s / getrandom)
    std::random_device device;
    for (std::size_t i = 0; i < size; i += 4)
    {
        const auto r = device();
        for (std::size_t j = 0; j < 4 && i + j < size; ++j)
            out[i + j] = (std::uint8_t) (r >> (8 * j));
    }
}

std::string newSecret()
{
    std::array<std::uint8_t, 16> bytes {};
    randomBytes (bytes.data(), bytes.size());
    std::string s;
    for (auto b : bytes)
        s += alphabet[b & 31];
    crypto_wipe (bytes.data(), bytes.size());
    return s;
}

std::string formatCode (const Invite& invite)
{
    auto up = [] (std::string s) {
        for (auto& c : s)
            c = (char) std::toupper ((unsigned char) c);
        return s;
    };
    const auto r = up (invite.room), k = up (invite.secret);
    if (r.size() != 10 || k.size() != 16)
        return {};
    return "LYNX-" + r.substr (0, 5) + "-" + r.substr (5) + "-" + k.substr (0, 4) + "-" + k.substr (4, 4) + "-" + k.substr (8, 4) + "-" + k.substr (12);
}

std::optional<Invite> parseCode (const std::string& text)
{
    // a link (…/join#CODE) or the code itself; separators and case do not matter; 0/1/8 typed for O/I/B are forgiven
    auto from = text;
    if (const auto hash = from.find ('#'); hash != std::string::npos)
        from = from.substr (hash + 1);
    std::string s;
    for (auto c : from)
    {
        const auto l = (char) std::tolower ((unsigned char) c);
        if (l == '0')
            s += 'o';
        else if (l == '1')
            s += 'i';
        else if (l == '8')
            s += 'b';
        else if ((l >= 'a' && l <= 'z') || (l >= '2' && l <= '7'))
            s += l;
        else if (l == '-' || l == ' ' || l == '\t' || l == '\n' || l == '\r' || l == '_' || l == '.')
            continue;
        else
            return std::nullopt;
    }
    if (s.rfind ("lynx", 0) == 0 && s.size() == 30)
        s = s.substr (4);
    if (s.size() != 26)
        return std::nullopt;
    return Invite { s.substr (0, 10), s.substr (10) };
}

Keys derive (const Invite& invite)
{
    Keys keys;
    // The salt is fixed and the secret alone goes into Argon2, so the host knows the relay token before the relay has
    // chosen the room (it creates the room with it); the room id goes into the session key's domain.
    const std::string salt = "LYNX SHARE live session salt v1";
    const auto pass = invite.secret;
    crypto_argon2_config config { CRYPTO_ARGON2_ID, 8192, 3, 1 }; // 8 MiB, 3 passes: ~50 ms once per join
    crypto_argon2_inputs inputs { (const std::uint8_t*) pass.data(), (const std::uint8_t*) salt.data(), (std::uint32_t) pass.size(), (std::uint32_t) salt.size() };
    std::vector<std::uint8_t> work ((std::size_t) config.nb_blocks * 1024);
    std::uint8_t master[32];
    crypto_argon2 (master, sizeof master, work.data(), config, inputs, crypto_argon2_no_extras);
    const std::string keyDomain = "session key:" + invite.room, tokenDomain = "relay room token";
    crypto_blake2b_keyed (keys.key.data(), 32, master, 32, (const std::uint8_t*) keyDomain.data(), keyDomain.size());
    std::uint8_t token[32];
    crypto_blake2b_keyed (token, 32, master, 32, (const std::uint8_t*) tokenDomain.data(), tokenDomain.size());
    static const char* hex = "0123456789abcdef";
    for (auto b : token)
        keys.relayToken += hex[b >> 4], keys.relayToken += hex[b & 15];
    crypto_wipe (master, sizeof master);
    crypto_wipe (work.data(), work.size());
    return keys;
}

Sealer::Sealer (const std::array<std::uint8_t, 32>& k) : key (k)
{
    std::uint8_t id[8];
    randomBytes (id, 8);
    sender = get64 (id);
}

std::vector<std::uint8_t> Sealer::seal (const std::vector<std::uint8_t>& plain)
{
    std::vector<std::uint8_t> inner (16 + plain.size());
    put64 (inner.data(), sender);
    put64 (inner.data() + 8, ++counter);
    if (! plain.empty())
        std::memcpy (inner.data() + 16, plain.data(), plain.size());
    std::vector<std::uint8_t> out (2 + 24 + 16 + inner.size());
    out[0] = magic;
    out[1] = version;
    randomBytes (out.data() + 2, 24);
    crypto_aead_lock (out.data() + 42, out.data() + 26, key.data(), out.data() + 2, (const std::uint8_t*) ad.data(), ad.size(), inner.data(), inner.size());
    crypto_wipe (inner.data(), inner.size());
    return out;
}

Opener::Opener (const std::array<std::uint8_t, 32>& k) : key (k) {}

std::optional<std::vector<std::uint8_t>> Opener::open (const std::vector<std::uint8_t>& frame, std::uint64_t* senderOut)
{
    if (frame.size() < overhead || frame[0] != magic || frame[1] != version)
        return std::nullopt;
    std::vector<std::uint8_t> inner (frame.size() - 42);
    if (crypto_aead_unlock (inner.data(), frame.data() + 26, key.data(), frame.data() + 2, (const std::uint8_t*) ad.data(), ad.size(), frame.data() + 42,
                            inner.size())
        != 0)
        return std::nullopt;
    const auto sender = get64 (inner.data()), counter = get64 (inner.data() + 8);
    auto& seen = last[sender];
    if (counter <= seen)
        return std::nullopt; // (a replay)
    seen = counter;
    if (senderOut != nullptr)
        *senderOut = sender;
    return std::vector<std::uint8_t> (inner.begin() + 16, inner.end());
}
// --- the host's identity -------------------------------------------------------------------------------------------

namespace
{
std::vector<std::uint8_t> hostMessage (const std::string& room, const std::string& nonce)
{
    const std::string text = "LYNX SHARE live host v1|" + room + "|" + nonce;
    return { text.begin(), text.end() };
}
} // namespace

Seed newSeed()
{
    Seed s {};
    randomBytes (s.data(), s.size());
    return s;
}

PublicKey publicKeyOf (const Seed& seed)
{
    auto copy = seed; // (Monocypher wipes the seed it is given)
    std::uint8_t secret[64];
    PublicKey pub {};
    crypto_eddsa_key_pair (secret, pub.data(), copy.data());
    crypto_wipe (secret, sizeof secret);
    return pub;
}

Signature signHost (const Seed& seed, const std::string& room, const std::string& nonce)
{
    auto copy = seed;
    std::uint8_t secret[64];
    PublicKey pub {};
    crypto_eddsa_key_pair (secret, pub.data(), copy.data());
    const auto m = hostMessage (room, nonce);
    Signature sig {};
    crypto_eddsa_sign (sig.data(), secret, m.data(), m.size());
    crypto_wipe (secret, sizeof secret);
    return sig;
}

bool checkHost (const PublicKey& key, const std::string& room, const std::string& nonce, const Signature& signature)
{
    const auto m = hostMessage (room, nonce);
    return crypto_eddsa_check (signature.data(), key.data(), m.data(), m.size()) == 0;
}

std::string toHex (const std::uint8_t* data, std::size_t size)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve (size * 2);
    for (std::size_t i = 0; i < size; ++i)
        out += digits[data[i] >> 4], out += digits[data[i] & 15];
    return out;
}

bool fromHex (const std::string& hex, std::uint8_t* out, std::size_t size)
{
    if (hex.size() != size * 2)
        return false;
    auto nibble = [] (char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    for (std::size_t i = 0; i < size; ++i)
    {
        const int hi = nibble (hex[2 * i]), lo = nibble (hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (std::uint8_t) (hi << 4 | lo);
    }
    return true;
}
} // namespace lynx::livecrypto

