#pragma once

// Menu accelerator letters (round 170a, Busy Works Beats' video: "right click and hit D for Delete"). FL's menus carry
// Windows '&' mnemonics in their captions (REA round 170a §4: TFruityLoopsMainForm's channel menu "&Delete...",
// "Cu&t", "C&opy", "&Paste", "&Clone", ...; the Piano roll / Playlist Edit actions "Cu&t", "&Copy", "&Paste",
// "&Delete", "Delete &all"): with the menu open, that key picks the item.
//
// LYNX keeps the letter out of band of the translatable text: menu code wraps the translated caption with
// mnemonic (text, 'D'), which puts the mark character U+0001 before the first matching letter (a word's first
// letter preferred). A translation without that letter gets it appended in brackets ("Excluir... (D)", as Windows
// does in East Asian menus), so the key is FL's in every language. The LookAndFeel draws the marked letter
// underlined and hides the mark; the patched juce::PopupMenu (cmake/LynxPatchJuce.cmake fix 12) picks the item when
// the key is typed. Pure (no JUCE), UTF-8.

#include <cctype>
#include <string>

namespace lynx::menumnemonic
{
inline constexpr char mark = '\x01';

// `text` with the mark before the letter `key` (ASCII letter or digit). Text after a tab (a shortcut column) is left
// alone. Text that already has a mark is returned unchanged.
inline std::string apply (const std::string& text, char key)
{
    if (text.find (mark) != std::string::npos || ! std::isalnum ((unsigned char) key))
        return text;

    const auto tab = text.find ('\t');
    const auto label = text.substr (0, tab);
    const auto rest = tab == std::string::npos ? std::string() : text.substr (tab);
    const auto lower = (char) std::tolower ((unsigned char) key);
    auto matches = [lower] (char c) { return (char) std::tolower ((unsigned char) c) == lower; };

    auto at = std::string::npos;
    for (std::size_t i = 0; i < label.size(); ++i) // a word's first letter first
        if (matches (label[i]) && (i == 0 || label[i - 1] == ' ' || label[i - 1] == '(' || label[i - 1] == '-' || label[i - 1] == '/'))
        {
            at = i;
            break;
        }
    if (at == std::string::npos)
        for (std::size_t i = 0; i < label.size(); ++i)
            if (matches (label[i]))
            {
                at = i;
                break;
            }

    if (at != std::string::npos)
        return label.substr (0, at) + mark + label.substr (at) + rest;

    // not in the translation: "Text (K)", the mark on the key
    std::string shown (1, (char) std::toupper ((unsigned char) key));
    return label + " (" + mark + shown + ")" + rest;
}

// `text` without the mark; `index` = byte index (in the result) of the marked character, or -1.
inline std::string strip (const std::string& text, int* index = nullptr)
{
    const auto at = text.find (mark);
    if (index != nullptr)
        *index = at == std::string::npos || at + 1 >= text.size() ? -1 : (int) at;
    if (at == std::string::npos)
        return text;
    std::string out;
    out.reserve (text.size());
    for (auto c : text)
        if (c != mark)
            out += c;
    return out;
}

// The key of a marked text (lower case), or 0.
inline char keyOf (const std::string& text)
{
    const auto at = text.find (mark);
    return at == std::string::npos || at + 1 >= text.size() ? 0 : (char) std::tolower ((unsigned char) text[at + 1]);
}
} // namespace lynx::menumnemonic
