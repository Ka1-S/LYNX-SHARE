// Round 190np: the main window's first-run onboarding (core/Onboarding.h has the logic, ui/Onboarding.h the cards): the
// welcome card, its choices, the "Next: 3 things to try" card, the empty-state hints and the contextual tips.
// Part of MainComponent (its members are defined here to keep MainComponent.cpp's hooks short).

#include "ui/MainComponent.h"

#include "core/ClipLauncher.h"
#include "core/DemoSongs.h"
#include "core/FirstSongTour.h"
#include "ui/AudioCheck.h"
#include "ui/FirstSongTour.h"
#include "ui/I18n.h"
#include "ui/StartFromPanel.h"
#include "ui/StartPage.h"
#include "ui/SetupWizard.h"
#include "ui/Updater.h"
#include "platform/Paths.h"

namespace lynx
{
using onboarding::Choice;

namespace
{
juce::File onboardingFile() { return platform::settingsFile ("onboarding.txt"); }

// the Simple layout's own Welcome screen must not open over a choice made on the welcome card
void markSimpleWelcomeSeen (SimpleWorkflow* simple, bool persistent)
{
    if (simple != nullptr)
        if (auto* tour = simple->tourForSnapshot())
            tour->machineForCheck().welcomeShown();
    if (! persistent)
        return;
    const auto file = FirstSongTour::stateFile();
    auto state = tour::parse (file.loadFileAsString().toStdString());
    if (! state.welcomeSeen)
    {
        state.welcomeSeen = true;
        file.replaceWithText (juce::String (tour::serialize (state)));
    }
}
} // namespace

void MainComponent::loadOnboarding()
{
    onboard = onboarding::parse (onboardingFile().loadFileAsString().toStdString());
}

void MainComponent::saveOnboarding()
{
    if (persistent)
        onboardingFile().replaceWithText (juce::String (onboarding::serialize (onboard)));
}

void MainComponent::setOnboardingForSnapshot (bool on)
{
    onboardingOn = on;
    if (! on)
    {
        hideGettingStarted();
        closeTip();
    }
    updateEmptyHint();
}

// --- the welcome card ---------------------------------------------------------------------------------------------------
void MainComponent::showWelcomeCard (bool firstRun)
{
    if (setupOverlay != nullptr)
        return;
    if (firstRun)
    {
        onboard.pending = true; // (until it is answered it comes back: a restart for another language, the app closed)
        saveOnboarding();
        // the first run's background scan (the old wizard started it on Finish; round 190fp: only when "every start" is on)
        if (persistent && backgroundScanAtStart() && ! engine.getPluginHost().backgroundScan().running)
            engine.getPluginHost().startBackgroundScan();
    }
    auto safe = juce::Component::SafePointer<MainComponent> (this);
    onboardui::WelcomeHost host;
    host.choose = [safe, firstRun] (Choice choice) {
        juce::MessageManager::callAsync ([safe, firstRun, choice] { // (the card's own button is still running)
            if (safe == nullptr)
                return;
            safe->setupOverlay.reset();
            if (firstRun && safe->persistent)
            {
                markSetupWizardDone();
                // issue #233: the automatic update check waits for this answer (updater::startAutomaticCheck returns
                // while setup-done.txt is missing); answered with the toggle ticked, the first check runs now
                if (safe->welcomeUpdates)
                    updater::startAutomaticCheck();
            }
            safe->applyWelcomeChoice (choice);
            if (safe->isShowing())
                safe->grabKeyboardFocus();
        });
    };
    host.moreSetup = [safe, firstRun] {
        juce::MessageManager::callAsync ([safe, firstRun] {
            if (safe == nullptr)
                return;
            safe->setupOverlay.reset();
            onboarding::answer (safe->onboard, Choice::none); // (the old wizard decides the rest, as before)
            safe->saveOnboarding();
            if (firstRun && safe->persistent)
                markSetupWizardDone();
            safe->showSetupWizard(); // (its Updates section shows the card's choice; its Finish runs the first check)
        });
    };
    host.language = [safe] (const juce::String& id) {
        if (safe != nullptr)
            safe->showLanguageChoice (id);
    };
    host.testSound = [safe] {
        if (safe != nullptr)
            showAudioCheck (safe->engine);
    };
    host.deviceName = [safe]() -> juce::String {
        if (safe == nullptr || ! safe->persistent)
            return tr ("Speakers (system default)");
        if (auto* device = safe->engine.getDeviceManager().getCurrentAudioDevice())
            return device->getName();
        return tr ("No audio device");
    };
    host.scanStatus = [safe]() -> juce::String {
        if (safe == nullptr || ! safe->persistent)
            return tr ("Plugins: scanning in the background");
        auto& plugins = safe->engine.getPluginHost();
        const auto s = plugins.backgroundScan();
        const auto known = plugins.getKnownPlugins().getNumTypes();
        if (s.running)
            return tr ("Plugins: scanning in the background");
        return tr ("Plugins: {0} found", { juce::String (known) });
    };
    // issue #233 (rollup 0.1.31 step 2): round 190ns's Updates choice on the card's bottom row, ticked by default; any answer
    // applies it (the snapshot / test windows only remember it: welcomeUpdatesForCheck)
    host.updatesShown = updater::offered();
    host.updatesOn = persistent ? updater::automatic() : true;
    welcomeUpdates = host.updatesOn;
    host.updates = [safe] (bool on) {
        if (safe == nullptr)
            return;
        safe->welcomeUpdates = on;
        if (safe->persistent)
            updater::setAutomatic (on);
    };
    setupOverlay = onboardui::makeWelcomeOverlay (std::move (host));
    addAndMakeVisible (*setupOverlay);
    setupOverlay->setBounds (getLocalBounds());
    setupOverlay->toFront (true);
    hostSetupOverlayNatively();
}

void MainComponent::showStartPage()
{
    // issue #252 (round 190oq): never over the welcome card / setup wizard or another overlay
    if (setupOverlay != nullptr)
        return;
    auto safe = juce::Component::SafePointer<MainComponent> (this);
    startpageui::Host host;
    for (int i = 0; i < recent.getNumFiles(); ++i)
    {
        const auto f = recent.getFile (i);
        startpage::Recent r;
        r.path = f.getFullPathName().toStdString();
        r.name = f.getFileNameWithoutExtension().toStdString();
        r.folder = f.getParentDirectory().getFullPathName().toStdString();
        r.exists = f.existsAsFile();
        r.modifiedMs = r.exists ? f.getLastModificationTime().toMilliseconds() : 0;
        r.bytes = r.exists ? f.getSize() : 0;
        host.recent.push_back (std::move (r));
    }
    const auto entries = startfrom::entries();
    for (const auto& e : entries)
    {
        host.templates.push_back (e.name);
        using Kind = startfrom::Entry::Kind;
        host.templateDetails.push_back (e.kind == Kind::demo ? tr ("Demo song") : e.kind == Kind::lynxDefault ? tr ("LYNX SHARE's default project")
                                        : e.kind == Kind::builtIn ? tr ("Template") : tr ("Your template"));
    }
    const auto closeThen = [safe] (std::function<void (MainComponent&)> then) {
        juce::MessageManager::callAsync ([safe, then = std::move (then)] { // (the page's own button is still running)
            if (safe == nullptr)
                return;
            safe->setupOverlay.reset();
            if (safe->isShowing())
                safe->grabKeyboardFocus();
            if (then)
                then (*safe);
        });
    };
    host.close = [closeThen] { closeThen ({}); };
    host.open = [closeThen] (const juce::String& path) {
        closeThen ([path] (MainComponent& m) {
            const juce::File file (path);
            if (! file.existsAsFile())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Open"),
                                                        tr ("{0} is no longer there.", { file.getFullPathName() }));
                return;
            }
            m.confirmDiscard ([safe = juce::Component::SafePointer<MainComponent> (&m), file] {
                if (safe != nullptr)
                    safe->openFile (file);
            });
        });
    };
    host.newProject = [closeThen] { closeThen ([] (MainComponent& m) { m.newProject (true); }); };
    host.openDialog = [closeThen] { closeThen ([] (MainComponent& m) { m.chooseFileToOpen(); }); };
    host.newFromTemplate = [closeThen, entries] (int index) {
        if (index < 0 || index >= (int) entries.size())
            return;
        closeThen ([entry = entries[(std::size_t) index]] (MainComponent& m) {
            m.confirmDiscard ([safe = juce::Component::SafePointer<MainComponent> (&m), entry] {
                if (safe == nullptr)
                    return;
                if (auto song = startfrom::songOf (entry))
                    safe->startNewProject (std::move (*song), {});
                else
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("New from template"),
                                                            tr ("{0} could not be opened.", { entry.name }));
            });
        });
    };
    host.showAtStart = startpage::enabled();
    host.setShowAtStart = [safe] (bool on) {
        startpage::setEnabled (on);
        if (safe != nullptr)
            safe->saveUiSettings();
    };
    setupOverlay = startpageui::makeOverlay (std::move (host));
    addAndMakeVisible (*setupOverlay);
    setupOverlay->setBounds (getLocalBounds());
    setupOverlay->toFront (true);
    hostSetupOverlayNatively();
}

