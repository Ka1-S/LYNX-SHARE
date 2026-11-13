// Round 190fc: the 0.1.27 release candidate's blocker and the two HIGH bugs of its interactive QA (round 190fb).
// (B1) Every close in the Simple layout crashed (access violation in PanelFrame::setAccessory): MainComponent's
//      `simpleWorkflow` member is declared before `workspace`, so it was destroyed after it, and ~SimpleWorkflow leaves
//      the style through its Host (setTitleAccessory -> the destroyed Workspace's frames). The destructor body now
//      destroys it while everything exists. The app route --simpleclosetest closes the main window with everything of
//      the Simple layout open (run under ASan on Linux and on Windows).
// (H1) The first-song tour's card and ring were children of the main window, under the panels' native child windows:
//      they are floaters now (ui/NativeChild.h), the ring passing every click and drop through.
// (H2) Space did nothing after clicks in the Simple window (the keys went to the dock's search box, a button, nobody):
//      MainComponent::simpleClickEnded gives them back to the main window (text fields excepted), and an overlay that
//      had the keys returns them when it goes. The app route --simplekeytest clicks and presses Space as the system does.
// These are the source guards; the behaviour is checked by the two app routes.

namespace rcfix190fc
{
inline std::size_t after (const std::string& text, const std::string& what, std::size_t from = 0)
{
    const auto at = text.find (what, from);
    return at == std::string::npos ? std::string::npos : at + what.size();
}
} // namespace rcfix190fc

TEST (rcfix190fc_simple_workflow_goes_before_the_workspace)
{
    using namespace i18ntest;
    const auto header = readFile ("src/ui/MainComponent.h");
    const auto member = header.find ("std::unique_ptr<SimpleWorkflow> simpleWorkflow;");
    const auto workspace = header.find ("Workspace workspace;");
    CHECK (member != std::string::npos && workspace != std::string::npos);
    const auto source = readFile ("src/ui/MainComponent.cpp");
    const auto dtor = source.find ("MainComponent::~MainComponent()");
    const auto end = source.find ("\nvoid MainComponent::paint", dtor);
    const auto reset = source.find ("simpleWorkflow.reset();", dtor);
    CHECK (dtor != std::string::npos && end != std::string::npos);
    // declared before the Workspace (so a plain member would outlive it): the destructor body must destroy it
    if (member < workspace)
        CHECK (reset != std::string::npos && reset < end);
    CHECK (source.find ("simpleWorkflowAliveAfterTeardown = SimpleWorkflow::alive.load();", dtor) < end);
    // ~SimpleWorkflow still leaves the style through the Host: only safe while the owner's members exist
    const auto simple = readFile ("src/ui/SimpleWorkflow.cpp");
    CHECK (simple.find ("alive.fetch_sub (1, std::memory_order_relaxed);") != std::string::npos);
    // the route exists and is wired
    const auto main = readFile ("src/Main.cpp");
    CHECK (main.find ("args.contains (\"--simpleclosetest\")") != std::string::npos);
    CHECK (main.find ("bool runSimpleCloseTest (const juce::String& reportPath)") != std::string::npos);
}

TEST (rcfix190fc_tour_card_floats_above_the_native_panels)
{
    using namespace i18ntest;
    const auto tour = readFile ("src/ui/FirstSongTour.cpp");
    const auto show = tour.find ("void FirstSongTour::showCard()");
    CHECK (show != std::string::npos);
    CHECK (tour.find ("nativechild::addFloater (c);", show) != std::string::npos);
    CHECK (tour.find ("nativechild::passInputThrough (c);", show) != std::string::npos);
    CHECK (tour.find ("juce::ComponentPeer::windowIgnoresMouseClicks", show) != std::string::npos);
    // the ring finds its targets inside the panels' native windows too
    CHECK (tour.find ("nativechild::attachedChildren (peer->getNativeHandle())") != std::string::npos);
    // the card takes no focus
    const auto card = tour.find ("class FirstSongTour::Card final");
    CHECK (tour.find ("setWantsKeyboardFocus (false);", card) < tour.find ("void show (int stepIndex, bool done)", card));
    CHECK (tour.find ("setMouseClickGrabsKeyboardFocus (false);", card) < tour.find ("void show (int stepIndex, bool done)", card));
}

TEST (rcfix190fc_simple_clicks_leave_the_keys_with_the_main_window)
{
    using namespace i18ntest;
    const auto source = readFile ("src/ui/MainComponent.cpp");
    const auto handler = source.find ("void MainComponent::simpleClickEnded (juce::Component* clicked)");
    CHECK (handler != std::string::npos);
    // text fields keep their keys; the overlays and knobs that keep theirs say so
    CHECK (source.find ("dynamic_cast<juce::TextEditor*> (c) != nullptr", handler) != std::string::npos);
    CHECK (source.find ("\"lynxKeepsKeys\"", handler) != std::string::npos);
    CHECK (source.find ("juce::Component::getCurrentlyModalComponent() != nullptr", handler) != std::string::npos);
    CHECK (source.find ("addGlobalMouseListener (&simpleClickKeys)") != std::string::npos);
    CHECK (source.find ("removeGlobalMouseListener (&simpleClickKeys)") != std::string::npos);
    // an overlay that had the keys gives them back when it goes
    CHECK (source.find ("const auto hadKeys = old != nullptr && old != c && old->hasKeyboardFocus (true);") != std::string::npos);
    CHECK (readFile ("src/ui/SimpleDock.cpp").find ("getProperties().set (\"lynxClickKeepsKeys\", true);") != std::string::npos);
    for (const char* file : { "src/ui/StartFromPanel.cpp", "src/ui/FirstSongTour.cpp", "src/ui/SimpleWorkflow.cpp" })
        CHECK (readFile (file).find ("getProperties().set (\"lynxKeepsKeys\", true);") != std::string::npos);
    const auto main = readFile ("src/Main.cpp");
    CHECK (main.find ("args.contains (\"--simplekeytest\")") != std::string::npos);
    // Windows: a key-down unused in an attached window is not also posted to the main window (it ran every shortcut twice)
    const auto native = readFile ("src/ui/NativeChild.cpp");
    const auto forwarder = native.find ("struct KeyForwarder final : juce::KeyListener");
    CHECK (forwarder != std::string::npos);
    CHECK (native.find ("bool keyStateChanged (bool isKeyDown, juce::Component* origin) override", forwarder) < native.find ("KeyForwarder& keyForwarder()"));
}

TEST (rcfix190fc_edison_slot_window_docks)
{
    using namespace i18ntest;
    const auto editor = readFile ("src/ui/AudioEditor.cpp");
    const auto open = editor.find ("AudioEditor* openWindow (AudioEngine& engine, Source source)");
    CHECK (open != std::string::npos);
    CHECK (rcfix190fc::after (editor, "if (source.kind == Source::Kind::slot)\n            dockOrKeepAbove (*window);", open) != std::string::npos);
    // --plugwintest clicks the docked editor where it can be seen (JUCE drops a button-down on a covered point)
    const auto main = readFile ("src/Main.cpp");
    CHECK (main.find ("editor_title_visible_at=") != std::string::npos);
    CHECK (main.find ("lynx::nativechild::postClickForTest (h, { 60, 8 });") == std::string::npos);
}
