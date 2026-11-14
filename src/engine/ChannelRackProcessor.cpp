#include "engine/ChannelRackProcessor.h"

namespace lynx
{
ChannelRackProcessor::ChannelRackProcessor (Transport& t, SnapshotExchange<RackState>& x, LiveEventQueue& l,
                                            LiveEventQueue& midi, MeterBank& m, WorkerPool* workers, std::function<void()> threadStart)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      transport (t),
      exchange (x),
      live (l),
      midiLive (midi),
      meters (m)
{
    renderer.setWorkerPool (workers);
    ahead = std::make_unique<RenderAhead> (renderer);
    ahead->setWorkerPool (workers, std::move (threadStart));
}

void ChannelRackProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    ahead->prepare (sampleRate, maximumExpectedSamplesPerBlock); // (render-ahead stops first: nothing is rendering now)
    renderer.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    scratch.setSize (2, maximumExpectedSamplesPerBlock);
    if (metronome != nullptr)
        metronome->prepare (sampleRate, maximumExpectedSamplesPerBlock);
}

void ChannelRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto numSamples = buffer.getNumSamples();
    const auto playing = transport.playing.load (std::memory_order_relaxed);
    const auto startTick = transport.positionTicks.load (std::memory_order_relaxed);
    const auto external = transport.externalBpm.load (std::memory_order_relaxed);
    renderer.setTempoOverride (external);

    // the metronome's click for this block, into its mixer track (FL: never in an export, so neither in an offline
    // render nor in a real-time bounce)
    const float* clickLeft = nullptr;
    const float* clickRight = nullptr;
    if (metronome != nullptr && numSamples <= metronome->capacity() && metronomeAllowed.load (std::memory_order_relaxed)
        && meters.bounceTap.getSource() == AudioCapture::off
        && metronome->render (metronome->bufferLeft(), metronome->bufferRight(), numSamples))
    {
        clickLeft = metronome->bufferLeft();
        clickRight = metronome->bufferRight();
    }
    renderer.setMetronomeInput (clickLeft, clickRight, transport.metronomeTrack.load (std::memory_order_relaxed));
    renderer.setTransportOptions (transport.playTruncatedOnTransport.load (std::memory_order_relaxed),
                                  transport.resetPluginsOnTransport.load (std::memory_order_relaxed),
                                  transport.userJumps.load (std::memory_order_relaxed));

    // render-ahead (round 190gq): what this block is, and — inside the device callback only — how the transport moves on
    renderahead::Block block;
    block.playing = playing;
    block.startTick = startTick;
    block.samples = numSamples;
    block.tps = transport.ticksPerSample();
    block.tempoOverride = external;
    block.playTruncated = transport.playTruncatedOnTransport.load (std::memory_order_relaxed);
    block.resetPlugins = transport.resetPluginsOnTransport.load (std::memory_order_relaxed);
    block.userJumps = transport.userJumps.load (std::memory_order_relaxed);
    block.metronomeTrack = transport.metronomeTrack.load (std::memory_order_relaxed);
    block.captureTrack = meters.capture.getSource();
    block.metering = metering.load (std::memory_order_relaxed);
    if (transport.inDeviceCallback.load (std::memory_order_relaxed))
    {
        transport.readMarkers (markers, markerVersion);
        model.sampleRate = transport.sampleRate.load (std::memory_order_relaxed);
        model.deviceBlock = transport.callbackSamples.load (std::memory_order_relaxed);
        model.offset = transport.callbackOffset.load (std::memory_order_relaxed);
        model.songMode = transport.songMode.load (std::memory_order_relaxed);
        model.loopStart = transport.loopStart.load (std::memory_order_relaxed);
        model.loopEnd = transport.loopEnd.load (std::memory_order_relaxed);
        model.songEnd = transport.songEnd.load (std::memory_order_relaxed);
        model.songStart = transport.songStart.load (std::memory_order_relaxed);
        model.linear = liveInput != nullptr && liveInput->linearSong.load (std::memory_order_relaxed);
        model.throughRegion = liveInput != nullptr && liveInput->noLoopRegion.load (std::memory_order_relaxed);
        model.markers = &markers;
        model.pendingAt = transport.pendingJumpAt.load (std::memory_order_relaxed);
        model.predictable = external <= 0.0 && transport.countIn.load (std::memory_order_relaxed) <= 0.0;
        block.model = &model;
    }

    auto* meterBank = metering.load (std::memory_order_relaxed) ? &meters : nullptr;
    if (buffer.getNumChannels() >= 2)
    {
        ahead->process (exchange, block, buffer.getWritePointer (0), buffer.getWritePointer (1), &live, meterBank, &midiLive);
    }
    else if (numSamples <= scratch.getNumSamples())
    {
        ahead->process (exchange, block, scratch.getWritePointer (0), scratch.getWritePointer (1), &live, meterBank, &midiLive);

        if (buffer.getNumChannels() == 1)
        {
            buffer.copyFrom (0, 0, scratch, 0, 0, numSamples);
            buffer.addFrom (0, 0, scratch, 1, 0, numSamples);
            buffer.applyGain (0.5f);
        }
    }
    else
    {
        buffer.clear();
    }
    // (the transport moves on with the tempo both parts rendered with while render-ahead runs; 0 = its own)
    transport.pinnedBpm.store (ahead->pinnedBpm(), std::memory_order_relaxed);

    // tempo automation: the transport advances at the tempo the renderer used
    const auto* state = ahead->currentState();
    if (state != nullptr && state->tempoMap && transport.externalBpm.load (std::memory_order_relaxed) <= 0.0)
        transport.bpm.store (renderer.currentBpm(), std::memory_order_relaxed);
}
} // namespace lynx
