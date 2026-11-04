// Round 169k: FL's View › Align all channel editors (AlignAllChannelEditorsAction, ShortCut 8315 = Shift+F12;
// docs/analise/REA_rodada169k.md §2.4).

TEST (gap169k_align_channel_editors_key)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ f12, shift }, Context::global, false) == "view.alignChannelEditors");
    CHECK (map.find ({ f12, ctrl }, Context::global, false) == "view.closeUnfocusedWindows");
    CHECK (map.find ({ f12 }, Context::global, false) == "view.closeWindows");
}
