#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "CuePlayer.h"

namespace djmix
{

// How a clip is rendered: at project tempo, transposed, at the output rate.
struct RenderSettings
{
    double tempo = 128.0;
    int transposeSemitones = 0;
    double sampleRate = 44100.0;

    bool operator== (const RenderSettings&) const = default;
};

// A stretch of a track, time-stretched so its beats fall on the project's
// beat grid (and transposed), at the output rate: frame 0 is source beat
// firstSourceBeat, and every beat after it is framesPerBeat() frames later.
struct RenderedClip
{
    juce::AudioBuffer<float> samples; // two channels
    RenderSettings settings;
    double fromBeat = 0.0;        // the source beats asked for
    double toBeat = 0.0;
    double firstSourceBeat = 0.0; // the source beat at frame 0 (later than fromBeat at the start of the track)

    double framesPerBeat() const { return settings.sampleRate * 60.0 / settings.tempo; }
    double frameAt (double sourceBeat) const { return (sourceBeat - firstSourceBeat) * framesPerBeat(); }
    bool covers (double from, double to) const { return fromBeat <= from + 1e-9 && toBeat >= to - 1e-9; }
};

// Renders source beats [fromBeat, toBeat) of a track whose beat grid (its
// effective beats, in seconds) is `beats`, with Rubber Band's R3 engine in
// offline mode. The stretch maps the source's beats onto the project grid:
// evenly for a steady track, and with bar lines pinned to their places where
// the track's tempo moves by more than a millisecond or so. Beyond either end
// of the track it is cut short.
//
// Blocking and slow (several seconds per minute of audio): call it on a
// worker thread. Returns null if `cancelled` becomes true along the way.
std::shared_ptr<RenderedClip> renderClip (const DecodedAudio& audio,
                                          const std::vector<double>& beats,
                                          double fromBeat,
                                          double toBeat,
                                          const RenderSettings& settings,
                                          const std::atomic<bool>* cancelled = nullptr);

} // namespace djmix
