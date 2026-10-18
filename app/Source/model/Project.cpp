#include "Project.h"

#include <algorithm>

namespace djmix
{

namespace
{
template <typename Items>
auto* findById (Items& items, int64_t id)
{
    const auto it = std::find_if (items.begin(), items.end(), [id] (const auto& item) { return item.id == id; });
    return it != items.end() ? &*it : nullptr;
}
} // namespace

Project Project::create (std::string name, double tempo)
{
    Project p;
    p.name = std::move (name);
    p.tempo = tempo;
    p.addLayer();
    return p;
}

const Layer* Project::layer (int64_t id) const { return findById (layers, id); }
Layer* Project::layer (int64_t id) { return findById (layers, id); }
const Clip* Project::clip (int64_t id) const { return findById (clips, id); }
Clip* Project::clip (int64_t id) { return findById (clips, id); }

int64_t Project::nextId() const
{
    int64_t highest = 0;
    for (const auto& l : layers)
        highest = std::max (highest, l.id);
    for (const auto& c : clips)
        highest = std::max (highest, c.id);
    return highest + 1;
}

int64_t Project::addLayer (std::string layerName)
{
    Layer l;
    l.id = nextId();
    l.name = layerName.empty() ? "Layer " + std::to_string (layers.size() + 1) : std::move (layerName);
    layers.push_back (l);
    return l.id;
}

double Project::lengthBeats() const
{
    double end = 0.0;
    for (const auto& c : clips)
        end = std::max (end, c.timelineEndBeat());
    return end;
}

bool isAudible (const Project& project, const Layer& layer)
{
    if (layer.mute)
        return false;
    const bool anySolo = std::any_of (project.layers.begin(), project.layers.end(), [] (const Layer& l) { return l.solo; });
    return ! anySolo || layer.solo;
}

} // namespace djmix
