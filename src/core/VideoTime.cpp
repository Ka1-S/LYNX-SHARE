#include "core/VideoTime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace lynx::vtime
{
namespace
{
constexpr std::array<Rate, rateCount> rates { {
    { 24000, 1001, false }, { 24, 1, false }, { 25, 1, false }, { 30000, 1001, true }, { 30000, 1001, false },
    { 30, 1, false }, { 48, 1, false }, { 50, 1, false }, { 60000, 1001, true }, { 60000, 1001, false }, { 60, 1, false },
} };

constexpr std::array<const char*, rateCount> ids { "23976", "24", "25", "2997df", "2997", "30", "48", "50", "5994df", "5994", "60" };

bool drops (const Rate& r) noexcept
{
    return r.drop && r.den == 1001 && (r.nominal() == 30 || r.nominal() == 60);
}

std::int64_t droppedPerMinute (const Rate& r) noexcept { return r.nominal() == 60 ? 4 : 2; }
} // namespace

Rate rateAt (int index) noexcept
{
    return rates[(std::size_t) (index < 0 ? 0 : index >= rateCount ? rateCount - 1 : index)];
}

int indexOf (const Rate& r) noexcept
{
    for (int i = 0; i < rateCount; ++i)
        if (rates[(std::size_t) i] == r)
            return i;
    return -1;
}

std::string rateName (const Rate& r)
{
    char buf[48];
    if (r.den == 1)
        std::snprintf (buf, sizeof (buf), "%d fps", r.num);
    else if (r.den == 1001 && r.nominal() == 24)
        std::snprintf (buf, sizeof (buf), "23.976 fps");
    else
        std::snprintf (buf, sizeof (buf), "%.2f fps", std::floor (r.fps() * 100.0) / 100.0);
    std::string s = buf;
    if (drops (r))
        s += " drop frame";
    return s;
}

std::string rateId (const Rate& r)
{
    const auto i = indexOf (r);
    if (i >= 0)
        return ids[(std::size_t) i];
    return std::to_string (r.num) + "/" + std::to_string (r.den) + (r.drop ? "df" : "");
}

std::optional<Rate> rateFromId (const std::string& id)
{
    for (int i = 0; i < rateCount; ++i)
        if (id == ids[(std::size_t) i])
            return rates[(std::size_t) i];
    const auto slash = id.find ('/');
    if (slash == std::string::npos)
        return std::nullopt;
    Rate r;
    r.num = std::atoi (id.substr (0, slash).c_str());
    r.den = std::atoi (id.substr (slash + 1).c_str());
    r.drop = id.size() > 2 && id.compare (id.size() - 2, 2, "df") == 0;
    if (r.num <= 0 || r.den <= 0 || r.fps() < 1.0 || r.fps() > 1000.0)
        return std::nullopt;
    return r;
}

std::optional<Rate> nearestRate (double fps) noexcept
{
    if (! (fps >= 1.0))
        return std::nullopt;
    std::optional<Rate> best;
    double err = 1e9;
    for (const auto& r : rates)
    {
        if (r.drop)
            continue; // a file's rate says nothing about how its timecode is labelled
        const auto e = std::abs (r.fps() - fps);
        if (e < err)
        {
            err = e;
            best = r;
        }
    }
    if (err > 0.01 * fps)
    {
        // an unlisted rate (12.5, 120 …): keep it as it is (rounded to a millesimal fraction)
        Rate r;
        r.num = (int) std::llround (fps * 1000.0);
        r.den = 1000;
        return r;
    }
    return best;
}

Timecode toTimecode (std::int64_t frame, const Rate& r) noexcept
{
    Timecode tc;
    if (frame < 0)
    {
        tc.negative = true;
        frame = -frame;
    }
    const std::int64_t nominal = std::max (1, r.nominal());
    if (drops (r))
    {
        const auto d = droppedPerMinute (r);
        const auto perTen = nominal * 600 - d * 9;
        const auto perMinute = nominal * 60 - d;
        const auto tens = frame / perTen;
        const auto rest = frame % perTen;
        frame += d * 9 * tens;
        if (rest > d)
            frame += d * ((rest - d) / perMinute);
    }
    tc.frames = (int) (frame % nominal);
    const auto total = frame / nominal;
    tc.seconds = (int) (total % 60);
    tc.minutes = (int) (total / 60 % 60);
    tc.hours = (int) (total / 3600);
    return tc;
}

std::int64_t toFrame (const Timecode& tc, const Rate& r) noexcept
{
    const std::int64_t nominal = std::max (1, r.nominal());
    const std::int64_t totalMinutes = (std::int64_t) tc.hours * 60 + tc.minutes;
    std::int64_t frame = (totalMinutes * 60 + tc.seconds) * nominal + tc.frames;
    if (drops (r))
        frame -= droppedPerMinute (r) * (totalMinutes - totalMinutes / 10);
    return tc.negative ? -frame : frame;
}

std::int64_t frameAt (double seconds, const Rate& r) noexcept
{
    return (std::int64_t) std::floor (seconds * (double) r.num / (double) r.den + 1e-6);
}

double frameStart (std::int64_t frame, const Rate& r) noexcept
{
    return (double) frame * (double) r.den / (double) r.num;
}

std::string format (const Timecode& tc, const Rate& r)
{
    char buf[48];
    std::snprintf (buf, sizeof (buf), "%s%02d:%02d:%02d%c%02d", tc.negative ? "-" : "", tc.hours, tc.minutes, tc.seconds,
                   drops (r) ? ';' : ':', tc.frames);
    return buf;
}

std::string format (double seconds, const Rate& r)
{
    return format (toTimecode (frameAt (seconds, r), r), r);
}

std::optional<Timecode> parse (const std::string& typed, const Rate& r)
{
    std::string s;
    bool negative = false;
    for (const auto c : typed)
        if (! std::isspace ((unsigned char) c) || ! s.empty())
            s += c;
    while (! s.empty() && std::isspace ((unsigned char) s.back()))
        s.pop_back();
    if (! s.empty() && s[0] == '-')
    {
        negative = true;
        s.erase (0, 1);
    }
    if (s.empty())
        return std::nullopt;
    std::vector<int> fields;
    bool separated = false;
    for (const auto c : s)
        if (c == ':' || c == ';' || c == '.' || c == ' ')
            separated = true;
        else if (! std::isdigit ((unsigned char) c))
            return std::nullopt;
    if (separated)
    {
        std::string cur;
        for (std::size_t i = 0; i <= s.size(); ++i)
        {
            const auto c = i < s.size() ? s[i] : ':';
            if (std::isdigit ((unsigned char) c))
                cur += c;
            else
            {
                if (cur.empty() || cur.size() > 4)
                    return std::nullopt;
                fields.push_back (std::atoi (cur.c_str()));
                cur.clear();
            }
        }
    }
    else
    {
        // digits right-aligned: …HHMMSSFF
        if (s.size() > 8)
            return std::nullopt;
        while (s.size() % 2 != 0)
            s.insert (s.begin(), '0');
        for (std::size_t i = 0; i < s.size(); i += 2)
            fields.push_back (std::atoi (s.substr (i, 2).c_str()));
    }
    if (fields.empty() || fields.size() > 4)
        return std::nullopt;
    while (fields.size() < 4)
        fields.insert (fields.begin(), 0);
    Timecode tc { fields[0], fields[1], fields[2], fields[3], negative };
    if (tc.minutes > 59 || tc.seconds > 59 || tc.frames >= std::max (1, r.nominal()))
        return std::nullopt;
    if (drops (r) && tc.seconds == 0 && tc.minutes % 10 != 0 && tc.frames < droppedPerMinute (r))
        tc.frames = (int) droppedPerMinute (r); // a dropped label: the first frame of that minute
    return tc;
}

std::optional<double> parseSeconds (const std::string& typed, const Rate& r)
{
    const auto tc = parse (typed, r);
    if (! tc)
        return std::nullopt;
    return frameStart (toFrame (*tc, r), r);
}
} // namespace lynx::vtime
