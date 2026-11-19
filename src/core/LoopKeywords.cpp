#include "core/LoopKeywords.h"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "core/KeyFollow.h"
#include "core/PreviewTempo.h"
#include "core/SearchQuery.h"

#ifndef TR_NOOP
#define TR_NOOP(text) text // marks the names for translation (ui/I18n.h defines the same)
#endif

namespace lynx::loopkeys
{
namespace
{
using C = Category;
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// The grids, column by column (GarageBand's columns hold one family each: drums | keys and synths | guitars and basses
// | orchestra, voices and effects); vocabulary() interleaves them row by row.
const std::vector<std::vector<Keyword>>& instrumentColumns()
{
    static const std::vector<std::vector<Keyword>> cols {
        { { "all drums", TR_NOOP ("All Drums"), C::instrument, { "drum", "drums", "drumkit", "kit", "beat", "beats", "break", "breakbeat", "percussion", "perc" }, {} },
          { "kits", TR_NOOP ("Kits"), C::instrument, { "kit", "kits", "drumkit" }, { "all drums" } },
          { "beats", TR_NOOP ("Beats"), C::instrument, { "beat", "beats", "break", "breaks", "breakbeat", "drumloop", "drum loop", "top loop", "toploop" }, { "all drums" } },
          { "percussion", TR_NOOP ("Percussion"), C::instrument, { "perc", "percussion", "shaker", "tambourine", "conga", "bongo", "cowbell", "woodblock", "timbale", "guiro", "triangle" }, { "all drums" } },
          { "kick", TR_NOOP ("Kick"), C::instrument, { "kick", "kik", "bd", "bassdrum", "bass drum" }, { "all drums" } },
          { "snare", TR_NOOP ("Snare"), C::instrument, { "snare", "snr", "sd", "rim", "rimshot", "sidestick" }, { "all drums" } },
          { "hi-hat", TR_NOOP ("Hi-Hat"), C::instrument, { "hat", "hihat", "hi hat", "hh", "openhat", "closedhat", "open hat", "closed hat" }, { "all drums" } },
          { "clap", TR_NOOP ("Clap"), C::instrument, { "clap", "snap", "fingersnap" }, { "all drums" } },
          { "cymbal", TR_NOOP ("Cymbal"), C::instrument, { "cymbal", "crash", "ride", "splash", "china" }, { "all drums" } },
          { "toms", TR_NOOP ("Toms"), C::instrument, { "tom", "toms", "floortom" }, { "all drums" } },
          { "shaker", TR_NOOP ("Shaker"), C::instrument, { "shaker", "shakers", "maraca", "maracas", "cabasa", "tambourine", "tamb" }, { "percussion", "all drums" } },
          { "conga", TR_NOOP ("Conga / Bongo"), C::instrument, { "conga", "congas", "bongo", "bongos", "djembe", "tabla" }, { "percussion", "all drums" } } },
        { { "piano", TR_NOOP ("Piano"), C::instrument, { "piano", "grand", "keys" }, {} },
          { "synths", TR_NOOP ("Synths"), C::instrument, { "synth", "synths", "saw", "supersaw", "analog", "moog", "juno" }, {} },
          { "elec piano", TR_NOOP ("Elec Piano"), C::instrument, { "rhodes", "epiano", "e piano", "electric piano", "wurli", "wurlitzer", "ep" }, {} },
          { "organ", TR_NOOP ("Organ"), C::instrument, { "organ", "hammond", "b3" }, {} },
          { "pads", TR_NOOP ("Pads"), C::instrument, { "pad", "pads", "atmosphere", "atmos", "drone" }, { "synths" } },
          { "leads", TR_NOOP ("Leads"), C::instrument, { "lead", "leads" }, { "synths" } },
          { "plucks", TR_NOOP ("Plucks"), C::instrument, { "pluck", "plucks", "pizz", "pizzicato" }, {} },
          { "arps", TR_NOOP ("Arpeggios"), C::instrument, { "arp", "arps", "arpeggio", "arpeggiated", "arpeggios" }, {} },
          { "mallets", TR_NOOP ("Mallets"), C::instrument, { "mallet", "mallets", "marimba", "vibes", "vibraphone", "xylophone", "glock", "glockenspiel", "kalimba" }, {} },
          { "bell", TR_NOOP ("Bell"), C::instrument, { "bell", "bells", "chime", "chimes", "celesta" }, {} },
          { "chords", TR_NOOP ("Chords"), C::instrument, { "chord", "chords", "stab", "stabs" }, {} },
          { "textures", TR_NOOP ("Textures"), C::instrument, { "texture", "textures", "ambience", "ambient", "soundscape", "foley" }, {} } },
        { { "guitars", TR_NOOP ("Guitars"), C::instrument, { "guitar", "guitars", "gtr", "riff", "strum", "strumming" }, {} },
          { "acoustic guitar", TR_NOOP ("Acoustic Guitar"), C::instrument, { "acoustic guitar", "nylon", "steelstring", "classical guitar" }, { "guitars", "acoustic" } },
          { "elec guitar", TR_NOOP ("Elec Guitar"), C::instrument, { "electric guitar", "egtr", "e guitar", "strat", "telecaster", "les paul" }, { "guitars", "electric" } },
          { "bass", TR_NOOP ("Bass"), C::instrument, { "bass", "bassline", "basses" }, {} },
          { "synth bass", TR_NOOP ("Synthetic Bass"), C::instrument, { "synth bass", "synthbass", "reese", "wobble", "sub", "subbass", "sub bass" }, { "bass" } },
          { "808", TR_NOOP ("808"), C::instrument, { "808", "808s" }, {} },
          { "elec bass", TR_NOOP ("Elec Bass"), C::instrument, { "electric bass", "e bass", "slap", "fretless", "precision", "jazz bass" }, { "bass" } },
          { "acoustic bass", TR_NOOP ("Acoustic Bass"), C::instrument, { "acoustic bass", "upright bass", "double bass", "contrabass" }, { "bass" } },
          { "strings", TR_NOOP ("Strings"), C::instrument, { "string", "strings", "ensemble strings", "orchestra", "orchestral", "violin", "viola", "cello", "harp" }, {} },
          { "violin", TR_NOOP ("Violin"), C::instrument, { "violin", "violins", "fiddle", "viola" }, { "strings" } },
          { "cello", TR_NOOP ("Cello"), C::instrument, { "cello", "cellos" }, { "strings" } },
          { "harp", TR_NOOP ("Harp"), C::instrument, { "harp", "harps" }, { "strings" } } },
        { { "brass", TR_NOOP ("Brass"), C::instrument, { "brass", "trumpet", "trombone", "horn", "horns", "tuba", "flugelhorn" }, {} },
          { "woodwind", TR_NOOP ("Woodwind"), C::instrument, { "woodwind", "woodwinds", "flute", "clarinet", "oboe", "bassoon", "sax", "saxophone" }, {} },
          { "saxophone", TR_NOOP ("Saxophone"), C::instrument, { "sax", "saxophone", "alto", "tenor sax" }, { "woodwind" } },
          { "flute", TR_NOOP ("Flute"), C::instrument, { "flute", "flutes", "piccolo", "recorder", "panflute", "pan flute" }, { "woodwind" } },
          { "horn", TR_NOOP ("Horn"), C::instrument, { "horn", "horns", "french horn" }, { "brass" } },
          { "vocals", TR_NOOP ("Vocals"), C::instrument, { "vocal", "vocals", "vox", "voice", "voices", "acapella", "acappella", "chant", "adlib", "adlibs", "sing" }, {} },
          { "choir", TR_NOOP ("Choir"), C::instrument, { "choir", "choirs", "chorale", "aahs", "oohs" }, { "vocals" } },
          { "female", TR_NOOP ("Female"), C::instrument, { "female", "woman", "girl", "fem" }, { "vocals" } },
          { "male", TR_NOOP ("Male"), C::instrument, { "male", "man", "boy" }, { "vocals" } },
          { "sound effects", TR_NOOP ("Sound Effects"), C::instrument, { "fx", "sfx", "effect", "effects", "noise", "sweep", "whoosh", "glitch", "vinyl", "foley", "transition" }, {} },
          { "impacts", TR_NOOP ("Impacts"), C::instrument, { "impact", "impacts", "boom", "slam", "downlifter" }, { "sound effects" } },
          { "risers", TR_NOOP ("Risers"), C::instrument, { "riser", "risers", "uplifter", "build", "buildup", "rise" }, { "sound effects" } } }
    };
    return cols;
}

const std::vector<std::vector<Keyword>>& genreColumns()
{
    static const std::vector<std::vector<Keyword>> cols {
        { { "hip hop", TR_NOOP ("Hip Hop"), C::genre, { "hip hop", "hiphop", "rap", "boombap", "boom bap" }, {} },
          { "trap", TR_NOOP ("Trap"), C::genre, { "trap", "drill" }, {} },
          { "lo-fi", TR_NOOP ("Lo-Fi"), C::genre, { "lofi", "lo fi", "chillhop" }, {} },
          { "rnb", TR_NOOP ("R&B / Soul"), C::genre, { "rnb", "r b", "soul", "neosoul" }, {} },
          { "funk", TR_NOOP ("Funk / Disco"), C::genre, { "funk", "funky", "disco", "boogie" }, {} } },
        { { "house", TR_NOOP ("House"), C::genre, { "house", "deephouse", "deep house", "techhouse", "garage" }, { "electronic" } },
          { "techno", TR_NOOP ("Techno"), C::genre, { "techno", "minimal", "industrial" }, { "electronic" } },
          { "electronic", TR_NOOP ("Electronic"), C::genre, { "edm", "electronic", "electro", "dance", "trance", "synthwave", "future" }, {} },
          { "dnb", TR_NOOP ("Drum & Bass"), C::genre, { "dnb", "drum and bass", "drumnbass", "jungle", "breakbeat" }, { "electronic" } },
          { "dubstep", TR_NOOP ("Dubstep"), C::genre, { "dubstep", "riddim", "dub" }, { "electronic" } } },
        { { "pop", TR_NOOP ("Pop"), C::genre, { "pop", "kpop", "jpop" }, {} },
          { "rock", TR_NOOP ("Rock / Blues"), C::genre, { "rock", "blues", "punk", "metal", "grunge", "indie" }, {} },
          { "jazz", TR_NOOP ("Jazz"), C::genre, { "jazz", "jazzy", "swing", "bebop", "bossa" }, {} },
          { "country", TR_NOOP ("Country / Folk"), C::genre, { "country", "folk", "bluegrass", "americana" }, {} },
          { "reggae", TR_NOOP ("Reggae"), C::genre, { "reggae", "dancehall", "ska" }, {} } },
        { { "latin", TR_NOOP ("Latin"), C::genre, { "latin", "reggaeton", "salsa", "samba", "cumbia", "baile", "funk carioca", "bossa" }, {} },
          { "afro", TR_NOOP ("Afrobeats"), C::genre, { "afro", "afrobeat", "afrobeats", "amapiano" }, {} },
          { "world", TR_NOOP ("World"), C::genre, { "world", "ethnic", "oriental", "arabic", "indian", "asian", "african" }, {} },
          { "cinematic", TR_NOOP ("Cinematic"), C::genre, { "cinematic", "film", "trailer", "epic" }, {} },
          { "ambient", TR_NOOP ("Ambient / Chill"), C::genre, { "ambient", "chill", "chillout", "downtempo" }, {} } }
    };
    return cols;
}

const std::vector<std::vector<Keyword>>& descriptorColumns()
{
    static const std::vector<std::vector<Keyword>> cols {
        { { "single", TR_NOOP ("Single"), C::descriptor, { "solo", "single" }, {} },
          { "part", TR_NOOP ("Part"), C::descriptor, { "part", "phrase", "riff", "melody", "line" }, {} },
          { "acoustic", TR_NOOP ("Acoustic"), C::descriptor, { "acoustic", "unplugged", "nylon", "upright" }, {} },
          { "clean", TR_NOOP ("Clean"), C::descriptor, { "clean" }, {} },
          { "dry", TR_NOOP ("Dry"), C::descriptor, { "dry" }, {} } },
        { { "ensemble", TR_NOOP ("Ensemble"), C::descriptor, { "ensemble", "section", "band", "orchestra", "choir" }, {} },
          { "fill", TR_NOOP ("Fill"), C::descriptor, { "fill", "fills", "roll", "turnaround" }, {} },
          { "electric", TR_NOOP ("Electric"), C::descriptor, { "electric", "electronic", "synth", "edm" }, {} },
          { "distorted", TR_NOOP ("Distorted"), C::descriptor, { "distorted", "distortion", "dist", "overdrive", "fuzz", "saturated", "crushed", "bitcrushed", "dirty", "gritty" }, {} },
          { "processed", TR_NOOP ("Processed"), C::descriptor, { "processed", "wet", "fx", "reverb", "delay", "filtered", "chopped", "reversed", "reverse", "glitch" }, {} } },
        { { "grooving", TR_NOOP ("Grooving"), C::descriptor, { "groove", "grooving", "groovy", "swing", "bounce", "bouncy", "funky" }, {} },
          { "melodic", TR_NOOP ("Melodic"), C::descriptor, { "melodic", "melody", "lead", "hook", "arp" }, {} },
          { "relaxed", TR_NOOP ("Relaxed"), C::descriptor, { "relaxed", "chill", "calm", "soft", "mellow", "smooth", "lofi", "gentle" }, {} },
          { "cheerful", TR_NOOP ("Cheerful"), C::descriptor, { "cheerful", "happy", "bright", "uplifting", "sunny", "joy" }, {} },
          { "loop", TR_NOOP ("Loop"), C::descriptor, { "loop", "loops" }, {} } },
        { { "arrhythmic", TR_NOOP ("Arrhythmic"), C::descriptor, { "arrhythmic", "freetime", "rubato", "drone" }, {} },
          { "dissonant", TR_NOOP ("Dissonant"), C::descriptor, { "dissonant", "atonal", "weird", "detuned", "eerie" }, {} },
          { "intense", TR_NOOP ("Intense"), C::descriptor, { "intense", "hard", "heavy", "aggressive", "punchy", "big", "loud", "massive" }, {} },
          { "dark", TR_NOOP ("Dark"), C::descriptor, { "dark", "sad", "moody", "evil", "horror", "deep" }, {} },
          { "one-shot", TR_NOOP ("One-Shot"), C::descriptor, { "oneshot", "one shot", "shot", "hit", "single hit" }, {} } }
    };
    return cols;
}

std::vector<Keyword> interleave()
{
    std::vector<Keyword> out;
    for (const auto* cols : { &instrumentColumns(), &genreColumns(), &descriptorColumns() })
    {
        std::size_t rows = 0;
        for (const auto& c : *cols)
            rows = std::max (rows, c.size());
        for (std::size_t r = 0; r < rows; ++r)
            for (const auto& c : *cols)
                if (r < c.size())
                    out.push_back (c[r]);
    }
    return out;
}

// the word index: a word (or "two words") -> the keywords it gives directly
const std::map<std::string, std::vector<const Keyword*>>& wordIndex()
{
    static const auto index = [] {
        std::map<std::string, std::vector<const Keyword*>> m;
        for (const auto& k : vocabulary())
            for (const auto* w : k.words)
                m[w].push_back (&k);
        return m;
    }();
    return index;
}

// what a whole name says about the scale: an explicit major / minor mark beside a root
bool hasMajorWord (const std::vector<std::string>& w)
{
    for (std::size_t i = 0; i < w.size(); ++i)
    {
        const auto& t = w[i];
        if (t == "maj" || t == "major")
            return true;
        if (t.size() >= 4 && t.size() <= 7 && (t.ends_with ("maj") || t.ends_with ("major")) && t[0] >= 'a' && t[0] <= 'g')
            return true;
    }
    return false;
}

bool isAudioExt (const std::string& e)
{
    static const std::set<std::string> exts { "wav", "aif", "aiff", "flac", "ogg", "mp3", "m4a", "wv", "ncw", "caf", "opus" };
    return exts.count (e) > 0;
}
} // namespace

namespace
{
FactorySource& factorySource()
{
    static FactorySource source;
    return source;
}
} // namespace

void setFactorySource (FactorySource source) { factorySource() = std::move (source); }

std::vector<std::string> factoryKeywords (const std::string& path)
{
    const auto& source = factorySource();
    return source != nullptr ? source (path) : std::vector<std::string> {};
}

const std::vector<Keyword>& vocabulary()
{
    static const auto all = interleave();
    return all;
}

const Keyword* find (const std::string& id)
{
    const auto key = lower (id);
    for (const auto& k : vocabulary())
        if (key == k.id || key == lower (k.label))
            return &k;
    return nullptr;
}

std::vector<const Keyword*> ofCategory (Category c)
{
    std::vector<const Keyword*> out;
    for (const auto& k : vocabulary())
        if (k.category == c)
            out.push_back (&k);
    return out;
}

int typeOf (const std::string& fileName)
{
    const auto dot = fileName.find_last_of ('.');
    if (dot == std::string::npos)
        return 0;
    const auto e = lower (fileName.substr (dot + 1));
    if (isAudioExt (e))
        return audio;
    if (e == "mid" || e == "midi")
        return midi;
    if (e == "fsc" || e == "fpt") // FL's score files (piano-roll patterns) and pattern presets
        return pattern;
    return 0;
}

std::vector<std::string> words (const std::string& path)
{
    auto p = path;
    const auto slash = p.find_last_of ("/\\");
    if (const auto dot = p.find_last_of ('.'); dot != std::string::npos && (slash == std::string::npos || dot > slash) && dot + 6 >= p.size())
        p.resize (dot);
    std::vector<std::string> out;
    std::string cur;
    const auto flush = [&] {
        if (! cur.empty())
            out.push_back (lower (cur));
        cur.clear();
    };
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        const auto c = p[i];
        if (std::isalnum ((unsigned char) c) == 0)
        {
            flush();
            continue;
        }
        // camelCase and letter/digit runs split ("DarkPad90" -> dark pad 90; "808s" stays as it is written)
        if (! cur.empty())
        {
            const auto prev = cur.back();
            const auto lowerToUpper = std::islower ((unsigned char) prev) != 0 && std::isupper ((unsigned char) c) != 0;
            const auto letterToDigit = std::isalpha ((unsigned char) prev) != 0 && std::isdigit ((unsigned char) c) != 0;
            if (lowerToUpper || letterToDigit)
                flush();
        }
        cur += c;
    }
    flush();
    return out;
}

