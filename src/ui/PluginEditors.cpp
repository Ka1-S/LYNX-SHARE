#include "ui/SlotWatch.h"
#include "ui/FrameTicker.h"
#include "ui/PatcherEditor.h"
#include "ui/ControllerEditors.h"
#include "ui/Controllers164.h"
#include "ui/NewtoneEditor.h"
#include "ui/AudioEditor.h"
#include "ui/VisualEditors.h"
#include "ui/FlExtraEditors.h"
#include "ui/PluginEditors.h"
#include "core/EditorLimit.h"
#include "core/LuxeVerb.h"
#include "core/PresetCycle.h"
#include "core/ShownEffectSlot.h"

#include <cstring>
#include <map>
#include <array>
#include <optional>

#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/Transmitter.h"
#include "core/TransporterFl.h"
#include "core/FruityShapers.h"
#include "core/FlCurve.h"
#include "core/GrossBeat.h"
#include "core/LynxTune.h"
#include "core/MaximusFx.h"
#include "core/MixSuite.h"
#include "core/EchoSuite.h"
#include "core/VoiceTools.h"
#include "core/ParametricEqFl.h"
#include "core/VocalPitch.h"
#include "core/FruityBasicFx.h"
#include "core/PluginLayout.h"
#include "core/StateParams.h"
#include "core/ConvolverFx.h"
#include "core/EquoFx.h"
#include "core/MultibandDelayFx.h"
#include "core/Vocodex.h"
#include "core/LovePhilter.h"
#include "core/HardwareInsert.h"
#include "engine/Text.h"
#include "ui/DynamicsDisplays.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/FacetEqEditor.h"
#include "ui/ProEditors.h"
#include "ui/ParametricEqEditor.h"
#include "core/EffectPresets.h"
#include "ui/PluginHeader.h"
#include "ui/PluginSkin.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

#include "ui/DesignDisplays.inc" // round 168r: the graphs of the designed editors

namespace lynx
{
namespace
{
bool demoDisplays = false; // --plugui: live displays show a synthetic signal

using plugui::Item;
using plugui::LayoutItem;

constexpr float headerHeight = 26.0f; // the presets bar (not scaled with the body)

bool hasStyle (const LayoutItem& item, std::uint32_t flag) noexcept
{
    return (item.style & flag) != 0;
}

// a row of tabs drawn as a column (taller than wide: Maximus' band selector)
bool isVertical (const LayoutItem& item) noexcept
{
    return item.type == Item::tabs && item.h > item.w * 1.2f;
}

// --- the editor ----------------------------------------------------------------------------------

class NativeEffectEditor final : public juce::Component,
                                 public ui::HintSource,
                                 private juce::ChangeListener,
                                 private juce::Timer
{
public:
    // `node` (round 165c): the editor of an effect node inside a Patcher instead of a mixer slot — `node.locate` finds its
    // slot in the song, `node.running` its running instance (the live meters); no presets bar, no automation targets
    struct NodeTarget
    {
        std::function<EffectSlot* (Song&)> locate;
        std::function<std::shared_ptr<Effect>()> running;
        juce::String name;
    };

    NativeEffectEditor (AudioEngine& e, int t, int s, float initialScale, int initialPage, NodeTarget nodeTarget = {})
        : engine (e),
          track (t),
          slot (s),
          node (std::move (nodeTarget))
    {
        if (const auto* fx = current())
        {
            kind = fx->kind;
            layout = &plugui::layoutFor (kind);
            if (kind == EffectKind::ember) // round 165: this window only opens for the Classic voicing — its one-screen
            {                              // panel, not the 1110-unit multiband page around it
                static const auto classic = [] { auto l = plugui::layoutFor (EffectKind::ember); l.height = 380; return l; }(); // (tp172d: 760 × 380)
                layout = &classic;
            }
            if (kind == EffectKind::maximus) // Soundgoodizer runs on the Maximus engine: its own page
                if (const auto blob = maximusfx::unpack (fx->state); blob && blob->build == hlfx::MaximusBuild::soundgoodizer)
                    layout = &plugui::soundgoodizerLayout();
            if (isSoundgoodizerApprox (*fx)) // without FL's preset files: the same page drives the approximation
            {
                sgApprox = true;
                layout = &plugui::soundgoodizerLayout();
            }
        }

        accent = skin::accentFor (sgApprox ? EffectKind::maximus : kind);
        familyAccent = accent;
        if (skin::artForEffect (kind) != skin::Art::none && ! sgApprox) // round 170j: its own art (Themed)
        {
            artLayer = std::make_unique<skin::ArtLayer>();
            addAndMakeVisible (*artLayer);
            artLayer->setArt (skin::artForEffect (kind), [this] { return editorStyle() == skin::Style::themed; });
        }
        if (kind == EffectKind::ember || kind == EffectKind::kilnChannel) // round 165: the Classic voicing's VU shows the output level, as the reference's
            vuMode = 2;
        anim.onFrame = [this] { repaint(); };
        page = initialPage;
        if (kind == EffectKind::equo && initialPage == 0)
            page = 3; // bank 1, volume (FL opens EQUO on its first bank)
        if (kind == EffectKind::luxeVerb && initialPage == 0) // round tp172b: open on the engine in use
            if (const auto* fx = current())
                page = fx->params[(std::size_t) luxeVerbSpaceFirst] >= 0.5f ? 1 : 0;
        if (kind == EffectKind::maximus && stateBacked() && initialPage == 0)
            if (const auto* fx = current())
                page = maximusfx::selectedBand (fx->state); // FL reopens Maximus on the band selected when it was saved
        engine.addChangeListener (this);
        const auto& l = activeLayout();
        for (const auto& item : l.items) // live meters and graphs (mixing suite, LYNX Tune's pitch, round 108's dynamics): 30 Hz
            if (item.type == Item::display && item.display != nullptr
                && (std::strcmp (item.display, "grmeter") == 0 || std::strcmp (item.display, "grleds") == 0 || std::strcmp (item.display, "vumeter") == 0 || std::strcmp (item.display, "iometer") == 0 || std::strcmp (item.display, "outmeter") == 0 || std::strcmp (item.display, "limitpill") == 0 || std::strcmp (item.display, "gcmeter") == 0
                    || std::strcmp (item.display, "pitchgraph") == 0 || std::strcmp (item.display, "panpos") == 0 || std::strcmp (item.display, "hwping") == 0
                    || std::strcmp (item.display, "pitchtrace") == 0 || std::strcmp (item.display, "pitchring") == 0 || std::strcmp (item.display, "tssenvelope") == 0
                    || dynui::isLive (item.display) || designui::isLive (item.display)))
            {
                liveDisplays = true;
                break;
            }
        for (const auto& item : l.items) // round 170c: a level meter well shows the slot's live input / output peaks
            if (item.type == Item::display && item.display != nullptr && levelDisplay (item.display))
                levelMeter = true;
        levelTrail.fill ({ -60.0f, -60.0f });
        if (levelMeter && ! node.locate)
            if (auto* times = engine.getProcessTimes(); times != nullptr && levelKey() < times->levelWanted.size())
                times->levelWanted[levelKey()].fetch_add (1, std::memory_order_relaxed);
        if (! node.locate)
            startTimerHz (30); // live displays, and the controls following their automation (cheap while nothing moves)
        else if (liveDisplays)
            startTimerHz (30);
        // round 170k2: never narrower than its header needs to show the whole name (the body keeps its scale)
        setSize (juce::jmax ((int) std::ceil ((float) l.width * initialScale), minimumWidth()),
                 (int) std::ceil ((float) l.height * initialScale + headerHeight + footerHeight()));
    }

    ~NativeEffectEditor() override
    {
        stopTimer();
        if (levelMeter && ! node.locate)
            if (auto* times = engine.getProcessTimes(); times != nullptr && levelKey() < times->levelWanted.size())
                times->levelWanted[levelKey()].fetch_sub (1, std::memory_order_relaxed);
        engine.removeChangeListener (this);
        ui::hideValueTip();
    }

    // the design size (for the window's aspect ratio)
    juce::Point<int> designSize() const { return { activeLayout().width, activeLayout().height }; }
    juce::String headerName() const { return layout == &plugui::soundgoodizerLayout() ? juce::String ("Soundgoodizer") : utf8 (effectInfo (kind).name); }

    // round 170k2: the narrowest window whose header still shows the plugin's whole name (the bar at its narrowest)
    int minimumWidth() const
    {
        const auto name = headerName();
        const auto left = menuButton().getRight() + 40.0f; // the arrow, LYNX's mark and the LED
        const auto right = usesBar() ? pluginheader::PresetBar::narrowWidth() : 22.0f + 26.0f + 90.0f + 4.0f + 6.0f; // prev, next, preset box
        return juce::roundToInt (std::ceil (left + skin::titleMinWidth (name, headerHeight) + right));
    }

    // The slot as shown: while the song plays, its automated parameters at their automation's value (issue #11 comment 4:
    // Parametric EQ 2's handles, Fruity Filter's knobs... move with the automation, as in FL). Edits always go through
    // setParams, so they change the Song's values.
    const EffectSlot* current() const
    {
        const auto* fx = stored();
        return node.locate ? fx : shownSlot.get (engine.getAutomationDisplay(), fx, track, slot, juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown());
    }

    // the slot in the Song (or the Patcher node)
    const EffectSlot* stored() const
    {
        if (node.locate)
        {
            const auto* fx = node.locate (const_cast<Song&> (engine.getSong()));
            return fx != nullptr && (layout == nullptr || fx->kind == kind) ? fx : nullptr;
        }
        const auto& mixer = engine.getSong().mixer;

        if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
            return nullptr;

        const auto& fx = mixer[(std::size_t) track].effects[(std::size_t) slot];
        return layout == nullptr || fx.kind == kind ? &fx : nullptr;
    }

    // --- geometry ----------------------------------------------------------------------------------

    const plugui::Layout& activeLayout() const
    {
        static const plugui::Layout empty { "", 360, 120, {} };
        return layout != nullptr ? *layout : empty;
    }

    float scale() const
    {
        const auto& l = activeLayout();
        return juce::jmax (0.25f, juce::jmin ((float) getWidth() / (float) l.width, ((float) getHeight() - headerHeight - footerHeight()) / (float) l.height));
    }

    // LYNX's own engines show a one-line description of the control under the mouse at the bottom (the reference
    // reverbs' description line, round 129)
    bool hasHelpLine() const
    {
        return layout != nullptr && layout != &plugui::soundgoodizerLayout()
               && (std::find (plugui::ownLayoutKinds().begin(), plugui::ownLayoutKinds().end(), kind) != plugui::ownLayoutKinds().end()
                   || kind == EffectKind::lynxTune); // round 135: LYNX Tune's panel too
    }
    float footerHeight() const { return hasHelpLine() ? 20.0f : 0.0f; }

    void paintHelpLine (juce::Graphics& g)
    {
        using namespace theme;
        const auto area = juce::Rectangle<float> (0.0f, (float) getHeight() - footerHeight(), (float) getWidth(), footerHeight());
        g.setColour (colour::bg0.withAlpha (0.55f));
        g.fillRect (area);
        juce::String text;
        if (hoverItem >= 0 && hoverItem < (int) activeLayout().items.size())
        {
            const auto& item = activeLayout().items[(std::size_t) hoverItem];
            if (const auto p = paramOf (item); p >= 0)
            {
                const auto what = pluginheader::describe (kind, effectInfo (kind).params[(std::size_t) p].name);
                text = (item.label != nullptr && *item.label != 0 ? tr (item.label) : utf8 (effectInfo (kind).params[(std::size_t) p].name))
                       + (what.isNotEmpty() ? juce::String::fromUTF8 (" \xe2\x80\x94 ") + what : juce::String());
            }
        }
        // round 135: nothing under the mouse -> what the current mode is (the reference's summary of the algorithm)
        if (text.isEmpty())
            if (const auto* fx = current())
                text = pluginheader::modeLine (kind, fx->params, engine.getSong().bpm);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawFittedText (text, area.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    }

    // round 135: the MIX caption of a LYNX engine toggles the mix lock (the reference reverbs: click the MIX label)
    bool isMixItem (const LayoutItem& item) const
    {
        const auto p = paramOf (item);
        return (item.type == Item::knob || (item.type == Item::vslider && ! hasStyle (item, plugui::captionLeft))) && p >= 0 && hasHelpLine()
               && item.label != nullptr && ! hasStyle (item, plugui::noCaption) && juce::String (effectInfo (kind).params[(std::size_t) p].name) == "Mix";
    }
    juce::Rectangle<float> captionArea (const LayoutItem& item) const
    {
        const auto r = rectOf (item);
        const auto s = scale();
        if (item.type == Item::vslider)
            return r.withTop (r.getBottom() - 13.0f * s);
        if (hasStyle (item, plugui::captionAbove))
            return r.withHeight (14.0f * s);
        const auto circle = knobCircle (item);
        return r.withTop (circle.getBottom()).withHeight (13.0f * s);
    }

    juce::Rectangle<float> rectOf (const LayoutItem& item) const
    {
        const auto s = scale();
        return { item.x * s, headerHeight + item.y * s, item.w * s, item.h * s };
    }

    // round tp172c: the art drawn now — Ridge Strip's 4K voicing has its own (dark) console look
    skin::Art artNow() const
    {
        const auto art = skin::artForEffect (kind);
        if (art == skin::Art::luxe) // round tp172b4: the Space page has the Pro-R role's own look
            return page == 1 ? skin::Art::luxeSpace : art;
        if (art != skin::Art::ridge) return art;
        const auto* fx = current();
        if (fx == nullptr) return art;
        const auto on = [&] (const char* name) {
            const auto i = plugui::paramIndex (kind, name);
            return i >= 0 && i < (int) fx->params.size() && fx->params[(std::size_t) i] >= 0.5f;
        };
        return on ("4K voicing") && ! on ("Modern voicing") && ! on ("Vocal strip") ? skin::Art::ridge4k : art;
    }

    bool visible (const LayoutItem& item) const
    {
        const auto* fx = current();
        return fx != nullptr && plugui::isShown (kind, item, page, fx->params, &fx->state);
    }

    // the destination-insert menus: Fruity Send's "Send to", the Transporter's loop send (round 167n)
    bool isSendTarget (int p) const
    {
        return (kind == EffectKind::fruitySend && p == 3) || (kind == EffectKind::flTransporter && p == transporter::sendParam);
    }

    // wide option menus of LYNX's own panels get step arrows at both ends (round 114)
    bool hasStepArrows (const LayoutItem& item) const
    {
        return item.type == Item::menu && item.w >= 110.0f && ! hasStyle (item, plugui::plainMenu) && kind != EffectKind::parametricEq && kind != EffectKind::fruitySend && kind != EffectKind::hardwareInsert
               && kind != EffectKind::flTransporter
               && std::find (plugui::ownLayoutKinds().begin(), plugui::ownLayoutKinds().end(), kind) != plugui::ownLayoutKinds().end();
    }

    // the knob's circle inside its cell (the caption takes the rest)
    juce::Rectangle<float> knobCircle (const LayoutItem& item) const
    {
        auto r = rectOf (item);
        const auto s = scale();
        if (hasStyle (item, plugui::valueAbove))
            r.removeFromTop (16.0f * s);
        if (hasStyle (item, plugui::captionAbove))
        {
            r.removeFromTop (14.0f * s);
            r.removeFromBottom (13.0f * s);
        }
        else if (! hasStyle (item, plugui::noCaption))
            r.removeFromBottom ((hasStyle (item, plugui::showValue) || hasStyle (item, plugui::hoverValue) ? 26.0f : 14.0f) * s);
        const auto d = juce::jmin (r.getWidth(), r.getHeight());
        return r.withSizeKeepingCentre (d, d).withY (r.getY());
    }

    int itemAt (juce::Point<float> p) const
    {
        const auto& items = activeLayout().items;

        for (int i = (int) items.size(); --i >= 0;)
        {
            const auto& item = items[(std::size_t) i];
            if (! visible (item) || item.type == Item::section || item.type == Item::label)
                continue;
            if (item.type == Item::display && rectOf (item).contains (p)
                && (plugui::displayTakesMouse (item.display) // round 168x: the list lives in PluginLayout (Maximus' curve added)
                    || isBars (item) || isRhythmEdit (item) || isRhythmPatterns (item)
                    || isGbAny (item))) // round 168m: Gross Beat's editor parts
                return i;
            if (item.type == Item::display || (item.param == nullptr && ! hasStyle (item, plugui::pageTabs)))
                continue;
            if (rectOf (item).contains (p))
                return i;
        }
        return -1;
    }

    int paramOf (const LayoutItem& item) const { return item.param != nullptr ? plugui::paramIndex (kind, item.param) : -1; }

    // --- values --------------------------------------------------------------------------------------

    // Controls stored in the slot's state (core/StateParams.h: Maximus' bands, Delay Bank's banks, band tables...)
    // edit those bytes instead of EffectSlot::params
    // the header's undo / A-B / preset bar (round 129): every editor whose setting is its parameters
    bool usesBar() const { return ! node.locate && ! stateBacked() && layout != &plugui::soundgoodizerLayout() && layout != nullptr; }

    // an edit of this editor's slot (a mixer slot or a Patcher node's)
    void editSlot (std::function<void (EffectSlot&)> change)
    {
        if (node.locate)
        {
            engine.editSong ([l = node.locate, change = std::move (change)] (Song& song) {
                if (auto* fx = l (song))
                    change (*fx);
            });
            return;
        }
        engine.editSong ([t = track, sl = slot, change = std::move (change)] (Song& song) {
            if (t >= 0 && t < (int) song.mixer.size() && sl >= 0 && sl < (int) song.mixer[(std::size_t) t].effects.size())
                change (song.mixer[(std::size_t) t].effects[(std::size_t) sl]);
        });
    }

    bool stateBacked() const { return plugui::usesStateParams (kind) && layout != &plugui::soundgoodizerLayout(); }
    bool isStateParam (int param) const { return stateBacked() && plugui::stateIndex (kind, param) >= 0; }
    const stateparams::Table& stateTable() const { return *stateparams::tableFor (kind); }

    float valueOf (int param) const
    {
        const auto* fx = current();
        if (isStateParam (param))
            return fx != nullptr ? plugui::stateValue01 (kind, param, fx->state) : 0.0f;
        return fx != nullptr && param >= 0 && param < maxEffectParams ? fx->params[(std::size_t) param] : 0.0f;
    }

    void setValue (int param, float value)
    {
        if (isStateParam (param))
        {
            const auto* fx = current();
            if (fx == nullptr)
                return;
            const auto s = plugui::stateIndex (kind, param);
            const auto raw = plugui::stateRaw (kind, param, value);
            if (stateTable().get (fx->state, s) == raw)
                return;
            editSlot ([bytes = stateTable().with (fx->state, s, raw)] (EffectSlot& target) { target.state = bytes; });
            return;
        }
        if (current() == nullptr || param < 0 || param >= maxEffectParams)
            return;
        if (kind == EffectKind::echoform) // round 135w: Filter link moves the low and high cut together
            if (const auto edits = echosuite::echoform::linkedEdit (current()->params, param, value); edits.size() > 1)
            {
                setParams (edits);
                return;
            }
        if (kind == EffectKind::glowLeveler) // round 165x: AUTO MAKEUP's reference point and its make-up kept when off
            if (const auto edits = mixsuite::glow::linkedEdit (current()->params, param, value); edits.size() > 1)
            {
                setParams (edits);
                return;
            }
        if (kind == EffectKind::ember) // round 165: the Classic voicing's Auto moves Output with Drive
            if (const auto edits = mixsuite::ember::linkedEdit (current()->params, param, value); edits.size() > 1)
            {
                setParams (edits);
                return;
            }
        if (usesBar())
            bar.willEdit();
        editSlot ([param, v = juce::jlimit (0.0f, 1.0f, value)] (EffectSlot& target) { target.params[(std::size_t) param] = v; });
        // round tp172d: "Advanced" (the Voicing off) swaps this Classic panel for Ember's multiband page
        if (kind == EffectKind::ember && param == (int) mixsuite::ember::voicing && value < 0.5f && track >= 0 && node.locate == nullptr)
            NativeEffectWindows::switchEmberEditor (engine, track, slot, false);
    }

    juce::String valueText (int param, float value) const
    {
        const auto* fx = current();
        if (fx == nullptr || param < 0)
            return {};
        if (isStateParam (param))
            return utf8 (stateTable().text (fx->state, plugui::stateIndex (kind, param), plugui::stateRaw (kind, param, value)).c_str());
        if (isSendTarget (param))
            return flbasic::sendTrack (value) > 0 ? insertName (flbasic::sendTrack (value)) : tr ("None");
        if (kind == EffectKind::hardwareInsert && (param == hwinsert::output || param == hwinsert::input))
            return hwChannelText (param == hwinsert::output, fx->params);
        return utf8 (formatEffectParam (kind, param, value, engine.getSong().bpm, fx->params).c_str());
    }

    // the hardware insert's output / input as the device names it ("Out 3 - Out 4", "In 2"; "None")
    juce::String hwChannelText (bool out, const EffectParams& p) const
    {
        const auto channel = hwinsert::channelOf (p[out ? hwinsert::output : hwinsert::input]);
        if (channel < 0)
            return tr ("None");
        if (out)
        {
            const auto mode = hwinsert::outputModeOf (p);
            const auto names = engine.getOutputChannelNames();
            auto text = juce::String::fromUTF8 (outputTargetName (channel, mode, names, false).c_str());
            return text.replace ("(mono)", tr ("(mono)")).replace ("(missing)", tr ("(missing)"));
        }
        const auto stereo = p[hwinsert::inputStereo] >= 0.5f;
        const auto names = engine.getInputChannelNames();
        return juce::String::fromUTF8 (inputSourceName (channel, stereo, names).c_str());
    }

    // the hardware insert's channel menus: the device's pairs, then every channel alone (as the mixer lists them)
    void showHwChannelMenu (bool out, juce::Rectangle<float> r)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        juce::PopupMenu menu;
        const auto chosen = hwinsert::channelOf (fx->params[out ? hwinsert::output : hwinsert::input]);
        const auto apply = [this] (std::vector<std::pair<int, float>> changes) {
            setParams (changes);
            engine.openHardwareInsertIo(); // (the device opens the channels: no trip to Audio settings)
        };
        if (out)
        {
            const auto mono = fx->params[hwinsert::outputMono] >= 0.5f;
            menu.addItem (tr ("None"), true, chosen < 0, [apply] { apply ({ { hwinsert::output, 0.0f } }); });
            const auto names = engine.getOutputChannelNames();
            if (names.empty())
                menu.addItem (tr ("No audio device is open"), false, false, [] {});
            for (const auto& t : outputTargets (names))
            {
                const auto isMono = t.mode != OutputMode::stereo;
                const auto busy = t.channel < 2 && engine.getSong().mixer[0].outputChannel == outputDefault; // the Master's own pair
                auto name = juce::String::fromUTF8 (t.name.c_str()) + (isMono ? " " + tr ("(mono)") : juce::String());
                if (busy)
                    name += "  " + tr ("(the Master plays here)");
                menu.addItem (name, true, chosen == t.channel && mono == isMono,
                              [apply, t, isMono] { apply ({ { hwinsert::output, hwinsert::channelValue (t.channel) }, { hwinsert::outputMono, isMono ? 1.0f : 0.0f } }); });
            }
        }
        else
        {
            const auto stereo = fx->params[hwinsert::inputStereo] >= 0.5f;
            menu.addItem (tr ("None"), true, chosen < 0, [apply] { apply ({ { hwinsert::input, 0.0f } }); });
            const auto names = engine.getInputChannelNames();
            if (names.empty())
                menu.addItem (tr ("Open the audio input"), true, false, [this] {
                    if (const auto result = engine.openAudioInput(); result.issue != AudioEngine::InputIssue::none)
                        ui::flashValueTip (*this, getLocalBounds().withHeight (40), tr ("No audio input could be opened (Options > Audio settings)"));
                    repaint();
                });
            for (const auto& source : inputSources (names))
                menu.addItem (juce::String::fromUTF8 (source.name.c_str()), true, chosen == source.channel && stereo == source.stereo,
                              [apply, source] { apply ({ { hwinsert::input, hwinsert::channelValue (source.channel) }, { hwinsert::inputStereo, source.stereo ? 1.0f : 0.0f } }); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
    }

    // an insert's name as the mixer shows it
    juce::String insertName (int t) const
    {
        const auto& mixer = engine.getSong().mixer;
        const auto name = t > 0 && t < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) t].name.c_str()) : juce::String();
        return name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (t) });
    }

