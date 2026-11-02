// Round 168b: FL's speech synthesizer (.speech files, the "Speech properties" form) and LYNX's own voice.
#include "core/SampleFiles.h"
#include "core/Speech.h"

TEST (speech168b_file_defaults_and_roundtrip)
{
    using namespace lynx::speech;
    Settings s;
    // FL's factory "Default.speech" (Data/Patches/Speech): Style 2, Personality 9, Pitch 92, Rate 150
    CHECK (parse ("[Speaker Settings]\r\nStyle=2\r\nPersonality=9\r\nPitch=92\r\nRate=150\r\nVoicingMode=0\r\nText=hello world\r\n", s));
    CHECK (s.style == Style::monotone && s.personality == 9 && s.pitch == 92 && s.rate == 150 && s.voicing == Voicing::normal);
    CHECK (s.text == "hello world");
    // missing keys keep FL's defaults (Colossus, 65 Hz, 138 WPM, Normal, style code 2)
    CHECK (parse ("[Speaker Settings]\nText=hi\n", s));
    CHECK (s.personality == 11 && s.pitch == 65 && s.rate == 138 && s.voicing == Voicing::normal && s.style == Style::monotone);
    // an empty text is refused, as FL refuses it; other sections and junk ignored
    CHECK (! parse ("[Speaker Settings]\nPitch=100\n", s));
    CHECK (! parse ("[Other]\nText=hello\n", s));
    CHECK (parse ("; comment\n[Other]\nText=no\n[speaker settings]\nPitch=abc\nText=yes\n", s));
    CHECK (s.text == "yes" && s.pitch == 65);
    // write() then parse() gives the same settings, in FL's key order and CRLF
    Settings w;
    w.personality = 3; w.pitch = 220; w.rate = 300; w.voicing = Voicing::whispered; w.style = Style::sing; w.text = "one_two three(5)";
    const auto text = write (w);
    CHECK (text == "[Speaker Settings]\r\nStyle=3\r\nPersonality=3\r\nPitch=220\r\nRate=300\r\nVoicingMode=2\r\nText=one_two three(5)\r\n");
    Settings back;
    CHECK (parse (text, back) && back == w);
}

TEST (speech168b_style_codes_and_names)
{
    using namespace lynx::speech;
    CHECK (styleToCode (Style::natural) == 0 && styleToCode (Style::monotone) == 2 && styleToCode (Style::sing) == 3 && styleToCode (Style::random) == 4);
    for (int i = 0; i < styleCount; ++i)
        CHECK (styleFromCode (styleToCode ((Style) i)) == (Style) i);
    CHECK (styleFromCode (1) == Style::natural); // the form: index = code - (code != 0)
    CHECK (styleFromCode (99) == Style::random);
    CHECK (std::string (personalityName (0)) == "Male" && std::string (personalityName (11)) == "Colossus" && std::string (personalityName (19)) == "Choirboy");
    CHECK (std::string (personalityName (20)).empty() && std::string (personalityName (-1)).empty());
    for (int i = 0; i < personalityCount; ++i)
        CHECK (std::string (personalityHint (i)).size() > 2);
    CHECK (std::string (voicingName (Voicing::whispered)) == "Whispered");
}

TEST (speech168b_keyboard_pitch)
{
    using namespace lynx::speech;
    CHECK (keyToStoredPitch (57) == 440);
    CHECK (keyToStoredPitch (45) == 220);
    CHECK (keyToStoredPitch (minKey) == 33 && keyToStoredPitch (maxKey) == 988);
    CHECK (hzToKey (65.0) == 24);  // FL's default 65 Hz is C2 on the form's keyboard
    CHECK (hzToKey (92.0) == 30);
    for (int k = minKey; k <= maxKey; ++k)
        CHECK (hzToKey (keyToStoredPitch (k)) == k);
}

TEST (speech168b_words_and_word_pitch)
{
    using namespace lynx::speech;
    const auto w = splitWords ("hello  big_world up(3) (7)x down(-12) bad(z)");
    CHECK (w.size() == 6);
    CHECK (w[0].text == "hello" && w[0].semitones == 0);
    CHECK (w[1].text == "big world");                  // "_" joins words into one slice
    CHECK (w[2].text == "up" && w[2].semitones == 3);
    CHECK (w[3].text == "(7)x" && w[3].semitones == 0); // starting with "(": spoken as written
    CHECK (w[4].text == "down" && w[4].semitones == -12);
    CHECK (w[5].text == "bad" && w[5].semitones == 0);  // StrToIntDef (.., 0)
    CHECK (splitWords ("   ").empty());
    // the voice pitch snapped to a semitone, moved, clamped to 10..988 Hz
    CHECK (wordPitchHz (440, 0) == 440);
    CHECK (wordPitchHz (440, 12) == 880);
    CHECK (wordPitchHz (445, 0) == 440);
    CHECK (wordPitchHz (440, 48) == 988);
    CHECK (wordPitchHz (65, -60) == 10);
}

TEST (speech168b_suggested_file_name)
{
    using namespace lynx::speech;
    Settings s;
    s.text = "hello big_world, it's 2 me!";
    s.personality = 9;
    CHECK (suggestedFileName (s) == "HelloBigWorldIts2Me_Robotoid.speech");
    s.text = "abcdefghij abcdefghij abcdefghij abcdefghij";
    s.personality = 0;
    CHECK (suggestedFileName (s) == "AbcdefghijAbcdefghijAbcdefghijAb_Male.speech");
}

TEST (speech168b_render_words_as_slices)
{
    using namespace lynx::speech;
    Settings s;
    s.text = "one two_three four(5)";
    s.pitch = 110;
    const auto r = render (s, 44100.0);
    CHECK (r.samples.size() > 44100 / 2);
    CHECK (r.wordStarts.size() == 3 && r.wordNames.size() == 3);
    CHECK (r.wordStarts[0] == 0 && r.wordStarts[1] > r.wordStarts[0] && r.wordStarts[2] > r.wordStarts[1]);
    CHECK (r.wordNames[1] == "two three");
    float peak = 0.0f;
    for (const auto x : r.samples)
        peak = std::max (peak, std::abs (x));
    CHECK (peak > 0.8f && peak < 0.95f);
    // deterministic, every style and personality speaks
    CHECK (render (s, 44100.0).samples == r.samples);
    for (int p = 0; p < personalityCount; ++p)
    {
        Settings v = personalityDefaults (p, s);
        CHECK (v.text == s.text && v.pitch >= 30 && v.pitch <= 990 && v.rate >= minRate && v.rate <= maxRate);
        v.rate = 400;
        CHECK (! render (v, 22050.0).samples.empty());
    }
    CHECK (toPhonemes ("cat").find ("AE") != std::string::npos);
    CHECK (spellNumber (1999) == "one thousand nine hundred ninety nine");
    CHECK (spellNumber (10) == "ten");
    CHECK (lynx::isSampleExtension (".SPEECH"));
}

#include "core/BrowserView.h"

TEST (speech168b_browser_lists_speech)
{
    using namespace lynx::browserview;
    CHECK (listed ("Default.speech", ContentFilter::all));
    CHECK (listed ("Default.SPEECH", ContentFilter::audio));
    CHECK (listed ("Default.speech", ContentFilter::samples));
    CHECK (! listed ("Default.speech", ContentFilter::projects));
}