std::set<std::string> keywordsOfWords (const std::vector<std::string>& w)
{
    std::set<std::string> out;
    const auto& index = wordIndex();
    const auto give = [&] (const std::string& word) {
        if (const auto it = index.find (word); it != index.end())
            for (const auto* k : it->second)
                out.insert (k->id);
    };
    // "bass drum" is a kick, not a bass: the pair is read first and its words are used up
    std::vector<char> used (w.size(), 0);
    for (std::size_t i = 0; i + 1 < w.size(); ++i)
    {
        const auto pair = w[i] + " " + w[i + 1];
        const auto joined = w[i] + w[i + 1];
        const auto before = out.size();
        give (pair);
        give (joined);
        if (out.size() != before)
            used[i] = used[i + 1] = 1;
    }
    for (std::size_t i = 0; i < w.size(); ++i)
    {
        if (used[i])
            continue;
        const auto& word = w[i];
        give (word);
        if (word.size() > 3 && word.back() == 's')
            give (word.substr (0, word.size() - 1));
    }
    // the keywords each implies (one level is enough: the lists name the top ones directly)
    std::set<std::string> implied;
    for (const auto& id : out)
        if (const auto* k = find (id))
            for (const auto* a : k->also)
                implied.insert (a);
    out.insert (implied.begin(), implied.end());
    return out;
}

