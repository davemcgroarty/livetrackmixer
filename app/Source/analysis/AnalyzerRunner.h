#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace juce
{
class ChildProcess;
}

namespace djmix
{

// Runs `djanalyze analyze` as a subprocess for each queued track, a few at a
// time, and reports progress parsed from its JSON lines.
//
// Callbacks run on the runner's worker threads; the caller hands them over to
// its own thread. Destroying the runner kills any running analyzer.
class AnalyzerRunner
{
public:
    struct Callbacks
    {
        std::function<void (int64_t trackId, double progress, const std::string& stage)> onProgress;
        std::function<void (int64_t trackId, const std::filesystem::path& analysisJson)> onFinished;
        std::function<void (int64_t trackId, const std::string& message)> onFailed;
    };

    AnalyzerRunner (Callbacks callbacks, int numWorkers = 2);
    ~AnalyzerRunner();

    AnalyzerRunner (const AnalyzerRunner&) = delete;
    AnalyzerRunner& operator= (const AnalyzerRunner&) = delete;

    // Used for jobs started after the call.
    void setExecutable (const std::filesystem::path& executable);
    void setCacheDirectory (const std::filesystem::path& cacheDir);

    void enqueue (int64_t trackId, const std::filesystem::path& audioFile);

    // Drops a queued job or kills a running one. No callback follows.
    void cancel (int64_t trackId);

    // Jobs queued or running.
    int activeJobCount() const;

    // The command line for an executable: djanalyze itself, or a Python
    // interpreter, which runs `-m djanalyze.cli`.
    static std::vector<std::string> commandFor (const std::filesystem::path& executable,
                                                const std::vector<std::string>& arguments);

    // Runs `<executable> --version` and returns its output, or nothing if it
    // did not run or did not identify itself as djanalyze.
    static std::optional<std::string> probeVersion (const std::filesystem::path& executable,
                                                    int timeoutMs = 60000);

private:
    struct Job
    {
        int64_t trackId = 0;
        std::filesystem::path audioFile;
    };

    void workerLoop();
    void run (const Job& job);

    Callbacks callbacks;

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> queue;
    std::map<int64_t, std::shared_ptr<juce::ChildProcess>> running;
    std::vector<int64_t> cancelled;
    std::filesystem::path executable;
    std::filesystem::path cacheDir;
    bool stopping = false;

    std::vector<std::thread> workers;
};

} // namespace djmix
