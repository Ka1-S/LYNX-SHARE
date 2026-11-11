#include "core/FruityDance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "core/Instruments.h"

namespace lynx::dance
{
namespace
{
constexpr float twoPi = 6.28318530718f;

const char* moveIds[] = { "freestyle", "waiting", "stepping", "jumping", "zombie", "waving", "hula", "windmill", "flail", "spin", "held" };

std::uint32_t mixBits (std::uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float smooth (float t) noexcept { return t * t * (3.0f - 2.0f * t); }

bool validUtf8 (const std::string& s) noexcept
{
    for (std::size_t i = 0; i < s.size();)
    {
        const auto c = (unsigned char) s[i];
        const auto extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xe ? 2 : (c >> 3) == 0x1e ? 3 : -1;
        if (extra < 0 || i + (std::size_t) extra >= s.size())
            return false;
        for (int k = 1; k <= extra; ++k)
            if (((unsigned char) s[i + (std::size_t) k] >> 6) != 0x2)
                return false;
        i += (std::size_t) extra + 1;
    }
    return true;
}

// text that is not UTF-8 is read as Latin-1
std::string toUtf8 (const std::string& s)
{
    if (validUtf8 (s))
        return s;
    std::string out;
    for (const auto ch : s)
    {
        const auto c = (unsigned char) ch;
        if (c < 0x80)
            out += (char) c;
        else
        {
            out += (char) (0xc0 | (c >> 6));
            out += (char) (0x80 | (c & 0x3f));
        }
    }
    return out;
}
} // namespace

const char* moveName (int move) noexcept
{
    switch (std::clamp (move, 0, moveCount - 1))
    {
        case freestyle: return "Freestyle";
        case waiting: return "Waiting";
        case stepping: return "Stepping";
        case jumping: return "Jumping";
        case zombie: return "Zombie";
        case waving: return "Waving";
        case hula: return "Hula";
        case windmill: return "Windmill";
        case flail: return "Flailing";
        case spin: return "Spinning";
        case held: return "Pose";
    }
    return "Freestyle";
}

std::vector<std::uint8_t> toState (const Settings& s)
{
    std::ostringstream o;
    char colour[16];
    std::snprintf (colour, sizeof (colour), "%08x", s.colour);
    o << "LYNX dance 1\nmove=";
    if (s.sheet.empty() && s.move >= 0 && s.move < moveCount)
        o << moveIds[s.move];
    else
        o << std::clamp (s.move, 0, maxMoves - 1); // (a sheet's moves are its rows: a number)
    o << "\nmirrored=" << (s.mirrored ? 1 : 0) << "\nvisible=" << (s.visible ? 1 : 0) << "\nscale=" << s.scale << "\nspeed=" << s.speed
      << "\nx=" << s.x << "\ny=" << s.y << "\ncolour=" << colour << "\n";
    // (only when used, so the states of dancers without a sheet stay as they were)
    if (s.blend != Settings {}.blend)
        o << "blend=" << s.blend << "\n";
    if (! s.sheet.empty())
    {
        auto path = s.sheet;
        for (auto& c : path)
            if (c == '\n' || c == '\r')
                c = ' ';
        o << "sheet=" << path << "\n";
    }
    const auto t = o.str();
    return { t.begin(), t.end() };
}

std::optional<Settings> fromState (const std::vector<std::uint8_t>& state)
{
    const std::string text (state.begin(), state.end());
    if (text.rfind ("LYNX dance", 0) != 0)
        return std::nullopt;
    Settings s;
    std::istringstream in (text);
    std::string line;
    std::getline (in, line);
    auto num = [] (const std::string& v, float lo, float hi, float fallback) {
        const auto f = std::strtof (v.c_str(), nullptr);
        return std::isfinite (f) ? std::clamp (f, lo, hi) : fallback;
    };
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = line.substr (0, eq), value = line.substr (eq + 1);
        if (key == "move")
        {
            for (int m = 0; m < moveCount; ++m)
                if (value == moveIds[m])
                    s.move = m;
            if (! value.empty() && value.find_first_not_of ("0123456789") == std::string::npos)
                s.move = std::clamp (std::atoi (value.c_str()), 0, maxMoves - 1);
        }
        else if (key == "blend")
            s.blend = num (value, 0.0f, 1.0f, Settings {}.blend);
        else if (key == "sheet")
            s.sheet = value;
        else if (key == "mirrored")
            s.mirrored = value != "0";
        else if (key == "visible")
            s.visible = value != "0";
        else if (key == "scale")
            s.scale = num (value, 0.25f, 3.0f, 1.0f);
        else if (key == "speed")
            s.speed = num (value, 0.125f, 4.0f, 1.0f);
        else if (key == "x")
            s.x = std::clamp (std::atoi (value.c_str()), -10000, 10000);
        else if (key == "y")
            s.y = std::clamp (std::atoi (value.c_str()), -10000, 10000);
        else if (key == "colour")
            s.colour = (std::uint32_t) std::strtoul (value.c_str(), nullptr, 16) | 0xff000000u;
    }
    return s;
}

std::optional<Settings> fromFl (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 40)
        return std::nullopt;
    auto i32 = [&state] (std::size_t at) {
        std::int32_t v = 0;
        std::memcpy (&v, state.data() + at, 4);
        return v;
    };
    const auto version = i32 (0);
    if (version < 1 || version > 16)
        return std::nullopt;
    Settings s;
    // params 0 … 6 (FL: move, mirrored, shown, blend /256, speed, x, y). The move is 0 = Freestyle, then the sheet's
    // rows; it is kept as it is (LYNX's own dancer clamps it when it draws).
    s.move = std::clamp ((int) i32 (4), 0, maxMoves - 1);
    s.mirrored = i32 (8) > 0;
    s.visible = i32 (12) > 0;
    s.blend = std::clamp ((float) i32 (16) / 256.0f, 0.0f, 1.0f);
    // FL's speed: 0.5 × (1 + |v| × 3 / 96) (or divided by it): LYNX keeps the ratio to the default 0.5
    const auto v = i32 (20);
    const auto d = 1.0f + (float) std::abs (v) * 3.0f / 96.0f;
    s.speed = std::clamp (v < 0 ? 1.0f / d : d, 0.125f, 4.0f);
    s.x = std::clamp ((int) i32 (24), -10000, 10000);
    s.y = std::clamp ((int) i32 (28), -10000, 10000);
    // version 2: after the display options (an int and four flags, 8 bytes) the sheet's file: a length byte (255: an
    // int follows with the length), then the characters; control characters at the end are dropped. Empty = FL's own.
    if (version >= 2 && state.size() > 40)
    {
        std::size_t at = 40;
        std::size_t length = state[at++];
        if (length == 0xff)
        {
            if (state.size() < at + 4)
                return s;
            length = (std::uint32_t) i32 (at);
            at += 4;
        }
        if (length <= state.size() - at)
        {
            std::string path ((const char*) state.data() + at, length);
            while (! path.empty() && (unsigned char) path.back() < 0x20)
                path.pop_back();
            s.sheet = toUtf8 (path);
        }
    }
    return s;
}

