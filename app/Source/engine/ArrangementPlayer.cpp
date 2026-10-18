#include "ArrangementPlayer.h"

#include <algorithm>
#include <cmath>

namespace djmix
{

namespace
{
constexpr double fallbackRate = 44100.0;
constexpr size_t unusedRendersKept = 8;
} // namespace

ArrangementPlayer::ArrangementPlayer (MasterPlayer& m,
                                      Resolve r,
                                      RenderCache::Post post,
                                      RenderCache::Decode decode,
                                      int renderWorkers)
    : master (m),
      resolve (std::move (r)),
      cache (std::move (post), std::move (decode), renderWorkers)
{
    cache.onRenderReady = [this]
    {
        rebuild();
        if (onChanged)
            onChanged();
    };
}

ArrangementPlayer::~ArrangementPlayer() = default;

void ArrangementPlayer::setProject (const Project& project)
{
    current = project;
    rebuild();
}

void ArrangementPlayer::refresh()
{
    rebuild();
}

MasterPlayer::State ArrangementPlayer::state()
{
    master.releaseUnused();
    return master.state();
}

ArrangementPlayer::ClipState ArrangementPlayer::clipState (int64_t clipId) const
{
    for (const auto& [id, s] : states)
        if (id == clipId)
            return s;
    return ClipState::missing;
}

bool ArrangementPlayer::allReady() const
{
    return std::all_of (states.begin(), states.end(), [] (const auto& s) { return s.second == ClipState::ready; });
}

void ArrangementPlayer::rebuild()
{
    const double deviceRate = master.deviceSampleRate();
    const double rate = deviceRate > 0.0 ? deviceRate : fallbackRate;

    auto snapshot = std::make_shared<PlaybackSnapshot>();
    snapshot->tempo = current.tempo;
    snapshot->sampleRate = rate;
    const double samplesPerBeat = snapshot->samplesPerBeat();

    for (const auto& layer : current.layers)
        snapshot->layerGains.push_back (isAudible (current, layer) ? static_cast<float> (layer.gain) : 0.0f);

    // Clips nearest the playhead render first.
    const double playhead = master.state().beat;
    std::vector<const Clip*> clips;
    for (const auto& c : current.clips)
        clips.push_back (&c);
    auto distance = [playhead] (const Clip* c)
    {
        if (playhead < c->timelineStartBeat)
            return c->timelineStartBeat - playhead;
        if (playhead > c->timelineEndBeat())
            return (playhead - c->timelineEndBeat()) + 1e6; // behind the playhead: last
        return 0.0;
    };
    std::stable_sort (clips.begin(), clips.end(), [&] (const Clip* a, const Clip* b) { return distance (a) < distance (b); });

    std::vector<RenderCache::Job> jobs;
    states.clear();
    for (const auto* clip : clips)
    {
        const auto layerIt = std::find_if (current.layers.begin(), current.layers.end(), [&] (const Layer& l) { return l.id == clip->layerId; });
        const auto source = resolve ? resolve (*clip) : std::nullopt;
        if (layerIt == current.layers.end() || ! source || (cache.errorFor (source->file)))
        {
            states.emplace_back (clip->id, ClipState::missing);
            continue;
        }

        RenderCache::Job job { source->file, source->beats, clip->sourceStartBeat, clip->sourceEndBeat(),
                               { current.tempo, clip->transposeSemitones, rate } };
        auto render = cache.find (job);
        auto state = ClipState::ready;
        if (render == nullptr)
        {
            jobs.push_back (job);
            render = cache.findStandIn (job);
            state = render != nullptr ? ClipState::standIn : ClipState::rendering;
        }
        states.emplace_back (clip->id, state);
        if (render == nullptr)
            continue;

        PlaybackSnapshot::Voice v;
        v.render = render.get();
        v.layer = static_cast<int> (layerIt - current.layers.begin());
        v.gain = static_cast<float> (clip->gain);
        v.start = std::llround (clip->timelineStartBeat * samplesPerBeat);
        v.end = std::llround (clip->timelineEndBeat() * samplesPerBeat);
        v.frameAtStart = render->frameAt (clip->sourceStartBeat);
        v.step = current.tempo / render->settings.tempo; // render frames per sample
        snapshot->voices.push_back (v);
        snapshot->renders.push_back (std::move (render));
    }

    cache.request (jobs);
    master.publish (std::move (snapshot));
    cache.trim (unusedRendersKept);
}

} // namespace djmix
