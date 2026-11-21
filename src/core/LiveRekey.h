#pragma once

// Round 190on (owner decision, the recommended option; ONLINE.md 3.5 of round 190oh): the host removes a guest from a live
// session. Three parts:
//   - the relay closes the guest's connection (its slot is free at once) and keeps its address out of the room
//     (POST /v1/rooms/<room>/kick, docs/design/SHARE_API.md §3.4);
//   - by default the session gets a new invite code for NEW joins: same room, new secret, so a new relay room token and a
//     new session key. The guests still connected are not asked anything: the host sends them the new code sealed with
//     the old key, and every computer opens frames with the old key for a while after switching (frames already on the
//     way), so the switch drops nothing. The removed guest never sees the new code. "Keep the same code" skips this;
//   - a knocker the host turns away is kept out the same way (the relay ban), so it can't knock again with the old code.
// No JUCE.

#include <string>

#include "core/LiveCrypto.h"

namespace lynx::liverekey
{
// The next invite: the same relay room, a new secret.
inline livecrypto::Invite rotated (const livecrypto::Invite& now)
{
    livecrypto::Invite next = now;
    for (int tries = 0; tries < 8 && next.secret == now.secret; ++tries)
        next.secret = livecrypto::newSecret();
    return next;
}

// The kick request's body: {"peer":n,"ban":true,"token":"..."} (token only when the code changes).
inline std::string kickBody (int relayPeer, bool ban, const std::string& newRelayToken)
{
    std::string out = "{\"peer\":" + std::to_string (relayPeer < 0 ? 0 : relayPeer) + ",\"ban\":" + (ban ? "true" : "false");
    if (! newRelayToken.empty())
        out += ",\"token\":\"" + newRelayToken + "\"";
    return out + "}";
}

} // namespace lynx::liverekey