std::vector<std::uint8_t> toFl (const Settings& s)
{
    std::vector<std::uint8_t> b;
    auto i32 = [&b] (std::int32_t v) {
        for (int k = 0; k < 4; ++k)
            b.push_back ((std::uint8_t) ((std::uint32_t) v >> (8 * k)));
    };
    i32 (2);
    i32 (std::clamp (s.move, 0, maxMoves - 1));
    i32 (s.mirrored ? 1 : 0);
    i32 (s.visible ? 1 : 0);
    i32 ((std::int32_t) std::lround (std::clamp (s.blend, 0.0f, 1.0f) * 256.0f));
    // the speed slider: -96 … 96 around FL's default (see fromFl)
    const auto ratio = std::clamp (s.speed, 0.25f, 4.0f);
    i32 ((std::int32_t) std::lround (ratio >= 1.0f ? (ratio - 1.0f) * 32.0f : -(1.0f / ratio - 1.0f) * 32.0f));
    i32 (s.x);
    i32 (s.y);
    // the display options as FL makes them: reflection 64; Sync changes, Keep in front, On desktop, Solid on
    i32 (64);
    for (int k = 0; k < 4; ++k)
        b.push_back (1);
    const auto length = s.sheet.size();
    if (length < 0xff)
        b.push_back ((std::uint8_t) length);
    else
    {
        b.push_back (0xff);
        i32 ((std::int32_t) length);
    }
    b.insert (b.end(), s.sheet.begin(), s.sheet.end());
    return b;
}

