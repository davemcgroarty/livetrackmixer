#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <juce_events/juce_events.h>

#include "LibraryDatabase.h"

namespace djmix
{

class AnalyzerRunner;

// Owns the track library and the analyzer runner, and keeps them in step.
// Lives on the message thread; listeners hear about every change there.
class LibraryController final : public juce::ChangeBroadcaster
{
public:
    LibraryController (std::unique_ptr<LibraryDatabase> database,
                       std::filesystem::path analyzerExecutable,
                       std::filesystem::path cacheDirectory);
    ~LibraryController() override;

    const std::vector<TrackRecord>& tracks() const { return trackList; }
    const TrackRecord* findTrack (int64_t id) const;

    // Overall analysis progress (0–1) for tracks being analysed right now.
    std::optional<double> progressFor (int64_t id) const;

    // Adds every audio file among `paths` (folders are searched) and queues
    // the new tracks for analysis. Returns how many tracks were added.
    int importPaths (const std::vector<std::filesystem::path>& paths);

    void reanalyse (int64_t id);
    void removeTrack (int64_t id);
    void setColour (int64_t id, int colour);

    // Hand corrections to a track's grid and sections.
    void setOverrides (int64_t id, const TrackOverrides& overrides);

    void setAnalyzerExecutable (const std::filesystem::path& executable);
    const std::filesystem::path& analyzerExecutable() const { return analyzer; }
    const std::filesystem::path& cacheDirectory() const { return cacheDir; }

    // The version of the analyzer set up now ("0.1.2"), once it has been asked;
    // empty before that or if it did not answer.
    const std::string& analyzerVersion() const { return currentAnalyzerVersion; }

private:
    void reload();
    void queueAnalysis (const TrackRecord& track);
    void resumeUnfinished();

    // Asks the analyzer for its version on a background thread, then
    // re-analyses tracks whose analysis an older (or newer) version wrote.
    void probeAnalyzerVersion();
    void handleAnalyzerVersion (int generation, const std::string& version);

    void handleProgress (int64_t id, double progress);
    void handleFinished (int64_t id, std::optional<AnalysisSummary> summary, const std::string& error);

    std::unique_ptr<LibraryDatabase> database;
    std::filesystem::path analyzer;
    std::filesystem::path cacheDir;
    std::vector<TrackRecord> trackList;
    std::map<int64_t, double> progress;
    std::string currentAnalyzerVersion;
    int probeGeneration = 0;
    std::vector<std::thread> probes; // joined on destruction

    // Cleared on destruction so callbacks still in flight from the runner's
    // threads find no controller to post to.
    std::shared_ptr<LibraryController*> alive;
    std::unique_ptr<AnalyzerRunner> runner;
};

} // namespace djmix