void MainComponent::applyWelcomeChoice (Choice choice)
{
    onboarding::answer (onboard, choice);
    saveOnboarding();
    if (choice == Choice::none) // Skip / × / Esc: the Pattern layout as it is, nothing started
    {
        updateEmptyHint();
        return;
    }
    const auto style = onboarding::layoutFor (choice);
    if (style == layoutstyle::Style::simple)
        markSimpleWelcomeSeen (simpleWorkflow.get(), persistent);
    if (style != layoutStyle || (style == layoutstyle::Style::simple && simpleWorkflow == nullptr))
    {
        setLayoutStyle (style); // (the runtime switch, as View › Layout style: F3 of the 190nj study)
        saveUiSettings();
    }
    switch (onboarding::starterFor (choice))
    {
        case onboarding::Starter::starterBeat:
            if (simpleWorkflow != nullptr && simpletracks::isEmptySong (engine.getSong()))
            {
                simpleWorkflow->doTourStep (tour::beat); // (Simple's own starter: a Drums track with a beat, its tempo)
                const auto& song = engine.getSong();
                for (int t = (int) song.playlistTracks.size() - 1; t >= 0; --t)
                    if (simpletracks::kindOf (song, t) == simpletracks::Kind::drums)
                    {
                        playlist.selectTrack (t);
                        simpleWorkflow->trackSelected (t); // (its Drum Machine in the dock)
                        break;
                    }
            }
            break;
        case onboarding::Starter::newTrackCard:
            if (simpleWorkflow != nullptr && simpletracks::isEmptySong (engine.getSong()) && ! simpleWorkflow->isChooserShowing())
                simpleWorkflow->showChooser();
            break;
        case onboarding::Starter::tour:
            if (simpleWorkflow != nullptr)
                simpleWorkflow->startOrContinueTour();
            break;
        default:
            break;
    }
    // the Mixer of the timeline group comes up with the first track (coordinator 190np: shown at once, it covered the
    // New-track card); F9 shows it any time before that
    mixerAfterFirstTrack = onboarding::showsMixer (choice) && simpletracks::isEmptySong (engine.getSong());
    if (onboarding::showsMixer (choice) && ! mixerAfterFirstTrack)
        showPanel (Panel::mixer);
    if (! onboarding::next3 (choice).empty())
        showGettingStarted();
    updateEmptyHint();
}