LoopInfo describe (const std::string& path, const std::string& wordsOf, const std::vector<std::string>& tags, const std::vector<std::string>& factory)
{
    LoopInfo info;
    info.path = path;
    const auto slash = path.find_last_of ("/\\");
    auto fileName = slash == std::string::npos ? path : path.substr (slash + 1);
    info.type = typeOf (fileName);
    info.name = fileName;
    if (const auto dot = info.name.find_last_of ('.'); dot != std::string::npos && dot > 0)
        info.name.resize (dot);
    const auto w = words (wordsOf.empty() ? path : wordsOf);
    info.keywords = keywordsOfWords (w);
    for (const auto* list : { &tags, &factory })
        for (const auto& t : *list)
            if (const auto* k = find (t))
            {
                info.keywords.insert (k->id);
                for (const auto* a : k->also)
                    info.keywords.insert (a);
            }
    // tempo: "120 bpm" / "bpm120" in the name, or a bare 60…200 in a name that also says it is a loop or gives a key
    const auto nameWords = words (fileName);
    info.tempo = previewtempo::explicitNameTempo (info.name);
    const auto key = keyfollow::keyInName (fileName);
    if (info.tempo <= 0.0 && (key.has_value() || info.keywords.count ("loop") > 0 || info.keywords.count ("beats") > 0))
        info.tempo = previewtempo::bareNameTempo (info.name);
    // the scale: the name's key (minor, major, or a root alone = good for both); none for drums and effects
    if (key.has_value())
    {
        info.keyRoot = key->root;
        info.scale = key->minor ? Scale::minor : hasMajorWord (nameWords) ? Scale::major : Scale::both;
    }
    else
    {
        static const std::set<std::string> atonal { "all drums", "kits", "beats", "percussion", "shaker", "conga", "sound effects", "impacts", "risers", "kick", "snare", "hi-hat", "clap", "cymbal", "toms" };
        const auto tonalToo = std::any_of (info.keywords.begin(), info.keywords.end(), [] (const std::string& k) {
            const auto* kw = find (k);
            return kw != nullptr && kw->category == Category::instrument && atonal.count (k) == 0 && k != "808";
        });
        if (! tonalToo && std::any_of (info.keywords.begin(), info.keywords.end(), [] (const std::string& k) { return atonal.count (k) > 0; }))
            info.scale = Scale::neither;
    }
    return info;
}

