#include "core/KeyFollow.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "core/AudioEditTools.h"
#include "core/Instruments.h"

namespace lynx::keyfollow
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// "C", "C#", "Db", "Csharp", "Dflat" -> pitch class; the rest of the token after the note goes to `rest`
std::optional<int> noteOf (const std::string& token, std::string& rest)
{
    if (token.empty())
        return std::nullopt;
    static constexpr int natural[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = (char) std::toupper ((unsigned char) token[0]);
    if (letter < 'A' || letter > 'G')
        return std::nullopt;
    int pc = natural[letter - 'A'];
    std::size_t i = 1;
    const auto tail = lower (token.substr (1));
    if (tail.rfind ("sharp", 0) == 0)
        pc += 1, i += 5;
    else if (tail.rfind ("flat", 0) == 0)
        pc -= 1, i += 4;
    else if (i < token.size() && token[i] == '#')
        pc += 1, ++i;
    else if (i < token.size() && token[i] == 'b' && (token.size() == 2 || lower (token.substr (2)).rfind ("m", 0) == 0))
        pc -= 1, ++i; // "Bb", "Ebm", "Abmaj" (a 'b' then nothing or a mode word)
    rest = token.substr (i);
    return ((pc % 12) + 12) % 12;
}

// m / min / minor -> minor, maj / major -> major, M -> major; nullopt for anything else ("" too)
std::optional<bool> modeWord (const std::string& word)
{
    if (word == "M")
        return false;
    const auto w = lower (word);
    if (w == "m" || w == "min" || w == "minor")
        return true;
    if (w == "maj" || w == "major")
        return false;
    return std::nullopt;
}

bool isTempoToken (const std::string& t)
{
    // "120", "120bpm", "98BPM"
    std::size_t digits = 0;
    while (digits < t.size() && std::isdigit ((unsigned char) t[digits]))
        ++digits;
    if (digits < 2 || digits > 3 || (digits < t.size() && lower (t.substr (digits)) != "bpm"))
        return false;
    const auto v = std::atoi (t.substr (0, digits).c_str());
    return v >= 60 && v <= 200;
}

constexpr std::array<double, 12> majorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr std::array<double, 12> minorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

double correlation (const std::array<double, 12>& x, const std::array<double, 12>& profile, int root)
{
    double mx = 0, my = 0;
    for (int i = 0; i < 12; ++i)
        mx += x[(std::size_t) i], my += profile[(std::size_t) i];
    mx /= 12.0, my /= 12.0;
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < 12; ++i)
    {
        const auto a = x[(std::size_t) ((i + root) % 12)] - mx, b = profile[(std::size_t) i] - my;
        sxy += a * b, sxx += a * a, syy += b * b;
    }
    return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
}

int relativeMajorRoot (const LoopKey& k) noexcept { return (k.root + (k.minor ? 3 : 0)) % 12; }
} // namespace

std::string keyName (const LoopKey& key) { return projectkey::rootName (key.root) + (key.minor ? " minor" : " major"); }

std::optional<LoopKey> keyInName (const std::string& fileName)
{
    auto name = fileName;
    if (const auto dot = name.find_last_of ('.'); dot != std::string::npos && dot > 0 && name.size() - dot <= 5)
        name = name.substr (0, dot);
    std::vector<std::string> tokens;
    std::string cur;
    for (const char c : name)
    {
        if (std::isalnum ((unsigned char) c) || c == '#')
            cur += c;
        else if (! cur.empty())
            tokens.push_back (cur), cur.clear();
    }
    if (! cur.empty())
        tokens.push_back (cur);
    // "120Am" / "Am120": a tempo glued to the key is split off
    std::vector<std::string> split;
    for (const auto& t : tokens)
    {
        std::size_t d = 0;
        while (d < t.size() && std::isdigit ((unsigned char) t[d]))
            ++d;
        if (d > 0 && d < t.size() && std::isalpha ((unsigned char) t[d]) && lower (t.substr (d)) != "bpm")
        {
            split.push_back (t.substr (0, d));
            split.push_back (t.substr (d));
            continue;
        }
        std::size_t e = t.size();
        while (e > 0 && std::isdigit ((unsigned char) t[e - 1]))
            --e;
        if (e > 0 && e < t.size() && t.size() - e >= 2)
        {
            split.push_back (t.substr (0, e));
            split.push_back (t.substr (e));
            continue;
        }
        split.push_back (t);
    }
    tokens = std::move (split);
    const bool tempo = std::any_of (tokens.begin(), tokens.end(), isTempoToken);

    std::optional<LoopKey> bare;
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        std::string rest;
        const auto pc = noteOf (tokens[i], rest);
        if (! pc)
            continue;
        const bool upper = std::isupper ((unsigned char) tokens[i][0]) != 0;
        if (! rest.empty())
        {
            if (const auto minor = modeWord (rest))
            {
                // "am" alone is a word; "amin" / "aminor" are keys
                if (! upper && lower (rest) == "m")
                    continue;
                return LoopKey { *pc, *minor };
            }
            continue;
        }
        // a note alone: the next token may be its mode ("A minor")
        if (i + 1 < tokens.size())
            if (const auto minor = modeWord (tokens[i + 1]); minor && (upper || tokens[i + 1].size() > 1))
                return LoopKey { *pc, *minor };
        if (upper && tempo && ! bare && tokens[i].size() <= 2)
            bare = LoopKey { *pc, false };
    }
    return bare;
}