Settings read (const std::vector<std::uint8_t>& state)
{
    if (auto s = fromState (state))
        return *s;
    if (auto s = fromFl (state))
        return *s;
    return {};
}

KeyAction actionForKey (int key) noexcept
{
    return actionForKey (key, moveCount);
}

KeyAction actionForKey (int key, int moves) noexcept
{
    // FL fills its key table for the loaded sheet (FUN_0071a410): keys 59 … 59 + rows - 1 (at most 127) and
    // 48 … 48 - (rows - 1) (at least 0), then Hide / Show on 53 / 54 over whatever was there
    KeyAction a;
    moves = std::clamp (moves, 1, maxMoves);
    if (key == hideKey)
        a.kind = KeyAction::Kind::hide;
    else if (key == showKey)
        a.kind = KeyAction::Kind::show;
    else if (key >= firstMoveKey && key < firstMoveKey + moves && key <= 127)
    {
        a.kind = KeyAction::Kind::move;
        a.move = key - firstMoveKey;
    }
    else if (key <= firstMirrorKey && key > firstMirrorKey - moves && key >= 0)
    {
        a.kind = KeyAction::Kind::move;
        a.move = firstMirrorKey - key;
        a.mirrored = true;
    }
    else if (key == hideKey)
        a.kind = KeyAction::Kind::hide;
    else if (key == showKey)
        a.kind = KeyAction::Kind::show;
    return a;
}

Pose Pose::mix (const Pose& a, const Pose& b, float t) noexcept
{
    auto m = [t] (float x, float y) { return x + (y - x) * t; };
    Pose p;
    p.x = m (a.x, b.x);
    p.y = m (a.y, b.y);
    p.lean = m (a.lean, b.lean);
    p.head = m (a.head, b.head);
    p.turn = m (a.turn, b.turn);
    p.squash = m (a.squash, b.squash);
    p.hipShift = m (a.hipShift, b.hipShift);
    p.tail = m (a.tail, b.tail);
    for (int i = 0; i < 2; ++i)
    {
        p.shoulder[(std::size_t) i] = m (a.shoulder[(std::size_t) i], b.shoulder[(std::size_t) i]);
        p.elbow[(std::size_t) i] = m (a.elbow[(std::size_t) i], b.elbow[(std::size_t) i]);
        p.hip[(std::size_t) i] = m (a.hip[(std::size_t) i], b.hip[(std::size_t) i]);
        p.knee[(std::size_t) i] = m (a.knee[(std::size_t) i], b.knee[(std::size_t) i]);
    }
    return p;
}

int freestyleMove (std::int64_t cycleIndex, std::uint32_t seed) noexcept
{
    // two cycles per choice; Stepping … Spinning
    const auto h = mixBits ((std::uint32_t) (cycleIndex / 2) * 2654435761u ^ seed);
    return stepping + (int) (h % (std::uint32_t) (spin - stepping + 1));
}