bool matches (const LoopInfo& loop, const Filter& f, bool favourite)
{
    if ((loop.type & f.types) == 0)
        return false;
    if (f.favouritesOnly && ! favourite)
        return false;
    for (const auto& k : f.keywords)
        if (loop.keywords.count (k) == 0)
            return false;
    if (f.scale != Scale::any && loop.scale != f.scale)
        return false;
    if (f.tempoMin > 0.0 || f.tempoMax > 0.0)
    {
        if (! (loop.tempo > 0.0))
            return false;
        if (f.tempoMin > 0.0 && loop.tempo < f.tempoMin - 1.0e-9)
            return false;
        if (f.tempoMax > 0.0 && loop.tempo > f.tempoMax + 1.0e-9)
            return false;
    }
    if (f.nearKey >= 0)
    {
        // GarageBand: "loops in keys close to the project key (within two semitones)"; loops without a key (drums) stay
        if (loop.keyRoot >= 0 && rootDistance (loop.keyRoot, f.nearKey) > 2)
            return false;
    }
    if (! f.text.empty() && ! search::matches (f.text, loop.name))
        return false;
    return true;
}

const std::vector<TempoRange>& tempoRanges()
{
    static const std::vector<TempoRange> ranges { { TR_NOOP ("Any"), 0.0, 0.0 },       { TR_NOOP ("Near the project tempo"), -1.0, -1.0 }, { TR_NOOP ("Up to 90"), 1.0, 90.0 },
                                                  { TR_NOOP ("90 - 110"), 90.0, 110.0 }, { TR_NOOP ("110 - 130"), 110.0, 130.0 },           { TR_NOOP ("130 - 150"), 130.0, 150.0 },
                                                  { TR_NOOP ("150 and up"), 150.0, 999.0 } };
    return ranges;
}

