#include "ui/ClipAudioPanel.h"

#include <cmath>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/ClipLauncher.h"
#include "core/ClipSample.h"
#include "core/WarpModes.h"
#include "ui/AudioEditor.h"
#include "ui/I18n.h"
#include "ui/NumBox.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// The warp modes in Live's order with Live's names, each on the LYNX engine that does that job (round 190dr-2):
// Beats, Tones, Texture and Complex are LYNX's own engines of those names, Re-Pitch is LYNX's resampling (speed and pitch
// together). Live's Complex Pro has no engine of its own in LYNX and is left out. The FL modes a channel may carry
// (General, Drums / transients, Vocal / mono) follow below a line, so its mode always shows.
struct ModeItem
{
    const char* name;
    int method; // StretchMethod, -1 = resample
};
const ModeItem liveModes[] = { { TR_NOOP ("Beats"), (int) StretchMethod::beats }, { TR_NOOP ("Tones"), (int) StretchMethod::tones },
                               { TR_NOOP ("Texture"), (int) StretchMethod::texture }, { TR_NOOP ("Re-Pitch"), -1 },
                               { TR_NOOP ("Complex"), (int) StretchMethod::complex } };
const ModeItem flModes[] = { { TR_NOOP ("General"), (int) StretchMethod::general }, { TR_NOOP ("Drums / transients"), (int) StretchMethod::transients },
                             { TR_NOOP ("Vocal / mono"), (int) StretchMethod::mono } };