Pose pose (int move, double cycles, std::uint32_t seed) noexcept
{
    if (move == freestyle)
    {
        const auto index = (std::int64_t) std::floor (cycles);
        const auto now = freestyleMove (index, seed);
        const auto before = freestyleMove (index - 1, seed);
        const auto within = (float) (cycles - std::floor (cycles));
        if (now != before && (index % 2) == 0 && within < 0.25f) // a short blend into the new move
            return Pose::mix (pose (before, cycles, seed), pose (now, cycles, seed), smooth (within / 0.25f));
        return pose (now, cycles, seed);
    }
    const auto phase = (float) (cycles - std::floor (cycles)); // 0..1 within the cycle
    const auto s = std::sin (twoPi * phase), c = std::cos (twoPi * phase);
    const auto s2 = std::sin (2.0f * twoPi * phase);
    Pose p;
    // rest: arms down a little out, legs straight
    p.shoulder = { 0.25f, -0.25f };
    p.elbow = { 0.15f, -0.15f };
    p.hip = { 0.08f, -0.08f };
    switch (move)
    {
        case waiting:
        {
            const auto slow = (float) std::sin (twoPi * cycles * 0.25);
            p.y = 0.006f * s;
            p.lean = 0.03f * slow;
            p.head = 0.08f * slow;
            p.tail = 0.3f * s;
            p.shoulder = { 0.18f, -0.18f };
            // a foot taps on the beat
            p.knee = { 0.0f, phase < 0.3f ? -0.35f * std::sin (twoPi * phase / 0.6f) : 0.0f };
            p.hip[1] = phase < 0.3f ? 0.15f * std::sin (twoPi * phase / 0.6f) : -0.08f;
            break;
        }
        case stepping:
        {
            p.hipShift = 0.07f * s;
            p.x = 0.04f * s;
            p.y = 0.02f * std::abs (s2);
            p.lean = -0.08f * s;
            p.head = 0.1f * s;
            p.hip = { 0.12f + 0.18f * std::max (0.0f, s), -0.12f + 0.18f * std::min (0.0f, s) };
            p.knee = { -0.5f * std::max (0.0f, s), 0.5f * std::max (0.0f, -s) };
            p.shoulder = { 0.3f - 0.45f * s, -0.3f - 0.45f * s };
            p.elbow = { 0.6f, -0.6f };
            p.tail = -0.5f * s;
            break;
        }
        case jumping:
        {
            const auto up = std::abs (std::sin (0.5f * twoPi * phase * 2.0f)); // two hops a cycle
            const auto hop = up * up;
            p.y = 0.16f * hop;
            p.squash = 1.0f - 0.12f * (1.0f - up) * (1.0f - up);
            p.shoulder = { 0.5f + 2.2f * hop, -0.5f - 2.2f * hop };
            p.elbow = { 0.3f * (1.0f - hop), -0.3f * (1.0f - hop) };
            p.hip = { 0.2f, -0.2f };
            p.knee = { -0.9f * (1.0f - up), 0.9f * (1.0f - up) };
            p.tail = 0.8f * hop;
            break;
        }
        case zombie:
        {
            p.x = 0.03f * s;
            p.lean = 0.12f + 0.05f * s;
            p.head = 0.35f + 0.1f * c;
            p.shoulder = { 1.5f + 0.1f * s, -1.5f + 0.1f * c }; // arms out in front
            p.elbow = { 0.05f, -0.05f };
            p.hip = { 0.1f + 0.2f * std::max (0.0f, s), -0.1f + 0.2f * std::min (0.0f, s) };
            p.knee = { -0.3f * std::max (0.0f, s), 0.3f * std::max (0.0f, -s) };
            p.y = -0.01f;
            p.tail = 0.1f * s;
            break;
        }
        case waving:
        {
            p.lean = 0.06f * s;
            p.head = -0.12f + 0.08f * s;
            p.shoulder = { 2.6f, -0.9f }; // left arm up, right on the hip
            p.elbow = { 0.5f + 0.6f * s2, -1.9f };
            p.hipShift = 0.03f * s;
            p.y = 0.012f * std::abs (s);
            p.tail = 0.6f * s2;
            break;
        }
        case hula:
        {
            p.hipShift = 0.09f * s;
            p.y = 0.015f * c;
            p.lean = -0.12f * s;
            p.head = 0.12f * s;
            p.shoulder = { 1.5f + 0.25f * s, -1.5f + 0.25f * s }; // arms out, waving
            p.elbow = { 0.6f * c, -0.6f * c };
            p.knee = { -0.25f, 0.25f };
            p.hip = { 0.15f, -0.15f };
            p.tail = 0.9f * s;
            break;
        }
        case windmill:
        {
            const auto a = twoPi * phase;
            p.shoulder = { a, -1.2f + 0.8f * std::sin (a + 1.5f) }; // one arm circles
            p.elbow = { 0.0f, -0.4f };
            p.lean = 0.1f * std::sin (a);
            p.y = 0.03f * std::abs (std::sin (a));
            p.hip = { 0.2f, -0.2f };
            p.knee = { -0.3f * std::abs (s), 0.3f * std::abs (s) };
            p.tail = std::sin (a);
            break;
        }
        case flail:
        {
            // fast and a bit random: arms and legs everywhere
            auto jitter = [seed, cycles] (int k) {
                const auto t = (float) (cycles * 4.0 + k * 0.37);
                const auto i = (std::uint32_t) std::floor (t);
                const auto f = smooth (t - std::floor (t));
                auto r = [seed, k] (std::uint32_t n) { return (float) (mixBits (n * 7919u + (std::uint32_t) k * 104729u + seed) & 0xffff) / 65535.0f * 2.0f - 1.0f; };
                return r (i) + (r (i + 1) - r (i)) * f;
            };
            p.shoulder = { 1.5f + 1.3f * jitter (1), -1.5f + 1.3f * jitter (2) };
            p.elbow = { 1.0f * jitter (3), 1.0f * jitter (4) };
            p.hip = { 0.4f * jitter (5), 0.4f * jitter (6) };
            p.knee = { -0.6f * std::abs (jitter (7)), 0.6f * std::abs (jitter (8)) };
            p.lean = 0.15f * jitter (9);
            p.head = 0.3f * jitter (10);
            p.y = 0.05f * std::abs (jitter (11));
            p.x = 0.04f * jitter (12);
            p.tail = 1.2f * jitter (13);
            break;
        }
        case spin:
        {
            p.turn = std::cos (twoPi * phase);
            p.shoulder = { 1.55f, -1.55f }; // arms out
            p.elbow = { 0.1f * s, -0.1f * s };
            p.head = 0.15f * s;
            p.hip = { 0.05f, -0.05f };
            p.y = 0.01f * std::abs (s2);
            p.tail = 1.0f;
            break;
        }
        case held:
        {
            p.shoulder = { 2.4f, -2.4f }; // arms up in a V
            p.elbow = { -0.1f, 0.1f };
            p.hip = { 0.3f, -0.3f };
            p.head = -0.1f;
            p.y = 0.005f * s;
            p.tail = 0.4f;
            break;
        }
        default: break;
    }
    return p;
}

