// LYNX's speech synthesizer: the text front end (round 168b). Numbers spelled out, a small dictionary of common words
// English letter-to-sound rules do not get right, and context rules in the style of the public-domain US Naval
// Research Laboratory report "Automatic translation of English text to phonetics by means of letter-to-sound rules"
// (Elovitz, Johnson, McHugh & Shore, NRL Report 7948, 1976), written out for LYNX. Nothing here comes from FL Studio or
// from the third-party engine FL uses (see core/Speech.h).

#include "core/Speech.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace lynx::speech
{
namespace
{
const char* const ones[] = { "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                             "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
                             "nineteen" };
const char* const tens[] = { "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety" };

void spellBelowThousand (int v, std::string& out)
{
    auto add = [&out] (const char* w) { if (! out.empty()) out += ' '; out += w; };
    if (v >= 100)
    {
        add (ones[v / 100]);
        add ("hundred");
        v %= 100;
        if (v == 0)
            return;
    }
    if (v < 20)
        add (ones[v]);
    else
    {
        add (tens[v / 10]);
        if (v % 10 != 0)
            add (ones[v % 10]);
    }
}

// ---- dictionary: words the rules get wrong (lower case -> phonemes, stress "1" on the main vowel) ----
struct Entry { const char* word; const char* phonemes; };
const Entry dictionary[] = {
    { "a", "AX" }, { "an", "AE1 N" }, { "and", "AE1 N D" }, { "are", "AA1 R" }, { "as", "AE1 Z" }, { "at", "AE1 T" },
    { "be", "B IY1" }, { "been", "B IH1 N" }, { "by", "B AY1" }, { "do", "D UW1" }, { "does", "D AH1 Z" },
    { "done", "D AH1 N" }, { "for", "F AO1 R" }, { "from", "F R AH1 M" }, { "go", "G OW1" }, { "has", "HH AE1 Z" },
    { "have", "HH AE1 V" }, { "he", "HH IY1" }, { "i", "AY1" }, { "i'm", "AY1 M" }, { "is", "IH1 Z" }, { "it", "IH1 T" },
    { "its", "IH1 T S" }, { "me", "M IY1" }, { "my", "M AY1" }, { "no", "N OW1" }, { "of", "AX V" }, { "oh", "OW1" },
    { "on", "AA1 N" }, { "one", "W AH1 N" }, { "or", "AO1 R" }, { "says", "S EH1 Z" }, { "said", "S EH1 D" },
    { "she", "SH IY1" }, { "so", "S OW1" }, { "some", "S AH1 M" }, { "the", "DH AX" }, { "there", "DH EH1 R" },
    { "their", "DH EH1 R" }, { "they", "DH EY1" }, { "to", "T UW1" }, { "too", "T UW1" }, { "two", "T UW1" },
    { "us", "AH1 S" }, { "was", "W AH1 Z" }, { "we", "W IY1" }, { "were", "W ER1" }, { "what", "W AH1 T" },
    { "where", "W EH1 R" }, { "who", "HH UW1" }, { "whose", "HH UW1 Z" }, { "why", "W AY1" }, { "with", "W IH1 DH" },
    { "would", "W UH1 D" }, { "could", "K UH1 D" }, { "should", "SH UH1 D" }, { "you", "Y UW1" },
    { "your", "Y AO1 R" }, { "yours", "Y AO1 R Z" }, { "yo", "Y OW1" }, { "ya", "Y AX" }, { "yes", "Y EH1 S" },
    { "eight", "EY1 T" }, { "four", "F AO1 R" }, { "nine", "N AY1 N" }, { "once", "W AH1 N S" },
    { "any", "EH1 N IY" }, { "many", "M EH1 N IY" }, { "only", "OW1 N L IY" }, { "every", "EH1 V R IY" },
    { "very", "V EH1 R IY" }, { "again", "AX G EH1 N" }, { "against", "AX G EH1 N S T" }, { "about", "AX B AW1 T" },
    { "because", "B IH K AO1 Z" }, { "into", "IH1 N T UW" }, { "people", "P IY1 P AX L" }, { "friend", "F R EH1 N D" },
    { "give", "G IH1 V" }, { "live", "L IH1 V" }, { "love", "L AH1 V" }, { "move", "M UW1 V" }, { "moves", "M UW1 V Z" },
    { "come", "K AH1 M" }, { "none", "N AH1 N" }, { "gone", "G AO1 N" }, { "know", "N OW1" }, { "new", "N UW1" },
    { "knew", "N UW1" }, { "now", "N AW1" }, { "how", "HH AW1" }, { "own", "OW1 N" }, { "low", "L OW1" },
    { "show", "SH OW1" }, { "slow", "S L OW1" }, { "flow", "F L OW1" }, { "bass", "B EY1 S" }, { "base", "B EY1 S" },
    { "music", "M Y UW1 Z IH K" }, { "rhythm", "R IH1 DH AX M" }, { "groove", "G R UW1 V" }, { "dance", "D AE1 N S" },
    { "party", "P AA1 R T IY" }, { "beat", "B IY1 T" }, { "speech", "S P IY1 CH" }, { "voice", "V OY1 S" },
    { "hello", "HH AX L OW1" }, { "okay", "OW K EY1" }, { "ok", "OW K EY1" }, { "dj", "D IY1 JH EY" },
    { "studio", "S T UW1 D IY OW" }, { "computer", "K AX M P Y UW1 T ER" }, { "robot", "R OW1 B AA T" },
    { "robots", "R OW1 B AA T S" }, { "human", "HH Y UW1 M AX N" }, { "alien", "EY1 L IY AX N" },
    { "machine", "M AX SH IY1 N" }, { "system", "S IH1 S T AX M" }, { "world", "W ER1 L D" }, { "girl", "G ER1 L" },
    { "woman", "W UH1 M AX N" }, { "women", "W IH1 M AX N" }, { "listen", "L IH1 S AX N" }, { "heart", "HH AA1 R T" },
    { "real", "R IY1 L" }, { "ready", "R EH1 D IY" }, { "baby", "B EY1 B IY" }, { "put", "P UH1 T" },
    { "push", "P UH1 SH" }, { "pull", "P UH1 L" }, { "full", "F UH1 L" }, { "generator", "JH EH1 N ER EY T ER" },
    { "problem", "P R AA1 B L AX M" }, { "future", "F Y UW1 CH ER" }, { "the", "DH AX" }, { "this", "DH IH1 S" },
    { "that", "DH AE1 T" }, { "then", "DH EH1 N" }, { "than", "DH AE1 N" }, { "them", "DH EH1 M" },
    { "these", "DH IY1 Z" }, { "those", "DH OW1 Z" }, { "thus", "DH AH1 S" }, { "though", "DH OW1" },
    { "through", "TH R UW1" }, { "lynx", "L IH1 NG K S" }, { "share", "SH EH1 R" }, { "zed", "Z EH1 D" },
    { "fruity", "F R UW1 T IY" }, { "loops", "L UW1 P S" }, { "synth", "S IH1 N TH" }, { "video", "V IH1 D IY OW" },
    { "audio", "AO1 D IY OW" }, { "radio", "R EY1 D IY OW" }, { "ladies", "L EY1 D IY Z" }, { "house", "HH AW1 S" },
    { "mouth", "M AW1 TH" }, { "sound", "S AW1 N D" }, { "sounds", "S AW1 N D Z" }, { "down", "D AW1 N" },
    { "star", "S T AA1 R" }, { "freedom", "F R IY1 D AX M" }, { "don't", "D OW1 N T" }, { "can't", "K AE1 N T" },
    { "won't", "W OW1 N T" }, { "it's", "IH1 T S" }, { "what's", "W AH1 T S" }, { "let's", "L EH1 T S" },
};

// ---- letter-to-sound rules: left context [match] right context -> phonemes ----
// Context symbols: '#' one or more vowels, ':' zero or more consonants, '^' one consonant, '.' a voiced consonant
// (b d g j l m n r v w z), '+' a front vowel (e i y), '%' a suffix (er e es ed ing ely), '&' a sibilant (s c g z x j
// ch sh), '@' t s r d l z n j th ch sh, ' ' a word boundary; letters and "'" match themselves.
struct Rule { const char* left; const char* match; const char* right; const char* out; };

const Rule rulesA[] = {
    { "", "a", " ", "AX" }, { " ", "are", " ", "AA R" }, { " ", "ar", "o", "AX R" }, { "", "ar", "#", "EH R" },
    { "^", "as", "#", "EY S" }, { "", "a", "wa", "AX" }, { "", "aw", "", "AO" }, { " :", "any", "", "EH N IY" },
    { "", "a", "^+#", "EY" }, { "#:", "ally", "", "AX L IY" }, { " ", "al", "#", "AX L" }, { "", "again", "", "AX G EH N" },
    { "#:", "ag", "e", "IH JH" }, { "", "a", "^+:#", "AE" }, { " :", "a", "^+ ", "EY" }, { "", "a", "^%", "EY" },
    { " ", "arr", "", "AX R" }, { "", "arr", "", "AE R" }, { " :", "ar", " ", "AA R" }, { "", "ar", " ", "ER" },
    { "", "ar", "", "AA R" }, { "", "air", "", "EH R" }, { "", "ai", "", "EY" }, { "", "ay", "", "EY" },
    { "", "au", "", "AO" }, { "#:", "al", " ", "AX L" }, { "#:", "als", " ", "AX L Z" }, { "", "alk", "", "AO K" },
    { "", "al", "^", "AO L" }, { " :", "able", "", "EY B AX L" }, { "", "able", "", "AX B AX L" },
    { "", "ang", "+", "EY N JH" }, { "", "a", "", "AE" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesB[] = {
    { " ", "be", "^#", "B IH" }, { "", "being", "", "B IY IH NG" }, { " ", "both", " ", "B OW TH" },
    { " ", "bus", "#", "B IH Z" }, { "", "buil", "", "B IH L" }, { "", "bb", "", "B" }, { "", "b", "", "B" },
    { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesC[] = {
    { " ", "ch", "^", "K" }, { "^e", "ch", "", "K" }, { "", "ch", "", "CH" }, { " s", "ci", "#", "S AY" },
    { "", "ci", "a", "SH" }, { "", "ci", "o", "SH" }, { "", "ci", "en", "SH" }, { "", "c", "+", "S" },
    { "", "ck", "", "K" }, { "", "com", "%", "K AH M" }, { "", "cc", "+", "K S" }, { "", "cc", "", "K" },
    { "", "c", "", "K" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesD[] = {
    { "#:", "ded", " ", "D IH D" }, { ".e", "d", " ", "D" }, { "#:^e", "d", " ", "T" }, { " ", "de", "^#", "D IH" },
    { " ", "do", " ", "D UW" }, { " ", "does", "", "D AH Z" }, { " ", "doing", "", "D UW IH NG" },
    { " ", "dow", "", "D AW" }, { "", "du", "a", "JH UW" }, { "", "dd", "", "D" }, { "", "d", "", "D" },
    { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesE[] = {
    { "#:", "e", " ", "" }, { "'^:", "e", " ", "" }, { " :", "e", " ", "IY" }, { "#", "ed", " ", "D" },
    { "#:", "e", "d ", "" }, { "", "ev", "er", "EH V" }, { "", "e", "^%", "IY" }, { "", "eri", "#", "IY R IY" },
    { "", "eri", "", "EH R IH" }, { "#:", "er", "#", "ER" }, { "", "er", "#", "EH R" }, { "", "er", "", "ER" },
    { " ", "even", "", "IY V EH N" }, { "#:", "e", "w", "" }, { "@", "ew", "", "UW" }, { "", "ew", "", "Y UW" },
    { "", "e", "o", "IY" }, { "#:&", "es", " ", "IH Z" }, { "#:", "e", "s ", "" }, { "#:", "ely", " ", "L IY" },
    { "#:", "ement", "", "M EH N T" }, { "", "eful", "", "F UH L" }, { "", "ee", "", "IY" }, { "", "earn", "", "ER N" },
    { " ", "ear", "^", "ER" }, { "", "ead", "", "EH D" }, { "#:", "ea", " ", "IY AX" }, { "", "ea", "su", "EH" },
    { "", "ea", "", "IY" }, { "", "eigh", "", "EY" }, { "", "ei", "", "IY" }, { " ", "eye", "", "AY" },
    { "", "ey", "", "IY" }, { "", "eu", "", "Y UW" }, { "", "e", "", "EH" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesF[] = {
    { "", "ful", "", "F UH L" }, { "", "ff", "", "F" }, { "", "f", "", "F" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesG[] = {
    { "", "giv", "", "G IH V" }, { " ", "g", "i^", "G" }, { "", "ge", "t", "G EH" }, { "su", "gges", "", "G JH EH S" },
    { "", "gg", "", "G" }, { " b#", "g", "", "G" }, { "", "g", "+", "JH" }, { "", "great", "", "G R EY T" },
    { "#", "gh", "", "" }, { "", "g", "", "G" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesH[] = {
    { " ", "hav", "", "HH AE V" }, { " ", "here", "", "HH IY R" }, { " ", "hour", "", "AW ER" },
    { "", "how", "", "HH AW" }, { "", "h", "#", "HH" }, { "", "h", "", "" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesI[] = {
    { " ", "in", "", "IH N" }, { " ", "i", " ", "AY" }, { "", "in", "d", "AY N" }, { "", "ier", "", "IY ER" },
    { "#:r", "ied", "", "IY D" }, { "", "ied", " ", "AY D" }, { "", "ien", "", "IY EH N" }, { "", "ie", "t", "AY EH" },
    { " :", "i", "%", "AY" }, { "", "i", "%", "IY" }, { "", "ie", "", "IY" }, { "", "i", "^+:#", "IH" },
    { "", "ir", "#", "AY R" }, { "", "iz", "%", "AY Z" }, { "", "is", "%", "AY Z" }, { "", "i", "d%", "AY" },
    { "+^", "i", "^+", "IH" }, { "", "i", "t%", "AY" }, { "#:^", "i", "^+", "IH" }, { "", "i", "^+", "AY" }, { "", "ir", "", "ER" },
    { "", "igh", "", "AY" }, { "", "ild", "", "AY L D" }, { "", "ign", " ", "AY N" }, { "", "ign", "^", "AY N" },
    { "", "ign", "%", "AY N" }, { "", "ique", "", "IY K" }, { "", "i", "", "IH" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesJ[] = { { "", "j", "", "JH" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesK[] = { { " ", "k", "n", "" }, { "", "k", "", "K" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesL[] = {
    { "", "lo", "c#", "L OW" }, { "l", "l", "", "" }, { "#:^", "l", "%", "AX L" }, { "", "lead", "", "L IY D" },
    { "", "l", "", "L" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesM[] = { { "", "mov", "", "M UW V" }, { "", "mm", "", "M" }, { "", "m", "", "M" },
                        { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesN[] = {
    { "e", "ng", "+", "N JH" }, { "", "ng", "r", "NG G" }, { "", "ng", "#", "NG G" }, { "", "ngl", "%", "NG G AX L" },
    { "", "ng", "", "NG" }, { "", "nk", "", "NG K" }, { " ", "now", " ", "N AW" }, { "", "nn", "", "N" },
    { "", "n", "", "N" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesO[] = {
    { "", "of", " ", "AX V" }, { "", "orough", "", "ER OW" }, { "#:", "or", " ", "ER" }, { "#:", "ors", " ", "ER Z" },
    { "", "or", "", "AO R" }, { " ", "one", "", "W AH N" }, { "", "ow", "", "OW" }, { " ", "over", "", "OW V ER" },
    { "", "ov", "", "AH V" }, { "", "o", "^%", "OW" }, { "", "o", "^en", "OW" }, { "", "o", "^i#", "OW" },
    { "", "ol", "d", "OW L" }, { "", "ought", "", "AO T" }, { "", "ough", "", "AH F" }, { " ", "ou", "", "AW" },
    { "h", "ou", "s#", "AW" }, { "", "ous", "", "AX S" }, { "", "our", "", "AO R" }, { "", "ould", "", "UH D" },
    { "^", "ou", "^l", "AH" }, { "", "oup", "", "UW P" }, { "", "ou", "", "AW" }, { "", "oy", "", "OY" },
    { "", "oing", "", "OW IH NG" }, { "", "oi", "", "OY" }, { "", "oor", "", "AO R" }, { "", "ook", "", "UH K" },
    { "", "ood", "", "UH D" }, { "", "oo", "", "UW" }, { "", "o", "e", "OW" }, { "", "o", " ", "OW" },
    { "", "oa", "", "OW" }, { " ", "only", "", "OW N L IY" }, { " ", "once", "", "W AH N S" }, { "", "on't", "", "OW N T" },
    { "c", "o", "n", "AA" }, { "", "o", "ng", "AO" }, { " :^", "o", "n", "AH" }, { "i", "on", "", "AX N" },
    { "#:", "on", " ", "AX N" }, { "#^", "on", "", "AX N" }, { "", "o", "st ", "OW" }, { "", "of", "^", "AO F" },
    { "", "other", "", "AH DH ER" }, { "", "oss", " ", "AO S" }, { "#:^", "om", "", "AH M" }, { "", "o", "", "AA" },
    { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesP[] = {
    { "", "ph", "", "F" }, { "", "peop", "", "P IY P" }, { "", "pow", "", "P AW" }, { "", "put", " ", "P UH T" },
    { "", "pp", "", "P" }, { "", "p", "", "P" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesQ[] = { { "", "quar", "", "K W AO R" }, { "", "qu", "", "K W" }, { "", "q", "", "K" },
                        { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesR[] = { { " ", "re", "^#", "R IY" }, { "", "rr", "", "R" }, { "", "r", "", "R" },
                        { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesS[] = {
    { "", "sh", "", "SH" }, { "#", "sion", "", "ZH AX N" }, { "", "some", "", "S AH M" }, { "#", "sur", "#", "ZH ER" },
    { "", "sur", "#", "SH ER" }, { "#", "su", "#", "ZH UW" }, { "#", "ssu", "#", "SH UW" }, { "#", "sed", " ", "Z D" },
    { "#", "s", "#", "Z" }, { "", "said", "", "S EH D" }, { "^", "sion", "", "SH AX N" }, { "", "s", "s", "" },
    { ".", "s", " ", "Z" }, { "#:.e", "s", " ", "Z" }, { "#:^##", "s", " ", "Z" }, { "#:^#", "s", " ", "S" },
    { "u", "s", " ", "S" }, { " :#", "s", " ", "Z" }, { " ", "sch", "", "S K" }, { "", "s", "c+", "" },
    { "#", "sm", "", "Z AX M" }, { "#", "sn", "'", "Z AX N" }, { "", "s", "", "S" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesT[] = {
    { " ", "the", " ", "DH AX" }, { "", "to", " ", "T UW" }, { "", "that", " ", "DH AE T" }, { " ", "this", " ", "DH IH S" },
    { " ", "they", "", "DH EY" }, { " ", "there", "", "DH EH R" }, { "", "ther", "", "DH ER" }, { "", "their", "", "DH EH R" },
    { " ", "than", " ", "DH AE N" }, { " ", "them", " ", "DH EH M" }, { "", "these", " ", "DH IY Z" },
    { " ", "then", "", "DH EH N" }, { "", "through", "", "TH R UW" }, { "", "those", "", "DH OW Z" },
    { "", "though", " ", "DH OW" }, { " ", "thus", "", "DH AH S" }, { "", "th", "", "TH" }, { "#:", "ted", " ", "T IH D" },
    { "s", "ti", "#n", "CH" }, { "", "ti", "o", "SH" }, { "", "ti", "a", "SH" }, { "", "tien", "", "SH AX N" },
    { "", "tur", "#", "CH ER" }, { "", "tu", "a", "CH UW" }, { " ", "two", "", "T UW" }, { "", "tt", "", "T" },
    { "", "t", "", "T" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesU[] = {
    { " ", "un", "i", "Y UW N" }, { " ", "un", "", "AH N" }, { " ", "upon", "", "AX P AO N" }, { "@", "ur", "#", "UH R" },
    { "", "ur", "#", "Y UH R" }, { "", "ur", "", "ER" }, { "", "u", "^ ", "AH" }, { "", "u", "^^", "AH" },
    { "", "uy", "", "AY" }, { " g", "u", "#", "" }, { "g", "u", "%", "" }, { "g", "u", "#", "W" },
    { "#n", "u", "", "Y UW" }, { "@", "u", "", "UW" }, { "", "u", "", "Y UW" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesV[] = { { "", "view", "", "V Y UW" }, { "", "v", "", "V" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesW[] = {
    { " ", "were", "", "W ER" }, { "", "wa", "s", "W AA" }, { "", "wa", "t", "W AA" }, { "", "where", "", "W EH R" },
    { "", "what", "", "W AA T" }, { "", "whol", "", "HH OW L" }, { "", "who", "", "HH UW" }, { "", "wh", "", "W" },
    { "", "war", "", "W AO R" }, { "", "wor", "^", "W ER" }, { "", "wr", "", "R" }, { "", "w", "", "W" },
    { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesX[] = { { " ", "x", "", "Z" }, { "", "x", "", "K S" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesY[] = {
    { "", "young", "", "Y AH NG" }, { " ", "you", "", "Y UW" }, { " ", "yes", "", "Y EH S" }, { " ", "y", "", "Y" },
    { "#:^", "y", " ", "IY" }, { "#:^", "y", "i", "IY" }, { " :", "y", " ", "AY" }, { " :", "y", "#", "AY" },
    { " :", "y", "^+:#", "IH" }, { " :", "y", "^#", "AY" }, { "", "y", "", "IH" }, { nullptr, nullptr, nullptr, nullptr } };
const Rule rulesZ[] = { { "", "zz", "", "Z" }, { "", "z", "", "Z" }, { nullptr, nullptr, nullptr, nullptr } };

const Rule* const ruleTable[26] = { rulesA, rulesB, rulesC, rulesD, rulesE, rulesF, rulesG, rulesH, rulesI,
                                    rulesJ, rulesK, rulesL, rulesM, rulesN, rulesO, rulesP, rulesQ, rulesR,
                                    rulesS, rulesT, rulesU, rulesV, rulesW, rulesX, rulesY, rulesZ };

bool isVowel (char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; }
bool isLetter (char c) { return c >= 'a' && c <= 'z'; }
bool isConsonant (char c) { return isLetter (c) && ! isVowel (c); }
bool isVoiced (char c) { return std::strchr ("bdgjlmnrvwz", c) != nullptr && c != 0; }
bool isFront (char c) { return c == 'e' || c == 'i' || c == 'y'; }

// Matches the right context `ctx` at word[pos...]; word is padded with spaces on both sides.
bool matchRight (const std::string& w, std::size_t pos, const char* ctx)
{
    for (; *ctx != 0; ++ctx)
    {
        const char c = *ctx;
        const char at = pos < w.size() ? w[pos] : ' ';
        switch (c)
        {
            case '#':
                if (! isVowel (at)) return false;
                while (pos < w.size() && isVowel (w[pos])) ++pos;
                break;
            case ':':
                while (pos < w.size() && isConsonant (w[pos])) ++pos;
                break;
            case '^': if (! isConsonant (at)) return false; ++pos; break;
            case '.': if (! isVoiced (at)) return false; ++pos; break;
            case '+': if (! isFront (at)) return false; ++pos; break;
            case '&':
                if (pos + 1 < w.size() && (w.compare (pos, 2, "ch") == 0 || w.compare (pos, 2, "sh") == 0)) { pos += 2; break; }
                if (std::strchr ("scgzxj", at) == nullptr || at == 0) return false;
                ++pos;
                break;
            case '@':
                if (pos + 1 < w.size() && (w.compare (pos, 2, "th") == 0 || w.compare (pos, 2, "ch") == 0 || w.compare (pos, 2, "sh") == 0)) { pos += 2; break; }
                if (std::strchr ("tsrdlznj", at) == nullptr || at == 0) return false;
                ++pos;
                break;
            case '%':
            {
                // er, e, es, ed, ing, ely (then the end of the word)
                static const char* const suffixes[] = { "ing", "ely", "er", "es", "ed", "e" };
                bool ok = false;
                for (const auto* s : suffixes)
                {
                    const auto n = std::strlen (s);
                    if (w.compare (pos, n, s) == 0 && (pos + n >= w.size() || ! isLetter (w[pos + n])))
                    {
                        pos += n;
                        ok = true;
                        break;
                    }
                }
                if (! ok) return false;
                break;
            }
            case ' ': if (isLetter (at) || at == '\'') return false; ++pos; break;
            default: if (at != c) return false; ++pos; break;
        }
    }
    return true;
}

// Matches the left context `ctx` ending just before word[pos] (read right to left).
bool matchLeft (const std::string& w, std::ptrdiff_t pos, const char* ctx)
{
    std::ptrdiff_t p = pos - 1;
    for (auto i = (std::ptrdiff_t) std::strlen (ctx) - 1; i >= 0; --i)
    {
        const char c = ctx[i];
        const char at = p >= 0 ? w[(std::size_t) p] : ' ';
        switch (c)
        {
            case '#':
                if (! isVowel (at)) return false;
                while (p >= 0 && isVowel (w[(std::size_t) p])) --p;
                break;
            case ':':
                while (p >= 0 && isConsonant (w[(std::size_t) p])) --p;
                break;
            case '^': if (! isConsonant (at)) return false; --p; break;
            case '.': if (! isVoiced (at)) return false; --p; break;
            case '+': if (! isFront (at)) return false; --p; break;
            case '&':
                if (p >= 1 && (w.compare ((std::size_t) p - 1, 2, "ch") == 0 || w.compare ((std::size_t) p - 1, 2, "sh") == 0)) { p -= 2; break; }
                if (std::strchr ("scgzxj", at) == nullptr || at == 0) return false;
                --p;
                break;
            case '@':
                if (p >= 1 && (w.compare ((std::size_t) p - 1, 2, "th") == 0 || w.compare ((std::size_t) p - 1, 2, "ch") == 0 || w.compare ((std::size_t) p - 1, 2, "sh") == 0)) { p -= 2; break; }
                if (std::strchr ("tsrdlznj", at) == nullptr || at == 0) return false;
                --p;
                break;
            case ' ': if (isLetter (at) || at == '\'') return false; --p; break;
            default: if (at != c) return false; --p; break;
        }
    }
    return true;
}

bool isVowelPhoneme (const std::string& p)
{
    static const char* const vowels[] = { "IY", "IH", "EY", "EH", "AE", "AA", "AO", "OW", "UH", "UW", "AH", "AX", "ER",
                                          "AY", "AW", "OY" };
    for (const auto* v : vowels)
        if (p == v)
            return true;
    return false;
}

// Puts the stress mark on the first full vowel (not AX), or the first vowel at all.
std::string addStress (const std::string& phonemes)
{
    if (phonemes.find ('1') != std::string::npos)
        return phonemes;
    std::vector<std::string> list;
    std::size_t start = 0;
    while (start < phonemes.size())
    {
        auto end = phonemes.find (' ', start);
        if (end == std::string::npos) end = phonemes.size();
        if (end > start) list.push_back (phonemes.substr (start, end - start));
        start = end + 1;
    }
    int pick = -1;
    for (int i = 0; i < (int) list.size() && pick < 0; ++i)
        if (isVowelPhoneme (list[(size_t) i]) && list[(size_t) i] != "AX")
            pick = i;
    for (int i = 0; i < (int) list.size() && pick < 0; ++i)
        if (isVowelPhoneme (list[(size_t) i]))
            pick = i;
    std::string out;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! out.empty()) out += ' ';
        out += list[(size_t) i];
        if (i == pick) out += '1';
    }
    return out;
}

// One word (lower-case letters and "'") to phonemes.
std::string wordToPhonemes (const std::string& word)
{
    for (const auto& e : dictionary)
        if (word == e.word)
            return e.phonemes;

    const std::string w = " " + word + " ";
    std::string out;
    std::size_t pos = 1;
    while (pos + 1 < w.size())
    {
        const char c = w[pos];
        if (! isLetter (c))
        {
            ++pos;
            continue;
        }
        bool matched = false;
        for (const auto* r = ruleTable[c - 'a']; r->match != nullptr; ++r)
        {
            const auto n = std::strlen (r->match);
            if (w.compare (pos, n, r->match) != 0)
                continue;
            if (! matchLeft (w, (std::ptrdiff_t) pos, r->left) || ! matchRight (w, pos + n, r->right))
                continue;
            if (*r->out != 0)
            {
                if (! out.empty()) out += ' ';
                out += r->out;
            }
            pos += n;
            matched = true;
            break;
        }
        if (! matched)
            ++pos;
    }
    // function words stay unstressed
    static const char* const unstressed[] = { "a", "the", "of", "to", "an", "and" };
    for (const auto* u : unstressed)
        if (word == u)
            return out;
    return addStress (out);
}

// A single letter said as its name ("b" -> "B IY1"), for spelled-out letters.
const char* letterName (char c)
{
    static const char* const names[26] = {
        "EY1", "B IY1", "S IY1", "D IY1", "IY1", "EH1 F", "JH IY1", "EY1 CH", "AY1", "JH EY1", "K EY1", "EH1 L", "EH1 M",
        "EH1 N", "OW1", "P IY1", "K Y UW1", "AA1 R", "EH1 S", "T IY1", "Y UW1", "V IY1", "D AH1 B AX L Y UW", "EH1 K S",
        "W AY1", "Z IY1" };
    return names[c - 'a'];
}
} // namespace

std::string spellNumber (std::int64_t value)
{
    if (value < 0)
        return "minus " + spellNumber (-value);
    if (value == 0)
        return "zero";
    std::string out;
    static const char* const scales[] = { "", "thousand", "million", "billion" };
    std::vector<int> groups;
    for (auto v = value; v > 0 && groups.size() < 4; v /= 1000)
        groups.push_back ((int) (v % 1000));
    for (int g = (int) groups.size() - 1; g >= 0; --g)
    {
        if (groups[(size_t) g] == 0)
            continue;
        std::string part;
        spellBelowThousand (groups[(size_t) g], part);
        if (! out.empty()) out += ' ';
        out += part;
        if (g > 0)
        {
            out += ' ';
            out += scales[g];
        }
    }
    return out;
}

std::string toPhonemes (std::string_view text)
{
    // lower case; digits to words; anything else that is not a letter or "'" separates words; ",.;:!?" pause
    std::string expanded;
    for (std::size_t i = 0; i < text.size();)
    {
        const auto c = (unsigned char) text[i];
        if (std::isdigit (c))
        {
            std::int64_t v = 0;
            std::size_t j = i;
            while (j < text.size() && std::isdigit ((unsigned char) text[j]) && j - i < 12)
                v = v * 10 + (text[j++] - '0');
            expanded += ' ' + spellNumber (v) + ' ';
            i = j;
            continue;
        }
        if (std::isalpha (c) || c == '\'')
            expanded += (char) std::tolower (c);
        else if (c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?')
            expanded += " _ ";
        else if (c == '-')
            expanded += ' ';
        else if (c == '=' )
            expanded += " equals ";
        else if (c == '&')
            expanded += " and ";
        else
            expanded += ' ';
        ++i;
    }

    std::string out;
    std::size_t start = 0;
    while (start < expanded.size())
    {
        while (start < expanded.size() && expanded[start] == ' ') ++start;
        auto end = expanded.find (' ', start);
        if (end == std::string::npos) end = expanded.size();
        if (end > start)
        {
            auto word = expanded.substr (start, end - start);
            // strip quotes around a word
            while (! word.empty() && word.front() == '\'') word.erase (0, 1);
            while (! word.empty() && word.back() == '\'') word.pop_back();
            std::string ph;
            if (word == "_")
                ph = "_";
            else if (word.size() == 1 && word != "a" && word != "i")
                ph = letterName (word[0]);
            else if (! word.empty())
                ph = wordToPhonemes (word);
            if (! ph.empty())
            {
                if (! out.empty()) out += ' ';
                out += ph;
            }
        }
        start = end + 1;
    }
    return out;
}
} // namespace lynx::speech