class ClipAudioPanel final : public juce::Component, private juce::ChangeListener
{
public:
    ClipAudioPanel (AudioEngine& e, int trackIn, int sceneIn, int channelIn) : engine (e), track (trackIn), scene (sceneIn), channel (channelIn)
    {
        using namespace theme;
        readFileInfo();

        // Warp | (Follow: left out, its meaning is not known yet)
        warp.setButtonText (tr ("Warp"));
        warp.setClickingTogglesState (true);
        warp.setTooltip (tr ("Warp: the sample follows the song's tempo. Off: it plays at its own speed. It belongs to the channel: every clip of this audio changes with it"));
        warp.onClick = [this] {
            if (refreshing || ! (fileSeconds > 0.0))
                return;
            engine.editSong ([c = channel, on = warp.getToggleState(), seconds = fileSeconds] (Song& s) { clipsample::setWarp (s, c, on, seconds); });
        };

        // the warp mode: Live's names first
        for (const auto& m : liveModes)
            mode.addItem (tr (m.name), m.method + 2);
        mode.addSeparator();
        for (const auto& m : flModes)
            mode.addItem (tr (m.name), m.method + 2);
        mode.setTooltip (tr ("Warp mode: how the audio is stretched. Re-Pitch changes speed and pitch together. Only this clip changes: when other clips play the same audio, it gets its own copy"));
        mode.onChange = [this] {
            if (refreshing)
                return;
            const auto id = mode.getSelectedId() - 2;
            editMode ([id] (Song& s, int c) { warpmodes::setMode (s, c, id); });
        };

        // the mode's own settings (round 190fm; Live's Clip View shows them under the mode): Beats › Preserve, Transient
        // Loop Mode and Transient Envelope; Tones / Texture › Grain Size
        for (int k = 0; k < (int) warpmodes::preserveNames().size(); ++k)
            preserve.addItem (tr (warpmodes::preserveNames()[(std::size_t) k]), k + 1);
        preserve.setTooltip (tr ("Preserve: where Beats cuts the audio: at its transients, or on a note grid"));
        preserve.onChange = [this] {
            if (! refreshing)
                editMode ([v = preserve.getSelectedId() - 1] (Song& s, int c) { warpmodes::setPreserve (s, c, v); });
        };
        for (int k = 0; k < (int) warpmodes::loopNames().size(); ++k)
            loopMode.addItem (tr (warpmodes::loopNames()[(std::size_t) k]), k + 1);
        loopMode.setTooltip (tr ("Transient Loop Mode: what fills the gap after each piece when the audio is slowed down"));
        loopMode.onChange = [this] {
            if (! refreshing)
                editMode ([v = loopMode.getSelectedId() - 1] (Song& s, int c) { warpmodes::setLoopMode (s, c, v); });
        };
        {
            ui::ValueSpec spec;
            spec.name = tr ("Transient Envelope: a fade over each piece (100: no fade)");
            spec.minimum = 0.0;
            spec.maximum = 100.0;
            spec.interval = 1.0;
            spec.pixelsForRange = 200.0;
            spec.defaultValue = 100.0;
            spec.toText = [] (double v) { return juce::String (juce::roundToInt (v)); };
            envelope = std::make_unique<ui::NumBox> (spec, [this] { return (double) sampler().warpModes.beatsEnvelope; }, [this] (double v) {
                editMode ([v] (Song& s, int c) { warpmodes::setEnvelope (s, c, v); });
            });
        }
        // Grain Size: one box per mode, each in Live's range with Live's default (Tones 12…100, 30; Texture 2…263, 65)
        for (auto texture : { false, true })
        {
            ui::ValueSpec spec;
            spec.name = tr ("Grain Size: the length of the pieces the audio is stretched in");
            spec.minimum = texture ? WarpModeSettings::textureGrainMin : WarpModeSettings::tonesGrainMin;
            spec.maximum = texture ? WarpModeSettings::textureGrainMax : WarpModeSettings::tonesGrainMax;
            spec.interval = 1.0;
            spec.pixelsForRange = texture ? 400.0 : 200.0;
            spec.defaultValue = texture ? WarpModeSettings::defaultTextureGrain : WarpModeSettings::defaultTonesGrain;
            spec.toText = [] (double v) { return juce::String (juce::roundToInt (v)); };
            (texture ? grainTexture : grainTones) = std::make_unique<ui::NumBox> (spec, [this, texture] {
                const auto& w = sampler().warpModes;
                return (double) (texture ? w.textureGrain : w.tonesGrain);
            }, [this] (double v) { editMode ([v] (Song& s, int c) { warpmodes::setGrain (s, c, v); }); });
        }

        // BPM with ÷2 and ×2 (the audio editor's commands, round 190cy, on this clip)
        {
            ui::ValueSpec spec;
            spec.name = tr ("BPM: the tempo the sample is read at");
            spec.minimum = 20.0;
            spec.maximum = 999.0;
            spec.interval = 0.01;
            spec.pixelsForRange = 979.0 * 4.0;
            spec.defaultValue = engine.getSong().bpm;
            spec.toText = [] (double v) { return juce::String (v, 2); };
            bpm = std::make_unique<ui::NumBox> (spec, [this] { return clipsample::fileBpm (sampler(), fileSeconds); }, [this] (double v) {
                engine.editSong ([c = channel, t = track, sc = scene, seconds = fileSeconds, v] (Song& s) { clipsample::setFileBpm (s, c, t, sc, seconds, v); });
            });
        }
        halve.setButtonText (juce::String::fromUTF8 ("\xc3\xb7" "2"));
        twice.setButtonText (juce::String::fromUTF8 ("\xc3\x97" "2"));
        halve.setTooltip (tr ("Halves the tempo the sample is read at: the clip plays it twice as fast"));
        twice.setTooltip (tr ("Doubles the tempo the sample is read at: the clip plays it half as fast"));
        halve.onClick = [this] { scaleTempo (0.5); };
        twice.onClick = [this] { scaleTempo (2.0); };

        gain.setSliderStyle (juce::Slider::LinearVertical);
        gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
        gain.setRange (-70.0, 24.0, 0.1);
        gain.setDoubleClickReturnValue (true, 0.0);
        gain.setTextValueSuffix (" dB");
        gain.setTooltip (tr ("Clip gain"));
        gain.onValueChange = [this] {
            editClip ([db = gain.getValue()] (Clip& c) { c.gain = db <= -70.0 ? 0.0f : (float) std::pow (10.0, db / 20.0); });
        };

        // Pitch: the knob (semitones) and the semitone and cent boxes under it
        pitchKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pitchKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pitchKnob.setRange (-12.0, 12.0, 1.0);
        pitchKnob.setDoubleClickReturnValue (true, 0.0);
        pitchKnob.setTooltip (tr ("Clip pitch in semitones"));
        pitchKnob.onValueChange = [this] {
            if (refreshing)
                return;
            semitones.setValue (pitchKnob.getValue(), juce::sendNotificationSync);
        };
        for (auto* s : { &semitones, &cents })
        {
            s->setSliderStyle (juce::Slider::LinearBar); // (a number box dragged sideways, as Live's st / ct boxes)
            s->setDoubleClickReturnValue (true, 0.0);
            s->setColour (juce::Slider::trackColourId, colour::bg4);
            s->onValueChange = [this] {
                const auto pitch = (float) juce::jlimit (-12.0, 12.0, semitones.getValue() + cents.getValue() / 100.0);
                editClip ([pitch] (Clip& c) { c.pitch = pitch; });
            };
        }
        semitones.setRange (-12.0, 12.0, 1.0);
        semitones.setTooltip (tr ("Clip pitch in semitones"));
        cents.setRange (-50.0, 50.0, 1.0);
        cents.setTooltip (tr ("Clip pitch, fine: cents"));

        reverse.setButtonText (tr ("Reverse"));
        reverse.setClickingTogglesState (true);
        reverse.setTooltip (tr ("Plays the clip backwards"));
        reverse.onClick = [this] { editClip ([on = reverse.getToggleState()] (Clip& c) { c.reversed = on; }); };
        edit.setButtonText (tr ("Edit"));
        edit.setTooltip (tr ("Opens the sample in the audio editor window"));
        edit.onClick = [this] { openAudioEditorForChannel (engine, channel); };
        fade.setButtonText (tr ("Fade"));
        fade.setClickingTogglesState (true);
        fade.setTooltip (tr ("Fade: short fades at the clip's start and end, so its edges don't click"));
        fade.onClick = [this] {
            editClip ([on = fade.getToggleState(), tempo = engine.getSong().bpm] (Clip& c) { clipsample::setEdgeFades (c, on, tempo); });
        };

        for (auto* b : { &warp, &reverse, &fade })
        {
            b->setColour (juce::TextButton::buttonOnColourId, colour::accent);
            b->setColour (juce::TextButton::textColourOnId, colour::onAccent);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &warp, &mode, bpm.get(), &halve, &twice, &gain, &pitchKnob, &semitones, &cents, &reverse, &edit, &fade })
            addAndMakeVisible (*c);
        for (auto* c : std::initializer_list<juce::Component*> { &preserve, &loopMode, envelope.get(), grainTones.get(), grainTexture.get() })
            addChildComponent (*c);