    juce::String trackName() const
    {
        if (node.locate)
            return node.name;
        const auto& mixer = engine.getSong().mixer;
        if (track == 0)
            return tr ("Master");
        const auto name = track < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str()) : juce::String();
        return name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) });
    }

    ui::ValueSpec specFor (const LayoutItem& item) const
    {
        ui::ValueSpec spec;
        const auto param = paramOf (item);
        const auto& info = effectInfo (kind);

        if (isStateParam (param))
        {
            // not automatable (the values live in the state): drag, wheel, reset and type-in only
            const auto& p = stateTable().params[(std::size_t) plugui::stateIndex (kind, param)];
            spec.name = trackName() + " - " + utf8 (info.name) + " - " + utf8 (p.name.c_str());
            spec.defaultValue = p.maximum > p.minimum ? (double) (p.defaultValue - p.minimum) / (double) (p.maximum - p.minimum) : 0.0;
            spec.pixelsForRange = item.type == Item::hslider ? juce::jmax (60.0, (double) rectOf (item).getWidth())
                                  : item.type == Item::vslider ? juce::jmax (60.0, (double) rectOf (item).getHeight())
                                                               : 180.0;
            if (item.steps > 0)
                spec.interval = 1.0 / item.steps;
            else if (p.maximum - p.minimum <= 16)
                spec.interval = 1.0 / juce::jmax (1, p.maximum - p.minimum);
            spec.toText = [this, param] (double v) { return valueText (param, (float) v); };
            spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
            return spec;
        }

        if (param < 0 || param >= (int) info.params.size())
            return spec;

        spec.name = trackName() + " - " + utf8 (info.name) + " - " + utf8 (info.params[(std::size_t) param].name);
        spec.defaultValue = info.params[(std::size_t) param].defaultValue;
        spec.pixelsForRange = item.type == Item::hslider ? juce::jmax (60.0, (double) rectOf (item).getWidth())
                              : item.type == Item::vslider ? juce::jmax (60.0, (double) rectOf (item).getHeight())
                                                           : 180.0;
        if (item.steps > 0)
            spec.interval = 1.0 / item.steps;
        spec.toText = [this, param] (double v) { return valueText (param, (float) v); };
        spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
        if (! node.locate)
            spec.target = AutomationTarget { AutomationTargetKind::effectParam, track, slot, param };
        return spec;
    }

    // --- painting ------------------------------------------------------------------------------------

    // this editor's style (round 126: one per plugin, alternating; Settings may force one)
    int styleKey() const { return skin::styleKey (kind, layout == &plugui::soundgoodizerLayout()); }
    skin::Style editorStyle() const { return skin::styleFor (kind, layout == &plugui::soundgoodizerLayout()); }

    // round 170j: the art's moving part (Nebula's twinkling stars) lives in the free strip right of the last menu
    juce::Rectangle<float> artStage() const
    {
        const LayoutItem* last = nullptr;
        for (const auto& item : activeLayout().items)
            if (item.type == Item::menu && (last == nullptr || item.y >= last->y))
                last = &item;
        if (last == nullptr)
            return {};
        const auto r = rectOf (*last);
        return { r.getRight() + 16.0f, r.getY() - 4.0f, juce::jmax (0.0f, (float) getWidth() - r.getRight() - 28.0f), r.getHeight() + 8.0f };
    }

    void resized() override
    {
        if (artLayer == nullptr)
            return;
        artLayer->setBounds (getLocalBounds());
        artLayer->setStage (artStage());
        artLayer->toFront (false);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const skin::StyleScope styleScope (editorStyle(), artNow());
        accent = editorStyle() == skin::Style::themed ? skin::artAccent (artNow()) : familyAccent;
        skin::canvas (g, getLocalBounds().toFloat(), accent);
        const auto* fx = current();

        if (fx == nullptr)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::body));
            g.drawText (tr ("The effect was removed from this slot."), getLocalBounds(), juce::Justification::centred);
            return;
        }

        paintHeader (g, *fx);

        const auto& items = activeLayout().items;
        for (int i = 0; i < (int) items.size(); ++i)
            if (visible (items[(std::size_t) i])
                && (artLayer == nullptr || g.clipRegionIntersects (rectOf (items[(std::size_t) i]).expanded (40.0f, 24.0f).getSmallestIntegerContainer())))
            {
                // round 170j: a partial repaint of the art draws only what it touches; round 170c: a control another mode bypasses is drawn dimmed
                const auto idle = plugui::isIdle (kind, items[(std::size_t) i], fx->params);
                if (idle) g.beginTransparencyLayer (0.35f);
                skin::setOnDarkFill (onDarkFill (i)); // round tp172d
                paintItem (g, items[(std::size_t) i], i, *fx);
                skin::setOnDarkFill (false);
                if (idle) g.endTransparencyLayer();
            }
        if (hasHelpLine())
            paintHelpLine (g);
    }

    juce::Rectangle<float> headerArea() const { return { 0.0f, 0.0f, (float) getWidth(), headerHeight }; }
    juce::Rectangle<float> menuButton() const { return { 4.0f, 3.0f, 22.0f, 20.0f }; }
    juce::Rectangle<float> nextButton() const { return { (float) getWidth() - 26.0f, 3.0f, 20.0f, 20.0f }; }
    juce::Rectangle<float> prevButton() const { return nextButton().translated (-22.0f, 0.0f); }
    juce::Rectangle<float> presetBox() const
    {
        // round 170k2: the box gives way to the title (never below 90 px)
        const auto name = headerName();
        const auto room = prevButton().getX() - 4.0f - 6.0f - (menuButton().getRight() + 40.0f) - skin::titleMinWidth (name, headerHeight);
        const auto w = juce::jlimit (90.0f, juce::jmax (90.0f, room), juce::jlimit (90.0f, 220.0f, (float) getWidth() * 0.32f));
        return { prevButton().getX() - w - 4.0f, 3.0f, w, 20.0f };
    }

    void paintHeader (juce::Graphics& g, const EffectSlot& fx)
    {
        using namespace theme;
        const auto area = headerArea();
        skin::headerBar (g, area, accent);

        // options menu (FL's arrow at the left of the plugin's title)
        drawArrow (g, menuButton(), 0.0f, hoverHeader == 1 ? accent : colour::textSecondary);

        // LYNX's mark, bypass LED, name, where it is
        skin::brandMark (g, juce::Rectangle<float> (menuButton().getRight() + 2.0f, 0.0f, 18.0f, area.getHeight()));
        const auto ledC = juce::Point<float> (menuButton().getRight() + 30.0f, area.getCentreY());
        skin::powerLed (g, ledC, 7.0f, fx.enabled);
        const auto name = headerName();
        auto text = area.withLeft (ledC.x + 10.0f).withRight (presetBox().getX() - 6.0f);
        if (usesBar())
            text = bar.place (area, true, ledC.x + 10.0f - area.getX() + skin::titleMinWidth (name, area.getHeight())).withLeft (ledC.x + 10.0f);
        const auto nameWidth = skin::titleText (g, text.withHeight (area.getHeight()), name, accent);
        text.removeFromLeft (nameWidth);
        // round 177b: the "Sidechain from" picker at the right end of the title space (when it fits)
        sidechainChip = {};
        if (showsSidechain())
            if (constexpr float w = 118.0f; text.getWidth() >= w + 14.0f)
            {
                sidechainChip = { text.getRight() - w, 3.0f, w, 20.0f };
                text.setRight (sidechainChip.getX() - 4.0f);
                paintSidechainChip (g, fx);
            }
        g.setFont (font (type::small));
        g.setColour (colour::textSecondary);
        // where it is: only when it fits whole (round 170k2: no "(I…" stub beside the name)
        if (const auto where = "(" + trackName() + ")"; ! plugui::hostedElsewhere && juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), where) <= text.getWidth() - 9.0f)
            g.drawText (where, text.withTrimmedLeft (8.0f), juce::Justification::centredLeft, false);

        if (usesBar())
        {
            bar.paint (g, accent);
            return;
        }

        // presets: name, previous, next
        const auto box = presetBox();
        skin::valuePlate (g, box, radius::md);
        if (hoverHeader == 2)
        {
            g.setColour (accent.withAlpha (0.5f));
            g.drawRoundedRectangle (box.reduced (0.5f), radius::md, 1.0f);
        }
        g.setColour (presetName.isNotEmpty() ? colour::textPrimary : colour::textSecondary);
        ui::drawFittingText (g, presetName.isNotEmpty() ? presetName : tr ("(no preset)"), box.reduced (8.0f, 0.0f), juce::Justification::centredRight,
                             font (type::small, presetName.isNotEmpty()), 8.0f, 0.0f, 0.75f);
        drawArrow (g, prevButton(), juce::MathConstants<float>::pi, hoverHeader == 3 ? accent : colour::textSecondary);
        drawArrow (g, nextButton(), 0.0f, hoverHeader == 4 ? accent : colour::textSecondary);
    }

    // round 177b: a mixer slot's effect with a side input shows its "Sidechain from" picker in the header
    bool showsSidechain() const { return ! node.locate && pluginheader::takesSidechain (engine, track, slot); }
    void paintSidechainChip (juce::Graphics& g, const EffectSlot& fx)
    {
        using namespace theme;
        const auto box = sidechainChip;
        const auto source = pluginheader::sidechainSourceText (engine.getSong(), fx);
        skin::valuePlate (g, box, radius::md);
        if (hoverHeader == 6 || source.isNotEmpty())
        {
            g.setColour (accent.withAlpha (hoverHeader == 6 ? 0.7f : 0.45f));
            g.drawRoundedRectangle (box.reduced (0.5f), radius::md, 1.0f);
        }
        const auto modulator = kind == EffectKind::vocoder || kind == EffectKind::vocodex;
        const auto text = (modulator ? tr ("Modulator") : tr ("SC")) + ": " + (source.isNotEmpty() ? source : tr ("None"));
        g.setColour (source.isNotEmpty() ? colour::textPrimary : colour::textSecondary);
        ui::drawFittingText (g, text, box.reduced (7.0f, 0.0f).withTrimmedRight (9.0f), juce::Justification::centredLeft,
                             font (type::small, source.isNotEmpty()), 8.0f, 0.0f, 0.75f);
        drawArrow (g, { box.getRight() - 14.0f, box.getY(), 10.0f, box.getHeight() }, juce::MathConstants<float>::halfPi,
                   hoverHeader == 6 ? accent : colour::textSecondary);
    }
    juce::Rectangle<float> sidechainChip; // where the header drew it (empty: not shown)

    static void drawArrow (juce::Graphics& g, juce::Rectangle<float> r, float angle, juce::Colour c)
    {
        juce::Path p;
        p.addTriangle (-3.0f, -5.0f, -3.0f, 5.0f, 4.0f, 0.0f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (r.getCentreX(), r.getCentreY()));
        g.setColour (c);
        g.fillPath (p);
    }

    juce::Colour arcColour (const EffectSlot& fx, int colourIndex = -1) const
    {
        if (! fx.enabled)
            return theme::colour::textDisabled;
        return colourIndex >= 0 ? peqview::bandColour (colourIndex) : accent;
    }

    void caption (juce::Graphics& g, const LayoutItem& item, juce::Rectangle<float> r, juce::Justification j = juce::Justification::centred)
    {
        using namespace theme;
        if (item.label == nullptr || *item.label == 0)
            return;
        const auto locked = isMixItem (item) && pluginheader::isMixLocked (kind);
        if (locked) // round 135: the locked MIX caption is boxed in the accent colour
        {
            const auto w = juce::jmin (r.getWidth(), juce::GlyphArrangement::getStringWidth (font (juce::jmax (8.0f, 9.5f * scale()), true), caps (tr (item.label))) + 10.0f);
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (r.withSizeKeepingCentre (w, r.getHeight()), 3.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (r.withSizeKeepingCentre (w, r.getHeight()), 3.0f, 1.0f);
        }
        g.setColour (locked ? theme::colour::textPrimary : hasStyle (item, plugui::dim) ? colour::textDisabled : skin::captionColour());
        // tp172d (look slice): a printed scale word with a unit ("20 Hz", "-24 dB") keeps its case
        const auto text = tr (item.label);
        const auto printedUnit = hasStyle (item, plugui::dim) && text.containsAnyOf ("0123456789");
        ui::drawFittingText (g, skin::lowerCaptions() ? text.toLowerCase() : printedUnit ? text : caps (text), r.expanded (0.0f, (skin::captionScale() - 1.0f) * 6.0f * scale()), j,
                            font (juce::jmax (8.0f, 9.5f * scale() * skin::captionScale()), true), 7.0f); // (round 165: fit, no "..."; tp172d: themed size)
    }

    // round tp172d: does item `index` sit on a top-level section the theme fills dark (the last one under its centre)?
    bool onDarkFill (int index) const
    {
        const auto& items = activeLayout().items;
        const auto& it = items[(std::size_t) index];
        if (it.type == Item::section)
            return false;
        const juce::Point<float> c { it.x + it.w * 0.5f, it.y + it.h * 0.5f };
        int ordinal = 0, under = -1;
        for (int k = 0; k < (int) items.size(); ++k)
        {
            const auto& s = items[(std::size_t) k];
            if (s.type != Item::section || hasStyle (s, plugui::raised) || ! visible (s))
                continue;
            if (juce::Rectangle<float> (s.x, s.y, s.w, s.h).contains (c))
                under = ordinal;
            ++ordinal;
        }
        return skin::sectionFillIsDark (under);
    }

    void paintItem (juce::Graphics& g, const LayoutItem& item, int index, const EffectSlot& fx)
    {
        using namespace theme;
        const auto r = rectOf (item);
        const auto s = scale();
        const auto param = paramOf (item);
        const auto value = valueOf (param);
        const auto hot = index == hoverItem || index == dragItem;
        const auto glow = index == dragItem ? 1.0f : anim.level (index);

        switch (item.type)
        {
            case Item::section:
            {
                {
                    int ordinal = 0; // round tp172d: the section's place among the top-level ones (a theme's own fills)
                    const auto& items = activeLayout().items;
                    for (int k = 0; k < index && k < (int) items.size(); ++k)
                        if (items[(std::size_t) k].type == Item::section && ! hasStyle (items[(std::size_t) k], plugui::raised) && visible (items[(std::size_t) k]))
                            ++ordinal;
                    skin::setSectionOrdinal (hasStyle (item, plugui::raised) ? -1 : ordinal);
                }
                skin::section (g, r, radius::lg * s, hasStyle (item, plugui::raised), fx.enabled ? accent : juce::Colours::transparentBlack);
                skin::setSectionOrdinal (-1);
                if (item.label != nullptr && *item.label != 0 && ! hasStyle (item, plugui::boxed))
                    skin::sectionTitle (g, r.withHeight (16.0f * s).translated (0.0f, 1.0f * s), caps (tr (item.label)), accent, juce::jmax (8.0f, 10.0f * s));
                break;
            }

            case Item::label:
                if (hasStyle (item, plugui::bigCaption) && item.label != nullptr) // round tp172d (slice 8): a hardware strip's big
                {                                                                    // caption, or (big) the model name plate
                    const auto heavy = hasStyle (item, plugui::big);
                    auto f = juce::Font (font (r.getHeight() * (heavy ? 0.92f : 0.62f), heavy));
                    if (heavy)
                    {
                        f.setHorizontalScale (0.82f); // a condensed, stencil-like plate
                        f.setExtraKerningFactor (0.04f);
                        g.setFont (f);
                        g.setColour (juce::Colours::white.withAlpha (0.35f)); // the engraving's lit lower edge
                        g.drawText (caps (tr (item.label)), r.translated (0.0f, 1.0f * s), juce::Justification::centred, false);
                        g.setColour (juce::Colour (0xff2c2d2f).withAlpha (fx.enabled ? 1.0f : 0.6f));
                        g.drawText (caps (tr (item.label)), r, juce::Justification::centred, false);
                        break;
                    }
                    g.setColour (skin::captionColour().withAlpha (fx.enabled ? 0.92f : 0.5f));
                    ui::drawFittingText (g, caps (tr (item.label)), r, juce::Justification::centredRight, f, 7.0f);
                    break;
                }
                if (hasStyle (item, plugui::wordmark) && item.label != nullptr) // round tp172d: a wide-spaced title in two tones
                {
                    const auto text = juce::String (item.label);
                    const auto first = text.upToFirstOccurrenceOf ("|", false, false), second = text.fromFirstOccurrenceOf ("|", false, false);
                    auto f = juce::Font (font (r.getHeight() * 0.78f, false));
                    f.setExtraKerningFactor (0.55f);
                    const auto gap = r.getHeight() * 0.35f;
                    const auto w1 = juce::GlyphArrangement::getStringWidth (f, first), w2 = juce::GlyphArrangement::getStringWidth (f, second);
                    auto x = r.getCentreX() - (w1 + gap + w2) * 0.5f;
                    g.setFont (f);
                    g.setColour (skin::captionColour().withAlpha (0.42f));
                    g.drawText (first, juce::Rectangle<float> (x, r.getY(), w1 + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
                    g.setColour (skin::captionColour());
                    g.drawText (second, juce::Rectangle<float> (x + w1 + gap, r.getY(), w2 + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
                    break;
                }
                if (hasStyle (item, plugui::dim) && ! hasStyle (item, plugui::boxed) && item.h > 30.0f && item.label != nullptr)
                {
                    // the plugin's name, big and readable (FL's logos are not reproduced; round 168q2)
                    g.setColour (skin::captionScale() > 1.0f ? skin::captionColour() : colour::textSecondary); // (tp172d: a themed panel's type)
                    if (skin::titleRules()) // round tp172d (look slice): the name centred between two rules (a hardware title bar)
                    {
                        juce::Font f (font (r.getHeight() * 0.62f, true));
                        f.setExtraKerningFactor (0.12f);
                        const auto tw = juce::jmin (r.getWidth() * 0.7f, juce::GlyphArrangement::getStringWidth (f, item.label));
                        g.setFont (f);
                        g.drawText (item.label, r, juce::Justification::centred, false);
                        const auto gap = 8.0f * s, y = r.getCentreY() - 1.0f * s;
                        g.fillRect (juce::Rectangle<float> (r.getX(), y, juce::jmax (0.0f, (r.getWidth() - tw) * 0.5f - gap), 2.0f * s));
                        g.fillRect (juce::Rectangle<float> (r.getCentreX() + tw * 0.5f + gap, y, juce::jmax (0.0f, (r.getWidth() - tw) * 0.5f - gap), 2.0f * s));
                        break;
                    }
                    ui::drawFittingText (g, item.label, r, hasStyle (item, plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centredRight,
                                         font (r.getHeight() * 0.62f, true), r.getHeight() * 0.25f, 0.0f, 0.8f); // (smaller, never "Wav...")
                    break;
                }
                if (hasStyle (item, plugui::boxed))
                    skin::section (g, r, radius::sm * s, true);
                caption (g, item, r, hasStyle (item, plugui::captionLeft | plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centred);
                break;

            case Item::value:
                if (hasStyle (item, plugui::boxed) && skin::ledReadouts()) // round tp172d: a red LED read-out
                {
                    ledReadout (g, r, valueText (param, value), s, fx.enabled);
                    break;
                }
                if (hasStyle (item, plugui::boxed)) // a read-out plate (FL's value boxes)
                    skin::valuePlate (g, r, radius::sm * s);
                g.setColour (hot ? accent : (fx.enabled ? (skin::captionScale() > 1.0f ? skin::valueColour (false) : colour::textPrimary) : colour::textDisabled));
                g.setFont (hasStyle (item, plugui::big) ? font (juce::jmax (12.0f, 19.0f * s)) // round tp172b4: a big read-out (LuxeVerb's Space time)
                                                       : font (juce::jmax (8.0f, 11.0f * s * juce::jmin (1.25f, skin::captionScale())), skin::captionScale() > 1.0f));
                g.drawFittedText (valueText (param, value), r.toNearestInt(),
                                  hasStyle (item, plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.7f);
                break;

            case Item::knob:
            {
                const auto circle = knobCircle (item);
                skin::KnobLook look;
                look.value = value;
                look.bipolar = hasStyle (item, plugui::bipolar);
                look.accent = item.colour >= 0 ? peqview::bandColour (item.colour) : accent;
                look.hover = glow;
                look.active = index == dragItem;
                look.enabled = fx.enabled;
                look.capTint = skin::capTintFor (item.param);
                skin::knob (g, circle, look);
                auto below = r.withTop (circle.getBottom() + 1.0f * s);
                if (hasStyle (item, plugui::valueAbove))
                {
                    const auto box = r.withHeight (13.0f * s).reduced (juce::jmax (0.0f, (r.getWidth() - 40.0f * s) * 0.5f), 0.0f);
                    skin::valuePlate (g, box, radius::sm);
                    g.setColour (hot ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
                    g.drawFittedText (valueText (param, value), box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                }
                if (hasStyle (item, plugui::captionAbove))
                {
                    caption (g, item, r.withHeight (13.0f * s).expanded (10.0f * s, 0.0f));
                    if (! hasStyle (item, plugui::showValue)) // round tp172d: the value only while dragged (its tip), as PanMan does
                        break;
                    g.setColour (hot ? look.accent.brighter (0.3f) : skin::valueColour (false));
                    g.setFont (font (juce::jmax (8.0f, 10.5f * s * skin::captionScale()), skin::captionScale() > 1.0f));
                    g.drawFittedText (valueText (param, value), below.withHeight (13.0f * s * skin::captionScale()).expanded (10.0f * s, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
                }
                else if (hasStyle (item, plugui::hoverValue)) // round tp172b5: the caption, the value under it on hover / drag
                {
                    caption (g, item, below.withHeight (12.0f * s));
                    if (hot || index == dragItem)
                    {
                        g.setColour (hot ? look.accent.brighter (0.3f) : skin::valueColour (true));
                        g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                        g.drawFittedText (valueText (param, value), below.withTrimmedTop (12.0f * s).withHeight (13.0f * s).expanded (12.0f * s, 0.0f).toNearestInt(),
                                          juce::Justification::centred, 1, 0.7f);
                    }
                }
                else if (! hasStyle (item, plugui::noCaption))
                {
                    caption (g, item, below.withHeight (12.0f * s));
                    if (hasStyle (item, plugui::showValue))
                    {
                        g.setColour (hot ? look.accent.brighter (0.3f) : skin::valueColour (false));
                        g.setFont (font (juce::jmax (8.0f, 10.5f * s * skin::captionScale()), skin::captionScale() > 1.0f));
                        g.drawFittedText (valueText (param, value), below.withTrimmedTop (12.0f * s).withHeight (13.0f * s * skin::captionScale()).toNearestInt(),
                                          juce::Justification::centred, 1, 0.7f);
                    }
                }
                break;
            }

            case Item::hslider:
            case Item::vslider:
            {
                const auto vertical = item.type == Item::vslider;
                auto area = r;
                if (item.label != nullptr && ! hasStyle (item, plugui::noCaption))
                {
                    if (hasStyle (item, plugui::captionLeft))
                        caption (g, item, area.removeFromLeft (area.getWidth() * 0.4f), juce::Justification::centredLeft);
                    else if (vertical)
                        caption (g, item, area.removeFromBottom (13.0f * s).expanded (18.0f * s, 0.0f));
                    else
                        caption (g, item, area.removeFromBottom (12.0f * s));
                }
                const auto thick = juce::jmax (3.0f, 4.0f * s);
                const auto trackRect = vertical ? area.withSizeKeepingCentre (thick, area.getHeight() - 10.0f * s)
                                                : area.withSizeKeepingCentre (area.getWidth() - 8.0f * s, thick);
                skin::slider (g, trackRect, value, vertical, hasStyle (item, plugui::bipolar), item.colour >= 0 ? peqview::bandColour (item.colour) : accent,
                              hot, fx.enabled, vertical ? juce::jmin (area.getWidth(), 20.0f * s) : juce::jmin (area.getHeight(), 16.0f * s), 9.0f * s);
                break;
            }

            case Item::radio:
            {
                const auto labels = plugui::optionLabels (item);
                const auto chosen = plugui::optionAt (item, value);
                const auto texts = optionTexts (item);
                if (item.columns > 0) // a grid of named cells (Gross Beat's slots)
                {
                    const auto cellAccent = item.colour >= 0 ? colour::info : accent;
                    g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
                    for (int o = 0; o < (int) texts.size(); ++o)
                    {
                        const auto cell = gridCell (item, o, (int) texts.size()).reduced (0.5f);
                        g.setColour (o == chosen ? cellAccent.withAlpha (0.22f) : ((o / item.columns) % 2 == 0 ? colour::bg0 : colour::bg1));
                        g.fillRect (cell);
                        if (o == chosen)
                        {
                            g.setColour (fx.enabled ? cellAccent : colour::textSecondary);
                            g.drawRect (cell, 1.0f);
                        }
                        g.setColour (o == chosen ? colour::textPrimary : colour::textSecondary);
                        if (! drawWaveGlyph (g, cell.reduced (cell.getWidth() * 0.25f, cell.getHeight() * 0.28f), juce::String::fromUTF8 (labels[(std::size_t) o].c_str())))
                            g.drawFittedText (texts[(std::size_t) o], cell.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
                    }
                    break;
                }
                const auto rowH = r.getHeight() / (float) juce::jmax (1, (int) labels.size());
                if (skin::hwKeys()) // round tp172d: a column of square keys, each with its caption (the originals' mode keys)
                {
                    for (int o = 0; o < (int) labels.size(); ++o)
                    {
                        const auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) o, r.getWidth(), rowH);
                        const auto k = juce::jmin (rowH * 0.7f, 24.0f * s);
                        skin::hwKey (g, juce::Rectangle<float> (k, k).withCentre ({ row.getX() + k * 0.5f + 2.0f * s, row.getCentreY() }), o == chosen && fx.enabled, hot);
                        g.setColour (skin::captionColour());
                        ui::drawFittingText (g, caps (texts[(std::size_t) o]), row.withTrimmedLeft (k + 10.0f * s), juce::Justification::centredLeft,
                                             font (juce::jmax (8.0f, 11.0f * s * skin::captionScale()), true), 7.0f);
                    }
                    break;
                }
                for (int o = 0; o < (int) labels.size(); ++o)
                {
                    const auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) o, r.getWidth(), rowH);
                    const auto dot = juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ row.getX() + 5.0f * s, row.getCentreY() });
                    if (o == chosen && fx.enabled)
                    {
                        g.setColour (accent.withAlpha (0.3f));
                        g.fillEllipse (dot.expanded (2.5f * s));
                    }
                    g.setColour (o == chosen ? (fx.enabled ? accent : colour::textSecondary) : juce::Colour (0xff0b0c0f));
                    g.fillEllipse (dot);
                    if (o != chosen)
                    {
                        g.setColour (colour::borderStrong);
                        g.drawEllipse (dot, 1.0f);
                    }
                    g.setColour (o == chosen ? colour::textPrimary : (hot ? colour::textSecondary : colour::textDisabled));
                    g.setFont (font (juce::jmax (8.0f, 9.5f * s)));
                    g.drawFittedText (texts[(std::size_t) o], row.withTrimmedLeft (12.0f * s).toNearestInt(),
                                      juce::Justification::centredLeft, 1, 0.7f);
                }
                break;
            }

            case Item::tabs:
            {
                const auto labels = plugui::optionLabels (item);
                const auto texts = optionTexts (item);
                const auto chosen = hasStyle (item, plugui::pageTabs) ? (item.steps > 0 ? (page / item.steps) % juce::jmax (1, (int) labels.size()) : page)
                                                                      : plugui::optionAt (item, value);
                const auto vertical = isVertical (item);
                const auto count = (float) juce::jmax (1, (int) labels.size());
                const auto w = vertical ? r.getWidth() : r.getWidth() / count;
                const auto h = vertical ? r.getHeight() / count : r.getHeight();
                if (hasStyle (item, plugui::batToggle) && texts.size() >= 2) // round tp172d (slice 8): COMPRESS / LIMIT — a bat toggle,
                {                                                             // option 0 above (lever up), option 1 under; the chosen word lit
                    const auto line = juce::jmin (13.0f * s, r.getHeight() * 0.2f);
                    const auto f = font (juce::jmax (8.0f, 10.5f * s * skin::captionScale()), true);
                    for (int o = 0; o < 2; ++o)
                    {
                        const auto band = o == 0 ? r.withHeight (line) : r.withTop (r.getBottom() - line);
                        g.setColour (o == chosen && fx.enabled ? accent : skin::captionColour());
                        ui::drawFittingText (g, caps (texts[(std::size_t) o]), band.expanded (14.0f * s, 0.0f), juce::Justification::centred, f, 7.0f);
                    }
                    const auto body = r.withTrimmedTop (line).withTrimmedBottom (line);
                    skin::batToggle (g, juce::Rectangle<float> (body.getHeight() * 0.36f, body.getHeight() * 0.36f).withCentre (body.getCentre()), chosen == 0, hot, fx.enabled);
                    break;
                }
                if (hasStyle (item, plugui::rotarySelector) && ! texts.empty()) // round tp172d (slice 8): VU DISPLAY — a black pointer
                {                                                                  // knob, its options printed round it, the chosen one lit
                    const auto line = 12.0f * s;
                    const auto knobArea = selectorKnobArea (item);
                    const auto f = font (juce::jmax (8.0f, 10.0f * s * skin::captionScale()), true);
                    const auto n = (int) texts.size();
                    for (int o = 0; o < n; ++o)
                    {
                        const auto a = selectorAngle (o, n);
                        const auto q = knobArea.getCentre() + juce::Point<float> { std::sin (a), -std::cos (a) } * (knobArea.getWidth() * 0.5f + 9.0f * s);
                        auto band = juce::Rectangle<float> (r.getWidth() * 0.5f, line).withCentre (q);
                        auto j = juce::Justification::centred;
                        if (std::sin (a) < -0.3f) { band = band.withRightX (q.x + 8.0f * s); j = juce::Justification::centredRight; }
                        if (std::sin (a) > 0.3f) { band = band.withX (q.x - 8.0f * s); j = juce::Justification::centredLeft; }
                        band = band.translated (0.0f, std::abs (std::sin (a)) > 0.3f ? -line * 0.9f : -2.0f * s);
                        g.setColour (o == chosen && fx.enabled ? accent : skin::captionColour());
                        ui::drawFittingText (g, caps (texts[(std::size_t) o]), band, j, f, 7.0f);
                    }
                    skin::selectorKnob (g, knobArea, selectorAngle (juce::jmax (0, chosen), n), hot, fx.enabled);
                    break;
                }
                if (skin::goldKeys() && ! vertical) // round tp172d (slice 8): the opto leveler's strip — gold keys side by side
                {
                    for (int o = 0; o < (int) labels.size(); ++o)
                    {
                        const auto cell = juce::Rectangle<float> (r.getX() + w * (float) o, r.getY(), w, h).reduced (1.5f * s, 0.0f);
                        const auto lit = o == chosen && fx.enabled;
                        skin::goldKey (g, cell, lit, hot);
                        g.setColour (juce::Colour (0xff23211a));
                        g.setFont (font (juce::jmax (7.0f, juce::jmin (h * 0.58f, 11.0f * s)), false));
                        g.drawFittedText (texts[(std::size_t) o].toUpperCase() == texts[(std::size_t) o] || texts[(std::size_t) o].containsAnyOf ("0123456789") ? texts[(std::size_t) o] : caps (texts[(std::size_t) o]),
                                          cell.toNearestInt(), juce::Justification::centred, 1, 0.7f); // ("50Hz" as printed, "OFF" in caps)
                    }
                    break;
                }
                if (skin::hwKeys() && ! vertical) // round tp172d: a row of square keys joined by a line (or wide keys for words)
                {
                    bool letters = true; // A / E / N / T / P: square keys; words: wide keys
                    for (const auto& t : texts) letters = letters && t.length() <= 2;
                    // round tp172d (look slice): PanMan's direction keys — "<", "<  >", ">" draw as vector arrows on
                    // square amber-lit keys, side by side (no joining line); a short word among them ("8") shares the style
                    bool arrows = false;
                    for (const auto& t : labels) arrows = arrows || t == "<  >";
                    if (arrows)
                    {
                        const auto amber = juce::Colour (0xffe8a632);
                        const auto side = juce::jmin (w - 6.0f * s, h);
                        for (int o = 0; o < (int) labels.size(); ++o)
                        {
                            const auto cell = juce::Rectangle<float> (side, side).withCentre ({ r.getX() + w * ((float) o + 0.5f), r.getCentreY() });
                            const auto lit = o == chosen && fx.enabled;
                            skin::hwKey (g, cell, lit, hot, amber);
                            const juce::String raw (labels[(std::size_t) o]);
                            const auto ink = lit ? juce::Colour (0xff2a1a04) : juce::Colours::white.withAlpha (0.9f);
                            const auto tri = side * 0.22f;
                            auto arrow = [&] (juce::Point<float> c, bool left)
                            {
                                juce::Path p;
                                const auto d = left ? -1.0f : 1.0f;
                                p.addTriangle (c.x + d * tri, c.y, c.x - d * tri * 0.75f, c.y - tri, c.x - d * tri * 0.75f, c.y + tri);
                                g.fillPath (p);
                            };
                            g.setColour (ink);
                            const auto c = cell.getCentre();
                            if (raw == "<")
                                arrow (c, true);
                            else if (raw == ">")
                                arrow (c, false);
                            else if (raw == "<  >")
                            {
                                arrow (c.translated (-tri * 0.95f, 0.0f), true);
                                arrow (c.translated (tri * 0.95f, 0.0f), false);
                            }
                            else
                            {
                                g.setFont (font (juce::jmax (8.0f, side * 0.42f), true));
                                g.drawText (caps (texts[(std::size_t) o]), cell.toNearestInt(), juce::Justification::centred, false);
                            }
                        }
                        break;
                    }
                    const auto key = juce::jmin (w * 0.62f, h);
                    if (letters)
                    {
                        g.setColour (skin::captionColour().withAlpha (0.8f));
                        g.fillRect (juce::Rectangle<float> (r.getX() + w * 0.5f, r.getCentreY() - 1.0f * s, w * (count - 1.0f), 2.0f * s));
                    }
                    for (int o = 0; o < (int) labels.size(); ++o)
                    {
                        const auto centre = juce::Point<float> (r.getX() + w * ((float) o + 0.5f), r.getCentreY());
                        const auto cell = letters ? juce::Rectangle<float> (key, key).withCentre (centre)
                                                  : juce::Rectangle<float> (w - 4.0f * s, h).withCentre (centre);
                        skin::hwKey (g, cell, o == chosen && fx.enabled, hot);
                        if (hasStyle (item, plugui::compactKeys)) // round tp172d (slice 8): LYNX's extras on a hardware face — small,
                        {                                          // muted until hovered, the chosen key lit
                            if (o != chosen && ! hot)
                            {
                                g.setColour (juce::Colour (0xffc8cbce).withAlpha (0.55f)); // (a veil of the light faceplate it sits on)
                                g.fillRoundedRectangle (cell, 2.0f * s);
                            }
                            g.setColour (o == chosen && fx.enabled ? juce::Colours::white : juce::Colours::white.withAlpha (hot ? 0.95f : 0.75f));
                            g.setFont (font (juce::jmax (7.0f, juce::jmin (h * 0.58f, 8.5f * s)), true));
                            g.drawFittedText (caps (texts[(std::size_t) o]), cell.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                            continue;
                        }
                        g.setColour (juce::Colours::black.withAlpha (0.5f));
                        g.setFont (font (letters ? juce::jmax (8.0f, key * 0.46f) : juce::jmax (7.0f, juce::jmin (h * 0.5f, 10.0f * s)), true));
                        g.drawText (caps (texts[(std::size_t) o]), cell.translated (0.0f, 1.0f).toNearestInt(), juce::Justification::centred, false);
                        g.setColour (juce::Colours::white);
                        g.drawText (caps (texts[(std::size_t) o]), cell.toNearestInt(), juce::Justification::centred, false);
                    }
                    break;
                }
                skin::valuePlate (g, r, radius::md);
                for (int o = 0; o < (int) labels.size(); ++o)
                {
                    const auto cell = (vertical ? juce::Rectangle<float> (r.getX(), r.getY() + h * (float) o, w, h)
                                                : juce::Rectangle<float> (r.getX() + w * (float) o, r.getY(), w, h)).reduced (1.0f);
                    if (vertical) // a band selector (Maximus): the band's colour on the left edge
                    {
                        g.setColour (peqview::bandColour (o).withAlpha (o == chosen ? 1.0f : 0.45f));
                        g.fillRect (cell.withWidth (juce::jmax (2.0f, 3.0f * s)));
                    }
                    if (o == chosen)
                        skin::button (g, vertical ? cell.withTrimmedLeft (juce::jmax (2.0f, 3.0f * s)) : cell, true, false,
                                      vertical ? peqview::bandColour (o) : (fx.enabled ? accent : colour::textSecondary), radius::sm);
                    g.setColour (o == chosen ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                    g.drawFittedText (caps (texts[(std::size_t) o]), cell.toNearestInt(),
                                      juce::Justification::centred, 1, 0.7f);
                }
                break;
            }

            case Item::toggle:
            {
                const auto on = (value >= 0.5f) != hasStyle (item, plugui::inverted) && ! hasStyle (item, plugui::momentary);
                if (hasStyle (item, plugui::batToggle)) // round tp172d (slice 8): AUTO MAKEUP — a bat toggle, the caption above
                {                                        // (lit while on, lever up), OFF under it (lit while off)
                    const auto f = font (juce::jmax (8.0f, 10.5f * s * skin::captionScale()), true);
                    const auto line = juce::jmin (13.0f * s, r.getHeight() * 0.16f);
                    const auto top = r.withHeight (line * 2.0f);
                    g.setColour (on && fx.enabled ? accent : skin::captionColour());
                    if (item.label != nullptr)
                    {
                        g.setFont (f);
                        g.drawFittedText (caps (tr (item.label)), top.expanded (14.0f * s, 0.0f).toNearestInt(), juce::Justification::centredBottom, 2, 0.8f);
                    }
                    g.setColour (! on && fx.enabled ? accent : skin::captionColour());
                    ui::drawFittingText (g, caps (tr ("Off")), r.withTop (r.getBottom() - line), juce::Justification::centred, f, 7.0f);
                    const auto body = r.withTrimmedTop (line * 2.0f).withTrimmedBottom (line);
                    skin::batToggle (g, juce::Rectangle<float> (body.getHeight() * 0.38f, body.getHeight() * 0.38f).withCentre (body.getCentre()), on, hot, fx.enabled);
                    break;
                }
                if (hasStyle (item, plugui::boxed) || hasStyle (item, plugui::momentary)) // a small lit button with its letter (FL's T / L buttons)
                {
                    if (on)
                    {
                        const auto c = fx.enabled ? accent : colour::textSecondary;
                        g.setColour (c.withAlpha (0.3f));
                        g.fillRoundedRectangle (r.expanded (1.5f), radius::sm + 1.5f);
                        g.setGradientFill (juce::ColourGradient (c.brighter (0.2f), 0.0f, r.getY(), c.darker (0.15f), 0.0f, r.getBottom(), false));
                        g.fillRoundedRectangle (r, radius::sm);
                    }
                    else
                        skin::button (g, r, false, hot, accent, radius::sm);
                    g.setColour (on ? colour::onAccent : colour::textSecondary);
                    if (item.label != nullptr)
                        ui::drawFittingText (g, caps (tr (item.label)), r, juce::Justification::centred, font (juce::jmax (8.0f, r.getHeight() * 0.8f), true), 6.5f);
                    break;
                }
                if (skin::hwKeys() && hasStyle (item, plugui::captionAbove)) // round tp172d (look slice): Little AlterBoy's LINK —
                {                                                               // a small square key joined to its neighbours by a line, the caption under it
                    const auto k = juce::jmin (r.getHeight() * 0.5f, 22.0f * s);
                    const auto key = juce::Rectangle<float> (k, k).withCentre ({ r.getCentreX(), r.getY() + k * 0.5f + 2.0f * s });
                    g.setColour (juce::Colours::black.withAlpha (0.75f));
                    g.fillRect (juce::Rectangle<float> (r.getX(), key.getCentreY() - 1.0f * s, r.getWidth(), 2.0f * s));
                    skin::hwKey (g, key, on && fx.enabled, hot);
                    g.setColour (skin::captionColour().getAlpha() > 0 ? skin::captionColour() : colour::textPrimary);
                    if (item.label != nullptr)
                        ui::drawFittingText (g, caps (tr (item.label)), r.withTop (key.getBottom() + 3.0f * s), juce::Justification::centredTop,
                                             font (juce::jmax (8.0f, 10.0f * s * skin::captionScale()), true), 7.0f);
                    break;
                }
                if (skin::hwKeys()) // round tp172d: a square key (lit when on), its caption beside it
                {
                    const auto k = juce::jmin (r.getHeight(), 26.0f * s);
                    const auto key = juce::Rectangle<float> (k * 1.3f, k).withCentre ({ r.getX() + k * 0.65f, r.getCentreY() });
                    skin::hwKey (g, key, on && fx.enabled, hot);
                    g.setColour (on ? juce::Colours::white : juce::Colours::white.withAlpha (0.7f));
                    g.setFont (font (juce::jmax (7.0f, k * 0.4f), true));
                    g.drawFittedText (on ? tr ("ON") : tr ("OFF"), key.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                    g.setColour (skin::captionColour().getAlpha() > 0 ? skin::captionColour() : colour::textPrimary);
                    if (item.label != nullptr)
                        ui::drawFittingText (g, caps (tr (item.label)), r.withTrimmedLeft (k * 1.3f + 6.0f * s), juce::Justification::centredLeft,
                                             font (juce::jmax (8.0f, 10.5f * s * skin::captionScale()), true), 7.0f);
                    break;
                }
                const auto box = juce::Rectangle<float> (16.0f * s, 9.0f * s).withCentre ({ r.getX() + 9.0f * s, r.getCentreY() });
                skin::switchPill (g, box, on, accent, fx.enabled);
                auto text = r.withTrimmedLeft (21.0f * s);
                const auto offLabel = skin::offLabelColour(); // round tp172c: a theme's own (readable on its panels)
                g.setColour (on ? (offLabel.isTransparent() || offLabel.getPerceivedBrightness() > 0.5f ? colour::textPrimary
                                                                                                          : offLabel.darker (0.6f)) // a light panel: lit = darker
                                 : (! offLabel.isTransparent() ? (hot ? offLabel.brighter (0.3f) : offLabel)
                                                                    : (hot ? colour::textSecondary : colour::textDisabled)));
                if (item.label != nullptr)
                    ui::drawFittingText (g, caps (tr (item.label)), text, juce::Justification::centredLeft, font (juce::jmax (8.0f, 9.5f * s), true), 7.0f);
                break;
            }

            case Item::stepper:
            {
                auto box = r;
                if (item.label != nullptr && ! hasStyle (item, plugui::noCaption))
                    caption (g, item, box.removeFromTop (12.0f * s));
                skin::valuePlate (g, box, radius::sm);
                if (hot)
                {
                    g.setColour (accent.withAlpha (0.5f));
                    g.drawRoundedRectangle (box.reduced (0.5f), radius::sm, 1.0f);
                }
                auto arrows = box.removeFromRight (juce::jmin (12.0f * s, box.getWidth() * 0.4f));
                g.setColour (colour::textSecondary);
                drawSmallArrow (g, arrows.removeFromTop (arrows.getHeight() * 0.5f), true);
                drawSmallArrow (g, arrows, false);
                g.setColour (colour::textPrimary);
                g.setFont (font (juce::jmax (8.0f, 11.0f * s), true));
                // (Love Philter's type box: FL's short names; the hint and the list keep the long ones)
                g.drawFittedText (kind == EffectKind::lovePhilter && juce::String (item.param).endsWith (" type")
                                      ? juce::String (philter::typeShortName (juce::roundToInt (value * 11.0f)))
                                      : valueText (param, value),
                                  box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                break;
            }

            case Item::display:
                paintDisplay (g, item, r, fx);
                break;

            case Item::menu:
            {
                if (kind == EffectKind::flTransporter && param == transporter::sendParam) // the loops' send (round 167n)
                {
                    const auto target = flbasic::sendTrack (value);
                    g.setColour ((target > 0 ? accent : colour::accentMuted).withAlpha (hot ? 1.0f : 0.85f));
                    g.fillRoundedRectangle (r, radius::sm * s);
                    g.setColour (target > 0 ? colour::onAccent : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                    g.drawFittedText (target > 0 ? tr ("Send to {0}", { juce::String (target) + "  " + insertName (target) })
                                                 : tr ("Send to") + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\xa6")),
                                      r.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
                    break;
                }
                if (kind == EffectKind::fruitySend) // the destination insert (FL's tinted box; "…" = none)
                {
                    const auto target = flbasic::sendTrack (value);
                    g.setColour ((target > 0 ? accent : colour::accentMuted).withAlpha (hot ? 1.0f : 0.85f));
                    g.fillRoundedRectangle (r, radius::sm * s);
                    g.setColour (target > 0 ? colour::onAccent : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
                    g.drawFittedText (target > 0 ? insertName (target) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")),
                                      r.reduced (2.0f * s, 0.0f).toNearestInt(), juce::Justification::centred, 2, 0.6f);
                    break;
                }
                const auto chosen = plugui::optionAt (item, value);
                if (hasStyle (item, plugui::plainMenu)) // round tp172d: the value as plain text (a click opens the menu)
                {
                    const auto labels = plugui::optionLabels (item);
                    auto t = chosen < (int) labels.size() ? tr (labels[(std::size_t) chosen]) : valueText (param, value);
                    if (skin::lowerCaptions())
                        t = t.toLowerCase();
                    g.setColour (skin::captionColour().withAlpha (hot ? 0.85f : 0.55f));
                    g.setFont (font (juce::jmax (9.0f, r.getHeight() * 0.62f), false));
                    g.drawFittedText (t, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
                    if (hot)
                        g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - 2.0f * s, juce::jmin (r.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t)), 1.0f * s));
                    break;
                }
                if (skin::ledReadouts() && hasStepArrows (item)) // round tp172d: EchoBoy's style display — a red LED window, step arrows left
                {
                    const auto labels = plugui::optionLabels (item);
                    const auto arrows = r.withWidth (14.0f * s);
                    for (const auto up : { true, false })
                    {
                        const auto half = up ? arrows.withTrimmedBottom (arrows.getHeight() * 0.5f) : arrows.withTrimmedTop (arrows.getHeight() * 0.5f);
                        skin::hwKey (g, half.reduced (1.0f * s), false, half.contains (mouseAt));
                        g.setColour (juce::Colours::white.withAlpha (0.85f));
                        drawSmallArrow (g, half.reduced (3.0f * s), up);
                    }
                    ledReadout (g, r.withTrimmedLeft (16.0f * s), chosen < (int) labels.size() ? tr (juce::String::fromUTF8 (labels[(std::size_t) chosen].c_str())) : valueText (param, value),
                                s, fx.enabled, juce::Justification::centredLeft);
                    break;
                }
                const auto off = chosen == 0 && kind == EffectKind::parametricEq;
                if (item.colour >= 0)
                {
                    const auto c = peqview::bandColour (item.colour);
                    g.setColour ((fx.enabled && ! off ? c : c.withSaturation (0.0f).darker (0.6f)).withAlpha (hot ? 1.0f : 0.85f));
                    g.fillRoundedRectangle (r, radius::sm * s);
                }
                else
                    skin::selector (g, r, hot, radius::md * s);
                g.setColour (colour::bg0);
                if (kind == EffectKind::parametricEq)
                    drawBandShape (g, r.reduced (2.0f * s, 4.0f * s), chosen);
                else
                {
                    // round 114: readable mode names (light on the control, bigger), arrows to step on wide menus
                    const auto labels = plugui::optionLabels (item);
                    auto textArea = r;
                    if (hasStepArrows (item))
                    {
                        const auto edge = 18.0f * s;
                        for (const auto left : { true, false })
                        {
                            const auto a = left ? r.withWidth (edge) : r.withLeft (r.getRight() - edge);
                            const auto c0 = a.getCentre();
                            juce::Path tri;
                            const auto h = 4.0f * s;
                            if (left) tri.addTriangle (c0.x + h * 0.7f, c0.y - h, c0.x + h * 0.7f, c0.y + h, c0.x - h * 0.7f, c0.y);
                            else tri.addTriangle (c0.x - h * 0.7f, c0.y - h, c0.x - h * 0.7f, c0.y + h, c0.x + h * 0.7f, c0.y);
                            g.setColour (a.contains (mouseAt) ? colour::textPrimary : colour::textSecondary);
                            g.fillPath (tri);
                        }
                        textArea = r.reduced (edge, 0.0f);
                    }
                    g.setColour (colour::textPrimary);
                    g.setFont (font (juce::jmax (9.0f, juce::jmin (12.0f * s, r.getHeight() * 0.55f)), true));
                    if (kind == EffectKind::lynxTune && param == lynxtune::scale && fx.params[lynxtune::moreScales] >= 0.25f)
                        g.drawFittedText (tr (fx.params[lynxtune::moreScales] >= 0.75f ? "Diminished" : "Whole tone"), textArea.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                    else if (kind == EffectKind::hardwareInsert) // the device's channel names (round 164d)
                        g.drawFittedText (valueText (param, value), textArea.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
                    else if (chosen < (int) labels.size())
                        g.drawFittedText (tr (juce::String::fromUTF8 (labels[(std::size_t) chosen].c_str())), textArea.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                }
                break;
            }
        }
    }

    // round tp172d (slice 8): a rotary selector's knob (the lower part of its box, under the top option's word) and the
    // angle of option k of n (spread over ±75 degrees round the top)
    juce::Rectangle<float> selectorKnobArea (const LayoutItem& item) const
    {
        const auto r = rectOf (item);
        const auto d = juce::jmin (r.getWidth() * 0.44f, r.getHeight() - 26.0f * scale());
        return juce::Rectangle<float> (d, d).withCentre ({ r.getCentreX(), r.getBottom() - d * 0.5f - 2.0f * scale() });
    }
    static float selectorAngle (int k, int n)
    {
        if (n <= 1) return 0.0f;
        const auto span = juce::degreesToRadians (150.0f);
        return -span * 0.5f + span * (float) k / (float) (n - 1);
    }

    // round tp172d (look slice): a red LED read-out (EchoBoy's time, tempo and style displays): a black window, a faint
    // ghost of unlit segments, the text in lit red
    static void ledReadout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float s, bool enabled, juce::Justification j = juce::Justification::centred)
    {
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (r, 2.0f * s);
        g.setColour (juce::Colour (0xff2a0a08));
        g.fillRoundedRectangle (r.reduced (2.0f * s), 1.5f * s);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f * s, 1.0f);
        juce::Font f (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), juce::jmax (8.0f, r.getHeight() * 0.62f), juce::Font::bold));
        g.setFont (f);
        const auto area = r.reduced (5.0f * s, 0.0f).toNearestInt();
        g.setColour (juce::Colour (0xffff3020).withAlpha (enabled ? 1.0f : 0.45f));
        g.drawFittedText (text, area, j, 1, 0.6f);
    }

    // a small drawing of an EQ band's response (peak, shelves, cuts, band-pass, notch)
    static void drawBandShape (juce::Graphics& g, juce::Rectangle<float> r, int type)
    {
        juce::Path p;
        const auto x0 = r.getX(), x1 = r.getRight(), xm = r.getCentreX(), y0 = r.getY(), y1 = r.getBottom(), ym = r.getCentreY();
        switch (type)
        {
            case 1: p.startNewSubPath (x0, ym); p.quadraticTo (xm, y0 - (y1 - y0) * 0.3f, x1, ym); break;            // peak
            case 2: p.startNewSubPath (x0, y0); p.lineTo (xm - 2, y0); p.lineTo (xm + 2, ym); p.lineTo (x1, ym); break; // low shelf
            case 3: p.startNewSubPath (x0, ym); p.lineTo (xm - 2, ym); p.lineTo (xm + 2, y0); p.lineTo (x1, y0); break; // high shelf
            case 4: p.startNewSubPath (x0 + 2, y1); p.quadraticTo (xm - 1, y0, x1, y0); break;                          // low cut
            case 5: p.startNewSubPath (x0, y0); p.quadraticTo (xm + 1, y0, x1 - 2, y1); break;                          // high cut
            case 6: p.startNewSubPath (x0, y1); p.quadraticTo (xm, y0 - (y1 - y0) * 0.6f, x1, y1); break;                // band-pass
            case 7: p.startNewSubPath (x0, y0); p.quadraticTo (xm, y1 + (y1 - y0) * 0.6f, x1, y0); break;                // notch
            default: p.startNewSubPath (x0, ym); p.lineTo (x1, ym); break;                                             // off
        }
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // a small drawing of an LFO / oscillator wave named in English (false: not a wave name)
    static bool drawWaveGlyph (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& name)
    {
        if (name != "Sine" && name != "Triangle" && name != "Square" && name != "Saw")
            return false;
        juce::Path p;
        constexpr int n = 32;
        for (int i = 0; i <= n; ++i)
        {
            const auto t = (float) i / (float) n;
            const auto y = name == "Sine" ? std::sin (t * juce::MathConstants<float>::twoPi)
                           : name == "Triangle" ? (t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f))
                           : name == "Square" ? (t < 0.5f ? 1.0f : -1.0f)
                                              : 2.0f * t - 1.0f;
            const juce::Point<float> pt { r.getX() + r.getWidth() * t, r.getCentreY() - r.getHeight() * 0.5f * y };
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return true;
    }

    static void drawSmallArrow (juce::Graphics& g, juce::Rectangle<float> r, bool up)
    {
        const auto c = r.getCentre();
        const auto w = juce::jmin (r.getWidth(), r.getHeight() * 2.0f) * 0.32f;
        juce::Path p;
        if (up)
            p.addTriangle (c.x - w, c.y + w * 0.5f, c.x + w, c.y + w * 0.5f, c.x, c.y - w * 0.5f);
        else
            p.addTriangle (c.x - w, c.y - w * 0.5f, c.x + w, c.y - w * 0.5f, c.x, c.y + w * 0.5f);
        g.fillPath (p);
    }

    // --- displays (graphs drawn from the parameters; FL's live analysers are not reproduced) -------------

    float param01 (const char* name) const { return valueOf (plugui::paramIndex (kind, name)); }

    // LYNX Tune's live display: the read-out on the left (note, cents, Hz, target, correction meter), the last 8 s of
    // sung (white) and corrected (accent) pitch on the right over the key's note lines
    void paintPitchDisplay (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx, bool readout = true)
    {
        using namespace theme;
        const auto s = scale();
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        auto noteText = [] (int n) { return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1); };

        std::vector<PitchPoint> history = pitchHistory;
        if (demoDisplays && history.empty()) // a sung phrase with vibrato and scoops, corrected toward the scale
            for (std::size_t i = 0; i < pitchHistoryLength; ++i)
            {
                const auto t = (float) i / 30.0f;
                const auto phrase = std::fmod (t, 2.7f);
                const auto base = t < 2.7f ? 64.0f : t < 5.4f ? 67.0f : 69.0f;
                const auto sung = phrase < 0.15f ? 0.0f : base - 0.6f * std::exp (-phrase * 5.0f) + 0.35f * std::sin (t * 20.0f) * juce::jmin (1.0f, phrase) + 0.18f;
                const auto shift = sung > 0.0f ? (base - sung) * (1.0f - std::exp (-phrase * 12.0f)) : 0.0f;
                history.push_back ({ sung, sung > 0.0f ? base : 0.0f, shift });
            }

        const auto last = history.empty() ? PitchPoint { 0.0f, 0.0f, 0.0f } : history.back();
        [[maybe_unused]] const auto voiced = last.sung > 0.5f;
        [[maybe_unused]] const auto ref = lynxtune::referenceHz (fx.params[(std::size_t) lynxtune::reference]);

        if (readout)
        {
            // --- read-out
            auto left = r.removeFromLeft (juce::jmin (176.0f * s, r.getWidth() * 0.34f)).reduced (8.0f * s, 6.0f * s);
            const auto noteArea = left.removeFromTop (left.getHeight() * 0.42f);
            g.setFont (font (juce::jmax (12.0f, 34.0f * s), true));
            g.setColour (voiced ? colour::textPrimary : colour::textDisabled);
            const auto nearest = (int) std::lround (last.sung);
            g.drawText (voiced ? noteText (nearest) : juce::String ("--"), noteArea.withTrimmedRight (noteArea.getWidth() * 0.4f), juce::Justification::centredLeft);
            g.setFont (font (juce::jmax (8.0f, 12.0f * s)));
            g.setColour (colour::textSecondary);
            if (voiced)
            {
                const auto cents = (int) std::lround ((last.sung - (float) nearest) * 100.0f);
                const auto hzNow = ref * std::exp2 ((last.sung - 69.0f) / 12.0f);
                const auto side = noteArea.withTrimmedLeft (noteArea.getWidth() * 0.6f);
                g.drawText ((cents > 0 ? "+" : "") + juce::String (cents) + " c", side.withHeight (side.getHeight() * 0.5f), juce::Justification::bottomLeft);
                g.drawText (juce::String (hzNow, 1) + " Hz", side.withTrimmedTop (side.getHeight() * 0.5f), juce::Justification::topLeft);
            }
            auto row = [&] (const juce::String& caption, const juce::String& value, juce::Colour c) {
                auto line = left.removeFromTop (juce::jmax (12.0f, 18.0f * s));
                g.setColour (colour::textDisabled);
                g.setFont (font (juce::jmax (7.0f, 10.0f * s), true));
                g.drawText (caption, line, juce::Justification::centredLeft);
                g.setColour (c);
                g.setFont (font (juce::jmax (8.0f, 12.0f * s), true));
                g.drawText (value, line, juce::Justification::centredRight);
            };
            row (caps (tr ("Target")), voiced && last.target > 0.5f ? noteText ((int) std::lround (last.target)) : juce::String ("--"), accent);
            const auto corrCents = (int) std::lround (last.shift * 100.0f);
            row (caps (tr ("Correction")), voiced ? (corrCents > 0 ? "+" : "") + juce::String (corrCents) + " c" : juce::String ("--"), colour::textPrimary);
            // the correction meter: ±100 cents from the centre
            auto bar = left.removeFromTop (juce::jmax (6.0f, 9.0f * s)).reduced (0.0f, 1.0f);
            skin::valuePlate (g, bar, radius::sm);
            g.setColour (colour::gridBar);
            g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() - 2.0f, bar.getBottom() + 2.0f);
            if (voiced)
            {
                const auto amount = juce::jlimit (-1.0f, 1.0f, last.shift);
                const auto x0 = bar.getCentreX(), x1 = bar.getCentreX() + bar.getWidth() * 0.5f * amount;
                g.setColour ((fx.enabled ? accent : colour::textDisabled).withAlpha (0.9f));
                g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (x0, x1), bar.getY(), std::abs (x1 - x0) + 1.0f, bar.getHeight()), radius::sm);
            }

        }

        // --- graph
        auto graph = r.reduced (6.0f * s, 6.0f * s);
        skin::well (g, graph, radius::md * s, fx.enabled ? accent : juce::Colours::transparentBlack);
        // vertical range: ±7 semitones around the recent pitch (follows the voice)
        float centre = 0.0f;
        int count = 0;
        for (auto i = history.size() > 60 ? history.size() - 60 : 0; i < history.size(); ++i)
            if (history[i].sung > 0.5f) { centre += history[i].sung; ++count; }
        centre = count > 0 ? std::round (centre / (float) count) : (displayCentre > 0.0f ? displayCentre : 60.0f);
        displayCentre = displayCentre <= 0.0f ? centre : displayCentre + 0.15f * (centre - displayCentre);
        const auto span = 7.5f;
        auto yFor = [&] (float note) { return graph.getCentreY() - (note - displayCentre) / span * graph.getHeight() * 0.5f; };
        // note lanes: the scale's notes brighter, C labelled
        const auto mask = vocal::scaleMask ((vocal::Scale) lynxtune::scaleIndex (fx.params[(std::size_t) lynxtune::scale]),
                                            lynxtune::keyIndex (fx.params[(std::size_t) lynxtune::key]), 0xfff);
        g.saveState();
        g.reduceClipRegion (graph.toNearestInt());
        g.setFont (font (juce::jmax (7.0f, 9.0f * s)));
        for (int n = (int) std::floor (displayCentre - span) - 1; n <= (int) std::ceil (displayCentre + span) + 1; ++n)
        {
            const auto inScale = ((mask >> (((n % 12) + 12) % 12)) & 1) != 0;
            const auto on = fx.params[(std::size_t) (lynxtune::noteC + ((n % 12) + 12) % 12)] >= 0.5f;
            const auto y = yFor ((float) n);
            g.setColour (inScale && on ? colour::gridBar : colour::gridStep);
            g.drawHorizontalLine ((int) y, graph.getX(), graph.getRight());
            if (inScale && on)
            {
                g.setColour (colour::textDisabled);
                g.drawText (noteText (n), juce::Rectangle<float> (graph.getX() + 3.0f, y - 7.0f, 30.0f * s, 12.0f), juce::Justification::centredLeft);
            }
        }
        // the curves, newest on the right
        const auto stepX = graph.getWidth() / (float) (pitchHistoryLength - 1);
        const auto x0 = graph.getRight() - stepX * (float) (history.size() - 1);
        for (int which = 0; which < 2; ++which)
        {
            juce::Path p;
            bool open = false;
            for (std::size_t i = 0; i < history.size(); ++i)
            {
                const auto& h = history[i];
                if (h.sung <= 0.5f) { open = false; continue; }
                const auto x = x0 + stepX * (float) i;
                const auto y = yFor (which == 0 ? h.sung : h.sung + h.shift);
                if (! open) p.startNewSubPath (x, y);
                else p.lineTo (x, y);
                open = true;
            }
            if (which == 0)
            {
                g.setColour (colour::textSecondary.withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.2f * s));
            }
            else
            {
                skin::glowStroke (g, p, fx.enabled ? accent : colour::textDisabled, 2.0f * s, fx.enabled ? 1.0f : 0.0f);
                // round 126: the newest corrected pitch as a lit bead
                if (fx.enabled && ! history.empty() && history.back().sung > 0.5f)
                {
                    const juce::Point<float> head { x0 + stepX * (float) (history.size() - 1), yFor (history.back().sung + history.back().shift) };
                    skin::bloom (g, head, 14.0f * s, accent, 0.6f);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (juce::Rectangle<float> (5.0f * s, 5.0f * s).withCentre (head));
                }
            }
        }
        g.restoreState();
        if (history.empty() || std::none_of (history.begin(), history.end(), [] (const PitchPoint& h) { return h.sung > 0.5f; }))
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 12.0f * s)));
            g.drawText (tr ("Play a voice or an instrument through the insert to see its pitch"), graph, juce::Justification::centred, true);
        }
        // legend
        g.setFont (font (juce::jmax (7.0f, 9.0f * s)));
        g.setColour (colour::textSecondary);
        g.drawText (tr ("input"), graph.removeFromTop (14.0f * s).removeFromRight (120.0f * s).withTrimmedRight (6.0f), juce::Justification::centredLeft);
        g.setColour (accent);
        g.drawText (tr ("corrected"), graph.withHeight (14.0f * s).withY (graph.getY() - 14.0f * s).removeFromRight (64.0f * s), juce::Justification::centredLeft);
    }

    // LYNX Tune's correction meter (round 120, the centre of the auto-mode workflow): a ring of segments from −100 to
    // +100 cents lit from the top toward the correction being applied, the target note's name in the middle and the
    // sung pitch's offset under it
    void paintPitchRing (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        PitchPoint last { 0.0f, 0.0f, 0.0f };
        if (tuneHold)
            last = holdPoint; // HOLD: the read-out frozen where it was clicked (round 128)
        else if (! pitchHistory.empty())
            last = pitchHistory.back();
        else if (demoDisplays)
            last = { 63.62f, 64.0f, 0.38f };
        const auto voiced = last.sung > 0.5f;
        const auto side = juce::jmin (r.getWidth(), r.getHeight());
        auto dial = r.withSizeKeepingCentre (side, side).reduced (4.0f * s);
        const auto c = dial.getCentre();
        const auto outer = dial.getWidth() * 0.5f, inner = outer * 0.74f;
        // round 126: the ring is the hero of the editor: a bloom of the accent behind it (stronger with the
        // correction), a deep bezel, a lit glass face
        const auto voicedCorr = voiced ? juce::jmin (1.0f, std::abs (last.shift)) : 0.0f;
        if (fx.enabled)
            skin::bloom (g, c, outer * 1.55f, accent, 0.5f + 0.3f * voicedCorr);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (dial.expanded (3.0f).translated (0.0f, 2.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3e48), c.x, dial.getY() - 3.0f, juce::Colour (0xff111216), c.x, dial.getBottom() + 3.0f, false));
        g.fillEllipse (dial.expanded (3.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050608), 0.0f, dial.getY(), juce::Colour (0xff121319), 0.0f, dial.getBottom(), false));
        g.fillEllipse (dial);
        if (fx.enabled && ! skin::reducedEffects())
        {
            juce::ColourGradient face (accent.withAlpha (0.10f + 0.12f * voicedCorr), c.x, c.y, accent.withAlpha (0.0f), c.x + inner, c.y, true);
            g.setGradientFill (face);
            g.fillEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (c));
        }
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (dial.reduced (0.5f), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawEllipse (juce::Rectangle<float> (inner * 2.0f - 6.0f, inner * 2.0f - 6.0f).withCentre (c), 1.0f);
        // 50 segments over 300° (−100 … +100 c), the top = 0
        constexpr int segments = 50;
        const auto sweep = juce::MathConstants<float>::pi * 5.0f / 3.0f;
        const auto corr = juce::jlimit (-1.0f, 1.0f, voiced ? last.shift : 0.0f);
        for (int k = 0; k < segments; ++k)
        {
            const auto u = ((float) k + 0.5f) / (float) segments * 2.0f - 1.0f; // −1 … +1
            const auto a = u * sweep * 0.5f;
            const auto lit = voiced && ((corr > 0.0f && u > 0.0f && u <= corr) || (corr < 0.0f && u < 0.0f && u >= corr));
            juce::Path seg;
            const auto w = sweep / (float) segments * 0.35f;
            seg.addCentredArc (c.x, c.y, (outer + inner) * 0.5f, (outer + inner) * 0.5f, 0.0f, a - w, a + w, true);
            const auto tipSeg = lit && std::abs (u) + 1.0f / (float) segments > std::abs (corr);
            if (lit && fx.enabled && ! skin::reducedEffects()) // a lit segment glows
            {
                g.setColour (accent.withAlpha (0.16f));
                g.strokePath (seg, juce::PathStrokeType ((outer - inner) * 1.5f));
                g.setColour (accent.withAlpha (0.3f));
                g.strokePath (seg, juce::PathStrokeType ((outer - inner) * 1.05f));
            }
            // lit: brighter toward the tip of the correction (the tip white-hot); unlit: dim glass LEDs
            const auto litColour = accent.interpolatedWith (juce::Colours::white, tipSeg ? 0.55f : 0.12f * std::abs (u));
            g.setColour (lit ? (fx.enabled ? litColour : colour::textDisabled) : juce::Colour (0xff22252c));
            g.strokePath (seg, juce::PathStrokeType ((outer - inner) * 0.8f));

        }
        g.setColour (colour::gridBar);
        g.drawLine (c.x, c.y - outer, c.x, c.y - inner, 1.5f);
        // scale marks
        g.setFont (font (juce::jmax (7.0f, 9.0f * s)));
        g.setColour (colour::textDisabled);
        for (const int m : { -100, -50, 0, 50, 100 })
        {
            const auto a = (float) m / 100.0f * sweep * 0.5f;
            const auto rr = outer + 9.0f * s;
            const juce::Point<float> pt { c.x + rr * std::sin (a), c.y - rr * std::cos (a) };
            g.drawText ((m > 0 ? "+" : "") + juce::String (m), juce::Rectangle<float> (28.0f * s, 12.0f * s).withCentre (pt), juce::Justification::centred);
        }
        // the note and the offset
        const auto target = (int) std::lround (last.target);
        g.setFont (font (juce::jmax (14.0f, inner * 0.66f), true));
        {
            const auto noteBox = juce::Rectangle<float> (inner * 1.6f, inner * 0.8f).withCentre (c.translated (0.0f, -inner * 0.08f));
            const auto noteName = voiced && last.target > 0.5f ? juce::String (names[((target % 12) + 12) % 12]) : juce::String ("--");
            if (voiced && fx.enabled && ! skin::reducedEffects()) // the note glows
            {
                g.setColour (accent.withAlpha (0.2f));
                for (const auto& o : { juce::Point<float> { -2.0f, 0.0f }, { 2.0f, 0.0f }, { 0.0f, -2.0f }, { 0.0f, 2.0f } })
                    g.drawText (noteName, noteBox.translated (o.x, o.y), juce::Justification::centred);
            }
            g.setColour (voiced ? (fx.enabled ? accent.interpolatedWith (juce::Colours::white, 0.3f) : colour::textSecondary) : colour::textDisabled);
            g.drawText (noteName, noteBox, juce::Justification::centred);
        }
        g.setFont (font (juce::jmax (8.0f, 11.0f * s)));
        g.setColour (colour::textSecondary);
        if (voiced)
        {
            const auto cents = (int) std::lround ((last.sung - (float) target) * 100.0f);
            g.drawText ((cents > 0 ? "+" : "") + juce::String (cents) + " c  " + juce::String (target / 12 - 1),
                        juce::Rectangle<float> (inner * 1.4f, 16.0f * s).withCentre (c.translated (0.0f, inner * 0.5f)), juce::Justification::centred);
        }
        else
            g.drawText (tr ("no pitch"), juce::Rectangle<float> (inner * 1.4f, 16.0f * s).withCentre (c.translated (0.0f, inner * 0.5f)), juce::Justification::centred);
        // HOLD (round 128): click the meter to freeze its read-out
        g.setFont (font (juce::jmax (7.0f, 9.0f * s), true));
        g.setColour (tuneHold ? accent : colour::textDisabled);
        g.drawText (tr ("HOLD"), juce::Rectangle<float> (inner, 12.0f * s).withCentre (c.translated (0.0f, inner * 0.78f)), juce::Justification::centred);
    }

    // --- LYNX Tune's keyboard (round 128; round 147: the Waves Tune Real-Time role's keyboard) ----------------------
    // A key edits its note name in every octave (the reference's Group Octaves, always on here): in BYPASS mode it
    // switches "pass uncorrected" (still a target, no correction), in REMOVE mode a click cycles the note's status like
    // the reference's — legal → illegal (to the nearest legal note) → illegal, pitch up → illegal, pitch down → legal.
    // LATCH off = the edit lasts only while the key is held (live use). Round 147: the view's navigation (− / + = fewer
    // / more octaves, ◀ / ▶ = scroll), the range bar over the keys (click or drag near a boundary: the notes outside
    // the range pass uncorrected), the range presets and Reset scale.
    int tuneFirstOctave = 2, tuneShownOctaves = 4; // the view: C2 … B5 by default (the editor's own, not saved)
    int tuneRangeDrag = -1;                       // 0 = the low boundary, 1 = the high one
    int tuneSendFlash = 0;                        // round 150: Send lit for a few repaints after a click
    enum TuneButton { tbBypass, tbRemove, tbReset, tbRange0, tbZoomOut = tbRange0 + lynxtune::rangePresets, tbZoomIn, tbLeft, tbRight, tbLatch,
                      tbSend, tbReceive, tbGroup, tbCount }; // round 150: Send / Receive; round 152: Group octaves
    juce::Rectangle<float> tuneKeysStrip (juce::Rectangle<float> r) const { return r.withHeight (22.0f * scale()); }
    juce::Rectangle<float> tuneKeysBed (juce::Rectangle<float> r) const { return r.withTrimmedTop (36.0f * scale()).reduced (2.0f * scale(), 2.0f * scale()); }
    juce::Rectangle<float> tuneRangeBar (juce::Rectangle<float> r) const
    {
        const auto s = scale();
        const auto bed = tuneKeysBed (r);
        return { bed.getX(), bed.getY() - 10.0f * s, bed.getWidth(), 8.0f * s };
    }
    juce::Rectangle<float> tuneModeButton (juce::Rectangle<float> r, int which) const
    {
        const auto s = scale();
        const auto strip = tuneKeysStrip (r).reduced (2.0f * s, 2.0f * s);
        auto at = [&] (float x, float w) { return juce::Rectangle<float> (strip.getX() + x * s, strip.getY(), w * s, strip.getHeight()); };
        // round 152: narrower buttons so Group octaves fits left of LATCH
        if (which == tbBypass) return at (0.0f, 58.0f);
        if (which == tbRemove) return at (62.0f, 58.0f);
        if (which == tbReset) return at (124.0f, 44.0f);
        if (which == tbSend) return at (172.0f, 40.0f);    // round 150
        if (which == tbReceive) return at (216.0f, 50.0f);
        if (which >= tbRange0 && which < tbZoomOut) return at (270.0f + 42.0f * (float) (which - tbRange0), 40.0f);
        if (which >= tbZoomOut && which <= tbRight) return at (568.0f + 22.0f * (float) (which - tbZoomOut), 20.0f);
        if (which == tbGroup) return at (658.0f, 46.0f); // round 152
        return strip.withLeft (strip.getRight() - 62.0f * s); // LATCH
    }
    int tuneVisibleWhites() const { return 7 * tuneShownOctaves; }
    int tuneFirstNote() const { return 12 * (tuneFirstOctave + 1); } // MIDI note of the first C shown (C4 = 60)
    float tuneNoteX (juce::Rectangle<float> bed, int note) const     // the left edge of a note's white key column
    {
        static const int whiteOf[12] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        const auto rel = note - tuneFirstNote();
        const auto oct = (int) std::floor ((double) rel / 12.0);
        const auto w = 7 * oct + whiteOf[((rel % 12) + 12) % 12];
        return bed.getX() + bed.getWidth() * (float) w / (float) tuneVisibleWhites();
    }
    // the MIDI note of the key under p, −1 = none
    int tuneKeyAt (juce::Rectangle<float> r, juce::Point<float> p) const
    {
        const auto bed = tuneKeysBed (r);
        if (! bed.contains (p))
            return -1;
        static const int whitePc[7] { 0, 2, 4, 5, 7, 9, 11 };
        const auto n = tuneVisibleWhites();
        const auto ww = bed.getWidth() / (float) n;
        if (p.y < bed.getY() + bed.getHeight() * 0.6f) // black keys first (on top)
            for (int w = 0; w < n; ++w)
            {
                const auto pc = whitePc[w % 7];
                if (pc == 4 || pc == 11)
                    continue;
                const auto bx = bed.getX() + ww * (float) (w + 1) - ww * 0.3f;
                if (p.x >= bx && p.x < bx + ww * 0.6f)
                    return tuneFirstNote() + 12 * (w / 7) + pc + 1;
            }
        const auto w = juce::jlimit (0, n - 1, (int) ((p.x - bed.getX()) / ww));
        return tuneFirstNote() + 12 * (w / 7) + whitePc[w % 7];
    }
    // the note under x in the range bar (the nearest white-key column), clamped to the view
    int tuneNoteAtX (juce::Rectangle<float> r, float x) const
    {
        const auto bed = tuneKeysBed (r);
        static const int whitePc[7] { 0, 2, 4, 5, 7, 9, 11 };
        const auto n = tuneVisibleWhites();
        const auto w = juce::jlimit (0, n - 1, (int) ((x - bed.getX()) / (bed.getWidth() / (float) n)));
        return tuneFirstNote() + 12 * (w / 7) + whitePc[w % 7];
    }
    void paintTuneKeys (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto& p = fx.params;
        const auto sc = (vocal::Scale) lynxtune::effectiveScale (p[lynxtune::scale], p[lynxtune::moreScales]);
        const auto inScale = vocal::scaleMask (sc, lynxtune::keyIndex (p[lynxtune::key]), 0xfff);
        const auto lowNote = lynxtune::rangeNote (p[lynxtune::rangeLow]), highNote = lynxtune::rangeNote (p[lynxtune::rangeHigh]);
        // the strip: modes, Reset scale, the range presets, the view's navigation, latch
        for (int b = 0; b < tbCount; ++b)
        {
            const auto br = tuneModeButton (r, b);
            bool lit = false;
            juce::String name;
            if (b == tbBypass) { lit = ! tuneRemove; name = tr ("Bypass"); }
            else if (b == tbRemove) { lit = tuneRemove; name = tr ("Remove"); }
            else if (b == tbReset) name = tr ("Reset");
            else if (b == tbSend) { lit = tuneSendFlash > 0; name = tr ("Send"); }
            else if (b == tbReceive) { lit = p[lynxtune::receiveScale] >= 0.5f; name = tr ("Receive"); }
            else if (b == tbGroup) { lit = lynxtune::groupOctavesOn (p.data()); name = tr ("Group"); }
            else if (b >= tbRange0 && b < tbZoomOut)
            {
                const auto [lo, hi] = lynxtune::rangePresetNotes (b - tbRange0);
                lit = lo == lowNote && hi == highNote;
                name = tr (lynxtune::rangePresetName (b - tbRange0));
            }
            else if (b == tbZoomOut) name = "-";
            else if (b == tbZoomIn) name = "+";
            else if (b == tbLeft) name = "<";
            else if (b == tbRight) name = ">";
            else { lit = tuneLatch; name = tr ("Latch"); }
            skin::button (g, br, lit, br.contains (mouseAt), b == tbBypass ? juce::Colour (0xffe0a040) : accent, radius::sm * s);
            g.setColour (lit ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
            g.drawText (name, br, juce::Justification::centred);
        }
        // the range bar: the notes inside the range lit, the two boundaries as handles
        const auto bed = tuneKeysBed (r);
        {
            const auto rb = tuneRangeBar (r);
            g.setColour (colour::bg0);
            g.fillRoundedRectangle (rb, 2.0f * s);
            const auto x0 = juce::jlimit (rb.getX(), rb.getRight(), tuneNoteX (bed, lowNote));
            const auto x1 = juce::jlimit (rb.getX(), rb.getRight(), tuneNoteX (bed, highNote + 1));
            if (x1 > x0)
            {
                g.setColour (accent.withAlpha (fx.enabled ? 0.55f : 0.25f));
                g.fillRoundedRectangle (juce::Rectangle<float> (x0, rb.getY(), x1 - x0, rb.getHeight()), 2.0f * s);
            }
            g.setColour (colour::textPrimary);
            for (const auto x : { x0, x1 })
                if (x > rb.getX() && x < rb.getRight())
                    g.fillRect (juce::Rectangle<float> (3.0f * s, rb.getHeight() + 4.0f * s).withCentre ({ x, rb.getCentreY() }));
        }
        // keys
        const auto n = tuneVisibleWhites();
        const auto ww = bed.getWidth() / (float) n;
        static const int whitePc[7] { 0, 2, 4, 5, 7, 9, 11 };
        const auto sungNote = pitchHistory.empty() || pitchHistory.back().sung < 0.5f ? -1 : (int) std::lround (pitchHistory.back().target);
        // round 152: each key's status in effect (its own when Group octaves is off)
        const auto mask = tuneMask (p);
        auto status = [&] (int note) { return lynxtune::noteStatus (p.data(), note, mask); };
        auto legalNote = [&] (int note) { const auto st = status (note); return st == lynxtune::KeyStatus::legal || st == lynxtune::KeyStatus::bypass; };
        auto keyColour = [&] (int note, bool black) {
            const auto bypassed = status (note) == lynxtune::KeyStatus::bypass;
            auto c = black ? juce::Colour (0xff1c1e24) : juce::Colour (0xffd9dbe0);
            if (! legalNote (note))
                c = black ? juce::Colour (0xff101114) : juce::Colour (0xff5a5d66); // removed / not in the scale
            else if (bypassed)
                c = black ? juce::Colour (0xff6b4d16) : juce::Colour (0xffe6be6e);
            if (note < lowNote || note > highNote) // outside the range: passes uncorrected
                c = c.interpolatedWith (colour::bg0, 0.55f);
            return c;
        };
        auto mark = [&] (juce::Rectangle<float> k, int note) {
            const auto pc = note % 12;
            const auto inRange = note >= lowNote && note <= highNote;
            const auto st = status (note);
            const bool own = ! lynxtune::groupOctavesOn (p.data()) && lynxtune::keyOverride (p.data(), note) != lynxtune::KeyStatus::follow;
            if (legalNote (note) && fx.enabled && inRange) // a target: the accent line at the key's top
            {
                g.setColour (st == lynxtune::KeyStatus::bypass ? juce::Colour (0xffe0a040) : accent);
                g.fillRect (k.withHeight (3.0f * s));
            }
            if (own) // round 152: a key with its own status (Group octaves off): an outline
            {
                g.setColour (accent.withAlpha (0.8f));
                g.drawRoundedRectangle (k.reduced (1.0f * s), 2.0f * s, 1.2f * s);
            }
            if (! legalNote (note))
            {
                const auto dir = st == lynxtune::KeyStatus::removedUp ? lynxtune::NoteDir::up
                                 : (st == lynxtune::KeyStatus::removedDown ? lynxtune::NoteDir::down : lynxtune::NoteDir::nearest);
                const auto x = juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ k.getCentreX(), k.getY() + 10.0f * s });
                g.setColour (accent.withAlpha (0.9f));
                if (dir != lynxtune::NoteDir::nearest) // illegal, pitch up / down: an arrow
                {
                    juce::Path a;
                    const auto up = dir == lynxtune::NoteDir::up;
                    a.addTriangle (x.getX(), up ? x.getBottom() : x.getY(), x.getRight(), up ? x.getBottom() : x.getY(), x.getCentreX(), up ? x.getY() : x.getBottom());
                    g.fillPath (a);
                }
                else if (((inScale >> pc) & 1u) != 0 || own) // removed by hand: a cross
                {
                    g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.5f * s);
                    g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.5f * s);
                }
            }
            if (note == sungNote && fx.enabled)
            {
                g.setColour (accent.withAlpha (0.55f));
                g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ k.getCentreX(), k.getBottom() - 9.0f * s }));
            }
        };
        for (int w = 0; w < n; ++w)
        {
            const auto note = tuneFirstNote() + 12 * (w / 7) + whitePc[w % 7];
            const auto k = juce::Rectangle<float> (bed.getX() + ww * (float) w, bed.getY(), ww - 1.0f, bed.getHeight());
            g.setColour (keyColour (note, false));
            g.fillRoundedRectangle (k, 2.0f * s);
            mark (k, note);
            if (note % 12 == 0)
            {
                g.setColour (juce::Colour (0xff3a3d45));
                g.setFont (font (juce::jmax (7.0f, 9.0f * s)));
                g.drawText ("C" + juce::String (note / 12 - 1), k.withTrimmedBottom (2.0f * s).withTop (k.getBottom() - 14.0f * s), juce::Justification::centred);
            }
        }
        for (int w = 0; w < n; ++w)
        {
            const auto pc = whitePc[w % 7];
            if (pc == 4 || pc == 11)
                continue;
            const auto note = tuneFirstNote() + 12 * (w / 7) + pc + 1;
            const auto k = juce::Rectangle<float> (bed.getX() + ww * (float) (w + 1) - ww * 0.3f, bed.getY(), ww * 0.6f, bed.getHeight() * 0.6f);
            g.setColour (keyColour (note, true));
            g.fillRoundedRectangle (k, 2.0f * s);
            mark (k, note);
        }
    }
    void tuneSetRangeAt (juce::Rectangle<float> r, float x, bool pickNearer)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        const auto note = tuneNoteAtX (r, x);
        const auto lo = lynxtune::rangeNote (fx->params[lynxtune::rangeLow]), hi = lynxtune::rangeNote (fx->params[lynxtune::rangeHigh]);
        if (pickNearer)
            tuneRangeDrag = std::abs (note - lo) <= std::abs (note - hi) ? 0 : 1;
        if (tuneRangeDrag == 0)
            setParams ({ { lynxtune::rangeLow, lynxtune::rangeNoteValue (juce::jmin (note, hi)) } });
        else if (tuneRangeDrag == 1)
            setParams ({ { lynxtune::rangeHigh, lynxtune::rangeNoteValue (juce::jmax (note, lo)) } });
        const auto shown = tuneRangeDrag == 0 ? lynxtune::rangeLow : lynxtune::rangeHigh;
        ui::showValueTip (*this, tuneRangeBar (r).toNearestInt(), tr ("Range") + " " + juce::String (lynxtune::paramText (shown, current()->params[(std::size_t) shown])));
    }
    // round 152: the pitch classes that are targets (the scale on the key, minus the notes switched off)
    static std::uint16_t tuneMask (const EffectParams& p)
    {
        std::uint16_t notes = 0;
        for (int k = 0; k < 12; ++k)
            if (p[(std::size_t) (lynxtune::noteC + k)] >= 0.5f) notes = (std::uint16_t) (notes | (1u << k));
        const auto sc = (vocal::Scale) lynxtune::effectiveScale (p[lynxtune::scale], p[lynxtune::moreScales]);
        return (std::uint16_t) (vocal::scaleMask (sc, lynxtune::keyIndex (p[lynxtune::key]), notes) & notes);
    }
    void tuneKeyClick (juce::Rectangle<float> r, juce::Point<float> at)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        for (int b = 0; b < tbCount; ++b)
            if (tuneModeButton (r, b).contains (at))
            {
                if (b == tbLatch)
                    tuneLatch = ! tuneLatch;
                else if (b == tbSend) // round 150: this scale to every other LYNX Tune with Receive on
                {
                    engine.editSong ([t = track, sl = slot] (Song& song) { lynxtune::sendScaleToAll (song, t, sl); });
                    tuneSendFlash = 8;
                }
                else if (b == tbReceive) // on: take the scale sent last now, and the ones sent later
                {
                    const bool nowOn = fx->params[lynxtune::receiveScale] < 0.5f;
                    std::vector<std::pair<int, float>> c { { lynxtune::receiveScale, nowOn ? 1.0f : 0.0f } };
                    lynxtune::SharedScale sh;
                    if (nowOn && lynxtune::publishedScale (sh))
                        for (std::size_t i = 0; i < sh.values.size(); ++i)
                            c.push_back ({ lynxtune::sharedScaleList()[i], sh.values[i] });
                    setParams (c);
                }
                else if (b == tbBypass || b == tbRemove)
                    tuneRemove = b == tbRemove;
                else if (b == tbGroup) // round 152: Group octaves on / off (the keys' own statuses are kept)
                    setParams ({ { lynxtune::groupOctaves, lynxtune::groupOctavesOn (fx->params.data()) ? 0.0f : 1.0f } });
                else if (b == tbReset) // every note of the scale legal again, nothing bypassed, no direction
                {
                    std::vector<std::pair<int, float>> c;
                    for (int k = 0; k < lynxtune::keyMapParams; ++k) // round 152: and no key of its own
                        c.push_back ({ lynxtune::keyMap1 + k, 0.0f });
                    for (int k = 0; k < 12; ++k)
                    {
                        c.push_back ({ lynxtune::noteC + k, 1.0f });
                        c.push_back ({ lynxtune::bypassC + k, 0.0f });
                        c.push_back ({ lynxtune::noteDirC + k, 0.0f });
                    }
                    setParams (c);
                }
                else if (b >= tbRange0 && b < tbZoomOut)
                {
                    // a range preset: the keyboard's range, and the detector's input type that covers it
                    const auto k = b - tbRange0;
                    const auto [lo, hi] = lynxtune::rangePresetNotes (k);
                    std::vector<std::pair<int, float>> c { { lynxtune::rangeLow, lynxtune::rangeNoteValue (lo) }, { lynxtune::rangeHigh, lynxtune::rangeNoteValue (hi) } };
                    static constexpr float inputType[lynxtune::rangePresets] { 2.0f / 3.0f, 2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0.0f, 0.0f, -1.0f };
                    if (inputType[k] >= 0.0f)
                        c.push_back ({ lynxtune::range, inputType[k] });
                    setParams (c);
                }
                else if (b == tbZoomOut) tuneShownOctaves = juce::jmin (7, tuneShownOctaves + 1);
                else if (b == tbZoomIn) tuneShownOctaves = juce::jmax (2, tuneShownOctaves - 1);
                else if (b == tbLeft) tuneFirstOctave = juce::jmax (0, tuneFirstOctave - 1);
                else if (b == tbRight) tuneFirstOctave = juce::jmin (9 - tuneShownOctaves, tuneFirstOctave + 1);
                tuneFirstOctave = juce::jlimit (0, juce::jmax (0, 9 - tuneShownOctaves), tuneFirstOctave);
                repaint();
                return;
            }
        if (tuneRangeBar (r).expanded (0.0f, 2.0f * scale()).contains (at))
        {
            tuneSetRangeAt (r, at.x, true);
            return;
        }
        const auto note = tuneKeyAt (r, at);
        if (note < 0)
            return;
        if (! lynxtune::groupOctavesOn (fx->params.data())) // round 152: Group octaves off — this key alone
        {
            auto copy = fx->params;
            const auto st = lynxtune::noteStatus (copy.data(), note, tuneMask (copy));
            const auto next = tuneRemove ? lynxtune::nextRemoveStatus (st)
                                         : (st == lynxtune::KeyStatus::bypass ? lynxtune::KeyStatus::legal : lynxtune::KeyStatus::bypass);
            const auto mp = lynxtune::keyMap1 + juce::jlimit (0, lynxtune::keyMapParams - 1, (note - lynxtune::keyMapFirst) / lynxtune::keyMapPerParam);
            tuneHeldMap = tuneLatch ? -1 : mp;
            tuneHeldMapValue = copy[(std::size_t) mp];
            lynxtune::setKeyOverride (copy.data(), note, next);
            setParams ({ { mp, copy[(std::size_t) mp] } });
            return;
        }
        const auto pc = note % 12;
        const auto onP = lynxtune::noteC + pc, byP = lynxtune::bypassC + pc, dirP = lynxtune::noteDirC + pc;
        const auto on = fx->params[(std::size_t) onP] >= 0.5f, by = fx->params[(std::size_t) byP] >= 0.5f;
        const auto dir = lynxtune::noteDir (fx->params[(std::size_t) dirP]);
        tuneHeld = tuneLatch ? -1 : pc;
        tuneHeldValues = { fx->params[(std::size_t) onP], fx->params[(std::size_t) byP] };
        tuneHeldDir = fx->params[(std::size_t) dirP];
        if (tuneRemove)
        {
            // the reference's status cycle: legal → illegal → illegal, pitch up → illegal, pitch down → legal (a note
            // outside the scale stays illegal: only its direction cycles)
            const auto sc = (vocal::Scale) lynxtune::effectiveScale (fx->params[lynxtune::scale], fx->params[lynxtune::moreScales]);
            const bool scaleNote = ((vocal::scaleMask (sc, lynxtune::keyIndex (fx->params[lynxtune::key]), 0xfff) >> pc) & 1u) != 0;
            const auto nextDir = dir == lynxtune::NoteDir::nearest ? 0.5f : (dir == lynxtune::NoteDir::up ? 1.0f : 0.0f);
            if (scaleNote && on)
                setParams ({ { onP, 0.0f }, { byP, 0.0f }, { dirP, 0.0f } });
            else if (scaleNote && dir == lynxtune::NoteDir::down)
                setParams ({ { onP, 1.0f }, { byP, 0.0f }, { dirP, 0.0f } });
            else
                setParams ({ { onP, on ? 1.0f : 0.0f }, { byP, 0.0f }, { dirP, nextDir } });
        }
        else
            setParams ({ { onP, 1.0f }, { byP, by && on ? 0.0f : 1.0f }, { dirP, 0.0f } });
    }
    void tuneKeyRelease()
    {
        if (tuneRangeDrag >= 0)
        {
            tuneRangeDrag = -1;
            ui::hideValueTip();
        }
        if (tuneHeldMap >= 0) // round 152
        {
            setParams ({ { tuneHeldMap, tuneHeldMapValue } });
            tuneHeldMap = -1;
        }
        if (tuneHeld < 0)
            return;
        setParams ({ { lynxtune::noteC + tuneHeld, tuneHeldValues.first }, { lynxtune::bypassC + tuneHeld, tuneHeldValues.second },
                     { lynxtune::noteDirC + tuneHeld, tuneHeldDir } });
        tuneHeld = -1;
    }
    juce::String tuneKeysHint (juce::Rectangle<float> r, juce::Point<float> at) const
    {
        for (int b = 0; b < tbCount; ++b)
            if (tuneModeButton (r, b).contains (at))
            {
                if (b == tbBypass) return tr ("Bypass mode: a key switches its note between corrected and passed as sung");
                if (b == tbRemove) return tr ("Remove mode: a key cycles its note: legal, illegal (to the nearest note), illegal pitch up, illegal pitch down");
                if (b == tbReset) return tr ("Reset scale: every note of the scale legal again, nothing bypassed");
                if (b == tbSend) return tr ("Send: copy this key, scale and keyboard to every other LYNX Tune whose Receive is on");
                if (b == tbReceive) return tr ("Receive: take the scale another LYNX Tune sends (switching it on takes the last one sent)");
                if (b == tbGroup) return tr ("Group octaves: on = a key edits its note in every octave; off = each key keeps its own status");
                if (b >= tbRange0 && b < tbZoomOut) return tr ("Range preset: notes sung outside the range pass uncorrected (Generic: the whole keyboard)");
                if (b == tbZoomOut || b == tbZoomIn) return tr ("Keyboard view: show more / fewer octaves");
                if (b == tbLeft || b == tbRight) return tr ("Keyboard view: scroll down / up an octave");
                return tr ("Latch: off = a key's edit lasts only while it is held");
            }
        if (tuneRangeBar (r).expanded (0.0f, 2.0f * scale()).contains (at))
            return tr ("Range: drag a boundary; notes sung outside it pass uncorrected");
        if (current() != nullptr && ! lynxtune::groupOctavesOn (current()->params.data())) // round 152
            return tuneRemove ? tr ("Click a key: change the status of this key only") : tr ("Click a key: bypass / correct this key only");
        return tuneRemove ? tr ("Click a key: change that note's status in every octave") : tr ("Click a key: bypass / correct that note in every octave");
    }
    // the hardware insert's round-trip read-out (round 164d): the delay in samples and ms, then what the ping says or what
    // is missing for the insert to work
    void paintHwPing (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::sm * s);
        auto area = r.reduced (8.0f * s, 6.0f * s);
        const auto samples = hwinsert::roundTripOf (fx.params[hwinsert::roundTrip]);
        const auto rate = engine.getTransport().sampleRate.load();
        g.setColour (samples > 0 ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (juce::jmax (12.0f, 22.0f * s), true));
        g.drawText (juce::String (samples) + " " + tr ("smp"), area.removeFromTop (28.0f * s), juce::Justification::centred, false);
        g.setColour (colour::textSecondary);
        g.setFont (font (juce::jmax (8.0f, 11.0f * s)));
        g.drawText (rate > 0.0 ? juce::String (1000.0 * samples / rate, 2) + " ms" : juce::String(), area.removeFromTop (16.0f * s), juce::Justification::centred, false);

        // the state (newest first): the ping running or its outcome, then what the routing lacks
        const auto ping = liveMeter (hwinsert::pingState);
        const auto flags = juce::roundToInt (liveMeter (hwinsert::status) * 16.0f);
        const auto outCh = hwinsert::channelOf (fx.params[hwinsert::output]), inCh = hwinsert::channelOf (fx.params[hwinsert::input]);
        juce::String text;
        auto warn = false;
        if (engine.getTransport().sampleRate.load() <= 0.0 || engine.getOutputChannelNames().empty())
            text = tr ("No audio device is open"), warn = true;
        else if (outCh < 0 || inCh < 0)
            text = tr ("Choose the output and the input your gear is wired to"), warn = true;
        else if ((flags & 2) != 0)
            text = tr ("The output is not open on this device"), warn = true;
        else if ((flags & 4) != 0)
            text = tr ("The input is not open on this device"), warn = true;
        else if (ping > 0.2f && ping < 0.3f)
            text = tr ("Pinging...");
        else if (ping > 0.6f && ping < 0.7f)
            text = tr ("Ping failed: no click came back (check the cable and the return level)"), warn = true;
        else if (ping > 0.7f && ping < 0.8f)
            text = tr ("Ping failed: clicks were missing"), warn = true;
        else if (ping > 0.8f)
            text = tr ("Ping failed: the round trip was not stable"), warn = true;
        else if (samples == 0)
            text = tr ("Not measured yet: press Ping"), warn = true;
        else if (const auto measured = hwinsert::measuredSetup (fx); ! measured.empty() && measured != engine.currentSetupKey())
            text = tr ("Measured for another audio setup (the device or the buffer size changed): press Ping"), warn = true; // round 165c
        else if (ping > 0.4f && ping < 0.6f)
            text = tr ("Measured: delay compensated");
        else
            text = tr ("Delay compensated (PDC)");
        g.setColour (warn ? juce::Colour (0xffe5a03b) : colour::textSecondary);
        g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
        g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, 3, 0.8f);
    }

    // shows a ping's result once (AudioEngine makes it the slot's delay: undoable, saved with the project)
    void hwPingTick()
    {
        const auto serial = juce::roundToInt (liveMeter (hwinsert::pingSerial) * 1024.0f);
        if (hwSerial < 0)
        {
            hwSerial = serial;
            return;
        }
        if (serial == hwSerial)
            return;
        hwSerial = serial;
        if (std::abs (liveMeter (hwinsert::pingState) - 0.5f) < 0.01f)
        {
            // (the engine applies the result to the slot, with the device setup it belongs to: round 165c)
            const auto samples = hwinsert::roundTripOf (liveMeter (hwinsert::pingSamples));
            for (const auto& item : activeLayout().items)
                if (item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "hwping") == 0)
                    ui::flashValueTip (*this, rectOf (item).toNearestInt(), tr ("Round trip: {0} samples", { juce::String (samples) }));
        }
        repaint();
    }
    int hwSerial = -1; // the last ping result seen (−1: not read yet)

    void paintAutoKey (juce::Graphics& g, juce::Rectangle<float> r)
    {
        using namespace theme;
        const auto s = scale();
        skin::button (g, r, autoKeyFlash > 0, r.contains (mouseAt), accent, radius::sm * s);
        g.setColour (colour::textPrimary);
        g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
        g.drawText (tr ("Auto Key"), r, juce::Justification::centred);
    }
    // LEARN (round 135): while LYNX Tune's "Learn scale" is on, every pitch class held on its MIDI channel joins the
    // custom scale (switching learning on starts a new scale)
    void learnScaleFromMidi()
    {
        const auto* fx = current();
        if (fx == nullptr || fx->kind != EffectKind::lynxTune)
            return;
        const bool on = fx->params[(std::size_t) lynxtune::learnMidi] >= 0.5f;
        if (! on)
        {
            learning = false;
            return;
        }
        if (! learning)
        {
            learning = true;
            learned = 0;
        }
        const auto held = (unsigned) std::lround (liveMeter (5) * 4096.0f) & 0xfffu;
        if ((held & ~learned) == 0)
            return;
        learned |= held;
        std::vector<std::pair<int, float>> changes { { lynxtune::scale, (float) (int) vocal::Scale::custom / (float) (vocal::scaleCount - 1) },
                                                     { lynxtune::moreScales, 0.0f } };
        for (int pc = 0; pc < 12; ++pc)
        {
            changes.push_back ({ lynxtune::noteC + pc, ((learned >> pc) & 1u) != 0 ? 1.0f : 0.0f });
            changes.push_back ({ lynxtune::bypassC + pc, 0.0f });
        }
        setParams (changes);
    }
    bool learning = false;
    unsigned learned = 0;

    // AUTO KEY: the key and major / minor scale of what was sung while the editor was open (time per pitch class)
    void autoKey (juce::Rectangle<float> r)
    {
        double total = 0.0;
        for (const auto x : keyHistogram)
            total += x;
        if (total < 30.0) // about a second of voiced singing
        {
            ui::flashValueTip (*this, r.toNearestInt(), tr ("Play the vocal for a few seconds first"));
            return;
        }
        const auto [k, mode] = vocal::detectKey (keyHistogram);
        if (k < 0)
            return;
        std::vector<std::pair<int, float>> changes { { lynxtune::key, (float) k / 11.0f },
                                                     { lynxtune::scale, (float) (int) mode / (float) (vocal::scaleCount - 1) },
                                                     { lynxtune::moreScales, 0.0f } };
        for (int pc = 0; pc < 12; ++pc)
        {
            changes.push_back ({ lynxtune::noteC + pc, 1.0f });
            changes.push_back ({ lynxtune::bypassC + pc, 0.0f });
        }
        setParams (changes);
        autoKeyFlash = 20;
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        ui::flashValueTip (*this, r.toNearestInt(), juce::String (names[k]) + " " + tr (mode == vocal::Scale::major ? "Major" : "Minor"));
    }

    // the live meter `output` of this slot's effect (published by the audio thread; 0 when not playing)
    float liveMeter (int output) const
    {
        if (node.running)
        {
            const auto fx = node.running();
            return fx != nullptr ? fx->controlOutput (output) : 0.0f;
        }
        const auto* times = engine.getProcessTimes();
        const auto key = (std::size_t) (track * maxEffectSlots + slot) * ProcessTimes::meterOutputs + (std::size_t) juce::jlimit (0, ProcessTimes::meterOutputs - 1, output);
        return times != nullptr && key < times->effectMeters.size() ? times->effectMeters[key].load (std::memory_order_relaxed) : 0.0f;
    }

    // the captions of a radio's options (Gross Beat: the slot names saved in the state)
    std::vector<juce::String> optionTexts (const LayoutItem& item) const
    {
        std::vector<juce::String> out;
        const auto labels = plugui::optionLabels (item);
        // the whole list is one translation ("Mono|Stereo|Ping pong|Off"); single words are translated alone
        const auto whole = juce::StringArray::fromTokens (tr (juce::String::fromUTF8 (item.options != nullptr ? item.options : "")), "|", "");
        for (std::size_t o = 0; o < labels.size(); ++o)
            out.push_back (whole.size() == (int) labels.size() ? whole[(int) o] : tr (juce::String::fromUTF8 (labels[o].c_str())));
        if (kind == EffectKind::grossBeat && item.columns > 0)
            if (const auto* fx = current())
            {
                const auto names = grossbeat::slotNames (fx->state, std::strcmp (item.param, "Volume slot") == 0);
                for (std::size_t i = 0; i < names.size() && i < out.size(); ++i)
                    if (! names[i].empty())
                        out[i] = juce::String::fromUTF8 (names[i].c_str());
            }
        return out;
    }

    // the cell of option `o` in a radio grid
    juce::Rectangle<float> gridCell (const LayoutItem& item, int o, int count) const
    {
        const auto r = rectOf (item);
        const auto rows = (count + item.columns - 1) / item.columns;
        const auto w = r.getWidth() / (float) item.columns, h = r.getHeight() / (float) juce::jmax (1, rows);
        return { r.getX() + w * (float) (o % item.columns), r.getY() + h * (float) (o / item.columns), w, h };
    }

    // --- round 167b: the Transmitter's envelope view and its transient / sustain sends (REA_rodada167b §1, §3)
    std::shared_ptr<Effect> runningEffect() const
    {
        if (node.running)
            return node.running();
        return engine.getEffect (track, slot);
    }

    // FL's envelope view: the last columns of the detector, newest on the right. Per column: the output's peak (grey,
    // mirrored around the middle), the transient envelope e (accent) and the sustain envelope 1 − e (light line).
    void paintTransmitterEnvelope (juce::Graphics& g, juce::Rectangle<float> area, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto running = runningEffect();
        const auto* view = transmitter::envelopeOf (running.get());
        const auto columns = juce::jmax (8, (int) area.getWidth());
        std::vector<transmitter::EnvelopeColumn> shown ((std::size_t) columns);
        if (view != nullptr)
        {
            const auto written = view->written.load (std::memory_order_acquire);
            const auto have = (int) juce::jmin<std::uint32_t> (written, (std::uint32_t) juce::jmin (columns, (int) transmitter::EnvelopeView::size - 1));
            for (int k = 0; k < have; ++k)
                shown[(std::size_t) (columns - have + k)] = view->columns[(written - (std::uint32_t) have + (std::uint32_t) k) % transmitter::EnvelopeView::size];
        }
        else if (demoDisplays) // a drum loop's envelopes, for the snapshots
            for (int k = 0; k < columns; ++k)
            {
                const auto t = std::fmod ((float) k / 46.0f, 1.0f);
                const auto e = std::exp (-t * 9.0f);
                shown[(std::size_t) k] = { 0.15f + 0.75f * std::exp (-t * 4.0f), e, 1.0f - e };
            }
        const auto x0 = area.getX(), w = area.getWidth() / (float) columns;
        const auto mid = area.getCentreY(), half = area.getHeight() * 0.5f;
        // the output, as a mirrored peak outline
        juce::Path wave;
        for (int k = 0; k < columns; ++k)
        {
            const auto y = mid - half * juce::jlimit (0.0f, 1.0f, shown[(std::size_t) k].output);
            if (k == 0) wave.startNewSubPath (x0, y); else wave.lineTo (x0 + w * (float) k, y);
        }
        for (int k = columns - 1; k >= 0; --k)
            wave.lineTo (x0 + w * (float) k, mid + half * juce::jlimit (0.0f, 1.0f, shown[(std::size_t) k].output));
        wave.closeSubPath();
        g.setColour (colour::textDisabled.withAlpha (fx.enabled ? 0.45f : 0.25f));
        g.fillPath (wave);
        auto line = [&] (float transmitter::EnvelopeColumn::* field, juce::Colour c) {
            juce::Path p;
            for (int k = 0; k < columns; ++k)
            {
                const auto y = area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, shown[(std::size_t) k].*field);
                if (k == 0) p.startNewSubPath (x0, y); else p.lineTo (x0 + w * (float) k, y);
            }
            skin::glowStroke (g, p, c, juce::jmax (1.2f, 1.6f * s), fx.enabled ? 1.0f : 0.0f);
        };
        line (&transmitter::EnvelopeColumn::sustain, (fx.enabled ? colour::textSecondary : colour::textDisabled).withAlpha (0.8f));
        line (&transmitter::EnvelopeColumn::transient, fx.enabled ? accent : colour::textDisabled);
        // the legend
        g.setFont (font (juce::jmax (7.0f, 9.0f * s), true));
        auto legend = area.reduced (4.0f * s, 2.0f * s).removeFromTop (12.0f * s);
        g.setColour (fx.enabled ? accent : colour::textDisabled);
        g.drawText (caps (tr ("Transient")), legend.removeFromLeft (80.0f * s), juce::Justification::centredLeft);
        g.setColour (colour::textSecondary);
        g.drawText (caps (tr ("Sustain")), legend.removeFromLeft (80.0f * s), juce::Justification::centredLeft);
    }

    // the send box of a part: FL's "send to mixer track" menu (… = not sent)
    void paintTransmitterSend (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx, int part)
    {
        using namespace theme;
        const auto s = scale();
        const auto target = fx.sendTracks[(std::size_t) part];
        g.setColour (target > 0 ? accent : colour::accentMuted);
        g.fillRoundedRectangle (r, radius::sm * s);
        g.setColour (target > 0 ? colour::onAccent : colour::textSecondary);
        g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
        const auto text = target > 0 ? tr ("Send to {0}", { juce::String (target) + "  " + insertName (target) }) : tr ("Send to") + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\xa6"));
        g.drawFittedText (text, r.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }

    void showTransmitterSendMenu (int part, juce::Rectangle<float> r)
    {
        const auto* fx = current();
        if (fx == nullptr || node.locate) // (a Patcher node has no insert to send from)
            return;
        juce::PopupMenu menu;
        const auto chosen = fx->sendTracks[(std::size_t) part];
        auto choose = [this, part] (int t) {
            editSlot ([part, t] (EffectSlot& e) {
                e.sendTracks[(std::size_t) part] = t;
                if (e.kind == EffectKind::equo) // (EQUO keeps its send track in its state too, as FL saves it)
                    equofx::setSendInsert (e.state, t);
            });
        };
        const auto& mixer = engine.getSong().mixer;
        if (fx->kind == EffectKind::equo) // FL's EQUO send box: (none) and the tracks this insert routes to (round 173d3)
        {
            menu.addItem ("(" + tr ("none") + ")", true, chosen <= 0, [choose] { choose (0); });
            for (int t = 1; t < (int) mixer.size(); ++t)
                if (t != track && (engine.getSong().routes (track, t) || t == chosen))
                    menu.addItem (insertName (t), true, t == chosen, [choose, t] { choose (t); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
            return;
        }
        menu.addItem (tr ("None"), true, chosen <= 0, [choose] { choose (0); });
        for (int t = 1; t < (int) mixer.size(); ++t)
            if (t != track)
                menu.addItem (juce::String (t) + "  " + insertName (t), engine.getSong().canRoute (track, t) || t == chosen, t == chosen, [choose, t] { choose (t); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
    }

    // round tp172b4: the Space engine's decay-rate EQ, the reference reverb's main display: the decay rate (100 % =
    // the SPACE time) from 20 Hz to 20 kHz on a log grid, shaped by the low and high decay multipliers past their
    // frequencies; percent on the left, hertz along the bottom
    void paintDecayEq (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        namespace sp = luxeverb::sp;
        const auto s = scale();
        const auto at = [&] (int i) { return valueOf (luxeVerbSpaceFirst + i); };
        const auto plot = r.withTrimmedLeft (44.0f * s).withTrimmedRight (14.0f * s).withTrimmedTop (22.0f * s).withTrimmedBottom (20.0f * s);
        const auto lo = std::log (20.0), hi = std::log (20000.0);
        const auto xOf = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) ((std::log (hz) - lo) / (hi - lo)); };
        constexpr float topOct = 1.5f, bottomOct = -3.4f; // log2 of the rate: 283 % … 9.5 %
        const auto yOf = [&] (double rate) {
            const auto o = (float) std::log2 (juce::jmax (1.0e-3, rate));
            return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (topOct - o) / (topOct - bottomOct));
        };
        const auto blue = skin::artAccent2 (skin::art());
        g.setFont (font (juce::jmax (8.0f, 10.5f * s)));
        // the frequency grid (1-2-5 marks a decade, the labelled ones brighter)
        for (double dec = 10.0; dec <= 10000.0; dec *= 10.0)
            for (int m = 1; m <= 9; ++m)
            {
                const auto hz = dec * m;
                if (hz < 20.0 || hz > 20000.0) continue;
                const auto x = xOf (hz);
                const auto major = m == 1 || m == 2 || m == 5;
                g.setColour (juce::Colours::white.withAlpha (major ? 0.05f : 0.022f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                if (major)
                {
                    g.setColour (colour::textDisabled);
                    g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                                juce::Rectangle<float> (x - 20.0f * s, plot.getBottom() + 3.0f * s, 40.0f * s, 14.0f * s), juce::Justification::centred, false);
                }
            }
        // the rate grid: an octave a line, labelled on the left
        for (int o = 1; o >= -3; --o)
        {
            const auto y = yOf (std::pow (2.0, o));
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (blue.withAlpha (0.85f));
            g.drawText (juce::String ((int) std::lround (100.0 * std::pow (2.0, o))) + "%", juce::Rectangle<float> (r.getX() + 2.0f * s, y - 7.0f * s, 38.0f * s, 14.0f * s),
                        juce::Justification::centredLeft, false);
        }
        g.setColour (blue);
        g.setFont (font (juce::jmax (8.0f, 11.0f * s)));
        g.drawText (tr ("Decay EQ"), juce::Rectangle<float> (r.getX() + 2.0f * s, r.getY() + 2.0f * s, 200.0f * s, 16.0f * s), juce::Justification::centredLeft, false);
        // the curve: the rate, times a smooth shelf to the low multiplier under the low frequency and to the high one over
        // the high frequency (half an octave of transition)
        const auto rate = sp::rateOf (at (sp::rate));
        const auto lowMult = sp::multOf (at (sp::lowMult)), highMult = sp::multOf (at (sp::highMult));
        const auto lowHz = sp::lowHz (at (sp::lowFreq)), highHz = sp::highHz (at (sp::highFreq));
        const auto shelf = [] (double hz, double corner, double mult, bool below) {
            const auto o = std::log2 (hz / corner) * (below ? -1.0 : 1.0);
            const auto w = 1.0 / (1.0 + std::exp (-o * 4.0));
            return std::pow (mult, w);
        };
        juce::Path curve;
        const auto steps = juce::jmax (64, (int) (plot.getWidth() / (3.0f * s)));
        for (int i = 0; i <= steps; ++i)
        {
            const auto hz = std::exp (lo + (hi - lo) * (double) i / (double) steps);
            const auto y = yOf (rate * shelf (hz, lowHz, lowMult, true) * shelf (hz, highHz, highMult, false));
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
            if (i == 0) curve.startNewSubPath (x, y);
            else curve.lineTo (x, y);
        }
        auto fill = curve;
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        const auto line = fx.enabled ? blue : colour::textDisabled;
        g.setGradientFill (juce::ColourGradient (line.withAlpha (0.10f), 0.0f, plot.getY(), line.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (line);
        g.strokePath (curve, juce::PathStrokeType (juce::jmax (1.2f, 1.5f * s)));
        // the two corner frequencies, as handles on the curve
        for (const auto hz : { lowHz, highHz })
        {
            const auto x = xOf (hz);
            const auto y = yOf (rate * shelf (hz, lowHz, lowMult, true) * shelf (hz, highHz, highMult, false));
            g.setColour (line.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (14.0f * s, 14.0f * s).withCentre ({ x, y }));
            g.setColour (line);
            g.drawEllipse (juce::Rectangle<float> (9.0f * s, 9.0f * s).withCentre ({ x, y }), 1.4f);
        }
    }

    void paintDisplay (juce::Graphics& g, const LayoutItem& item, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const juce::String id (item.display != nullptr ? item.display : "");
        if (id != "faderscale" && id != "decayeq" && id != "pitchport") // round 137: the fader scale is bare text beside its fader (tp172b4: the decay EQ lies on the field)
            skin::well (g, r, radius::md * s, fx.enabled ? accent : juce::Colours::transparentBlack);
        const auto curveColour = fx.enabled ? accent : colour::textDisabled;
        const auto inner = r.reduced (6.0f * s);
        if (dynui::paint (g, id, r, s, fx, [this] (int k) { return liveMeter (k); }, dynHistory)) // the dynamics suite
            return;
        if (designui::paint (g, id, r, s, fx, accent, designHistory, demoDisplays, item.param)) // round 168r
            return;
        if (id == "tssenvelope")
        {
            paintTransmitterEnvelope (g, inner, fx);
            return;
        }
        if (id == "pitchport") // round 190ez: Pitcher's bottom line, FL's "MIDI Input Port Number: ..." ("..." = none)
        {
            const auto port = effectMidiInPort (fx);
            const juce::Font f (theme::font (juce::jmax (8.0f, 11.0f * scale())));
            const auto caption = tr ("MIDI Input Port Number:");
            const auto w = juce::GlyphArrangement::getStringWidth (f, caption) + 6.0f * scale();
            g.setFont (f);
            g.setColour (theme::colour::textSecondary);
            g.drawText (caption, r.withWidth (w).toNearestInt(), juce::Justification::centredLeft, false);
            g.setFont (theme::font (juce::jmax (8.0f, 11.0f * scale()), true));
            g.setColour (fx.enabled ? theme::colour::textPrimary : theme::colour::textDisabled);
            g.drawText (port >= 0 ? juce::String (port) : juce::String ("..."), r.withTrimmedLeft (w).toNearestInt(), juce::Justification::centredLeft, false);
            return;
        }
        if (id == "equosend") // FL's send track box: "---" (none) or the insert's number, with its up / down marks
        {
            const auto t = fx.sendTracks[0];
            g.setColour (theme::colour::bg0.withAlpha (0.35f));
            g.fillRect (r);
            g.setColour (t > 0 ? theme::colour::textPrimary : theme::colour::textSecondary);
            g.setFont (theme::font (juce::jmax (8.0f, 10.0f * scale())));
            g.drawText (t > 0 ? juce::String (t) : juce::String ("---"), r.withTrimmedRight (9.0f * scale()).toNearestInt(), juce::Justification::centredRight, false);
            const auto ar = r.removeFromRight (9.0f * scale()).reduced (1.5f * scale(), r.getHeight() * 0.22f);
            juce::Path up, down;
            up.addTriangle (ar.getX(), ar.getCentreY() - 1.0f, ar.getRight(), ar.getCentreY() - 1.0f, ar.getCentreX(), ar.getY());
            down.addTriangle (ar.getX(), ar.getCentreY() + 1.0f, ar.getRight(), ar.getCentreY() + 1.0f, ar.getCentreX(), ar.getBottom());
            g.fillPath (up);
            g.fillPath (down);
            return;
        }
        if (id == "tsssend0" || id == "tsssend1")
        {
            paintTransmitterSend (g, r, fx, id == "tsssend0" ? 0 : 1);
            return;
        }

        auto grid = [&] (int columns, int rows) {
            g.setColour (colour::gridBeat);
            for (int i = 1; i < columns; ++i)
                g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / (float) columns), inner.getY(), inner.getBottom());
            for (int i = 1; i < rows; ++i)
                g.drawHorizontalLine ((int) (inner.getY() + inner.getHeight() * (float) i / (float) rows), inner.getX(), inner.getRight());
        };

        auto strokeCurve = [&] (const std::function<float (float)>& f, juce::Rectangle<float> area, bool fillBelow) {
            juce::Path p;
            const auto steps = juce::jmax (16, (int) area.getWidth() / 2);
            for (int i = 0; i <= steps; ++i)
            {
                const auto x = (float) i / (float) steps;
                const auto y = juce::jlimit (0.0f, 1.0f, f (x));
                const juce::Point<float> pt { area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y };
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
            if (fillBelow)
            {
                auto fill = p;
                fill.lineTo (area.getRight(), area.getBottom());
                fill.lineTo (area.getX(), area.getBottom());
                fill.closeSubPath();
                g.setGradientFill (juce::ColourGradient (curveColour.withAlpha (0.28f), 0.0f, area.getY(), curveColour.withAlpha (0.02f), 0.0f, area.getBottom(), false));
                g.fillPath (fill);
            }
            skin::glowStroke (g, p, curveColour, juce::jmax (1.5f, 2.0f * s), fx.enabled ? 1.0f : 0.0f);
        };

        if (id == "limiter" || id == "compressor" || id == "dynamics")
        {
            // static transfer curve: input level (x, −60…0 dB) to output level (y)
            grid (6, 4);
            const auto threshold01 = id == "limiter" ? param01 ("Ceiling") : param01 (id == "compressor" ? "Threshold" : "Comp. threshold");
            const auto ratio01 = id == "limiter" ? 1.0f : param01 ("Ratio");
            const auto t = juce::jlimit (0.05f, 1.0f, threshold01);
            const auto ratio = 1.0f + ratio01 * 19.0f;
            strokeCurve ([t, ratio] (float x) { return x <= t ? x : t + (x - t) / ratio; }, inner, true);
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) (inner.getBottom() - inner.getHeight() * t), inner.getX(), inner.getRight());
            // round 170c (the "shell" audit: FL's display is live, this one was a still picture): the signal's
            // input → output peaks of the last ~1.5 s as a fading trail of dots on the curve's axes (−60 … 0 dB)
            if (! node.locate)
                for (int k = 0; k < (int) levelTrail.size(); ++k)
                {
                    const auto& [inDb, outDb] = levelTrail[(std::size_t) ((levelTrailAt + k) % (int) levelTrail.size())];
                    if (inDb <= -59.5f) continue;
                    const auto x = juce::jlimit (0.0f, 1.0f, (inDb + 60.0f) / 60.0f), y = juce::jlimit (0.0f, 1.0f, (outDb + 60.0f) / 60.0f);
                    const auto age = (float) (k + 1) / (float) levelTrail.size(); // 1 = newest
                    const auto d = (k == (int) levelTrail.size() - 1 ? 7.0f : 3.0f) * s;
                    g.setColour ((k == (int) levelTrail.size() - 1 ? colour::textPrimary : curveColour).withAlpha (0.15f + 0.85f * age * age));
                    g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ inner.getX() + inner.getWidth() * x, inner.getBottom() - inner.getHeight() * y }));
                }
        }
        else if (id == "shaper") // the feedback saturation: drive from the level, knee and symmetry
        {
            grid (4, 4);
            const auto drive = 1.0f + 5.0f * param01 (item.param != nullptr ? item.param : "Distortion");
            const auto knee = 0.5f + 3.0f * param01 ("Hardness");
            const auto bias = (param01 ("Symmetry") - 0.5f) * 0.6f;
            strokeCurve ([drive, knee, bias] (float x) {
                const auto u = (x * 2.0f - 1.0f) * drive + bias;
                const auto y = u / std::pow (1.0f + std::pow (std::abs (u), knee * 2.0f), 1.0f / (knee * 2.0f));
                return 0.5f + 0.5f * y;
            }, inner, false);
        }
        else if (id == "multiband")
        {
            // the three bands between the crossovers
            grid (8, 4);
            const auto low = param01 ("Low crossover"), high = param01 ("High crossover");
            const juce::Colour bandColours[] = { peqview::bandColour (0), peqview::bandColour (2), peqview::bandColour (5) };
            const float edges[] = { 0.0f, juce::jlimit (0.05f, 0.95f, low), juce::jlimit (0.05f, 0.95f, juce::jmax (low + 0.02f, high)), 1.0f };
            for (int b = 0; b < 3; ++b)
            {
                const auto band = juce::Rectangle<float> (inner.getX() + inner.getWidth() * edges[b], inner.getY() + inner.getHeight() * 0.35f,
                                                          inner.getWidth() * (edges[b + 1] - edges[b]), inner.getHeight() * 0.65f);
                g.setColour (bandColours[b].withAlpha (fx.enabled ? 0.18f : 0.06f));
                g.fillRect (band);
                g.setColour (bandColours[b].withAlpha (fx.enabled ? 0.9f : 0.3f));
                g.drawHorizontalLine ((int) band.getY(), band.getX(), band.getRight());
            }
        }
        else if (id == "decayeq") // round tp172b4: LuxeVerb's Space engine — the decay time across the spectrum
        {
            paintDecayEq (g, r, fx);
            return;
        }
        else if (id == "envelope") // decay shape of a reverb / delay tail
        {
            grid (8, 4);
            const auto decay = 0.15f + param01 (item.param != nullptr ? item.param : "Decay") * 3.0f;
            strokeCurve ([decay] (float x) { return std::exp (-x * 4.0f / decay) * 0.9f; }, inner, true);
        }
        else if (id == "meter") // level meter well: the slot's live input and output peaks (round 170c; before: an empty grid)
        {
            g.setColour (colour::gridBeat);
            for (int i = 1; i < 12; ++i)
                g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 12.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
            const auto to01 = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
            const auto vertical = r.getHeight() >= r.getWidth();
            auto area = r.reduced (3.0f);
            const auto gap = 2.0f;
            for (int k = 0; k < 2; ++k)
            {
                const auto bar = vertical ? juce::Rectangle<float> (area.getX() + (area.getWidth() + gap) * 0.5f * (float) k, area.getY(), (area.getWidth() - gap) * 0.5f, area.getHeight())
                                          : juce::Rectangle<float> (area.getX(), area.getY() + (area.getHeight() + gap) * 0.5f * (float) k, area.getWidth(), (area.getHeight() - gap) * 0.5f);
                skin::meter (g, bar, to01 (levelDb[(std::size_t) k]), to01 (levelHoldDb[(std::size_t) k]), vertical);
            }
        }
        else if (id == "clip") // hard / soft clipping at the threshold
        {
            grid (4, 4);
            const auto t = juce::jlimit (0.02f, 1.0f, param01 (item.param != nullptr ? item.param : "Threshold"));
            const auto soft = param01 ("Type") >= 0.5f;
            const auto pre = 1.0f + 30.0f * param01 ("Pre amp") * param01 ("Pre amp");
            const auto post = 0.25f + 0.75f * param01 ("Post gain");
            strokeCurve ([t, soft, pre, post] (float x) {
                const auto u = x * pre;
                return post * (soft ? t * std::tanh (u / t) : juce::jmin (u, t));
            }, inner, false);
        }
        else if (id == "softclip") // linear up to the threshold, then a soft knee into the ceiling
        {
            grid (4, 4);
            const auto t = juce::jlimit (0.05f, 0.98f, 0.3f + 0.7f * param01 ("Threshold"));
            const auto post = 0.4f + 0.6f * param01 ("Post");
            strokeCurve ([t, post] (float x) {
                const auto y = x <= t ? x : t + (1.0f - t) * std::tanh ((x - t) / (1.0f - t));
                return y * post;
            }, inner, false);
        }
        else if (id == "sgamount" || id == "sgpresets")
        {
            paintSoundgoodizer (g, id, r, fx);
            return;
        }
        else if (id == "geminipan") // Gemini Doubler: where the voices and the direct signal sit (half circle, gain = radius)
        {
            using namespace voicetools::gemini;
            const auto c = juce::Point<float> (inner.getCentreX(), inner.getBottom() - 4.0f * s);
            const auto radius = juce::jmin (inner.getWidth() * 0.5f, inner.getHeight()) - 8.0f * s;
            g.setColour (colour::gridBeat);
            for (int k = 1; k <= 3; ++k)
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, radius * (float) k / 3.0f, radius * (float) k / 3.0f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                g.strokePath (arc, juce::PathStrokeType (1.0f));
            }
            for (const auto deg : { -45.0f, 45.0f })
            {
                const auto a = juce::degreesToRadians (deg);
                g.drawLine (c.x, c.y, c.x + radius * std::sin (a), c.y - radius * std::cos (a), 1.0f);
            }
            // gain −48 … 0 dB → radius 0 … 1; the dot's angle is its pan (±45° = the hard sides)
            auto dot = [&] (float pan01, float gain01, bool active, juce::Colour col, const juce::String& text) {
                const auto a = juce::degreesToRadians ((float) panDegrees (pan01)); // ±45° = the diagonals
                const auto rr = radius * juce::jlimit (0.05f, 1.0f, gain01);
                const auto pt = juce::Point<float> (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
                g.setColour (active ? col : colour::textDisabled);
                g.fillEllipse (juce::Rectangle<float> (14.0f * s, 14.0f * s).withCentre (pt));
                g.setColour (colour::bg0);
                g.setFont (font (9.0f * s, true));
                g.drawText (text, juce::Rectangle<float> (14.0f * s, 14.0f * s).withCentre (pt), juce::Justification::centred, false);
            };
            dot (param01 ("Direct pan"), param01 ("Direct gain"), param01 ("Direct") >= 0.5f, colour::textPrimary, "D");
            dot (param01 ("V1 pan"), param01 ("V1 gain"), param01 ("V1 on") >= 0.5f, curveColour, "1");
            dot (param01 ("V2 pan"), param01 ("V2 gain"), param01 ("V2 on") >= 0.5f, curveColour.withRotatedHue (0.12f), "2");
            // round 135w: voices 3 and 4 (shown when on)
            if (param01 ("V3 on") >= 0.5f)
                dot (param01 ("V3 pan"), param01 ("V3 gain"), true, curveColour.withRotatedHue (0.24f), "3");
            if (param01 ("V4 on") >= 0.5f)
                dot (param01 ("V4 pan"), param01 ("V4 gain"), true, curveColour.withRotatedHue (0.36f), "4");
        }
        else if (id == "geminimap") // Gemini Doubler: each voice's delay (across, 0 … 100 ms) against its detune (up, ±100 c)
        {
            grid (5, 4);
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
            g.setColour (colour::textSecondary);
            g.setFont (font (8.5f * s));
            for (int k = 0; k <= 5; ++k)
                g.drawText (juce::String (k * 20), juce::Rectangle<float> (inner.getX() + inner.getWidth() * (float) k / 5.0f - 12.0f * s, inner.getBottom() - 11.0f * s, 24.0f * s, 10.0f * s),
                            juce::Justification::centred, false);
            // round 137: the detune scale up the left side (+100 / 0 / −100 cents)
            static const std::pair<float, const char*> centsMarks[] { { 0.0f, "+100" }, { 0.5f, "0" }, { 1.0f, "-100" } };
            for (const auto& [frac, text] : centsMarks)
                g.drawText (text, juce::Rectangle<float> (inner.getX() + 1.0f * s, inner.getY() + inner.getHeight() * frac - (frac > 0.9f ? 22.0f : frac > 0.1f ? 5.0f : 0.0f) * s, 30.0f * s, 10.0f * s),
                            juce::Justification::centredLeft, false);
            for (int k = 0; k < 4; ++k) // round 135w: four voices (3 and 4 only when on)
            {
                const juce::String pre = "V" + juce::String (k + 1) + " ";
                if (k >= 2 && param01 ((pre + "on").toRawUTF8()) < 0.5f)
                    continue;
                const auto x = inner.getX() + inner.getWidth() * param01 ((pre + "delay").toRawUTF8());
                const auto y = inner.getBottom() - inner.getHeight() * param01 ((pre + "detune").toRawUTF8());
                const auto col = curveColour.withRotatedHue (0.12f * (float) k);
                g.setColour ((param01 ((pre + "on").toRawUTF8()) >= 0.5f ? col : colour::textDisabled));
                // the LFO's reach on the pitch as a bar through the dot
                const auto depth = std::abs (param01 ((pre + "depth").toRawUTF8()) - 0.5f) * 2.0f * inner.getHeight();
                if (depth > 1.0f)
                    g.fillRect (juce::Rectangle<float> (3.0f * s, depth).withCentre ({ x, y }));
                g.fillEllipse (juce::Rectangle<float> (12.0f * s, 12.0f * s).withCentre ({ x, y }));
            }
        }
        else if (id == "geminieq") // Gemini Doubler: the voices' low / high shelves
        {
            using namespace voicetools::gemini;
            grid (4, 4);
            const bool lo = param01 ("Low shelf") >= 0.5f, hi = param01 ("High shelf") >= 0.5f;
            // round 165w: the law's range over the height (measured: −88 … +12 dB; round 124's: −30 … +12) and its shapes
            const bool measured = param01 ("EQ law") >= 0.5f;
            const auto floorDb = measured ? -88.0 : -30.0, spanDb = measured ? 100.0 : 42.0;
            const auto lg = lo ? shelfDb (param01 ("Low gain"), measured) : 0.0, hg = hi ? shelfDb (param01 ("High gain"), measured) : 0.0;
            const auto lf = freqHz (param01 ("Low freq")), hf = freqHz (param01 ("High freq"));
            strokeCurve ([=] (float x) {
                const auto f = 20.0 * std::pow (1000.0, (double) x);
                const auto db = shelfResponseDb (f, lf, lg, false, measured) + shelfResponseDb (f, hf, hg, true, measured);
                return (float) std::clamp ((db - floorDb) / spanDb, 0.0, 1.0);
            }, inner, true);
            // round 137: the L / H markers (drag: frequency / gain; double-click: on / off) and the frequency labels
            g.setFont (font (8.5f * s));
            g.setColour (colour::textSecondary);
            static const std::pair<double, const char*> freqMarks[] { { 62.0, "62" }, { 250.0, "250" }, { 1000.0, "1K" }, { 4000.0, "4K" } };
            for (const auto& [hz, text] : freqMarks)
            {
                const auto x = inner.getX() + inner.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0));
                g.drawText (text, juce::Rectangle<float> (x - 12.0f * s, inner.getBottom() - 11.0f * s, 24.0f * s, 10.0f * s), juce::Justification::centred, false);
            }
            const auto zero = inner.getBottom() - inner.getHeight() * (float) (-floorDb / spanDb);
            g.drawText ("0", juce::Rectangle<float> (inner.getX() + 1.0f * s, zero - 5.0f * s, 14.0f * s, 10.0f * s), juce::Justification::centredLeft, false);
            for (const auto& h : geminiHandles (item))
            {
                const bool isLow = h.on == lowOn, onNow = valueOf (h.on) >= 0.5f;
                const auto box = juce::Rectangle<float> (14.0f * s, 14.0f * s).withCentre (h.at);
                g.setColour (onNow ? curveColour : colour::textDisabled);
                g.fillEllipse (box);
                g.setColour (colour::bg0);
                g.setFont (font (9.0f * s, true));
                g.drawText (isLow ? "L" : "H", box, juce::Justification::centred, false);
            }
        }
        else if (id == "xy") // a pad: `param` across, `param2` up (drag the point)
        {
            grid (4, 4);
            const auto x = inner.getX() + inner.getWidth() * param01 (item.param);
            const auto y = inner.getBottom() - inner.getHeight() * param01 (item.param2);
            g.setColour (curveColour);
            g.drawEllipse (juce::Rectangle<float> (10.0f * s, 10.0f * s).withCentre ({ x, y }), juce::jmax (1.5f, 2.0f * s));
        }
        else if (id == "voices") // the chorus voices spread across the stereo field (a static picture)
        {
            g.setColour (colour::gridBeat);
            g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
            const auto spread = 0.2f + 0.8f * param01 ("Movement");
            for (int i = 0; i < 8; ++i)
            {
                const auto t = (float) i / 7.0f;
                const auto x = inner.getCentreX() + (t - 0.5f) * inner.getWidth() * spread;
                g.setColour (curveColour.withAlpha (0.35f + 0.5f * (1.0f - std::abs (t - 0.5f))));
                g.fillRoundedRectangle (juce::Rectangle<float> (3.0f * s, inner.getHeight() * (0.3f + 0.4f * std::abs (std::sin ((float) i * 1.7f)))).withCentre ({ x, inner.getCentreY() }), 1.5f);
            }
        }
        else if (id == "panpad") // pan (x) and volume (y) as a crosshair
        {
            grid (6, 6);
            const auto x = inner.getX() + inner.getWidth() * param01 ("Pan");
            const auto y = inner.getBottom() - inner.getHeight() * param01 ("Volume");
            g.setColour (curveColour);
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
            g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ x, y }));
        }
        else if (id == "waveshaper") // round 165: FL's point editor (the map: input across, level up)
        {
            grid (8, 8);
            g.setColour (colour::gridBar);
            g.drawLine ({ inner.getBottomLeft(), inner.getTopRight() }, 1.0f); // the identity, for reference
            const auto ws = wsState();
            const auto& table = wsTable (ws.points, wsUnipolar());
            strokeCurve ([&table, uni = wsUnipolar()] (float x) { return flshape::waveShaperLevelAt (table, uni, x); }, inner, false);
            const auto times = flshape::waveShaperTimes (ws.points);
            // tension handles (hollow, between the points), then the points
            for (std::size_t i = 1; i < ws.points.size(); ++i)
                if (const auto h = wsHandlePos (inner, ws, times, (int) i); h.x > -1.0f)
                {
                    const auto hot = (int) i == wsHoverHandle || (int) i == wsTension;
                    g.setColour (hot ? colour::textPrimary : curveColour.withAlpha (0.85f));
                    g.drawEllipse (juce::Rectangle<float> (8.0f * s, 8.0f * s).withCentre (h), juce::jmax (1.0f, 1.3f * s));
                }
            for (std::size_t i = 0; i < ws.points.size(); ++i)
            {
                const auto at = wsPos (inner, times[i], ws.points[i].y);
                const auto hot = (int) i == wsHoverPoint || (int) i == wsPoint;
                g.setColour (colour::bg0);
                g.fillEllipse (juce::Rectangle<float> (11.0f * s, 11.0f * s).withCentre (at));
                g.setColour (hot ? colour::textPrimary : curveColour);
                g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre (at));
            }
        }
        else if (id == "room") // nested walls: the room size and the decay
        {
            const auto size = 0.25f + 0.7f * param01 (item.param != nullptr ? item.param : "Room size");
            const auto c = inner.getCentre();
            for (int i = 0; i < 6; ++i)
            {
                const auto k = size * (1.0f - 0.14f * (float) i);
                const auto box = juce::Rectangle<float> (inner.getWidth() * k, inner.getHeight() * k * 0.8f).withCentre (c);
                g.setColour (curveColour.withAlpha (0.75f - 0.11f * (float) i));
                g.drawRoundedRectangle (box, 4.0f * s, juce::jmax (1.0f, 1.5f * s));
            }
        }
        else if (id == "maxcurve") // Maximus: the selected band's compression curve, edited with FL's gestures (round 168x)
            paintMaximusCurve (g, inner, fx);
        else if (id == "lpcurve" || id == "cvcurve" || id == "vxcurve") // Love Philter's articulators, Convolver's EQ, Vocodex's maps (round 173d3)
            paintPhilterCurve (g, inner, fx);
        else if (id == "vxmapsel") // Vocodex: the map shown (round 173d3)
            paintVxMapSelector (g, r);
        else if (id == "maxbands") // Maximus: the three bands over 20 Hz…20 kHz with their crossovers and the low cut
        {
            const auto raw = [&] (int p) { return maximusfx::stateParam (fx.state, p).value_or (maximusfx::stateParams()[(std::size_t) p].defaultValue); };
            const auto xOf = [&] (double hz) { return inner.getX() + inner.getWidth() * (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0)); };
            auto lo = 30.0 * std::pow (533.33, raw (0) / 65536.0), hi = 30.0 * std::pow (533.33, raw (2) / 65536.0);
            if (lo > hi)
                std::swap (lo, hi);
            const float edges[4] { inner.getX(), xOf (lo), xOf (hi), inner.getRight() };
            static const char* names[3] { "Low", "Mid", "High" };
            for (int b = 0; b < 3; ++b)
            {
                const auto mode = raw (7 + 13 * b);
                const auto area = juce::Rectangle<float> (edges[b], inner.getY(), edges[b + 1] - edges[b], inner.getHeight());
                const auto c = peqview::bandColour (b);
                g.setColour (c.withAlpha (! fx.enabled || mode >= 2 ? 0.05f : (b == page ? 0.24f : 0.12f)));
                g.fillRect (area);
                g.setColour (b == page ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                g.drawFittedText (caps (tr (names[b])) + (mode == 2 ? " · " + tr ("Muted") : mode == 3 ? " · " + tr ("Off") : juce::String()),
                                  area.reduced (2.0f * s).withHeight (14.0f * s).toNearestInt(), juce::Justification::centred, 1, 0.6f);
            }
            g.setColour (colour::textSecondary);
            for (int k = 1; k <= 2; ++k)
                g.drawVerticalLine ((int) edges[k], inner.getY(), inner.getBottom());
            if (raw (6) >= 0) // low cut
            {
                const auto x = xOf (5.0 * std::pow (16.0, raw (6) / 65536.0));
                g.setColour (accent.withAlpha (0.6f));
                g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            }
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
            for (const auto hz : { 100.0, 1000.0, 10000.0 })
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (30.0f * s, 12.0f * s).withCentre ({ xOf (hz), inner.getBottom() - 6.0f * s }).toNearestInt(),
                            juce::Justification::centred, false);
        }
        else if (id == "eq")
        {
            paintEq (g, r, fx);
            return;
        }
        else if (id == "grossbeat" || id.startsWith ("gb"))
        {
            paintGrossBeat (g, id, r, fx, item);
            return;
        }
        else if (id == "eqinfo") // the selected band, or how to use the graph
        {
            g.setColour (colour::bg1);
            g.fillRect (r);
            g.setFont (font (juce::jmax (9.0f, 11.0f * s)));
            if (eqSelected >= 0)
            {
                g.setColour (peqview::bandColour (eqSelected));
                g.drawFittedText (peqview::bandText (fx.params, eqSelected), r.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
            }
            else
            {
                g.setColour (colour::textSecondary);
                g.drawFittedText (tr ("Drag a handle (frequency and gain); mouse wheel = Q; right-click = type; double-click = 0 dB"),
                                  r.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
            }
            return;
        }
        else if (id == "filter")
        {
            grid (8, 4);
            const auto cutoff = param01 ("Cutoff");
            const auto reso = param01 ("Resonance");
            strokeCurve ([cutoff, reso] (float x) {
                const auto d = (x - cutoff) * 8.0f;
                const auto bump = reso * 0.35f * std::exp (-d * d);
                return 0.6f + bump - (x > cutoff ? (x - cutoff) * 2.2f : 0.0f);
            }, inner, true);
        }
        else if (id == "pitchgraph") // LYNX Tune: detected pitch, target, correction and the scrolling pitch graph
        {
            paintPitchDisplay (g, r, fx);
            return;
        }
        else if (id == "pitchtrace") // LYNX Tune (round 120): the scrolling pitch graph alone
        {
            paintPitchDisplay (g, r, fx, false);
            return;
        }
        else if (id == "pitchring") // LYNX Tune (round 120): the correction meter
        {
            paintPitchRing (g, r, fx);
            return;
        }
        else if (id == "tunekeys") // LYNX Tune (round 128): the keyboard with BYPASS / REMOVE and LATCH
        {
            paintTuneKeys (g, r, fx);
            return;
        }
        else if (id == "tuneautokey") // LYNX Tune (round 128): AUTO KEY
        {
            paintAutoKey (g, r);
            return;
        }
        else if (id == "hwping") // the hardware insert (round 164d): the round trip, the ping's state, what is missing
        {
            paintHwPing (g, r, fx);
            return;
        }
        else if (id == "grleds") // round 165: a ladder of reduction LEDs (Ridge Strip: the console's 3 / 6 / 10 / 14 / 20 dB)
        {
            const auto db = juce::jlimit (0.0f, 1.0f, liveMeter (item.steps)) * 30.0f;
            auto area = r.reduced (1.0f * s);
            const auto caption = area.removeFromBottom (14.0f * s);
            static const int steps[] { 20, 14, 10, 6, 3 };
            const auto h = area.getHeight() / 5.0f;
            g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
            for (int k = 0; k < 5; ++k)
            {
                const auto cell = juce::Rectangle<float> (area.getX(), area.getY() + h * (float) k, area.getWidth(), h);
                // round tp172c: the LED keeps to the ladder's width (a tall ladder kept growing it over its numbers)
                const auto d = juce::jmin (cell.getHeight() * 0.5f, cell.getWidth() * 0.34f);
                const auto led = juce::Rectangle<float> (d, d).withCentre ({ cell.getX() + 2.0f * s + d * 0.5f, cell.getCentreY() });
                const bool lit = fx.enabled && db >= (float) steps[k] - 0.05f;
                g.setColour (lit ? accent : colour::gridBeat);
                g.fillEllipse (led);
                if (lit)
                {
                    g.setColour (accent.withAlpha (0.35f));
                    g.drawEllipse (led.expanded (1.5f * s), 1.0f * s);
                }
                g.setColour (colour::textSecondary);
                g.drawText (juce::String (steps[k]), cell.withTrimmedLeft (led.getRight() - cell.getX() + 2.0f * s), juce::Justification::centredLeft, false);
            }
            if (item.label != nullptr && *item.label != 0)
            {
                g.setColour (colour::textSecondary);
                g.drawText (caps (tr (item.label)), caption, juce::Justification::centred, false);
            }
            return;
        }
        else if (id == "grmeter") // live gain reduction of the mixing suite (0..30 dB, from the right like a VU's GR)
        {
            const auto amount = juce::jlimit (0.0f, 1.0f, liveMeter (item.steps));
            const auto bar = r.reduced (1.5f * s);
            if (bar.getHeight() > 2.0f * bar.getWidth()) // a tall meter (Quell): the reduction grows downwards, 0 dB at the top
            {
                if (editorStyle() == skin::Style::themed && kind == EffectKind::quellDenoiser)
                {
                    // round tp172d: the reference's meter — a dark recessed well, a thin soft light bar, the scale
                    // printed on both sides, the held reduction in a small orange read-out
                    const auto box = bar.withTrimmedBottom (22.0f * s);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3035), box.getX(), box.getY(), juce::Colour (0xff15171a), box.getX(), box.getBottom(), false));
                    g.fillRoundedRectangle (box, 6.0f * s);
                    g.setColour (juce::Colours::black.withAlpha (0.6f));
                    g.drawRoundedRectangle (box, 6.0f * s, 1.2f);
                    const auto well = box.reduced (box.getWidth() * 0.36f, 12.0f * s);
                    g.setColour (juce::Colour (0xff050607));
                    g.fillRoundedRectangle (well.expanded (3.0f * s), 3.0f * s);
                    g.setFont (font (juce::jmax (6.5f, 8.0f * s)));
                    for (int db = 0; db <= 30; db += 6)
                    {
                        const auto y = well.getY() + well.getHeight() * (float) db / 30.0f;
                        g.setColour (juce::Colour (0xffb9bec4));
                        g.drawHorizontalLine ((int) y, box.getX() + 4.0f * s, well.getX() - 6.0f * s);
                        g.drawHorizontalLine ((int) y, well.getRight() + 6.0f * s, box.getRight() - 4.0f * s);
                        g.drawText (juce::String (-db), juce::Rectangle<float> (box.getX() + 2.0f * s, y - 10.0f * s, well.getX() - box.getX() - 8.0f * s, 9.0f * s), juce::Justification::centredRight, false);
                        g.drawText (juce::String (-db), juce::Rectangle<float> (well.getRight() + 6.0f * s, y - 10.0f * s, box.getRight() - well.getRight() - 8.0f * s, 9.0f * s), juce::Justification::centredLeft, false);
                    }
                    const auto fill = well.reduced (well.getWidth() * 0.25f, 0.0f).withHeight (well.getHeight() * amount);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8ecef).withAlpha (fx.enabled ? 0.75f : 0.3f), fill.getX(), 0.0f, juce::Colour (0xff8a9096).withAlpha (fx.enabled ? 0.6f : 0.25f), fill.getRight(), 0.0f, false));
                    g.fillRect (fill);
                    grHold = juce::jmax (grHold, amount);
                    const auto readout = juce::Rectangle<float> (bar.getCentreX() - 22.0f * s, bar.getBottom() - 18.0f * s, 44.0f * s, 16.0f * s);
                    g.setColour (juce::Colour (0xff070809));
                    g.fillRoundedRectangle (readout, 3.0f * s);
                    g.setColour (skin::valueColour (false));
                    g.drawText (grHold > 0.0005f ? juce::String (-grHold * 30.0f, 1) : juce::String ("0.0"), readout, juce::Justification::centred, false);
                    return;
                }
                const auto well = bar.reduced (0.0f, 4.0f * s).withTrimmedBottom (14.0f * s);
                g.setColour (colour::gridBeat);
                g.setFont (font (8.5f * s));
                for (int db = 0; db <= 30; db += 6)
                {
                    const auto y = well.getY() + well.getHeight() * (float) db / 30.0f;
                    g.setColour (colour::gridBeat);
                    g.drawHorizontalLine ((int) y, well.getX(), well.getRight());
                    g.setColour (colour::textSecondary);
                    g.drawText (juce::String (-db), juce::Rectangle<float> (well.getX(), y - 10.0f * s, 22.0f * s, 10.0f * s), juce::Justification::centredLeft, false);
                }
                g.setColour ((fx.enabled ? accent : colour::textDisabled).withAlpha (0.85f));
                g.fillRoundedRectangle (well.withTrimmedLeft (well.getWidth() * 0.35f).withHeight (well.getHeight() * amount), radius::sm);
                // round 137: the read-out holds the deepest reduction (the reference's peak box; click the meter = reset)
                grHold = juce::jmax (grHold, amount);
                g.setColour (colour::textSecondary);
                g.drawText ((grHold > 0.0005f ? juce::String (-grHold * 30.0f, 1) : juce::String ("0.0")) + " dB", bar.withTop (bar.getBottom() - 14.0f * s), juce::Justification::centred, false);
                return;
            }
            g.setColour (colour::gridBeat);
            for (int db = 3; db < 30; db += 3)
                g.drawVerticalLine ((int) (bar.getRight() - bar.getWidth() * (float) db / 30.0f), bar.getY(), bar.getBottom());
            g.setColour ((fx.enabled ? accent : colour::textDisabled).withAlpha (0.85f));
            g.fillRoundedRectangle (bar.withLeft (bar.getRight() - bar.getWidth() * amount), radius::sm);
            if (item.label != nullptr && *item.label != 0)
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (juce::jmax (7.0f, juce::jmin (10.0f * s, bar.getHeight() * 0.95f)), true));
                g.drawText (caps (tr (item.label)) + "  " + juce::String (amount * 30.0f, 1) + " dB", bar.reduced (3.0f * s, 0.0f),
                            juce::Justification::centredLeft, false);
            }
            return;
        }
        else if (id == "faderscale") // round 137: a 0 … 100 scale beside a tall fader (the reference prints it on both sides)
        {
            auto area = r.withTrimmedBottom (13.0f * s);
            const auto top = area.getY() + 5.0f * s, bottom = area.getBottom() - 5.0f * s;
            g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
            for (int k = 0; k <= 100; k += 10)
            {
                const auto y = bottom - (bottom - top) * (float) k / 100.0f;
                const auto right = item.steps == 1; // round tp172d: the scale on the fader's right side, mirrored
                g.setColour (colour::gridBeat);
                if (right) g.drawHorizontalLine ((int) y, area.getX(), area.getX() + 6.0f * s);
                else g.drawHorizontalLine ((int) y, area.getRight() - 6.0f * s, area.getRight());
                g.setColour (skin::captionColour().getAlpha() > 0 && editorStyle() == skin::Style::themed ? skin::captionColour().withAlpha (0.75f) : colour::textSecondary);
                g.drawText (juce::String (k), juce::Rectangle<float> (right ? area.getX() + 8.0f * s : area.getX(), y - 5.0f * s, area.getWidth() - 8.0f * s, 10.0f * s),
                            right ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
            }
            return;
        }
        else if (id == "panpos") // round 135: where the panner has the sound now (the rhythmic panners' L … C … R strip)
        {
            const auto at = juce::jlimit (0.0f, 1.0f, liveMeter (1));
            constexpr int segments = 29;
            const auto area = r.reduced (2.0f * s);
            g.setColour (colour::bg0);
            g.fillRoundedRectangle (area, 3.0f * s);
            const auto w = area.getWidth() / (float) segments;
            for (int k = 0; k < segments; ++k)
            {
                const auto centre = ((float) k + 0.5f) / (float) segments;
                const auto d = std::abs (centre - at) * (float) segments;
                const auto glow = juce::jlimit (0.0f, 1.0f, 1.0f - d / 2.5f);
                const auto cell = juce::Rectangle<float> (area.getX() + w * (float) k, area.getY(), w, area.getHeight()).reduced (1.0f * s, 3.0f * s);
                g.setColour (accent.withAlpha (0.10f + 0.90f * glow * glow));
                g.fillRoundedRectangle (cell, 1.5f * s);
            }
            g.setColour (colour::textSecondary.withAlpha (0.5f));
            g.drawVerticalLine ((int) area.getCentreX(), area.getY(), area.getBottom());
            return;
        }
        else if (id == "iometer") // round 129: input and output peak meters with peak holds and read-outs (−48 … 0 dBFS)
        {
            // the originals' level meters (SSL E-Channel's IN / OUT meter, RComp's input and output, Doubler's output)
            const auto fracOf = [] (float linear) { return juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (juce::jmax (1e-6f, linear)) + 48.0f) / 48.0f); };
            const auto base = item.steps > 0 ? item.steps : 2; // round 135w: the engine's meter outputs (LYNX Tune: 3 / 4)
            const auto only = item.colour; // round tp172c7: 0 / 1 = the input or the output bar alone (Kiln's side columns)
            if (only != 1) ioMeters[0].update (fracOf (liveMeter (base)), 0.02f);
            if (only != 0) ioMeters[1].update (fracOf (liveMeter (base + 1)), 0.02f);
            ioClip = ioClip || liveMeter (base + 1) >= 0.999f; // round 135w: the clip lamp (Doubler's), click to reset
            const auto labelH = 13.0f * s, readH = 13.0f * s;
            auto area = r.reduced (2.0f * s);
            const auto top = area.removeFromTop (labelH), bottom = area.removeFromBottom (readH);
            const auto scaleW = 18.0f * s;
            auto bars = area.withTrimmedLeft (scaleW);
            g.setFont (font (juce::jmax (7.0f, 8.0f * s)));
            for (int db = 0; db >= -48; db -= 12)
            {
                const auto y = bars.getY() + bars.getHeight() * (float) -db / 48.0f;
                g.setColour (colour::gridBeat);
                g.drawHorizontalLine ((int) y, bars.getX(), bars.getRight());
                g.setColour (colour::textSecondary);
                g.drawText (juce::String (db), juce::Rectangle<float> (area.getX(), y - 6.0f * s, scaleW - 2.0f * s, 12.0f * s), juce::Justification::centredRight, false);
            }
            const auto w = bars.getWidth() / (only == 0 || only == 1 ? 1.0f : 2.0f);
            const char* names[] = { TR_NOOP ("In"), TR_NOOP ("Out") };
            if (kind == EffectKind::hardwareInsert) // round 164d: what goes to the gear and what comes back
                names[0] = TR_NOOP ("Send"), names[1] = TR_NOOP ("Return");
            for (int k = (only == 1 ? 1 : 0); k < (only == 0 ? 1 : 2); ++k)
            {
                const auto bar = juce::Rectangle<float> (bars.getX() + w * (float) (only == 1 ? 0 : k), bars.getY(), w, bars.getHeight()).reduced (3.0f * s, 0.0f);
                skin::meter (g, bar, ioMeters[(std::size_t) k].level, ioMeters[(std::size_t) k].peak, true);
                const auto column = juce::Rectangle<float> (bar.getX() - 4.0f * s, 0.0f, bar.getWidth() + 8.0f * s, 0.0f);
                g.setColour (colour::textSecondary);
                g.setFont (font (juce::jmax (7.0f, 8.5f * s), true));
                if (k == 1 && ioClip) // round 135w: the output clipped since the last click
                {
                    g.setColour (juce::Colour (0xffe5383b));
                    g.fillRoundedRectangle (column.withY (top.getY()).withHeight (labelH).reduced (4.0f * s, 1.0f * s), radius::sm);
                    g.setColour (colour::textPrimary);
                }
                g.drawText (caps (tr (names[k])), column.withY (top.getY()).withHeight (labelH), juce::Justification::centred, false);
                const auto peakDb = ioMeters[(std::size_t) k].peak * 48.0f - 48.0f;
                g.setColour (peakDb > -0.1f ? juce::Colour (0xffe5383b) : colour::textPrimary);
                g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
                g.drawText (ioMeters[(std::size_t) k].peak > 0.001f ? juce::String (peakDb, 1) : juce::String ("-inf"),
                            column.withY (bottom.getY()).withHeight (readH), juce::Justification::centred, false);
            }
            return;
        }
        else if (id == "gcmeter") // round 152: RComp's bipolar gain-change meter (+6 … 0 … −12, deeper reductions compressed)
        {
            // the engine's control output 5 = the block's signed gain change in dB (negative = reduction, positive = the
            // upward expansion below 1 : 1); the bar grows from the 0 dB line, the read-out holds the largest change
            const auto change = liveMeter (5);
            gcLevel = std::abs (change) > std::abs (gcLevel) ? change : gcLevel + 0.25f * (change - gcLevel);
            if (std::abs (change) > std::abs (gcHold))
                gcHold = change;
            static constexpr float dbs[] { 9.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -24.0f, -30.0f };
            static constexpr float fracs[] { 0.0f, 0.08f, 0.20f, 0.38f, 0.53f, 0.66f, 0.80f, 0.94f, 1.0f };
            const auto fracOf = [] (float db)
            {
                db = juce::jlimit (-30.0f, 9.0f, db);
                for (int k = 0; k + 1 < 9; ++k)
                    if (db >= dbs[k + 1])
                        return fracs[k] + (fracs[k + 1] - fracs[k]) * (dbs[k] - db) / (dbs[k] - dbs[k + 1]);
                return 1.0f;
            };
            const auto readH = 14.0f * s;
            auto area = r.reduced (2.0f * s);
            const auto bottom = area.removeFromBottom (readH);
            g.setColour (colour::bg3);
            g.fillRoundedRectangle (area, radius::sm * s);
            const auto scaleW = 20.0f * s;
            const auto well = area.withTrimmedLeft (scaleW).reduced (3.0f * s, 4.0f * s);
            g.setFont (font (juce::jmax (7.0f, 8.0f * s)));
            for (const auto db : { 6, 3, 0, -3, -6, -12, -24 })
            {
                const auto y = well.getY() + well.getHeight() * fracOf ((float) db);
                g.setColour (db == 0 ? colour::textSecondary.withAlpha (0.6f) : colour::gridBeat);
                g.drawHorizontalLine ((int) y, well.getX(), well.getRight());
                g.setColour (colour::textSecondary);
                g.drawText (juce::String (db), juce::Rectangle<float> (area.getX(), y - 5.0f * s, scaleW - 2.0f * s, 10.0f * s), juce::Justification::centredRight, false);
            }
            const auto y0 = well.getY() + well.getHeight() * fracOf (0.0f), y1 = well.getY() + well.getHeight() * fracOf (gcLevel);
            g.setColour ((fx.enabled ? accent : colour::textDisabled).withAlpha (gcLevel > 0.0f ? 0.55f : 0.85f));
            g.fillRoundedRectangle (juce::Rectangle<float> (well.getX() + well.getWidth() * 0.2f, std::min (y0, y1), well.getWidth() * 0.6f, std::abs (y1 - y0)), radius::sm);
            g.setColour (colour::textPrimary);
            g.setFont (font (juce::jmax (7.0f, 9.0f * s)));
            g.drawText (std::abs (gcHold) > 0.05f ? juce::String (gcHold, 1) : juce::String ("0.0"), bottom, juce::Justification::centred, false);
            return;
        }
        else if (id == "limitpill") // round 135w: the output limiter's activity pill (RComp's), lit while it limits
        {
            const auto active = liveMeter (4) > 0.001f;
            g.setColour (active ? juce::Colour (0xffe5383b) : colour::bg3);
            g.fillRoundedRectangle (r.reduced (1.0f * s), r.getHeight() * 0.5f);
            return;
        }
        else if (id == "outmeter") // round 135w: an output peak meter with a clip lamp and the highest peak (click: reset)
        {
            // H-Delay's output meter (0 … −48 dB) and peak lamp, DeEsser's output meter (−30 … 0, `steps` = 30)
            const auto range = (float) (item.steps > 0 ? item.steps : 48);
            const auto peakNow = liveMeter (3);
            const auto fracOf = [range] (float linear) { return juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (juce::jmax (1e-6f, linear)) + range) / range); };
            outMeter.update (fracOf (peakNow), 0.02f);
            outMax = juce::jmax (outMax, peakNow);
            outClip = outClip || peakNow >= 0.999f;
            const auto lampH = 10.0f * s, readH = 13.0f * s;
            auto area = r.reduced (2.0f * s);
            const auto lamp = area.removeFromTop (lampH).withSizeKeepingCentre (juce::jmin (area.getWidth(), 16.0f * s), lampH - 2.0f * s);
            area.removeFromTop (2.0f * s);
            const auto bottom = area.removeFromBottom (readH);
            g.setColour (outClip ? juce::Colour (0xffe5383b) : colour::bg3);
            g.fillRoundedRectangle (lamp, radius::sm);
            const auto scaleW = juce::jmin (16.0f * s, area.getWidth() * 0.45f);
            auto bar = area.withTrimmedLeft (scaleW).reduced (2.0f * s, 0.0f);
            g.setFont (font (juce::jmax (6.5f, 7.5f * s)));
            const int stepDb = range > 36.0f ? 12 : 6;
            for (int db = 0; db >= -(int) range; db -= stepDb)
            {
                const auto y = bar.getY() + bar.getHeight() * (float) -db / range;
                g.setColour (colour::gridBeat);
                g.drawHorizontalLine ((int) y, bar.getX(), bar.getRight());
                g.setColour (colour::textSecondary);
                g.drawText (juce::String (db), juce::Rectangle<float> (area.getX(), y - 5.0f * s, scaleW - 1.0f * s, 10.0f * s), juce::Justification::centredRight, false);
            }
            skin::meter (g, bar, outMeter.level, outMeter.peak, true);
            const auto maxDb = 20.0f * std::log10 (juce::jmax (1e-6f, outMax));
            g.setColour (maxDb > -0.1f ? juce::Colour (0xffe5383b) : colour::textPrimary);
            g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
            g.drawText (outMax > 1e-5f ? juce::String (maxDb, 1) : juce::String ("-inf"), bottom, juce::Justification::centred, false);
            return;
        }
        else if (id == "taptempo") // round 129: a tap pad (H-Delay's TAP): tap twice or more to set the delay time
        {
            const auto lit = juce::Time::getMillisecondCounter() - lastTap < 150;
            if (skin::ledReadouts()) // round tp172d: EchoBoy's TAP TEMPO — a square key and the tempo on a red LED read-out
            {
                const auto key = r.withWidth (r.getHeight());
                skin::hwKey (g, key, lit, false);
                auto bpm = tapBpm;
                if (bpm <= 0.0)
                {
                    const auto timeParam = plugui::paramIndex (kind, item.param);
                    const auto t = timeParam >= 0 ? valueText (timeParam, fx.params[(std::size_t) timeParam]) : juce::String();
                    const auto ms = t.endsWith (" ms") ? t.getDoubleValue() : t.endsWith (" s") ? t.getDoubleValue() * 1000.0 : 0.0;
                    bpm = ms > 0.0 ? 60000.0 / ms : 0.0;
                }
                ledReadout (g, r.withTrimmedLeft (key.getWidth() + 4.0f * s).reduced (0.0f, r.getHeight() * 0.18f),
                            bpm > 0.0 ? juce::String (bpm, 1) + " bpm" : juce::String ("--- bpm"), s, fx.enabled);
                return;
            }
            skin::button (g, r, lit, false, accent, radius::sm * s);
            g.setColour (lit ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (juce::jmax (7.0f, 10.0f * s), true));
            g.drawText (tapBpm > 0.0 ? caps (tr ("Tap")) + "  " + juce::String (tapBpm, 1) : caps (tr ("Tap")), r, juce::Justification::centred, false);
            return;
        }
        else if (id == "vumeter") // round 114: an analog-style VU needle showing the gain reduction (the optical leveler)
        {
            // gain reduction (needle down from 0 VU) or the output level (0 VU = −18 dBFS), like the reference's selector
            // round 129: input / gain reduction / output, like the reference's VU DISPLAY selector (round 165x: the "VU display" parameter)
            syncVuMode();
            const auto level = liveMeter (vuMode == 1 ? 2 : 3);
            const auto target = vuMode != 0 ? -juce::jlimit (-3.0f, 30.0f, -(20.0f * std::log10 (juce::jmax (1e-6f, level)) + 18.0f))
                                            : -juce::jlimit (0.0f, 1.0f, liveMeter (item.steps)) * 30.0f;
            vuNeedle += (-target - vuNeedle) * 0.35f; // a meter's inertia (~300 ms at the 30 Hz refresh)
            auto face = r.reduced (2.0f * s);
            if (skin::vuBezel()) // round tp172d (look slice): a chrome bezel round the meter window
            {
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillRoundedRectangle (r.translated (0.0f, 2.0f * s), 10.0f * s);
                const auto dark = skin::vuBezelDark(); // (slice 8: an opto leveler's gunmetal frame)
                g.setGradientFill (juce::ColourGradient (juce::Colour (dark ? 0xff5a5c60 : 0xffe6e8ea), 0.0f, r.getY(), juce::Colour (dark ? 0xff1c1d1f : 0xff4a4c50), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, 10.0f * s);
                g.setGradientFill (juce::ColourGradient (juce::Colour (dark ? 0xff202124 : 0xff2e3034), 0.0f, r.getY(), juce::Colour (dark ? 0xff4b4d51 : 0xffb8babe), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r.reduced (4.0f * s), 8.0f * s);
                g.setColour (juce::Colour (0xff101010));
                g.fillRoundedRectangle (r.reduced (6.0f * s), 7.0f * s);
                face = r.reduced (9.0f * s);
            }
            g.setColour (juce::Colour (0xffd9cfb4).withAlpha (fx.enabled ? 1.0f : 0.5f));
            g.fillRoundedRectangle (face, radius::lg * s);
            if (kind == EffectKind::kilnChannel) // round tp172c7: the console's lamp-lit amber face, warmer in the middle
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6c66a).withAlpha (fx.enabled ? 1.0f : 0.5f), face.getCentreX(), face.getCentreY(),
                                                         juce::Colour (0xffb8822e).withAlpha (fx.enabled ? 1.0f : 0.5f), face.getX(), face.getY(), true));
                g.fillRoundedRectangle (face, radius::lg * s);
            }
            g.setColour (colour::bg0);
            g.drawRoundedRectangle (face, radius::lg * s, 2.0f * s);
            const juce::Point<float> pivot { face.getCentreX(), face.getBottom() + face.getHeight() * 0.18f };
            const auto radiusV = face.getHeight() * 0.92f;
            const auto fracOf = [] (float db) { // the VU scale: −20 … +3 on a linear-voltage law
                const auto lo = std::pow (10.0f, -1.0f), hi = std::pow (10.0f, 3.0f / 20.0f);
                return juce::jlimit (0.0f, 1.0f, (std::pow (10.0f, db / 20.0f) - lo) / (hi - lo));
            };
            const auto angleOf = [&] (float db) { return juce::MathConstants<float>::pi * (-0.27f + 0.54f * fracOf (db)); };
            g.setFont (font (juce::jmax (7.0f, 9.0f * s), true));
            for (const auto db : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
            {
                const auto a = angleOf (db);
                const juce::Point<float> dir { std::sin (a), -std::cos (a) };
                g.setColour (db > 0.0f ? juce::Colour (0xffb3261e) : juce::Colour (0xff2a2622));
                g.drawLine ({ pivot + dir * (radiusV * 0.86f), pivot + dir * (radiusV * 0.95f) }, 1.4f * s);
                if (db == -20.0f || db == -10.0f || db == -5.0f || db == -3.0f || db == 0.0f || db == 3.0f)
                    g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db),
                                juce::Rectangle<float> (24.0f * s, 12.0f * s).withCentre (pivot + dir * (radiusV * 1.04f)).toNearestInt(), juce::Justification::centred, false);
            }
            g.setColour (juce::Colour (0xffb3261e).withAlpha (0.8f));
            juce::Path redArc;
            redArc.addCentredArc (pivot.x, pivot.y, radiusV * 0.9f, radiusV * 0.9f, 0.0f, angleOf (0.0f), angleOf (3.0f), true);
            g.strokePath (redArc, juce::PathStrokeType (3.0f * s));
            g.setColour (juce::Colour (0xff2a2622));
            g.setFont (font (juce::jmax (8.0f, 11.0f * s), true));
            g.drawText ("VU", face.withTop (face.getY() + face.getHeight() * 0.5f).withHeight (16.0f * s).toNearestInt(), juce::Justification::centred, false);
            g.setFont (font (juce::jmax (7.0f, 8.5f * s)));
            g.drawText ((vuMode == 2 ? tr ("Output") : vuMode == 1 ? tr ("Input") : tr ("Gain reduction")) + "  " + juce::String (-vuNeedle, 1) + (vuMode != 0 ? " VU" : " dB"), face.withTop (face.getBottom() - 16.0f * s).toNearestInt(), juce::Justification::centred, false);
            // the needle
            const auto a = angleOf (-vuNeedle);
            const juce::Point<float> dir { std::sin (a), -std::cos (a) };
            g.saveState();
            g.reduceClipRegion (face.toNearestInt());
            g.setColour (juce::Colour (0xff141210));
            g.drawLine ({ pivot + dir * (radiusV * 0.25f), pivot + dir * (radiusV * 0.97f) }, 1.6f * s);
            g.restoreState();
            return;
        }
        else if (id == "ridgeeq") // the strip's EQ + filters (20 Hz .. 20 kHz, ±20 dB)
        {
            grid (10, 4);
            const auto params = fx.params;
            strokeCurve ([&params] (float x) {
                const auto hz = 20.0 * std::pow (1000.0, (double) x);
                return 0.5f + (float) mixsuite::ridge::responseDb (params, 48000.0, hz) / 40.0f;
            }, inner, false);
        }
        else if (id == "ridgecomp") // compressor's static curve (−60 .. 0 dB in and out)
        {
            grid (6, 4);
            const auto params = fx.params;
            strokeCurve ([&params] (float x) {
                return 1.0f + (float) mixsuite::ridge::compressorCurveDb (params, (double) (x - 1.0f) * 60.0) / 60.0f;
            }, inner, true);
        }
        else if (id == "hushband") // the detector's filter (high-pass or band) at the frequency, 1 .. 20 kHz
        {
            grid (8, 4);
            const auto f0 = mixsuite::hush::hpHz (param01 ("Frequency"), param01 ("Range law") >= 0.5f && param01 ("Voicing") < 0.5f); // round 165x
            const auto band = param01 ("Band detector") >= 0.5f;
            strokeCurve ([f0, band] (float x) {
                const auto u = 1000.0 * std::pow (20.0, (double) x) / f0;
                const auto u2 = u * u;
                const auto mag = band ? (u / 1.4) / std::sqrt ((1.0 - u2) * (1.0 - u2) + u2 / (1.4 * 1.4))
                                      : u2 / std::sqrt ((1.0 - u2) * (1.0 - u2) + 2.0 * u2);
                return 0.9f + (float) (20.0 * std::log10 (std::max (1e-6, mag))) / 40.0f;
            }, inner, true);
        }
        else if (id == "embercurve") // the style's transfer curve at the drive (x = ±1: the peaks of a −12 dBFS sine)
        {
            grid (4, 4);
            const auto style = mixsuite::ember::styleOf (param01 ("Style"));
            const auto drive = 0.25f * (float) std::pow (10.0, mixsuite::ember::driveDb (param01 ("Drive")) / 20.0);
            const auto norm = 1.0f / juce::jmax (0.05f, std::abs (mixsuite::ember::shape (style, drive)));
            strokeCurve ([style, drive, norm] (float x) {
                return 0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, mixsuite::ember::shape (style, (x * 2.0f - 1.0f) * drive) * norm);
            }, inner, false);
        }
        else if (id == "echotrain" || id == "lattice") // the echoes / taps on a time axis: left up, right down
        {
            grid (8, 2);
            const auto params = fx.params;
            const auto bpm = engine.getSong().bpm > 1.0 ? engine.getSong().bpm : 120.0;
            struct Tick { double ms; double level; bool right; };
            std::vector<Tick> ticks;
            double window = 50.0;
            if (id == "echotrain")
            {
                using namespace echosuite::echoform;
                const auto st = styleOf (params[style]);
                auto tL = sideMs (params, false, bpm), tR = sideMs (params, true, bpm);
                if (st == Style::single || st == Style::quad)
                    tR = tL;
                else if (st == Style::ratio)
                    tR = tL * (0.01 + 0.99 * (double) params[ratio]);
                const auto gain = juce::jlimit (0.0, 1.0, feedbackGain (params[feedback]));
                window = juce::jmax (50.0, 4.5 * juce::jmax (tL, tR));
                if (params[rhythm] >= 0.5f) // round 137: the Rhythm pattern, three passes of it (left and right alike)
                {
                    double slot0 = 0.0;
                    const auto taps = rhythmTaps (params, bpm, &slot0);
                    const auto length = rhythmLengthMs (params, bpm);
                    window = 3.0 * length * 1.02;
                    auto level = 1.0;
                    for (int pass = 0; pass < 3; ++pass, level *= gain)
                        for (int c = 0; c < 2; ++c)
                        {
                            if (pass > 0)
                                ticks.push_back ({ length * pass, slot0 * level, c == 1 });
                            for (const auto& t : taps)
                                ticks.push_back ({ length * pass + t.ms, t.level * level, c == 1 });
                        }
                }
                else if (st == Style::pingPong)
                {
                    auto t = tL, level = 1.0;
                    for (int k = 0; k < 24 && t <= window; ++k)
                    {
                        ticks.push_back ({ t, level, (k % 2) == 1 });
                        t += (k % 2) == 0 ? tR : tL;
                        level *= params[pairs] >= 0.5f ? ((k % 2) == 1 ? gain : 1.0) : std::sqrt (gain); // round 119: pairs
                    }
                }
                else
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto t = c == 0 ? tL : tR;
                        auto level = 1.0;
                        for (int k = 1; k <= 24 && t > 0.0 && t * k <= window; ++k, level *= gain)
                        {
                            if (st == Style::quad)
                                for (int q = 1; q < 4; ++q)
                                    ticks.push_back ({ t * (k - 1 + q / 4.0), 0.5 * level, c == 1 });
                            ticks.push_back ({ t * k, st == Style::quad ? 0.5 * level : level, c == 1 });
                        }
                    }
            }
            else
            {
                using namespace echosuite::lattice;
                const auto n = tapCount (params[taps]);
                double longest = 1.0;
                for (int k = 0; k < n; ++k)
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto t = tapMs (params, k, n, c == 1);
                        longest = juce::jmax (longest, t);
                        ticks.push_back ({ t, std::abs (tapLevel (params, k, n)) * std::sqrt ((double) n), c == 1 });
                    }
                window = longest * 1.15;
            }
            const auto mid = inner.getCentreY();
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) mid, inner.getX(), inner.getRight());
            g.setColour (curveColour);
            for (const auto& t : ticks)
            {
                if (t.level < 0.01)
                    continue;
                const auto x = inner.getX() + inner.getWidth() * (float) (t.ms / window);
                const auto h = inner.getHeight() * 0.48f * (float) juce::jlimit (0.0, 1.0, t.level);
                g.fillRect (juce::Rectangle<float> (x - 1.0f * s, t.right ? mid : mid - h, juce::jmax (1.5f, 2.0f * s), h));
            }
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
            g.drawText (juce::String (window >= 1000.0 ? juce::String (window / 1000.0, 2) + " s" : juce::String ((int) window) + " ms"),
                        inner.reduced (2.0f * s), juce::Justification::bottomRight);            if (id == "echotrain") // round 137: the delay read-out (H-Delay's central display): the note and its tempo, or the ms
            {
                using namespace echosuite::echoform;
                const auto kindL = syncKind (params[syncL]);
                juce::String text;
                if (kindL == 0)
                    text = juce::String (juce::roundToInt (sideMs (params, false, bpm))) + " ms";
                else
                    text = juce::String (formatEffectParam (EffectKind::echoform, noteL, params[noteL], bpm)) + (kindL == 2 ? " D" : kindL == 3 ? " T" : "")
                           + "  " + juce::String (tempoFor (params, bpm), 1) + " BPM";
                g.setColour (colour::textSecondary);
                g.drawText (text, inner.reduced (2.0f * s), juce::Justification::topRight);
            }
        }
        else if (paintFxDisplay (g, id, item, inner, fx))
        {
        }
        else // a plain well with the plugin's name (FL's scopes / animations are not reproduced)
        {
            g.setColour (colour::bg2);
            g.setFont (font (juce::jmax (10.0f, juce::jmin (28.0f, r.getHeight() * 0.22f)), true));
            g.drawFittedText (caps (utf8 (effectInfo (kind).name)), r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }

        if (item.label != nullptr && *item.label != 0)
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
            g.drawText (caps (tr (item.label)), inner.withHeight (12.0f * s), juce::Justification::topLeft);
        }
    }

