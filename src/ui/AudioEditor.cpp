#include "ui/AudioEditor.h"
#include "ui/AudioEditorTools.h"
#include "ui/AudioToMidi.h"
#include "ui/BrowserComponent.h"
#include "ui/EditorTools.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/AudioCapture.h"
#include "core/AudioEditTools.h"
#include "core/AutoWarp.h"
#include "core/EdisonFx.h"
#include "core/EdisonScript.h"
#include "core/SessionKeys.h"
#include "core/Instruments.h"
#include "core/MixerOps.h"
#include "core/NoteTools.h"
#include "core/NewtoneFx.h"
#include "core/WaveAnalysis.h"
#include "core/ValueMath.h"
#include "core/WarpEdit.h"
#include "core/WarpTools.h"
#include "core/WarpView.h"
#include "core/ClipLauncher.h"
#include "engine/SampleLibrary.h"
#include "engine/Text.h"
#include "platform/Paths.h"
#include "ui/AudioClipboard.h"
#include "ui/FileChoosers.h"
#include "ui/HintBar.h"
#include "ui/ScriptPanel.h"
#include "ui/ValueControl.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
namespace ae = audioedit;

// one clipboard for every editor window (copy in one, paste in another); Tools › Clipboard saves it (round 181b)
ae::Audio& clipboard()
{
    return audioclipboard::editor();
}

// an audio file read on a worker thread (an audio script's Sample.LoadFromFile)
bool readAudioFile (const juce::File& file, ae::Audio& out)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (juce::int64) 1 << 31 || reader->numChannels == 0)
        return false;
    const auto channels = (int) juce::jmin (2u, reader->numChannels);
    juce::AudioBuffer<float> buffer (channels, (int) reader->lengthInSamples);
    if (! reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, channels > 1))
        return false;
    out = ae::silence (channels, reader->sampleRate, buffer.getNumSamples());
    for (int c = 0; c < channels; ++c)
        std::copy (buffer.getReadPointer (c), buffer.getReadPointer (c) + buffer.getNumSamples(), out.channels[(std::size_t) c].begin());
    return true;
}

// the clean clip look (docs/analise/UI_STUDY_playlist_clips.md): one light filled shape in a tint of the audio colour
juce::Colour waveColour() { return theme::colour::audioClip.interpolatedWith (juce::Colours::white, 0.7f); }

enum class TimeFormat
{
    automatic,
    samples,
    seconds,
    bars
};

enum class RecordMode
{
    now,     // starts at once
    onInput, // starts when the input goes above the threshold
    onPlay   // starts with the song playback, stops with it
};

constexpr float inputThreshold = 0.01f; // -40 dBFS

juce::String formatSeconds (double s)
{
    const auto negative = s < 0.0;
    s = std::abs (s);
    const auto minutes = (int) (s / 60.0);
    const auto rest = s - minutes * 60.0;
    return juce::String (negative ? "-" : "") + juce::String (minutes) + ":" + juce::String (rest, 3).paddedLeft ('0', 6);
}

// Spectrogram colours (LYNX's own map): the canvas, a deep red, the accent, amber, then white for the loudest.
juce::Colour spectrumColour (float db)
{
    const auto t = juce::jlimit (0.0f, 1.0f, (db + 96.0f) / 96.0f);
    static const juce::Colour stops[] = { theme::colour::bg0, juce::Colour (0xff3a1016), theme::colour::accent, theme::colour::warning,
                                          juce::Colour (0xfffff4dc) };
    static const float at[] = { 0.0f, 0.35f, 0.62f, 0.85f, 1.0f };
    for (int i = 1; i < 5; ++i)
        if (t <= at[i])
            return stops[i - 1].interpolatedWith (stops[i], (t - at[i - 1]) / (at[i] - at[i - 1]));
    return stops[4];
}

// --- a small parameter window for the tools (sliders + an optional choice; Preview / Accept / Cancel) ------------
class ToolPanel final : public juce::Component
{
public:
    struct Param
    {
        juce::String name;
        double min = 0.0, max = 1.0, value = 0.0, step = 0.0;
        juce::String suffix;
    };
    using Action = std::function<void (const std::vector<double>&, int)>;

    ToolPanel (std::vector<Param> ps, juce::StringArray choices, const juce::String& choiceName, int choice, Action applyAction, Action previewAction)
        : params (std::move (ps)), apply (std::move (applyAction)), preview (std::move (previewAction))
    {
        for (const auto& p : params)
        {
            auto* label = labels.add (new juce::Label ({}, p.name));
            label->setFont (theme::font (theme::type::body));
            label->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (label);

            auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
            s->setRange (p.min, p.max, p.step);
            s->setValue (p.value, juce::dontSendNotification);
            s->setDoubleClickReturnValue (true, p.value);
            s->setTextValueSuffix (p.suffix);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 22);
            addAndMakeVisible (s);
        }

        if (! choices.isEmpty())
        {
            choiceLabel.setText (choiceName, juce::dontSendNotification);
            choiceLabel.setFont (theme::font (theme::type::body));
            choiceLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (choiceLabel);
            choiceBox.addItemList (choices, 1);
            choiceBox.setSelectedItemIndex (choice, juce::dontSendNotification);
            addAndMakeVisible (choiceBox);
        }

        previewButton.setButtonText (tr ("Preview"));
        acceptButton.setButtonText (tr ("Accept"));
        cancelButton.setButtonText (tr ("Cancel"));
        acceptButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        previewButton.onClick = [this] {
            if (preview)
                preview (values(), choiceBox.getSelectedItemIndex());
        };
        acceptButton.onClick = [this] {
            if (apply)
                apply (values(), choiceBox.getSelectedItemIndex());
            closeToolWindowOf (*this);
        };
        cancelButton.onClick = [this] { closeToolWindowOf (*this); };
        previewButton.setVisible (preview != nullptr);
        for (auto* b : { &previewButton, &acceptButton, &cancelButton })
            addAndMakeVisible (*b);

        setSize (420, 24 + (int) params.size() * 36 + (choices.isEmpty() ? 0 : 36) + 52);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (theme::space::lg);
        if (choiceBox.getNumItems() > 0)
        {
            auto row = area.removeFromTop (28);
            choiceLabel.setBounds (row.removeFromLeft (120));
            choiceBox.setBounds (row);
            area.removeFromTop (8);
        }
        for (int i = 0; i < sliders.size(); ++i)
        {
            auto row = area.removeFromTop (28);
            labels[i]->setBounds (row.removeFromLeft (120));
            sliders[i]->setBounds (row);
            area.removeFromTop (8);
        }
        auto buttons = area.removeFromBottom (30);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        acceptButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        previewButton.setBounds (buttons.removeFromRight (90));
    }

private:
    std::vector<double> values() const
    {
        std::vector<double> v;
        for (auto* s : sliders)
            v.push_back (s->getValue());
        return v;
    }

    std::vector<Param> params;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> sliders;
    juce::Label choiceLabel;
    juce::ComboBox choiceBox;
    juce::TextButton previewButton, acceptButton, cancelButton;
    Action apply, preview;
};


// --- the editor --------------------------------------------------------------------------------------------------
struct Source
{
    enum class Kind
    {
        none,
        file,
        channel,
        insert,
        slot // an Edison effect slot (round 190cd): the editor is that plugin's window, its audio is the slot's
    };
    Kind kind = Kind::none;
    int index = -1; // channel or insert
    juce::String path;
    int clip = -1; // a channel opened from a Playlist audio clip: that clip
    std::uint64_t instance = 0; // slot: the slot's runtime identity (EffectSlot::instance), wherever it moves
    int gridTrack = -1, gridScene = -1; // round 190cy: opened from a clip of the grid (the Detail View's Clip page)
};