// --- Next: 3 things to try ----------------------------------------------------------------------------------------------
void MainComponent::showGettingStarted()
{
    auto choice = onboarding::chosen (onboard);
    if (onboarding::next3 (choice).empty())
        choice = onboarding::choiceForLayout (layoutStyle);
    if (onboard.next3Hidden)
    {
        onboard.next3Hidden = false;
        saveOnboarding();
    }
    if (next3Card == nullptr)
    {
        next3Card = std::make_unique<onboardui::Next3Card>();
        auto safe = juce::Component::SafePointer<MainComponent> (this);
        next3Card->act = [safe] (onboarding::Item item) {
            juce::MessageManager::callAsync ([safe, item] {
                if (safe != nullptr)
                    safe->next3Act (item);
            });
        };
        next3Card->close = [safe] {
            juce::MessageManager::callAsync ([safe] {
                if (safe != nullptr)
                    safe->hideGettingStarted();
            });
        };
        next3Card->tutorials = [] { juce::URL ("https://lynx-share.com/wiki/getting-started/").launchInDefaultBrowser(); };
    }
    if (persistent)
        onboardCtx.plugins = engine.getPluginHost().getKnownPlugins().getNumTypes();
    const auto all = onboarding::tick (onboard, choice, engine.getSong(), onboardCtx);
    next3Card->show (choice, onboard.ticked, all);
    placeOnboardingCards();
}

void MainComponent::hideGettingStarted()
{
    if (next3Card == nullptr)
        return;
    onboardui::removeHosted (*next3Card);
    next3Card.reset();
    if (! onboard.next3Hidden)
    {
        onboard.next3Hidden = true; // (Help › Getting started shows it again)
        saveOnboarding();
    }
}

