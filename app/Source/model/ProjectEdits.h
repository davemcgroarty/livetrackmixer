#pragma once

#include <cstdint>
#include <vector>

#include "Project.h"

namespace djmix
{

// Edits to a project. Each takes the project and returns it after the edit,
// so an undo step can keep the project before and after. Positions are in
// beats; an edit naming a clip or layer that does not exist changes nothing.
namespace projectEdits
{
    // Clips are never shorter than this (beats).
    constexpr double minClipLength = 0.25;

    // Adds a clip (given a new id, which `newId` receives if set).
    Project addClip (const Project& project, Clip clip, int64_t* newId = nullptr);

    // Moves a clip to a layer and timeline start (not before beat 0).
    Project moveClip (const Project& project, int64_t clipId, int64_t layerId, double timelineStartBeat);

    // Moves a clip's start, keeping its end where it is in the music: the
    // source start moves with it. At least minClipLength stays.
    Project trimClipStart (const Project& project, int64_t clipId, double timelineStartBeat);

    // Moves a clip's end; its start stays.
    Project trimClipEnd (const Project& project, int64_t clipId, double timelineEndBeat);

    // Cuts a clip in two at a timeline beat inside it; the second half gets
    // a new id (`newId`). Nothing happens if the beat is not inside it.
    Project splitClip (const Project& project, int64_t clipId, double beat, int64_t* newId = nullptr);

    Project removeClips (const Project& project, const std::vector<int64_t>& clipIds);

    // The clips sounding at a timeline beat (starting at or before it, ending after).
    std::vector<int64_t> clipsAt (const Project& project, double beat);

    // Within Project::minTempo and maxTempo.
    Project setTempo (const Project& project, double bpm);

    // Adds a layer at the bottom (its id in `newId`).
    Project addLayer (const Project& project, int64_t* newId = nullptr);

    // Removes a layer and its clips. The last layer stays.
    Project removeLayer (const Project& project, int64_t layerId);

    // Replaces a layer's settings (name, gain, mute, solo, cue), keeping its id.
    Project setLayer (const Project& project, const Layer& layer);
} // namespace projectEdits

} // namespace djmix
