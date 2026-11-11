// Round 190w: FL 2026's playback locator in the channel settings' sample display (What's New 21138; [SHOT] flgap190w):
// the Sampler reports where its newest voice reads, as a fraction of the played region in play order.

TEST (r190w_sampler_reports_its_play_position)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    data->left.assign (1001, 0.25f);
    auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
    sampler->prepare (48000.0);
    CHECK (sampler->playPosition() < 0.0f); // silent

    sampler->noteOn (60, 1.0f);
    std::vector<float> l (2000), r (2000);
    sampler->render (l.data(), r.data(), 250, 1.0f, 1.0f);
    CHECK (std::abs (sampler->playPosition() - 0.25f) < 0.01f); // 250 of 1000 frames at the original rate
    sampler->render (l.data(), r.data(), 250, 1.0f, 1.0f);
    CHECK (std::abs (sampler->playPosition() - 0.5f) < 0.01f);
    sampler->render (l.data(), r.data(), 2000, 1.0f, 1.0f); // past its end: the voice is gone
    CHECK (sampler->playPosition() < 0.0f);

    // the start offset (a fraction of the region) is where the position starts; the reversed sample counts in play
    // order as well (the display draws it mirrored)
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.sampler.startOffset = 0.5f;
    c.sampler.reverse = true;
    sampler->configure (c, 120.0);
    sampler->noteOn (60, 1.0f);
    sampler->render (l.data(), r.data(), 100, 1.0f, 1.0f);
    CHECK (sampler->playPosition() > 0.55f && sampler->playPosition() < 0.65f);

    // engines other than the Sampler report none
    auto synth = lynx::makeInstrument (lynx::InstrumentKind::synth);
    synth->prepare (48000.0);
    synth->noteOn (60, 1.0f);
    synth->render (l.data(), r.data(), 100, 1.0f, 1.0f);
    CHECK (synth->playPosition() < 0.0f);
}
