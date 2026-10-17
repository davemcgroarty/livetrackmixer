#include "LibraryController.h"

#include "ImportScanner.h"
#include "TrackColours.h"
#include "analysis/AnalysisLoader.h"
#include "analysis/AnalyzerProtocol.h"
#include "analysis/AnalyzerRunner.h"

namespace djmix
{

namespace
{
// Loads the analyzer's JSON on the runner's thread so the message thread only
// has to store the few fields the library keeps.
std::optional<AnalysisSummary> summarise (const std::filesystem::path& json, std::string& error)
{
    const auto result = AnalysisLoader::fromFile (json);
    if (const auto* failure = std::get_if<AnalysisLoadError> (&result))
    {
        error = "The analysis file could not be read: " + failure->message;
        return std::nullopt;
    }

    const auto& analysis = std::get<TrackAnalysis> (result);
    return AnalysisSummary { analysis.fileHash,      analysis.bpm,         analysis.key.name, analysis.key.camelot,
                             analysis.durationSec, analysis.beatConfidence, json,
                             analysis.analyzerVersion };
}

// Runs `action` on the message thread if the controller still exists then.
template <typename Action>
void post (const std::weak_ptr<LibraryController*>& weak, Action action)
{
    juce::MessageManager::callAsync ([weak, action = std::move (action)]
    {
        if (auto self = weak.lock())
            action (**self);
    });
}
} // namespace

LibraryController::LibraryController (std::unique_ptr<LibraryDatabase> db,
                                      std::filesystem::path analyzerExecutable,
                                      std::filesystem::path cacheDirectory)
    : database (std::move (db)),
      analyzer (std::move (analyzerExecutable)),
      cacheDir (std::move (cacheDirectory)),
      alive (std::make_shared<LibraryController*> (this))
{
    std::weak_ptr<LibraryController*> weak = alive;

    // Runner callbacks arrive on worker threads; hop to the message thread.
    AnalyzerRunner::Callbacks callbacks;
    callbacks.onProgress = [weak] (int64_t id, double fraction, const std::string&)
    {
        post (weak, [id, fraction] (LibraryController& c) { c.handleProgress (id, fraction); });
    };
    callbacks.onFinished = [weak] (int64_t id, const std::filesystem::path& json)
    {
        std::string error;
        auto summary = summarise (json, error);
        post (weak, [id, summary, error] (LibraryController& c) { c.handleFinished (id, summary, error); });
    };
    callbacks.onFailed = [weak] (int64_t id, const std::string& message)
    {
        post (weak, [id, message] (LibraryController& c) { c.handleFinished (id, std::nullopt, message); });
    };

    runner = std::make_unique<AnalyzerRunner> (std::move (callbacks));
    runner->setExecutable (analyzer);
    runner->setCacheDirectory (cacheDir);

    reload();
    resumeUnfinished();
    probeAnalyzerVersion();
}

LibraryController::~LibraryController()
{
    alive.reset();
    runner.reset(); // kills running analyzers and joins the workers
    for (auto& probe : probes)
        probe.join();
}

const TrackRecord* LibraryController::findTrack (int64_t id) const
{
    for (const auto& t : trackList)
        if (t.id == id)
            return &t;
    return nullptr;
}

std::optional<double> LibraryController::progressFor (int64_t id) const
{
    if (const auto it = progress.find (id); it != progress.end())
        return it->second;
    return std::nullopt;
}

int LibraryController::importPaths (const std::vector<std::filesystem::path>& paths)
{
    int added = 0;
    auto usage = database->colourUsage();
    for (const auto& file : collectAudioFiles (paths))
    {
        const auto name = trackNameFromFile (file);
        const int colour = nextTrackColour (usage);
        if (auto id = database->addTrack (file, name.title, name.artist, colour))
        {
            ++usage[static_cast<size_t> (colour)];
            ++added;
            if (auto track = database->track (*id))
                queueAnalysis (*track);
        }
    }
    reload();
    return added;
}

void LibraryController::reanalyse (int64_t id)
{
    if (auto track = database->track (id))
    {
        queueAnalysis (*track);
        reload();
    }
}

void LibraryController::removeTrack (int64_t id)
{
    runner->cancel (id);
    progress.erase (id);
    database->removeTrack (id);
    reload();
}

void LibraryController::setColour (int64_t id, int colour)
{
    database->setColour (id, colour);
    reload();
}

void LibraryController::setAnalyzerExecutable (const std::filesystem::path& executable)
{
    analyzer = executable;
    runner->setExecutable (executable);

    // Tracks that failed because no analyzer was set up get another go.
    for (const auto& track : trackList)
        if (track.status == AnalysisStatus::failed)
            queueAnalysis (track);
    reload();

    currentAnalyzerVersion.clear();
    probeAnalyzerVersion();
}

void LibraryController::reload()
{
    trackList = database->allTracks();
    sendChangeMessage();
}

void LibraryController::queueAnalysis (const TrackRecord& track)
{
    database->setStatus (track.id, AnalysisStatus::pending);
    runner->enqueue (track.id, track.path);
}

void LibraryController::resumeUnfinished()
{
    std::error_code error;
    for (const auto& track : trackList)
    {
        const bool unfinished = track.status == AnalysisStatus::pending || track.status == AnalysisStatus::analysing;
        const bool analysisMissing = track.status == AnalysisStatus::ready
                                     && ! std::filesystem::exists (track.analysisFile, error);
        if (unfinished || analysisMissing)
            queueAnalysis (track);
    }
    reload();
}

void LibraryController::probeAnalyzerVersion()
{
    if (analyzer.empty())
        return;

    const int generation = ++probeGeneration;
    probes.emplace_back ([weak = std::weak_ptr<LibraryController*> (alive), exe = analyzer, generation]
    {
        const auto output = AnalyzerRunner::probeVersion (exe);
        const auto version = output ? parseAnalyzerVersion (*output) : std::nullopt;
        if (version)
            post (weak, [generation, v = *version] (LibraryController& c) { c.handleAnalyzerVersion (generation, v); });
    });
}

void LibraryController::handleAnalyzerVersion (int generation, const std::string& version)
{
    if (generation != probeGeneration)
        return; // the analyzer was changed while this one was asked

    currentAnalyzerVersion = version;
    bool queued = false;
    for (const auto& track : trackList)
    {
        if (track.status == AnalysisStatus::ready && track.analyzerVersion != version)
        {
            queueAnalysis (track);
            queued = true;
        }
    }
    if (queued)
        reload();
}

void LibraryController::handleProgress (int64_t id, double fraction)
{
    if (! findTrack (id))
        return;
    const bool first = progress.count (id) == 0;
    progress[id] = fraction;
    if (first)
    {
        database->setStatus (id, AnalysisStatus::analysing);
        reload();
    }
    else
    {
        sendChangeMessage(); // only the progress bar moved
    }
}

void LibraryController::handleFinished (int64_t id, std::optional<AnalysisSummary> summary, const std::string& error)
{
    progress.erase (id);
    if (! findTrack (id))
        return;

    if (summary)
        database->setAnalysis (id, *summary);
    else
        database->setStatus (id, AnalysisStatus::failed, error);
    reload();
}

} // namespace djmix