// ---------------------------------------------------------------------------------------------------------------
// Sprite sheets in FL's format (issue #121; FL's code: REA_rodada190ba-121.md)
// ---------------------------------------------------------------------------------------------------------------
std::vector<std::string> sheetNames (const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t at = text.rfind ("\xef\xbb\xbf", 0) == 0 ? 3 : 0;
    while (at < text.size())
    {
        auto end = text.find_first_of ("\r\n", at);
        if (end == std::string::npos)
            end = text.size();
        lines.push_back (toUtf8 (text.substr (at, end - at)));
        at = end;
        if (at < text.size())
            at += text[at] == '\r' && at + 1 < text.size() && text[at + 1] == '\n' ? 2 : 1;
    }
    return lines;
}

std::vector<std::string> defaultSheetNames()
{
    return { "Waiting", "Stepping", "Jumping", "Zombie", "Waving", "Hula", "Windmill", "Zitabata", "Dervish", "Held" };
}

std::string sheetNamesPath (const std::string& picturePath)
{
    const auto slash = picturePath.find_last_of ("/\\");
    const auto dot = picturePath.find_last_of ('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return picturePath + ".txt";
    return picturePath.substr (0, dot) + ".txt";
}

SheetLayout sheetLayout (int width, int height, int nameLines) noexcept
{
    SheetLayout l;
    l.rows = std::clamp (nameLines, 1, maxMoves);
    if (width < sheetColumns || height < l.rows)
        return l; // (FL stretches such a picture's canvas; nothing to show either way)
    l.frameWidth = width / sheetColumns;
    l.frameHeight = height / l.rows;
    l.valid = width % l.frameWidth == 0 && height % l.frameHeight == 0;
    return l;
}

int sheetMoveCount (int rows) noexcept { return std::clamp (rows, 1, maxMoves); }
int sheetHeldRow (int rows) noexcept { return std::max (rows, 1) - 1; }

std::vector<std::string> sheetMoveNames (const std::vector<std::string>& names)
{
    std::vector<std::string> moves { "Freestyle" };
    for (std::size_t i = 0; i + 1 < names.size(); ++i)
        moves.push_back (names[i]);
    return moves;
}

SheetAnimator::SheetAnimator (std::uint32_t seed) noexcept : random (seed != 0 ? seed : 1) {}

void SheetAnimator::draw (int rows) noexcept
{
    // FL: Random (rows - 2) + 1 when the sheet has three rows or more, else 0
    random = mixBits (random + 0x9e3779b9u);
    freestyleRow = rows >= 3 ? 1 + (int) (random % (std::uint32_t) (rows - 2)) : 0;
    drawnFor = rows;
}

int SheetAnimator::rowFor (int rows, int move, bool playing) const noexcept
{
    if (! playing)
        return 0;
    move = std::clamp (move, 0, rows - 1);
    return move == 0 ? freestyleRow : move - 1;
}

SheetFrame SheetAnimator::advance (double cycles, int rows, int move, bool mirrored, bool playing, float blend, bool held) noexcept
{
    rows = std::clamp (rows, 1, maxMoves);
    if (drawnFor != rows)
        draw (rows);
    if (! std::isfinite (cycles))
        cycles = 0.0;
    const auto phase = cycles - std::floor (cycles);
    if (phase <= lastPhase) // a cycle begins: the row changes now
    {
        shownRow = rowFor (rows, move, playing);
        shownMirrored = mirrored;
        draw (rows);
    }
    lastPhase = phase;

    SheetFrame f;
    const auto steps = phase * sheetColumns;
    f.column = std::clamp ((int) std::floor (steps), 0, sheetColumns - 1);
    const auto within = (float) (steps - std::floor (steps));
    f.row = held ? sheetHeldRow (rows) : std::clamp (shownRow, 0, rows - 1);
    f.mirrored = ! held && shownMirrored; // (FL shows the held row unmirrored)
    const auto b = held ? 0.7f : std::clamp (blend, 0.0f, 1.0f);
    f.nextRow = f.row;
    f.nextColumn = (f.column + 1) % sheetColumns;
    if (b > 0.0f && within >= 1.0f - b)
    {
        // FL's steps of 1/255
        f.mix = std::clamp (std::round ((within - 1.0f + b) * 255.0f / b) / 255.0f, 0.0f, 1.0f);
        if (f.nextColumn == 0 && ! held)
            f.nextRow = std::clamp (rowFor (rows, move, playing), 0, rows - 1);
    }
    if (f.mix <= 0.0f)
    {
        f.nextRow = f.row;
        f.nextColumn = f.column;
    }
    return f;
}

// ---------------------------------------------------------------------------------------------------------------
// The silent instrument
// ---------------------------------------------------------------------------------------------------------------
void Signals::push (int key) noexcept
{
    const auto n = count.load (std::memory_order_relaxed);
    ring[n % ring.size()].store (key, std::memory_order_relaxed);
    count.store (n + 1, std::memory_order_release);
}

int Signals::pull (std::uint32_t& seen, int* keys, int max) const noexcept
{
    const auto n = count.load (std::memory_order_acquire);
    auto from = seen;
    if (n - from > (std::uint32_t) ring.size())
        from = n - (std::uint32_t) ring.size();
    int got = 0;
    for (auto i = from; i != n && got < max; ++i)
        keys[got++] = ring[i % ring.size()].load (std::memory_order_relaxed);
    seen = n;
    return got;
}

namespace
{
class DanceInstrument final : public Instrument
{
public:
    void prepare (double) noexcept override {}
    void noteOn (int key, float) noexcept override { signals.push (key); }
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
    bool idle() const noexcept override { return true; } // (never a voice: its render can be skipped)

    Signals signals;
};
} // namespace

std::unique_ptr<Instrument> makeInstrument()
{
    return std::make_unique<DanceInstrument>();
}

Signals* signalsOf (Instrument* instrument) noexcept
{
    auto* d = dynamic_cast<DanceInstrument*> (instrument);
    return d != nullptr ? &d->signals : nullptr;
}
} // namespace lynx::dance
