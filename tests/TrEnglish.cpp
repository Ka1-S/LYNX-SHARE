// rollup 0.1.32 step 5: ProjectFile.cpp's save message (round 190rm's checked writer) calls tr(). The small test executables
// that build ProjectFile.cpp without the UI (no translation tables, no plugin host) take the English text, with the {0} {1} ...
// arguments filled in as src/ui/I18n.cpp does.
#include "ui/I18n.h"

namespace lynx
{
juce::String tr (const juce::String& english) { return english; }
juce::String tr (const char* englishUtf8) { return juce::String::fromUTF8 (englishUtf8); }
juce::String tr (const char* englishUtf8, std::initializer_list<juce::String> args)
{
    auto text = tr (englishUtf8);
    int i = 0;
    for (const auto& a : args)
        text = text.replace ("{" + juce::String (i++) + "}", a);
    return text;
}
} // namespace lynx