class AudioEditor final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public audiotools::Host,
                          public juce::DragAndDropTarget,
                          private juce::Timer
{
public:
    AudioEditor (AudioEngine& e, Source s) : engine (e), source (std::move (s))
    {
        auto setupIcon = [this] (IconButton& b, const juce::String& tip) {
            b.setFlat (true);
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        setupIcon (menuButton, tr ("Menu: file, edit, tools, regions, view"));
        setupIcon (undoButton, tr ("Undo") + " (Ctrl+Z)");
        setupIcon (redoButton, tr ("Redo") + " (Ctrl+Y)");
        setupIcon (historyButton, tr ("Undo history"));
        setupIcon (recordButton, tr ("Record") + " (R)");
        setupIcon (recordMenuButton, tr ("What and when to record"));
        setupIcon (playButton, tr ("Play / stop the selection") + " (Space)");
        setupIcon (stopButton, tr ("Stop"));
        setupIcon (snapButton, snapCaption (snapMode)); // FL: the hint names the snap mode (round 190ee)
        setupIcon (fitButton, tr ("Zoom out full"));
        setupIcon (saveButton, tr ("Save as WAV..."));
        setupIcon (dragButton, tr ("Drag / copy sample / selection: drag it to the Playlist or the Channel Rack; right-click copies it"));
        dragButton.onDragOut = [this] { dragOut(); };
        dragButton.onRightClick = [this] {
            if (doc.audio.empty())
                return;
            clipboard() = ae::copy (doc.audio, ae::orAll (doc, selection));
            setStatus (tr ("Sent to clipboard"), false);
        };
        // Follow (issue #12): the view pages along with the playback position; off, it stays where you look
        setupIcon (followButton, tr ("Follow: the view pages along with the playback position; off, it stays where you scrolled"));
        followButton.setClickingTogglesState (false);
        followButton.setOnColour (theme::colour::bg6);
        followButton.setIconColourWhenOn (theme::colour::accent);
        followButton.setToggleState (EditorGlobals::get().isFollowingView (EditorGlobals::FollowView::audioEditor), juce::dontSendNotification);
        followButton.onClick = [this] {
            auto& g = EditorGlobals::get();
            g.setFollowingView (EditorGlobals::FollowView::audioEditor, ! g.isFollowingView (EditorGlobals::FollowView::audioEditor));
            followButton.setToggleState (g.isFollowingView (EditorGlobals::FollowView::audioEditor), juce::dontSendNotification);
        };
        recordButton.setOnColour (theme::colour::record);
        recordButton.setClickingTogglesState (false);
        snapButton.setClickingTogglesState (true);

        menuButton.onClick = [this] { mainMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton)); };
        warpButton.setButtonText (tr ("Warp markers")); // ("Warp" alone is a synth control's name in the tables)
        warpButton.setTooltip (tr ("Warp markers: tie points of the audio to beats; the audio between them stretches (the channel's markers). Drag a marker: its audio goes to another beat; Shift + drag: the audio slides under it"));
        warpButton.setClickingTogglesState (true);
        warpButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        warpButton.onClick = [this] { setWarpMode (warpButton.getToggleState()); };
        autoWarpButton.setButtonText (tr ("Auto-warp"));
        autoWarpButton.setTooltip (tr ("Place warp markers from the tempo (the file name's, else its length) and, in a long recording, on every bar"));
        autoWarpButton.onClick = [this] { autoWarp(); };
        addChildComponent (warpButton);
        addChildComponent (autoWarpButton);
        // round 190cg: the tempo of the selected segment with Live's ÷2 and ×2 beside it [SHOT live-m3-01-clipview-warp:
        // "BPM 120.00 /2 x2" in the Audio panel]
        warpBpmBox.setEditable (true);
        warpBpmBox.setJustificationType (juce::Justification::centred);
        warpBpmBox.setFont (theme::font (theme::type::small));
        warpBpmBox.setColour (juce::Label::backgroundColourId, theme::colour::bg1);
        warpBpmBox.setColour (juce::Label::outlineColourId, theme::colour::border);
        warpBpmBox.setTooltip (tr ("Tempo of the selected segment (BPM): type a value, or use the buttons beside it"));
        warpBpmBox.onTextChange = [this] {
            const auto bpm = warpBpmBox.getText().replaceCharacter (',', '.').getDoubleValue();
            const auto segment = warpSegment();
            if (bpm >= 5.0 && bpm <= 2000.0 && segment >= 0)
                editWarp ([segment, bpm] (SamplerSettings& sampler, double) { warptools::setSegmentBpm (sampler, segment, bpm); });
            refreshWarpBpm();
        };
        warpHalveButton.setButtonText (juce::String::fromUTF8 ("\xc3\xb7" "2"));
        warpHalveButton.setTooltip (tr ("Half the tempo: the audio plays twice as fast"));
        warpHalveButton.onClick = [this] { scaleWarpTempo (0.5); };
        warpDoubleButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97" "2"));
        warpDoubleButton.setTooltip (tr ("Twice the tempo: the audio plays half as fast"));
        warpDoubleButton.onClick = [this] { scaleWarpTempo (2.0); };
        for (auto* c : std::initializer_list<juce::Component*> { &warpBpmBox, &warpHalveButton, &warpDoubleButton })
            addChildComponent (*c);
        warpButton.setVisible (source.kind == Source::Kind::channel);
        undoButton.onClick = [this] { undo(); };
        redoButton.onClick = [this] { redo(); };
        historyButton.onClick = [this] { historyMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&historyButton)); };
        recordButton.onClick = [this] { toggleRecording(); };
        recordMenuButton.onClick = [this] { recordMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton)); };
        playButton.onClick = [this] { playing ? stopPlayback() : startPlayback(); };
        stopButton.onClick = [this] {
            stopPlayback();
            if (recState != RecState::idle)
                stopRecording();
        };
        snapButton.onClick = [this] { snapOn = snapButton.getToggleState(); };
        fitButton.onClick = [this] { zoomToFit(); };
        saveButton.onClick = [this] { saveAs (bits); };

        auto setupText = [this] (juce::TextButton& b, const juce::String& text, const juce::String& tip) {
            b.setButtonText (text);
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        setupText (loopButton, tr ("Loop"), tr ("Loop the playback") + " (L)");
        loopButton.setClickingTogglesState (true);
        loopButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        loopButton.onClick = [this] {
            loopPlay = loopButton.getToggleState();
            if (slotMode())
                setSlotParam (edisonfx::loopPlay, loopPlay ? 1.0f : 0.0f);
            if (playing)
                startPlayback();
        };
        // the playback volume (round 162, issue #9): how loud the editor plays, without changing the audio (the
        // Amp... tool changes the audio itself); remembered with the settings
        volumeKnob.setRange (0.0, controls::maxUtilityVolume, 0.01);
        volumeKnob.setDoubleClickReturnValue (true, 1.0);
        volumeKnob.textFromValueFunction = [] (double v) { return ui::utilityVolumeText (v); };
        volumeKnob.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue() / 100.0; };
        volumeKnob.setMouseClickGrabsKeyboardFocus (false);
        volumeKnob.setWantsKeyboardFocus (false);
        volumeKnob.setValue (engine.getEditorVolume(), juce::dontSendNotification);
        volumeKnob.onValueChange = [this] {
            engine.setEditorVolume (volumeKnob.getValue());
            refreshVolumeKnob();
        };
        addAndMakeVisible (volumeKnob);
        refreshVolumeKnob();
        setupText (viewButton, tr ("Spectrum"), tr ("Show the waveform or the spectrogram") + " (S)");
        viewButton.setClickingTogglesState (true);
        viewButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        viewButton.onClick = [this] {
            spectrumView = viewButton.getToggleState();
            repaint();
        };
        setupText (envButton, tr ("Edit: Wave"), tr ("What the mouse edits: the waveform, or the volume / panning / stereo separation envelope drawn over it (E)"));
        envButton.onClick = [this] { envModeMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&envButton)); };
        setupText (sendButton, tr ("Send"), tr ("Send to the Playlist as an audio clip, to a new Sampler channel or back to the channel"));
        sendButton.setColour (juce::TextButton::buttonColourId, theme::colour::accentMuted);
        sendButton.onClick = [this] { sendMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sendButton)); };

        // quick tools (the second row)
        struct Quick
        {
            const char* text;
            const char* tip;
            std::function<void()> action;
        };
        const Quick quick[] = {
            { TR_NOOP ("Cut"), TR_NOOP ("Cut the selection (Ctrl+X)"), [this] { cutSelection(); } },
            { TR_NOOP ("Copy"), TR_NOOP ("Copy the selection (Ctrl+C)"), [this] { copySelection(); } },
            { TR_NOOP ("Paste"), TR_NOOP ("Paste at the cursor, or over the selection (Ctrl+V)"), [this] { paste (false); } },
            { TR_NOOP ("Delete"), TR_NOOP ("Delete the selection (Del)"), [this] { deleteSelection(); } },
            { TR_NOOP ("Trim"), TR_NOOP ("Keep only the selection"), [this] { trimSelection(); } },
            { TR_NOOP ("Silence"), TR_NOOP ("Silence the selection (Clear)"), [this] { edit (tr ("Silence"), [] (ae::Document& d, ae::Range r) { ae::clear (d, r); }, true); } },
            { TR_NOOP ("Fade in"), TR_NOOP ("Fade in over the selection (curve: the arrow next to Fade out)"), [this] { fade (true); } },
            { TR_NOOP ("Fade out"), TR_NOOP ("Fade out over the selection"), [this] { fade (false); } },
            { "~", TR_NOOP ("Fade curve"), [this] { curveMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (quickButtons[8])); } },
            { TR_NOOP ("Normalize"), TR_NOOP ("Normalize the selection (or everything) to 0 dB"), [this] { edit (tr ("Normalize"), [] (ae::Document& d, ae::Range r) { ae::normalize (d, r); }); } },
            { TR_NOOP ("Amp..."), TR_NOOP ("Volume and panning"), [this] { showAmp(); } },
            { TR_NOOP ("Reverse"), TR_NOOP ("Reverse the selection (or everything)"), [this] { edit (tr ("Reverse"), [] (ae::Document& d, ae::Range r) { ae::reverse (d, r); }); } },
            { TR_NOOP ("Invert"), TR_NOOP ("Reverse the polarity"), [this] { edit (tr ("Reverse polarity"), [] (ae::Document& d, ae::Range r) { ae::invert (d, r); }); } },
            { TR_NOOP ("Center"), TR_NOOP ("Remove the DC offset"), [this] { edit (tr ("Remove DC offset"), [] (ae::Document& d, ae::Range r) { ae::removeDc (d, r); }); } },
            { TR_NOOP ("Stretch..."), TR_NOOP ("Time stretch / pitch shift"), [this] { openEdisonTool (audiotools::Tool::stretch); } },
            { TR_NOOP ("Resample..."), TR_NOOP ("Change the sample rate"), [this] { showResample(); } },
            { TR_NOOP ("Marker"), TR_NOOP ("Add a marker at the cursor (M)"), [this] { addMarkerAtCursor(); } },
            { TR_NOOP ("Region"), TR_NOOP ("Make the selection a region"), [this] { addRegionFromSelection(); } },
            { TR_NOOP ("Set loop"), TR_NOOP ("Make the selection the loop"), [this] { setLoopFromSelection(); } },
        };
        for (const auto& q : quick)
        {
            auto* b = quickButtons.add (new juce::TextButton (q.text[0] == '~' ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")) : tr (q.text)));
            b->setTooltip (tr (q.tip));
            b->onClick = q.action;
            addAndMakeVisible (b);
        }

        setWantsKeyboardFocus (true);
        setSize (1120, slotMode() ? 630 : 600);
        history.reset (doc, tr ("Empty").toStdString());
        if (slotMode())
            setupSlotMode();
        updateButtons();
        startTimerHz (30);
    }

    ~AudioEditor() override
    {
        if (slotMode())
        {
            // the plugin goes on without its window: a recording continues (AudioEngine::pumpEdisonRecordings), the audio stays in the slot
            if (playing)
                if (const auto p = processor())
                    p->play (nullptr, false);
            if (session != nullptr && ! session->recording())
                engine.flushEdisonAudio();
            return;
        }
        if (playing)
            engine.previewSample (nullptr);
        if (recState != RecState::idle)
            engine.getMeters().capture.setSource (AudioCapture::off);
        releaseInput();
    }

    // --- an Edison slot (round 190cd) -------------------------------------------------------------------------
    bool slotMode() const noexcept { return source.kind == Source::Kind::slot; }

    // where the slot is now (it may have moved): { track, slot }, { -1, -1 } when it is gone
    std::pair<int, int> slotPlace() const
    {
        const auto& mixer = engine.getSong().mixer;
        for (std::size_t t = 0; t < mixer.size(); ++t)
            for (std::size_t i = 0; i < mixer[t].effects.size(); ++i)
                if (mixer[t].effects[i].kind == EffectKind::edison && mixer[t].effects[i].instance.id == source.instance)
                    return { (int) t, (int) i };
        return { -1, -1 };
    }

    const EffectSlot* slotDef() const
    {
        const auto [t, i] = slotPlace();
        return t < 0 ? nullptr : &engine.getSong().mixer[(std::size_t) t].effects[(std::size_t) i];
    }

    std::shared_ptr<edisonfx::Processor> processor() const
    {
        const auto [t, i] = slotPlace();
        return t < 0 ? nullptr : std::dynamic_pointer_cast<edisonfx::Processor> (engine.getEffect (t, i));
    }

    float slotParam (int param) const
    {
        const auto* fx = slotDef();
        return fx != nullptr ? fx->params[(std::size_t) param] : edisonfx::info().params[(std::size_t) param].defaultValue;
    }

    void setSlotParam (int param, float value)
    {
        const auto [t, i] = slotPlace();
        if (t < 0 || slotParam (param) == value)
            return;
        engine.editSong ([t = t, i = i, param, value] (Song& song) {
            if (t < (int) song.mixer.size() && i < (int) song.mixer[(std::size_t) t].effects.size())
                song.mixer[(std::size_t) t].effects[(std::size_t) i].params[(std::size_t) param] = value;
        });
        syncSlotControls();
    }

    static juce::String maxTimeName (int index)
    {
        const auto minutes = edisonfx::maxMinutes[juce::jlimit (0, edisonfx::maxTimeCount - 1, index)];
        return minutes == 0 ? tr ("Forever") : tr ("{0} min", { juce::String (minutes) });
    }

    static juce::String modeHelp (edisonfx::Mode m)
    {
        switch (m)
        {
            case edisonfx::Mode::now: return tr ("Recording starts at once");
            case edisonfx::Mode::onInput: return tr ("Recording starts when the input is heard");
            case edisonfx::Mode::input: return tr ("Records only while the input is heard (it pauses in silence)");
            case edisonfx::Mode::onPlay: return tr ("Records while the song plays");
        }
        return {};
    }

    void setupSlotMode()
    {
        session = edisonfx::findSession (source.instance);
        if (session == nullptr)
            if (const auto* fx = slotDef())
                session = edisonfx::sessionFor (*fx);
        recordMenuButton.setVisible (false);
        recordButton.setTooltip (tr ("Record this slot's input") + " (R)");

        // FL's four record modes, by FL's names (the plugin's mode list)
        static const char* const modeNames[] = { TR_NOOP ("Now"), TR_NOOP ("On input"), TR_NOOP ("Input"), TR_NOOP ("On play") };
        for (int m = 0; m < edisonfx::modeCount; ++m)
            recModeBox.addItem (tr (modeNames[m]), m + 1);
        recModeBox.setTooltip (tr ("When / what to record"));
        recModeBox.onChange = [this] {
            const auto m = (edisonfx::Mode) juce::jlimit (0, edisonfx::modeCount - 1, recModeBox.getSelectedItemIndex());
            setSlotParam (edisonfx::recordMode, edisonfx::modeValue (m));
            setStatus (modeHelp (m), false);
        };
        for (int i = 0; i < edisonfx::maxTimeCount; ++i)
            maxTimeBox.addItem (maxTimeName (i), i + 1);
        maxTimeBox.setTooltip (tr ("Maximum recording time"));
        maxTimeBox.onChange = [this] {
            setSlotParam (edisonfx::maxTime, (float) maxTimeBox.getSelectedItemIndex() / (float) (edisonfx::maxTimeCount - 1));
        };
        for (auto* box : { &recModeBox, &maxTimeBox })
        {
            box->setWantsKeyboardFocus (false);
            addAndMakeVisible (*box);
        }
        auto setupSwitch = [this] (juce::TextButton& b, const juce::String& text, const juce::String& tip, int param) {
            b.setButtonText (text);
            b.setTooltip (tip);
            b.setClickingTogglesState (true);
            b.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
            b.setWantsKeyboardFocus (false);
            b.onClick = [this, &b, param] { setSlotParam (param, b.getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible (b);
        };
        setupSwitch (appendButton, tr ("Append"), tr ("A new recording goes after the audio already here (off: it replaces it)"), edisonfx::append);
        setupSwitch (slaveButton, tr ("Slave"), tr ("Slave playback to host: while the song plays, this audio plays at the song time") + " (H)", edisonfx::slave);
        setupSwitch (muteInputButton, tr ("Mute input"), tr ("Mute input: the slot's input is not passed on") + " (I)", edisonfx::muteInput);
        syncSlotControls();

        if (session != nullptr)
        {
            adoptSession ({}, true);
            if (session->recording()) // (a recording started before this window was opened)
            {
                beforeRecording = doc;
                recState = session->phase() == edisonfx::Phase::recording ? RecState::recording : RecState::waiting;
                recordButton.setToggleState (true, juce::dontSendNotification);
                showSlotPhase();
            }
        }
    }

    // the switches show the slot's parameters (they also change by undo, or from another window)
    void syncSlotControls()
    {
        const auto* fx = slotDef();
        if (fx == nullptr)
            return;
        recModeBox.setSelectedItemIndex ((int) edisonfx::modeOf (fx->params), juce::dontSendNotification);
        maxTimeBox.setSelectedItemIndex (edisonfx::maxTimeIndexOf (fx->params), juce::dontSendNotification);
        appendButton.setToggleState (fx->params[edisonfx::append] >= 0.5f, juce::dontSendNotification);
        slaveButton.setToggleState (fx->params[edisonfx::slave] >= 0.5f, juce::dontSendNotification);
        muteInputButton.setToggleState (fx->params[edisonfx::muteInput] >= 0.5f, juce::dontSendNotification);
        loopPlay = fx->params[edisonfx::loopPlay] >= 0.5f;
        loopButton.setToggleState (loopPlay, juce::dontSendNotification);
        shownParams.assign (fx->params.begin(), fx->params.begin() + edisonfx::paramCount);
    }

    // the editor shows the session's document (`label` empty: as a newly opened one, else one more history step)
    void adoptSession (const juce::String& label, bool opening = false)
    {
        const juce::ScopedValueSetter<bool> guard (adopting, true);
        doc = session->document();
        seenRevision = session->revision();
        if (opening)
        {
            history.reset (doc, tr ("Open").toStdString());
            selection = {};
            cursor = 0;
            modified = false;
            documentChanged();
            zoomToFit();
        }
        else
        {
            history.commit (doc, label.toStdString());
            modified = true;
            documentChanged();
        }
    }

    void showSlotPhase()
    {
        if (recState == RecState::recording)
            setStatus (tr ("Recording..."), false);
        else if (recState == RecState::waiting)
            setStatus (edisonfx::modeOf (slotDef() != nullptr ? slotDef()->params : EffectParams {}) == edisonfx::Mode::onPlay
                           ? tr ("Waiting for project playback...")
                           : tr ("Waiting for audio input..."),
                       false);
    }

    void startSlotRecording()
    {
        stopPlayback();
        const auto p = processor();
        if (session == nullptr || p == nullptr)
        {
            setStatus (tr ("This slot is not processing audio (the effect was removed)"), true);
            return;
        }
        const auto appendTake = slotParam (edisonfx::append) >= 0.5f;
        if (! session->start (p, appendTake))
            return;
        beforeRecording = doc;
        if (! appendTake)
        {
            beforeRecording = {};
            beforeRecording.title = doc.title;
        }
        recState = edisonfx::modeOf (slotDef()->params) == edisonfx::Mode::now ? RecState::recording : RecState::waiting;
        recordButton.setToggleState (true, juce::dontSendNotification);
        showSlotPhase();
        repaint();
    }

    // the recording ended (Record, Stop, or the maximum recording time): the take is in the session's document
    void finishSlotRecording()
    {
        recState = RecState::idle;
        recordButton.setToggleState (false, juce::dontSendNotification);
        if (session->revision() != seenRevision)
        {
            adoptSession (tr ("Record"));
            const auto lost = session->droppedFrames();
            setStatus (tr ("Recorded {0}", { formatTime (selection.length()) }) + (lost > 0 ? "  (" + tr ("{0} frames lost", { juce::String (lost) }) + ")" : ""), lost > 0);
            engine.flushEdisonAudio(); // a take is in the project's recording folder as soon as it exists
        }
        else
        {
            const juce::ScopedValueSetter<bool> guard (adopting, true);
            doc = session->document();
            documentChanged();
            setStatus (tr ("Nothing was recorded"), false);
        }
    }

    // --- opening ---------------------------------------------------------------------------------------------
    bool loadFile (const juce::File& file)
    {
        auto data = engine.getSampleLibrary().load (file.getFullPathName());
        if (data == nullptr)
        {
            setStatus (tr ("Could not read {0}", { file.getFileName() }), true);
            return false;
        }

        ae::Document d;
        d.audio.sampleRate = data->sampleRate;
        d.audio.channels.emplace_back (data->leftData(), data->leftData() + data->frames());
        if (data->isStereo())
            d.audio.channels.emplace_back (data->rightData(), data->rightData() + data->frames());
        if (data->loopStart >= 0 && data->loopEnd > data->loopStart)
            ae::setLoop (d, { data->loopStart, data->loopEnd });

        if (file.hasFileExtension ("wav"))
        {
            juce::MemoryBlock bytes;
            if (file.loadFileAsData (bytes))
            {
                ae::Document marks = d;
                if (ae::decodeWavMarkers (static_cast<const std::uint8_t*> (bytes.getData()), bytes.getSize(), marks))
                {
                    d.markers = marks.markers;
                    if (marks.hasLoop())
                        ae::setLoop (d, { marks.loopStart, marks.loopEnd });
                }
            }
        }

        d.title = file.getFileNameWithoutExtension().toStdString();
        setDocument (std::move (d), tr ("Open"));
        lastFile = file;
        return true;
    }

    void setDocument (ae::Document d, const juce::String& label)
    {
        doc = std::move (d);
        history.reset (doc, label.toStdString());
        selection = {};
        cursor = 0;
        modified = false;
        documentChanged();
        zoomToFit();
        updateTitle();
    }

    void selectForSnapshot (ae::Range r, bool spectrum)
    {
        selection = r;
        cursor = r.start;
        spectrumView = spectrum;
        viewButton.setToggleState (spectrum, juce::dontSendNotification);
        repaint();
    }

    juce::PopupMenu mainMenu();

    // --- warp markers (round 175a, M3) ------------------------------------------------------------------------
    // The channel's markers sit in a lane above the waveform, each at the file time it ties to, labelled with its
    // beat (bar.beat). Drag one sideways to tie its beat to another point of the audio (it snaps to the attacks,
    // shown as small ticks; [Alt] no snap); double-click the lane to add a marker or to remove one; Del removes the
    // selected one. Edits go to the song at once (undoable there); opened from a Playlist clip whose channel other
    // clips share, the clip first gets its own copy of the channel, so the others keep their timing.
    const ChannelDef* warpChannel() const
    {
        const auto& song = engine.getSong();
        return source.kind == Source::Kind::channel && juce::isPositiveAndBelow (source.index, (int) song.channels.size())
                   ? &song.channels[(std::size_t) source.index]
                   : nullptr;
    }
    double warpFileSeconds() const
    {
        if (const auto* c = warpChannel())
            if (const auto data = engine.getSampleLibrary().findLoaded (juce::String::fromUTF8 (c->samplePath.c_str())); data != nullptr && data->sampleRate > 0.0)
                return (double) data->frames() / data->sampleRate;
        return doc.audio.sampleRate > 0.0 ? (double) doc.audio.frames() / doc.audio.sampleRate : 0.0;
    }
    std::vector<WarpPoint> warpMarkers() const
    {
        const auto* c = warpChannel();
        return c != nullptr && c->sampler.keepPitch && c->sampler.warp.size() >= 2 ? c->sampler.warp : std::vector<WarpPoint> {};
    }
    void setWarpModeForPanel (bool on) { setWarpMode (on); }
    // --warpshot (round 190cy): ÷2 / ×2 as the buttons do it; a marker held in the middle of a drag
    void scaleWarpTempoForSnapshot (double factor) { scaleWarpTempo (factor); }
    void holdWarpDragForSnapshot (int index, double beatOrSeconds, bool slides)
    {
        const auto markers = warpMarkers();
        if (index <= 0 || index >= (int) markers.size())
            return;
        if (drag != Drag::warp)
        {
            syncAxis();
            dragAxis = axis;
        }
        warpSelected = warpDrag = index;
        warpDragSlides = slides;
        warpDragBefore = markers;
        warpDragBeat = slides ? markers[(std::size_t) index].beats : beatOrSeconds;
        warpDragSeconds = slides ? beatOrSeconds : markers[(std::size_t) index].seconds;
        drag = Drag::warp;
        syncAxis();
        repaint();
    }
    // --warpshot (round 190db): -1 as the workflow, 0 file time, 1 beats
    void setBeatTimeForSnapshot (int choice) { setBeatTimeChoice (choice); }
    void setGridClip (int track, int scene)
    {
        source.gridTrack = track;
        source.gridScene = scene;
    }
    void setWarpMode (bool on)
    {
        warpMode = on && source.kind == Source::Kind::channel;
        warpButton.setToggleState (warpMode, juce::dontSendNotification);
        autoWarpButton.setVisible (warpMode);
        for (auto* c : std::initializer_list<juce::Component*> { &warpBpmBox, &warpHalveButton, &warpDoubleButton })
            c->setVisible (warpMode);
        warpSelected = warpDrag = warpHover = -1;
        resized();
        syncAxis();
        repaint();
    }
    // ÷2 / ×2 (round 190cy, as Live 12.4.16): the markers' beats and the clip the editor was opened from (a Playlist
    // clip or a clip of the grid) scale together, so the clip keeps the same audio in half / twice the beats
    void scaleWarpTempo (double factor)
    {
        editWarp ([factor] (SamplerSettings& sampler, double) { warptools::scaleTempo (sampler, factor); }, factor);
    }
    void editWarp (const std::function<void (SamplerSettings&, double)>& fn, double clipFactor = 0.0)
    {
        const auto seconds = warpFileSeconds();
        if (source.kind != Source::Kind::channel || ! (seconds > 0.0))
            return;
        auto channel = source.index;
        engine.editSong ([&, clip = source.clip, gridTrack = source.gridTrack, gridScene = source.gridScene] (Song& s) {
            if (clip >= 0)
                if (const auto own = warpedit::channelForClipEdit (s, clip); own >= 0)
                    channel = own;
            if (! juce::isPositiveAndBelow (channel, (int) s.channels.size()))
                return;
            auto& sampler = s.channels[(std::size_t) channel].sampler;
            warpedit::ensureMarkers (sampler, seconds, s.bpm);
            fn (sampler, seconds);
            warpedit::syncLength (sampler, seconds);
            if (clipFactor > 0.0)
            {
                if (juce::isPositiveAndBelow (clip, (int) s.playlist.size()) && s.playlist[(std::size_t) clip].kind == ClipKind::audio)
                    warptools::scaleClip (s.playlist[(std::size_t) clip], clipFactor);
                if (s.clipGrid)
                    if (auto* c = const_cast<Clip*> (launcher::clipAt (*s.clipGrid, gridTrack, gridScene)); c != nullptr && c->kind == ClipKind::audio && c->channel == channel)
                        warptools::scaleClip (*c, clipFactor);
            }
        });
        source.index = channel;
        syncAxis();
        repaint();
    }
    void findWarpAttacks()
    {
        if (warpAttacksOf == doc.audio.channels.data() && ! warpAttacks.empty())
            return;
        warpAttacks.clear();
        warpAttacksOf = doc.audio.channels.data();
        if (doc.audio.empty() || ! (doc.audio.sampleRate > 0.0))
            return;
        std::vector<float> mono (doc.audio.channels[0]);
        if (doc.audio.numChannels() > 1)
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] = 0.5f * (mono[i] + doc.audio.channels[1][i]);
        for (const auto a : stretchdetail::detectAttacks (mono.data(), (long) mono.size(), doc.audio.sampleRate, 6.0))
            warpAttacks.push_back ((double) a / doc.audio.sampleRate);
    }
    double secondsToX (double seconds) const { return frameToX (seconds * doc.audio.sampleRate); }

    // --- the beat-time view (round 190db; Live 12.4.16's Clip View draws a warped clip in beats, live check 190cw shot
    // warp190cg-09). A warped channel's audio with the warp lane shown is drawn through its markers: the ruler counts
    // bars.beats, each marker segment of the file is resampled to its beat span, and a marker drag redraws the two
    // segments around the marker as it moves. The view's units are beats scaled so the file spans [0, frames], so the
    // view position, the zoom and everything kept in file frames (selection, markers, loop, envelopes, cursor) work as in
    // file time; the selection stays in file time, so the edits act on the audio they show. Unwarped audio and the
    // Pattern workflow (unless chosen in the Warp markers menu) keep the file-time view.
    bool beatTimeChosen() const noexcept { return beatTimeChoice < 0 ? EditorGlobals::get().clipLaunchWorkflow : beatTimeChoice > 0; }
    double unitOfFrame (double frame) const noexcept { return axis.valid() ? axis.unitOfFrame (frame) : frame; }
    double frameOfUnit (double unit) const noexcept { return axis.valid() ? axis.frameOfUnit (unit) : unit; }
    bool warpDragHeld() const noexcept { return drag == Drag::warp && warpDrag > 0 && warpDrag < (int) warpDragBefore.size(); }
    // brings `axis` up to the channel's markers (or the held drag's preview); the view keeps its place. True on a change.
    bool syncAxis()
    {
        warpview::BeatAxis next;
        if (warpMode && beatTimeChosen() && ! doc.audio.empty() && doc.audio.sampleRate > 0.0)
        {
            if (warpDragHeld() && dragAxis.valid())
                next = warpview::withScaleOf (dragAxis, warpDragSlides ? warpview::slidePreview (warpDragBefore, warpDrag, warpDragSeconds)
                                                                       : warpview::stretchPreview (warpDragBefore, warpDrag, warpDragBeat));
            else if (const auto markers = warpMarkers(); markers.size() >= 2)
                next = warpview::makeAxis (markers, (double) doc.audio.frames(), doc.audio.sampleRate);
        }
        if (next.valid() == axis.valid() && (! next.valid() || (next.markers == axis.markers && next.beat0 == axis.beat0 && next.unitsPerBeat == axis.unitsPerBeat && next.frames == axis.frames)))
            return false;
        if (axis.valid() && next.valid())
            warpview::keepView (axis, next, viewStart, fpp);
        else if (const auto width = waveArea.getWidth(); width > 0)
        {
            // to or from file time: the same stretch of the file stays in view
            const auto left = frameOfUnit (viewStart), right = frameOfUnit (viewStart + (double) width * fpp);
            const auto l = next.valid() ? next.unitOfFrame (left) : left, r = next.valid() ? next.unitOfFrame (right) : right;
            viewStart = l;
            fpp = r > l ? (r - l) / (double) width : fpp;
        }
        axis = std::move (next);
        ++axisVersion;
        clampView();
        return true;
    }
    void setBeatTimeChoice (int choice)
    {
        beatTimeChoice = choice;
        syncAxis();
        repaint();
    }
    // what a held drag changes on screen: the ruler, the lane and the waveform between the marker's neighbours (they keep
    // their beats, so their place, during a drag in beats), and the overview
    void repaintWarpDrag()
    {
        if (! axis.valid() || ! warpDragHeld() || envMode != EnvMode::wave)
        {
            repaint();
            return;
        }
        const auto& w = warpDragBefore;
        const auto xOfBeat = [this] (double b) { return (float) waveArea.getX() + (float) ((axis.unitOfBeat (b) - viewStart) / fpp); };
        const auto left = juce::jmax ((float) waveArea.getX(), xOfBeat (w[(std::size_t) warpDrag - 1].beats) - 3.0f);
        const auto right = warpDrag + 1 < (int) w.size() ? juce::jmin ((float) waveArea.getRight(), xOfBeat (w[(std::size_t) warpDrag + 1].beats) + 60.0f)
                                                         : (float) waveArea.getRight();
        if (right > left)
            repaint (juce::Rectangle<float>::leftTopRightBottom (left, (float) rulerArea.getY(), right, (float) waveArea.getBottom()).getSmallestIntegerContainer());
        repaint (overviewArea);
    }
    int warpMarkerAtX (int x) const
    {
        const auto markers = warpMarkers();
        int best = -1;
        double bestDx = 5.0;
        for (std::size_t k = 0; k < markers.size(); ++k)
            if (const auto dx = std::abs (secondsToX (markers[k].seconds) - x); dx <= bestDx)
            {
                bestDx = dx;
                best = (int) k;
            }
        return best;
    }
    void paintWarpLane (juce::Graphics& g)
    {
        using namespace theme;
        g.setColour (colour::bg1);
        g.fillRect (warpLane);
        if (doc.audio.empty())
            return;
        findWarpAttacks();
        // the attacks: short ticks along the lane's bottom
        g.setColour (colour::textDisabled.withAlpha (0.45f));
        for (const auto a : warpAttacks)
            if (const auto x = secondsToX (a); x >= warpLane.getX() && x <= warpLane.getRight())
                g.drawVerticalLine ((int) std::round (x), (float) warpLane.getBottom() - 4.0f, (float) warpLane.getBottom());
        const auto markers = warpMarkers();
        refreshWarpBpm();
        // the pseudo warp marker (round 190cg): the attack under the mouse offers a marker to grab
        // [live-exe-strings.txt:1675853 sInfoTextPseudoWarpMarker]
        if (warpHover >= 0 && warpHover < (int) warpAttacks.size() && warpDrag < 0)
        {
            const auto x = (float) secondsToX (warpAttacks[(std::size_t) warpHover]);
            juce::Path ghost;
            ghost.addTriangle (x - 5.0f, (float) warpLane.getY() + 2.0f, x + 5.0f, (float) warpLane.getY() + 2.0f, x, (float) warpLane.getY() + 10.0f);
            g.setColour (colour::textSecondary.withAlpha (0.75f));
            g.fillPath (ghost);
            g.setColour (colour::textSecondary.withAlpha (0.35f));
            g.drawVerticalLine ((int) std::round (x), (float) warpLane.getY() + 10.0f, (float) waveArea.getBottom());
        }
        if (markers.empty())
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::small));
            g.drawText (tr ("No warp markers: double-click here to add one, or Auto-warp"), warpLane.reduced (6, 0), juce::Justification::centredLeft);
            return;
        }
        const auto perBar = juce::jmax (1, engine.getSong().beatsPerBar);
        g.setFont (font (type::small));
        // in beats the axis holds the markers as drawn (a held drag's preview included): each sits on its beat
        const auto inBeats = axis.valid() && axis.markers.size() == markers.size();
        for (std::size_t k = 0; k < markers.size(); ++k)
        {
            const auto stretching = (int) k == warpDrag && ! warpDragSlides && ! warpDragBefore.empty();
            const auto seconds = inBeats ? axis.markers[k].seconds
                                 : stretching ? warpedit::secondsAt (warpDragBefore, warpDragBeat) // (where its new beat is now)
                                 : (int) k == warpDrag ? warpDragSeconds
                                                       : markers[k].seconds;
            const auto x = (float) secondsToX (seconds);
            if (x < warpLane.getX() - 40.0f || x > warpLane.getRight() + 2.0f)
                continue;
            const auto selected = (int) k == warpSelected;
            const auto fill = k == 0 ? colour::textDisabled : (selected ? colour::accent : colour::accent.withMultipliedSaturation (0.55f).brighter (0.2f));
            juce::Path flag;
            flag.addTriangle (x - 5.0f, (float) warpLane.getY() + 2.0f, x + 5.0f, (float) warpLane.getY() + 2.0f, x, (float) warpLane.getY() + 10.0f);
            g.setColour (fill);
            g.fillPath (flag);
            g.setColour (fill.withAlpha (selected ? 0.8f : 0.4f));
            g.drawVerticalLine ((int) std::round (x), (float) warpLane.getY() + 10.0f, (float) waveArea.getBottom());
            const auto b = inBeats ? axis.markers[k].beats : stretching ? warpDragBeat : markers[k].beats;
            const auto bar = (int) std::floor (b / perBar) + 1;
            const auto beat = b - (bar - 1) * perBar + 1.0;
            const auto label = juce::String (bar) + "." + (std::abs (beat - std::round (beat)) < 0.005 ? juce::String ((int) std::round (beat)) : juce::String (beat, 2));
            g.setColour (colour::textSecondary);
            g.drawText (label, juce::Rectangle<float> (x + 6.0f, (float) warpLane.getY(), 48.0f, (float) warpLane.getHeight()), juce::Justification::centredLeft);
        }
    }
    void warpMouseDown (const juce::MouseEvent& e)
    {
        auto m = warpMarkerAtX (e.x);
        // a pseudo marker grabbed (round 190cg): it becomes a marker on that attack, selected and already held
        if (m < 0 && ! e.mods.isPopupMenu() && warpHover >= 0 && warpHover < (int) warpAttacks.size())
        {
            const auto seconds = warpAttacks[(std::size_t) warpHover];
            editWarp ([seconds] (SamplerSettings& sampler, double) { warpedit::addMarker (sampler, seconds); });
            m = warpMarkerAtX ((int) std::round (secondsToX (seconds)));
            warpHover = -1;
        }
        warpSelected = m;
        if (m > 0 && ! e.mods.isPopupMenu())
        {
            warpDrag = m;
            warpDragSeconds = warpMarkers()[(std::size_t) m].seconds;
            // round 190cy (Live 12.4.16): a plain drag moves the marker with its audio to another beat (the audio
            // stretches around it); Shift + drag slides the audio under the marker, which keeps its beat
            warpDragSlides = e.mods.isShiftDown();
            warpDragBefore = warpMarkers();
            warpDragBeat = warpDragBefore[(std::size_t) m].beats;
            // round 190db: in beats the drag keeps the axis' scale; the pointer's beat is read on it
            syncAxis();
            dragAxis = axis;
            slideDownBeat = axis.valid() ? axis.beatOfUnit (viewStart + (double) (e.x - waveArea.getX()) * fpp) : 0.0;
            drag = Drag::warp;
        }
        else if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (tr ("Remove this warp marker"), m > 0, false, [this, m] { removeWarpMarker (m); });
            menu.addItem (tr ("Auto-warp"), [this] { autoWarp(); });
            menu.addItem (tr ("Remove all warp markers"), warpMarkers().size() > 2, false, [this] {
                editWarp ([] (SamplerSettings& s, double) { warpedit::clearMarkers (s); });
            });
            menu.addItem (tr ("Show the audio in beats"), true, beatTimeChosen(), [this] { setBeatTimeChoice (beatTimeChosen() ? 0 : 1); });
            const auto markers = warpMarkers();
            addWarpCommands (menu, m >= 0 && m < (int) markers.size() ? markers[(std::size_t) m].seconds : (double) xToFrame (e.x) / doc.audio.sampleRate);
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        }
        repaint();
    }
    void warpMouseDrag (const juce::MouseEvent& e)
    {
        const auto markers = warpMarkers();
        if (warpDrag <= 0 || warpDrag >= (int) markers.size())
            return;
        if (axis.valid() && dragAxis.valid() && warpDrag < (int) warpDragBefore.size())
        {
            warpMouseDragInBeats (e);
            return;
        }
        auto seconds = (double) xToFrame (e.x) / doc.audio.sampleRate;
        if (! warpDragSlides)
        {
            // to the beat the pointer's place played on (onto a sixteenth within 6 px; Alt: free), between the neighbours
            auto beat = warptools::stretchDragBeat (warpDragBefore, seconds);
            if (const auto sixteenth = std::round (beat * 4.0) / 4.0;
                ! e.mods.isAltDown() && std::abs (secondsToX (warpedit::secondsAt (warpDragBefore, sixteenth)) - e.x) <= 6.0)
                beat = sixteenth;
            const auto& w = warpDragBefore;
            const auto lo = w[(std::size_t) warpDrag - 1].beats + warpedit::minBeatGap;
            const auto hi = warpDrag + 1 < (int) w.size() ? w[(std::size_t) warpDrag + 1].beats - warpedit::minBeatGap : 1.0e6;
            warpDragBeat = juce::jlimit (lo, juce::jmax (lo, hi), beat);
            repaint();
            return;
        }
        if (! e.mods.isAltDown()) // onto an attack within 6 px
            for (const auto a : warpAttacks)
                if (std::abs (secondsToX (a) - e.x) <= 6.0)
                {
                    seconds = a;
                    break;
                }
        const auto lo = markers[(std::size_t) warpDrag - 1].seconds + warpedit::minSecondGap;
        const auto hi = warpDrag + 1 < (int) markers.size() ? markers[(std::size_t) warpDrag + 1].seconds - warpedit::minSecondGap : warpFileSeconds();
        warpDragSeconds = juce::jlimit (lo, juce::jmax (lo, hi), seconds);
        repaint();
    }
    // round 190db: a drag in the beat-time view (Live 12.4.16, live check 190cw shot warp190cg-09). Plain: the marker
    // and its audio follow the pointer onto its beat (a sixteenth within 6 px; Alt: free), so the audio on both sides
    // stretches as it moves. Shift: the marker keeps its beat and the audio slides under it with the pointer (an attack
    // within 6 px of the marker lands on it; Alt: free). Only the two segments around the marker are drawn again.
    void warpMouseDragInBeats (const juce::MouseEvent& e)
    {
        const auto& w = warpDragBefore;
        const auto pointerBeat = dragAxis.beatOfUnit (viewStart + (double) (e.x - waveArea.getX()) * fpp);
        const auto pixels = [this] (double units) { return std::abs (units) / fpp; };
        if (! warpDragSlides)
        {
            auto beat = pointerBeat;
            if (const auto sixteenth = std::round (beat * 4.0) / 4.0; ! e.mods.isAltDown() && pixels (dragAxis.unitOfBeat (sixteenth) - dragAxis.unitOfBeat (beat)) <= 6.0)
                beat = sixteenth;
            warpDragBeat = warpview::stretchPreview (w, warpDrag, beat)[(std::size_t) warpDrag].beats;
        }
        else
        {
            auto seconds = warpview::slideDragSeconds (w, warpDrag, slideDownBeat, pointerBeat);
            if (! e.mods.isAltDown())
            {
                const auto rate = doc.audio.sampleRate;
                auto best = 6.0;
                for (const auto a : warpAttacks)
                    if (const auto d = pixels (dragAxis.unitOfFrame (a * rate) - dragAxis.unitOfFrame (seconds * rate)); d <= best)
                    {
                        best = d;
                        seconds = a;
                    }
                // (attacks are found over the editor's copy; the file's end is the limit)
            }
            warpDragSeconds = warpview::slidePreview (w, warpDrag, seconds)[(std::size_t) warpDrag].seconds;
        }
        syncAxis();
        repaintWarpDrag();
    }
    void warpMouseUp()
    {
        const auto index = warpDrag, at = warpDrag;
        const auto seconds = warpDragSeconds;
        warpDrag = -1;
        drag = Drag::none;
        const auto markers = warpMarkers();
        if (! warpDragSlides)
        {
            // the marker goes to its new beat with its audio: the audio on both sides stretches
            if (const auto beat = warpDragBeat; index > 0 && index < (int) markers.size() && std::abs (markers[(std::size_t) index].beats - beat) > 1.0e-6)
                editWarp ([at, beat] (SamplerSettings& s, double file) { warpedit::moveMarker (s, at, beat, file); });
        }
        else if (index > 0 && index < (int) markers.size() && std::abs (markers[(std::size_t) index].seconds - seconds) > 1.0e-6)
            editWarp ([at, seconds] (SamplerSettings& s, double) {
                // the marker keeps its beat and ties it to another point of the audio
                warptools::slideMarker (s, at, seconds);
            });
        warpDragBefore.clear();
        dragAxis = {};
        syncAxis();
        repaint();
    }
    void warpDoubleClick (const juce::MouseEvent& e)
    {
        if (const auto m = warpMarkerAtX (e.x); m >= 0)
        {
            removeWarpMarker (m);
            return;
        }
        auto seconds = (double) xToFrame (e.x) / doc.audio.sampleRate;
        if (! e.mods.isAltDown())
            for (const auto a : warpAttacks)
                if (std::abs (secondsToX (a) - e.x) <= 6.0)
                {
                    seconds = a;
                    break;
                }
        editWarp ([seconds] (SamplerSettings& s, double) { warpedit::addMarker (s, seconds); });
        warpSelected = warpMarkerAtX ((int) std::round (secondsToX (seconds)));
    }
    void removeWarpMarker (int index)
    {
        if (index <= 0)
            return;
        editWarp ([index] (SamplerSettings& s, double) { warpedit::removeMarker (s, index); });
        warpSelected = -1;
    }
    // --- warp commands (round 190cg, core/WarpTools.h): Live's sample editor commands [live-exe-strings.txt:1645479…1645494
    // sMenuWarpFromHere / …StartAtSongTempo / …Straight / …StraightWithSongTempo, sMenuSet111Here,
    // sMenuWarpSampleAsLoopInBars; 1676273 sMenuTextInsertWarpMarker; 1586628 sMenuQuantize, 1586633 sMenuQuantizeSettings]
    // the segment the tempo box shows: the selected marker's, else the first
    int warpSegment() const
    {
        const auto count = (int) warpMarkers().size();
        return count < 2 ? -1 : juce::jlimit (0, count - 2, warpSelected < 0 ? 0 : warpSelected);
    }
    void refreshWarpBpm()
    {
        if (! warpBpmBox.isVisible() || warpBpmBox.isBeingEdited())
            return;
        const auto segment = warpSegment();
        const auto text = segment < 0 ? juce::String ("-") : juce::String (warptools::segmentBpm (warpMarkers(), segment), 2);
        if (warpBpmBox.getText() != text)
            warpBpmBox.setText (text, juce::dontSendNotification);
        warpBpmBox.setEnabled (segment >= 0);
        warpHalveButton.setEnabled (segment >= 0);
        warpDoubleButton.setEnabled (segment >= 0);
    }
    // the attack a pseudo marker is offered on at `x`: within 5 px, and not where a marker already sits
    int warpAttackAtX (int x) const
    {
        if (warpMarkerAtX (x) >= 0)
            return -1;
        int best = -1;
        double bestDx = 5.0;
        for (std::size_t k = 0; k < warpAttacks.size(); ++k)
            if (const auto dx = std::abs (secondsToX (warpAttacks[k]) - x); dx <= bestDx)
            {
                bestDx = dx;
                best = (int) k;
            }
        return best;
    }
    // variant 0: auto-warp from `seconds`; 1: starting at the song's tempo; 2: straight (one tempo); 3: the song's tempo
    void warpFromHere (double seconds, int variant)
    {
        if (doc.audio.empty() || source.kind != Source::Kind::channel || ! (doc.audio.sampleRate > 0.0))
            return;
        const auto bpm = engine.getSong().bpm;
        if (variant == 3)
        {
            editWarp ([seconds, bpm] (SamplerSettings& sampler, double file) { warptools::warpTempoFromHere (sampler, seconds, file, bpm); });
            return;
        }
        const auto from = (std::size_t) juce::jlimit<std::int64_t> (0, doc.audio.frames(), (std::int64_t) std::llround (seconds * doc.audio.sampleRate));
        SampleData sample;
        sample.sampleRate = doc.audio.sampleRate;
        sample.left.assign (doc.audio.channels[0].begin() + (std::ptrdiff_t) from, doc.audio.channels[0].end());
        if (doc.audio.numChannels() > 1)
            sample.right.assign (doc.audio.channels[1].begin() + (std::ptrdiff_t) from, doc.audio.channels[1].end());
        autowarp::Options o;
        o.songBpm = bpm;
        o.beatsPerBar = juce::jmax (1, engine.getSong().beatsPerBar);
        if (variant == 1)
            o.knownBpm = bpm;
        if (variant == 2)
            o.trackBars = 1.0e12; // never bar by bar: one tempo
        const auto result = autowarp::analyse (sample, o);
        if (result.markers.size() < 2)
        {
            setStatus (tr ("Auto-warp found no beat to lock to"), true);
            return;
        }
        editWarp ([seconds, tail = result.markers] (SamplerSettings& sampler, double file) { warptools::warpTailFromHere (sampler, seconds, file, tail); });
        setStatus (tr ("Auto-warp: {0} markers at {1} BPM", { juce::String ((int) result.markers.size()), juce::String (result.bpm, 2) }), false);
    }
    // the audio at `seconds` becomes 1.1.1; the clip the editor was opened from starts there
    void setWarpOneOneOne (double seconds)
    {
        const auto file = warpFileSeconds();
        if (source.kind != Source::Kind::channel || ! (file > 0.0))
            return;
        auto channel = source.index;
        engine.editSong ([&, clip = source.clip] (Song& song) {
            if (clip >= 0)
                if (const auto own = warpedit::channelForClipEdit (song, clip); own >= 0)
                    channel = own;
            if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
                return;
            auto& sampler = song.channels[(std::size_t) channel].sampler;
            warpedit::ensureMarkers (sampler, file, song.bpm);
            warptools::setOneOneOne (sampler, seconds);
            warpedit::syncLength (sampler, file);
            if (juce::isPositiveAndBelow (clip, (int) song.playlist.size()))
            {
                auto& c = song.playlist[(std::size_t) clip];
                c.offset = juce::jmax<std::int64_t> (0, (std::int64_t) std::llround (warptools::playedBeatOfZero (sampler, file) * ppq * juce::jlimit (0.01, 100.0, c.stretch)));
                c.offsetFraction = 0.0f;
            }
        });
        source.index = channel;
        repaint();
    }
    void insertWarpMarkers()
    {
        if (doc.audio.empty() || selection.empty() || ! (doc.audio.sampleRate > 0.0))
            return;
        findWarpAttacks();
        const auto from = (double) selection.start / doc.audio.sampleRate, to = (double) selection.end / doc.audio.sampleRate;
        std::vector<double> at;
        for (const auto a : warpAttacks)
            if (a >= from && a <= to)
                at.push_back (a);
        if (at.empty())
            at = { from, to }; // no attack inside: the selection's edges
        editWarp ([at] (SamplerSettings& sampler, double) { warptools::insertMarkers (sampler, at); });
        if (! warpMode)
            setWarpMode (true);
    }
    void quantizeWarp()
    {
        if (doc.audio.empty() || source.kind != Source::Kind::channel || ! (doc.audio.sampleRate > 0.0))
            return;
        findWarpAttacks();
        const auto from = selection.empty() ? 0.0 : (double) selection.start / doc.audio.sampleRate;
        const auto to = selection.empty() ? (double) doc.audio.frames() / doc.audio.sampleRate : (double) selection.end / doc.audio.sampleRate;
        int placed = 0;
        editWarp ([&placed, from, to, points = warpAttacks, grid = warpGridBeats, amount = warpAmount] (SamplerSettings& sampler, double) {
            placed = warptools::quantize (sampler, points, from, to, grid, amount);
        });
        if (! warpMode)
            setWarpMode (true);
        setStatus (placed > 0 ? tr ("{0} warp markers placed", { juce::String (placed) }) : tr ("No attack to quantize here"), placed == 0);
    }
    void addWarpCommands (juce::PopupMenu& menu, double seconds)
    {
        const auto has = ! doc.audio.empty();
        const auto bpm = juce::String (engine.getSong().bpm, 2);
        const auto perBar = (double) juce::jmax (1, engine.getSong().beatsPerBar);
        const auto bars = warptools::loopBars (warpFileSeconds(), engine.getSong().bpm, perBar);
        menu.addSeparator();
        menu.addItem (tr ("Warp from here"), has, false, [this, seconds] { warpFromHere (seconds, 0); });
        menu.addItem (tr ("Warp from here (start at {0} BPM)", { bpm }), has, false, [this, seconds] { warpFromHere (seconds, 1); });
        menu.addItem (tr ("Warp from here (straight)"), has, false, [this, seconds] { warpFromHere (seconds, 2); });
        menu.addItem (tr ("Warp {0} BPM from here", { bpm }), has, false, [this, seconds] { warpFromHere (seconds, 3); });
        menu.addItem (tr ("Warp as {0}-bar loop", { juce::String ((int) bars) }), has, false, [this, bars, perBar] {
            editWarp ([beats = bars * perBar] (SamplerSettings& sampler, double file) { warptools::warpAsLoop (sampler, file, beats); });
        });
        menu.addItem (tr ("Set 1.1.1 here"), has, false, [this, seconds] { setWarpOneOneOne (seconds); });
        menu.addSeparator();
        menu.addItem (tr ("Insert warp markers") + sessionkeys::menuKey (sessionkeys::Command::audioInsertWarpMarkers), has && ! selection.empty(), false,
                      [this] { insertWarpMarkers(); });
        menu.addItem (tr ("Quantize audio") + sessionkeys::menuKey (sessionkeys::Command::audioQuantize), has, false, [this] { quantizeWarp(); });
        juce::PopupMenu settings;
        const std::pair<const char*, double> grids[] = { { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 } };
        for (const auto& [name, beats] : grids)
            settings.addItem (name, true, std::abs (warpGridBeats - beats) < 1.0e-9, [this, beats = beats] { warpGridBeats = beats; });
        settings.addSeparator();
        for (const auto percent : { 100, 75, 50, 25 })
            settings.addItem (juce::String (percent) + " %", true, std::abs (warpAmount * 100.0 - percent) < 0.5, [this, percent] { warpAmount = percent / 100.0; });
        menu.addSubMenu (tr ("Quantize settings"), settings, has);
    }
    void autoWarp()
    {
        if (doc.audio.empty() || source.kind != Source::Kind::channel)
            return;
        SampleData sample;
        sample.sampleRate = doc.audio.sampleRate;
        sample.left = doc.audio.channels[0];
        if (doc.audio.numChannels() > 1)
            sample.right = doc.audio.channels[1];
        autowarp::Options o;
        o.songBpm = engine.getSong().bpm;
        o.beatsPerBar = juce::jmax (1, engine.getSong().beatsPerBar);
        o.knownBpm = autowarp::bpmInName (juce::File (source.path).getFileNameWithoutExtension().toStdString());
        const auto result = autowarp::analyse (sample, o);
        if (result.markers.size() < 2)
        {
            setStatus (tr ("Auto-warp found no beat to lock to"), true);
            return;
        }
        editWarp ([markers = result.markers] (SamplerSettings& s, double) {
            s.warp = markers;
            s.keepPitch = true;
        });
        if (! warpMode)
            setWarpMode (true);
        setStatus (tr ("Auto-warp: {0} markers at {1} BPM", { juce::String ((int) result.markers.size()), juce::String (result.bpm, 2) }), false);
    }
    void setEnvModeForSnapshot (int mode) { setEnvMode ((EnvMode) juce::jlimit (0, 3, mode)); }
    // round 190ee: a marker's menu (with its Trigger note submenu) and FL's snap mode, for the off-screen checks
    juce::PopupMenu markerMenuForSnapshot (int index) { return juce::isPositiveAndBelow (index, (int) doc.markers.size()) ? markerMenu (index) : juce::PopupMenu(); }
    void setSnapForSnapshot (int mode) { setSnapMode (mode); }
    juce::String snapTooltipForSnapshot() const { return snapButton.getTooltip() + (snapButton.getToggleState() ? " [on]" : " [off]"); }
    std::int64_t snapFrameForSnapshot (std::int64_t f) const { return snapFrame (f); }

    // --- layout and painting ---------------------------------------------------------------------------------
    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (36).reduced (6, 4);
        auto place = [&bar] (juce::Component& c, int w, int gap = 2) {
            c.setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (gap);
        };
        place (menuButton, 28, 8);
        place (undoButton, 28);
        place (redoButton, 28);
        place (historyButton, 18, 12);
        place (recordButton, 30);
        place (recordMenuButton, 18, 6);
        place (playButton, 30);
        place (stopButton, 30, 6);
        place (loopButton, 54, 4);
        place (volumeKnob, 26, 12);
        place (viewButton, 76, 6);
        place (envButton, 120, 6);
        place (snapButton, 28);
        place (fitButton, 28);
        place (followButton, 28, 8);
        if (warpButton.isVisible())
            place (warpButton, 100);
        if (autoWarpButton.isVisible())
            place (autoWarpButton, 78);
        if (warpBpmBox.isVisible())
        {
            place (warpBpmBox, 58);
            place (warpHalveButton, 26);
            place (warpDoubleButton, 26);
        }
        sendButton.setBounds (bar.removeFromRight (70));
        bar.removeFromRight (6);
        dragButton.setBounds (bar.removeFromRight (28));
        bar.removeFromRight (2);
        saveButton.setBounds (bar.removeFromRight (28));

        if (slotMode()) // the plugin's own switches, under the transport (FL: next to its Record button)
        {
            auto row = area.removeFromTop (30).reduced (6, 3);
            row.removeFromLeft (112); // (they start under the Record button)
            auto put = [&row] (juce::Component& c, int w, int gap = 4) {
                c.setBounds (row.removeFromLeft (w));
                row.removeFromLeft (gap);
            };
            put (recModeBox, 104);
            put (maxTimeBox, 96);
            put (appendButton, 72, 14);
            put (slaveButton, 64);
            put (muteInputButton, 96);
        }

        // each button gets the width its text needs, plus an equal share of what is left (round 165o: translated
        // labels such as "Normalisieren" or "Ausschneiden" are never cut). When one row cannot hold them all (German,
        // Russian, Polish… at the default width) the buttons go on two rows instead; English keeps its single row.
        const auto n = quickButtons.size();
        const auto curveWidth = 18;
        const juce::Font font (theme::font (theme::type::body));
        std::vector<float> natural;
        float total = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            natural.push_back (i == 8 ? (float) curveWidth : juce::GlyphArrangement::getStringWidth (font, quickButtons[i]->getButtonText()) + 18.0f);
            total += natural.back() + (i == 7 ? 0.0f : 2.0f);
        }
        const auto rowWidth = (float) (area.getWidth() - 12);
        const auto twoRows = total - 14.0f * (float) (n - 1) > rowWidth && n > 9; // not even with 4 px padding
        auto tools = area.removeFromTop (twoRows ? 56 : 30).reduced (6, 2);
        // lays out buttons [first, last) in one row: natural widths plus an equal share of the free space; still too
        // narrow -> the padding goes first (down to 4 px a side), then the text squeezes (drawFittedText)
        auto layoutRow = [&] (juce::Rectangle<int> row, int first, int last)
        {
            float need = 0.0f;
            int stretchable = 0;
            for (int i = first; i < last; ++i)
            {
                need += natural[(std::size_t) i] + (i == 7 || i == last - 1 ? 0.0f : 2.0f);
                stretchable += i == 8 ? 0 : 1;
            }
            const auto avail = (float) row.getWidth();
            const auto extra = juce::jmax (0.0f, avail - need) / (float) juce::jmax (1, stretchable);
            auto shrink = 0.0f, scale = 1.0f;
            if (need > avail)
            {
                const auto padding = 14.0f * (float) stretchable;
                shrink = 14.0f * juce::jlimit (0.0f, 1.0f, (need - avail) / padding);
                const auto after = need - shrink * (float) stretchable;
                scale = after > avail ? avail / after : 1.0f;
            }
            float x = (float) row.getX();
            for (int i = first; i < last; ++i)
            {
                const auto w = i == 8 ? (float) curveWidth : (natural[(std::size_t) i] - shrink) * scale + extra;
                quickButtons[i]->setBounds (juce::roundToInt (x), row.getY(), juce::roundToInt (x + w) - juce::roundToInt (x), row.getHeight());
                x += w + (i == 7 ? 0.0f : 2.0f);
            }
        };
        if (! twoRows)
            layoutRow (tools, 0, n);
        else
        {
            // split near half the total width, never between Fade out (7) and its curve menu (8)
            int split = 1;
            float run = 0.0f;
            while (split < n - 1 && run + natural[(std::size_t) split - 1] < total * 0.5f)
                run += natural[(std::size_t) split++ - 1] + 2.0f;
            if (split == 8)
                ++split;
            const auto top = tools.removeFromTop (tools.getHeight() / 2);
            layoutRow (top.withTrimmedBottom (1), 0, split);
            layoutRow (tools.withTrimmedTop (1), split, n);
        }

        infoArea = area.removeFromBottom (24);
        overviewArea = area.removeFromBottom (18).reduced (6, 2);
        area.removeFromBottom (2);
        area = area.reduced (6, 0);
        levelArea = area.removeFromLeft (34);
        rulerArea = area.removeFromTop (24);
        rulerArea.setX (area.getX());
        warpLane = warpMode ? area.removeFromTop (18) : juce::Rectangle<int>();
        waveArea = area;
        clampView();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        if (syncAxis()) // (the markers changed elsewhere: the Playlist, undo; the whole view follows)
            repaint();
        g.fillAll (colour::bg2);
        g.setColour (colour::bg3);
        g.fillRect (getLocalBounds().removeFromTop (36));
        g.setColour (colour::border);
        g.drawHorizontalLine (36, 0.0f, (float) getWidth());

        paintRuler (g);
        paintLevels (g);
        if (spectrumView)
            paintSpectrum (g);
        else
            paintWave (g);
        paintOverlays (g);
        if (warpMode)
            paintWarpLane (g);
        paintOverview (g);
        paintInfo (g);
    }

    // --- mouse ----------------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        drag = Drag::none;
        syncAxis();
        if (doc.audio.empty())
        {
            if (e.mods.isPopupMenu())
                mainMenu().showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            return;
        }

        const auto pos = e.getPosition();

        if (warpMode && warpLane.contains (pos))
        {
            warpMouseDown (e);
            return;
        }

        if (overviewArea.contains (pos))
        {
            drag = Drag::overview;
            centreViewOnOverview (e.x);
            return;
        }

        if (rulerArea.contains (pos))
        {
            // the loop handles live in the ruler's bottom strip
            if (doc.hasLoop() && pos.y >= rulerArea.getBottom() - 7)
            {
                if (std::abs (frameToX ((double) doc.loopStart) - (float) e.x) < 6.0f)
                {
                    beginPointDrag (Drag::loopStart);
                    return;
                }
                if (std::abs (frameToX ((double) doc.loopEnd) - (float) e.x) < 6.0f)
                {
                    beginPointDrag (Drag::loopEnd);
                    return;
                }
            }
            if (const auto m = markerAt (e.x); m >= 0)
            {
                if (e.mods.isPopupMenu())
                    markerMenu (m).showMenuAsync (juce::PopupMenu::Options().withMousePosition());
                else
                {
                    dragMarkerIndex = m;
                    beginPointDrag (Drag::marker);
                }
                return;
            }
        }

        if (envMode != EnvMode::wave && waveArea.contains (pos) && envelopeMouseDown (e))
            return;

        if (e.mods.isPopupMenu())
        {
            editMenu (true).showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            return;
        }

        if (waveArea.contains (pos) || rulerArea.contains (pos))
        {
            const auto f = snapFrame (xToFrame (e.x));
            if (e.mods.isShiftDown() && ! selection.empty())
                anchor = std::abs (f - selection.start) < std::abs (f - selection.end) ? selection.end : selection.start;
            else if (e.mods.isShiftDown())
                anchor = cursor;
            else
                anchor = f;
            selection = { std::min (anchor, f), std::max (anchor, f) };
            cursor = f;
            drag = Drag::select;
            if (playing && ! e.mods.isShiftDown())
                startPlayback();
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::overview)
        {
            centreViewOnOverview (e.x);
            return;
        }
        if (drag == Drag::envPoint || drag == Drag::envBend)
        {
            envelopeMouseDrag (e);
            return;
        }
        if (drag == Drag::warp)
        {
            warpMouseDrag (e);
            return;
        }
        if (drag == Drag::none)
            return;

        // auto-scroll past the edges
        if (e.x < waveArea.getX())
            viewStart -= (waveArea.getX() - e.x) * fpp * 0.25;
        else if (e.x > waveArea.getRight())
            viewStart += (e.x - waveArea.getRight()) * fpp * 0.25;
        clampView();

        const auto f = snapFrame (xToFrame (e.x), drag == Drag::marker ? dragMarkerIndex : -1);
        if (drag == Drag::select)
        {
            selection = { std::min (anchor, f), std::max (anchor, f) };
            cursor = f;
        }
        else if (drag == Drag::marker && juce::isPositiveAndBelow (dragMarkerIndex, (int) doc.markers.size()))
        {
            auto& m = doc.markers[(std::size_t) dragMarkerIndex];
            const auto length = m.end - m.start;
            m.start = juce::jlimit<std::int64_t> (0, doc.audio.frames() - length, f);
            m.end = m.start + length;
        }
        else if (drag == Drag::loopStart)
            doc.loopStart = juce::jlimit<std::int64_t> (0, doc.loopEnd - 1, f);
        else if (drag == Drag::loopEnd)
            doc.loopEnd = juce::jlimit<std::int64_t> (doc.loopStart + 1, doc.audio.frames(), f);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag == Drag::warp)
        {
            warpMouseUp();
            return;
        }
        if (drag == Drag::envPoint || drag == Drag::envBend)
        {
            drag = Drag::none;
            envDragPoint = -1;
            if (envChanged)
                commit (tr ("Edit envelope"));
            envChanged = false;
            return;
        }
        if (drag == Drag::marker || drag == Drag::loopStart || drag == Drag::loopEnd)
        {
            const auto& before = history.current();
            const auto moved = before.loopStart != doc.loopStart || before.loopEnd != doc.loopEnd || before.markers.size() != doc.markers.size()
                               || ! std::equal (before.markers.begin(), before.markers.end(), doc.markers.begin(),
                                                [] (const ae::Marker& a, const ae::Marker& b) { return a.start == b.start && a.end == b.end; });
            if (moved)
            {
                ae::sortMarkers (doc);
                commit (drag == Drag::marker ? tr ("Move marker") : tr ("Move loop"));
            }
        }
        drag = Drag::none;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hover = warpMode && ! doc.audio.empty() && warpLane.contains (e.getPosition()) ? warpAttackAtX (e.x) : -1;
        if (std::exchange (warpHover, hover) != hover)
            repaint (warpLane.getUnion (waveArea));
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        if (std::exchange (warpHover, -1) != -1)
            repaint (warpLane.getUnion (waveArea));
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        syncAxis();
        if (warpMode && ! doc.audio.empty() && warpLane.contains (e.getPosition()))
        {
            warpDoubleClick (e);
            return;
        }
        if (doc.audio.empty() || ! waveArea.contains (e.getPosition()))
            return;
        // the region under the mouse, else the stretch between the markers around it, else everything
        const auto f = xToFrame (e.x);
        std::int64_t from = 0, to = doc.audio.frames();
        for (const auto& m : doc.markers)
            if (m.isRegion() && f >= m.start && f < m.end)
            {
                selection = { m.start, m.end };
                repaint();
                return;
            }
        for (const auto& m : doc.markers)
        {
            if (m.start <= f)
                from = std::max (from, m.start);
            if (m.start > f)
                to = std::min (to, m.start);
        }
        selection = { from, to };
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (doc.audio.empty())
            return;
        const auto delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            zoomAround ((float) e.x, delta > 0 ? 0.8 : 1.25);
        else
        {
            viewStart -= delta * (double) waveArea.getWidth() * fpp * 0.5;
            clampView();
            repaint();
        }
    }

    // --- keys -----------------------------------------------------------------------------------------------
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto ctrl = k.getModifiers().isCtrlDown() || k.getModifiers().isCommandDown();
        const auto shift = k.getModifiers().isShiftDown();
        const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        const auto code = k.getKeyCode();

        if (warpMode && warpSelected > 0 && (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey))
        {
            removeWarpMarker (warpSelected);
            return true;
        }
        // round 190cg: Live's Insert Warp Markers and Quantize on a channel's audio
        // (round 190cm: default keys only in the Clip-launch layout, core/SessionKeys.h; elsewhere the chords are not
        // taken, so Ctrl+I stays "Recording waits for input" and the commands are in the warp menu)
        if (ctrl && ! shift && source.kind == Source::Kind::channel && (code == 'I' || c == 'i') && ! selection.empty()
            && sessionkeys::defaultKeyActs (sessionkeys::Command::audioInsertWarpMarkers))
        {
            insertWarpMarkers();
            return true;
        }
        if (ctrl && ! shift && source.kind == Source::Kind::channel && warpMode && (code == 'U' || c == 'u')
            && sessionkeys::defaultKeyActs (sessionkeys::Command::audioQuantize))
        {
            quantizeWarp();
            return true;
        }
        if (ctrl && (code == 'Z' || c == 'z'))
            shift ? redo() : undo();
        else if (ctrl && (code == 'Y' || c == 'y'))
            redo();
        else if (ctrl && (code == 'X' || c == 'x'))
            cutSelection();
        else if (ctrl && (code == 'C' || c == 'c'))
            copySelection();
        else if (ctrl && (code == 'V' || c == 'v'))
            paste (false);
        else if (ctrl && (code == 'A' || c == 'a'))
        {
            selection = { 0, doc.audio.frames() };
            repaint();
        }
        else if (ctrl && (code == 'S' || c == 's'))
            saveAs (bits);
        else if (code == juce::KeyPress::spaceKey)
            playing ? stopPlayback() : startPlayback();
        else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
            deleteSelection();
        else if (code == juce::KeyPress::escapeKey)
        {
            if (recState != RecState::idle)
                stopRecording();
            selection = {};
            repaint();
        }
        else if (code == juce::KeyPress::homeKey)
            setCursor (0);
        else if (code == juce::KeyPress::endKey)
            setCursor (doc.audio.frames());
        else if (! ctrl && c == 'r')
            toggleRecording();
        else if (! ctrl && c == 'l')
            loopButton.triggerClick();
        else if (! ctrl && c == 's')
            viewButton.triggerClick();
        else if (! ctrl && c == 'm')
            addMarkerAtCursor();
        else if (! ctrl && c == 'e')
            setEnvMode ((EnvMode) (((int) envMode + 1) % 4));
        else if (! ctrl && c == 'f')
            zoomToFit();
        else if (slotMode() && ! ctrl && c == 'h')
            slaveButton.triggerClick();
        else if (slotMode() && ! ctrl && c == 'i')
            muteInputButton.triggerClick();
        else if (slotMode() && ! ctrl && c == 'a')
            followButton.triggerClick();
        else if (! ctrl && c >= '1' && c <= '4') // FL's Snap menu keys (round 190ee)
            setSnapMode ((int) (c - '1'));
        else if (c == '+' || c == '=')
            zoomAround ((float) waveArea.getCentreX(), 0.5);
        else if (c == '-')
            zoomAround ((float) waveArea.getCentreX(), 2.0);
        else
            return false;
        return true;
    }

    // --- drag and drop of files -----------------------------------------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && SampleLibrary::isSampleFile (juce::File (files[0]));
    }

    // a sample dragged from LYNX's Browser too (issue #12); not the editor's own Drag button
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return details.sourceComponent.get() != &dragButton && BrowserComponent::isBrowserDrag (details)
               && SampleLibrary::isSampleFile (BrowserComponent::draggedFile (details));
    }

    // round 190ee: the selection (or the whole sample) written to LYNX/Edits with its markers and loop, then dragged
    // as the Browser drags a file, so the Playlist and the Channel Rack take it as they take a dropped sample
    juce::File writePartFile (ae::Range range)
    {
        range = ae::orAll (doc, range);
        ae::Document part;
        part.audio = ae::copy (doc.audio, range);
        part.title = doc.title;
        for (auto m : doc.markers)
            if (m.start >= range.start && m.end <= range.end)
            {
                m.start -= range.start;
                m.end -= range.start;
                part.markers.push_back (m);
            }
        if (doc.hasLoop() && doc.loopStart >= range.start && doc.loopEnd <= range.end)
            ae::setLoop (part, { doc.loopStart - range.start, doc.loopEnd - range.start });
        const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Edits");
        folder.createDirectory();
        const auto whole = range.start == 0 && range.end == doc.audio.frames();
        const auto file = folder.getNonexistentChildFile (safeTitle() + (whole ? " (edit)" : " (part)"), ".wav");
        const auto bytes = ae::encodeWav (part, bits);
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            setStatus (tr ("Could not write {0}", { file.getFullPathName() }), true);
            return {};
        }
        engine.getSampleLibrary().forget (file.getFullPathName());
        return file;
    }

    static juce::DragAndDropContainer* containerIn (juce::Component& c, int depth)
    {
        if (auto* d = dynamic_cast<juce::DragAndDropContainer*> (&c))
            return d;
        if (depth > 0)
            for (auto* child : c.getChildren())
                if (auto* d = containerIn (*child, depth - 1))
                    return d;
        return nullptr;
    }

    void dragOut()
    {
        if (doc.audio.empty())
            return;
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
        for (int i = 0; container == nullptr && i < juce::Desktop::getInstance().getNumComponents(); ++i) // (a window of its own)
            if (auto* top = juce::Desktop::getInstance().getComponent (i))
                container = containerIn (*top, 3);
        if (container == nullptr)
            return;
        const auto file = writePartFile (selection);
        if (file == juce::File {})
            return;
        container->startDragging (juce::String (BrowserComponent::dragDescription) + "|" + file.getFullPathName(), &dragButton, juce::ScaledImage(), true);
    }

    void itemDropped (const SourceDetails& details) override
    {
        const auto file = BrowserComponent::draggedFile (details);
        if (file.existsAsFile())
            filesDropped ({ file.getFullPathName() }, 0, 0);
    }

    // FL's Edison: a dropped file replaces what the editor shows (issue #12); with Ctrl held it is pasted at the
    // cursor instead (LYNX's earlier behaviour)
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.isEmpty() || recState != RecState::idle)
            return;
        if (doc.audio.empty() || ! juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown())
        {
            stopPlayback();
            const auto previous = source;
            if (! slotMode()) // (an Edison slot stays that slot: the file becomes its audio)
                source = { Source::Kind::file, -1, files[0] };
            if (! loadFile (juce::File (files[0])))
                source = previous;
            return;
        }
        // into a document: pasted at the cursor
        auto data = engine.getSampleLibrary().load (files[0]);
        if (data == nullptr)
            return;
        ae::Audio clip;
        clip.sampleRate = data->sampleRate;
        clip.channels.emplace_back (data->leftData(), data->leftData() + data->frames());
        if (data->isStereo())
            clip.channels.emplace_back (data->rightData(), data->rightData() + data->frames());
        const auto at = cursor;
        ae::insert (doc, at, clip);
        selection = { at, at + ae::conform (clip, doc.audio.numChannels(), doc.audio.sampleRate).frames() };
        commit (tr ("Paste"));
    }

    // menus shared with the snapshot check
    juce::PopupMenu editMenu (bool contextual);