Detection detectKey (const SampleData& audio)
{
    Detection out;
    out.chroma.assign (12, 0.0f);
    const auto rate = audio.sampleRate > 0.0 ? audio.sampleRate : 44100.0;
    const auto total = std::min<std::size_t> (audio.frames(), (std::size_t) (rate * 30.0));
    constexpr int n = 8192, hop = 4096;
    if (total < (std::size_t) n)
        return out;
    const auto* l = audio.leftData();
    const auto* r = audio.rightData();
    std::vector<double> window (n);
    for (int i = 0; i < n; ++i)
        window[(std::size_t) i] = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * i / (n - 1));
    const auto lo = std::max (2, (int) std::floor (55.0 * n / rate));
    const auto hi = std::min (n / 2 - 2, (int) std::ceil (2100.0 * n / rate));
    std::array<double, 12> chroma {};
    std::vector<double> re (n), im (n), mag ((std::size_t) n / 2);
    for (std::size_t start = 0; start + (std::size_t) n <= total; start += (std::size_t) hop)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto x = (double) l[start + (std::size_t) i] + (r != nullptr && r != l ? (double) r[start + (std::size_t) i] : (double) l[start + (std::size_t) i]);
            re[(std::size_t) i] = 0.5 * x * window[(std::size_t) i];
            im[(std::size_t) i] = 0.0;
        }
        audioedit::fftInPlace (re, im, false);
        for (int k = 0; k < n / 2; ++k)
            mag[(std::size_t) k] = std::hypot (re[(std::size_t) k], im[(std::size_t) k]);
        // spectral peaks well above their neighbourhood (noise and drums give few), parabolic frequency
        for (int k = lo; k <= hi; ++k)
        {
            const auto m = mag[(std::size_t) k];
            if (m <= mag[(std::size_t) k - 1] || m < mag[(std::size_t) k + 1])
                continue;
            double local = 0.0;
            int count = 0;
            for (int j = std::max (1, k - 24); j <= std::min (n / 2 - 1, k + 24); ++j)
                if (std::abs (j - k) > 2)
                    local += mag[(std::size_t) j], ++count;
            local /= std::max (1, count);
            if (m < 4.0 * local)
                continue;
            const auto a = mag[(std::size_t) k - 1], c = mag[(std::size_t) k + 1];
            const auto denom = a - 2.0 * m + c;
            const auto delta = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
            const auto freq = ((double) k + delta) * rate / n;
            const auto midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            const auto pc = ((int) std::lround (midi) % 12 + 12) % 12;
            chroma[(std::size_t) pc] += m - local;
        }
    }
    double sum = 0.0;
    for (auto v : chroma)
        sum += v;
    if (sum <= 0.0)
        return out;
    for (int i = 0; i < 12; ++i)
        out.chroma[(std::size_t) i] = (float) (chroma[(std::size_t) i] / sum);

    struct Score
    {
        LoopKey key;
        double r;
    };
    std::vector<Score> scores;
    for (int root = 0; root < 12; ++root)
    {
        scores.push_back ({ { root, false }, correlation (chroma, majorProfile, root) });
        scores.push_back ({ { root, true }, correlation (chroma, minorProfile, root) });
    }
    std::sort (scores.begin(), scores.end(), [] (const Score& a, const Score& b) { return a.r > b.r; });
    const auto& best = scores.front();
    double other = -1.0;
    for (const auto& s : scores)
        if (relativeMajorRoot (s.key) != relativeMajorRoot (best.key))
        {
            other = s.r;
            break;
        }
    out.correlation = (float) best.r;
    out.margin = (float) (best.r - other);
    // a pure tone or a single chord also correlates: it needs at least three pitch classes of weight
    int strong = 0;
    for (auto v : out.chroma)
        strong += v >= 0.05f ? 1 : 0;
    if (best.r >= 0.6 && best.r - other >= 0.08 && strong >= 3)
        out.key = best.key;
    return out;
}

std::optional<LoopKey> loopKeyOf (const Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return std::nullopt;
    const auto& s = song.channels[(std::size_t) channel].sampler;
    if (s.loopKeyRoot < 0)
        return std::nullopt;
    return LoopKey { s.loopKeyRoot % 12, s.loopKeyMinor };
}

void setLoopKey (Song& song, int channel, std::optional<LoopKey> key, int source)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return;
    auto& s = song.channels[(std::size_t) channel].sampler;
    s.loopKeyRoot = key ? ((key->root % 12) + 12) % 12 : -1;
    s.loopKeyMinor = key && key->minor;
    s.loopKeySource = key ? (std::uint8_t) std::clamp (source, 0, 3) : (std::uint8_t) 0;
    projectkey::retuneFollowers (song);
}

void setFollow (Song& song, const std::vector<int>& clips, bool on)
{
    const auto project = projectkey::get (song);
    for (const auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
        {
            auto& c = song.playlist[(std::size_t) i];
            c.followKey = on;
            projectkey::retuneClip (c, song, project);
        }
}

int shiftOf (const Song& song, const Clip& clip)
{
    const auto project = projectkey::get (song);
    if (clip.kind != ClipKind::audio || ! clip.followKey || ! project)
        return 0;
    const auto key = loopKeyOf (song, clip.channel);
    return key ? projectkey::loopShift (key->root, key->minor, *project) : 0;
}

projectkey::Key projectKeyFor (const LoopKey& key)
{
    const auto order = projectkey::commonFirst(); // Major (Ionian), Minor Natural (Aeolian) first
    return { ((key.root % 12) + 12) % 12, order[key.minor ? 1u : 0u] };
}
} // namespace lynx::keyfollow