#include "ui/PluginEditorsFx.inc"

#include "ui/GrossBeatEditor.inc"
#include "ui/MaximusCurveEditor.inc"
#include "ui/LovePhilterEditor.inc"

    // --- the parametric EQ's graph (FL's Parametric EQ 2 arrangement) -----------------------------------

    static constexpr double eqRange = 18.0; // FL's graph spans ±18 dB

    // rows above the graph (octaves, band names), frequencies under it, dB scale at its right
    juce::Rectangle<float> eqGraph (juce::Rectangle<float> r) const
    {
        const auto s = scale();
        return r.withTrimmedTop (29.0f * s).withTrimmedBottom (16.0f * s).withTrimmedRight (22.0f * s);
    }

    void paintEq (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto graph = eqGraph (r);
        g.setFont (font (juce::jmax (7.5f, 9.0f * s), true));

        // octave marks (C1…C9) and the usual band names, on plates like FL's
        const auto octaveRow = juce::Rectangle<float> (graph.getX(), r.getY() + 1.0f * s, graph.getWidth(), 11.0f * s);
        for (int o = 1; o <= 9; ++o)
        {
            const auto x = peqview::xForHz (graph, 32.703 * std::pow (2.0, o - 1));
            const auto next = peqview::xForHz (graph, 32.703 * std::pow (2.0, o));
            const auto plate = juce::Rectangle<float> (x, octaveRow.getY(), juce::jmin (next, graph.getRight()) - x - 1.0f, octaveRow.getHeight());
            g.setColour (colour::bg2);
            g.fillRect (plate);
            g.setColour (colour::textDisabled);
            g.drawText ("C" + juce::String (o), plate.withTrimmedLeft (2.0f), juce::Justification::centredLeft, false);
        }
        struct Range { double lo, hi; const char* name; };
        static const Range ranges[] = { { 20, 60, TR_NOOP ("Sub") }, { 60, 250, TR_NOOP ("Bass") }, { 250, 500, TR_NOOP ("Low mid") },
                                        { 500, 2000, TR_NOOP ("Mid") }, { 2000, 4000, TR_NOOP ("High mid") }, { 4000, 6000, TR_NOOP ("Prs") },
                                        { 6000, 20000, TR_NOOP ("Treble") } };
        for (const auto& range : ranges)
        {
            const auto x0 = peqview::xForHz (graph, range.lo), x1 = peqview::xForHz (graph, range.hi);
            const auto plate = juce::Rectangle<float> (x0, r.getY() + 15.0f * s, x1 - x0 - 1.0f, 11.0f * s);
            g.setColour (colour::bg3);
            g.fillRect (plate);
            g.setColour (colour::textSecondary);
            g.drawFittedText (caps (tr (range.name)), plate.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }

        drawParametricEqCurve (g, graph, fx.params, juce::jmax (8000.0, engine.getTransport().sampleRate.load()), fx.enabled, eqSelected, eqRange, false,
                               node.locate ? nullptr : &eqAnalyser); // round 166f: FL's spectrum and heatmap

        // frequencies under the graph, dB at its right
        g.setColour (colour::textDisabled);
        for (auto [hz, text] : { std::pair { 20.0, "20" }, { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                                 { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
            g.drawText (text, juce::Rectangle<float> (peqview::xForHz (graph, hz) + 2.0f, graph.getBottom() + 2.0f * s, 40.0f * s, 11.0f * s),
                        juce::Justification::centredLeft, false);
        for (int db = -18; db <= 18; db += 6)
            g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<float> (graph.getRight() + 1.0f, peqview::yForDb (graph, db, eqRange) - 6.0f * s, 21.0f * s, 12.0f * s),
                        juce::Justification::centredRight, false);

        // the band handles
        for (int b = 0; b < peq::bands; ++b)
        {
            if (peq::type (fx.params[(std::size_t) peq::param (b, 0)]) == peq::BandType::off)
                continue;
            const auto centre = peqview::handlePosition (graph, fx.params, b, eqRange);
            const auto d = juce::jmax (12.0f, 16.0f * s);
            const auto h = juce::Rectangle<float> (d, d).withCentre (centre);
            const auto c = peqview::bandColour (b);
            if (b == eqSelected)
            {
                g.setColour (c.withAlpha (0.25f));
                g.fillEllipse (h.expanded (4.0f));
            }
            g.setColour (colour::bg0.withAlpha (0.7f));
            g.fillEllipse (h);
            g.setColour (c.withAlpha (fx.enabled ? 1.0f : 0.4f));
            g.drawEllipse (h.reduced (1.0f), juce::jmax (1.5f, 2.0f * s));
            g.setFont (font (juce::jmax (8.0f, 9.5f * s), true));
            g.drawText (juce::String (b + 1), h, juce::Justification::centred);
        }
    }

    void setParams (const std::vector<std::pair<int, float>>& changes)
    {
        if (current() == nullptr)
            return;
        if (usesBar())
            bar.willEdit();
        editSlot ([changes] (EffectSlot& target) {
            for (auto [p, v] : changes)
                if (p >= 0 && p < maxEffectParams)
                    target.params[(std::size_t) p] = juce::jlimit (0.0f, 1.0f, v);
        });
    }

    bool eqMouseDown (const juce::MouseEvent& e, const LayoutItem& item)
    {
        const auto* fx = current();
        const auto graph = eqGraph (rectOf (item));
        const auto band = peqview::bandAt (graph, fx->params, e.position, eqRange);
        eqSelected = band;
        repaint();
        if (band < 0 && e.mods.isPopupMenu() && ! node.locate) // round 166f: the display (FL's Options › Histogram / Heatmap)
        {
            juce::PopupMenu menu;
            peqview::addDisplayItems (menu, [safe = juce::Component::SafePointer<juce::Component> (this)] {
                if (safe != nullptr) safe->repaint();
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return true;
        }
        if (band < 0)
            return true;
        if (e.mods.isPopupMenu())
        {
            peqview::showBandMenu (*this, fx->params, band, [this] (std::vector<std::pair<int, float>> changes) { setParams (changes); });
            return true;
        }
        eqDragOffset = peqview::handlePosition (graph, fx->params, band, eqRange) - e.position;
        eqDragging = true;
        return true;
    }

    void eqMouseDrag (const juce::MouseEvent& e)
    {
        const auto* fx = current();
        if (fx == nullptr || eqSelected < 0 || dragItem < 0)
            return;
        const auto graph = eqGraph (rectOf (activeLayout().items[(std::size_t) dragItem]));
        const auto p = e.position + eqDragOffset;
        std::vector<std::pair<int, float>> changes { { peq::param (eqSelected, 1), peq::hzValue (peqview::hzForX (graph, p.x)) } };
        if (peqview::hasGain (fx->params, eqSelected))
            changes.push_back ({ peq::param (eqSelected, 2), peq::gainValue (peqview::dbForY (graph, p.y, eqRange)) });
        setParams (changes);
        ui::showValueTip (*this, juce::Rectangle<int> (6, 6).withCentre (e.getPosition()), peqview::bandText (fx->params, eqSelected));
    }

    // --- Soundgoodizer: Amount and the preset A–D live in the slot's state ---------------------------------

    std::optional<maximusfx::Blob> soundgoodizerBlob() const
    {
        const auto* fx = current();
        return fx != nullptr ? maximusfx::unpack (fx->state) : std::nullopt;
    }

    // the approximation keeps the plugin's own state (version, preset, amount); edits rebuild its chain
    std::optional<maximusfx::SoundgoodizerState> soundgoodizerApproxState() const
    {
        const auto* fx = current();
        return fx != nullptr && sgApprox ? maximusfx::parseSoundgoodizer (fx->state) : std::nullopt;
    }

    void writeSoundgoodizerApprox (int preset, int amount)
    {
        engine.editSong ([t = track, sl = slot, rebuilt = soundgoodizerApproxSlot (preset, amount)] (Song& song) {
            auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) sl];
            fx.branches = rebuilt.branches;
            fx.state = rebuilt.state;
        });
    }

    float soundgoodizerAmount() const
    {
        if (sgApprox)
        {
            const auto sg = soundgoodizerApproxState();
            return sg ? (float) sg->amount / 1000.0f : 0.6f;
        }
        const auto blob = soundgoodizerBlob();
        return blob && blob->mix >= 0 ? (float) blob->mix / 1000.0f : 0.6f;
    }

    void writeSoundgoodizer (const maximusfx::Blob& blob)
    {
        engine.editSong ([t = track, sl = slot, bytes = maximusfx::pack (blob)] (Song& song) {
            song.mixer[(std::size_t) t].effects[(std::size_t) sl].state = bytes;
        });
    }

    void setSoundgoodizerAmount (float v)
    {
        if (sgApprox)
        {
            if (const auto sg = soundgoodizerApproxState())
                writeSoundgoodizerApprox (sg->preset, juce::roundToInt (v * 1000.0f));
            return;
        }
        if (auto blob = soundgoodizerBlob())
        {
            blob->mix = juce::roundToInt (v * 1000.0f);
            writeSoundgoodizer (*blob);
        }
    }

    const std::vector<std::uint8_t>& soundgoodizerPresetBytes (int preset)
    {
        auto& bytes = sgPresets[(std::size_t) preset];
        if (bytes.empty())
        {
            const auto roots = findFlStudioRoots();
            juce::MemoryBlock data;
            if (! roots.factoryData.empty()
                && juce::File (juce::String::fromUTF8 (roots.factoryData.c_str()))
                       .getChildFile ("Plugins/Fruity/Effects/Soundgoodizer/Data/Presets")
                       .getChildFile (maximusfx::soundgoodizerPresetFile (preset))
                       .loadFileAsData (data))
            {
                const auto* b = static_cast<const std::uint8_t*> (data.getData());
                bytes.assign (b, b + data.getSize());
            }
        }
        return bytes;
    }

    int soundgoodizerPreset()
    {
        if (sgApprox)
        {
            const auto sg = soundgoodizerApproxState();
            return sg ? sg->preset : -1;
        }
        const auto blob = soundgoodizerBlob();
        for (int p = 0; blob && p < 4; ++p)
            if (const auto& bytes = soundgoodizerPresetBytes (p); ! bytes.empty() && bytes == blob->state)
                return p;
        return -1;
    }

    void setSoundgoodizerPreset (int preset)
    {
        if (sgApprox)
        {
            if (const auto sg = soundgoodizerApproxState())
                writeSoundgoodizerApprox (preset, sg->amount);
            return;
        }
        auto blob = soundgoodizerBlob();
        const auto& bytes = soundgoodizerPresetBytes (preset);
        if (! blob || bytes.empty())
            return;
        blob->state = bytes;
        writeSoundgoodizer (*blob);
    }

    void paintSoundgoodizer (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        if (id == "sgamount")
        {
            const auto amount = soundgoodizerAmount();
            drawKnob (g, r, amount, false, fx.enabled ? accent : colour::textDisabled, sgDragging);
            const auto body = r.reduced (r.getWidth() * 0.16f);
            g.setGradientFill (juce::ColourGradient (colour::bg6, body.getCentreX(), body.getY(), colour::bg4, body.getCentreX(), body.getBottom(), false));
            g.fillEllipse (body);
            g.setColour (colour::borderStrong);
            g.drawEllipse (body, 1.0f);
            g.setColour (colour::textPrimary);
            g.setFont (font (juce::jmax (10.0f, 18.0f * s), true));
            g.drawText (juce::String (amount * 100.0f, 0) + "%", body, juce::Justification::centred, false);
            return;
        }
        const auto chosen = soundgoodizerPreset();
        for (int p = 0; p < 4; ++p)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + r.getWidth() * (float) p / 4.0f, r.getY(), r.getWidth() / 4.0f, r.getHeight());
            const auto led = juce::Rectangle<float> (8.0f * s, 8.0f * s).withCentre ({ cell.getCentreX(), cell.getY() + 8.0f * s });
            g.setColour (p == chosen ? (fx.enabled ? accent : colour::textSecondary) : colour::ledOff);
            g.fillEllipse (led);
            g.setColour (p == chosen ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (juce::jmax (10.0f, 20.0f * s), true));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + p)), cell.withTrimmedTop (16.0f * s), juce::Justification::centred, false);
        }
    }

    // a pad sets two parameters at once
    // --- round 137: Gemini Doubler's graphs are editable (the reference: "Gain, Pan, Delay and Detune can be controlled
    // using the graphs"): drag a voice on the pan / gain half circle or on the delay / detune map, drag the EQ's L / H
    // markers (frequency across, gain up), double-click a marker = that shelf on / off
    static bool isGeminiGraph (const LayoutItem& item)
    {
        return item.type == Item::display && item.display != nullptr
               && (std::strcmp (item.display, "geminipan") == 0 || std::strcmp (item.display, "geminimap") == 0 || std::strcmp (item.display, "geminieq") == 0);
    }
    // the handles of a graph: (parameter across / around, parameter up / out, position on screen)
    struct GeminiHandle { int px = -1, py = -1, on = -1; juce::Point<float> at; };
    std::vector<GeminiHandle> geminiHandles (const LayoutItem& item) const
    {
        using namespace voicetools::gemini;
        std::vector<GeminiHandle> out;
        const auto s = scale();
        const auto inner = rectOf (item).reduced (6.0f * s);
        const juce::String id (item.display);
        if (id == "geminipan")
        {
            const auto c = juce::Point<float> (inner.getCentreX(), inner.getBottom() - 4.0f * s);
            const auto radius = juce::jmin (inner.getWidth() * 0.5f, inner.getHeight()) - 8.0f * s;
            auto add = [&] (int panP, int gainP, int onP) {
                const auto a = juce::degreesToRadians ((float) panDegrees (valueOf (panP)));
                const auto rr = radius * juce::jlimit (0.05f, 1.0f, valueOf (gainP));
                out.push_back ({ panP, gainP, onP, { c.x + rr * std::sin (a), c.y - rr * std::cos (a) } });
            };
            add (directPan, directGain, directOn);
            for (int v = 0; v < voices; ++v)
                if (v < 2 || valueOf (voiceParam (v, vOn)) >= 0.5f)
                    add (voiceParam (v, vPan), voiceParam (v, vGain), voiceParam (v, vOn));
        }
        else if (id == "geminimap")
        {
            for (int v = 0; v < voices; ++v)
                if (v < 2 || valueOf (voiceParam (v, vOn)) >= 0.5f)
                    out.push_back ({ voiceParam (v, vDelay), voiceParam (v, vDetune), voiceParam (v, vOn),
                                     { inner.getX() + inner.getWidth() * valueOf (voiceParam (v, vDelay)),
                                       inner.getBottom() - inner.getHeight() * valueOf (voiceParam (v, vDetune)) } });
        }
        else
        {
            auto add = [&] (int freqP, int gainP, int onP) {
                const auto hz = freqHz (valueOf (freqP));
                const auto x = (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0));
                const bool measured = valueOf (eqLaw) >= 0.5f; // round 165w: the height is the gain's own scale
                const auto y = valueOf (onP) >= 0.5f ? valueOf (gainP) : shelfValue (0.0, measured);
                out.push_back ({ freqP, gainP, onP, { inner.getX() + inner.getWidth() * x, inner.getBottom() - inner.getHeight() * y } });
            };
            add (lowFreq, lowGain, lowOn);
            add (highFreq, highGain, highOn);
        }
        return out;
    }
    int geminiNearest (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto handles = geminiHandles (item);
        int best = -1;
        auto bestD = 18.0f * scale();
        for (int k = 0; k < (int) handles.size(); ++k)
            if (const auto d = handles[(std::size_t) k].at.getDistanceFrom (p); d < bestD)
                best = k, bestD = d;
        return best;
    }
    void geminiMouse (const juce::MouseEvent& e, const LayoutItem& item)
    {
        using namespace voicetools::gemini;
        const auto handles = geminiHandles (item);
        if (geminiHandle < 0 || geminiHandle >= (int) handles.size())
            return;
        const auto& h = handles[(std::size_t) geminiHandle];
        const auto s = scale();
        const auto inner = rectOf (item).reduced (6.0f * s);
        const juce::String id (item.display);
        float x = 0.0f, y = 0.0f;
        if (id == "geminipan")
        {
            const auto c = juce::Point<float> (inner.getCentreX(), inner.getBottom() - 4.0f * s);
            const auto radius = juce::jmax (1.0f, juce::jmin (inner.getWidth() * 0.5f, inner.getHeight()) - 8.0f * s);
            const auto deg = juce::jlimit (-45.0f, 45.0f, juce::radiansToDegrees (std::atan2 (e.position.x - c.x, juce::jmax (0.001f, c.y - e.position.y))));
            x = (deg + 45.0f) / 90.0f;
            y = juce::jlimit (0.0f, 1.0f, e.position.getDistanceFrom (c) / radius);
        }
        else if (id == "geminimap")
        {
            x = juce::jlimit (0.0f, 1.0f, (e.position.x - inner.getX()) / juce::jmax (1.0f, inner.getWidth()));
            y = juce::jlimit (0.0f, 1.0f, (inner.getBottom() - e.position.y) / juce::jmax (1.0f, inner.getHeight()));
        }
        else
        {
            const auto fx = juce::jlimit (0.0f, 1.0f, (e.position.x - inner.getX()) / juce::jmax (1.0f, inner.getWidth()));
            x = freqValue (20.0 * std::pow (1000.0, (double) fx));
            y = juce::jlimit (0.0f, 1.0f, (inner.getBottom() - e.position.y) / juce::jmax (1.0f, inner.getHeight()));
        }
        std::vector<std::pair<int, float>> changes { { h.px, x }, { h.py, y } };
        if (id == "geminieq" && valueOf (h.on) < 0.5f) // dragging a marker turns its shelf on
            changes.push_back ({ h.on, 1.0f });
        setParams (changes);
        ui::showValueTip (*this, rectOf (item).toNearestInt(), valueText (h.px, x) + " / " + valueText (h.py, y));
    }
    int geminiHandle = -1;
    float grHold = 0.0f; // round 137: the tall reduction meter's held value
    float gcLevel = 0.0f, gcHold = 0.0f; // round 152: the bipolar gain-change meter (dB, signed) and its held change
    bool geminiDragging = false;

    void xyMouse (const juce::MouseEvent& e, const LayoutItem& item)
    {
        const auto inner = rectOf (item).reduced (6.0f * scale());
        const auto px = plugui::paramIndex (kind, item.param), py = plugui::paramIndex (kind, item.param2);
        const auto x = juce::jlimit (0.0f, 1.0f, (e.position.x - inner.getX()) / inner.getWidth());
        const auto y = juce::jlimit (0.0f, 1.0f, (inner.getBottom() - e.position.y) / inner.getHeight());
        setParams ({ { px, x }, { py, y } });
        ui::showValueTip (*this, rectOf (item).toNearestInt(), valueText (px, x) + " / " + valueText (py, y));
    }

    // --- Fruity WaveShaper's curve (round 165: FL's point editor) ---------------------------------------
    // FL's gestures: right-click adds a point (hold and drag to place it), left-drag moves one (Shift keeps its
    // level, Ctrl its position), Alt+click or right-click › Delete removes it, the hollow handle between two points
    // bends the segment (Ctrl: fine; right-click: straight again); right-click a point for its shape and value.
    static bool isWaveShaperGraph (const LayoutItem& item) { return item.type == Item::display && juce::String (item.display) == "waveshaper"; }

    bool wsUnipolar() const { return valueOf (plugui::paramIndex (kind, "Unipolar")) >= 0.5f; }

    flshape::WaveShaperState wsState() const
    {
        const auto* fx = current();
        if (fx == nullptr)
            return flshape::waveShaperForEditing ({});
        auto s = flshape::waveShaperForEditing (fx->state, fx->params.data());
        if (wsDragState)
            s.points = wsDragPoints; // shown at once while the edit travels to the engine
        return s;
    }

    const std::vector<std::int16_t>& wsTable (const std::vector<flshape::WsPoint>& points, bool unipolar) const
    {
        // the curve as the engine shapes it (one table per change of the points)
        auto same = wsCacheUni == unipolar && wsCachePoints.size() == points.size();
        for (std::size_t i = 0; same && i < points.size(); ++i)
            same = wsCachePoints[i].dt == points[i].dt && wsCachePoints[i].y == points[i].y && wsCachePoints[i].tension == points[i].tension
                   && wsCachePoints[i].mode == points[i].mode;
        if (! same || wsCacheTable.empty())
        {
            wsCachePoints = points;
            wsCacheUni = unipolar;
            wsCacheTable = flshape::shaperTable (points, unipolar);
        }
        return wsCacheTable;
    }

    juce::Rectangle<float> wsArea (const LayoutItem& item) const { return rectOf (item).reduced (6.0f * scale()); }

    static juce::Point<float> wsPos (juce::Rectangle<float> area, float x, float y)
    {
        return { area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y };
    }

    // the tension handle of the segment that ends at point i (x < 0: none — a hold, or too narrow to grab)
    juce::Point<float> wsHandlePos (juce::Rectangle<float> area, const flshape::WaveShaperState& ws, const std::vector<float>& times, int i) const
    {
        if (i < 1 || i >= (int) ws.points.size() || ws.points[(std::size_t) i].mode == 2)
            return { -2.0f, -2.0f };
        const auto x0 = times[(std::size_t) i - 1], x1 = times[(std::size_t) i];
        if ((x1 - x0) * area.getWidth() < 16.0f * scale())
            return { -2.0f, -2.0f };
        const auto xm = 0.5f * (x0 + x1);
        return wsPos (area, xm, flshape::waveShaperLevelAt (wsTable (ws.points, wsUnipolar()), wsUnipolar(), xm));
    }

    int wsPointAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto ws = wsState();
        const auto times = flshape::waveShaperTimes (ws.points);
        const auto area = wsArea (item);
        int best = -1;
        auto bestD = 8.0f * scale();
        for (std::size_t i = 0; i < ws.points.size(); ++i)
            if (const auto d = wsPos (area, times[i], ws.points[i].y).getDistanceFrom (p); d <= bestD)
                best = (int) i, bestD = d;
        return best;
    }

    int wsHandleAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto ws = wsState();
        const auto times = flshape::waveShaperTimes (ws.points);
        const auto area = wsArea (item);
        for (int i = 1; i < (int) ws.points.size(); ++i)
            if (const auto h = wsHandlePos (area, ws, times, i); h.x > -1.0f && h.getDistanceFrom (p) <= 7.0f * scale())
                return i;
        return -1;
    }

    juce::Point<float> wsMouseValue (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto area = wsArea (item);
        return { juce::jlimit (0.0f, 1.0f, (p.x - area.getX()) / juce::jmax (1.0f, area.getWidth())),
                 juce::jlimit (0.0f, 1.0f, (area.getBottom() - p.y) / juce::jmax (1.0f, area.getHeight())) };
    }

    void wsWrite (std::vector<flshape::WsPoint> points)
    {
        const auto* fx = current();
        if (fx == nullptr)
            return;
        auto s = flshape::waveShaperForEditing (fx->state, fx->params.data());
        s.points = std::move (points);
        s.unipolar = wsUnipolar();
        if (usesBar())
            bar.willEdit();
        engine.editSong ([t = track, sl = slot, bytes = flshape::writeWaveShaper (s, fx->state)] (Song& song) {
            song.mixer[(std::size_t) t].effects[(std::size_t) sl].state = bytes;
        });
        repaint();
    }

    juce::String wsValueText (float y) const
    {
        // the level as FL's hint shows it: percent of the map (bipolar 0…100 %, unipolar −100…100 %)
        return wsUnipolar() ? juce::String (juce::roundToInt ((y * 2.0f - 1.0f) * 100.0f)) + "%" : juce::String (juce::roundToInt (y * 100.0f)) + "%";
    }

    void wsShowTip (const LayoutItem& item, float x, float y)
    {
        ui::showValueTip (*this, rectOf (item).toNearestInt(), tr ("In") + " " + juce::String (juce::roundToInt (x * 100.0f)) + "%  " + tr ("Out") + " " + wsValueText (y));
    }

    // mouse down on the graph; true = a drag follows
    bool wsMouseDown (const juce::MouseEvent& e, const LayoutItem& item)
    {
        auto points = wsState().points;
        const auto point = wsPointAt (item, e.position);
        const auto handle = point < 0 ? wsHandleAt (item, e.position) : -1;
        const auto v = wsMouseValue (item, e.position);
        wsDownMods = e.mods;

        if (e.mods.isPopupMenu())
        {
            if (point >= 0)
                wsPointMenu (point);
            else if (handle >= 0) // straight again
            {
                points[(std::size_t) handle].tension = 0.0f;
                wsWrite (points);
            }
            else // FL: a new point at the mouse, dragged while the button is held
            {
                wsPoint = flshape::addWaveShaperPoint (points, v.x, v.y);
                wsDragPoints = points;
                wsDragState = true;
                wsWrite (points);
                wsShowTip (item, v.x, v.y);
                return true;
            }
            return false;
        }
        if (point >= 0 && e.mods.isAltDown()) // FL: Alt+click deletes
        {
            if (flshape::removeWaveShaperPoint (points, point))
                wsWrite (points);
            return false;
        }
        if (point >= 0)
        {
            wsPoint = point;
            wsDragPoints = points;
            wsDragState = true;
            const auto t = flshape::waveShaperTimes (points);
            wsShowTip (item, t[(std::size_t) point], points[(std::size_t) point].y);
            return true;
        }
        if (handle >= 0)
        {
            // which way the tension bends this segment: the handle follows the mouse up and down
            wsTension = handle;
            wsTensionStart = points[(std::size_t) handle].tension;
            auto bent = points;
            bent[(std::size_t) handle].tension = juce::jlimit (-1.0f, 1.0f, wsTensionStart + (wsTensionStart > 0.8f ? -0.1f : 0.1f));
            const auto times = flshape::waveShaperTimes (points);
            const auto xm = 0.5f * (times[(std::size_t) handle - 1] + times[(std::size_t) handle]);
            const auto uni = wsUnipolar();
            const auto a = flshape::waveShaperLevelAt (flshape::shaperTable (points, uni), uni, xm);
            const auto b = flshape::waveShaperLevelAt (flshape::shaperTable (bent, uni), uni, xm);
            const auto up = (b - a) * (bent[(std::size_t) handle].tension - wsTensionStart);
            wsTensionSign = up < 0.0f ? -1.0f : 1.0f;
            wsDragPoints = points;
            wsDragState = true;
            return true;
        }
        return false;
    }

    void wsMouseDrag (const juce::MouseEvent& e, const LayoutItem& item)
    {
        if (! wsDragState)
            return;
        auto points = wsDragPoints;
        if (wsTension >= 0 && wsTension < (int) points.size())
        {
            const auto dy = -(float) e.getDistanceFromDragStartY() / ((e.mods.isCtrlDown() ? 600.0f : 150.0f) * scale());
            points[(std::size_t) wsTension].tension = juce::jlimit (-1.0f, 1.0f, wsTensionStart + wsTensionSign * dy);
            ui::showValueTip (*this, rectOf (item).toNearestInt(), tr ("Tension") + " " + juce::String (juce::roundToInt (points[(std::size_t) wsTension].tension * 100.0f)) + "%");
        }
        else if (wsPoint >= 0 && wsPoint < (int) points.size())
        {
            const auto t = flshape::waveShaperTimes (points);
            auto v = wsMouseValue (item, e.position);
            // FL: Shift keeps the level, Ctrl keeps the position
            if (e.mods.isShiftDown() && ! wsDownMods.isShiftDown())
                v.y = points[(std::size_t) wsPoint].y;
            if (e.mods.isCtrlDown())
                v.x = t[(std::size_t) wsPoint];
            flshape::moveWaveShaperPoint (points, wsPoint, v.x, v.y);
            const auto moved = flshape::waveShaperTimes (points);
            wsShowTip (item, moved[(std::size_t) wsPoint], points[(std::size_t) wsPoint].y);
        }
        else
            return;
        wsDragPoints = points;
        wsWrite (points);
    }

    void wsMouseUp()
    {
        wsDragState = false;
        wsPoint = wsTension = -1;
        ui::hideValueTip();
        repaint();
    }

    void wsPointMenu (int point)
    {
        const auto ws = wsState();
        if (point < 0 || point >= (int) ws.points.size())
            return;
        const auto& p = ws.points[(std::size_t) point];
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Out") + " " + wsValueText (p.y));
        const auto inner = point > 0 && point + 1 < (int) ws.points.size();
        menu.addItem (tr ("Delete"), inner, false, [this, point] {
            auto pts = wsState().points;
            if (flshape::removeWaveShaperPoint (pts, point))
                wsWrite (pts);
        });
        if (point > 0) // the shape of the segment that ends here (FL's modes)
        {
            menu.addSectionHeader (tr ("Mode"));
            for (const auto& m : flshape::waveShaperModes())
                menu.addItem (tr (m.name), true, p.mode == m.mode, [this, point, mode = m.mode] {
                    auto pts = wsState().points;
                    if (point < (int) pts.size())
                    {
                        pts[(std::size_t) point].mode = mode;
                        wsWrite (pts);
                    }
                });
        }
        menu.addSeparator();
        menu.addItem (tr ("Copy value"), [y = p.y] { wsCopiedValue = y; });
        menu.addItem (tr ("Paste value"), wsCopiedValue >= 0.0f, false, [this, point] {
            auto pts = wsState().points;
            if (point < (int) pts.size())
            {
                pts[(std::size_t) point].y = wsCopiedValue;
                wsWrite (pts);
            }
        });
        menu.addItem (tr ("Type in value..."), [this, point] { wsTypeValue (point); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void wsTypeValue (int point)
    {
        const auto ws = wsState();
        if (point < 0 || point >= (int) ws.points.size())
            return;
        const auto uni = wsUnipolar();
        const auto y = ws.points[(std::size_t) point].y;
        auto* window = new juce::AlertWindow (tr ("Point value"), uni ? tr ("Percent (-100 to 100)") : tr ("Percent (0 to 100)"), juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("value", juce::String (uni ? (y * 2.0f - 1.0f) * 100.0f : y * 100.0f, 1));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<NativeEffectEditor> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, point, uni] (int result) {
                                     if (result != 1 || self == nullptr)
                                         return;
                                     const auto typed = window->getTextEditorContents ("value").getFloatValue() / 100.0f;
                                     auto pts = self->wsState().points;
                                     if (point < (int) pts.size())
                                     {
                                         pts[(std::size_t) point].y = juce::jlimit (0.0f, 1.0f, uni ? (typed + 1.0f) * 0.5f : typed);
                                         self->wsWrite (pts);
                                     }
                                 }),
                                 true);
    }

    juce::String wsHint (const LayoutItem& item, juce::Point<float> p) const
    {
        if (wsPointAt (item, p) >= 0)
            return tr ("Point: [LMB] drag moves ([Shift] keeps the level, [Ctrl] the position)  [Alt][LMB] delete  [RMB] shape, value, delete");
        if (wsHandleAt (item, p) >= 0)
            return tr ("Tension: [LMB] drag up / down bends the segment ([Ctrl] fine)  [RMB] straight");
        return tr ("Shaping curve: [RMB] add a point (hold and drag to place it)  [LMB] drag a point  [Alt][LMB] delete a point");
    }

    int wsPoint = -1, wsTension = -1, wsHoverPoint = -1, wsHoverHandle = -1;
    float wsTensionStart = 0.0f, wsTensionSign = 1.0f;
    bool wsDragState = false;
    std::vector<flshape::WsPoint> wsDragPoints;
    juce::ModifierKeys wsDownMods;
    mutable std::vector<flshape::WsPoint> wsCachePoints;
    mutable std::vector<std::int16_t> wsCacheTable;
    mutable bool wsCacheUni = false;
    static inline float wsCopiedValue = -1.0f;

    // --- mouse -----------------------------------------------------------------------------------------

    int headerItemAt (juce::Point<float> p) const
    {
        if (! headerArea().contains (p))
            return 0;
        if (menuButton().expanded (2.0f).contains (p))
            return 1;
        if (! sidechainChip.isEmpty() && sidechainChip.contains (p))
            return 6;
        if (usesBar())
            return bar.contains (p) ? 5 : 0;
        if (presetBox().contains (p))
            return 2;
        if (prevButton().contains (p))
            return 3;
        if (nextButton().contains (p))
            return 4;
        return 0;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        const auto h = headerItemAt (p);
        const auto i = h == 0 ? itemAt (p) : -1;
        if (i >= 0 && (hasStepArrows (activeLayout().items[(std::size_t) i]) || isRhythmEdit (activeLayout().items[(std::size_t) i])))
        {
            mouseAt = p; // the menu's arrows light under the mouse
            repaint();
        }
        if (usesBar() && bar.hover (p))
            repaint();
        if (h != hoverHeader || i != hoverItem)
        {
            hoverHeader = h;
            hoverItem = i;
            anim.setHot (i);
            repaint();
        }
        if (kind == EffectKind::grossBeat && i >= 0 && gbMouseMove (activeLayout().items[(std::size_t) i], p)) // round 168m
            return;
        if (kind == EffectKind::maximus && mxMouseMove (i >= 0 ? activeLayout().items[(std::size_t) i] : LayoutItem {}, p)) // round 168x
            return;
        if ((kind == EffectKind::lovePhilter || kind == EffectKind::convolver || kind == EffectKind::vocodex) && lpMouseMove (i >= 0 ? activeLayout().items[(std::size_t) i] : LayoutItem {}, p)) // round 173d3
            return;
        if (i >= 0 && isWaveShaperGraph (activeLayout().items[(std::size_t) i])) // round 165: points and handles light up
        {
            const auto& item = activeLayout().items[(std::size_t) i];
            const auto point = wsPointAt (item, p), handle = point < 0 ? wsHandleAt (item, p) : -1;
            if (point != wsHoverPoint || handle != wsHoverHandle)
            {
                wsHoverPoint = point;
                wsHoverHandle = handle;
                repaint();
            }
            setMouseCursor (point >= 0 ? juce::MouseCursor::DraggingHandCursor
                                       : handle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
            return;
        }
        if (wsHoverPoint >= 0 || wsHoverHandle >= 0)
        {
            wsHoverPoint = wsHoverHandle = -1;
            repaint();
        }
        setMouseCursor (i >= 0 || h > 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        bar.hoverExit();
        hoverHeader = 0;
        hoverItem = -1;
        anim.setHot (-1);
        mouseAt = { -1.0f, -1.0f };
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (current() == nullptr)
            return;

        if (const auto h = headerItemAt (e.position); h > 0)
        {
            if (h == 6)
            {
                pluginheader::showSidechainPicker (engine, track, slot, this, sidechainChip.getSmallestIntegerContainer());
                return;
            }
            if (h == 5)
            {
                const auto flFiles = presetFiles(); // round 168r: ◀ ▶ also run through FL's presets after LYNX's bank
                const pluginheader::PresetBar::FlPresets fl {
                    [this] (juce::PopupMenu& m) { addFlPresets (m); }, [this] (int delta) { stepPreset (delta); }, [this] { return presetName; },
                    [flFiles] { return flFiles.size(); },
                    [this, flFiles] {
                        const auto* fx = current();
                        for (int i = 0; i < flFiles.size(); ++i)
                            if (flFiles[i].getFileNameWithoutExtension() == presetName && fx != nullptr && fx->presetName == presetName.toStdString())
                                return i;
                        return -1;
                    },
                    [this, flFiles] (int i) {
                        if (i >= 0 && i < flFiles.size())
                        {
                            loadPreset (flFiles[i]);
                            ui::postHint ({ presetName, juce::String (presetcycle::positionText (i, flFiles.size())) }, 2500);
                        }
                    } };
                bar.click (bar.partAt (e.position), *this, &fl);
                return;
            }
            if (h == 1 || h == 2 || e.mods.isPopupMenu())
                showOptionsMenu (h == 2);
            else
                stepPreset (h == 3 ? -1 : 1);
            return;
        }

        const auto index = itemAt (e.position);
        if (index < 0)
            return;
        bar.beginGesture();

        const auto& item = activeLayout().items[(std::size_t) index];
        const auto r = rectOf (item);

        if (kind == EffectKind::grossBeat && gbMouseDown (e, item, index)) // round 168m: the envelope editor and slot settings
            return;

        if (! e.mods.isPopupMenu() && isMixItem (item) && captionArea (item).contains (e.position))
        {
            pluginheader::toggleMixLock (kind);
            repaint();
            return;
        }

        if (hasStyle (item, plugui::pageTabs))
        {
            const auto n = juce::jmax (1, (int) plugui::optionLabels (item).size());
            const auto along = isVertical (item) ? (e.position.y - r.getY()) / r.getHeight() : (e.position.x - r.getX()) / r.getWidth();
            const auto o = juce::jlimit (0, n - 1, (int) (along * (float) n));
            if (item.steps > 0) // one digit of the page (EQUO: target ×1, bank ×3)
                page += (o - (page / item.steps) % n) * item.steps;
            else
                page = o;
            if (const auto p = paramOf (item); p >= 0) // round tp172b: page tabs that also choose a value (LuxeVerb's engine)
                setValue (p, n > 1 ? (float) o / (float) (n - 1) : 0.0f);
            hoverItem = -1;
            repaint();
            return;
        }

        if (isWaveShaperGraph (item)) // round 165: FL's point editor
        {
            if (wsMouseDown (e, item))
                dragItem = index;
            return;
        }
        if (isMaximusCurve (item)) // round 168x: FL's envelope editor on Maximus' In/Out graph (issue #40)
        {
            if (mxMouseDown (e, item))
                dragItem = index;
            return;
        }
        if (isPhilterCurve (item)) // round 173d3: Love Philter's articulator editor
        {
            if (lpMouseDown (e, item))
                dragItem = index;
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "vxmapsel") // round 173d3: Vocodex's map dropdown
        {
            vxMapMenu (rectOf (item));
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "pitchport") // round 190ez: drag up / down = port
        {
            if (! e.mods.isPopupMenu() && current() != nullptr)
            {
                portDragStart = effectMidiInPort (*current());
                portDragY = e.position.y;
                dragItem = index;
            }
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "equosend") // round 173d3: EQUO's send box
        {
            showTransmitterSendMenu (0, rectOf (item));
            return;
        }
        if (item.type == Item::display && (juce::String (item.display) == "tsssend0" || juce::String (item.display) == "tsssend1"))
        {
            showTransmitterSendMenu (juce::String (item.display) == "tsssend0" ? 0 : 1, rectOf (item));
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "tunekeys")
        {
            tuneKeyClick (rectOf (item), e.position);
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "tuneautokey")
        {
            autoKey (rectOf (item));
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "pitchring") // HOLD
        {
            tuneHold = ! tuneHold;
            if (tuneHold && ! pitchHistory.empty())
                holdPoint = pitchHistory.back();
            repaint();
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "vumeter") // the VU's selector: gain reduction / output / input
        {
            setVuMode ((vuMode + 1) % 3);
            repaint();
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "iometer") // round 135w: reset the peaks and the clip lamp
        {
            ioMeters = {};
            ioClip = false;
            repaint();
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "gcmeter") // round 152: reset the held change
        {
            gcHold = 0.0f;
            repaint();
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "grmeter") // round 137: reset the held reduction
        {
            grHold = 0.0f;
            repaint();
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "outmeter") // round 135w: reset the peak and the clip lamp
        {
            outMax = 0.0f;
            outClip = false;
            repaint();
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "taptempo") // round 129: the tap pad
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastTap > 2000)
                tapIntervals.clear();
            else
            {
                tapIntervals.push_back ((double) (now - lastTap));
                if (tapIntervals.size() > 4)
                    tapIntervals.erase (tapIntervals.begin());
                double sum = 0.0;
                for (const auto t : tapIntervals) sum += t;
                const auto msAvg = sum / (double) tapIntervals.size();
                tapBpm = 60000.0 / msAvg;
                const auto timeParam = plugui::paramIndex (kind, item.param), syncParam = plugui::paramIndex (kind, item.param2);
                std::vector<std::pair<int, float>> changes;
                if (const auto sync = presets::valueFromText (kind, syncParam, "ms"); sync && syncParam >= 0)
                    changes.push_back ({ syncParam, *sync });
                if (const auto t = presets::valueFromText (kind, timeParam, juce::String (msAvg, 1).toStdString() + " ms"); t && timeParam >= 0)
                    changes.push_back ({ timeParam, *t });
                bar.beginGesture();
                setParams (changes);
            }
            lastTap = now;
            repaint();
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "sgpresets")
        {
            const auto r = rectOf (item);
            setSoundgoodizerPreset (juce::jlimit (0, 3, (int) ((e.position.x - r.getX()) / r.getWidth() * 4.0f)));
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "sgamount")
        {
            dragItem = index;
            sgDragging = true;
            sgStart = soundgoodizerAmount();
            ui::showValueTip (*this, rectOf (item).toNearestInt(), juce::String (sgStart * 100.0f, 0) + "%");
            return;
        }
        if (item.type == Item::display && (juce::String (item.display) == "prismbands" || juce::String (item.display) == "emberbands"))
        {
            // band displays (round 108): drag a crossover line, or click a band to edit it (its page)
            const auto* fx = current();
            if (const auto p = dynui::crossoverNear (*fx, r, e.position.x, 6.0f * scale()); p >= 0)
            {
                dragItem = index;
                crossoverDrag = p;
                return;
            }
            if (! activeLayout().pages.empty())
            {
                page = juce::jlimit (0, (int) activeLayout().pages.size() - 1, dynui::bandAt (*fx, r, e.position.x));
                repaint();
            }
            return;
        }
        if (isGeminiGraph (item)) // round 137
        {
            geminiHandle = geminiNearest (item, e.position);
            if (geminiHandle >= 0)
            {
                dragItem = index;
                geminiDragging = true;
                bar.beginGesture();
            }
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "xy")
        {
            dragItem = index;
            xyDragging = true;
            xyMouse (e, item);
            return;
        }
        if (isRhythmPatterns (item)) // round 144: the rhythm editor's pattern menu
        {
            rhythmPatternsMenu (item);
            return;
        }
        if (isRhythmEdit (item))
        {
            if (e.mods.isPopupMenu())
            {
                rhythmPatternsMenu (item);
                return;
            }
            dragItem = index;
            rhythmMouse (e, item, true);
            return;
        }
        if (isBars (item))
        {
            dragItem = index;
            barsDragging = true;
            barsMouse (e, item, true);
            return;
        }
        if (item.type == Item::display) // the EQ graph
        {
            if (eqMouseDown (e, item) && eqDragging)
                dragItem = index;
            return;
        }

        const auto param = paramOf (item);
        if (param < 0)
            return;

        const auto spec = specFor (item);

        if (item.type == Item::menu && ! ui::isResetClick (e))
        {
            if (kind == EffectKind::parametricEq && item.colour >= 0)
            {
                eqSelected = item.colour;
                repaint();
                peqview::showBandMenu (*this, current()->params, item.colour, [this] (std::vector<std::pair<int, float>> changes) { setParams (changes); });
                return;
            }
            if (kind == EffectKind::hardwareInsert && (param == hwinsert::output || param == hwinsert::input))
            {
                showHwChannelMenu (param == hwinsert::output, r);
                return;
            }
            if (isSendTarget (param)) // FL's list: no destination, then every other insert by name (+ Transporter's send, round 167n)
            {
                juce::PopupMenu menu;
                const auto chosen = flbasic::sendTrack (valueOf (param));
                menu.addItem (tr ("None"), true, chosen <= 0, [this, param] { setValue (param, 0.0f); });
                const auto& mixer = engine.getSong().mixer;
                for (int t = 1; t < (int) mixer.size(); ++t)
                    if (t != track)
                        menu.addItem (juce::String (t) + "  " + insertName (t), engine.getSong().canRoute (track, t) || t == chosen, t == chosen,
                                      [this, param, t] { setValue (param, flbasic::sendTrackValue (t)); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
                return;
            }
            if (kind == EffectKind::lynxTune && param == lynxtune::scale) // round 128: + whole tone, diminished
            {
                const auto* fx = current();
                const auto more = fx != nullptr ? juce::roundToInt (fx->params[lynxtune::moreScales] * 2.0f) : 0;
                juce::PopupMenu menu;
                const auto labels = plugui::optionLabels (item);
                const auto chosen = plugui::optionAt (item, valueOf (param));
                for (int o = 0; o < (int) labels.size(); ++o)
                    menu.addItem (optionTexts (item)[(std::size_t) o], true, more == 0 && o == chosen,
                                  [this, param, v = plugui::optionValue (item, o)] { setParams ({ { param, v }, { lynxtune::moreScales, 0.0f } }); });
                menu.addSeparator();
                menu.addItem (tr ("Whole tone"), true, more == 1, [this] { setParams ({ { lynxtune::moreScales, 0.5f } }); });
                menu.addItem (tr ("Diminished"), true, more == 2, [this] { setParams ({ { lynxtune::moreScales, 1.0f } }); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
                return;
            }
            const auto labels = plugui::optionLabels (item);
            const auto chosen = plugui::optionAt (item, valueOf (param));
            // round 114: wide mode menus step with the arrows at their ends (the reference reverbs' ◀ ▶), the middle opens the list
            if (hasStepArrows (item) && ! labels.empty())
            {
                const auto edge = 18.0f * scale();
                const auto dir = e.position.x < r.getX() + edge ? -1 : e.position.x > r.getRight() - edge ? 1 : 0;
                if (dir != 0)
                {
                    const auto n = (int) labels.size();
                    const auto next = (chosen + dir + n) % n;
                    setValue (param, plugui::optionValue (item, next));
                    ui::flashValueTip (*this, r.toNearestInt(), optionTexts (item)[(std::size_t) next]);
                    return;
                }
            }
            juce::PopupMenu menu;
            for (int o = 0; o < (int) labels.size(); ++o)
                menu.addItem (optionTexts (item)[(std::size_t) o], true, o == chosen,
                              [this, param, v = plugui::optionValue (item, o)] { setValue (param, v); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
            return;
        }

        if (e.mods.isPopupMenu() && listedStepper (item)) // round 170c (#14): its values, then the parameter items
        {
            showStepperMenu (item, true);
            return;
        }
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, spec, valueOf (param), [this, param] (double v) { setValue (param, (float) v); });
            return;
        }

        if (ui::isResetClick (e))
        {
            setValue (param, (float) spec.defaultValue);
            ui::flashValueTip (*this, r.toNearestInt(), spec.text (spec.defaultValue));
            return;
        }

        switch (item.type)
        {
            case Item::radio:
            case Item::tabs:
            {
                const auto n = juce::jmax (1, (int) plugui::optionLabels (item).size());
                auto o = item.type == Item::radio || isVertical (item) ? (int) ((e.position.y - r.getY()) / r.getHeight() * (float) n)
                                                  : (int) ((e.position.x - r.getX()) / r.getWidth() * (float) n);
                if (hasStyle (item, plugui::batToggle) && n == 2) // round tp172d (slice 8): a click flips the bat toggle
                    o = plugui::optionAt (item, valueOf (param)) == 0 ? 1 : 0;
                if (hasStyle (item, plugui::rotarySelector)) // ... and turns the selector to the printed option nearest the click
                {
                    const auto c = selectorKnobArea (item).getCentre();
                    const auto a = std::atan2 (e.position.x - c.x, c.y - e.position.y);
                    auto best = 1e9f;
                    for (int k = 0; k < n; ++k)
                        if (const auto d = std::abs (a - selectorAngle (k, n)); d < best)
                            best = d, o = k;
                }
                if (item.type == Item::radio && item.columns > 0)
                    for (int k = 0; k < n; ++k)
                        if (gridCell (item, k, n).contains (e.position))
                            o = k;
                setValue (param, plugui::optionValue (item, juce::jlimit (0, n - 1, o)));
                return;
            }
            case Item::toggle:
                setValue (param, valueOf (param) >= 0.5f ? 0.0f : 1.0f);
                return;
            case Item::stepper:
                // round 170c (#14): the arrows' halves split at the value box's centre (the caption above it moved the
                // split down before), and a small stepper's arrows get a 10 px hit area
                if (const auto arrow = stepperArrowAt (item, e.position); arrow != 0)
                {
                    stepStepper (item, arrow);
                    return;
                }
                stepperDragIndex = listedStepper (item) ? stepperIndex (item, valueOf (param)) : -1;
                stepperMoved = false;
                break;
            case Item::hslider:
            case Item::vslider:
            case Item::knob:
            case Item::value:
            case Item::menu:
            case Item::section:
            case Item::label:
            case Item::display:
                break;
        }

        dragItem = index;
        drag.begin (e, valueOf (param));
        ui::showValueTip (*this, r.toNearestInt(), spec.text (valueOf (param)));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mxDragging && dragItem >= 0) // round 168x: Maximus' curve
        {
            mxMouseDrag (e, activeLayout().items[(std::size_t) dragItem]);
            return;
        }
        if (lpDragging && dragItem >= 0) // round 173d3: Love Philter's articulator curve
        {
            lpMouseDrag (e, activeLayout().items[(std::size_t) dragItem]);
            return;
        }
        if (wsDragState && dragItem >= 0) // round 165: the WaveShaper's curve
        {
            wsMouseDrag (e, activeLayout().items[(std::size_t) dragItem]);
            return;
        }
        if (gbDragging && dragItem >= 0) // round 168m: Gross Beat's envelope
        {
            gbEnvelopeDrag (e, activeLayout().items[(std::size_t) dragItem]);
            return;
        }
        if (portDragStart >= -1 && dragItem >= 0) // round 190ez: Pitcher's MIDI port, one number per 11 px (FL: 33 px up
        {                                         // took "..." to 2, live FL check 190ew)
            const auto port = juce::jlimit (-1, 255, portDragStart + (int) std::floor ((portDragY - e.position.y) / 11.0f));
            if (current() != nullptr && port != effectMidiInPort (*current()))
                editSlot ([port] (EffectSlot& target) { setEffectMidiInPort (target, port); });
            return;
        }
        if (crossoverDrag >= 0 && dragItem >= 0)
        {
            const auto r = rectOf (activeLayout().items[(std::size_t) dragItem]);
            const auto v = dynui::crossoverValueAt (*current(), r, e.position.x);
            setValue (crossoverDrag, v);
            ui::showValueTip (*this, r.toNearestInt(), valueText (crossoverDrag, v));
            return;
        }
        if (eqDragging)
        {
            eqMouseDrag (e);
            return;
        }
        if (sgDragging && dragItem >= 0)
        {
            const auto v = juce::jlimit (0.0f, 1.0f, sgStart - (float) e.getDistanceFromDragStartY() / (e.mods.isCtrlDown() ? 800.0f : 200.0f));
            setSoundgoodizerAmount (v);
            ui::showValueTip (*this, rectOf (activeLayout().items[(std::size_t) dragItem]).toNearestInt(), juce::String (v * 100.0f, 0) + "%");
            return;
        }
        if (xyDragging && dragItem >= 0)
        {
            xyMouse (e, activeLayout().items[(std::size_t) dragItem]);
            return;
        }
        if (geminiDragging && dragItem >= 0) // round 137
        {
            geminiMouse (e, activeLayout().items[(std::size_t) dragItem]);
            return;
        }
        if (barsDragging && dragItem >= 0)
        {
            barsMouse (e, activeLayout().items[(std::size_t) dragItem], false);
            return;
        }
        if (rhythmDrag >= 0 && dragItem >= 0) // round 144
        {
            rhythmMouse (e, activeLayout().items[(std::size_t) dragItem], false);
            return;
        }
        if (tuneRangeDrag >= 0) // round 147: LYNX Tune's range boundary
        {
            for (const auto& it : activeLayout().items)
                if (it.type == Item::display && juce::String (it.display) == "tunekeys")
                    tuneSetRangeAt (rectOf (it), e.position.x, false);
            return;
        }
        if (dragItem < 0 || ! drag.isActive())
            return;
        const auto& item = activeLayout().items[(std::size_t) dragItem];
        const auto param = paramOf (item);
        const auto spec = specFor (item);
        if (listedStepper (item) && stepperDragIndex >= 0) // round 170c (#14): one value per stepperDragPixels
        {
            stepperMoved = stepperMoved || std::abs (e.getDistanceFromDragStartY()) >= 3;
            const auto v = stepperValue (item, stepperDragIndex + (int) std::trunc (-(float) e.getDistanceFromDragStartY() / stepperDragPixels));
            setValue (param, v);
            ui::showValueTip (*this, rectOf (item).toNearestInt(), spec.text (v));
            return;
        }
        auto v = drag.update (e, spec);
        if (item.type == Item::hslider) // horizontal sliders follow the mouse sideways too
        {
            const auto r = rectOf (item);
            v = juce::jlimit (0.0, 1.0, (double) ((e.position.x - r.getX() - 4.0f * scale()) / juce::jmax (1.0f, r.getWidth() - 8.0f * scale())));
            if (e.mods.isCtrlDown())
                v = drag.update (e, spec);
        }
        setValue (param, (float) v);
        ui::showValueTip (*this, rectOf (item).toNearestInt(), spec.text (v));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        eqDragging = false;
        if (portDragStart >= -1) // round 190ez
        {
            portDragStart = -2;
            dragItem = -1;
        }
        if (wsDragState) // round 165
        {
            wsMouseUp();
            dragItem = -1;
        }
        if (mxDragging) // round 168x
        {
            mxMouseUp();
            dragItem = -1;
        }
        if (lpDragging) // round 173d3
        {
            lpMouseUp();
            dragItem = -1;
        }
        if (gbDragging) // round 168m
        {
            gbEnvelopeUp();
            dragItem = -1;
        }
        tuneKeyRelease(); // LYNX Tune's keyboard without LATCH: the edit lasts while the key is held
        if (crossoverDrag >= 0)
        {
            crossoverDrag = -1;
            dragItem = -1;
            ui::hideValueTip();
        }
        if (geminiDragging) // round 137
        {
            geminiDragging = false;
            geminiHandle = -1;
            dragItem = -1;
            ui::hideValueTip();
        }
        if (rhythmDrag >= 0 || (dragItem >= 0 && isRhythmEdit (activeLayout().items[(std::size_t) dragItem]))) // round 144
        {
            rhythmDrag = -1;
            dragItem = -1;
            ui::hideValueTip();
            repaint();
        }
        if (xyDragging || sgDragging || barsDragging)
        {
            xyDragging = sgDragging = barsDragging = false;
            dragItem = -1;
            ui::hideValueTip();
        }
        if (dragItem >= 0)
        {
            drag.end();
            // round 170c (issue #14: "Oversampling: Off" could not be changed): a click on a stepper's value (no drag)
            // lists its values, as FL's value selectors do; the arrows step, a drag or the wheel moves it
            stepperDragIndex = -1;
            if (const auto& item = activeLayout().items[(std::size_t) dragItem];
                listedStepper (item) && ! stepperMoved && ! e.mods.isPopupMenu())
            {
                dragItem = -1;
                ui::hideValueTip();
                ++stepperMenusShown;
                showStepperMenu (item, false);
                repaint();
                return;
            }
            dragItem = -1;
            ui::hideValueTip();
            repaint();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto index = itemAt (e.position);
        if (index < 0)
            return;
        const auto& item = activeLayout().items[(std::size_t) index];
        if (isRhythmEdit (item)) // round 144: add / remove a tap
        {
            rhythmDoubleClick (e, item);
            return;
        }
        if (isRhythmPatterns (item) || isWaveShaperGraph (item) || isGbAny (item) || isMaximusCurve (item) || isPhilterCurve (item))
            return;
        if (isGeminiGraph (item)) // round 137: an EQ marker = its shelf on / off
        {
            if (juce::String (item.display) == "geminieq")
                if (const auto k = geminiNearest (item, e.position); k >= 0)
                {
                    const auto h = geminiHandles (item)[(std::size_t) k];
                    setParams ({ { h.on, valueOf (h.on) >= 0.5f ? 0.0f : 1.0f } });
                }
            return;
        }
        if (item.type == Item::display) // EQ graph: the band under the mouse back to 0 dB
        {
            if (const auto* fx = current(); fx != nullptr)
                if (const auto b = peqview::bandAt (eqGraph (rectOf (item)), fx->params, e.position, eqRange); b >= 0)
                    setParams ({ { peq::param (b, 2), 0.5f } });
            return;
        }
        if (item.type == Item::radio || item.type == Item::tabs || item.type == Item::toggle || item.type == Item::menu || paramOf (item) < 0)
            return;
        const auto spec = specFor (item);
        setValue (paramOf (item), (float) spec.defaultValue);
    }

    // --- steppers (round 170c, issue #14: "Oversampling: Off" could not be changed on a Mac) --------------------
    // A stepper with a short list of values (its `steps` = the last value's index, 1…64) works like FL's value selectors:
    // the arrows step one value, a click on the value lists them all (a menu; the right-click menu lists them too, above
    // the usual parameter items), a vertical drag moves one value per stepperDragPixels, the wheel one value per notch and a
    // trackpad one value per stepperTrackpadDelta of accumulated scroll (a Mac trackpad sends many tiny smooth deltas).
    static constexpr float stepperDragPixels = 10.0f, stepperTrackpadDelta = 0.12f;
    static bool listedStepper (const LayoutItem& item) noexcept { return item.type == Item::stepper && item.steps > 0 && item.steps <= 64; }

    juce::Rectangle<float> stepperBox (const LayoutItem& item) const
    {
        auto box = rectOf (item);
        if (item.label != nullptr && ! hasStyle (item, plugui::noCaption))
            box.removeFromTop (12.0f * scale());
        return box;
    }

    juce::Rectangle<float> stepperArrows (const LayoutItem& item) const
    {
        auto box = stepperBox (item);
        return box.removeFromRight (juce::jmin (12.0f * scale(), box.getWidth() * 0.4f));
    }

    // the arrows' hit area: at least 10 px wide (the drawn arrows of a small stepper are narrower), never the whole box
    int stepperArrowAt (const LayoutItem& item, juce::Point<float> p) const
    {
        const auto box = stepperBox (item);
        const auto drawn = stepperArrows (item);
        const auto hit = box.withLeft (juce::jmax (box.getX() + box.getWidth() * 0.5f, juce::jmin (drawn.getX(), box.getRight() - 10.0f * scale())));
        if (! hit.contains (p))
            return 0;
        return p.y < box.getCentreY() ? 1 : -1;
    }

    float stepperValue (const LayoutItem& item, int index) const { return juce::jlimit (0.0f, 1.0f, (float) index / (float) item.steps); }
    int stepperIndex (const LayoutItem& item, float value) const { return juce::jlimit (0, item.steps, juce::roundToInt (value * (float) item.steps)); }

    void stepStepper (const LayoutItem& item, int delta)
    {
        const auto param = paramOf (item);
        if (param < 0 || delta == 0)
            return;
        const auto step = item.steps > 0 ? 1.0f / (float) item.steps : 0.01f;
        const auto v = listedStepper (item) ? stepperValue (item, stepperIndex (item, valueOf (param)) + delta)
                                            : juce::jlimit (0.0f, 1.0f, valueOf (param) + (float) delta * step);
        setValue (param, v);
        ui::reportTweak (specFor (item));
        ui::flashValueTip (*this, rectOf (item).toNearestInt(), valueText (param, valueOf (param)));
    }

    // every value of a listed stepper, the current one ticked (FL's value selectors list theirs)
    juce::PopupMenu stepperMenu (const LayoutItem& item)
    {
        juce::PopupMenu menu;
        const auto param = paramOf (item);
        if (param < 0 || ! listedStepper (item))
            return menu;
        const auto now = stepperIndex (item, valueOf (param));
        for (int k = 0; k <= item.steps; ++k)
            menu.addItem (valueText (param, stepperValue (item, k)), true, k == now,
                          [safe = juce::Component::SafePointer<NativeEffectEditor> (this), param, v = stepperValue (item, k)] {
                              if (safe != nullptr)
                                  safe->setValue (param, v);
                          });
        return menu;
    }

    void showStepperMenu (const LayoutItem& item, bool withParameterItems)
    {
        auto menu = stepperMenu (item);
        if (menu.getNumItems() == 0)
            return;
        const auto param = paramOf (item);
        const auto spec = specFor (item);
        if (withParameterItems)
        {
            menu.addSeparator();
            ui::addValueItems (menu, *this, &engine, spec, valueOf (param), [safe = juce::Component::SafePointer<NativeEffectEditor> (this), param] (double v) {
                if (safe != nullptr)
                    safe->setValue (param, (float) v);
            });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (stepperBox (item).toNearestInt())));
    }

public:
    // Round 170c (#14): every stepper of the current page driven by synthetic input — a click on each arrow, a click on
    // the value (its menu), a drag up and down, wheel notches, trackpad scrolls. One line per stepper: OK / FAIL and why.
    juce::String stepperSelfTest()
    {
        juce::String report;
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto& items = activeLayout().items;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            if (item.type != Item::stepper || ! visible (item))
                continue;
            const auto param = paramOf (item);
            juce::String line = juce::String (effectInfo (kind).name) + " / " + (item.param != nullptr ? item.param : "?") + " (steps " + juce::String (item.steps) + "): ";
            if (param < 0)
            {
                report << line << "FAIL no parameter\n";
                continue;
            }
            // a control kept in FL's state (Maximus, Vocodex...) is editable only once the slot has FL's state
            if (isStateParam (param) && current() != nullptr && ! stateTable().get (current()->state, plugui::stateIndex (kind, param)))
            {
                report << line << "SKIP state-backed, the slot has no FL state\n";
                continue;
            }
            juce::StringArray fails;
            const auto now = juce::Time::getCurrentTime();
            auto event = [&] (juce::Point<float> p, juce::ModifierKeys mods, std::optional<juce::Point<float>> down = {}) {
                return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, this, this, now, down.value_or (p), now, 1, false);
            };
            const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
            // (a state parameter's edit reaches the slot through the engine's message queue)
            auto settle = [this] { engine.dispatchPendingMessages(); };
            auto set = [&] (float v) { setValue (param, v); settle(); };
            auto click = [&] (juce::Point<float> p) {
                mouseDown (event (p, left));
                mouseUp (event (p, juce::ModifierKeys()));
                juce::PopupMenu::dismissAllActiveMenus();
                settle();
            };
            const auto box = stepperBox (item);
            const auto arrows = stepperArrows (item);
            const auto upAt = juce::Point<float> (arrows.getCentreX(), box.getY() + box.getHeight() * 0.25f);
            const auto downAt = juce::Point<float> (arrows.getCentreX(), box.getY() + box.getHeight() * 0.75f);
            const auto body = juce::Point<float> (box.getX() + juce::jmax (2.0f, (arrows.getX() - box.getX()) * 0.5f), box.getCentreY());
            const auto start = valueOf (param);
            const auto low = 0.0f;

            // arrows: from the lowest value, up then down
            set (low);
            click (upAt);
            const auto afterUp = valueOf (param);
            if (! (afterUp > low))
                fails.add ("up arrow");
            click (downAt);
            if (std::abs (valueOf (param) - low) > 1.0e-4f)
                fails.add ("down arrow (" + valueText (param, low) + " -> " + valueText (param, afterUp) + " -> " + valueText (param, valueOf (param))
                           + ", raw " + juce::String (afterUp, 4) + " / " + juce::String (valueOf (param), 4) + ")");

            if (listedStepper (item))
            {
                // the value's menu: every value, choosing the last one sets it
                auto menu = stepperMenu (item);
                if (menu.getNumItems() != item.steps + 1)
                    fails.add ("menu has " + juce::String (menu.getNumItems()) + " items");
                std::function<void()> last;
                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    last = it.getItem().action;
                if (last)
                    last();
                settle();
                if (std::abs (valueOf (param) - 1.0f) > 1.0e-4f)
                    fails.add ("menu choice");
                // a click on the value opens it (and changes nothing by itself)
                set (low);
                stepperMenusShown = 0;
                click (body);
                if (stepperMenusShown != 1)
                    fails.add ("click on the value shows no menu");
                if (std::abs (valueOf (param) - low) > 1.0e-4f)
                    fails.add ("click on the value changed it");

                // drag up three values, then back down
                set (low);
                mouseDown (event (body, left));
                mouseDrag (event (body.translated (0.0f, -3.4f * stepperDragPixels), left, body));
                settle();
                const auto dragged = stepperIndex (item, valueOf (param));
                mouseDrag (event (body, left, body));
                mouseUp (event (body, juce::ModifierKeys(), body));
                juce::PopupMenu::dismissAllActiveMenus();
                settle();
                if (dragged != juce::jmin (3, item.steps))
                    fails.add ("drag up gave value " + juce::String (dragged));
                if (stepperIndex (item, valueOf (param)) != 0)
                    fails.add ("drag back");

                // a wheel notch up, then a trackpad scroll (many small smooth deltas) up
                set (low);
                juce::MouseWheelDetails notch { 0.0f, 0.15f, false, false, false };
                mouseWheelMove (event (body, juce::ModifierKeys()), notch);
                settle();
                if (stepperIndex (item, valueOf (param)) != 1)
                    fails.add ("wheel notch gave value " + juce::String (stepperIndex (item, valueOf (param))));
                set (low);
                stepperWheel = 0.0f;
                juce::MouseWheelDetails pad { 0.0f, 0.02f, false, true, false };
                for (int k = 0; k < 7; ++k)
                {
                    mouseWheelMove (event (body, juce::ModifierKeys()), pad);
                    settle();
                }
                if (stepperIndex (item, valueOf (param)) != 1)
                    fails.add ("trackpad scroll gave value " + juce::String (stepperIndex (item, valueOf (param))));
            }
            set (start);
            report << line << (fails.isEmpty() ? juce::String ("OK") : "FAIL " + fails.joinIntoString (", ")) << "\n";
        }
        return report;
    }

private:
    int stepperMenusShown = 0;     // (the self-test counts the menus a click opens)
    float stepperWheel = 0.0f;     // a trackpad's accumulated scroll over a listed stepper
    int stepperDragIndex = -1;     // the value a drag started from (-1 = no stepper drag)
    int portDragStart = -2;        // round 190ez: Pitcher's MIDI port when its drag started (-2 = no drag)
    float portDragY = 0.0f;
    bool stepperMoved = false;     // that drag moved (then its mouse-up opens no menu)

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto index = itemAt (e.position);
        if (index < 0)
            return;
        const auto& item = activeLayout().items[(std::size_t) index];
        if (isWaveShaperGraph (item) || isGbAny (item) || isMaximusCurve (item) || isPhilterCurve (item))
            return;
        if (item.type == Item::display && juce::String (item.display) == "tssenvelope") // FL's envelope view speed
        {
            if (auto* view = const_cast<transmitter::EnvelopeView*> (transmitter::envelopeOf (runningEffect().get())))
            {
                const auto speed = juce::jlimit (0, 100, view->speed.load() + (wheel.deltaY > 0 ? 2 : -2));
                view->speed.store (speed);
                view->columnLength.store (transmitter::columnSamples (juce::jmax (1.0, engine.getTransport().sampleRate.load()), speed));
                repaint();
            }
            return;
        }
        if (item.type == Item::display) // EQ graph: Q / bandwidth of the band under the mouse (or the selected one)
        {
            const auto* fx = current();
            if (fx == nullptr)
                return;
            auto b = peqview::bandAt (eqGraph (rectOf (item)), fx->params, e.position, eqRange);
            if (b < 0)
                b = eqSelected;
            if (b < 0)
                return;
            eqSelected = b;
            const auto q = fx->params[(std::size_t) peq::param (b, 3)];
            // wheel up = narrower; FL's width grows the other way
            const auto step = peq::flLaw (fx->params) ? -wheel.deltaY * 0.15f : wheel.deltaY * 0.15f;
            setParams ({ { peq::param (b, 3), q + step } });
            return;
        }
        const auto param = paramOf (item);
        if (param < 0)
            return;
        auto spec = specFor (item);
        if (item.type == Item::radio || item.type == Item::tabs)
        {
            const auto n = (int) plugui::optionLabels (item).size();
            const auto o = plugui::optionAt (item, valueOf (param)) + (wheel.deltaY > 0 ? (item.type == Item::radio ? -1 : 1) : (item.type == Item::radio ? 1 : -1));
            setValue (param, plugui::optionValue (item, juce::jlimit (0, n - 1, o)));
            return;
        }
        if (listedStepper (item)) // round 170c (#14): one value per notch; a trackpad's small deltas add up to one
        {
            const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
            if (delta == 0.0f || ui::wheelScrolling())
                return;
            const auto sign = wheel.isReversed ? -1.0f : 1.0f;
            if (! wheel.isSmooth)
            {
                stepStepper (item, delta * sign > 0.0f ? 1 : -1);
                return;
            }
            stepperWheel += delta * sign;
            if (std::abs (stepperWheel) >= stepperTrackpadDelta)
            {
                stepStepper (item, stepperWheel > 0.0f ? 1 : -1);
                stepperWheel = 0.0f;
            }
            return;
        }
        if (item.type == Item::stepper && item.steps > 1000 && wheel.deltaY != 0.0f) // a fine count (the hardware insert's delay in samples): one per notch, Shift ×10
        {
            const auto step = (e.mods.isShiftDown() ? 10.0f : 1.0f) / (float) item.steps;
            setValue (param, juce::jlimit (0.0f, 1.0f, valueOf (param) + (wheel.deltaY > 0.0f ? step : -step)));
            ui::flashValueTip (*this, rectOf (item).toNearestInt(), valueText (param, valueOf (param)));
            return;
        }
        if (const auto v = ui::wheelValue (valueOf (param), e, wheel, spec))
        {
            setValue (param, (float) *v);
            ui::flashValueTip (*this, rectOf (item).toNearestInt(), spec.text (*v));
        }
    }

    ui::Hint hintAt (juce::Point<int> position) override
    {
        const auto p = position.toFloat();
        switch (headerItemAt (p))
        {
            case 1: return { tr ("Options: presets, reset, bypass"), {}, false, true };
            case 2: return { tr ("Preset: click for the list of FL presets of this plugin"), presetName, false, true };
            case 3: return { tr ("Previous preset"), {}, false, false };
            case 4: return { tr ("Next preset"), {}, false, false };
            case 5: return { bar.hintFor (bar.partAt (p)), bar.presetLabel(), false, true };
            case 6: return { tr ("Sidechain input: the insert this effect listens to. Picking one routes it here at level 0; it keeps its own outputs  [LMB] choose"),
                             {}, false, true };
            default: break;
        }
        const auto index = itemAt (p);
        if (index < 0)
            return {};
        const auto& item = activeLayout().items[(std::size_t) index];
        if (item.type == Item::display && juce::String (item.display) == "sgamount")
            return { tr ("Amount: drag up or down (Ctrl: fine)"), juce::String (soundgoodizerAmount() * 100.0f, 0) + "%", false, false };
        if (item.type == Item::display && juce::String (item.display) == "sgpresets")
            return { sgApprox ? tr ("Soundgoodizer preset A to D (approximated: FL Studio's preset files were not found)")
                              : tr ("Soundgoodizer preset A to D (read from your FL installation)"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "vumeter")
            return { tr ("VU meter: click to show the gain reduction, the output or the input level"), vuMode == 2 ? tr ("Output") : vuMode == 1 ? tr ("Input") : tr ("Gain reduction"), false, false };
        if (item.type == Item::display && juce::String (item.display) == "taptempo")
            return { tr ("Tap tempo: click in time (two taps or more) to set the delay"), tapBpm > 0.0 ? juce::String (tapBpm, 1) + " BPM" : juce::String(), false, false };
        if (item.type == Item::display && juce::String (item.display) == "iometer")
            return { tr ("Input and output level (peak, dBFS); the numbers hold the highest peak; OUT turns red when the output clips; click to reset"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "gcmeter")
            return { tr ("Gain change (dB): down = reduction, up = the expansion's boost below 1:1; the number holds the largest change; click to reset"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "limitpill")
            return { tr ("Output limiter: lit while it keeps the output under 0 dBFS"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "outmeter")
            return { tr ("Output level (peak, dBFS): the lamp lights when the output clips, the number holds the highest peak; click to reset"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "xy")
            return { tr ("Drag the point: both values at once"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "tunekeys") // round 147
            return { tuneKeysHint (rectOf (item), p), {}, false, false };
        if (isRhythmEdit (item)) // round 144
            return { tr ("Rhythm taps: drag up / down = level, sideways = time; double-click = add or remove a tap; right-click = patterns"), {}, false, false };
        if (isRhythmPatterns (item))
            return { tr ("Load a tap pattern (LYNX SHARE's own), or go back to even spacing"), {}, false, false };
        if (isWaveShaperGraph (item)) // round 165
            return { wsHint (item, p), {}, false, true };
        if (isMaximusCurve (item)) // round 168x
            return { mxHint (item, p), {}, false, true };
        if (isPhilterCurve (item)) // round 173d3
            return { lpHint (item, p), {}, false, true };
        if (item.type == Item::display && juce::String (item.display) == "pitchport") // round 190ez
            return { tr ("MIDI input port number: drag up / down (\"...\" = none). The MIDI Out channels on this port play Pitcher's MIDI mode and Harmonize"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "equosend") // round 173d3: FL's hint
            return { tr ("Send track number"), current() != nullptr && current()->sendTracks[0] > 0 ? insertName (current()->sendTracks[0]) : juce::String ("---"), false, false };
        if (isGbAny (item)) // round 168m
            return { gbHint (item, p), {}, false, true };
        if (item.type == Item::display)
            return { tr ("Drag a handle (frequency and gain); mouse wheel = Q; right-click = type; double-click = 0 dB"), {}, false, true };
        if (hasStyle (item, plugui::pageTabs))
            return { tr ("Page: switch the controls shown"), {}, false, false };
        if (isMixItem (item) && captionArea (item).contains (p))
            return { tr ("MIX label: click to keep the Mix while presets change (mix lock)"), pluginheader::isMixLocked (kind) ? tr ("locked") : tr ("unlocked"), false, false };
        const auto spec = specFor (item);
        if (const auto* fx = current(); fx != nullptr && plugui::isIdle (kind, item, fx->params)) // round 170c
            return { juce::String (spec.name) + ": " + tr ("no effect with the current mode (another engine is in use)"), spec.text (valueOf (paramOf (item))), false, false };
        if (item.type == Item::stepper && item.steps > 0 && item.steps <= 64) // round 170c (issue #14)
            return { juce::String (spec.name) + ": " + tr ("arrows step, click for the list, drag or mouse wheel to change"), spec.text (valueOf (paramOf (item))), false, false };
        return ui::valueHint (spec, valueOf (paramOf (item)));
    }

    // --- presets ---------------------------------------------------------------------------------------

    juce::Array<juce::File> presetFiles() const
    {
        juce::Array<juce::File> all;
        const auto roots = findFlStudioRoots();
        if (roots.data.empty() || layout == &plugui::soundgoodizerLayout()) // Soundgoodizer: its presets A–D are on the page
            return all;
        // FL's list and order (round 167o, FLEngine 0x12b6c70 / 0x662a40): FL's folder and the user's merged, a
        // folder's own presets before its subfolders, numbers as numbers
        for (const auto& plugin : flPluginsForEffect (kind))
            for (const auto& p : presetcycle::listPresets (presetcycle::pluginPresetFolders (roots, false, plugin)))
                all.add (juce::File (juce::String::fromUTF8 (p.path.c_str())));
        return all;
    }

    void loadPreset (const juce::File& f)
    {
        juce::MemoryBlock data;
        if (! f.loadFileAsData (data))
            return;
        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
        auto fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), engine.getSong().bpm, natives::flDataReader (flStudioAccess()));
        if (! fx || fx->kind != kind)
            return;
        presetName = f.getFileNameWithoutExtension();
        if (usesBar())
        {
            bar.beginGesture();
            bar.willEdit();
            fx->presetName = presetName.toStdString();
        }
        engine.editSong ([t = track, s = slot, fx = *fx] (Song& song) {
            auto& target = song.mixer[(std::size_t) t].effects[(std::size_t) s];
            const auto enabled = target.enabled;
            const auto mix = target.mix;
            target = fx;
            target.enabled = enabled;
            target.mix = mix;
        });
        repaint();
    }

    void stepPreset (int delta)
    {
        const auto files = presetFiles();
        if (files.isEmpty())
            return;
        auto at = -1;
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == presetName)
                at = i;
        at = presetcycle::step (at, delta, files.size());
        loadPreset (files[at]);
        ui::postHint ({ presetName, juce::String (presetcycle::positionText (at, files.size())) }, 2500); // FL's "n / N"
    }

    // FL's factory presets of this plugin, in folders (the header's preset menu)
    void addFlPresets (juce::PopupMenu& presets)
    {
        const auto files = presetFiles();
        std::map<juce::String, juce::PopupMenu> folders;
        for (const auto& f : files)
            folders[f.getParentDirectory().getFileName()].addItem (f.getFileNameWithoutExtension(), true, f.getFileNameWithoutExtension() == presetName,
                                                                   [this, f] { loadPreset (f); });
        if (folders.size() == 1)
            presets = folders.begin()->second;
        else
            for (auto& [name, menu] : folders)
                presets.addSubMenu (name, menu);
    }

    void showOptionsMenu (bool presetsOnly)
    {
        juce::PopupMenu presets;
        const auto files = presetFiles();
        std::map<juce::String, juce::PopupMenu> folders;
        const auto roots = findFlStudioRoots();
        for (const auto& f : files)
        {
            const auto folder = f.getParentDirectory().getFileName();
            auto& menu = folders[folder];
            menu.addItem (f.getFileNameWithoutExtension(), true, f.getFileNameWithoutExtension() == presetName, [this, f] { loadPreset (f); });
        }
        if (folders.size() == 1)
            presets = folders.begin()->second;
        else
            for (auto& [name, menu] : folders)
                presets.addSubMenu (name, menu);

        if (presetsOnly)
        {
            if (presets.getNumItems() == 0)
                presets.addItem (tr ("No FL presets found for this plugin"), false, false, [] {});
            presets.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (presetBox().toNearestInt())));
            return;
        }

        juce::PopupMenu menu;
        if (presets.getNumItems() > 0)
            menu.addSubMenu (tr ("Presets"), presets);
        if (showsSidechain()) // round 177b
        {
            juce::PopupMenu side;
            pluginheader::addSidechainPicker (side, engine, track, slot);
            menu.addSubMenu (kind == EffectKind::vocoder || kind == EffectKind::vocodex ? tr ("Modulator from") : tr ("Sidechain from"), side);
        }
        skin::addStyleMenu (menu, styleKey(), [safe = juce::Component::SafePointer<NativeEffectEditor> (this)] {
            if (safe != nullptr) safe->repaint();
        });
        menu.addItem (tr ("Reset to defaults"), [this] {
            const auto& info = effectInfo (kind);
            presetName = {};
            if (usesBar())
            {
                bar.beginGesture();
                bar.willEdit();
            }
            auto bytes = current() != nullptr ? current()->state : std::vector<std::uint8_t> {};
            if (stateBacked())
            {
                const auto& table = stateTable();
                for (int p = 0; p < (int) table.params.size(); ++p)
                    bytes = table.with (bytes, p, table.params[(std::size_t) p].defaultValue);
            }
            engine.editSong ([t = track, s = slot, info, bytes, state = stateBacked()] (Song& song) {
                auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) s];
                for (std::size_t i = 0; i < info.params.size() && i < fx.params.size(); ++i)
                    fx.params[i] = info.params[i].defaultValue;
                fx.presetName.clear();
                if (state)
                    fx.state = bytes;
            });
        });
        // switches that FL keeps out of the panel (in its own menus): on / off here
        if (! activeLayout().menuSwitches.empty())
        {
            menu.addSeparator();
            for (const auto* name : activeLayout().menuSwitches)
                if (const auto p = plugui::paramIndex (kind, name); p >= 0)
                {
                    const auto on = valueOf (p) >= 0.5f;
                    menu.addItem (utf8 (name), true, on, [this, p, on] { setValue (p, on ? 0.0f : 1.0f); });
                }
            menu.addSeparator();
        }
        const auto* fx = current();
        // (round 190ez: the MIDI input port of the effects that take notes left this menu, as FL 2026's wrapper menu
        // has none: Pitcher sets it in its own panel's bottom line, Gross Beat in its Options menu, live FL check 190ew)
        menu.addItem (tr ("Bypass"), true, fx != nullptr && ! fx->enabled, [this] {
            editSlot ([] (EffectSlot& target) { target.enabled = ! target.enabled; });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (menuButton().toNearestInt())));
    }

    // None, Port 0 … 31 (the plugin wrapper menu's list; a port set higher, e.g. by an FL project, is listed too)
    juce::PopupMenu midiPortMenu()
    {
        const auto* fx = current();
        const auto now = fx != nullptr ? effectMidiInPort (*fx) : -1;
        juce::PopupMenu ports;
        auto set = [this] (int port) { editSlot ([port] (EffectSlot& target) { setEffectMidiInPort (target, port); }); };
        ports.addItem (tr ("None"), true, now < 0, [set] { set (-1); });
        for (int port = 0; port < 32; ++port)
            ports.addItem (tr ("Port {0}", { juce::String (port) }), true, now == port, [set, port] { set (port); });
        if (now >= 32)
            ports.addItem (tr ("Port {0}", { juce::String (now) }), true, true, [] {});
        return ports;
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

public:
    // round 166f: the analyser part of the timer, for off-screen pictures (--eq166fshot)
    void tickDisplaysForSnapshot()
    {
        if (kind == EffectKind::parametricEq && ! node.locate)
            for (const auto& item : activeLayout().items)
                if (item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "eq") == 0)
                    eqAnalyser.update (engine, track, slot, true, eqGraph (rectOf (item)));
    }

private:
    void timerCallback() override
    {
        if (! isShowing()) return; // a hidden editor costs nothing
        if (! node.locate && shownSlot.changed (engine.getAutomationDisplay(), track, slot)) // the controls follow their automation
            repaint();
        if (levelMeter && ! node.locate) // the dynamics curves' live trail (the wells themselves: levelFrame)
        {
            levelTrail[(std::size_t) levelTrailAt] = { levelDb[0], levelDb[1] };
            levelTrailAt = (levelTrailAt + 1) % (int) levelTrail.size();
        }
        if (kind == EffectKind::parametricEq && ! node.locate) // round 166f: the spectrum / heatmap of the EQ's output
            for (const auto& item : activeLayout().items)
                if (item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "eq") == 0)
                {
                    const auto graph = eqGraph (rectOf (item));
                    if (eqAnalyser.update (engine, track, slot, isMouseOver (true) || hasKeyboardFocus (true), graph))
                        repaint (graph.getSmallestIntegerContainer().expanded (2));
                }
        if (! liveDisplays) return;
        learnScaleFromMidi();
        if (kind == EffectKind::hardwareInsert)
            hwPingTick();
        if (kind == EffectKind::transmitter) // the envelope view scrolls while columns arrive
        {
            const auto* view = transmitter::envelopeOf (runningEffect().get());
            const auto written = view != nullptr ? view->written.load (std::memory_order_relaxed) : 0u;
            if (written != envelopeSeen)
            {
                envelopeSeen = written;
                for (const auto& item : activeLayout().items)
                    if (item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "tssenvelope") == 0 && visible (item))
                        repaint (rectOf (item).getSmallestIntegerContainer());
            }
            return;
        }
        // an idle display (silent track: meters unchanged for longer than the longest history) is not redrawn
        float meters = 0.0f;
        for (int k = 0; k < 6; ++k) meters += liveMeter (k) * (float) (k + 1);
        quietFrames = std::abs (meters - lastMeters) > 1e-6f ? 0 : quietFrames + 1;
        lastMeters = meters;
        if (quietFrames > 300 && ! demoDisplays) return;
        bool history = false;
        for (const auto& item : activeLayout().items)
        {
            if (item.type == Item::display && item.display != nullptr && (std::strcmp (item.display, "pitchgraph") == 0 || std::strcmp (item.display, "pitchtrace") == 0
                                                                       || (std::strcmp (item.display, "pitchring") == 0 && ! pitchTraced())) && visible (item))
            {
                if (std::strcmp (item.display, "pitchring") == 0 || std::strcmp (item.display, "pitchtrace") == 0)
                {
                    // round 128: what is sung, for AUTO KEY (time per pitch class, fading over ~30 s)
                    for (auto& x : keyHistogram)
                        x *= 0.999;
                    if (const auto sungNote = liveMeter (0) * 127.0f; sungNote > 0.5f)
                        keyHistogram[(std::size_t) (((int) std::lround (sungNote) % 12 + 12) % 12)] += 1.0;
                    if (autoKeyFlash > 0)
                        --autoKeyFlash;
                    if (tuneSendFlash > 0 && --tuneSendFlash == 0) // round 150
                        repaint();
                }
                // LYNX Tune's pitch history (30 points a second)
                pitchHistory.push_back ({ liveMeter (0) * 127.0f, liveMeter (1) * 127.0f, (liveMeter (2) - 0.5f) * 4.0f });
                if (pitchHistory.size() > pitchHistoryLength)
                    pitchHistory.erase (pitchHistory.begin(), pitchHistory.begin() + (std::ptrdiff_t) (pitchHistory.size() - pitchHistoryLength));
                repaint (rectOf (item).getSmallestIntegerContainer());
            }
            if (item.type == Item::display && item.display != nullptr && (std::strcmp (item.display, "grmeter") == 0 || std::strcmp (item.display, "grleds") == 0 || std::strcmp (item.display, "vumeter") == 0 || std::strcmp (item.display, "iometer") == 0 || std::strcmp (item.display, "outmeter") == 0 || std::strcmp (item.display, "limitpill") == 0 || std::strcmp (item.display, "gcmeter") == 0 || std::strcmp (item.display, "pitchring") == 0 || dynui::isLive (item.display)) && visible (item))
            {
                history = history || std::strcmp (item.display, "grhistory") == 0;
                repaint (rectOf (item).getSmallestIntegerContainer());
            }
        }
        if (history)
            dynHistory.push (liveMeter (0), liveMeter (2), liveMeter (3));
        for (const auto& item : activeLayout().items) // round 168r: the Peak Controller's outputs scroll
            if (item.type == Item::display && designui::isLive (item.display) && visible (item))
            {
                designHistory.push (liveMeter (0), liveMeter (1), liveMeter (2));
                repaint (rectOf (item).getSmallestIntegerContainer());
                break;
            }
    }

    designui::History designHistory; // round 168r: the Peak Controller's outputs (peakmeter)
    std::uint32_t envelopeSeen = 0; // the Transmitter's envelope columns already drawn
    AudioEngine& engine;
    int track, slot;
    NodeTarget node;
    EffectKind kind = EffectKind::eq;
    const plugui::Layout* layout = nullptr;
    int page = 0;
    int hoverItem = -1, hoverHeader = 0, dragItem = -1;
    juce::Colour accent { skin::colour::crimson }; // the plugin family's accent (ui/PluginSkin.h); the art's when Themed
    juce::Colour familyAccent { skin::colour::crimson };
    std::unique_ptr<skin::ArtLayer> artLayer; // round 170j: the art's moving part
    skin::Animator anim;                            // hover fades (runs only while something moves)
    int quietFrames = 1000; // frames since the live meters last moved (the displays start at rest)
    float lastMeters = 0.0f;
    bool liveDisplays = false;            // the layout has live meters / graphs (redrawn by the timer)
    // round 170c: the "meter" wells' live levels (dBFS: input, output), their holds and the slot's key
    bool levelMeter = false;
    std::array<float, 2> levelDb { -60.0f, -60.0f }, levelHoldDb { -60.0f, -60.0f };
    std::array<double, 2> levelHoldMs { 0.0, 0.0 };
    std::array<std::pair<float, float>, 45> levelTrail {};
    int levelTrailAt = 0;
    double levelFrameMs = 0.0;
    // round 170c: the level meter wells (input / output peaks, held 1 s, falling 24 dB/s); round 174e: at the display's
    // refresh (they were on the 30 Hz timer), only the wells
    bool levelFrame (double nowMs)
    {
        if (! levelMeter || node.locate)
            return false;
        auto* times = engine.getProcessTimes();
        if (times == nullptr || levelKey() >= times->levelWanted.size())
            return false;
        const auto elapsed = levelFrameMs > 0.0 ? juce::jlimit (0.0, 250.0, nowMs - levelFrameMs) : 1000.0 / 30.0;
        levelFrameMs = nowMs;
        const auto fall = (float) (24.0 * elapsed / 1000.0);
        bool moved = false;
        for (int k = 0; k < 2; ++k)
        {
            const auto peak = times->slotLevels[levelKey() * 2 + (std::size_t) k].exchange (0.0f, std::memory_order_relaxed);
            const auto db = juce::jlimit (-60.0f, 6.0f, juce::Decibels::gainToDecibels (peak, -100.0f));
            auto& shown = levelDb[(std::size_t) k];
            auto& hold = levelHoldDb[(std::size_t) k];
            const auto before = shown, holdBefore = hold;
            shown = db >= shown ? db : juce::jmax (db, shown - fall);
            if (db >= hold)
            {
                hold = db;
                levelHoldMs[(std::size_t) k] = 1000.0;
            }
            else if ((levelHoldMs[(std::size_t) k] -= elapsed) < 0.0)
                hold = juce::jmax (shown, hold - fall);
            moved = moved || std::abs (shown - before) > 0.01f || std::abs (hold - holdBefore) > 0.01f;
        }
        if (moved)
            for (const auto& item : activeLayout().items)
                if (item.type == Item::display && item.display != nullptr && levelDisplay (item.display) && visible (item))
                    repaint (rectOf (item).getSmallestIntegerContainer().expanded (2));
        return moved;
    }
    static bool levelDisplay (const char* id)
    {
        return std::strcmp (id, "meter") == 0 || std::strcmp (id, "limiter") == 0 || std::strcmp (id, "compressor") == 0 || std::strcmp (id, "dynamics") == 0;
    }
    std::size_t levelKey() const { return (std::size_t) juce::jmax (0, track) * (std::size_t) maxEffectSlots + (std::size_t) juce::jmax (0, slot); }
    mutable ShownEffectSlot shownSlot;    // the slot with its automation's values (current())
    ui::ValueDrag drag;
    ui::FrameTicker levelFrames { *this, [this] (double ms) { return levelFrame (ms); } };
    juce::String presetName;
    pluginheader::PresetBar bar { engine, track, slot }; // undo / redo, A / B, presets (round 129)
    peqview::Analyser eqAnalyser; // round 166f: Parametric EQ 2's spectrum and heatmap
    int eqSelected = -1;
    bool eqDragging = false, xyDragging = false, sgDragging = false;
    struct PitchPoint { float sung, target, shift; }; // MIDI note numbers (0 = none) and the applied correction in semitones
    static constexpr std::size_t pitchHistoryLength = 240; // 8 s
    std::vector<PitchPoint> pitchHistory;
    // LYNX Tune (round 128): HOLD, the keyboard's mode / latch, AUTO KEY's histogram
    bool tuneHold = false, tuneRemove = true, tuneLatch = true;
    int tuneHeldMap = -1;          // round 152: the key-map parameter held while the key is down (latch off)
    float tuneHeldMapValue = 0.0f;
    PitchPoint holdPoint { 0.0f, 0.0f, 0.0f };
    int tuneHeld = -1, autoKeyFlash = 0;
    std::pair<float, float> tuneHeldValues { 1.0f, 0.0f };
    float tuneHeldDir = 0.0f; // round 147
    std::array<double, 12> keyHistogram {};
    // is the scrolling pitch trace on the current page? (it then feeds the history; else the ring does)
    bool pitchTraced() const
    {
        for (const auto& item : activeLayout().items)
            if (item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "pitchtrace") == 0 && visible (item))
                return true;
        return false;
    }
    float displayCentre = 0.0f;
    float sgStart = 0.0f;
    std::array<std::vector<std::uint8_t>, 4> sgPresets {}; // the installation's preset files A–D (read on demand)
    bool sgApprox = false; // a Soundgoodizer approximation container (no FL preset files) shown on the Soundgoodizer page
    juce::Point<float> eqDragOffset;
    dynui::History dynHistory; // the dynamics suite's scrolling gain-reduction graph
    int crossoverDrag = -1;    // the crossover parameter being dragged on a band display
    float vuNeedle = 0.0f;     // the VU display's needle (dB below 0 VU, with inertia)
    std::array<skin::Ballistics, 2> ioMeters {};
    bool ioClip = false;         // the "iometer" display's clip lamp (round 135w)
    skin::Ballistics outMeter;   // the "outmeter" display (round 135w): level, highest peak since the last click, clip lamp
    float outMax = 0.0f;
    bool outClip = false;
    juce::uint32 lastTap = 0;          // the tap pad (round 129)
    std::vector<double> tapIntervals;
    double tapBpm = 0.0; // the "iometer" display: input and output (round 129)
    int vuMode = 0;            // what the VU shows: 0 gain reduction, 1 input level, 2 output level (0 VU = −18 dBFS)
    // round 165x: Glow keeps the VU display as a parameter (saved with the project, translated from the reference's state)
    void syncVuMode()
    {
        if (kind == EffectKind::glowLeveler)
            if (const auto* fx = current(); fx != nullptr && fx->params.size() > (std::size_t) mixsuite::glow::vuDisplay)
                vuMode = mixsuite::glow::vuModeOf (fx->params[(std::size_t) mixsuite::glow::vuDisplay]);
    }
    void setVuMode (int mode)
    {
        vuMode = mode;
        if (kind == EffectKind::glowLeveler && current() != nullptr)
            setParams ({ { mixsuite::glow::vuDisplay, mode == 1 ? 0.5f : mode == 2 ? 1.0f : 0.0f } });
    }
    juce::Point<float> mouseAt { -1.0f, -1.0f };
};