private:
    enum class Drag
    {
        none,
        warp,
        select,
        marker,
        loopStart,
        loopEnd,
        overview,
        envPoint,
        envBend
    };
    enum class RecState
    {
        idle,
        waiting,
        recording
    };

    // --- coordinates ----------------------------------------------------------------------------------------
    // (round 190db: through the warp markers in the beat-time view; the same as before in file time)
    float frameToX (double frame) const noexcept { return (float) waveArea.getX() + (float) ((unitOfFrame (frame) - viewStart) / fpp); }
    std::int64_t xToFrame (int x) const noexcept
    {
        return juce::jlimit<std::int64_t> (0, doc.audio.frames(), (std::int64_t) std::llround (frameOfUnit (viewStart + (double) (x - waveArea.getX()) * fpp)));
    }
    double maxFpp() const noexcept { return juce::jmax (1.0, (double) doc.audio.frames() / juce::jmax (1, waveArea.getWidth())); }

    void clampView()
    {
        fpp = juce::jlimit (1.0 / 32.0, maxFpp(), fpp);
        const auto visible = (double) waveArea.getWidth() * fpp;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, (double) doc.audio.frames() - visible), viewStart);
    }

    void zoomToFit()
    {
        fpp = maxFpp();
        viewStart = 0.0;
        clampView();
        repaint();
    }

    void zoomAround (float x, double factor)
    {
        const auto frame = viewStart + (x - (float) waveArea.getX()) * fpp;
        fpp *= factor;
        clampView();
        viewStart = frame - (x - (float) waveArea.getX()) * fpp;
        clampView();
        repaint();
    }

    void zoomTo (ae::Range r)
    {
        if (r.empty())
            return;
        const auto from = unitOfFrame ((double) r.start), to = unitOfFrame ((double) r.end);
        fpp = (to - from) / juce::jmax (1, waveArea.getWidth());
        clampView();
        viewStart = from;
        clampView();
        repaint();
    }

    void centreViewOnOverview (int x)
    {
        const auto frames = (double) doc.audio.frames();
        const auto f = (double) (x - overviewArea.getX()) / juce::jmax (1, overviewArea.getWidth()) * frames;
        viewStart = f - (double) waveArea.getWidth() * fpp * 0.5;
        clampView();
        repaint();
    }

    // FL's Edison snap (round 190ee; Edison_x64.dll snap function 0x855d20, Snap menu Tags 0…4): the toolbar's Snap
    // button switches snapping on and off, the Snap menu picks what it snaps to. LYNX's grid is the one its ruler
    // draws at the current zoom (FL's own grid-step law is not decoded); "Snap to pitch period" is not built.
    static juce::String snapCaption (int mode)
    {
        static const char* const names[] { TR_NOOP ("Snap to grid"), TR_NOOP ("Snap to regions"), TR_NOOP ("Snap to samples"), TR_NOOP ("Snap to zero-crossing") };
        return tr (names[juce::jlimit (0, 3, mode)]);
    }

    void setSnapMode (int mode)
    {
        snapMode = juce::jlimit (0, 3, mode);
        snapButton.setTooltip (snapCaption (snapMode));
        snapButton.setToggleState (true, juce::sendNotification); // (FL: picking a mode turns snapping on)
    }

    // the grid the ruler shows: beats in the beat-time view, the time ruler's labelled step otherwise
    std::int64_t snapToGrid (std::int64_t f) const
    {
        if (doc.audio.empty())
            return f;
        if (axis.valid())
        {
            const auto perBar = juce::jmax (1, engine.getSong().beatsPerBar);
            const auto pixelsPerBeat = axis.unitsPerBeat / fpp;
            const auto step = warpview::rulerStep (pixelsPerBeat, perBar);
            const auto tick = step <= 1.0 ? 0.25 : (step <= perBar ? 1.0 : (double) perBar);
            const auto grid = tick * pixelsPerBeat >= 6.0 ? tick : step;
            const auto beat = std::round (axis.beatOfUnit (unitOfFrame ((double) f)) / grid) * grid;
            return juce::jlimit<std::int64_t> (0, doc.audio.frames(), (std::int64_t) std::llround (frameOfUnit (axis.unitOfBeat (beat))));
        }
        const auto rate = doc.audio.sampleRate;
        const auto step = timeRulerStep() * rate;
        if (! (step > 0.0))
            return f;
        return juce::jlimit<std::int64_t> (0, doc.audio.frames(), (std::int64_t) std::llround (std::round ((double) f / step) * step));
    }

    // FL: the nearest region edge within 12 pixels, other than the region being moved
    std::int64_t snapToRegions (std::int64_t f, int skip) const
    {
        auto best = f;
        auto bestPx = 12.0f;
        for (int i = 0; i < (int) doc.markers.size(); ++i)
            if (i != skip)
                for (const auto edge : { doc.markers[(std::size_t) i].start, doc.markers[(std::size_t) i].end })
                    if (const auto px = std::abs (frameToX ((double) edge) - frameToX ((double) f)); px <= bestPx)
                        best = edge, bestPx = px;
        return best;
    }

    std::int64_t snapFrame (std::int64_t f, int skipMarker = -1) const
    {
        if (! snapOn)
            return f;
        switch (snapMode)
        {
            case 0: return snapToGrid (f);
            case 1: return snapToRegions (f, skipMarker);
            case 2: return f; // (positions are whole samples already)
            default: return ae::nearestZeroCrossing (doc.audio, f, (std::int64_t) juce::jmax (8.0, fpp * 6.0));
        }
    }

    int markerAt (int x) const
    {
        for (int i = viewRegions ? (int) doc.markers.size() - 1 : -1; i >= 0; --i)
            if (std::abs (frameToX ((double) doc.markers[(std::size_t) i].start) - (float) x) < 5.0f)
                return i;
        return -1;
    }

    // markers and loop ends move live; mouseUp commits the move against the history's current state
    void beginPointDrag (Drag d) { drag = d; }

    void setCursor (std::int64_t f)
    {
        cursor = juce::jlimit<std::int64_t> (0, doc.audio.frames(), f);
        selection = {};
        if (frameToX ((double) cursor) < (float) waveArea.getX() || frameToX ((double) cursor) > (float) waveArea.getRight())
        {
            viewStart = unitOfFrame ((double) cursor) - (double) waveArea.getWidth() * fpp * 0.5;
            clampView();
        }
        repaint();
    }

    // --- painting -------------------------------------------------------------------------------------------
    // per pixel column x of a lane: the min/max of the samples it covers (exact when zoomed in, from the
    // summary when zoomed out)
    bool columnRange (int channel, double from, double to, float& lo, float& hi) const { return framesRange (channel, from, to, lo, hi, fpp > 256.0); }
    // the min / max of frames [from, to): from the summary when asked to (wide stretches), else exact
    bool framesRange (int channel, double from, double to, float& lo, float& hi, bool fromSummary) const
    {
        const auto frames = doc.audio.frames();
        if (to <= 0.0 || from >= (double) frames)
            return false;
        if (fromSummary && summary != nullptr)
        {
            const auto p = summary->query (from, to, doc.audio.numChannels() > 1 ? channel : -1);
            if (! p.valid)
                return false;
            lo = p.lo;
            hi = p.hi;
            return true;
        }
        const auto& data = doc.audio.channels[(std::size_t) channel];
        auto a = (std::int64_t) std::floor (from), b = (std::int64_t) std::ceil (to);
        a = juce::jlimit<std::int64_t> (0, frames - 1, a);
        b = juce::jlimit<std::int64_t> (a + 1, frames, b);
        lo = hi = data[(std::size_t) a];
        for (auto i = a + 1; i < b; ++i)
        {
            lo = std::min (lo, data[(std::size_t) i]);
            hi = std::max (hi, data[(std::size_t) i]);
        }
        return true;
    }

    juce::Rectangle<float> laneBounds (int lane) const
    {
        const auto lanes = juce::jmax (1, doc.audio.numChannels());
        const auto h = (float) waveArea.getHeight() / (float) lanes;
        return { (float) waveArea.getX(), (float) waveArea.getY() + h * (float) lane, (float) waveArea.getWidth(), h };
    }

    void paintWave (juce::Graphics& g)
    {
        using namespace theme;
        g.setColour (colour::bg0);
        g.fillRect (waveArea);

        if (doc.audio.empty())
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::body));
            g.drawFittedText (recState != RecState::idle ? tr ("Waiting for input...")
                                                         : tr ("Empty: press Record, drop an audio file here or open one from the menu"),
                              waveArea.reduced (20), juce::Justification::centred, 2);
            return;
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (waveArea);
        paintSelectionAndRegions (g);
        if (axis.valid())
            paintBeatGrid (g);

        const auto lanes = doc.audio.numChannels();
        for (int lane = 0; lane < lanes; ++lane)
        {
            const auto r = laneBounds (lane);
            const auto centre = r.getCentreY();
            const auto half = r.getHeight() * 0.5f - 2.0f;

            // level guides: centre line and -6 dB
            g.setColour (colour::gridBeat);
            g.drawHorizontalLine ((int) (centre - half * 0.5f), r.getX(), r.getRight());
            g.drawHorizontalLine ((int) (centre + half * 0.5f), r.getX(), r.getRight());
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) centre, r.getX(), r.getRight());
            if (const auto mark = shownNoiseThreshold(); viewNoiseMark && mark > 0.0f && mark < 1.0f)
            {
                // the noise threshold (what "Acquire noise threshold" measured; in a slot, what the input must exceed)
                g.setColour (colour::warning.withAlpha (0.55f));
                g.drawHorizontalLine ((int) (centre - half * mark), r.getX(), r.getRight());
                g.drawHorizontalLine ((int) (centre + half * mark), r.getX(), r.getRight());
            }
            if (lane > 0)
            {
                g.setColour (colour::border);
                g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
            }

            if (axis.valid())
            {
                paintLaneInBeats (g, lane, centre, half);
                continue;
            }
            const auto width = waveArea.getWidth();
            if (fpp < 1.0)
            {
                // zoomed in past one sample per pixel: the samples joined by a line, dots when far apart
                const auto& data = doc.audio.channels[(std::size_t) lane];
                const auto first = juce::jmax<std::int64_t> (0, (std::int64_t) std::floor (viewStart) - 1);
                const auto last = juce::jmin<std::int64_t> (doc.audio.frames() - 1, (std::int64_t) std::ceil (viewStart + width * fpp) + 1);
                juce::Path line;
                for (auto i = first; i <= last; ++i)
                {
                    const auto p = juce::Point<float> (frameToX ((double) i), centre - juce::jlimit (-1.0f, 1.0f, data[(std::size_t) i]) * half);
                    i == first ? line.startNewSubPath (p) : line.lineTo (p);
                }
                g.setColour (waveColour());
                g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                if (fpp < 1.0 / 6.0)
                    for (auto i = first; i <= last; ++i)
                        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ frameToX ((double) i), centre - juce::jlimit (-1.0f, 1.0f, data[(std::size_t) i]) * half }));
                continue;
            }

            // one filled shape: the top edge (maxima) left to right, the bottom edge (minima) back
            std::vector<float> tops ((std::size_t) width), bottoms ((std::size_t) width);
            std::vector<char> valid ((std::size_t) width);
            for (int x = 0; x < width; ++x)
            {
                const auto from = viewStart + x * fpp;
                float lo = 0.0f, hi = 0.0f;
                valid[(std::size_t) x] = columnRange (lane, from, from + fpp, lo, hi) ? 1 : 0;
                tops[(std::size_t) x] = centre - juce::jlimit (-1.0f, 1.0f, hi) * half;
                bottoms[(std::size_t) x] = centre - juce::jlimit (-1.0f, 1.0f, lo) * half;
                if (bottoms[(std::size_t) x] - tops[(std::size_t) x] < 1.0f) // silence still shows as a hairline
                {
                    const auto mid = (bottoms[(std::size_t) x] + tops[(std::size_t) x]) * 0.5f;
                    tops[(std::size_t) x] = mid - 0.5f;
                    bottoms[(std::size_t) x] = mid + 0.5f;
                }
            }
            fillWave (g, valid, tops, bottoms);
        }
    }

    // one filled shape per lane: the top edge (maxima) left to right, the bottom edge (minima) back
    void fillWave (juce::Graphics& g, const std::vector<char>& valid, const std::vector<float>& tops, const std::vector<float>& bottoms)
    {
        const auto width = (int) valid.size();
        juce::Path wave;
        for (int x = 0; x < width;)
        {
            while (x < width && ! valid[(std::size_t) x])
                ++x;
            if (x >= width)
                break;
            const auto start = x;
            while (x < width && valid[(std::size_t) x])
                ++x;
            const auto px = [this] (int i) { return (float) (waveArea.getX() + i); };
            wave.startNewSubPath (px (start), tops[(std::size_t) start]);
            for (int i = start + 1; i < x; ++i)
                wave.lineTo (px (i), tops[(std::size_t) i]);
            wave.lineTo (px (x), tops[(std::size_t) (x - 1)]);
            wave.lineTo (px (x), bottoms[(std::size_t) (x - 1)]);
            for (int i = x - 1; i >= start; --i)
                wave.lineTo (px (i), bottoms[(std::size_t) i]);
            wave.closeSubPath();
        }
        g.setColour (waveColour());
        g.fillPath (wave);
    }

    // round 190db: a lane of the beat-time view. Each pixel column shows the stretch of the file its beats play
    // (warpview::columnPeaks keeps the columns a drag does not touch); zoomed in past a sample per pixel, the samples
    // joined by a line at their beats.
    void paintLaneInBeats (juce::Graphics& g, int lane, float centre, float half)
    {
        const auto width = waveArea.getWidth();
        const auto& data = doc.audio.channels[(std::size_t) lane];
        const auto firstFrame = axis.frameOfUnit (viewStart), lastFrame = axis.frameOfUnit (viewStart + (double) width * fpp);
        if (lastFrame - firstFrame < (double) width)
        {
            const auto first = juce::jmax<std::int64_t> (0, (std::int64_t) std::floor (firstFrame) - 1);
            const auto last = juce::jmin<std::int64_t> (doc.audio.frames() - 1, (std::int64_t) std::ceil (lastFrame) + 1);
            juce::Path line;
            for (auto i = first; i <= last; ++i)
            {
                const auto p = juce::Point<float> (frameToX ((double) i), centre - juce::jlimit (-1.0f, 1.0f, data[(std::size_t) i]) * half);
                i == first ? line.startNewSubPath (p) : line.lineTo (p);
            }
            g.setColour (waveColour());
            g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        if ((int) waveColumns.size() != doc.audio.numChannels())
            waveColumns.assign ((std::size_t) doc.audio.numChannels(), {});
        auto& columns = waveColumns[(std::size_t) lane];
        warpview::columnPeaks (axis, viewStart, fpp, width, (std::uint64_t) docVersion, columns, [this, lane] (double from, double to, float& lo, float& hi) {
            return framesRange (lane, from, to, lo, hi, to - from > 256.0);
        });
        std::vector<float> tops ((std::size_t) width), bottoms ((std::size_t) width);
        for (std::size_t x = 0; x < (std::size_t) width; ++x)
        {
            tops[x] = centre - juce::jlimit (-1.0f, 1.0f, columns.hi[x]) * half;
            bottoms[x] = centre - juce::jlimit (-1.0f, 1.0f, columns.lo[x]) * half;
            if (bottoms[x] - tops[x] < 1.0f)
            {
                const auto mid = (bottoms[x] + tops[x]) * 0.5f;
                tops[x] = mid - 0.5f;
                bottoms[x] = mid + 0.5f;
            }
        }
        fillWave (g, columns.valid, tops, bottoms);
    }

    // round 190db: the beat lines behind the waveform in the beat-time view (bars stronger), at the ruler's step
    void paintBeatGrid (juce::Graphics& g)
    {
        using namespace theme;
        const auto perBar = juce::jmax (1, engine.getSong().beatsPerBar);
        const auto step = warpview::rulerStep (axis.unitsPerBeat / fpp, perBar);
        const auto firstBeat = axis.beatOfUnit (viewStart), lastBeat = axis.beatOfUnit (viewStart + (double) waveArea.getWidth() * fpp);
        for (auto b = std::floor (firstBeat / step) * step; b <= lastBeat; b += step)
        {
            const auto x = (float) waveArea.getX() + (float) ((axis.unitOfBeat (b) - viewStart) / fpp);
            const auto onBar = std::abs (b / perBar - std::round (b / perBar)) < 1.0e-6;
            g.setColour (onBar ? colour::gridBar : colour::gridBeat);
            g.drawVerticalLine ((int) x, (float) waveArea.getY(), (float) waveArea.getBottom());
        }
    }

    void paintSelectionAndRegions (juce::Graphics& g)
    {
        using namespace theme;
        for (const auto& m : doc.markers)
            if (viewRegions && m.isRegion())
            {
                g.setColour (colour::warning.withAlpha (0.07f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (frameToX ((double) m.start), (float) waveArea.getY(), frameToX ((double) m.end),
                                                                        (float) waveArea.getBottom()));
            }
        if (! selection.empty())
        {
            g.setColour (colour::selectionFill);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (frameToX ((double) selection.start), (float) waveArea.getY(),
                                                                    frameToX ((double) selection.end), (float) waveArea.getBottom()));
        }
    }

    void paintSpectrum (juce::Graphics& g)
    {
        using namespace theme;
        g.setColour (colour::bg0);
        g.fillRect (waveArea);
        if (doc.audio.empty())
        {
            paintWave (g);
            return;
        }

        const auto columns = juce::jmax (1, waveArea.getWidth() / 2);
        const auto height = juce::jmax (1, waveArea.getHeight());
        // the file range in view (round 190db: in beats, the range the view's beats play; the columns are then
        // redistributed along the beats, so a marker drag only redistributes them)
        const auto from = (std::int64_t) frameOfUnit (viewStart);
        const auto to = juce::jmin (doc.audio.frames(), (std::int64_t) std::ceil (frameOfUnit (viewStart + waveArea.getWidth() * fpp)));
        const auto key = juce::String (docVersion) + "|" + juce::String (from) + "|" + juce::String (to) + "|" + juce::String (columns) + "x"
                         + juce::String (height) + "|" + juce::String (fftSize) + (logFrequency ? "l" : "n");
        if (key != spectrumKey)
        {
            const auto spec = ae::spectrogram (doc.audio, { from, to }, columns, fftSize);
            spectrumImage = juce::Image (juce::Image::RGB, columns, height, false);
            const auto nyquist = doc.audio.sampleRate * 0.5;
            const auto binHz = doc.audio.sampleRate / fftSize;
            for (int y = 0; y < height; ++y)
            {
                const auto t = 1.0 - (double) y / juce::jmax (1, height - 1);
                const auto hz = logFrequency ? 20.0 * std::pow (nyquist / 20.0, t) : t * nyquist;
                const auto bin = juce::jlimit (0, spec.bins - 1, (int) std::lround (hz / binHz));
                // the bins between this row and the next (log scale, high frequencies) are summarised by their max
                const auto nextT = 1.0 - (double) (y + 1) / juce::jmax (1, height - 1);
                const auto nextHz = logFrequency ? 20.0 * std::pow (nyquist / 20.0, nextT) : nextT * nyquist;
                const auto binLow = juce::jlimit (0, spec.bins - 1, (int) std::lround (nextHz / binHz));
                for (int c = 0; c < columns; ++c)
                {
                    auto db = spec.at (c, bin);
                    for (int b = binLow; b < bin; ++b)
                        db = std::max (db, spec.at (c, b));
                    spectrumImage.setPixelAt (c, y, spectrumColour (db));
                }
            }
            spectrumKey = key;
        }
        if (axis.valid() && to > from)
        {
            const auto warpedKey = key + "|" + juce::String ((juce::int64) axisVersion) + "|" + juce::String (viewStart) + "|" + juce::String (fpp);
            if (warpedKey != spectrumWarpedKey)
            {
                spectrumWarped = juce::Image (juce::Image::RGB, columns, height, false);
                const juce::Image::BitmapData source (spectrumImage, juce::Image::BitmapData::readOnly);
                juce::Image::BitmapData out (spectrumWarped, juce::Image::BitmapData::writeOnly);
                const auto unitsPerColumn = (double) waveArea.getWidth() * fpp / columns;
                for (int c = 0; c < columns; ++c)
                {
                    const auto f = axis.frameOfUnit (viewStart + ((double) c + 0.5) * unitsPerColumn);
                    const auto sc = juce::jlimit (0, columns - 1, (int) ((f - (double) from) / (double) (to - from) * columns));
                    for (int y = 0; y < height; ++y)
                        out.setPixelColour (c, y, source.getPixelColour (sc, y));
                }
                spectrumWarpedKey = warpedKey;
            }
            g.drawImage (spectrumWarped, waveArea.toFloat());
        }
        else
            g.drawImage (spectrumImage, waveArea.toFloat());

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (waveArea);
        if (! selection.empty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (frameToX ((double) selection.start), (float) waveArea.getY(),
                                                                    frameToX ((double) selection.end), (float) waveArea.getBottom()));
        }
    }

    void paintLevels (juce::Graphics& g)
    {
        using namespace theme;
        g.setColour (colour::bg2);
        g.fillRect (levelArea);
        g.setFont (font (type::caption));
        g.setColour (colour::textSecondary);
        if (doc.audio.empty())
            return;
        if (spectrumView)
        {
            const auto nyquist = doc.audio.sampleRate * 0.5;
            for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            {
                if (hz >= nyquist)
                    continue;
                const auto t = logFrequency ? std::log (hz / 20.0) / std::log (nyquist / 20.0) : hz / nyquist;
                const auto y = (float) waveArea.getBottom() - (float) t * (float) waveArea.getHeight();
                g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String ((int) hz), juce::Rectangle<float> (0.0f, y - 6.0f, 30.0f, 12.0f).withX ((float) levelArea.getX()),
                            juce::Justification::centredRight);
            }
            return;
        }
        for (int lane = 0; lane < doc.audio.numChannels(); ++lane)
        {
            const auto r = laneBounds (lane);
            const auto half = r.getHeight() * 0.5f - 2.0f;
            auto label = [&] (float level, const juce::String& text) {
                const auto y = r.getCentreY() - level * half;
                g.drawText (text, juce::Rectangle<float> ((float) levelArea.getX(), y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
            };
            if (r.getHeight() > 70.0f)
            {
                label (0.5f, "-6");
                label (-0.5f, "-6");
            }
            label (0.0f, doc.audio.numChannels() > 1 ? (lane == 0 ? "L" : "R") : "0");
        }
    }

    void paintRuler (juce::Graphics& g)
    {
        using namespace theme;
        g.setColour (colour::bg3);
        g.fillRect (rulerArea);
        if (doc.audio.empty())
            return;

        if (axis.valid())
            paintBeatRuler (g);
        else
            paintTimeRuler (g);

        // regions and markers: flags with their names; the loop: a bar along the bottom
        paintRulerMarks (g);
    }

    // round 190db: the beat-time view's ruler, labelled as Live's Clip View labels it ("1", "1.2", "1.2.3"), with
    // sixteenth ticks when there is room
    void paintBeatRuler (juce::Graphics& g)
    {
        using namespace theme;
        const auto perBar = juce::jmax (1, engine.getSong().beatsPerBar);
        const auto pixelsPerBeat = axis.unitsPerBeat / fpp;
        const auto step = warpview::rulerStep (pixelsPerBeat, perBar);
        const auto tick = step <= 1.0 ? 0.25 : (step <= perBar ? 1.0 : (double) perBar);
        const auto firstBeat = axis.beatOfUnit (viewStart), lastBeat = axis.beatOfUnit (viewStart + (double) waveArea.getWidth() * fpp);
        const auto xOf = [this] (double b) { return (float) waveArea.getX() + (float) ((axis.unitOfBeat (b) - viewStart) / fpp); };
        g.setFont (font (type::caption));
        if (tick * pixelsPerBeat >= 6.0)
        {
            g.setColour (colour::gridBeat);
            for (auto b = std::floor (firstBeat / tick) * tick; b <= lastBeat; b += tick)
                if (const auto x = xOf (b); x >= (float) rulerArea.getX())
                    g.drawVerticalLine ((int) x, (float) rulerArea.getBottom() - 4.0f, (float) rulerArea.getBottom());
        }
        for (auto b = std::floor (firstBeat / step) * step; b <= lastBeat; b += step)
        {
            const auto x = xOf (b);
            if (x < (float) rulerArea.getX())
                continue;
            g.setColour (colour::gridBar);
            g.drawVerticalLine ((int) x, (float) rulerArea.getBottom() - 7.0f, (float) rulerArea.getBottom());
            g.setColour (colour::textSecondary);
            g.drawText (juce::String (warpview::beatLabel (b, perBar)), juce::Rectangle<float> (x + 3.0f, (float) rulerArea.getY(), 60.0f, 13.0f),
                        juce::Justification::centredLeft);
        }
    }

    // the time ruler's step in seconds: one that leaves ~90 px between labels (beats doubled in the bar format)
    double timeRulerStep() const
    {
        const auto rate = doc.audio.sampleRate;
        const auto secondsPerPixel = fpp / rate;
        double step = 0.001;
        for (double s : { 0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 })
            if (s / secondsPerPixel >= 90.0)
            {
                step = s;
                break;
            }
        if (timeFormat == TimeFormat::bars)
        {
            step = 60.0 / juce::jmax (1.0, engine.getSong().bpm);
            while (step / secondsPerPixel < 90.0)
                step *= 2.0;
        }
        return step;
    }

    void paintTimeRuler (juce::Graphics& g)
    {
        using namespace theme;
        const auto rate = doc.audio.sampleRate;
        const auto step = timeRulerStep();

        g.setFont (font (type::caption));
        const auto firstSecond = viewStart / rate;
        const auto lastSecond = (viewStart + waveArea.getWidth() * fpp) / rate;
        for (auto t = std::floor (firstSecond / step) * step; t <= lastSecond; t += step)
        {
            const auto x = frameToX (t * rate);
            if (x < (float) rulerArea.getX())
                continue;
            g.setColour (colour::gridBar);
            g.drawVerticalLine ((int) x, (float) rulerArea.getBottom() - 7.0f, (float) rulerArea.getBottom());
            g.setColour (colour::textSecondary);
            g.drawText (formatTime ((std::int64_t) std::llround (t * rate)), juce::Rectangle<float> (x + 3.0f, (float) rulerArea.getY(), 90.0f, 13.0f),
                        juce::Justification::centredLeft);
        }
    }

    void paintRulerMarks (juce::Graphics& g)
    {
        using namespace theme;
        for (const auto& m : doc.markers)
        {
            if (! viewRegions)
                break;
            const auto x = frameToX ((double) m.start);
            if (m.isRegion())
            {
                g.setColour (colour::warning.withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, (float) rulerArea.getY() + 12.0f, frameToX ((double) m.end), (float) rulerArea.getBottom() - 5.0f));
            }
            juce::Path flag;
            flag.addTriangle (x, (float) rulerArea.getY() + 12.0f, x + 7.0f, (float) rulerArea.getY() + 15.5f, x, (float) rulerArea.getY() + 19.0f);
            g.setColour (colour::warning);
            g.fillPath (flag);
            g.drawVerticalLine ((int) x, (float) rulerArea.getY() + 12.0f, (float) rulerArea.getBottom());
            if (! m.name.empty())
                g.drawText (juce::String::fromUTF8 (m.name.c_str()), juce::Rectangle<float> (x + 8.0f, (float) rulerArea.getY() + 12.0f, 120.0f, 8.0f),
                            juce::Justification::centredLeft);
        }
        if (doc.hasLoop() && viewLoop)
        {
            const auto a = frameToX ((double) doc.loopStart), b = frameToX ((double) doc.loopEnd);
            g.setColour (colour::accent);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (a, (float) rulerArea.getBottom() - 4.0f, b, (float) rulerArea.getBottom()));
            g.fillRect (juce::Rectangle<float> (a, (float) rulerArea.getBottom() - 8.0f, 2.0f, 8.0f));
            g.fillRect (juce::Rectangle<float> (b - 2.0f, (float) rulerArea.getBottom() - 8.0f, 2.0f, 8.0f));
        }
    }

    void paintOverlays (juce::Graphics& g)
    {
        using namespace theme;
        if (doc.audio.empty())
            return;
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (waveArea);

        for (const auto& m : doc.markers)
        {
            if (! viewRegions)
                break;
            g.setColour (colour::warning.withAlpha (0.55f));
            g.drawVerticalLine ((int) frameToX ((double) m.start), (float) waveArea.getY(), (float) waveArea.getBottom());
            if (m.isRegion())
            {
                g.setColour (colour::warning.withAlpha (0.3f));
                g.drawVerticalLine ((int) frameToX ((double) m.end), (float) waveArea.getY(), (float) waveArea.getBottom());
            }
        }
        if (doc.hasLoop() && viewLoop)
        {
            g.setColour (colour::accent.withAlpha (0.6f));
            const float dash[] = { 4.0f, 3.0f };
            for (auto f : { doc.loopStart, doc.loopEnd })
            {
                const auto x = frameToX ((double) f);
                g.drawDashedLine ({ x, (float) waveArea.getY(), x, (float) waveArea.getBottom() }, dash, 2);
            }
        }
        paintEnvelopes (g);
        if (! selection.empty())
        {
            g.setColour (colour::selection.withAlpha (0.8f));
            g.drawVerticalLine ((int) frameToX ((double) selection.start), (float) waveArea.getY(), (float) waveArea.getBottom());
            g.drawVerticalLine ((int) frameToX ((double) selection.end), (float) waveArea.getY(), (float) waveArea.getBottom());
        }
        else
        {
            g.setColour (colour::textPrimary.withAlpha (0.7f));
            g.drawVerticalLine ((int) frameToX ((double) cursor), (float) waveArea.getY(), (float) waveArea.getBottom());
        }
        if (playing && playhead >= 0)
        {
            g.setColour (colour::playhead);
            g.fillRect (juce::Rectangle<float> (frameToX ((double) playhead) - 0.5f, (float) waveArea.getY(), 2.0f, (float) waveArea.getHeight()));
        }
        if (recState != RecState::idle)
        {
            auto badge = juce::Rectangle<float> ((float) waveArea.getX() + 8.0f, (float) waveArea.getY() + 8.0f, recState == RecState::recording ? 70.0f : 150.0f, 20.0f);
            g.setColour (colour::bg1.withAlpha (0.85f));
            g.fillRoundedRectangle (badge, radius::md);
            g.setColour (colour::record);
            g.fillEllipse (badge.removeFromLeft (20.0f).reduced (5.0f));
            g.setFont (font (type::small, true));
            g.drawText (recState == RecState::recording ? juce::String ("REC") : tr ("Waiting..."), badge, juce::Justification::centredLeft);
        }
    }

    void paintOverview (juce::Graphics& g)
    {
        using namespace theme;
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (overviewArea.toFloat(), radius::sm);
        if (doc.audio.empty() || summary == nullptr)
            return;
        const auto frames = (double) doc.audio.frames();
        const auto perPixel = frames / juce::jmax (1, overviewArea.getWidth());
        const auto centre = (float) overviewArea.getCentreY();
        const auto half = overviewArea.getHeight() * 0.5f - 1.0f;
        g.setColour (waveColour().withAlpha (0.55f));
        for (int x = 0; x < overviewArea.getWidth(); ++x)
        {
            // (round 190db: in beats, each pixel shows the file its beats play)
            const auto p = summary->query (frameOfUnit (x * perPixel), frameOfUnit ((x + 1) * perPixel), -1);
            if (! p.valid)
                break;
            const auto a = juce::jmin (1.0f, std::max (std::abs (p.lo), std::abs (p.hi)));
            g.drawVerticalLine (overviewArea.getX() + x, centre - a * half, centre + a * half + 1.0f);
        }
        const auto x0 = (float) overviewArea.getX() + (float) (viewStart / frames) * overviewArea.getWidth();
        const auto w = (float) ((double) waveArea.getWidth() * fpp / frames) * overviewArea.getWidth();
        g.setColour (colour::accent.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (x0, (float) overviewArea.getY(), juce::jmax (3.0f, w), (float) overviewArea.getHeight()));
        g.setColour (colour::accent);
        g.drawRect (juce::Rectangle<float> (x0, (float) overviewArea.getY(), juce::jmax (3.0f, w), (float) overviewArea.getHeight()), 1.0f);
    }

    void paintInfo (juce::Graphics& g)
    {
        using namespace theme;
        g.setColour (colour::bg1);
        g.fillRect (infoArea);
        g.setColour (colour::border);
        g.drawHorizontalLine (infoArea.getY(), 0.0f, (float) getWidth());

        auto area = infoArea.reduced (10, 0);
        g.setFont (font (type::small));
        if (status.isNotEmpty())
        {
            g.setColour (statusIsError ? colour::meterClip : colour::textSecondary);
            g.drawText (status, area.removeFromRight (juce::jmin (420, area.getWidth() / 2)), juce::Justification::centredRight);
        }
        if (doc.audio.empty())
            return;

        juce::StringArray parts;
        parts.add (juce::String (juce::roundToInt (doc.audio.sampleRate)) + " Hz");
        parts.add (doc.audio.numChannels() > 1 ? tr ("Stereo") : tr ("Mono"));
        parts.add (tr ("Length") + " " + formatTime (doc.audio.frames()));
        const auto r = ae::orAll (doc, selection);
        if (! selection.empty())
            parts.add (tr ("Selection") + " " + formatTime (selection.start) + " - " + formatTime (selection.end) + " (" + formatTime (selection.length()) + ")");
        else
            parts.add (tr ("Cursor") + " " + formatTime (cursor));
        const auto p = ae::peak (doc.audio, r);
        parts.add (tr ("Peak") + " " + (p > 0.0f ? juce::String (20.0f * std::log10 (p), 1) + " dB" : juce::String ("-inf dB")));
        g.setColour (colour::textPrimary);
        g.drawText (parts.joinIntoString ("   |   "), area, juce::Justification::centredLeft);
    }

    juce::String formatTime (std::int64_t frames) const
    {
        const auto rate = doc.audio.sampleRate;
        switch (timeFormat)
        {
            case TimeFormat::samples: return juce::String (frames);
            case TimeFormat::bars:
            {
                const auto beats = (double) frames / rate * engine.getSong().bpm / 60.0;
                const auto perBar = juce::jmax (1, engine.getSong().beatsPerBar);
                const auto bar = (int) (beats / perBar);
                const auto beat = (int) beats - bar * perBar;
                const auto tick = (int) ((beats - std::floor (beats)) * 96.0);
                return juce::String (bar + 1) + ":" + juce::String (beat + 1) + ":" + juce::String (tick).paddedLeft ('0', 2);
            }
            case TimeFormat::automatic:
                if (fpp < 1.0)
                    return juce::String (frames);
                [[fallthrough]];
            case TimeFormat::seconds: return formatSeconds ((double) frames / rate);
        }
        return {};
    }

    // --- edits ----------------------------------------------------------------------------------------------
    void commit (const juce::String& label)
    {
        history.commit (doc, label.toStdString());
        modified = true;
        documentChanged();
    }

    // runs `fn` on the selection (an empty selection: the whole audio, unless `needsSelection`)
    void edit (const juce::String& label, const std::function<void (ae::Document&, ae::Range)>& fn, bool needsSelection = false)
    {
        if (doc.audio.empty() || recState != RecState::idle)
            return;
        if (needsSelection && selection.empty())
        {
            setStatus (tr ("Select a part first (drag over the waveform)"), false);
            return;
        }
        stopPlayback();
        const auto frames = doc.audio.frames();
        const auto edited = ae::orAll (doc, selection);
        fn (doc, selection);
        if (ae::hasEnvelopes (doc) && doc.audio.frames() != frames)
            ae::remapEnvelopes (doc, frames, edited, edited.length() + (doc.audio.frames() - frames));
        // a length-changing edit of the selection keeps the edited part selected
        if (! selection.empty())
            selection.end = juce::jmax (selection.start, selection.end + (doc.audio.frames() - frames));
        commit (label);
    }

    void documentChanged()
    {
        if (slotMode() && session != nullptr && ! adopting && recState == RecState::idle)
        {
            session->setDocument (doc); // the slot plays and keeps what the editor shows
            seenRevision = session->revision();
        }
        ++docVersion;
        selection = ae::clamp (doc, selection);
        cursor = juce::jlimit<std::int64_t> (0, doc.audio.frames(), cursor);
        rebuildSummary();
        clampView();
        updateButtons();
        updateTitle();
        repaint();
    }

    void rebuildSummary()
    {
        if (doc.audio.empty())
        {
            summary.reset();
            return;
        }
        const auto* l = doc.audio.channels[0].data();
        const auto* r = doc.audio.numChannels() > 1 ? doc.audio.channels[1].data() : nullptr;
        summary = std::make_shared<wave::Summary> (wave::analyse (l, r, (std::size_t) doc.audio.frames(), doc.audio.sampleRate));
    }

    void undo()
    {
        if (! history.canUndo() || recState != RecState::idle)
            return;
        stopPlayback();
        doc = history.undo();
        modified = true;
        documentChanged();
    }

    void redo()
    {
        if (! history.canRedo() || recState != RecState::idle)
            return;
        stopPlayback();
        doc = history.redo();
        modified = true;
        documentChanged();
    }

    void copySelection()
    {
        if (selection.empty())
            return;
        clipboard() = ae::copy (doc.audio, selection);
        setStatus (tr ("Copied {0}", { formatTime (selection.length()) }), false);
    }

    void cutSelection()
    {
        if (selection.empty())
            return;
        clipboard() = ae::copy (doc.audio, selection);
        const auto at = selection.start;
        edit (tr ("Cut"), [] (ae::Document& d, ae::Range r) { ae::erase (d, r); }, true);
        selection = {};
        cursor = at;
        repaint();
    }

    void trimSelection()
    {
        if (selection.empty())
            return;
        edit (tr ("Trim"), [] (ae::Document& d, ae::Range r) { ae::trim (d, r); }, true);
        selection = {};
        cursor = 0;
        zoomToFit();
    }

    void deleteSelection()
    {
        if (selection.empty())
            return;
        const auto at = selection.start;
        edit (tr ("Delete"), [] (ae::Document& d, ae::Range r) { ae::erase (d, r); }, true);
        selection = {};
        cursor = at;
        repaint();
    }

    // insert at the cursor, replace the selection, or (mix) sum at the cursor / selection start
    void paste (bool mix)
    {
        const auto& clip = clipboard();
        if (clip.empty() || recState != RecState::idle)
            return;
        stopPlayback();
        const auto wasEmpty = doc.audio.empty();
        const auto at = selection.empty() ? cursor : selection.start;
        const auto length = wasEmpty ? clip.frames() : ae::conform (clip, doc.audio.numChannels(), doc.audio.sampleRate).frames();
        if (mix)
            ae::pasteMix (doc, at, clip);
        else if (! selection.empty())
            ae::pasteReplace (doc, selection, clip);
        else
            ae::insert (doc, at, clip);
        selection = { at, at + length };
        commit (mix ? tr ("Paste mix") : tr ("Paste"));
        if (wasEmpty)
            zoomToFit();
    }

    // FL's Paste mix (envelope): the first time it shows the mix envelope to shape (top = the clipboard, bottom = the
    // original); the second time it mixes the clipboard in at the cursor / selection start with that envelope
    void pasteMixWithEnvelope()
    {
        const auto& clip = clipboard();
        if (clip.empty() || doc.audio.empty() || recState != RecState::idle)
            return;
        if (envMode != EnvMode::mix)
        {
            if (doc.mixEnvelope.empty())
                doc.mixEnvelope = ae::Envelope::flat (0.5);
            setEnvMode (EnvMode::mix);
            setStatus (tr ("Shape the mix envelope (top: the clipboard, bottom: the original), then choose Edit > Paste mix (envelope): apply."), false);
            repaint();
            return;
        }
        stopPlayback();
        const auto at = selection.empty() ? cursor : selection.start;
        const auto mix = doc.mixEnvelope;
        doc.mixEnvelope = {};
        const auto r = ae::pasteMixEnvelope (doc, at, clip, mix);
        setEnvMode (EnvMode::wave);
        selection = r;
        commit (tr ("Paste mix (envelope)"));
    }

    // FL's Scratch (envelope): the first time it shows the position envelope (a rising diagonal = the audio as it is);
    // the second time it renders the audio played along that envelope
    void scratchWithEnvelope()
    {
        if (doc.audio.empty() || recState != RecState::idle)
            return;
        if (envMode != EnvMode::scratch)
        {
            if (doc.scratchEnvelope.empty())
                doc.scratchEnvelope.points = { { 0.0, 0.0, 0.0f }, { 1.0, 1.0, 0.0f } };
            setEnvMode (EnvMode::scratch);
            setStatus (tr ("Draw the play position over time (rising = forwards, falling = backwards, flat = held), then choose Tools > Scratch (envelope): apply."), false);
            repaint();
            return;
        }
        stopPlayback();
        const auto env = doc.scratchEnvelope;
        doc.scratchEnvelope = {};
        ae::scratch (doc, env);
        setEnvMode (EnvMode::wave);
        commit (tr ("Scratch (envelope)"));
    }

    // the clipboard stretched to the selection's length (pitch kept), or drum-stretched (slices keep their attacks),
    // replacing the selection
    void pasteFitted (bool drum)
    {
        const auto& clip = clipboard();
        if (clip.empty() || selection.empty() || recState != RecState::idle)
            return;
        stopPlayback();
        ae::Document part;
        part.audio = ae::conform (clip, doc.audio.numChannels(), doc.audio.sampleRate);
        const auto ratio = (double) selection.length() / (double) juce::jmax<std::int64_t> (1, part.audio.frames());
        if (drum)
        {
            ae::DrumStretchSettings s;
            s.ratio = ratio;
            ae::drumloopStretch (part, {}, s);
        }
        else
            ae::timeStretch (part, {}, ratio);
        for (auto& ch : part.audio.channels)
            ch.resize ((std::size_t) selection.length(), 0.0f);
        ae::pasteReplace (doc, selection, part.audio);
        commit (drum ? tr ("Paste replace (drum)") : tr ("Paste stretch"));
    }

    void fade (bool in)
    {
        const auto curve = fadeCurve;
        edit (in ? tr ("Fade in") : tr ("Fade out"), [in, curve] (ae::Document& d, ae::Range r) { in ? ae::fadeIn (d, r, curve) : ae::fadeOut (d, r, curve); });
    }

    void addMarkerAtCursor()
    {
        if (doc.audio.empty())
            return;
        ae::addMarker (doc, selection.empty() ? cursor : selection.start, {});
        commit (tr ("Add marker"));
    }

    void addRegionFromSelection()
    {
        if (selection.empty())
        {
            setStatus (tr ("Select a part first (drag over the waveform)"), false);
            return;
        }
        ae::addRegion (doc, selection, tr ("Region").toStdString() + " " + std::to_string (std::count_if (doc.markers.begin(), doc.markers.end(), [] (const ae::Marker& m) { return m.isRegion(); }) + 1));
        commit (tr ("Add region"));
    }

    void setLoopFromSelection()
    {
        if (selection.empty())
        {
            setStatus (tr ("Select a part first (drag over the waveform)"), false);
            return;
        }
        ae::setLoop (doc, selection);
        commit (tr ("Set loop"));
    }

    // --- Tools › Analysis › Convert to score and dump to piano roll (REA_rodada167c.md §4): the whole sample's notes
    // into the selected channel's notes of the current pattern (replacing them), as FL's Edison
    void convertToScore()
    {
        if (doc.audio.empty() || scoreBusy)
            return;
        stopPlayback();
        auto data = std::make_shared<SampleData>();
        data->sampleRate = doc.audio.sampleRate;
        data->left = doc.audio.channels[0];
        if (doc.audio.numChannels() > 1)
            data->right = doc.audio.channels[1];
        scoreBusy = true;
        setStatus (tr ("Detecting score..."), false);
        juce::Component::SafePointer<AudioEditor> self (this);
        std::thread ([self, data] {
            auto notes = std::make_shared<std::vector<newtonefx::DetectedNote>> (newtonefx::detectScore (*data));
            juce::MessageManager::callAsync ([self, notes, rate = data->sampleRate] {
                if (self == nullptr)
                    return;
                self->scoreBusy = false;
                if (notes->empty())
                {
                    self->setStatus (tr ("No notes found in the sample"), false);
                    return;
                }
                self->setStatus (tr ("Dumping score..."), false);
                const auto channel = self->engine.getSelectedChannel();
                int count = 0;
                self->engine.editSong ([&] (Song& s) {
                    if (channel >= 0 && channel < (int) s.channels.size())
                        count = newtonefx::dumpScore (s, channel, *notes, rate);
                });
                self->setStatus (tr ("{0} notes put in the Piano roll of the selected channel", { juce::String (count) }), false);
            });
        }).detach();
    }

    // --- Tools › Analysis › Convert to MIDI (round a2m189b): the selection (or the whole audio) as a new pattern
    void convertToMidi (a2m::Mode mode)
    {
        if (doc.audio.empty())
            return;
        stopPlayback();
        const auto range = selection.empty() ? ae::Range { 0, (std::int64_t) doc.audio.frames() } : ae::clamp (doc, selection);
        const auto from = range.start;
        std::vector<float> mono ((std::size_t) range.length(), 0.0f);
        const auto channels = doc.audio.numChannels();
        for (const auto& ch : doc.audio.channels)
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] += ch[(std::size_t) from + i] / (float) channels;
        juce::Component::SafePointer<AudioEditor> self (this);
        convertAudioToMidi (engine, std::move (mono), doc.audio.sampleRate, mode, safeTitle(), [self] (int count) {
            if (self != nullptr && count > 0)
                self->setStatus (tr ("{0} notes put in a new pattern", { juce::String (count) }), false);
        });
    }

    // --- scripts (Tools › Scripting: Run script / Edit last script; REA_rodada167c.md §2) -----------------------------
    struct ScriptRef
    {
        juce::String name;   // menu name
        juce::String key;    // the form memory's key (the script's path, or "lynx:<folder>/<name>")
        juce::String folder; // a built-in script's folder
        std::string source;  // a built-in script's text (a file is read when it runs)
        juce::File file;
    };

    struct ScriptJob
    {
        ScriptRef ref;
        std::string source;
        ae::Document start;
        ae::Range selection;
        ae::Audio clip;
        std::vector<ae::script::Form> answers;
        std::uint64_t version = 0;
    };

    struct ScriptResult
    {
        ae::script::Outcome outcome;
        ae::Document doc;
        std::optional<ae::Audio> clip;
    };

    // where scripts come from: FL Studio's own folder (System/Config/Audio scripts, read in place), then the user's
    // Documents/LYNX/Audio scripts
    static std::vector<std::pair<juce::String, juce::File>> scriptFolders()
    {
        static const auto fl = [] {
            const auto roots = findFlStudioRoots();
            return roots.factoryData.empty() ? juce::File()
                                             : platform::localFile (roots.factoryData).getChildFile ("System").getChildFile ("Config").getChildFile ("Audio scripts");
        }();
        std::vector<std::pair<juce::String, juce::File>> out;
        if (fl.isDirectory())
            out.emplace_back ("FL Studio", fl);
        out.emplace_back (tr ("My scripts"), platform::documentsFolder().getChildFile ("Audio scripts"));
        return out;
    }

    juce::PopupMenu scriptMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("LYNX SHARE");
        std::map<juce::String, juce::PopupMenu> own;
        for (const auto& b : ae::script::builtins())
        {
            ScriptRef r;
            r.name = juce::String::fromUTF8 (b.name);
            r.folder = juce::String::fromUTF8 (b.folder);
            r.key = "lynx:" + r.folder + "/" + r.name;
            r.source = b.source;
            own[r.folder].addItem (r.name, true, false, [this, r] { runScript (r); });
        }
        for (auto& [folder, sub] : own)
            menu.addSubMenu (folder, sub);
        for (const auto& [title, dir] : scriptFolders())
        {
            menu.addSectionHeader (title);
            auto addFolder = [this] (const juce::File& d, juce::PopupMenu& into, auto& self, int depth) -> bool {
                bool any = false;
                auto subs = d.findChildFiles (juce::File::findDirectories, false);
                subs.sort();
                for (const auto& s : subs)
                {
                    juce::PopupMenu sub;
                    if (depth < 3 && self (s, sub, self, depth + 1))
                    {
                        into.addSubMenu (s.getFileName(), sub);
                        any = true;
                    }
                }
                auto files = d.findChildFiles (juce::File::findFiles, false, "*.pyscript");
                files.sort();
                for (const auto& f : files)
                {
                    ScriptRef r;
                    r.name = f.getFileNameWithoutExtension();
                    r.key = f.getFullPathName();
                    r.file = f;
                    into.addItem (r.name, true, false, [this, r] { runScript (r); });
                    any = true;
                }
                return any;
            };
            if (! dir.isDirectory() || ! addFolder (dir, menu, addFolder, 0))
                menu.addItem (tr ("(no scripts)"), false, false, nullptr);
        }
        menu.addSeparator();
        menu.addItem (tr ("Open my scripts folder"), true, false, [] {
            const auto dir = platform::documentsFolder().getChildFile ("Audio scripts");
            dir.createDirectory();
            if (platform::testMode())
                platform::testModeLog ("open folder " + dir.getFullPathName());
            else
                dir.startAsProcess();
        });
        return menu;
    }

    void runScript (const ScriptRef& ref)
    {
        if (scriptBusy || recState != RecState::idle)
            return;
        auto job = std::make_shared<ScriptJob>();
        job->ref = ref;
        if (ref.file != juce::File())
        {
            juce::MemoryBlock bytes;
            if (! ref.file.loadFileAsData (bytes) || ! juce::CharPointer_UTF8::isValidString (static_cast<const char*> (bytes.getData()), (int) bytes.getSize()))
            {
                showScriptText (tr ("Error"), tr ("Failed to load the script file. Please make sure it is saved in utf8-format."));
                return;
            }
            job->source.assign (static_cast<const char*> (bytes.getData()), bytes.getSize());
        }
        else
            job->source = ref.source;
        lastScript = ref;
        stopPlayback();
        job->start = doc;
        job->selection = selection;
        job->clip = clipboard();
        job->version = docVersion;
        scriptStep (job);
    }

    // runs the script on a worker thread (the Python thread does the work; the UI never waits for it)
    static void launchScript (std::shared_ptr<const ScriptJob> job, std::vector<ae::script::Form> answers,
                              std::function<void (std::shared_ptr<ScriptResult>)> done)
    {
        std::thread ([job, answers = std::move (answers), done = std::move (done)] {
            auto r = std::make_shared<ScriptResult>();
            r->doc = job->start;
            ae::script::Host host;
            host.clipboard = [job] (ae::Audio& a) {
                a = job->clip;
                return ! a.empty();
            };
            host.setClipboard = [r] (const ae::Audio& a) { r->clip = a; };
            host.loadFile = [] (const std::string& path, ae::Audio& a) { return readAudioFile (juce::File (juce::String::fromUTF8 (path.c_str())), a); };
            r->outcome = ae::script::run (job->source, r->doc, job->selection, answers, host);
            juce::MessageManager::callAsync ([done, r] { done (r); });
        }).detach();
    }

    void scriptStep (std::shared_ptr<ScriptJob> job)
    {
        scriptBusy = true;
        setStatus (tr ("Running script {0}...", { job->ref.name }), false);
        juce::Component::SafePointer<AudioEditor> self (this);
        launchScript (job, job->answers, [self, job] (std::shared_ptr<ScriptResult> r) {
            if (self != nullptr)
                self->scriptFinished (job, *r);
        });
    }

    void scriptFinished (std::shared_ptr<ScriptJob> job, ScriptResult& r)
    {
        scriptBusy = false;
        using S = ae::script::Outcome::Status;
        auto& out = r.outcome;
        if (out.status == S::failed)
        {
            setStatus (tr ("The script stopped with an error"), true);
            showScriptText (tr ("Script error"), juce::String::fromUTF8 (out.error.c_str()));
            return;
        }
        juce::Component::SafePointer<AudioEditor> self (this);
        if (out.status == S::needsForm)
        {
            setStatus ({}, false);
            auto form = out.form;
            const auto key = job->ref.key.toStdString();
            if (form.controls.size() == 1 && form.controls[0].hint == "file") // Sample.LoadFromFile_Ask
            {
                auto chooser = lynx::ui::makeSharedFileChooser (tr ("Open"), juce::File(), "*.wav;*.flac;*.aif;*.aiff;*.ogg;*.mp3");
                lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self, job, form, chooser] (const juce::FileChooser& fc) {
                    if (self == nullptr)
                        return;
                    auto answer = form;
                    answer.controls[0].text = fc.getResult().getFullPathName().toStdString();
                    job->answers.push_back (answer);
                    self->scriptStep (job);
                });
                return;
            }
            ae::script::restore (key, form);
            auto panel = std::make_unique<ScriptPanel> (
                form,
                [self, job, key] (const ae::script::Form& f) {
                    ae::script::remember (key, f);
                    job->answers.push_back (f);
                    if (self != nullptr)
                        self->scriptStep (job);
                },
                [self, job] (const ae::script::Form& f) {
                    if (self != nullptr)
                        self->previewScript (job, f);
                },
                [self] {
                    if (self != nullptr)
                        self->editLastScript();
                });
            openToolWindow (std::move (panel), tr ("Configure script"), "audioeditor-script");
            return;
        }

        // done: one undo step (nothing when the script changed nothing)
        for (const auto& m : out.messages)
            showScriptText (tr ("Script message"), juce::String::fromUTF8 (m.c_str()));
        if (r.clip)
            clipboard() = std::move (*r.clip);
        if (docVersion != job->version)
        {
            setStatus (tr ("The audio changed while the script ran: run it again"), true);
            return;
        }
        const bool same = r.doc.audio.sampleRate == doc.audio.sampleRate && r.doc.audio.channels == doc.audio.channels && r.doc.markers.size() == doc.markers.size()
                          && std::equal (r.doc.markers.begin(), r.doc.markers.end(), doc.markers.begin(), [] (const ae::Marker& a, const ae::Marker& b) {
                                 return a.start == b.start && a.end == b.end && a.name == b.name && a.info == b.info && a.keyNum == b.keyNum;
                             });
        setStatus (tr ("Script {0} done", { job->ref.name }), false);
        if (same)
            return;
        const bool wasEmpty = doc.audio.empty();
        if (wasEmpty)
            history.reset (doc, tr ("Empty").toStdString());
        doc = std::move (r.doc);
        commit (tr ("Script: {0}", { job->ref.name }));
        if (wasEmpty)
            zoomToFit();
    }

    // Preview: the script with these values on a copy, played from the selection (or the start)
    void previewScript (std::shared_ptr<ScriptJob> job, const ae::script::Form& f)
    {
        auto answers = job->answers;
        answers.push_back (f);
        const auto ticket = ++previewTicket;
        juce::Component::SafePointer<AudioEditor> self (this);
        launchScript (job, std::move (answers), [self, job, ticket] (std::shared_ptr<ScriptResult> r) {
            if (self == nullptr || ticket != self->previewTicket || r->outcome.status != ae::script::Outcome::Status::done || r->doc.audio.empty())
                return;
            const auto from = juce::jlimit<std::int64_t> (0, r->doc.audio.frames(), job->selection.empty() ? 0 : job->selection.start);
            self->playAudio (ae::copy (r->doc.audio, { from, r->doc.audio.frames() }), 0, false);
        });
    }

    // Edit last script: the script's file in the system's editor (a LYNX script is first saved to My scripts)
    void editLastScript()
    {
        if (lastScript.key.isEmpty())
            return;
        auto file = lastScript.file;
        if (file == juce::File())
        {
            file = platform::documentsFolder().getChildFile ("Audio scripts").getChildFile (lastScript.folder).getChildFile (lastScript.name + ".pyscript");
            if (! file.existsAsFile())
            {
                file.getParentDirectory().createDirectory();
                file.replaceWithText (juce::String::fromUTF8 (lastScript.source.c_str()), false, false, "\n");
            }
            lastScript.file = file;
            lastScript.key = file.getFullPathName();
        }
        if (platform::testMode())
            platform::testModeLog ("edit script " + file.getFullPathName());
        else if (! file.startAsProcess())
            file.revealToUser();
    }

    void showScriptText (const juce::String& title, const juce::String& text)
    {
        if (platform::testMode())
        {
            platform::testModeLog (title + ": " + text);
            return;
        }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, title, text, tr ("OK"), this);
    }

    // --- tool windows ---------------------------------------------------------------------------------------
    // plays `fn` applied to a copy of the selection (Preview)
    void previewProcessed (const std::function<void (ae::Document&)>& fn)
    {
        ae::Document part;
        part.audio = ae::copy (doc.audio, ae::orAll (doc, selection));
        fn (part);
        playAudio (part.audio, 0, false);
    }

    void openTool (std::unique_ptr<ToolPanel> panel, const juce::String& title)
    {
        openToolWindow (std::move (panel), title, "audioeditor-tool");
    }

    void showAmp() { openEdisonTool (audiotools::Tool::amp); } // Edison's Amp form (round 165l)

    void showResample()
    {
        if (doc.audio.empty())
            return;
        static const int rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000 };
        juce::StringArray names;
        int current = 5;
        for (int i = 0; i < 10; ++i)
        {
            names.add (juce::String (rates[i]) + " Hz");
            if (std::abs (rates[i] - doc.audio.sampleRate) < 1.0)
                current = i;
        }
        juce::Component::SafePointer<AudioEditor> self (this);
        openTool (std::make_unique<ToolPanel> (std::vector<ToolPanel::Param> {}, names, tr ("Sample rate"), current,
                                               [self] (const std::vector<double>&, int i) {
                                                   if (self != nullptr && i >= 0)
                                                   {
                                                       self->selection = {};
                                                       self->edit (tr ("Resample"), [i] (ae::Document& d, ae::Range) { ae::resample (d, rates[i]); });
                                                       self->zoomToFit();
                                                   }
                                               },
                                               nullptr),
                  tr ("Resample"));
    }

    void showInsertSilence()
    {
        if (doc.audio.empty())
            return;
        juce::Component::SafePointer<AudioEditor> self (this);
        openTool (std::make_unique<ToolPanel> (std::vector<ToolPanel::Param> { { tr ("Length"), 1.0, 10000.0, 500.0, 1.0, " ms" } }, juce::StringArray {},
                                               juce::String {}, 0,
                                               [self] (const std::vector<double>& v, int) {
                                                   if (self == nullptr)
                                                       return;
                                                   const auto frames = (std::int64_t) std::llround (v[0] * 0.001 * self->doc.audio.sampleRate);
                                                   const auto at = self->cursor;
                                                   ae::insertSilence (self->doc, at, frames);
                                                   self->selection = { at, at + frames };
                                                   self->commit (tr ("Insert silence"));
                                               },
                                               nullptr),
                  tr ("Insert silence"));
    }

    void showNormalizeTo()
    {
        if (doc.audio.empty())
            return;
        juce::Component::SafePointer<AudioEditor> self (this);
        openTool (std::make_unique<ToolPanel> (std::vector<ToolPanel::Param> { { tr ("Peak"), -24.0, 0.0, -0.3, 0.1, " dB" } }, juce::StringArray {}, juce::String {}, 0,
                                               [self] (const std::vector<double>& v, int) {
                                                   if (self != nullptr)
                                                       self->edit (tr ("Normalize"), [v] (ae::Document& d, ae::Range r) { ae::normalize (d, r, (float) v[0]); });
                                               },
                                               nullptr),
                  tr ("Normalize"));
    }

    void autoSlice (float sensitivity)
    {
        if (doc.audio.empty())
            return;
        const auto r = ae::orAll (doc, selection);
        auto part = ae::copy (doc.audio, r);
        int added = 0;
        for (auto f : ae::detectOnsets (part, sensitivity))
            if (f > 0 || r.start > 0)
            {
                ae::addMarker (doc, r.start + f, {});
                ++added;
            }
        if (added > 0)
            commit (tr ("Auto-slice"));
        setStatus (tr ("{0} markers added", { juce::String (added) }), false);
    }

    void gridSlice (double beats)
    {
        if (doc.audio.empty())
            return;
        const auto step = beats * 60.0 / juce::jmax (1.0, engine.getSong().bpm) * doc.audio.sampleRate;
        const auto r = ae::orAll (doc, selection);
        int added = 0;
        for (auto f = (double) r.start + step; f < (double) r.end - 1.0; f += step, ++added)
            ae::addMarker (doc, (std::int64_t) std::llround (f), {});
        if (added > 0)
            commit (tr ("Grid slice"));
    }

    // --- playback -------------------------------------------------------------------------------------------
    void playAudio (const ae::Audio& audio, std::int64_t from, bool loop)
    {
        if (audio.empty())
            return;
        if (slotMode())
        {
            // an Edison slot plays into its own insert, after the slot (round 190cd)
            const auto p = processor();
            if (p == nullptr)
                return;
            p->play (std::make_shared<const ae::Audio> (audio), loop, (float) engine.getEditorVolume());
        }
        else
        {
            auto data = std::make_shared<SampleData>();
            data->sampleRate = audio.sampleRate;
            data->left = audio.channels[0];
            if (audio.numChannels() > 1)
                data->right = audio.channels[1];
            engine.previewSample (data, loop, 1.0f, true); // (at the volume knob's level)
        }
        playing = true;
        playFrom = from;
        playStartedMs = juce::Time::getMillisecondCounterHiRes();
        playhead = from;
        playButton.setIcon (Icon::pause);
    }

    void startPlayback()
    {
        if (doc.audio.empty() || recState != RecState::idle)
            return;
        auto r = selection;
        if (r.empty() && loopPlay && doc.hasLoop())
            r = { doc.loopStart, doc.loopEnd };
        if (r.empty())
            r = { cursor, doc.audio.frames() };
        if (r.length() < 2)
            r = { 0, doc.audio.frames() };
        if (ae::hasEnvelopes (doc))
        {
            // the envelopes are heard before they are mixed down (like Edison)
            ae::Document mixed;
            mixed.audio = doc.audio;
            mixed.volumeEnvelope = doc.volumeEnvelope;
            mixed.panEnvelope = doc.panEnvelope;
            mixed.separationEnvelope = doc.separationEnvelope;
            ae::mixdownEnvelopes (mixed);
            playAudio (ae::copy (mixed.audio, r), r.start, loopPlay);
            return;
        }
        playAudio (ae::copy (doc.audio, r), r.start, loopPlay);
    }

    void stopPlayback()
    {
        if (! playing)
            return;
        if (slotMode())
        {
            if (const auto p = processor())
                p->play (nullptr, false);
        }
        else
            engine.previewSample (nullptr);
        playing = false;
        playhead = -1;
        playButton.setIcon (Icon::play);
        repaint();
    }

    // --- recording ------------------------------------------------------------------------------------------
    void toggleRecording()
    {
        if (recState == RecState::idle)
            startRecording();
        else
            stopRecording();
    }

    void startRecording()
    {
        if (slotMode())
        {
            startSlotRecording();
            return;
        }
        stopPlayback();
        auto* device = engine.getDeviceManager().getCurrentAudioDevice();
        if (device == nullptr)
        {
            setStatus (tr ("No audio device is open (Settings > Audio)"), true);
            return;
        }
        if (recordSource == AudioCapture::input)
        {
            if (! engine.hasAudioInput())
            {
                if (const auto result = engine.openAudioInput(); result.issue != AudioEngine::InputIssue::none)
                {
                    setStatus (result.detail.isNotEmpty() ? result.detail : tr ("No active audio input (Settings > Audio)"), true);
                    return;
                }
                openedInput = true; // closed again when the recording stops (round 165q)
            }
            device = engine.getDeviceManager().getCurrentAudioDevice();
            if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0)
            {
                setStatus (tr ("No active audio input (Settings > Audio)"), true);
                return;
            }
        }
        recordRate = device->getCurrentSampleRate();
        recordStereo = recordSource != AudioCapture::input || device->getActiveInputChannels().countNumberOfSetBits() > 1;
        recL.clear();
        recR.clear();
        beforeRecording = doc;
        recordAt = selection.empty() ? cursor : selection.start;
        if (doc.audio.empty())
            recordAt = 0;
        engine.getMeters().capture.setSource (recordSource);
        recState = recordMode == RecordMode::now ? RecState::recording : RecState::waiting;
        recordButton.setToggleState (true, juce::dontSendNotification);
        if (recordMode == RecordMode::onPlay && ! engine.getTransport().playing.load())
            setStatus (tr ("Recording starts when the song plays"), false);
        else if (recordMode == RecordMode::onInput)
            setStatus (tr ("Recording starts when the input is heard"), false);
        else
            setStatus (tr ("Recording from {0}", { sourceName (recordSource) }), false);
        repaint();
    }

    void drainRecording()
    {
        if (slotMode())
        {
            if (! session->pump())
            {
                finishSlotRecording();
                return;
            }
            const auto now = session->phase() == edisonfx::Phase::recording ? RecState::recording : RecState::waiting;
            if (now != recState)
            {
                recState = now;
                showSlotPhase();
            }
            return;
        }
        std::vector<float> l, r;
        engine.getMeters().capture.pull (l, r);
        if (l.empty())
            return;
        std::size_t from = 0;
        if (recState == RecState::waiting)
        {
            if (recordMode == RecordMode::onPlay)
            {
                if (! engine.getTransport().playing.load())
                    return;
            }
            else
            {
                while (from < l.size() && std::abs (l[from]) < inputThreshold && std::abs (r[from]) < inputThreshold)
                    ++from;
                if (from == l.size())
                    return;
            }
            recState = RecState::recording;
            setStatus (tr ("Recording from {0}", { sourceName (recordSource) }), false);
        }
        recL.insert (recL.end(), l.begin() + (std::ptrdiff_t) from, l.end());
        recR.insert (recR.end(), r.begin() + (std::ptrdiff_t) from, r.end());
        if (recordMode == RecordMode::onPlay && ! engine.getTransport().playing.load())
            stopRecording();
    }

    ae::Audio recordedAudio() const
    {
        ae::Audio a;
        a.sampleRate = recordRate;
        a.channels.push_back (recL);
        if (recordStereo)
            a.channels.push_back (recR);
        return a;
    }

    // the document = what it was, with the recording inserted at the cursor
    void showRecording()
    {
        if (slotMode())
        {
            doc = session->preview(); // (what the document will be when the recording stops now)
            recordAt = session->takeStart();
        }
        else
        {
            doc = beforeRecording;
            const auto rec = recordedAudio();
            if (doc.audio.empty())
            {
                doc.audio = rec;
                doc.title = tr ("Recording").toStdString();
            }
            else
                ae::insert (doc, recordAt, rec);
        }
        const auto length = doc.audio.frames() - beforeRecording.audio.frames();
        selection = { recordAt, recordAt + length };
        cursor = recordAt + length;
        rebuildSummary();
        ++docVersion;
        // follow the end
        const auto visible = (double) waveArea.getWidth() * fpp;
        if (beforeRecording.audio.empty() || (double) cursor > viewStart + visible)
        {
            if (beforeRecording.audio.empty() && (double) cursor > visible)
                fpp = juce::jmax (fpp, (double) cursor / juce::jmax (1, waveArea.getWidth()) * 1.25);
            viewStart = juce::jmax (0.0, (double) cursor - visible * 0.8);
        }
        clampView();
        repaint();
    }

    // the input this editor opened for its recording: closed again (FL does not keep the microphone open; Windows
    // showed its microphone-in-use icon until LYNX quit — round 165q)
    void releaseInput()
    {
        if (! openedInput)
            return;
        openedInput = false;
        engine.releaseAudioInputIfUnused();
    }

    void stopRecording()
    {
        if (recState == RecState::idle)
            return;
        if (slotMode())
        {
            session->stop();
            finishSlotRecording();
            return;
        }
        drainRecording();
        engine.getMeters().capture.setSource (AudioCapture::off);
        releaseInput();
        const auto got = ! recL.empty();
        const auto dropped = engine.getMeters().capture.droppedFrames();
        recState = RecState::idle;
        recordButton.setToggleState (false, juce::dontSendNotification);
        if (got)
        {
            showRecording();
            commit (tr ("Record"));
            setStatus (tr ("Recorded {0}", { formatTime ((std::int64_t) recL.size()) }) + (dropped > 0 ? "  (" + tr ("{0} frames lost", { juce::String (dropped) }) + ")" : ""), dropped > 0);
        }
        else
        {
            doc = beforeRecording;
            documentChanged();
            setStatus (tr ("Nothing was recorded"), false);
        }
        recL.clear();
        recR.clear();
    }

    juce::String sourceName (int s) const
    {
        if (s == AudioCapture::input)
            return tr ("the audio input");
        if (s == 0)
            return tr ("the Master");
        const auto& mixer = engine.getSong().mixer;
        if (juce::isPositiveAndBelow (s, (int) mixer.size()) && ! mixer[(std::size_t) s].name.empty())
            return tr ("insert {0}", { juce::String (s) }) + " (" + juce::String::fromUTF8 (mixer[(std::size_t) s].name.c_str()) + ")";
        return tr ("insert {0}", { juce::String (s) });
    }