void MainComponent::next3Act (onboarding::Item item)
{
    using onboarding::Item;
    switch (item)
    {
        case Item::play:
            if (! engine.getTransport().playing.load())
                playStop();
            break;
        case Item::addLoop:
            if (layoutStyle == layoutstyle::Style::simple)
                showLoopsDock();
            else
                showPanel (Panel::browser);
            break;
        case Item::recordVoice:
        case Item::recordAudio:
            if (simpleWorkflow != nullptr && layoutStyle == layoutstyle::Style::simple)
            {
                simpletracks::Request q;
                q.kind = simpletracks::Kind::voice;
                q.input = 0;
                q.inputStereo = false;
                simpleWorkflow->add (std::move (q)); // (armed: Record takes the voice)
            }
            else
                showPanel (Panel::mixer);
            break;
        case Item::steps: showPanel (Panel::channelRack); break;
        case Item::openFlp:
        case Item::openAls: chooseFileToOpen(); break;
        case Item::plugins: showSetupWizard(); break; // (its plugin folders and Scan now)
        case Item::slotClip: showPanel (Panel::browser); break;
        case Item::instrumentTrack:
            if (simpleWorkflow != nullptr && layoutStyle == layoutstyle::Style::simple)
                simpleWorkflow->doTourStep (tour::instrument);
            break;
        case Item::openMixer: showPanel (Panel::mixer); break;
        default: break;
    }
    updateOnboarding();
    placeOnboardingCards();
}

void MainComponent::onboardingTick()
{
    // what the view shows (sticky): the transport ran, the Mixer is up, a clip launched, plugins are known
    auto changed = false;
    const auto set = [&changed] (bool& flag, bool now) {
        if (now && ! flag)
        {
            flag = true;
            changed = true;
        }
    };
    set (onboardCtx.played, engine.getTransport().playing.load());
    set (onboardCtx.mixerShown, workspace.isPanelVisible (Panel::mixer));
    if (const auto* launcher = engine.launcherState())
        set (onboardCtx.sceneLaunched, launcher->anyPlaying.load());
    if (persistent)
        if (const auto known = engine.getPluginHost().getKnownPlugins().getNumTypes(); known != onboardCtx.plugins)
        {
            onboardCtx.plugins = known;
            changed = true;
        }
    if (changed)
        updateOnboarding();
    placeOnboardingCards(); // (the Loops dock or the Simple dock opened or closed: the card stays beside them, never over them)
}

void MainComponent::updateOnboarding()
{
    if (! onboardingOn)
        return;
    if (mixerAfterFirstTrack && ! simpletracks::isEmptySong (engine.getSong()))
    {
        mixerAfterFirstTrack = false;
        if (! workspace.isPanelVisible (Panel::mixer))
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
                if (safe != nullptr)
                {
                    safe->showPanel (Panel::mixer);
                    safe->placeOnboardingCards();
                }
            });
    }
    if (next3Card != nullptr)
    {
        const auto before = onboard.ticked;
        const auto choice = next3Card->getChoice();
        const auto all = onboarding::tick (onboard, choice, engine.getSong(), onboardCtx);
        const auto wasAll = onboard.ticked == before && all;
        next3Card->show (choice, onboard.ticked, all);
        if (onboard.ticked != before)
        {
            saveOnboarding();
            placeOnboardingCards(); // (the "all done" line makes it taller)
        }
        if (all && ! wasAll) // "Nice, that's the basics": it goes away by itself after a while
            juce::Timer::callAfterDelay (8000, [safe = juce::Component::SafePointer<MainComponent> (this)] {
                if (safe != nullptr && safe->next3Card != nullptr)
                    safe->hideGettingStarted();
            });
    }
    updateEmptyHint();
}