// --- the window ------------------------------------------------------------------------------------

class NativeEffectWindow final : public juce::DocumentWindow
{
public:
    NativeEffectWindow (AudioEngine& engine, int track, int slot, const juce::String& title)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          gone (engine, track, slot, [this] { closeButtonPressed(); })
    {
        setUsingNativeTitleBar (true);
        auto* editor = new NativeEffectEditor (engine, track, slot, 1.25f, 0);
        const auto design = editor->designSize();
        setContentOwned (editor, true);
        setResizable (true, false);
        // keep FL's proportions while resizing (the presets bar has a fixed height)
        if (auto* c = getConstrainer())
        {
            c->setMinimumSize (juce::jmax (design.x / 2, editor->minimumWidth()), design.y / 2); // round 170k2: the name fits
            c->setMaximumSize (design.x * 3, design.y * 3 + 200);
        }
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        markLinkedWindow (*this, -1, track); // FL's Auto select linked modules (round 187a)
        dockOrKeepAbove (*this); // FL's docked plugin window (round 190ev)
        skin::playOpenAnimation (*editor, editor->editorStyle()); // round 126: fade / scale in
    }

    ~NativeEffectWindow() override { editorlimit::forget (this); }

    void closeButtonPressed() override;

    void activeWindowStatusChanged() override
    {
        DocumentWindow::activeWindowStatusChanged();
        if (isActiveWindow())
            editorlimit::touch (this);
    }

private:
    ui::SlotGoneWatcher gone;
};