public:
    // Record on, if it is not (the mixer's "Open audio editor and record")
    void armRecording()
    {
        if (recState == RecState::idle)
            startRecording();
    }

    void recordInputWithSong()
    {
        recordSource = AudioCapture::input;
        recordMode = RecordMode::onPlay;
        startRecording();
    }

    void setRecordSource (int s)
    {
        recordSource = s;
        setStatus (tr ("Press Record to capture {0}", { sourceName (s) }), false);
    }

private:
    // --- output ---------------------------------------------------------------------------------------------
    juce::String safeTitle() const
    {
        auto t = juce::File::createLegalFileName (juce::String::fromUTF8 (doc.title.c_str())).trim();
        return t.isEmpty() ? juce::String ("Audio") : t;
    }

    // the edited audio as a new WAV in Music\LYNX\Edits (a new name each time: the Sampler and the waveform caches
    // key on the path, so nothing stale is ever shown)
    juce::File writeEditFile()
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Edits");
        folder.createDirectory();
        const auto file = folder.getNonexistentChildFile (safeTitle() + " (edit)", ".wav");
        if (! writeWav (file, bits))
            return {};
        return file;
    }

    bool writeWav (const juce::File& file, ae::WavFormat format)
    {
        const auto bytes = ae::encodeWav (doc, format);
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            setStatus (tr ("Could not write {0}", { file.getFullPathName() }), true);
            return false;
        }
        engine.getSampleLibrary().forget (file.getFullPathName());
        return true;
    }

    void saveAs (ae::WavFormat format)
    {
        if (doc.audio.empty())
            return;
        auto start = lastFile.existsAsFile() ? lastFile.getSiblingFile (safeTitle() + ".wav")
                                             : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (safeTitle() + ".wav");
        chooser = lynx::ui::makeFileChooser (tr ("Save as WAV"), start, "*.wav");
        juce::Component::SafePointer<AudioEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [self, format] (const juce::FileChooser& fc) {
                                  if (self == nullptr || fc.getResult() == juce::File {})
                                      return;
                                  const auto file = fc.getResult().withFileExtension ("wav");
                                  if (self->writeWav (file, format))
                                  {
                                      self->bits = format;
                                      self->lastFile = file;
                                      self->modified = false;
                                      self->setStatus (tr ("Saved {0}", { file.getFileName() }), false);
                                      self->updateTitle();
                                  }
                              });
    }

    void openFileDialog()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Open audio file"), lastFile, engine.getSampleLibrary().wildcard());
        juce::Component::SafePointer<AudioEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc) {
            if (self == nullptr || ! fc.getResult().existsAsFile())
                return;
            self->stopPlayback();
            if (! self->slotMode())
                self->source = { Source::Kind::file, -1, fc.getResult().getFullPathName() };
            self->loadFile (fc.getResult());
        });
    }

    // each region (or each stretch between markers when there are no regions) as its own WAV in a folder
    void exportRegions()
    {
        std::vector<ae::Range> parts;
        std::vector<std::string> names;
        for (const auto& m : doc.markers)
            if (m.isRegion())
            {
                parts.push_back ({ m.start, m.end });
                names.push_back (m.name);
            }
        if (parts.empty())
        {
            std::int64_t from = 0;
            for (const auto& m : doc.markers)
                if (m.start > from)
                {
                    parts.push_back ({ from, m.start });
                    names.push_back ({});
                    from = m.start;
                }
            if (from > 0 && from < doc.audio.frames())
            {
                parts.push_back ({ from, doc.audio.frames() });
                names.push_back ({});
            }
        }
        if (parts.empty())
        {
            setStatus (tr ("No regions or markers to export"), true);
            return;
        }
        chooser = lynx::ui::makeFileChooser (tr ("Export regions to a folder"), juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        juce::Component::SafePointer<AudioEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [self, parts, names] (const juce::FileChooser& fc) {
                                  if (self == nullptr || ! fc.getResult().isDirectory())
                                      return;
                                  int written = 0;
                                  for (std::size_t i = 0; i < parts.size(); ++i)
                                  {
                                      ae::Document part;
                                      part.audio = ae::copy (self->doc.audio, parts[i]);
                                      const auto name = names[i].empty() ? self->safeTitle() + " " + juce::String (i + 1).paddedLeft ('0', 2)
                                                                         : juce::File::createLegalFileName (juce::String::fromUTF8 (names[i].c_str()));
                                      const auto bytes = ae::encodeWav (part, self->bits);
                                      written += fc.getResult().getNonexistentChildFile (name, ".wav").replaceWithData (bytes.data(), bytes.size()) ? 1 : 0;
                                  }
                                  self->setStatus (tr ("{0} files written", { juce::String (written) }), false);
                              });
    }

    void sendToPlaylist()
    {
        if (doc.audio.empty())
            return;
        const auto file = writeEditFile();
        if (file == juce::File {})
            return;
        const auto& song = engine.getSong();
        const auto length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) (doc.audio.seconds() * song.bpm / 60.0 * ppq));
        const auto start = (std::int64_t) (engine.getTransport().positionTicks.load() / ppq) * ppq; // at the beat of the song position
        // the first track with room for it
        int track = 0;
        for (;; ++track)
        {
            const auto busy = std::any_of (song.playlist.begin(), song.playlist.end(),
                                           [&] (const Clip& c) { return c.track == track && c.start < start + length && c.end() > start; });
            if (! busy)
                break;
        }
        const auto path = file.getFullPathName().toStdString();
        const auto name = safeTitle().toStdString();
        engine.editSong ([=] (Song& s) {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::audioClip;
            c.sampler.declickMode = audioClipDeclickMode; // FL's AudioTrack.fst
            c.samplePath = path;
            c.mixerTrack = s.claimFreeInsert();
            s.channels.push_back (c);
            Clip clip;
            clip.kind = ClipKind::audio;
            clip.channel = (int) s.channels.size() - 1;
            clip.track = track;
            clip.start = start;
            clip.length = length;
            s.playlist.push_back (clip);
            s.mode = PlayMode::song;
        });
        modified = false;
        setStatus (tr ("Sent to the Playlist (track {0})", { juce::String (track + 1) }), false);
        updateTitle();
    }

    // FL's Edison "Send to channel" of a part (issue #12): the selection (or a region) as a new Sampler channel's
    // sample; the editor keeps the whole audio (nothing cut or cropped)
    void sendRangeToNewSampler (ae::Range range)
    {
        range = ae::clamp (doc, range);
        if (doc.audio.empty() || range.empty())
            return;
        ae::Document part;
        part.audio = ae::copy (doc.audio, range);
        part.title = doc.title;
        if (doc.hasLoop() && doc.loopStart >= range.start && doc.loopEnd <= range.end)
            ae::setLoop (part, { doc.loopStart - range.start, doc.loopEnd - range.start });
        const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Edits");
        folder.createDirectory();
        const auto file = folder.getNonexistentChildFile (safeTitle() + " (part)", ".wav");
        const auto bytes = ae::encodeWav (part, bits);
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            setStatus (tr ("Could not write {0}", { file.getFullPathName() }), true);
            return;
        }
        engine.getSampleLibrary().forget (file.getFullPathName());
        const auto path = file.getFullPathName().toStdString();
        const auto name = (safeTitle() + " (part)").toStdString();
        int index = 0;
        engine.editSong ([&] (Song& s) {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::sampler;
            c.samplePath = path;
            c.mixerTrack = s.claimFreeInsert();
            s.channels.push_back (c);
            index = (int) s.channels.size() - 1;
        });
        engine.setSelectedChannel (index);
        setStatus (tr ("The selection went to a new Sampler channel ({0})", { juce::String::fromUTF8 (name.c_str()) }), false);
    }

    void sendToNewSampler()
    {
        if (doc.audio.empty())
            return;
        const auto file = writeEditFile();
        if (file == juce::File {})
            return;
        const auto path = file.getFullPathName().toStdString();
        const auto name = safeTitle().toStdString();
        int index = 0;
        engine.editSong ([&] (Song& s) {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::sampler;
            c.samplePath = path;
            c.mixerTrack = s.claimFreeInsert();
            s.channels.push_back (c);
            index = (int) s.channels.size() - 1;
        });
        engine.setSelectedChannel (index);
        modified = false;
        setStatus (tr ("Sent to a new Sampler channel"), false);
        updateTitle();
    }

    bool sourceChannelValid() const
    {
        const auto& channels = engine.getSong().channels;
        return source.kind == Source::Kind::channel && juce::isPositiveAndBelow (source.index, (int) channels.size())
               && juce::String::fromUTF8 (channels[(std::size_t) source.index].samplePath.c_str()) == source.path;
    }

    void sendBackToChannel()
    {
        if (doc.audio.empty() || ! sourceChannelValid())
            return;
        const auto file = writeEditFile();
        if (file == juce::File {})
            return;
        const auto path = file.getFullPathName().toStdString();
        const auto ch = source.index;
        engine.editSong ([=] (Song& s) { s.channels[(std::size_t) ch].samplePath = path; });
        source.path = file.getFullPathName();
        modified = false;
        setStatus (tr ("The channel now plays the edited audio"), false);
        updateTitle();
    }

    // FL's "Send to selected channel": the Channel Rack's selected channel plays the audio, when it plays a sample
    int selectedSamplerChannel() const
    {
        const auto ch = engine.getSelectedChannel();
        const auto& channels = engine.getSong().channels;
        if (! juce::isPositiveAndBelow (ch, (int) channels.size()))
            return -1;
        const auto kind = channels[(std::size_t) ch].instrument;
        return kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip ? ch : -1;
    }

    void sendToSelectedChannel()
    {
        const auto ch = selectedSamplerChannel();
        if (doc.audio.empty() || ch < 0)
            return;
        const auto file = writeEditFile();
        if (file == juce::File {})
            return;
        const auto path = file.getFullPathName().toStdString();
        engine.editSong ([=] (Song& s) {
            if (ch < (int) s.channels.size())
                s.channels[(std::size_t) ch].samplePath = path;
        });
        setStatus (tr ("The channel now plays the edited audio"), false);
    }

    // --- menus ----------------------------------------------------------------------------------------------
    juce::PopupMenu historyMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Undo history"));
        for (int i = history.size() - 1; i >= 0; --i)
            menu.addItem (juce::String::fromUTF8 (history.label (i).c_str()), true, i == history.getPosition(), [this, i] {
                if (recState != RecState::idle)
                    return;
                stopPlayback();
                doc = history.jumpTo (i);
                modified = true;
                documentChanged();
            });
        return menu;
    }

    juce::PopupMenu curveMenu()
    {
        juce::PopupMenu menu;
        const std::pair<ae::Curve, const char*> curves[] = { { ae::Curve::smooth, TR_NOOP ("Smooth (S-curve)") },
                                                             { ae::Curve::linear, TR_NOOP ("Linear") },
                                                             { ae::Curve::exponential, TR_NOOP ("Slow start") },
                                                             { ae::Curve::logarithmic, TR_NOOP ("Fast start") } };
        menu.addSectionHeader (tr ("Fade curve"));
        for (const auto& [curve, name] : curves)
            menu.addItem (tr (name), true, fadeCurve == curve, [this, c = curve] { fadeCurve = c; });
        return menu;
    }

    juce::PopupMenu recordMenu()
    {
        juce::PopupMenu menu;
        if (slotMode()) // the plugin records its slot's input: when, how long, and what its playback does
        {
            const auto* fx = slotDef();
            const auto mode = fx != nullptr ? edisonfx::modeOf (fx->params) : edisonfx::Mode::onInput;
            const auto limit = fx != nullptr ? edisonfx::maxTimeIndexOf (fx->params) : 0;
            menu.addSectionHeader (tr ("When / what to record"));
            for (int m = 0; m < edisonfx::modeCount; ++m)
                menu.addItem (tr (edisonfx::modeName ((edisonfx::Mode) m)), true, (int) mode == m,
                              [this, m] { setSlotParam (edisonfx::recordMode, edisonfx::modeValue ((edisonfx::Mode) m)); });
            menu.addItem (tr ("Append"), true, slotParam (edisonfx::append) >= 0.5f,
                          [this] { setSlotParam (edisonfx::append, slotParam (edisonfx::append) >= 0.5f ? 0.0f : 1.0f); });
            juce::PopupMenu time;
            for (int i = 0; i < edisonfx::maxTimeCount; ++i)
                time.addItem (maxTimeName (i), true, limit == i,
                              [this, i] { setSlotParam (edisonfx::maxTime, (float) i / (float) (edisonfx::maxTimeCount - 1)); });
            menu.addSubMenu (tr ("Maximum recording time"), time);
            menu.addSectionHeader (tr ("Playback"));
            menu.addItem (tr ("Mute input") + "\tI", true, slotParam (edisonfx::muteInput) >= 0.5f,
                          [this] { setSlotParam (edisonfx::muteInput, slotParam (edisonfx::muteInput) >= 0.5f ? 0.0f : 1.0f); });
            menu.addItem (tr ("Slave playback to host") + "\tH", true, slotParam (edisonfx::slave) >= 0.5f,
                          [this] { setSlotParam (edisonfx::slave, slotParam (edisonfx::slave) >= 0.5f ? 0.0f : 1.0f); });
            menu.addItem (tr ("Ignore own selection"), true, slotParam (edisonfx::ignoreOwnSelection) >= 0.5f, [this] {
                setSlotParam (edisonfx::ignoreOwnSelection, slotParam (edisonfx::ignoreOwnSelection) >= 0.5f ? 0.0f : 1.0f);
            });
            return menu;
        }
        menu.addSectionHeader (tr ("Record from"));
        menu.addItem (tr ("Audio input"), true, recordSource == AudioCapture::input, [this] { recordSource = AudioCapture::input; });
        menu.addItem (tr ("Master output"), true, recordSource == 0, [this] { recordSource = 0; });
        juce::PopupMenu inserts;
        const auto& mixer = engine.getSong().mixer;
        for (int t = 1; t < (int) mixer.size(); ++t)
            inserts.addItem (sourceName (t), true, recordSource == t, [this, t] { recordSource = t; });
        menu.addSubMenu (tr ("Mixer insert"), inserts, inserts.getNumItems() > 0, nullptr, recordSource > 0);
        menu.addSectionHeader (tr ("Start"));
        menu.addItem (tr ("Immediately"), true, recordMode == RecordMode::now, [this] { recordMode = RecordMode::now; });
        menu.addItem (tr ("When the input is heard"), true, recordMode == RecordMode::onInput, [this] { recordMode = RecordMode::onInput; });
        menu.addItem (tr ("When the song plays (stops with it)"), true, recordMode == RecordMode::onPlay, [this] { recordMode = RecordMode::onPlay; });
        return menu;
    }

    juce::PopupMenu sendMenu()
    {
        juce::PopupMenu menu;
        const auto has = ! doc.audio.empty();
        menu.addItem (tr ("Send to the Playlist as an audio clip"), has, false, [this] { sendToPlaylist(); });
        menu.addItem (tr ("Send to the selected channel"), has && selectedSamplerChannel() >= 0, false, [this] { sendToSelectedChannel(); });
        menu.addItem (tr ("Send to a new Sampler channel"), has, false, [this] { sendToNewSampler(); });
        menu.addItem (tr ("Send the selection to a new Sampler channel"), has && ! selection.empty(), false,
                      [this] { sendRangeToNewSampler (selection); });
        if (source.kind == Source::Kind::channel)
        {
            const auto& channels = engine.getSong().channels;
            const auto name = juce::isPositiveAndBelow (source.index, (int) channels.size())
                                  ? juce::String::fromUTF8 (channels[(std::size_t) source.index].name.c_str())
                                  : juce::String();
            menu.addItem (tr ("Replace the sample of \"{0}\"", { name }), has && sourceChannelValid(), false, [this] { sendBackToChannel(); });
        }
        return menu;
    }

    // --- FL's region trigger notes (round 190ee; Edison_x64.dll RegionMenuPopup 0x86d850, DefaultNoteMenuClick
    // 0x8712e0, AssignRegionsMenuClick 0x868c30): the key a region answers when the sample is played by keys
    // (Marker::keyNum, saved in the WAV's "lxkn" chunk) ------------------------------------------------------------
    int assignWalk = -1;        // the region "Assign trigger notes to all..." is asking about; -1 = no walk
    bool assignChanged = false;

    // FL's note captions: the note and its octave with C5 = 60, and the General MIDI drum name where there is one
    static juce::String triggerKeyName (int key)
    {
        return juce::MidiMessage::getMidiNoteName (key, true, false, 0) + juce::String (key / 12);
    }

    void setTriggerNote (int index, int key)
    {
        if (! juce::isPositiveAndBelow (index, (int) doc.markers.size()))
            return;
        auto& m = doc.markers[(std::size_t) index];
        if (m.keyNum == key)
            return;
        m.keyNum = key;
        if (assignWalk >= 0)
            assignChanged = true; // (the walk is one undo step)
        else
            commit (tr ("Set trigger note for {0}", { m.name.empty() ? tr ("Region") : juce::String::fromUTF8 (m.name.c_str()) }));
        repaint();
    }

    juce::PopupMenu triggerNoteMenu (int index)
    {
        juce::PopupMenu menu;
        const auto now = doc.markers[(std::size_t) index].keyNum;
        menu.addItem (tr ("Default / none"), true, now < 0, [this, index] { setTriggerNote (index, -1); });
        const auto last = juce::jlimit (0, 127, engine.lastPlayedKey());
        menu.addItem (tr ("Last hit ({0})", { triggerKeyName (last) }), [this, index, last] { setTriggerNote (index, last); });
        if (assignWalk >= 0) // FL shows these two only during the walk
        {
            menu.addSeparator();
            menu.addItem (tr ("Cancel all"), [this] { assignWalk = -2; });
        }
        for (int key = 0; key < 128; ++key)
        {
            if (key % 12 == 0)
                menu.addColumnBreak(); // (FL: a column per octave)
            auto text = triggerKeyName (key);
            if (const auto* drum = notetools::generalMidiDrumName (key))
                text << " (" << drum << ")";
            menu.addItem (text, true, now == key, [this, index, key] { setTriggerNote (index, key); });
        }
        return menu;
    }

    // FL: every region in turn is selected and played, and its menu opens at its start until the last one or
    // "Cancel all"; the whole walk is one undo step ("Assign all regions")
    void assignTriggerNotes()
    {
        assignWalk = -1;
        assignChanged = false;
        assignStep (0);
    }

    void assignStep (int from)
    {
        auto next = -1;
        if (assignWalk != -2)
            for (int i = juce::jmax (0, from); i < (int) doc.markers.size(); ++i)
                if (doc.markers[(std::size_t) i].isRegion())
                {
                    next = i;
                    break;
                }
        if (next < 0)
        {
            assignWalk = -1;
            selection = {};
            if (playing)
                stopPlayback();
            if (assignChanged)
                commit (tr ("Assign all regions"));
            assignChanged = false;
            repaint();
            return;
        }
        assignWalk = next;
        const auto& m = doc.markers[(std::size_t) next];
        selection = { m.start, m.end };
        startPlayback();
        repaint();
        const auto x = juce::jlimit (waveArea.getX(), waveArea.getRight(), (int) frameToX ((double) m.start));
        juce::Component::SafePointer<AudioEditor> self (this);
        markerMenu (next).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                                             localAreaToGlobal (juce::Rectangle<int> (x, waveArea.getY(), 1, 1))),
                                         [self, next] (int) {
                                             juce::MessageManager::callAsync ([self, next] {
                                                 if (self != nullptr)
                                                     self->assignStep (self->assignWalk == -2 ? (int) self->doc.markers.size() : next + 1);
                                             });
                                         });
    }

    juce::PopupMenu markerMenu (int index)
    {
        juce::PopupMenu menu;
        const auto m = doc.markers[(std::size_t) index];
        menu.addSectionHeader (m.name.empty() ? (m.isRegion() ? tr ("Region") : tr ("Marker")) : juce::String::fromUTF8 (m.name.c_str()));
        menu.addItem (tr ("Rename..."), [this, index] { renameMarker (index); });
        menu.addItem (tr ("Set info..."), [this, index] { setMarkerInfo (index); });
        menu.addSubMenu (tr ("Trigger note"), triggerNoteMenu (index));
        menu.addItem (tr ("Clone as region"), [this, index] { cloneRegion (index); });
        if (m.isRegion())
        {
            menu.addItem (tr ("Select"), [this, m] {
                selection = { m.start, m.end };
                repaint();
            });
            menu.addItem (tr ("Zoom on"), [this, m] { zoomTo ({ m.start, m.end }); });
            menu.addItem (tr ("Send to a new Sampler channel"), [this, m] { sendRangeToNewSampler ({ m.start, m.end }); });
            menu.addItem (tr ("Set loop"), [this, m] {
                ae::setLoop (doc, { m.start, m.end });
                commit (tr ("Set loop"));
            });
            menu.addItem (tr ("Turn into marker"), [this, index] {
                doc.markers[(std::size_t) index].end = doc.markers[(std::size_t) index].start;
                commit (tr ("Turn into marker"));
            });
        }
        menu.addItem (tr ("Delete"), [this, index] {
            doc.markers.erase (doc.markers.begin() + index);
            commit (tr ("Delete marker"));
        });
        return menu;
    }

    void renameMarker (int index)
    {
        auto* box = new juce::AlertWindow (tr ("Rename"), {}, juce::MessageBoxIconType::NoIcon, this);
        box->addTextEditor ("name", juce::String::fromUTF8 (doc.markers[(std::size_t) index].name.c_str()));
        box->addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<AudioEditor> self (this);
        box->enterModalState (true, juce::ModalCallbackFunction::create ([self, box, index] (int result) {
                                  if (self != nullptr && result == 1 && juce::isPositiveAndBelow (index, (int) self->doc.markers.size()))
                                  {
                                      self->doc.markers[(std::size_t) index].name = box->getTextEditorContents ("name").toStdString();
                                      self->commit (tr ("Rename marker"));
                                  }
                              }),
                              true);
    }

    void setStatus (const juce::String& text, bool error)
    {
        status = text;
        statusIsError = error;
        repaint (infoArea);
    }

    void updateButtons()
    {
        undoButton.setEnabled (history.canUndo());
        redoButton.setEnabled (history.canRedo());
        const auto has = ! doc.audio.empty();
        for (auto* b : quickButtons)
            b->setEnabled (has);
        playButton.setEnabled (has);
        saveButton.setEnabled (has);
        dragButton.setEnabled (has);
    }

    void updateTitle()
    {
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        {
            auto name = tr ("Audio editor");
            if (slotMode()) // the plugin's window: its name, then where it sits (like every effect window)
            {
                const auto [t, i] = slotPlace();
                name = utf8 (edisonfx::info().name);
                if (t >= 0)
                {
                    const auto track = t == 0 ? tr ("Master") : juce::String::fromUTF8 (engine.getSong().mixer[(std::size_t) t].name.c_str());
                    name += " - " + (track.isNotEmpty() ? track : tr ("Insert {0}", { juce::String (t) })) + " (" + tr ("slot {0}", { juce::String (i + 1) }) + ")";
                }
            }
            window->setName (name + (doc.title.empty() ? juce::String() : " - " + juce::String::fromUTF8 (doc.title.c_str())) + (modified && ! slotMode() ? " *" : ""));
        }
    }

    void parentHierarchyChanged() override { updateTitle(); }

    void timerCallback() override
    {
        if (slotMode() && session != nullptr)
        {
            const auto* fx = slotDef();
            if (fx == nullptr) // the slot was deleted: its window goes with it
            {
                stopTimer();
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AudioEditor> (this)] {
                    if (safe != nullptr)
                        closeToolWindowOf (*safe);
                });
                return;
            }
            if (! std::equal (shownParams.begin(), shownParams.end(), fx->params.begin()))
                syncSlotControls();
            if (recState == RecState::idle && session->recording()) // (armed from outside: the mixer's "…and record")
            {
                beforeRecording = doc;
                recState = RecState::waiting;
                recordButton.setToggleState (true, juce::dontSendNotification);
                showSlotPhase();
            }
            else if (recState == RecState::idle && session->revision() != seenRevision) // (changed behind the window)
                adoptSession (tr ("Record"));
            session->setSlaveOffset (selection.empty() ? 0 : selection.start);
            // slaved to the song: the playback marker shows where the content is being played
            if (! playing && recState == RecState::idle)
            {
                const auto p = processor();
                const auto pos = p != nullptr && fx->params[edisonfx::slave] >= 0.5f && engine.getTransport().playing.load() ? p->playPosition() : -1.0;
                const auto shown = pos >= 0.0 ? (std::int64_t) pos : (std::int64_t) -1;
                if (shown != playhead)
                {
                    playhead = shown;
                    repaint();
                }
            }
        }
        if (recState != RecState::idle)
        {
            drainRecording();
            if (recState == RecState::recording && ++recordTicks % 6 == 0) // the view follows ~5 times a second
                showRecording();
            if (recState == RecState::idle)
                return;
            repaint();
        }
        if (playing)
        {
            const auto slotPlayer = slotMode() ? processor() : nullptr;
            const auto pos = slotMode() ? (slotPlayer != nullptr ? slotPlayer->playPosition() : -1.0) : engine.getPreviewPosition();
            if (pos < 0.0)
            {
                if (juce::Time::getMillisecondCounterHiRes() - playStartedMs > 250.0)
                    stopPlayback();
                return;
            }
            const auto before = playhead;
            playhead = playFrom < 0 ? -1 : playFrom + (std::int64_t) pos; // (a tool preview is not on the timeline)
            // page along with the playhead (Follow on, issue #12)
            if (playhead >= 0 && EditorGlobals::get().isFollowingView (EditorGlobals::FollowView::audioEditor)
                && (unitOfFrame ((double) playhead) > viewStart + (double) waveArea.getWidth() * fpp || unitOfFrame ((double) playhead) < viewStart) && fpp < maxFpp())
            {
                viewStart = unitOfFrame ((double) playhead);
                clampView();
                repaint();
            }
            else if (before != playhead)
                repaint (waveArea.withTrimmedTop (-2));
        }
    }

    // --- Edison's tools (round 165l): the tool windows' host ---------------------------------------------------
