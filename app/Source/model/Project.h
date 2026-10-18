#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace djmix
{

// A layer of the arrangement (design spec, "Data model"). A project keeps its
// layers in the order they are drawn, top first.
struct Layer
{
    int64_t id = 0;
    std::string name;
    double gain = 1.0; // linear
    bool mute = false;
    bool solo = false;
    bool cue = false; // also sent to Cue (the layer's headphone button)

    bool operator== (const Layer&) const = default;
};

// A stretch of a library track placed on a layer. Everything is in beats, not
// seconds: the source range counts beats of the track's effective grid from
// its first beat (beat 0 is the first beat, 4 the fifth, 2.5 halfway between
// the third and fourth), and the timeline range counts beats at project tempo
// from the start of the arrangement. A clip's colour comes from its track.
struct Clip
{
    int64_t id = 0;
    int64_t trackId = 0;
    std::string fileHash; // so a moved file can be relinked
    int64_t layerId = 0;
    double sourceStartBeat = 0.0;
    double timelineStartBeat = 0.0;
    double lengthBeats = 0.0;
    double gain = 1.0; // linear
    int transposeSemitones = 0;
    std::string label;

    double sourceEndBeat() const { return sourceStartBeat + lengthBeats; }
    double timelineEndBeat() const { return timelineStartBeat + lengthBeats; }

    bool operator== (const Clip&) const = default;
};

struct Project
{
    static constexpr double minTempo = 40.0;
    static constexpr double maxTempo = 250.0;
    static constexpr int maxTranspose = 12;

    std::string name;
    double tempo = 128.0; // BPM
    std::optional<std::string> key; // Camelot, e.g. "8A"
    std::vector<Layer> layers;
    std::vector<Clip> clips;

    // A new project with one layer.
    static Project create (std::string name, double tempo = 128.0);

    const Layer* layer (int64_t id) const;
    Layer* layer (int64_t id);
    const Clip* clip (int64_t id) const;
    Clip* clip (int64_t id);

    // One more than any layer or clip id, for the next one added.
    int64_t nextId() const;

    // Adds a layer at the bottom and returns its id.
    int64_t addLayer (std::string name = {});

    // Where the last clip ends, in beats.
    double lengthBeats() const;

    double secondsPerBeat() const { return 60.0 / tempo; }

    bool operator== (const Project&) const = default;
};

// Whether a layer is heard: not muted, and soloed if any layer is.
bool isAudible (const Project& project, const Layer& layer);

} // namespace djmix
