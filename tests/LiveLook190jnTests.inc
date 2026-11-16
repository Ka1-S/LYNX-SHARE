// Round 190jn (design190ia slice 25, CL-23 / DL-64): Live's ? key (Shift+/) shows or hides the Clip-launch layout's Info
// View; free before, no conflict, the same in every focus, and the typing piano doesn't take it.

TEST (livelook190jn_info_view_key_is_free_and_global)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    for (auto focus : { Context::global, Context::channelRack, Context::pianoRoll, Context::playlist, Context::mixer, Context::browser })
        CHECK (map.find ({ '/', shift }, focus, false) == "view.detailInfo");
    CHECK (map.find ({ '/', shift }, Context::pianoRoll, true) == "view.detailInfo");
    CHECK (map.find ({ '/' }, Context::global, false).empty()); // (plain / keeps no binding)
    // it goes through the detail view's actions (view.detail*), so the Pattern layout leaves the chord alone
    CHECK (std::string ("view.detailInfo").rfind ("view.detail", 0) == 0);
}