public:
    const ae::Document& toolDocument() const override { return doc; }
    ae::Range toolSelection() const override { return selection; }
    void toolApply (const juce::String& label, ae::Document d, ae::Range newSelection) override
    {
        if (recState != RecState::idle)
            return;
        stopPlayback();
        const auto frames = doc.audio.frames();
        const auto edited = ae::orAll (doc, selection);
        doc = std::move (d);
        if (ae::hasEnvelopes (doc) && doc.audio.frames() != frames)
            ae::remapEnvelopes (doc, frames, edited, edited.length() + (doc.audio.frames() - frames));
        selection = ae::clamp (doc, newSelection);
        commit (label);
    }
    void toolPreview (const ae::Audio& audio) override
    {
        stopPlayback();
        playAudio (audio, -1, false);
    }
    void toolStopPreview() override { stopPlayback(); }
    double toolBpm() const override { return juce::jmax (1.0, engine.getSong().bpm); }
    ae::NoiseProfile& toolNoiseProfile() override { return noiseProfile; }
    const ae::Audio& toolClipboard() const override { return clipboard(); }
    ae::Envelope* toolEnvelope() override
    {
        if (envMode == EnvMode::wave)
            setEnvMode (EnvMode::volume);
        return &currentEnvelope();
    }
    void toolEnvelopeChanged (bool commitIt, const juce::String& label) override
    {
        if (commitIt)
            commit (label);
        else
            repaint();
    }
    juce::Component& toolComponent() override { return *this; }

