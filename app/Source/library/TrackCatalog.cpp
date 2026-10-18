#include "TrackCatalog.h"

#include <system_error>
#include <variant>

#include "analysis/AnalysisLoader.h"
#include "analysis/WaveformFile.h"
#include "view/EffectiveAnalysis.h"

namespace djmix
{

TrackCatalog::Key TrackCatalog::keyFor (const TrackRecord& t)
{
    return { t.path, t.analysisFile, t.analyzerVersion, t.fileHash, t.title, t.colour, t.overrides };
}

std::shared_ptr<const TrackMaterial> TrackCatalog::get (const TrackRecord& track)
{
    const auto key = keyFor (track);
    auto& entry = entries[track.id];
    if (entry.key == key && (entry.material != nullptr || ! entry.error.empty()))
        return entry.material;

    // Only the overrides (or the name or colour) changed: keep what was read.
    const bool sameFiles = entry.base != nullptr && entry.key.analysisFile == key.analysisFile
                           && entry.key.analyzerVersion == key.analyzerVersion;
    entry.key = key;
    entry.material = nullptr;
    entry.error.clear();

    if (! sameFiles)
    {
        entry.base = nullptr;
        entry.waveform = nullptr;
        std::error_code missing;
        if (track.analysisFile.empty() || ! std::filesystem::exists (track.analysisFile, missing))
        {
            entry.error = "not analysed yet";
            return nullptr;
        }
        auto result = AnalysisLoader::fromFile (track.analysisFile);
        if (const auto* error = std::get_if<AnalysisLoadError> (&result))
        {
            entry.error = error->message;
            return nullptr;
        }
        auto base = std::make_shared<TrackAnalysis> (std::move (std::get<TrackAnalysis> (result)));
        const auto overview = WaveformFile::read (track.analysisFile.parent_path() / base->waveformFile);
        entry.waveform = std::make_shared<const WaveformPyramid> (overview ? WaveformPyramid (*overview) : WaveformPyramid());
        entry.base = std::move (base);
    }

    auto material = std::make_shared<TrackMaterial>();
    material->trackId = track.id;
    material->file = track.path;
    material->fileHash = track.fileHash;
    material->title = track.title;
    material->colour = track.colour;
    material->analysis = effectiveAnalysis (*entry.base, track.overrides);
    material->waveform = entry.waveform;
    entry.material = std::move (material);
    return entry.material;
}

std::string TrackCatalog::errorFor (int64_t trackId) const
{
    const auto it = entries.find (trackId);
    return it != entries.end() ? it->second.error : std::string();
}

} // namespace djmix
