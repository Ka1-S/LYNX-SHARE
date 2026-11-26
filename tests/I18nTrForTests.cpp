// Round 190ru (hotfix 0.1.31.3): ui/I18n.cpp's tr() functions for the small test programs that build engine/ProjectFile.cpp
// alone (lynx_remote_project_tests, lynx_crossclipboard_tests). Since the checked writer, ProjectFile::save says why a save
// failed through tr() ("Not saved: {0}. Your project file was not changed."); I18n.cpp itself needs PluginHost. Same bodies
// as in ui/I18n.cpp (juce::translate, juce_core).
#include "ui/I18n.h"

namespace lynx
{
juce::String tr (const juce::String& english) { return juce::translate (english); }

juce::String tr (const char* englishUtf8) { return juce::translate (juce::String::fromUTF8 (englishUtf8)); }

juce::String tr (const char* englishUtf8, std::initializer_list<juce::String> args)
{
    auto text = tr (englishUtf8);
    int i = 0;
    for (const auto& a : args)
        text = text.replace ("{" + juce::String (i++) + "}", a);
    return text;
}
} // namespace lynx