private:
    enum class EnvMode
    {
        wave,
        volume,
        panning,
        separation,
        mix,    // the mix envelope of Paste mix (envelope)
        scratch // the position envelope of Scratch (envelope)
    };
    EnvMode envMode = EnvMode::wave;
    ae::NoiseProfile noiseProfile;
    float noiseThreshold = -1.0f;
    int envDragPoint = -1;
    bool envChanged = false;
    float envBendStartY = 0.0f, envBendStartTension = 0.0f;

    ae::Envelope& currentEnvelope()
    {
        return envMode == EnvMode::panning ? doc.panEnvelope : envMode == EnvMode::separation ? doc.separationEnvelope : envMode == EnvMode::mix ? doc.mixEnvelope : envMode == EnvMode::scratch ? doc.scratchEnvelope : doc.volumeEnvelope;
    }

    static const char* envModeName (EnvMode m)
    {
        switch (m)
        {
            case EnvMode::wave: return TR_NOOP ("Edit: Wave");
            case EnvMode::volume: return TR_NOOP ("Edit: Volume");
            case EnvMode::panning: return TR_NOOP ("Edit: Panning");
            case EnvMode::separation: return TR_NOOP ("Edit: Stereo sep");
            case EnvMode::mix: return TR_NOOP ("Edit: Paste mix");
            case EnvMode::scratch: return TR_NOOP ("Edit: Scratch");
        }
        return "";
    }

    void setEnvMode (EnvMode m)
    {
        envMode = m;
        envButton.setButtonText (tr (envModeName (m)));
        envButton.setToggleState (m != EnvMode::wave, juce::dontSendNotification);
        if (m != EnvMode::wave)
            setStatus (tr ("Right-click (or click) adds a point, drag moves it, right-click or Alt+click on a point deletes it, the hollow handles bend. Tools > Mixdown amp envelopes applies it."), false);
        repaint();
    }

    juce::PopupMenu envModeMenu()
    {
        juce::PopupMenu m;
        for (auto mode : { EnvMode::wave, EnvMode::volume, EnvMode::panning, EnvMode::separation })
            m.addItem (tr (envModeName (mode)), mode == EnvMode::wave || doc.audio.numChannels() > 1 || mode == EnvMode::volume, envMode == mode, [this, mode] { setEnvMode (mode); });
        if (envMode == EnvMode::mix || envMode == EnvMode::scratch)
            m.addItem (tr (envModeName (envMode)), true, true, [] {});
        return m;
    }

    // envelope y 0..1 <-> the wave area's height
    float envY (double y) const noexcept { return (float) waveArea.getBottom() - 4.0f - (float) y * ((float) waveArea.getHeight() - 8.0f); }
    double envValue (float py) const noexcept { return juce::jlimit (0.0, 1.0, (double) (((float) waveArea.getBottom() - 4.0f - py) / ((float) waveArea.getHeight() - 8.0f))); }
    float envX (double x) const noexcept { return frameToX (x * (double) juce::jmax<std::int64_t> (1, doc.audio.frames() - 1)); }
    double envPosition (int px) const noexcept { return juce::jlimit (0.0, 1.0, (double) xToFrame (px) / (double) juce::jmax<std::int64_t> (1, doc.audio.frames() - 1)); }

    int envelopeHit (juce::Point<float> p)
    {
        auto& env = currentEnvelope();
        for (int i = 0; i < (int) env.points.size(); ++i)
            if (juce::Point<float> (envX (env.points[(std::size_t) i].x), envY (env.points[(std::size_t) i].y)).getDistanceFrom (p) < 7.0f)
                return i;
        return -1;
    }

    // the segment (its end point) whose hollow tension handle is under p, or -1
    int envelopeHandleHit (juce::Point<float> p)
    {
        auto& env = currentEnvelope();
        for (int i = 1; i < (int) env.points.size(); ++i)
        {
            const auto& a = env.points[(std::size_t) i - 1];
            const auto& b = env.points[(std::size_t) i];
            if (envX (b.x) - envX (a.x) < 14.0f)
                continue;
            const auto mx = 0.5 * (a.x + b.x);
            if (juce::Point<float> (envX (mx), envY (env.at (mx))).getDistanceFrom (p) < 6.0f)
                return i;
        }
        return -1;
    }

    // FL's point editing on the envelope: right-click (or click) on an empty spot adds a point and drags it, drag a
    // point moves it, right-click or Alt+click on a point deletes it, the hollow handle between two points bends the
    // segment (right-click it: straight). Returns true when the click was for the envelope.
    bool envelopeMouseDown (const juce::MouseEvent& e)
    {
        auto& env = currentEnvelope();
        const auto hit = envelopeHit (e.position);
        envChanged = false;
        if (env.empty())
            env = ae::Envelope::flat (0.5); // a new envelope starts flat at its neutral value (unity / centre)
        if (hit >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown()))
        {
            env.points.erase (env.points.begin() + hit);
            if (env.points.empty())
                env = {};
            commit (tr ("Delete envelope point"));
            return true;
        }
        if (const auto h = envelopeHandleHit (e.position); h > 0 && hit < 0)
        {
            if (e.mods.isPopupMenu())
            {
                env.points[(std::size_t) h].tension = 0.0f;
                commit (tr ("Edit envelope"));
                return true;
            }
            envDragPoint = h;
            envBendStartY = e.position.y;
            envBendStartTension = env.points[(std::size_t) h].tension;
            drag = Drag::envBend;
            return true;
        }
        envDragPoint = hit >= 0 ? hit : env.insert (envPosition (e.x), envValue (e.position.y));
        envChanged = hit < 0;
        drag = Drag::envPoint;
        repaint();
        return true;
    }

    void envelopeMouseDrag (const juce::MouseEvent& e)
    {
        auto& env = currentEnvelope();
        if (! juce::isPositiveAndBelow (envDragPoint, (int) env.points.size()))
            return;
        auto& p = env.points[(std::size_t) envDragPoint];
        if (drag == Drag::envBend)
        {
            const auto rising = p.y >= env.points[(std::size_t) envDragPoint - 1].y;
            p.tension = juce::jlimit (-1.0f, 1.0f, envBendStartTension + (e.position.y - envBendStartY) / 120.0f * (rising ? 1.0f : -1.0f));
        }
        else
        {
            const auto lo = envDragPoint > 0 ? env.points[(std::size_t) envDragPoint - 1].x : 0.0;
            const auto hi = envDragPoint + 1 < (int) env.points.size() ? env.points[(std::size_t) envDragPoint + 1].x : 1.0;
            p.x = juce::jlimit (lo, hi, envPosition (e.x));
            p.y = envValue (e.position.y);
            const auto y = p.y;
            setStatus (envMode == EnvMode::volume ? tr ("Volume {0} dB", { juce::String (20.0 * std::log10 (juce::jmax (1.0e-6f, ae::envelopeGain (y))), 1) })
                                                  : envMode == EnvMode::mix ? tr ("Clipboard {0}%", { juce::String (juce::roundToInt (y * 100.0)) })
                                                  : envMode == EnvMode::scratch ? tr ("Play position {0} s", { juce::String (y * (double) doc.audio.frames() / doc.audio.sampleRate, 3) })
                                                  : envMode == EnvMode::panning ? tr ("Panning {0}", { juce::String (juce::roundToInt ((y * 2.0 - 1.0) * 100.0)) })
                                                                                : tr ("Stereo separation {0}", { juce::String (juce::roundToInt ((y * 2.0 - 1.0) * 100.0)) }),
                       false);
        }
        envChanged = true;
        repaint();
    }

    void paintEnvelopes (juce::Graphics& g)
    {
        struct Lane
        {
            const ae::Envelope* env;
            EnvMode mode;
            juce::Colour colour;
        };
        const Lane lanes[] = { { &doc.volumeEnvelope, EnvMode::volume, theme::colour::meterLow },
                               { &doc.panEnvelope, EnvMode::panning, theme::colour::info },
                               { &doc.separationEnvelope, EnvMode::separation, theme::colour::warning },
                               { &doc.mixEnvelope, EnvMode::mix, theme::colour::accent },
                               { &doc.scratchEnvelope, EnvMode::scratch, theme::colour::accent } };
        for (const auto& l : lanes)
        {
            const auto active = envMode == l.mode;
            if (l.env->empty() && ! active)
                continue;
            if (active)
            {
                // the neutral line (unity / centre)
                g.setColour (l.colour.withAlpha (0.25f));
                g.drawHorizontalLine ((int) envY (0.5), (float) waveArea.getX(), (float) waveArea.getRight());
            }
            if (l.env->empty())
                continue;
            juce::Path path;
            for (int x = waveArea.getX(); x <= waveArea.getRight(); x += 2)
            {
                const auto pt = juce::Point<float> ((float) x, envY (l.env->at (envPosition (x))));
                if (x == waveArea.getX())
                    path.startNewSubPath (pt);
                else
                    path.lineTo (pt);
            }
            g.setColour (l.colour.withAlpha (active ? 0.95f : 0.4f));
            g.strokePath (path, juce::PathStrokeType (active ? 1.8f : 1.2f));
            if (active)
                for (int i = 1; i < (int) l.env->points.size(); ++i)
                {
                    const auto& a = l.env->points[(std::size_t) i - 1];
                    const auto& b = l.env->points[(std::size_t) i];
                    if (envX (b.x) - envX (a.x) < 14.0f)
                        continue;
                    const auto mx = 0.5 * (a.x + b.x);
                    g.setColour (l.colour.withAlpha (0.8f));
                    g.drawEllipse (envX (mx) - 3.0f, envY (l.env->at (mx)) - 3.0f, 6.0f, 6.0f, 1.2f);
                }
            if (active)
                for (const auto& p : l.env->points)
                {
                    const auto c = juce::Point<float> (envX (p.x), envY (p.y));
                    g.setColour (l.colour);
                    g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
                    g.setColour (theme::colour::bg0);
                    g.drawEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f, 1.0f);
                }
        }
    }

    void openEdisonTool (audiotools::Tool t)
    {
        if (doc.audio.empty() || recState != RecState::idle)
            return;
        audiotools::showTool (*this, t);
    }

    void envelopeEdit (const juce::String& label, const std::function<void (ae::Envelope&)>& fn)
    {
        if (doc.audio.empty())
            return;
        if (envMode == EnvMode::wave)
            setEnvMode (EnvMode::volume);
        auto& env = currentEnvelope();
        if (env.empty())
            env = ae::Envelope::flat (0.5);
        fn (env);
        commit (label);
    }

    void acquireThreshold()
    {
        if (doc.audio.empty())
            return;
        if (selection.empty())
        {
            setStatus (tr ("Select a part with only the background noise first"), false);
            return;
        }
        noiseThreshold = ae::acquireNoiseThreshold (doc.audio, selection);
        setStatus (tr ("Noise threshold: {0} dB", { juce::String (juce::Decibels::gainToDecibels (noiseThreshold, -120.0f), 1) }), false);
        if (slotMode()) // FL: the plugin has one noise threshold, which "On input" / "Input" recording waits for too
            setSlotParam (edisonfx::noiseThreshold, juce::jlimit (0.0f, 1.0f, noiseThreshold));
        repaint();
    }

    // the threshold to show: the one acquired here, else the slot's own
    float shownNoiseThreshold() const
    {
        return noiseThreshold >= 0.0f ? noiseThreshold : slotMode() ? slotParam (edisonfx::noiseThreshold) : -1.0f;
    }

    // --- FL's Select / Zoom / region commands of the editor (round 190cd; the same commands as Slicex's wave editor) ---
    void selectBeside (bool before)
    {
        if (selection.empty())
            return;
        selection = before ? ae::Range { 0, selection.start } : ae::Range { selection.end, doc.audio.frames() };
        cursor = selection.start;
        repaint();
    }

    void selectZoomedPart()
    {
        if (doc.audio.empty())
            return;
        const auto from = juce::jlimit<std::int64_t> (0, doc.audio.frames(), (std::int64_t) std::llround (viewStart));
        const auto to = juce::jlimit<std::int64_t> (from, doc.audio.frames(), (std::int64_t) std::llround (viewStart + (double) waveArea.getWidth() * fpp));
        selection = { from, to };
        cursor = from;
        repaint();
    }

    // the region before / after the selected one (round the ends), shown and selected
    void selectRegionStep (int step)
    {
        std::vector<int> regions;
        for (int i = 0; i < (int) doc.markers.size(); ++i)
            if (doc.markers[(std::size_t) i].isRegion())
                regions.push_back (i);
        if (regions.empty())
            return;
        const auto n = (int) regions.size();
        int at = -1;
        for (int i = 0; i < n && at < 0; ++i)
            if (const auto& m = doc.markers[(std::size_t) regions[(std::size_t) i]]; m.start == selection.start && m.end == selection.end)
                at = i;
        const auto to = step > 0 ? (at + 1) % n : (at <= 0 ? n - 1 : at - 1);
        const auto& m = doc.markers[(std::size_t) regions[(std::size_t) to]];
        selection = { m.start, m.end };
        cursor = m.start;
        if ((double) m.start < viewStart || (double) m.end > viewStart + (double) waveArea.getWidth() * fpp)
        {
            if ((double) (m.end - m.start) > (double) waveArea.getWidth() * fpp)
                zoomTo ({ m.start, m.end });
            else
            {
                viewStart = (double) m.start - ((double) waveArea.getWidth() * fpp - (double) (m.end - m.start)) * 0.5;
                clampView();
            }
        }
        repaint();
    }

    // the view at its closest zoom around one end of the selection
    void zoomOnSelectionEdge (bool left)
    {
        if (selection.empty())
            return;
        const auto frame = (double) (left ? selection.start : selection.end);
        fpp = 1.0 / 32.0;
        clampView();
        viewStart = frame - (double) waveArea.getWidth() * fpp * 0.5;
        clampView();
        repaint();
    }

    // FL names the copy "<name> #2" and keeps its start and length; the trigger note stays with the first
    void cloneRegion (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) doc.markers.size()))
            return;
        auto copy = doc.markers[(std::size_t) index];
        if (copy.name.empty())
            copy.name = (copy.isRegion() ? tr ("Region") : tr ("Marker")).toStdString();
        copy.name += " #2";
        copy.keyNum = -1;
        if (! copy.isRegion()) // (a marker cloned "as region": to the next marker, else to the end)
        {
            copy.end = doc.audio.frames();
            for (const auto& m : doc.markers)
                if (m.start > copy.start)
                    copy.end = std::min (copy.end, m.start);
        }
        doc.markers.insert (doc.markers.begin() + index + 1, copy);
        commit (tr ("Clone as region"));
    }

    void setMarkerInfo (int index)
    {
        auto* box = new juce::AlertWindow (tr ("Set info"), {}, juce::MessageBoxIconType::NoIcon, this);
        box->addTextEditor ("info", juce::String::fromUTF8 (doc.markers[(std::size_t) index].info.c_str()));
        box->addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<AudioEditor> self (this);
        box->enterModalState (true, juce::ModalCallbackFunction::create ([self, box, index] (int result) {
                                  if (self != nullptr && result == 1 && juce::isPositiveAndBelow (index, (int) self->doc.markers.size()))
                                  {
                                      self->doc.markers[(std::size_t) index].info = box->getTextEditorContents ("info").toStdString();
                                      self->commit (tr ("Set info"));
                                  }
                              }),
                              true);
    }

    // "Rename all...": one name box after the other, from marker `index` on (Cancel stops the round)
    void renameAllMarkers (int index, bool changed = false)
    {
        if (! juce::isPositiveAndBelow (index, (int) doc.markers.size()))
        {
            if (changed)
                commit (tr ("Rename all"));
            return;
        }
        const auto& m = doc.markers[(std::size_t) index];
        selection = m.isRegion() ? ae::Range { m.start, m.end } : selection;
        repaint();
        auto* box = new juce::AlertWindow (tr ("Rename") + " (" + juce::String (index + 1) + "/" + juce::String ((int) doc.markers.size()) + ")", {},
                                           juce::MessageBoxIconType::NoIcon, this);
        box->addTextEditor ("name", juce::String::fromUTF8 (m.name.c_str()));
        box->addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<AudioEditor> self (this);
        box->enterModalState (true, juce::ModalCallbackFunction::create ([self, box, index, changed] (int result) {
                                  if (self == nullptr)
                                      return;
                                  if (result != 1 || ! juce::isPositiveAndBelow (index, (int) self->doc.markers.size()))
                                  {
                                      if (changed)
                                          self->commit (tr ("Rename all"));
                                      return;
                                  }
                                  const auto name = box->getTextEditorContents ("name").toStdString();
                                  const auto differs = name != self->doc.markers[(std::size_t) index].name;
                                  self->doc.markers[(std::size_t) index].name = name;
                                  self->renameAllMarkers (index + 1, changed || differs);
                              }),
                              true);
    }

    float gateThreshold()
    {
        if (noiseThreshold < 0.0f && slotMode()) // the plugin's own threshold (FL: 0.02 until one is acquired)
            return slotParam (edisonfx::noiseThreshold);
        if (noiseThreshold >= 0.0f)
            return noiseThreshold * 1.12f; // a little over what was measured
        // not acquired: -48 dB under the peak
        return ae::peak (doc.audio, {}) * juce::Decibels::decibelsToGain (-48.0f);
    }

    void regionsTool (const juce::String& label, const std::function<int (ae::Document&)>& fn)
    {
        if (std::none_of (doc.markers.begin(), doc.markers.end(), [] (const ae::Marker& m) { return m.isRegion(); }))
        {
            setStatus (tr ("There are no regions (Regions > Auto-slice or Trim all noise & slice up make them)"), false);
            return;
        }
        stopPlayback();
        const auto n = fn (doc);
        commit (label);
        setStatus (tr ("{0} regions", { juce::String (n) }), false);
    }

    AudioEngine& engine;
    Source source;
    ae::Document doc;
    ae::History history;
    std::shared_ptr<wave::Summary> summary;
    int docVersion = 0;
    bool modified = false;
    juce::File lastFile;
    ae::WavFormat bits = ae::WavFormat::int24;

    ae::Range selection;
    std::int64_t cursor = 0, anchor = 0;
    double viewStart = 0.0, fpp = 1.0;
    Drag drag = Drag::none;
    int dragMarkerIndex = -1;

    bool spectrumView = false, logFrequency = true;
    int fftSize = 2048;
    juce::Image spectrumImage;
    juce::String spectrumKey;
    TimeFormat timeFormat = TimeFormat::automatic;
    bool snapOn = false; // FL's Snap button (off in its form); snapMode: 0 grid, 1 regions, 2 samples, 3 zero-crossing
    int snapMode = 0;
    // FL's view switches (round 190cd): regions and markers, the loop, the noise threshold mark
    bool viewRegions = true, viewLoop = true, viewNoiseMark = false;
    ae::Curve fadeCurve = ae::Curve::smooth;

    bool playing = false, loopPlay = false;
    ScriptRef lastScript;         // Edit last script
    bool scriptBusy = false;      // a script is running (its form may be open)
    bool scoreBusy = false;       // Convert to score is analysing
    std::uint64_t previewTicket = 0;
    std::int64_t playFrom = 0, playhead = -1;
    double playStartedMs = 0.0;

    RecState recState = RecState::idle;
    bool openedInput = false; // startRecording opened the device input itself (released when it stops)
    RecordMode recordMode = RecordMode::now;
    int recordSource = AudioCapture::input;
    std::vector<float> recL, recR;
    ae::Document beforeRecording;
    std::int64_t recordAt = 0;
    double recordRate = 44100.0;
    bool recordStereo = true;
    int recordTicks = 0;

    // an Edison slot (round 190cd): the slot's document and take, and the plugin's switches
    std::shared_ptr<edisonfx::Session> session;
    std::uint64_t seenRevision = 0;
    bool adopting = false; // the editor is taking the session's document (not the other way round)
    std::vector<float> shownParams;
    juce::ComboBox recModeBox, maxTimeBox;
    juce::TextButton appendButton, slaveButton, muteInputButton;

    juce::String status;
    bool statusIsError = false;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> rulerArea, waveArea, levelArea, overviewArea, infoArea;
    IconButton menuButton { "menu", Icon::panelMenu }, undoButton { "undo", Icon::undo }, redoButton { "redo", Icon::redo },
        historyButton { "history", Icon::chevronDown }, recordButton { "record", Icon::record }, recordMenuButton { "recordMenu", Icon::chevronDown },
        playButton { "play", Icon::play }, stopButton { "stop", Icon::stop }, snapButton { "snap", Icon::magnet }, fitButton { "fit", Icon::zoom },
        saveButton { "save", Icon::save }, followButton { "follow", Icon::follow };
    // FL's DragBtn (round 190ee, hint "Drag / copy sample / selection", MouseDown 0x88ea00): drag = the sample or the
    // selection as a WAV onto the Playlist / Channel Rack (the Browser's file drag); right-click = copy to the clipboard
    struct DragSourceButton final : public IconButton
    {
        using IconButton::IconButton;
        std::function<void()> onDragOut, onRightClick;
        bool started = false;
        void mouseDown (const juce::MouseEvent& e) override
        {
            started = false;
            if (e.mods.isPopupMenu())
            {
                if (onRightClick)
                    onRightClick();
                return;
            }
            IconButton::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! started && ! e.mods.isPopupMenu() && e.getDistanceFromDragStart() > 4)
            {
                started = true;
                if (onDragOut)
                    onDragOut();
            }
        }
    };
    DragSourceButton dragButton { "drag", Icon::audioFile };
    juce::TextButton loopButton, viewButton, sendButton, envButton;
    // warp markers (round 175a, M3): a lane above the waveform with the channel's markers, for a channel source
    juce::TextButton warpButton, autoWarpButton;
    bool warpMode = false;
    juce::Rectangle<int> warpLane;
    int warpDrag = -1, warpSelected = -1;   // marker index (in the channel's markers)
    double warpDragSeconds = 0.0;           // where the dragged marker goes (applied on release)
    // round 190cy: Shift + drag slides the audio under the marker; a plain drag takes it to `warpDragBeat` (the beat of
    // the pointer's place in `warpDragBefore`, the markers when the drag began)
    bool warpDragSlides = false;
    double warpDragBeat = 0.0;
    std::vector<WarpPoint> warpDragBefore;
    std::vector<double> warpAttacks;        // the file's attacks (seconds): the transient ticks, and where drags snap
    const void* warpAttacksOf = nullptr;    // the audio they were found in
    // round 190cg: the pseudo marker under the mouse (index into warpAttacks), the segment tempo box with ÷2 / ×2,
    // and the Quantize settings (grid in beats, amount 0…1; kept while the editor is open)
    int warpHover = -1;
    juce::Label warpBpmBox;
    juce::TextButton warpHalveButton, warpDoubleButton;
    double warpGridBeats = 0.25, warpAmount = 1.0;
    // round 190db: the beat-time view of a warped clip (core/WarpView.h). `axis` is valid while the view is in beats;
    // `dragAxis` is the axis when a marker drag began (its scale holds during the drag, so the pointer's beat and the
    // markers the drag does not touch stay put); `waveColumns` keep each pixel column's peaks with its stretch of the file.
    int beatTimeChoice = -1; // -1: as the workflow (beats in the Clip-launch workflow), 0 / 1: chosen in the menu
    warpview::BeatAxis axis, dragAxis;
    double slideDownBeat = 0.0; // Shift + drag in beats: the pointer's beat at the press
    std::uint64_t axisVersion = 0;
    std::vector<warpview::Columns> waveColumns;
    juce::Image spectrumWarped;
    juce::String spectrumWarpedKey;
    juce::Slider volumeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::OwnedArray<juce::TextButton> quickButtons;

    void refreshVolumeKnob()
    {
        volumeKnob.setValue (engine.getEditorVolume(), juce::dontSendNotification);
        const auto text = tr ("Playback volume: {0} (the audio is not changed; Tools > Amp... changes it)", { ui::utilityVolumeText (engine.getEditorVolume()) });
        volumeKnob.setTooltip (text);
        ui::setHint (volumeKnob, text + "  " + tr ("[LMB] drag  [Wheel] step  [2xLMB] 100%  [RMB] type a value"), true);
    }

};