std::map<std::pair<int, int>, std::unique_ptr<NativeEffectWindow>>& windows()
{
    static std::map<std::pair<int, int>, std::unique_ptr<NativeEffectWindow>> open;
    return open;
}

void NativeEffectWindow::closeButtonPressed()
{
    for (auto& [key, window] : windows())
        if (window.get() == this)
        {
            juce::MessageManager::callAsync ([k = key] { windows().erase (k); });
            return;
        }
}
} // namespace

void setNativeEditorDemo (bool on)
{
    demoDisplays = on;
}

juce::Point<int> nativeEditorMinimumSize (juce::Component& editor)
{
    // as NativeEffectWindow's constrainer (the editor there is made at 1.25, its design size is the layout's)
    auto* native = dynamic_cast<NativeEffectEditor*> (&editor);
    if (native == nullptr) return {};
    const auto design = native->designSize();
    return { juce::jmax (design.x / 2, native->minimumWidth()), design.y / 2 };
}

bool tickNativeEffectEditor (juce::Component& editor)
{
    auto* native = dynamic_cast<NativeEffectEditor*> (&editor);
    if (native == nullptr) return false;
    native->tickDisplaysForSnapshot();
    return true;
}

std::unique_ptr<juce::Component> makeNativeEffectEditor (AudioEngine& engine, int track, int slot, float scale, int page)
{
    return std::make_unique<NativeEffectEditor> (engine, track, slot, scale, page);
}

