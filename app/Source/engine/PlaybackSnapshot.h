#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ClipRenderer.h"

namespace djmix
{

// Everything the audio thread needs to play the arrangement on Master, built
// on the message thread after each edit (design spec, "Project state
// hand-off"). Never changed once published: the next edit builds a new one.
// Positions are timeline samples at the snapshot's tempo and sample rate,
// counted from the start of the arrangement.
struct PlaybackSnapshot
{
    struct Voice
    {
        const RenderedClip* render = nullptr; // kept alive by `renders`
        int layer = 0;          // index into layerGains
        float gain = 1.0f;      // the clip's own gain
        int64_t start = 0;      // where the clip starts; it fades in over the few ms before
        int64_t end = 0;        // where it ends; it fades out over the few ms before
        double frameAtStart = 0.0; // the render frame heard at `start`
        double step = 1.0;      // render frames per sample: 1, unless the render is for another tempo
    };

    double tempo = 128.0;
    double sampleRate = 44100.0;
    std::vector<Voice> voices;
    std::vector<float> layerGains; // 0 for a layer that is not heard
    std::vector<std::shared_ptr<const RenderedClip>> renders;
    uint64_t serial = 0; // set by MasterPlayer::publish

    double samplesPerBeat() const { return sampleRate * 60.0 / tempo; }
};

} // namespace djmix