juce::PopupMenu AudioEditor::editMenu (bool contextual)
{
    juce::PopupMenu menu;
    const auto has = ! doc.audio.empty();
    const auto sel = ! selection.empty();
    const auto clip = ! clipboard().empty();
    if (! contextual)
    {
        menu.addItem (tr ("Undo") + "\tCtrl+Z", history.canUndo(), false, [this] { undo(); });
        menu.addItem (tr ("Redo") + "\tCtrl+Y", history.canRedo(), false, [this] { redo(); });
        menu.addSubMenu (tr ("Undo history"), historyMenu());
        menu.addSeparator();
    }
    menu.addItem (tr ("Cut") + "\tCtrl+X", sel, false, [this] { cutSelection(); });
    menu.addItem (tr ("Copy") + "\tCtrl+C", sel, false, [this] { copySelection(); });
    menu.addItem (tr ("Paste insert") + "\tCtrl+V", clip && selection.empty(), false, [this] { paste (false); });
    menu.addItem (tr ("Paste replace"), clip && sel, false, [this] { paste (false); });
    menu.addItem (tr ("Paste mix"), clip && has, false, [this] { paste (true); });
    menu.addItem (envMode == EnvMode::mix ? tr ("Paste mix (envelope): apply") : tr ("Paste mix (envelope)"), clip && has, false, [this] { pasteMixWithEnvelope(); });
    menu.addItem (tr ("Paste mix (spectral)..."), clip && has, false, [this] {
        if (selection.empty())
            selection = { cursor, juce::jmin (doc.audio.frames(), cursor + ae::conform (clipboard(), doc.audio.numChannels(), doc.audio.sampleRate).frames()) };
        openEdisonTool (audiotools::Tool::pasteSpectral);
    });
    // Edison's "Paste stretch" / "Paste replace (drum)" (round 165l): the clipboard brought to the selection's length
    menu.addItem (tr ("Paste stretch"), clip && sel, false, [this] { pasteFitted (false); });
    menu.addItem (tr ("Paste replace (drum)"), clip && sel, false, [this] { pasteFitted (true); });
    menu.addSeparator();
    menu.addItem (tr ("Delete") + "\tDel", sel, false, [this] { deleteSelection(); });
    menu.addItem (tr ("Clear (silence)"), sel, false, [this] { edit (tr ("Silence"), [] (ae::Document& d, ae::Range r) { ae::clear (d, r); }, true); });
    menu.addItem (tr ("Trim (keep the selection)"), sel, false, [this] { trimSelection(); });
    // (right-click on the selection: issue #12)
    menu.addItem (tr ("Send the selection to a new Sampler channel"), sel, false, [this] { sendRangeToNewSampler (selection); });
    menu.addItem (tr ("Insert silence..."), has, false, [this] { showInsertSilence(); });
    menu.addItem (tr ("Delete the part after the loop"), has && doc.hasLoop(), false,
                  [this] { edit (tr ("Delete after loop"), [] (ae::Document& d, ae::Range) { ae::deleteAfterLoop (d); }); });
    menu.addSeparator();
    menu.addItem (tr ("Select all") + "\tCtrl+A", has, false, [this] {
        selection = { 0, doc.audio.frames() };
        repaint();
    });
    menu.addItem (tr ("Deselect") + "\tEsc", sel, false, [this] {
        selection = {};
        repaint();
    });
    if (contextual)
    {
        menu.addSeparator();
        menu.addItem (tr ("Add marker") + "\tM", has, false, [this] { addMarkerAtCursor(); });
        menu.addItem (tr ("Add region"), sel, false, [this] { addRegionFromSelection(); });
        menu.addItem (tr ("Set loop"), sel, false, [this] { setLoopFromSelection(); });
        menu.addItem (tr ("Zoom on the selection"), sel, false, [this] { zoomTo (selection); });
    }
    return menu;
}