juce::String stepperSelfTest (AudioEngine& engine, int track, int slot, int pages)
{
    juce::String report;
    for (int page = 0; page < juce::jmax (1, pages); ++page)
    {
        NativeEffectEditor editor (engine, track, slot, 1.0f, page);
        report << editor.stepperSelfTest();
    }
    return report;
}

std::unique_ptr<juce::Component> makeNodeEffectEditor (AudioEngine& engine, std::function<EffectSlot* (Song&)> locate,
                                                       std::function<std::shared_ptr<Effect>()> running, const juce::String& name, float scale)
{
    return std::make_unique<NativeEffectEditor> (engine, -1, -1, scale, 0, NativeEffectEditor::NodeTarget { std::move (locate), std::move (running), name });
}

namespace NativeEffectWindows
{
bool canOpen (EffectKind kind)
{
    return kind != EffectKind::plugin && kind != EffectKind::parallel;
}

bool canOpen (const EffectSlot& slot)
{
    return canOpen (slot.kind) || isSoundgoodizerApprox (slot);
}

void open (AudioEngine& engine, int track, int slot)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return;

    const auto& fxSlot = mixer[(std::size_t) track].effects[(std::size_t) slot];
    const auto kind = fxSlot.kind;
    if (kind == EffectKind::patcher) // its node graph editor (REA_rodada141)
    {
        PatcherWindows::openSlot (engine, track, slot);
        return;
    }
    if (VisualWindows::handles (kind)) // Wave Candy / Visualizer: displays fed by the slot's tap (round 164c)
    {
        VisualWindows::open (engine, track, slot);
        return;
    }
    if (FlExtraWindows::handles (kind)) // Big Clock, Tuner, HTML NoteBook (round 165g)
    {
        FlExtraWindows::open (engine, track, slot);
        return;
    }
    if (! canOpen (fxSlot))
        return;
    if (kind == EffectKind::formulaController) // the formula, its knobs and a monitor (REA round 133)
    {
        FormulaControllerWindows::open (engine, track, slot);
        return;
    }
    if (Controller164Windows::handles (kind)) // the X-Y / X-Y-Z pad, the Control Surface's panel (round 164)
    {
        Controller164Windows::open (engine, track, slot);
        return;
    }
    if (kind == EffectKind::edison) // the audio editor as the slot's plugin window (round 190cd)
    {
        openEdisonSlot (engine, track, slot);
        return;
    }
    if (NewtoneWindows::handles (kind)) // load a sample, edit its notes / warp markers, hear it (round 142)
    {
        NewtoneWindows::open (engine, track, slot);
        return;
    }
    // (the Parametric EQ opens here, in its layout editor: ParametricEqWindows::open forwards to this function, so
    // forwarding back to it was an endless loop — every way of opening the Parametric EQ froze LYNX; round 157a)
    if (kind == EffectKind::facetEq) // its own editor (analyser, nodes, dynamics; REA round 103)
    {
        FacetEqWindows::open (engine, track, slot);
        return;
    }
    // round 136: Ember in its Classic voicing opens the layout editor (the Classic controls); the Pro editor is Ember's own
    const bool emberClassic = kind == EffectKind::ember && fxSlot.params.size() > (std::size_t) mixsuite::ember::voicing
                              && fxSlot.params[(std::size_t) mixsuite::ember::voicing] > 0.5f;
    if (ProEditorWindows::handles (kind) && ! emberClassic) // display-first editors of the dynamics / saturation engines (REA round 114)
    {
        ProEditorWindows::open (engine, track, slot);
        return;
    }