std::pair<double, double> tempoBounds (int index, double bpm)
{
    const auto& r = tempoRanges();
    if (index <= 0 || index >= (int) r.size())
        return { 0.0, 0.0 };
    if (r[(std::size_t) index].min < 0.0)
    {
        const auto b = bpm > 0.0 ? bpm : 120.0;
        return { b * 0.94, b * 1.06 };
    }
    return { r[(std::size_t) index].min, r[(std::size_t) index].max };
}

int loopBars (double seconds, double tempo, int beatsPerBar) noexcept
{
    if (! (seconds > 0.0) || ! (tempo > 0.0) || beatsPerBar <= 0)
        return 0;
    const auto bars = seconds * tempo / 60.0 / beatsPerBar;
    return bars < 0.5 ? 0 : (int) std::lround (bars);
}

std::string tempoText (const LoopInfo& loop)
{
    if (! (loop.tempo > 0.0))
        return {};
    char text[32];
    if (std::abs (loop.tempo - std::round (loop.tempo)) < 0.005)
        std::snprintf (text, sizeof (text), "%d", (int) std::lround (loop.tempo));
    else
        std::snprintf (text, sizeof (text), "%.1f", loop.tempo);
    return text;
}

std::string keyText (const LoopInfo& loop)
{
    if (loop.keyRoot < 0)
        return {};
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (names[loop.keyRoot % 12]) + (loop.scale == Scale::minor ? "m" : "");
}