        editor = makeAudioEditorPanel (engine, channel, -1, true);
        if (editor != nullptr)
        {
            setAudioEditorGridClip (*editor, track, scene); // (round 190cy: ÷2 / ×2 halve / double this clip too)
            addAndMakeVisible (*editor);
        }
        engine.addChangeListener (this);
        refresh();
    }
    ~ClipAudioPanel() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto column = getLocalBounds().removeFromLeft (columnWidth);
        g.setColour (colour::bg1);
        g.fillRect (column);
        g.setColour (colour::border);
        g.fillRect (column.getRight() - 1, 0, 1, getHeight());
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption, true));
        g.drawText (tr ("Audio"), column.withHeight (18).reduced (8, 0), juce::Justification::centredLeft);
        g.setFont (font (type::caption));
        g.drawText (tr ("BPM"), bpmLabel, juce::Justification::centredLeft);
        if (grainTones->isVisible() || grainTexture->isVisible())
            g.drawText (tr ("Grain Size"), grainLabel, juce::Justification::centredLeft);
        if (envelope->isVisible())
            g.drawText (tr ("Envelope"), envelopeLabel, juce::Justification::centredLeft);
        g.drawText (tr ("Gain"), gainLabel, juce::Justification::centred);
        g.drawText (tr ("Pitch"), pitchLabel, juce::Justification::centred);
        g.drawText ("st", stLabel, juce::Justification::centred);
        g.drawText ("ct", ctLabel, juce::Justification::centred);
        // the sample's title over its editor, as Live's "loop16.wav 48.0 kHz · 16-Bit · 2 Ch"
        if (! titleBar.isEmpty())
        {
            g.setColour (colour::bg2);
            g.fillRect (titleBar);
            g.setColour (colour::border);
            g.fillRect (titleBar.getX(), titleBar.getBottom() - 1, titleBar.getWidth(), 1);
            const auto icon = titleBar.withWidth (22).toFloat().withSizeKeepingCentre (14.0f, 10.0f);
            g.setColour (colour::textSecondary);
            g.drawRect (icon, 1.0f);
            for (int k = 0; k < 4; ++k) // (a small waveform in the mark)
            {
                const auto x = icon.getX() + 3.0f + (float) k * 2.6f, h = k % 2 == 0 ? 5.0f : 2.5f;
                g.fillRect (x, icon.getCentreY() - h * 0.5f, 1.2f, h);
            }
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small));
            g.drawText (titleText, titleBar.withTrimmedLeft (26).withTrimmedRight (6), juce::Justification::centredLeft, true);
        }
    }

    // Live's Audio section [SHOT live-clipview-audio-panel.png]: on the left Warp, the mode, BPM ÷2 ×2, then Reverse | Edit
    // and Fade at the bottom; on the right the Gain fader with its dB read-out, then Pitch: the knob, st and ct boxes
    void resized() override
    {
        auto area = getLocalBounds();
        auto column = area.removeFromLeft (columnWidth).reduced (8, 0);
        column.removeFromTop (20);
        column.removeFromBottom (4);
        auto right = column.removeFromRight (86);
        column.removeFromRight (8);
        auto left = column;
        warp.setBounds (left.removeFromTop (20).withWidth (left.getWidth() / 2));
        left.removeFromTop (4);
        mode.setBounds (left.removeFromTop (22));
        // the mode's own settings under it (Beats: Preserve, then the loop mode and the envelope; Tones / Texture: Grain Size)
        {
            left.removeFromTop (4);
            auto row1 = left.removeFromTop (20);
            preserve.setBounds (row1);
            grainLabel = row1.removeFromLeft (row1.getWidth() / 2);
            grainTones->setBounds (row1);
            grainTexture->setBounds (row1);
            left.removeFromTop (4);
            loopMode.setBounds (left.removeFromTop (20));
            left.removeFromTop (4);
            auto row3 = left.removeFromTop (20); // (Envelope on its own row, like Grain Size: its label fits)
            envelopeLabel = row3.removeFromLeft (row3.getWidth() / 2);
            envelope->setBounds (row3);
        }
        fade.setBounds (left.removeFromBottom (18).withWidth (left.getWidth() / 2 - 2));
        left.removeFromBottom (4);
        {
            auto row = left.removeFromBottom (20);
            reverse.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
            row.removeFromLeft (4);
            edit.setBounds (row);
        }
        left.removeFromBottom (8);
        {
            auto row = left.removeFromBottom (20);
            bpm->setBounds (row.removeFromLeft (row.getWidth() - 56));
            row.removeFromLeft (4);
            halve.setBounds (row.removeFromLeft (24));
            row.removeFromLeft (2);
            twice.setBounds (row);
            bpmLabel = left.removeFromBottom (14);
        }

        gainLabel = right.removeFromTop (14);
        auto pitchArea = right.removeFromBottom (juce::jmin (88, right.getHeight() / 2 + 10));
        gain.setBounds (right);
        pitchLabel = pitchArea.removeFromTop (14);
        pitchKnob.setBounds (pitchArea.removeFromTop (30).withSizeKeepingCentre (30, 30));
        auto boxes = pitchArea;
        auto labels = boxes.removeFromBottom (12);
        stLabel = labels.removeFromLeft (labels.getWidth() / 2);
        ctLabel = labels;
        semitones.setBounds (boxes.removeFromLeft (boxes.getWidth() / 2 - 2));
        boxes.removeFromLeft (4);
        cents.setBounds (boxes);

        titleBar = area.removeFromTop (titleText.isEmpty() ? 0 : 20);
        if (editor != nullptr)
            editor->setBounds (area);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    const SamplerSettings& sampler() const
    {
        static const SamplerSettings none;
        const auto& song = engine.getSong();
        return juce::isPositiveAndBelow (channel, (int) song.channels.size()) ? song.channels[(std::size_t) channel].sampler : none;
    }
    const Clip* clip() const
    {
        const auto& song = engine.getSong();
        return song.clipGrid ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
    }
    void editClip (const std::function<void (Clip&)>& fn)
    {
        if (refreshing)
            return;
        engine.editSong ([this, &fn] (Song& s) {
            if (s.clipGrid)
                if (auto* c = const_cast<Clip*> (launcher::clipAt (*s.clipGrid, track, scene)); c != nullptr && c->kind == ClipKind::audio)
                    fn (*c);
        });
    }
    // An edit of the warp mode or its settings: on this clip's own channel (O16: a copy when other clips share it, so only
    // this clip changes); the audio editor follows the new channel.
    void editMode (std::function<void (Song&, int)> fn)
    {
        if (refreshing)
            return;
        auto target = channel;
        engine.editSong ([this, &fn, &target] (Song& s) {
            target = warpmodes::ownChannelForGridClip (s, track, scene);
            if (target >= 0)
                fn (s, target);
        });
        if (target >= 0 && target != channel)
        {
            channel = target;
            editor = makeAudioEditorPanel (engine, channel, -1, true);
            if (editor != nullptr)
            {
                setAudioEditorGridClip (*editor, track, scene);
                addAndMakeVisible (*editor);
            }
            resized();
            refresh();
        }
    }
    void scaleTempo (double factor)
    {
        engine.editSong ([c = channel, t = track, sc = scene, seconds = fileSeconds, factor] (Song& s) { clipsample::scaleWarpedTempo (s, c, t, sc, seconds, factor); });
    }
    void refresh()
    {
        const auto* c = clip();
        const auto& song = engine.getSong();
        if (c == nullptr || ! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            return;
        const juce::ScopedValueSetter<bool> guard (refreshing, true);
        const auto& s = sampler();
        const auto n = juce::dontSendNotification;
        mode.setSelectedId (s.keepPitch ? (int) s.stretchMethod + 2 : 1, n);
        const auto warped = clipsample::isWarped (s);
        warp.setToggleState (warped, n);
        warp.setEnabled (fileSeconds > 0.0);
        for (auto* w : std::initializer_list<juce::Component*> { bpm.get(), &halve, &twice })
            w->setEnabled (warped && fileSeconds > 0.0);
        bpm->repaint();
        gain.setValue (c->gain > 0.0f ? juce::jlimit (-70.0, 24.0, 20.0 * std::log10 ((double) c->gain)) : -70.0, n);
        const auto whole = std::round ((double) c->pitch);
        semitones.setValue (whole, n);
        pitchKnob.setValue (whole, n);
        cents.setValue (std::round (((double) c->pitch - whole) * 100.0), n);
        reverse.setToggleState (c->reversed, n);
        // the mode's own settings: shown for the mode they belong to, only while the sample is warped and keeps its pitch
        const auto beats = warped && s.keepPitch && s.stretchMethod == StretchMethod::beats;
        const auto tones = warped && s.keepPitch && s.stretchMethod == StretchMethod::tones;
        const auto texture = warped && s.keepPitch && s.stretchMethod == StretchMethod::texture;
        preserve.setVisible (beats);
        loopMode.setVisible (beats);
        envelope->setVisible (beats);
        grainTones->setVisible (tones);
        grainTexture->setVisible (texture);
        preserve.setSelectedId ((int) s.warpModes.beatsPreserve + 1, n);
        loopMode.setSelectedId ((int) s.warpModes.beatsLoop + 1, n);
        envelope->repaint();
        grainTones->repaint();
        grainTexture->repaint();
        repaint();
        fade.setToggleState (clipsample::hasEdgeFades (*c), n);
    }

    // The file's rate, depth, channels and length, read from its header once (the panel is made again for another clip).
    void readFileInfo()
    {
        const auto& song = engine.getSong();
        if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            return;
        const juce::File file (juce::String::fromUTF8 (song.channels[(std::size_t) channel].samplePath.c_str()));
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        double rate = 0.0;
        int bits = 0, channels = 0;
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr && reader->sampleRate > 0.0)
        {
            rate = reader->sampleRate;
            bits = (int) reader->bitsPerSample;
            channels = (int) reader->numChannels;
            fileSeconds = (double) reader->lengthInSamples / reader->sampleRate;
        }
        else if (const auto data = engine.getSampleLibrary().findLoaded (file.getFullPathName()); data != nullptr && data->sampleRate > 0.0)
        {
            rate = data->sampleRate;
            channels = data->right.empty() && data->mappedRight == nullptr ? 1 : 2;
            fileSeconds = (double) (data->isMapped() ? data->mappedFrames : data->left.size()) / data->sampleRate;
        }
        if (file.getFileName().isNotEmpty())
            titleText = juce::String::fromUTF8 (clipsample::infoText (file.getFileName().toStdString(), rate, bits, channels).c_str());
    }

    static constexpr int columnWidth = 236;
    AudioEngine& engine;
    int track, scene, channel;
    juce::TextButton warp, halve, twice, reverse, edit, fade;
    juce::ComboBox mode, preserve, loopMode;
    std::unique_ptr<ui::NumBox> bpm, envelope, grainTones, grainTexture;
    juce::Slider gain, pitchKnob, semitones, cents;
    juce::Rectangle<int> grainLabel, envelopeLabel, bpmLabel, gainLabel, pitchLabel, stLabel, ctLabel, titleBar;
    juce::String titleText;
    double fileSeconds = 0.0;
    std::unique_ptr<juce::Component> editor;
    bool refreshing = false;
};
} // namespace

std::unique_ptr<juce::Component> makeClipAudioPanel (AudioEngine& engine, int track, int scene)
{
    const auto& song = engine.getSong();
    const auto* clip = song.clipGrid ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
    if (clip == nullptr || clip->kind != ClipKind::audio || ! juce::isPositiveAndBelow (clip->channel, (int) song.channels.size()))
        return {};
    return std::make_unique<ClipAudioPanel> (engine, track, scene, clip->channel);
}
} // namespace lynx