    auto& open = windows();
    const auto key = std::make_pair (track, slot);

    if (auto it = open.find (key); it != open.end())
    {
        if (it->second->getContentComponent() != nullptr && static_cast<NativeEffectEditor*> (it->second->getContentComponent())->current() != nullptr)
        {
            it->second->toFront (true);
            editorlimit::touch (it->second.get());
            return;
        }
        open.erase (it); // the slot now holds another effect
    }

    const auto name = track == 0 ? tr ("Master") : juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str());
    const auto title = utf8 (isSoundgoodizerApprox (fxSlot) ? "Soundgoodizer" : effectInfo (kind).name) + " - " + (name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) })) + " ("
                       + tr ("slot {0}", { juce::String (slot + 1) }) + ")";
    open[key] = std::make_unique<NativeEffectWindow> (engine, track, slot, title);
    // FL's "Limit the number of open plugin editors" (round 189o, core/EditorLimit.h): LYNX's own effect windows count too
    auto* window = open[key].get();
    editorlimit::opened (window, [window] {
        editorlimit::forget (window);
        juce::MessageManager::callAsync ([window] {
            auto& all = windows();
            for (auto it = all.begin(); it != all.end(); ++it)
                if (it->second.get() == window)
                {
                    all.erase (it);
                    return;
                }
        });
    });
}

void switchEmberEditor (AudioEngine& engine, int track, int slot, bool classic)
{
    juce::MessageManager::callAsync ([&engine, track, slot, classic]
    {
        if (classic)
        {
            ProEditorWindows::close (track, slot);
            open (engine, track, slot);
        }
        else
        {
            windows().erase ({ track, slot });
            ProEditorWindows::open (engine, track, slot);
        }
    });
}

void closeAll()
{
    windows().clear();
    FormulaControllerWindows::closeAll();
    Controller164Windows::closeAll();
    NewtoneWindows::closeAll();
    VisualWindows::closeAll();
    FlExtraWindows::closeAll();
    FacetEqWindows::closeAll();
    ProEditorWindows::closeAll();
}
} // namespace NativeEffectWindows
} // namespace lynx