Layout Layout::defaults()
{
    Layout l;
    for (const auto& k : vocabulary())
        l.order[(std::size_t) k.category].push_back (k.id);
    return l;
}

bool Layout::swap (Category c, int a, int b)
{
    auto& o = order[(std::size_t) c];
    if (a == b || a < 0 || b < 0 || a >= (int) o.size() || b >= (int) o.size())
        return false;
    std::swap (o[(std::size_t) a], o[(std::size_t) b]);
    return true;
}

bool Layout::place (Category c, int at, const std::string& id)
{
    auto& o = order[(std::size_t) c];
    const auto* k = find (id);
    if (k == nullptr || at < 0 || at >= (int) o.size() || o[(std::size_t) at] == k->id)
        return false;
    const auto it = std::find (o.begin(), o.end(), std::string (k->id));
    if (it != o.end())
        std::iter_swap (it, o.begin() + at); // (shown already: the two places swap)
    else
        o[(std::size_t) at] = k->id; // (a keyword of another category: it takes the place)
    return true;
}

std::string Layout::toText() const
{
    static const char* names[] { "instrument", "genre", "descriptor" };
    std::ostringstream out;
    for (int c = 0; c < categoryCount; ++c)
    {
        out << names[c] << "=";
        for (std::size_t i = 0; i < order[(std::size_t) c].size(); ++i)
            out << (i ? "," : "") << order[(std::size_t) c][i];
        out << "\n";
    }
    return out.str();
}

Layout Layout::fromText (const std::string& text)
{
    const auto base = defaults();
    auto l = base;
    static const char* names[] { "instrument", "genre", "descriptor" };
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto name = line.substr (0, eq);
        for (int c = 0; c < categoryCount; ++c)
            if (name == names[c])
            {
                std::vector<std::string> ids;
                std::istringstream items (line.substr (eq + 1));
                std::string id;
                while (std::getline (items, id, ','))
                    if (const auto* k = find (id); k != nullptr && std::find (ids.begin(), ids.end(), std::string (k->id)) == ids.end())
                        ids.push_back (k->id);
                // the grid keeps its size: a missing default keyword fills the rest
                const auto& wanted = base.order[(std::size_t) c];
                for (const auto& d : wanted)
                    if (ids.size() < wanted.size() && std::find (ids.begin(), ids.end(), d) == ids.end())
                        ids.push_back (d);
                ids.resize (std::min (ids.size(), wanted.size()));
                l.order[(std::size_t) c] = ids;
            }
    }
    return l;
}
} // namespace lynx::loopkeys
