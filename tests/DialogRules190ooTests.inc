// Round 190oo (design review 2): LYNX's one dialog system without JUCE (core/DialogRules.h). WG-6: the button order of
// every dialog on ui::dialog::Alert; WG-22: the accent strip and the divider on whole physical pixels.

#include "core/DialogRules.h"

TEST (dialog190oo_ok_cancel_put_the_primary_at_the_far_right)
{
    using lynx::dialogrules::buttonOrder;
    // JUCE's OK / Cancel box: OK (0, Return) and Cancel (1, Escape) -> Cancel | OK
    CHECK ((buttonOrder (2, 0, 1, false) == std::vector<int> { 1, 0 }));
    // the sample Pitch window: OK (Return), Reset, Cancel (Escape) -> Reset | Cancel | OK
    CHECK ((buttonOrder (3, 0, 2, false) == std::vector<int> { 1, 2, 0 }));
    // without a primary: as added, Cancel last
    CHECK ((buttonOrder (3, -1, 2, false) == std::vector<int> { 0, 1, 2 }));
    // the plugin crash prompt: Reload (the first, the default), Always run separately, Close (Escape)
    // -> Always run separately | Close | Reload
    CHECK ((buttonOrder (3, 0, 2, false) == std::vector<int> { 1, 2, 0 }));
    // a notice: one OK that is both Return and Escape (the primary)
    CHECK ((buttonOrder (1, 0, -1, false) == std::vector<int> { 0 }));
    // the record question: Cancel alone (Escape)
    CHECK ((buttonOrder (1, -1, 0, false) == std::vector<int> { 0 }));
    CHECK (buttonOrder (0, -1, -1, false).empty());
}

TEST (dialog190oo_one_accent_button_the_return_default)
{
    using lynx::dialogrules::primaryButton;
    CHECK (primaryButton (2, 0, 1) == 0);   // OK / Cancel: OK (Return)
    CHECK (primaryButton (3, 0, 2) == 0);   // Pitch: OK (Return), Reset, Cancel
    CHECK (primaryButton (1, 0, -1) == 0);  // a notice's OK (Return and Escape)
    CHECK (primaryButton (3, -1, 2) == 0);  // JUCE's three-button box, the crash prompt: the first takes the focus and Return
    CHECK (primaryButton (1, -1, 0) == -1); // the record question's lone Cancel stays secondary
    CHECK (primaryButton (2, -1, 0) == -1); // a first button that is the Escape one is no default
    CHECK (primaryButton (0, -1, -1) == -1);
}

TEST (dialog190oo_fl_messages_keep_their_order)
{
    using lynx::dialogrules::buttonOrder;
    // FL's Yes | No (Yes = Return, No = Escape) stays as FL shows it
    CHECK ((buttonOrder (2, 0, 1, true) == std::vector<int> { 0, 1 }));
    CHECK ((buttonOrder (3, 0, 2, true) == std::vector<int> { 0, 1, 2 }));
}

TEST (dialog190oo_bands_land_on_whole_device_rows)
{
    using lynx::dialogrules::bandRows;
    CHECK (bandRows (3.0f, 1.5f) == 5.0f); // the strip @150 (was 4.5 rows: 4 full, one at 50 %)
    CHECK (bandRows (1.0f, 1.5f) == 1.0f); // the divider @150 (was 1.5 rows)
    CHECK (bandRows (3.0f, 1.25f) == 4.0f);
    CHECK (bandRows (1.0f, 1.25f) == 1.0f);
    CHECK (bandRows (3.0f, 2.0f) == 6.0f); // whole scales as before
    CHECK (bandRows (1.0f, 2.0f) == 2.0f);
    CHECK (bandRows (1.0f, 2.5f) == 2.0f);
    CHECK (bandRows (1.0f, 0.75f) == 1.0f); // never under one row
}
