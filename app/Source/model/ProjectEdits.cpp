#include "ProjectEdits.h"

#include <algorithm>

namespace djmix::projectEdits
{

Project addClip (const Project& project, Clip clip, int64_t* newId)
{
    if (project.layer (clip.layerId) == nullptr || ! (clip.lengthBeats > 0.0))
        return project;
    Project p = project;
    clip.id = p.nextId();
    clip.timelineStartBeat = std::max (0.0, clip.timelineStartBeat);
    clip.lengthBeats = std::max (minClipLength, clip.lengthBeats);
    if (newId != nullptr)
        *newId = clip.id;
    p.clips.push_back (std::move (clip));
    return p;
}

Project moveClip (const Project& project, int64_t clipId, int64_t layerId, double timelineStartBeat)
{
    Project p = project;
    auto* c = p.clip (clipId);
    if (c == nullptr || p.layer (layerId) == nullptr)
        return project;
    c->layerId = layerId;
    c->timelineStartBeat = std::max (0.0, timelineStartBeat);
    return p;
}

Project trimClipStart (const Project& project, int64_t clipId, double timelineStartBeat)
{
    Project p = project;
    auto* c = p.clip (clipId);
    if (c == nullptr)
        return project;
    const double end = c->timelineEndBeat();
    const double start = std::clamp (timelineStartBeat, 0.0, end - minClipLength);
    const double moved = start - c->timelineStartBeat;
    c->timelineStartBeat = start;
    c->sourceStartBeat += moved;
    c->lengthBeats = end - start;
    return p;
}

Project trimClipEnd (const Project& project, int64_t clipId, double timelineEndBeat)
{
    Project p = project;
    auto* c = p.clip (clipId);
    if (c == nullptr)
        return project;
    c->lengthBeats = std::max (minClipLength, timelineEndBeat - c->timelineStartBeat);
    return p;
}

Project splitClip (const Project& project, int64_t clipId, double beat, int64_t* newId)
{
    const auto* original = project.clip (clipId);
    if (original == nullptr || beat < original->timelineStartBeat + minClipLength
        || beat > original->timelineEndBeat() - minClipLength)
        return project;

    Project p = project;
    auto* first = p.clip (clipId);
    Clip second = *first;
    const double cut = beat - first->timelineStartBeat;
    first->lengthBeats = cut;
    second.id = p.nextId();
    second.timelineStartBeat = beat;
    second.sourceStartBeat += cut;
    second.lengthBeats -= cut;
    if (newId != nullptr)
        *newId = second.id;

    // Right after the first, so the clip list keeps its order.
    const auto at = std::find_if (p.clips.begin(), p.clips.end(), [clipId] (const Clip& c) { return c.id == clipId; });
    p.clips.insert (at + 1, second);
    return p;
}

Project removeClips (const Project& project, const std::vector<int64_t>& clipIds)
{
    Project p = project;
    std::erase_if (p.clips, [&] (const Clip& c) { return std::find (clipIds.begin(), clipIds.end(), c.id) != clipIds.end(); });
    return p;
}

std::vector<int64_t> clipsAt (const Project& project, double beat)
{
    std::vector<int64_t> ids;
    for (const auto& c : project.clips)
        if (c.timelineStartBeat <= beat && c.timelineEndBeat() > beat)
            ids.push_back (c.id);
    return ids;
}

Project setTempo (const Project& project, double bpm)
{
    Project p = project;
    p.tempo = std::clamp (bpm, Project::minTempo, Project::maxTempo);
    return p;
}

Project addLayer (const Project& project, int64_t* newId)
{
    Project p = project;
    const auto id = p.addLayer();
    if (newId != nullptr)
        *newId = id;
    return p;
}

Project removeLayer (const Project& project, int64_t layerId)
{
    if (project.layers.size() <= 1 || project.layer (layerId) == nullptr)
        return project;
    Project p = project;
    std::erase_if (p.layers, [layerId] (const Layer& l) { return l.id == layerId; });
    std::erase_if (p.clips, [layerId] (const Clip& c) { return c.layerId == layerId; });
    return p;
}

Project setLayer (const Project& project, const Layer& layer)
{
    Project p = project;
    auto* l = p.layer (layer.id);
    if (l == nullptr)
        return project;
    *l = layer;
    return p;
}

} // namespace djmix::projectEdits
