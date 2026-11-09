// --- Round 179a: the interface font saved in Settings: blank or missing falls back to the default without a fuss --

#include "core/UiFontChoice.h"

TEST (uifont179a_saved_font_is_used_when_installed)
{
    const auto installed = [] (const std::string& name) { return name == "Noto Sans"; };
    CHECK (lynx::chooseUiFont ("Noto Sans", installed) == "Noto Sans");
    CHECK (lynx::chooseUiFont (" Noto Sans\r\n", installed) == "Noto Sans"); // (a file edited by hand)
}

TEST (uifont179a_missing_or_blank_font_means_the_default)
{
    const auto installed = [] (const std::string& name) { return name == "Noto Sans"; };
    CHECK (lynx::chooseUiFont ("Gone Font", installed).empty());
    CHECK (lynx::chooseUiFont ("", installed).empty());
    CHECK (lynx::chooseUiFont (" \n", installed).empty());
}