// --- the empty-state hint -----------------------------------------------------------------------------------------------
void MainComponent::updateEmptyHint()
{
    using onboarding::EmptyHint;
    auto kind = EmptyHint::none;
    if (onboardingOn)
    {
        auto simpleCard = false;
        if (simpleWorkflow != nullptr && layoutStyle == layoutstyle::Style::simple)
        {
            const auto* tour = simpleWorkflow->tourForSnapshot();
            simpleCard = simpleWorkflow->isChooserShowing() || simpleWorkflow->isStartFromShowing() || (tour != nullptr && tour->isWelcomeShowing())
                      || simpleWorkflow->tourRunning();
        }
        kind = onboarding::emptyHintFor (layoutStyle, engine.getSong(), simpleCard, ! onboard.hintsOff);
        if (kind == EmptyHint::clipGrid && clipGridView == nullptr)
            kind = EmptyHint::none;
    }
    juce::Component* parent = kind == EmptyHint::clipGrid ? clipGridView.get() : kind == EmptyHint::none ? nullptr : static_cast<juce::Component*> (&playlist);
    if (emptyHint != nullptr && (emptyHint->getKind() != kind || emptyHint->getParentComponent() != parent))
    {
        emptyHint->attachTo (nullptr);
        emptyHint.reset();
    }
    if (kind == EmptyHint::none)
        return;
    if (emptyHint == nullptr)
    {
        auto safe = juce::Component::SafePointer<MainComponent> (this);
        const auto later = [safe] (std::function<void (MainComponent&)> fn) {
            return [safe, fn] {
                juce::MessageManager::callAsync ([safe, fn] {
                    if (safe != nullptr)
                        fn (*safe);
                });
            };
        };
        std::vector<onboardui::EmptyHintCard::Button> buttons;
        std::function<juce::Rectangle<int>()> where;
        if (kind == EmptyHint::playlist)
        {
            buttons.push_back ({ tr ("Channel Rack (F6)"), later ([] (MainComponent& m) { m.showPanel (Panel::channelRack); }), "empty.channelRack" });
            buttons.push_back ({ tr ("Open a demo song"), later ([] (MainComponent& m) {
                                     m.confirmDiscard ([safe = juce::Component::SafePointer<MainComponent> (&m)] {
                                         if (safe != nullptr)
                                             safe->startNewProject (demos::make (1), {});
                                     });
                                 }),
                                 "empty.demo" });
            buttons.push_back ({ tr ("Open an .flp project..."), later ([] (MainComponent& m) { m.chooseFileToOpen(); }), "empty.open" });
            where = [this] { return playlist.gridAreaForHints(); };
        }
        else if (kind == EmptyHint::clipGrid)
        {
            buttons.push_back ({ tr ("Browse loops"), later ([] (MainComponent& m) { m.showPanel (Panel::browser); }), "empty.browse" });
            buttons.push_back ({ tr ("Open an .als set..."), later ([] (MainComponent& m) { m.chooseFileToOpen(); }), "empty.open" });
            where = [this] {
                auto* grid = clipGridView.get();
                if (grid == nullptr)
                    return juce::Rectangle<int>();
                auto b = grid->getLocalBounds();
                return b.withHeight (juce::jmax (260, b.getHeight() * 55 / 100));
            };
        }
        else
        {
            buttons.push_back ({ tr ("+ Add track"), later ([] (MainComponent& m) {
                                     if (m.simpleWorkflow != nullptr)
                                         m.simpleWorkflow->showChooser();
                                 }),
                                 "empty.addTrack" });
            buttons.push_back ({ tr ("Open Loops"), later ([] (MainComponent& m) { m.showLoopsDock(); }), "empty.loops" });
            buttons.push_back ({ tr ("First-song tour"), later ([] (MainComponent& m) { m.startFirstSongTourAnywhere(); }), "empty.tour" });
            where = [this] { return playlist.gridAreaForHints(); };
        }
        emptyHint = std::make_unique<onboardui::EmptyHintCard> (kind, std::move (buttons), std::move (where));
    }
    emptyHint->attachTo (parent);
}

// --- Help › First-song tour in every layout -----------------------------------------------------------------------------
void MainComponent::startFirstSongTourAnywhere()
{
    markSimpleWelcomeSeen (simpleWorkflow.get(), persistent);
    if (layoutStyle != layoutstyle::Style::simple || simpleWorkflow == nullptr)
    {
        setLayoutStyle (layoutstyle::Style::simple); // (the tour's controls are the Simple layout's)
        saveUiSettings();
    }
    if (simpleWorkflow != nullptr)
        simpleWorkflow->startOrContinueTour();
    updateEmptyHint();
}

