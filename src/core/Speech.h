#pragma once

// LYNX's speech synthesizer (round 168b): text in, a spoken sample out, one slice per word.
//
// FL Studio's "Speech synthesizer" (the .speech files of Data/Patches/Speech and the "Speech properties" form) drives
// Image-Line's Speaker_x64.dll, a wrapper around a licensed third-party engine (SoftVoice, Inc.'s sveng64.dll). The
// wrapper and FL's form are FL's own and were studied with REA (docs/analise/REA_rodada168b.md); LYNX follows them where
// they are FL's logic: the .speech file (an INI "[Speaker Settings]" section), its defaults, the text syntax (words split
// at spaces, "_" joins words into one slice, "word(N)" moves that word N semitones), pitch quantised to keyboard keys
// and clamped to the keyboard's range, the four styles and their file codes (0, 2, 3, 4), the three voicing modes,
// the 20 personality names, the 20..500 words per minute range and the file name the form suggests when saving.
// The voice itself is LYNX's own: a formant synthesizer with letter-to-sound rules written for LYNX (no code, rules,
// dictionary or data of the third-party engine), so it is LYNX's equivalent, not a copy of FL's sound. No JUCE.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lynx::speech
{
// The 20 personalities in FL's order (the file stores the index).
inline constexpr int personalityCount = 20;
const char* personalityName (int index) noexcept;  // "Male" ... "Choirboy"; "" when out of range
const char* personalityHint (int index) noexcept;  // FL's one-line description of each

// Intonation styles: the form's list order and the code the file stores (FL skips code 1).
enum class Style { natural, monotone, sing, random };
inline constexpr int styleCount = 4;
const char* styleName (Style style) noexcept;
const char* styleHint (Style style) noexcept;
int styleToCode (Style style) noexcept;            // natural 0, monotone 2, sing 3, random 4
Style styleFromCode (int code) noexcept;           // the form's mapping: index = code - (code != 0), clamped

// Voicing modes (the file stores the index).
enum class Voicing { normal, breathy, whispered };
inline constexpr int voicingCount = 3;
const char* voicingName (Voicing voicing) noexcept;
const char* voicingHint (Voicing voicing) noexcept;

// Word rate limits of the form's wheel (words per minute) and its default.
inline constexpr int minRate = 20, maxRate = 500, defaultRate = 138;
// The form's keyboard (keys 12..71, FL's key 57 = A 440 Hz): the voice pitch is stored in Hz, rounded, and the form
// snaps it to the nearest key.
inline constexpr int minKey = 12, maxKey = 71;
double keyToHz (int key) noexcept;                 // 440 * 2^((key - 57) / 12)
int hzToKey (double hz) noexcept;                  // nearest key (not clamped)
int keyToStoredPitch (int key) noexcept;           // round (keyToHz (key)): what the form writes

struct Settings
{
    // FL's defaults when a key is missing from the file (Colossus, 65 Hz, 138 WPM, Normal, Monotone).
    int personality = 11;
    int pitch = 65;                  // Hz
    int rate = defaultRate;          // words per minute
    Voicing voicing = Voicing::normal;
    Style style = Style::monotone;
    std::string text;                // UTF-8

    bool operator== (const Settings&) const = default;
};

// What choosing a personality in the dialog sets (pitch, rate, style, voicing): FL asks its engine for that voice's
// defaults; these are LYNX's own voices' defaults (several match FL's factory presets named after a voice).
Settings personalityDefaults (int personality, const Settings& keepText);

// The .speech file ("[Speaker Settings]" INI section, CRLF). parse() returns false when the text is empty (FL refuses
// such a file too); unknown keys are ignored, missing keys keep FL's defaults.
bool parse (std::string_view fileText, Settings& out);
std::string write (const Settings& settings);

// The file name FL's form suggests when you accept it: the text with its first letter and every letter after a space
// or "_" capitalised, only letters and digits kept, cut to 32 characters, then "_" and the personality name, ".speech".
std::string suggestedFileName (const Settings& settings);

// One spoken chunk of the text: a slice of the result.
struct Word
{
    std::string text;               // as written (with "_" shown as spaces)
    int semitones = 0;              // the "(N)" pitch offset
};
// FL's wrapper: the text split at spaces, "_" turned into spaces inside a chunk, a trailing "(N)" read as a pitch
// offset in semitones (a chunk that starts with "(" is spoken as written); empty chunks dropped.
std::vector<Word> splitWords (std::string_view text);

// The pitch a chunk is spoken at: the voice pitch snapped to the nearest semitone (from 16.35 Hz), moved by the
// chunk's offset and clamped to 10..988 Hz (FL's wrapper).
int wordPitchHz (int pitch, int semitones) noexcept;

struct Rendered
{
    std::vector<float> samples;      // mono, peak about -1 dBFS
    double sampleRate = 44100.0;
    std::vector<std::int64_t> wordStarts;  // frame where each word (slice) starts
    std::vector<std::string> wordNames;
};

// Speaks the settings. Deterministic (the Random style is seeded from the text). Any thread.
Rendered render (const Settings& settings, double sampleRate = 44100.0);

// ---- the text front end (exposed for tests) ----
// Phonemes of a chunk of text as space-separated ARPAbet-like codes ("HH AX L OW"), "_" for a pause, with a stress
// mark "1" after the stressed vowel of each content word.
std::string toPhonemes (std::string_view text);
// Numbers spelled out ("10" -> "ten", "1999" -> "one thousand nine hundred ninety nine").
std::string spellNumber (std::int64_t value);
} // namespace lynx::speech
