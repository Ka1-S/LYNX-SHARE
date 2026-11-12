#include "ui/ClipAudioPanel.h"

#include <cmath>

#include "core/ClipLauncher.h"
#include "ui/AudioEditor.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class ClipAudioPanel final : public juce::Component, private juce::ChangeListener
{
public:
    ClipAudioPanel (AudioEngine& e, int trackIn, int sceneIn, int channelIn) : engine (e), track (trackIn), scene (sceneIn), channel (channelIn)
    {
        using namespace theme;
        // the stretch modes, in the order of Edit › Switch audio clips to
        const std::pair<const char*, int> modes[] = { { TR_NOOP ("Resample"), -1 }, { TR_NOOP ("General"), 0 }, { TR_NOOP ("Drums / transients"), 1 },
                                                      { TR_NOOP ("Vocal / mono"), 2 }, { TR_NOOP ("Beats"), 3 }, { TR_NOOP ("Tones"), 4 },
                                                      { TR_NOOP ("Texture"), 5 }, { TR_NOOP ("Complex"), 6 } };
        for (const auto& [name, id] : modes)
            mode.addItem (tr (name), id + 2);
        mode.setTooltip (tr ("How the audio is stretched. It belongs to the channel: every clip of this audio changes with it"));
        mode.onChange = [this] {
            const auto id = mode.getSelectedId() - 2;
            engine.editSong ([c = channel, id] (Song& s) {
                if (! juce::isPositiveAndBelow (c, (int) s.channels.size()))
                    return;
                auto& sampler = s.channels[(std::size_t) c].sampler;
                sampler.keepPitch = id >= 0;
                if (id >= 0)
                    sampler.stretchMethod = (StretchMethod) id;
            });
        };
        addAndMakeVisible (mode);

        gain.setSliderStyle (juce::Slider::LinearVertical);
        gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 18);
        gain.setRange (-70.0, 24.0, 0.1);
        gain.setDoubleClickReturnValue (true, 0.0);
        gain.setTextValueSuffix (" dB");
        gain.setTooltip (tr ("Clip gain"));
        gain.onValueChange = [this] {
            editClip ([db = gain.getValue()] (Clip& c) { c.gain = db <= -70.0 ? 0.0f : (float) std::pow (10.0, db / 20.0); });
        };
        addAndMakeVisible (gain);

        for (auto* s : { &semitones, &cents })
        {
            s->setSliderStyle (juce::Slider::IncDecButtons);
            s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
            s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 46, 20);
            s->setDoubleClickReturnValue (true, 0.0);
            s->onValueChange = [this] {
                const auto pitch = (float) juce::jlimit (-12.0, 12.0, semitones.getValue() + cents.getValue() / 100.0);
                editClip ([pitch] (Clip& c) { c.pitch = pitch; });
            };
            addAndMakeVisible (*s);
        }
        semitones.setRange (-12.0, 12.0, 1.0);
        semitones.setTextValueSuffix (" st");
        semitones.setTooltip (tr ("Clip pitch in semitones"));
        cents.setRange (-50.0, 50.0, 1.0);
        cents.setTextValueSuffix (" ct");
        cents.setTooltip (tr ("Clip pitch, fine: cents"));

        reverse.setButtonText (tr ("Reverse"));
        reverse.setClickingTogglesState (true);
        reverse.setColour (juce::TextButton::buttonOnColourId, colour::accent);
        reverse.setColour (juce::TextButton::textColourOnId, colour::onAccent);
        reverse.setTooltip (tr ("Plays the clip backwards"));
        reverse.onClick = [this] { editClip ([on = reverse.getToggleState()] (Clip& c) { c.reversed = on; }); };
        addAndMakeVisible (reverse);

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
        g.drawText (tr ("Gain"), gainLabel, juce::Justification::centred);
        g.drawText (tr ("Pitch"), pitchLabel, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto column = area.removeFromLeft (columnWidth).reduced (8, 0);
        column.removeFromTop (20);
        mode.setBounds (column.removeFromTop (22));
        column.removeFromTop (6);
        auto bottom = column.removeFromBottom (juce::jmin (26, column.getHeight() / 4));
        reverse.setBounds (bottom.reduced (0, 2));
        auto left = column.removeFromLeft (column.getWidth() / 2 - 2);
        column.removeFromLeft (4);
        gainLabel = left.removeFromTop (14);
        gain.setBounds (left);
        pitchLabel = column.removeFromTop (14);
        semitones.setBounds (column.removeFromTop (22));
        column.removeFromTop (4);
        cents.setBounds (column.removeFromTop (22));
        if (editor != nullptr)
            editor->setBounds (area);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

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
    void refresh()
    {
        const auto* c = clip();
        const auto& song = engine.getSong();
        if (c == nullptr || ! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            return;
        const juce::ScopedValueSetter<bool> guard (refreshing, true);
        const auto& sampler = song.channels[(std::size_t) channel].sampler;
        mode.setSelectedId (sampler.keepPitch ? (int) sampler.stretchMethod + 2 : 1, juce::dontSendNotification);
        gain.setValue (c->gain > 0.0f ? juce::jlimit (-70.0, 24.0, 20.0 * std::log10 ((double) c->gain)) : -70.0, juce::dontSendNotification);
        const auto whole = std::round ((double) c->pitch);
        semitones.setValue (whole, juce::dontSendNotification);
        cents.setValue (std::round (((double) c->pitch - whole) * 100.0), juce::dontSendNotification);
        reverse.setToggleState (c->reversed, juce::dontSendNotification);
    }

    static constexpr int columnWidth = 176;
    AudioEngine& engine;
    int track, scene, channel;
    juce::ComboBox mode;
    juce::Slider gain, semitones, cents;
    juce::TextButton reverse;
    juce::Rectangle<int> gainLabel, pitchLabel;
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