// --- contextual tips ----------------------------------------------------------------------------------------------------
bool MainComponent::maybeShowTip (onboarding::Tip tip, juce::Component* anchor)
{
    if (! onboardingOn || anchor == nullptr || ! tipGate.canShow (tip, onboard) || setupOverlay != nullptr)
        return false;
    tipGate.shown();
    closeTip();
    tipCard = std::make_unique<onboardui::TipCard> (tip);
    tipAnchor = anchor;
    auto safe = juce::Component::SafePointer<MainComponent> (this);
    const auto close = [safe] {
        juce::MessageManager::callAsync ([safe] {
            if (safe != nullptr)
                safe->closeTip();
        });
    };
    tipCard->action = [safe, tip, close] {
        if (safe == nullptr)
            return;
        if (tip == onboarding::Tip::songModeEmpty)
        {
            if (safe->patternModeButton.onClick) // (Pattern mode: the pattern being edited plays)
                safe->patternModeButton.onClick();
        }
        else
            juce::MessageManager::callAsync ([safe] {
                if (safe != nullptr)
                    safe->showAudioSettings();
            });
        close();
    };
    tipCard->gotIt = [safe, tip, close] {
        if (safe == nullptr)
            return;
        onboarding::TipGate::gotIt (safe->onboard, tip);
        safe->saveOnboarding();
        close();
    };
    tipCard->noMore = [safe, close] {
        if (safe == nullptr)
            return;
        onboarding::TipGate::noMoreTips (safe->onboard);
        safe->saveOnboarding();
        close();
    };
    placeOnboardingCards();
    return true;
}

void MainComponent::closeTip()
{
    if (tipCard == nullptr)
        return;
    onboardui::removeHosted (*tipCard);
    tipCard.reset();
    tipAnchor = nullptr;
}

// --- placing the cards ---------------------------------------------------------------------------------------------------
void MainComponent::placeOnboardingCards()
{
    if (next3Card != nullptr)
    {
        // the lower right of the Playlist (M2): clear of the toolbar, the ruler, the first tracks and the drop lane, and
        // above the Simple dock / Clip-launch detail view (not part of the Playlist's own bounds) and its scroll bar
        auto area = playlist.isShowing() || (! persistent && playlist.isVisible()) ? onboardui::areaIn (*this, playlist) : juce::Rectangle<int>();
        const auto w = onboardui::Next3Card::width, h = next3Card->idealHeight();
        if (area.getWidth() < w + 40 || area.getHeight() < h + PlaylistComponent::toolbarHeight + 60)
            area = getLocalBounds().withTrimmedTop (toolbarHeight);
        auto bounds = juce::Rectangle<int> (area.getRight() - w - 28, area.getBottom() - h - 36, w, h)
                          .constrainedWithin (getLocalBounds().withTrimmedTop (toolbarHeight));
        // never over the Mixer (shown with the timeline choice, or by F9): above it, else at the Playlist's top
        if (mixer.isShowing() || (! persistent && mixer.isVisible()))
            if (const auto m = onboardui::areaIn (*this, mixer).withTrimmedTop (-30); m.intersects (bounds)) // (with its panel's title row)
            {
                const auto top = area.getY() + PlaylistComponent::toolbarHeight + 16;
                bounds.setY (m.getY() - h - 16 >= top ? m.getY() - h - 16 : top);
            }
        onboardui::hostOnTop (*next3Card, *this, bounds);
    }
    if (tipCard != nullptr)
    {
        const auto w = onboardui::TipCard::width, h = tipCard->idealHeight();
        auto bounds = juce::Rectangle<int> (getWidth() / 2 - w / 2, toolbarHeight + 8, w, h);
        if (tipAnchor != nullptr && (tipAnchor->isShowing() || (! persistent && tipAnchor->isVisible())))
        {
            const auto ring = onboardui::areaIn (*this, *tipAnchor);
            const tour::Box r { ring.getX(), ring.getY(), ring.getWidth(), ring.getHeight() };
            const tour::Box all { 0, 0, getWidth(), getHeight() };
            if (const auto p = tour::placeCard (r, all, w, h, 10); p.side != tour::Side::none)
                bounds = { p.card.x, p.card.y, p.card.w, p.card.h };
        }
        onboardui::hostOnTop (*tipCard, *this, bounds);
    }
}
} // namespace lynx

namespace lynx
{
void MainComponent::checkRecordTip()
{
    if (! onboardingOn)
        return;
    const auto o = engine.getRecordOptions();
    int inputs = 0;
    if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
        inputs = device->getActiveInputChannels().countNumberOfSetBits();
    if (onboarding::recordNoInputSignal (o.recordAudio, o.recordNotes, layoutStyle == layoutstyle::Style::simple, inputs, ! engine.getArmedInserts().empty()))
        maybeShowTip (onboarding::Tip::recordNoInput, &recordButton);
}
} // namespace lynx