juce::PopupMenu AudioEditor::mainMenu()
{
    const auto has = ! doc.audio.empty();
    juce::PopupMenu file;
    file.addItem (tr ("New (empty)"), [this] {
        stopPlayback();
        if (! slotMode())
            source = {};
        setDocument ({}, tr ("Empty"));
    });
    file.addItem (tr ("Open audio file..."), [this] { openFileDialog(); });
    juce::PopupMenu save;
    save.addItem (tr ("16-bit"), has, bits == ae::WavFormat::int16, [this] { saveAs (ae::WavFormat::int16); });
    save.addItem (tr ("24-bit"), has, bits == ae::WavFormat::int24, [this] { saveAs (ae::WavFormat::int24); });
    save.addItem (tr ("32-bit float"), has, bits == ae::WavFormat::float32, [this] { saveAs (ae::WavFormat::float32); });
    file.addSubMenu (tr ("Save as WAV"), save, has);
    file.addItem (tr ("Export regions as files..."), has && ! doc.markers.empty(), false, [this] { exportRegions(); });
    file.addSeparator();
    file.addItem (tr ("Send to the Playlist as an audio clip"), has, false, [this] { sendToPlaylist(); });
    file.addItem (tr ("Send to a new Sampler channel"), has, false, [this] { sendToNewSampler(); });

    juce::PopupMenu tools;
    using T = audiotools::Tool;
    const auto envOn = has && envMode != EnvMode::wave;
    const auto envAny = has && ae::hasEnvelopes (doc);
    tools.addSectionHeader (tr ("Envelope"));
    tools.addSubMenu (tr ("Edit"), envModeMenu(), has);
    tools.addItem (tr ("Cancel all envelopes"), envAny, false, [this] {
        doc.volumeEnvelope = doc.panEnvelope = doc.separationEnvelope = doc.mixEnvelope = doc.scratchEnvelope = {};
        if (envMode == EnvMode::mix || envMode == EnvMode::scratch)
            setEnvMode (EnvMode::wave);
        commit (tr ("Cancel all envelopes"));
    });
    tools.addItem (tr ("Add points at selection"), has && ! selection.empty(), false, [this] {
        const auto n = (double) juce::jmax<std::int64_t> (1, doc.audio.frames() - 1);
        const std::vector<double> xs { (double) selection.start / n, (double) selection.end / n };
        envelopeEdit (tr ("Add points at selection"), [xs] (ae::Envelope& e) { ae::addPointsAt (e, xs); });
    });
    tools.addItem (tr ("Add points at regions"), has && ! doc.markers.empty(), false, [this] {
        const auto n = (double) juce::jmax<std::int64_t> (1, doc.audio.frames() - 1);
        std::vector<double> xs;
        for (const auto& m : doc.markers)
        {
            xs.push_back ((double) m.start / n);
            if (m.isRegion())
                xs.push_back ((double) m.end / n);
        }
        envelopeEdit (tr ("Add points at regions"), [xs] (ae::Envelope& e) { ae::addPointsAt (e, xs); });
    });
    tools.addItem (tr ("Flip vertically"), envOn, false, [this] { envelopeEdit (tr ("Flip vertically"), [] (ae::Envelope& e) { ae::flipLevels (e); }); });
    tools.addItem (tr ("Scale levels..."), has, false, [this] { openEdisonTool (T::scaleLevels); });
    tools.addItem (tr ("Normalize levels"), envOn, false, [this] { envelopeEdit (tr ("Normalize levels"), [] (ae::Envelope& e) { ae::normalizeLevels (e); }); });
    tools.addItem (tr ("Decimate points..."), has, false, [this] { openEdisonTool (T::decimate); });
    tools.addItem (tr ("Filter..."), has, false, [this] { openEdisonTool (T::envFilter); });
    tools.addItem (tr ("Smooth up..."), has, false, [this] { openEdisonTool (T::smoothUp); });
    tools.addItem (tr ("Turn all points smooth"), envOn, false, [this] { envelopeEdit (tr ("Turn all points smooth"), [] (ae::Envelope& e) { ae::makePointsSmooth (e); }); });
    tools.addItem (tr ("Create sequence..."), has, false, [this] { openEdisonTool (T::createSequence); });

    tools.addSectionHeader (tr ("Amp"));
    tools.addItem (tr ("Mixdown amp envelopes"), envAny, false, [this] {
        stopPlayback();
        ae::mixdownEnvelopes (doc);
        setEnvMode (EnvMode::wave);
        commit (tr ("Mixdown amp envelopes"));
    });
    tools.addItem (tr ("Amp..."), has, false, [this] { showAmp(); });
    tools.addItem (tr ("Reverse polarity"), has, false, [this] { edit (tr ("Reverse polarity"), [] (ae::Document& d, ae::Range r) { ae::invert (d, r); }); });
    tools.addItem (tr ("Normalize"), has, false, [this] { edit (tr ("Normalize"), [] (ae::Document& d, ae::Range r) { ae::normalize (d, r); }); });
    tools.addItem (tr ("Normalize to..."), has, false, [this] { showNormalizeTo(); });
    tools.addItem (tr ("Lossy normalize"), has, false, [this] { edit (tr ("Lossy normalize"), [] (ae::Document& d, ae::Range r) { ae::lossyNormalize (d, r); }); });
    tools.addItem (tr ("Fade in"), has, false, [this] { fade (true); });
    tools.addItem (tr ("Fade out"), has, false, [this] { fade (false); });
    tools.addSubMenu (tr ("Fade curve"), curveMenu());
    tools.addItem (tr ("Declick in"), has, false, [this] { edit (tr ("Declick in"), [] (ae::Document& d, ae::Range r) { ae::declickIn (d, r); }); });
    tools.addItem (tr ("Declick out"), has, false, [this] { edit (tr ("Declick out"), [] (ae::Document& d, ae::Range r) { ae::declickOut (d, r); }); });
    tools.addSectionHeader (tr ("Waveform"));
    tools.addItem (tr ("Remove DC offset (center)"), has, false, [this] { edit (tr ("Remove DC offset"), [] (ae::Document& d, ae::Range r) { ae::removeDc (d, r); }); });
    tools.addSectionHeader (tr ("Time"));
    tools.addItem (tr ("Reverse"), has, false, [this] { edit (tr ("Reverse"), [] (ae::Document& d, ae::Range r) { ae::reverse (d, r); }); });
    tools.addItem (tr ("Time stretch / pitch shift..."), has, false, [this] { openEdisonTool (T::stretch); });
    tools.addItem (tr ("Drum(loop) stretch..."), has, false, [this] { openEdisonTool (T::drumStretch); });
    tools.addItem (tr ("Claw machine..."), has, false, [this] { openEdisonTool (T::claw); });
    tools.addItem (envMode == EnvMode::scratch ? tr ("Scratch (envelope): apply") : tr ("Scratch (envelope)"), has, false, [this] { scratchWithEnvelope(); });
    tools.addItem (tr ("Resample..."), has, false, [this] { showResample(); });
    tools.addSectionHeader (tr ("Channels"));
    const auto stereo = doc.audio.numChannels() > 1;
    tools.addItem (tr ("Swap channels"), has && stereo, false, [this] { edit (tr ("Swap channels"), [] (ae::Document& d, ae::Range) { ae::swapChannels (d); }); });
    tools.addItem (tr ("Convert to mono (mix)"), has && stereo, false, [this] { edit (tr ("Convert to mono"), [] (ae::Document& d, ae::Range) { ae::toMono (d, ae::MonoSource::merged); }); });
    tools.addItem (tr ("Convert the left channel to mono"), has && stereo, false, [this] { edit (tr ("Convert to mono"), [] (ae::Document& d, ae::Range) { ae::toMono (d, ae::MonoSource::left); }); });
    tools.addItem (tr ("Convert the right channel to mono"), has && stereo, false, [this] { edit (tr ("Convert to mono"), [] (ae::Document& d, ae::Range) { ae::toMono (d, ae::MonoSource::right); }); });
    tools.addItem (tr ("Convert a mono signal to mono format"), has && stereo, false, [this] {
        stopPlayback();
        if (ae::monoIfMono (doc))
            commit (tr ("Convert to mono"));
        else
            setStatus (tr ("The left and right channels differ: the audio is really stereo"), false);
    });
    tools.addItem (tr ("Convert to stereo"), has && ! stereo, false, [this] { edit (tr ("Convert to stereo"), [] (ae::Document& d, ae::Range) { ae::toStereo (d); }); });
    tools.addSectionHeader (tr ("Synthesis"));
    tools.addItem (tr ("Generate noise"), true, false, [this] {
        stopPlayback();
        if (doc.audio.channels.empty())
        {
            doc.audio = ae::silence (2, 44100.0, 0);
            history.reset (doc, tr ("Empty").toStdString());
        }
        const auto at = selection.empty() ? cursor : selection.start;
        const auto length = selection.empty() ? (std::int64_t) doc.audio.sampleRate : selection.length();
        ae::generateNoise (doc, selection, 1.0f, (std::uint32_t) juce::Random::getSystemRandom().nextInt(), length);
        selection = { at, at + length };
        commit (tr ("Generate noise"));
        if (history.size() <= 2)
            zoomToFit();
    });
    tools.addSectionHeader (tr ("Scripting"));
    tools.addSubMenu (tr ("Run script"), scriptMenu(), ! scriptBusy);
    tools.addItem (lastScript.key.isEmpty() ? tr ("Edit last script") : tr ("Edit last script: {0}", { lastScript.name }), ! lastScript.key.isEmpty(), false,
                   [this] { editLastScript(); });
    tools.addSectionHeader (tr ("Noise gating"));
    tools.addItem (tr ("Acquire noise threshold"), has && ! selection.empty(), false, [this] { acquireThreshold(); });
    tools.addItem (tr ("Gate noise"), has, false, [this] {
        const auto t = gateThreshold();
        edit (tr ("Gate noise"), [t] (ae::Document& d, ae::Range r) { ae::gateNoise (d, r, t); });
    });
    tools.addItem (tr ("Trim side noise"), has, false, [this] {
        const auto t = gateThreshold();
        edit (tr ("Trim side noise"), [t] (ae::Document& d, ae::Range r) { ae::trimSideNoise (d, r, t); });
    });
    tools.addItem (tr ("Trim all noise && slice up"), has, false, [this] {
        const auto t = gateThreshold();
        int n = 0;
        edit (tr ("Trim all noise & slice up"), [t, &n] (ae::Document& d, ae::Range r) { n = ae::trimNoiseAndSlice (d, r, t); });
        setStatus (tr ("{0} regions", { juce::String (n) }), false);
    });
    tools.addSectionHeader (tr ("Spectral"));
    tools.addItem (tr ("Convolution reverb..."), has, false, [this] { openEdisonTool (T::reverb); });
    tools.addItem (tr ("Blur..."), has, false, [this] { openEdisonTool (T::blur); });
    tools.addItem (tr ("Equalize..."), has, false, [this] { openEdisonTool (T::equalize); });
    tools.addItem (tr ("Acquire noise profile"), has && ! selection.empty(), false, [this] {
        noiseProfile = ae::acquireNoiseProfile (doc.audio, selection);
        setStatus (tr ("Noise profile acquired from the selection: Clean up (denoise) uses it"), false);
    });
    tools.addItem (tr ("Clean up (denoise)..."), has, false, [this] { openEdisonTool (T::denoise); });
    tools.addItem (tr ("Match spectrum..."), has, false, [this] { openEdisonTool (T::matchSpectrum); });
    tools.addItem (tr ("Vocal denoise/isolator..."), has, false, [this] { openEdisonTool (T::vocalDenoise); });
    tools.addItem (tr ("Deverb..."), has, false, [this] { openEdisonTool (T::deverb); });
    tools.addSectionHeader (tr ("Regions"));
    tools.addItem (tr ("Normalize all regions"), has, false, [this] { regionsTool (tr ("Normalize all regions"), [] (ae::Document& d) { return ae::normalizeRegions (d); }); });
    tools.addItem (tr ("Declick in all regions"), has, false, [this] { regionsTool (tr ("Declick in all regions"), [] (ae::Document& d) { return ae::declickRegions (d, true, false); }); });
    tools.addItem (tr ("Declick out all regions"), has, false, [this] { regionsTool (tr ("Declick out all regions"), [] (ae::Document& d) { return ae::declickRegions (d, false, true); }); });
    tools.addItem (tr ("Perfect all regions"), has, false, [this] { regionsTool (tr ("Perfect all regions"), [] (ae::Document& d) { return ae::perfectRegions (d); }); });
    tools.addItem (tr ("Tune loop..."), has, false, [this] { openEdisonTool (T::loopTuner); });
    tools.addSectionHeader (tr ("Analysis"));
    tools.addItem (tr ("Convert to score and dump to piano roll"), has && ! scoreBusy, false, [this] { convertToScore(); });
    {
        // round a2m189b: Live's Convert Melody / Harmony / Drums, into a new pattern (the selection, or the whole audio)
        juce::PopupMenu convert;
        const std::pair<const char*, a2m::Mode> modes[] = { { TR_NOOP ("Melody to new pattern"), a2m::Mode::melody },
                                                            { TR_NOOP ("Harmony to new pattern"), a2m::Mode::harmony },
                                                            { TR_NOOP ("Drums to new pattern"), a2m::Mode::drums } };
        for (const auto& [label, mode] : modes)
            convert.addItem (tr (label), [this, m = mode] { convertToMidi (m); });
        tools.addSubMenu (tr ("Convert to MIDI"), convert, has);
    }

    juce::PopupMenu regions;
    regions.addItem (tr ("Add marker") + "\tM", has, false, [this] { addMarkerAtCursor(); });
    regions.addItem (tr ("Add region"), has && ! selection.empty(), false, [this] { addRegionFromSelection(); });
    regions.addItem (tr ("Set loop"), has && ! selection.empty(), false, [this] { setLoopFromSelection(); });
    regions.addItem (tr ("Remove loop"), doc.hasLoop(), false, [this] {
        ae::setLoop (doc, {});
        commit (tr ("Remove loop"));
    });
    regions.addItem (tr ("Rename all..."), has && ! doc.markers.empty(), false, [this] { renameAllMarkers (0); });
    regions.addItem (tr ("Assign trigger notes to all..."), has && std::any_of (doc.markers.begin(), doc.markers.end(), [] (const ae::Marker& m) { return m.isRegion(); }),
                     false, [this] { assignTriggerNotes(); });
    {
        // FL's Assign all to (0x86e370): every marker in order; FL's three pad layouts are not built
        juce::PopupMenu to;
        const std::pair<const char*, ae::NoteLayout> layouts[] = { { TR_NOOP ("Whole keyboard"), ae::NoteLayout::wholeKeyboard },
                                                                   { TR_NOOP ("White notes"), ae::NoteLayout::whiteNotes },
                                                                   { TR_NOOP ("Black notes"), ae::NoteLayout::blackNotes },
                                                                   { TR_NOOP ("Default / none"), ae::NoteLayout::none },
                                                                   { "C5", ae::NoteLayout::c5 } };
        for (const auto& [label, layout] : layouts)
            to.addItem (tr (label), [this, l = layout] {
                ae::assignTriggerNotes (doc, l);
                commit (tr ("Assign all regions to notes"));
            });
        regions.addSubMenu (tr ("Assign all to"), to, has && ! doc.markers.empty());
    }
    regions.addItem (tr ("Delete markers in the selection"), has && ! doc.markers.empty(), false, [this] {
        if (ae::removeMarkers (doc, ae::orAll (doc, selection)) > 0)
            commit (tr ("Delete markers"));
    });
    regions.addSeparator();
    juce::PopupMenu slice;
    slice.addItem (tr ("Dull auto-slicing"), has, false, [this] { autoSlice (0.2f); });
    slice.addItem (tr ("Medium auto-slicing"), has, false, [this] { autoSlice (0.5f); });
    slice.addItem (tr ("Sharp auto-slicing"), has, false, [this] { autoSlice (0.85f); });
    slice.addSeparator();
    slice.addItem (tr ("Every beat (project tempo)"), has, false, [this] { gridSlice (1.0); });
    slice.addItem (tr ("Every 1/2 beat"), has, false, [this] { gridSlice (0.5); });
    slice.addItem (tr ("Every 1/4 beat"), has, false, [this] { gridSlice (0.25); });
    regions.addSubMenu (tr ("Auto-slice"), slice, has);

    juce::PopupMenu view;
    view.addItem (tr ("Waveform"), true, ! spectrumView, [this] { viewButton.setToggleState (false, juce::sendNotification); });
    view.addItem (tr ("Spectrogram") + "\tS", true, spectrumView, [this] { viewButton.setToggleState (true, juce::sendNotification); });
    juce::PopupMenu precision;
    for (int size : { 512, 1024, 2048, 4096, 8192 })
        precision.addItem (tr ("{0} bands", { juce::String (size / 2) }), true, fftSize == size, [this, size] {
            fftSize = size;
            repaint();
        });
    view.addSubMenu (tr ("Spectrum precision"), precision);
    view.addItem (tr ("Logarithmic frequency scale"), true, logFrequency, [this] {
        logFrequency = ! logFrequency;
        repaint();
    });
    juce::PopupMenu time;
    const std::pair<TimeFormat, const char*> formats[] = { { TimeFormat::automatic, TR_NOOP ("Auto") },
                                                           { TimeFormat::seconds, TR_NOOP ("Minutes : seconds") },
                                                           { TimeFormat::samples, TR_NOOP ("Samples") },
                                                           { TimeFormat::bars, TR_NOOP ("Bar : beat : tick") } };
    for (const auto& [f, name] : formats)
        time.addItem (tr (name), true, timeFormat == f, [this, f = f] {
            timeFormat = f;
            repaint();
        });
    view.addSubMenu (tr ("Time format"), time);
    view.addSeparator();
    view.addItem (tr ("Zoom in") + "\t+", has, false, [this] { zoomAround ((float) waveArea.getCentreX(), 0.5); });
    view.addItem (tr ("Zoom out") + "\t-", has, false, [this] { zoomAround ((float) waveArea.getCentreX(), 2.0); });
    view.addItem (tr ("Zoom out full") + "\tF", has, false, [this] { zoomToFit(); });
    view.addItem (tr ("Zoom on the selection"), has && ! selection.empty(), false, [this] { zoomTo (selection); });
    view.addItem (tr ("Zoom on left of selection"), has && ! selection.empty(), false, [this] { zoomOnSelectionEdge (true); });
    view.addItem (tr ("Zoom on right of selection"), has && ! selection.empty(), false, [this] { zoomOnSelectionEdge (false); });
    view.addSeparator();
    view.addItem (tr ("Regions"), true, viewRegions, [this] {
        viewRegions = ! viewRegions;
        repaint();
    });
    view.addItem (tr ("Loop"), true, viewLoop, [this] {
        viewLoop = ! viewLoop;
        repaint();
    });
    view.addItem (tr ("Noise threshold mark"), true, viewNoiseMark, [this] {
        viewNoiseMark = ! viewNoiseMark;
        if (viewNoiseMark && shownNoiseThreshold() <= 0.0f)
            setStatus (tr ("No noise threshold yet: select a part with only the background noise, then Tools > Acquire noise threshold"), false);
        repaint();
    });

    // FL's Snap menu (round 190ee): the mode, keys 1…4 (picking one turns snapping on)
    juce::PopupMenu snap;
    for (int mode = 0; mode < 4; ++mode)
        snap.addItem (snapCaption (mode) + "\t" + juce::String (mode + 1), true, snapMode == mode, [this, mode] { setSnapMode (mode); });

    // FL's Select menu (the commands Slicex's wave editor has too)
    juce::PopupMenu select;
    const auto anyRegion = std::any_of (doc.markers.begin(), doc.markers.end(), [] (const ae::Marker& m) { return m.isRegion(); });
    select.addItem (tr ("Select before current selection"), has && ! selection.empty(), false, [this] { selectBeside (true); });
    select.addItem (tr ("Select after current selection"), has && ! selection.empty(), false, [this] { selectBeside (false); });
    select.addItem (tr ("Select zoomed part"), has, false, [this] { selectZoomedPart(); });
    select.addSeparator();
    select.addItem (tr ("Select previous region"), has && anyRegion, false, [this] { selectRegionStep (-1); });
    select.addItem (tr ("Select next region"), has && anyRegion, false, [this] { selectRegionStep (1); });

    juce::PopupMenu menu;
    menu.addSubMenu (tr ("File"), file);
    menu.addSubMenu (tr ("Edit"), editMenu (false));
    menu.addSubMenu (tr ("Select"), select, has);
    menu.addSubMenu (tr ("Tools"), tools, has);
    menu.addSubMenu (tr ("Regions"), regions, has);
    menu.addSubMenu (tr ("View"), view);
    menu.addSubMenu (tr ("Snap"), snap);
    menu.addSubMenu (tr ("Record"), recordMenu());
    if (source.kind == Source::Kind::channel)
    {
        juce::PopupMenu warp;
        warp.addItem (tr ("Show warp markers"), has, warpMode, [this] { setWarpMode (! warpMode); });
        // round 190db: a warped clip drawn in beats while the markers are shown (default: the Clip-launch workflow)
        warp.addItem (tr ("Show the audio in beats"), has, beatTimeChosen(), [this] { setBeatTimeChoice (beatTimeChosen() ? 0 : 1); });
        warp.addItem (tr ("Auto-warp"), has, false, [this] { autoWarp(); });
        warp.addItem (tr ("Remove the selected warp marker"), warpMode && warpSelected > 0, false, [this] { removeWarpMarker (warpSelected); });
        warp.addItem (tr ("Remove all warp markers"), has && warpMarkers().size() > 2, false, [this] {
            editWarp ([] (SamplerSettings& s, double) { warpedit::clearMarkers (s); });
        });
        // (round 190cg) from the selected marker, else from the cursor
        const auto markers = warpMarkers();
        addWarpCommands (warp, warpSelected >= 0 && warpSelected < (int) markers.size() ? markers[(std::size_t) warpSelected].seconds
                                   : doc.audio.sampleRate > 0.0 ? (double) cursor / doc.audio.sampleRate : 0.0);
        menu.addSubMenu (tr ("Warp markers"), warp);
    }
    return menu;
}

juce::String windowKey (const Source& s)
{
    switch (s.kind)
    {
        case Source::Kind::file: return "audioeditor:" + s.path;
        case Source::Kind::channel: return "audioeditor:channel:" + juce::String (s.index);
        case Source::Kind::insert: return "audioeditor:insert:" + juce::String (s.index);
        case Source::Kind::slot: return "audioeditor:slot:" + juce::String ((juce::int64) s.instance);
        case Source::Kind::none: break;
    }
    return "audioeditor";
}

AudioEditor* openWindow (AudioEngine& engine, Source source)
{
    auto editor = std::make_unique<AudioEditor> (engine, source);
    auto* raw = editor.get();
    if (auto* window = openToolWindow (std::move (editor), tr ("Audio editor"), windowKey (source)))
    {
        window->setResizable (true, false);
        window->setResizeLimits (720, 380, 4000, 3000);
    }
    return raw;
}
} // namespace

void openAudioEditor (AudioEngine& engine)
{
    openWindow (engine, {});
}

void openAudioEditor (AudioEngine& engine, const juce::File& file)
{
    Source s { Source::Kind::file, -1, file.getFullPathName() };
    openWindow (engine, s)->loadFile (file);
}

void openAudioEditorForChannel (AudioEngine& engine, int channel, int clip)
{
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
        return;
    const auto& c = song.channels[(std::size_t) channel];
    const auto path = juce::String::fromUTF8 (c.samplePath.c_str());
    auto* editor = openWindow (engine, { Source::Kind::channel, channel, path, clip });
    if (path.isNotEmpty())
        editor->loadFile (juce::File (path));
}

std::unique_ptr<juce::Component> makeAudioEditorPanel (AudioEngine& engine, int channel, int clip, bool showWarp)
{
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
        return {};
    const auto path = juce::String::fromUTF8 (song.channels[(std::size_t) channel].samplePath.c_str());
    auto editor = std::make_unique<AudioEditor> (engine, Source { Source::Kind::channel, channel, path, clip });
    if (path.isNotEmpty())
        editor->loadFile (juce::File (path));
    if (showWarp)
        editor->setWarpModeForPanel (true);
    return editor;
}

void scaleAudioEditorTempoForSnapshot (juce::Component& panel, double factor)
{
    if (auto* editor = dynamic_cast<AudioEditor*> (&panel))
        editor->scaleWarpTempoForSnapshot (factor);
}

void holdAudioEditorWarpDragForSnapshot (juce::Component& panel, int marker, double beatOrSeconds, bool slides)
{
    if (auto* editor = dynamic_cast<AudioEditor*> (&panel))
        editor->holdWarpDragForSnapshot (marker, beatOrSeconds, slides);
}

void setAudioEditorBeatTimeForSnapshot (juce::Component& panel, int choice)
{
    if (auto* editor = dynamic_cast<AudioEditor*> (&panel))
        editor->setBeatTimeForSnapshot (choice);
}

void setAudioEditorGridClip (juce::Component& panel, int track, int scene)
{
    if (auto* editor = dynamic_cast<AudioEditor*> (&panel))
        editor->setGridClip (track, scene);
}

void openAudioEditorRecording (AudioEngine& engine)
{
    openWindow (engine, {})->recordInputWithSong();
}

void openAudioEditorForInsert (AudioEngine& engine, int insert)
{
    auto* editor = openWindow (engine, { Source::Kind::insert, insert, {} });
    editor->setRecordSource (insert);
}

namespace
{
std::optional<Source> slotSource (AudioEngine& engine, int track, int slot)
{
    const auto& mixer = engine.getSong().mixer;
    if (! juce::isPositiveAndBelow (track, (int) mixer.size()) || ! juce::isPositiveAndBelow (slot, (int) mixer[(std::size_t) track].effects.size())
        || mixer[(std::size_t) track].effects[(std::size_t) slot].kind != EffectKind::edison)
        return std::nullopt;
    Source s;
    s.kind = Source::Kind::slot;
    s.index = track;
    s.instance = mixer[(std::size_t) track].effects[(std::size_t) slot].instance.id;
    return s;
}
} // namespace

void openEdisonSlot (AudioEngine& engine, int track, int slot, bool record)
{
    const auto source = slotSource (engine, track, slot);
    if (! source)
        return;
    AudioEditor* editor = nullptr;
    if (auto* open = findToolWindow (windowKey (*source)))
    {
        open->toFront (true);
        editor = dynamic_cast<AudioEditor*> (open->getContentComponent());
    }
    else
        editor = openWindow (engine, *source);
    if (editor != nullptr && record)
        editor->armRecording();
}

std::unique_ptr<juce::Component> makeEdisonSlotEditor (AudioEngine& engine, int track, int slot)
{
    const auto source = slotSource (engine, track, slot);
    if (! source)
        return {};
    return std::make_unique<AudioEditor> (engine, *source);
}

int addEdisonSlot (AudioEngine& engine, int track, bool openAndRecord)
{
    int at = -1;
    engine.editSong ([track, openAndRecord, &at] (Song& song) {
        if (track < 0 || track >= (int) song.mixer.size() || (int) song.mixer[(std::size_t) track].effects.size() >= maxEffectSlots)
            return;
        auto slot = makeEffectSlot (EffectKind::edison);
        if (openAndRecord)
            edisonfx::setOpenAndRecord (slot.params);
        auto& effects = song.mixer[(std::size_t) track].effects;
        for (int row = 0; row < maxEffectSlots && at < 0; ++row) // FL: the first empty slot of the track
            if (mixerops::effectAtRow (effects, row) < 0)
                at = mixerops::insertEffectAtRow (song, track, slot, row);
    });
    return at;
}

juce::MessageBoxOptions removeAllEdisonInstancesQuestion()
{
    // FL's box word for word (its "?" after a space included), less FL's second line "(No undo)": LYNX's removal is
    // one undo step (removeAllEdisonInstances), so that line would be false here
    return juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::QuestionIcon)
        .withTitle (tr ("Remove Edison instances"))
        .withMessage (tr ("Are you sure you want to remove all Edison instances from the project ?"))
        .withButton (tr ("Yes"))
        .withButton (tr ("No"));
}

void removeAllEdisonInstances (AudioEngine& engine)
{
    engine.editSongStructural ([] (Song& song) { mixerops::removeEffectsOfKind (song, EffectKind::edison); });
}

void askRemoveAllEdisonInstances (AudioEngine& engine)
{
    juce::AlertWindow::showAsync (removeAllEdisonInstancesQuestion(), [&engine] (int result) {
        if (result == 1)
            removeAllEdisonInstances (engine);
    });
}

std::unique_ptr<juce::Component> makeAudioEditorForSnapshot (AudioEngine& engine, audioedit::Document doc, bool spectrum)
{
    auto editor = std::make_unique<AudioEditor> (engine, Source {});
    const auto frames = doc.audio.frames();
    editor->setDocument (std::move (doc), tr ("Open"));
    editor->selectForSnapshot ({ frames / 4, frames / 2 }, spectrum);
    return editor;
}

std::unique_ptr<juce::Component> makeAudioToolPanelForSnapshot (juce::Component& editor, int tool, int envelopeMode)
{
    auto* e = dynamic_cast<AudioEditor*> (&editor);
    if (e == nullptr || tool < 0 || tool > (int) audiotools::Tool::deverb)
        return {};
    e->setEnvModeForSnapshot (envelopeMode);
    return audiotools::makeToolPanel (*e, (audiotools::Tool) tool);
}

const char* audioToolNameForSnapshot (int tool)
{
    if (tool < 0 || tool > (int) audiotools::Tool::deverb)
        return nullptr;
    return audiotools::toolTitle ((audiotools::Tool) tool);
}

void setAudioEditorEnvelopeModeForSnapshot (juce::Component& editor, int mode)
{
    if (auto* e = dynamic_cast<AudioEditor*> (&editor))
        e->setEnvModeForSnapshot (mode);
}

static juce::StringArray menuLinesForSnapshot (const juce::PopupMenu& root)
{
    juce::StringArray lines;
    std::function<void (const juce::PopupMenu&, const juce::String&)> walk = [&] (const juce::PopupMenu& menu, const juce::String& prefix) {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.isSeparator || item.text.isEmpty())
                continue;
            const auto text = prefix + item.text.upToFirstOccurrenceOf ("\t", false, false);
            if (item.subMenu != nullptr)
                walk (*item.subMenu, text + " > ");
            else if (! item.isSectionHeader)
                lines.add (text + (item.isEnabled ? "" : "  (disabled)") + (item.isTicked ? "  [x]" : ""));
        }
    };
    walk (root, {});
    return lines;
}

// round 190ee
juce::StringArray audioEditorMarkerMenuForSnapshot (juce::Component& component, int marker)
{
    auto* editor = dynamic_cast<AudioEditor*> (&component);
    return editor != nullptr ? menuLinesForSnapshot (editor->markerMenuForSnapshot (marker)) : juce::StringArray();
}

juce::String setAudioEditorSnapForSnapshot (juce::Component& component, int mode, std::int64_t probeFrame)
{
    auto* editor = dynamic_cast<AudioEditor*> (&component);
    if (editor == nullptr)
        return {};
    editor->setSnapForSnapshot (mode);
    return editor->snapTooltipForSnapshot() + "; frame " + juce::String (probeFrame) + " snaps to " + juce::String (editor->snapFrameForSnapshot (probeFrame));
}

juce::StringArray audioEditorMenuForSnapshot (juce::Component& component)
{
    juce::StringArray lines;
    auto* editor = dynamic_cast<AudioEditor*> (&component);
    if (editor == nullptr)
        return lines;
    std::function<void (const juce::PopupMenu&, const juce::String&)> walk = [&] (const juce::PopupMenu& menu, const juce::String& prefix) {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.isSeparator || item.text.isEmpty())
                continue;
            const auto text = prefix + item.text.upToFirstOccurrenceOf ("\t", false, false);
            if (item.subMenu != nullptr)
                walk (*item.subMenu, text + " > ");
            else if (! item.isSectionHeader)
                lines.add (text + (item.isEnabled ? "" : "  (disabled)"));
        }
    };
    walk (editor->mainMenu(), {});
    return lines;
}
} // namespace lynx
