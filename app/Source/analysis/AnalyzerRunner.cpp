#include "AnalyzerRunner.h"

#include <algorithm>
#include <cctype>

#include <juce_core/juce_core.h>

#include "AnalyzerProtocol.h"
#include "library/LibraryDatabase.h"

namespace djmix
{

namespace
{
juce::String toJuce (const std::string& utf8)
{
    return juce::String::fromUTF8 (utf8.c_str());
}

bool isPythonInterpreter (const std::filesystem::path& executable)
{
    auto stem = pathToUtf8 (executable.stem());
    std::transform (stem.begin(), stem.end(), stem.begin(), [] (unsigned char c) { return std::tolower (c); });
    return stem.rfind ("python", 0) == 0;
}
} // namespace

AnalyzerRunner::AnalyzerRunner (Callbacks callbacksToUse, int numWorkers)
    : callbacks (std::move (callbacksToUse))
{
    for (int i = 0; i < std::max (1, numWorkers); ++i)
        workers.emplace_back ([this] { workerLoop(); });
}

AnalyzerRunner::~AnalyzerRunner()
{
    {
        std::lock_guard lock (mutex);
        stopping = true;
        queue.clear();
        for (auto& [id, process] : running)
            process->kill();
    }
    wake.notify_all();
    for (auto& worker : workers)
        worker.join();
}

void AnalyzerRunner::setExecutable (const std::filesystem::path& newExecutable)
{
    std::lock_guard lock (mutex);
    executable = newExecutable;
}

void AnalyzerRunner::setCacheDirectory (const std::filesystem::path& newCacheDir)
{
    std::lock_guard lock (mutex);
    cacheDir = newCacheDir;
}

void AnalyzerRunner::enqueue (int64_t trackId, const std::filesystem::path& audioFile)
{
    {
        std::lock_guard lock (mutex);
        const bool alreadyQueued = std::any_of (queue.begin(), queue.end(), [&] (const Job& j) { return j.trackId == trackId; });
        if (alreadyQueued || running.count (trackId) > 0)
            return;
        cancelled.erase (std::remove (cancelled.begin(), cancelled.end(), trackId), cancelled.end());
        queue.push_back ({ trackId, audioFile });
    }
    wake.notify_one();
}

void AnalyzerRunner::cancel (int64_t trackId)
{
    std::lock_guard lock (mutex);
    queue.erase (std::remove_if (queue.begin(), queue.end(), [&] (const Job& j) { return j.trackId == trackId; }),
                 queue.end());
    if (auto it = running.find (trackId); it != running.end())
    {
        cancelled.push_back (trackId);
        it->second->kill();
    }
}

int AnalyzerRunner::activeJobCount() const
{
    std::lock_guard lock (mutex);
    return static_cast<int> (queue.size() + running.size());
}

std::vector<std::string> AnalyzerRunner::commandFor (const std::filesystem::path& exe,
                                                     const std::vector<std::string>& arguments)
{
    std::vector<std::string> command { pathToUtf8 (exe) };
    if (isPythonInterpreter (exe))
    {
        command.push_back ("-m");
        command.push_back ("djanalyze.cli");
    }
    command.insert (command.end(), arguments.begin(), arguments.end());
    return command;
}

std::optional<std::string> AnalyzerRunner::probeVersion (const std::filesystem::path& exe, int timeoutMs)
{
    if (exe.empty())
        return std::nullopt;

    juce::StringArray args;
    for (const auto& part : commandFor (exe, { "--version" }))
        args.add (toJuce (part));

    juce::ChildProcess process;
    if (! process.start (args, juce::ChildProcess::wantStdOut))
        return std::nullopt;

    const auto output = process.readAllProcessOutput().trim();
    if (! process.waitForProcessToFinish (timeoutMs))
    {
        process.kill();
        return std::nullopt;
    }
    if (process.getExitCode() != 0 || ! output.startsWith ("djanalyze"))
        return std::nullopt;
    return output.toStdString();
}

void AnalyzerRunner::workerLoop()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock lock (mutex);
            wake.wait (lock, [this] { return stopping || ! queue.empty(); });
            if (stopping)
                return;
            job = queue.front();
            queue.pop_front();
        }
        run (job);
    }
}

void AnalyzerRunner::run (const Job& job)
{
    auto process = std::make_shared<juce::ChildProcess>();
    std::filesystem::path exe, outDir;
    {
        std::lock_guard lock (mutex);
        exe = executable;
        outDir = cacheDir;
        if (stopping)
            return;
        running[job.trackId] = process;
    }

    auto finish = [&] (auto&& report)
    {
        bool wasCancelled = false;
        {
            std::lock_guard lock (mutex);
            running.erase (job.trackId);
            const auto it = std::find (cancelled.begin(), cancelled.end(), job.trackId);
            wasCancelled = it != cancelled.end() || stopping;
            if (it != cancelled.end())
                cancelled.erase (it);
        }
        if (! wasCancelled)
            report();
    };

    auto fail = [&] (const std::string& message)
    {
        finish ([&] {
            if (callbacks.onFailed)
                callbacks.onFailed (job.trackId, message);
        });
    };

    if (exe.empty())
        return fail ("The analyzer (djanalyze) is not set up. Choose it in Settings.");

    juce::StringArray args;
    for (const auto& part : commandFor (exe, analyzerArguments (job.audioFile, outDir)))
        args.add (toJuce (part));

    if (! process->start (args, juce::ChildProcess::wantStdOut))
        return fail ("Could not start the analyzer at " + pathToUtf8 (exe) + ".");

    // Progress lines arrive one at a time and ChildProcess reads block until
    // the buffer is full, so read a byte at a time.
    std::optional<AnalyzerEvent> outcome;
    std::vector<std::string> otherOutput;
    std::string line;
    char c = 0;

    auto handleLine = [&]
    {
        if (line.empty())
            return;
        if (auto event = parseAnalyzerLine (line))
        {
            if (event->kind == AnalyzerEvent::Kind::progress)
            {
                if (callbacks.onProgress)
                    callbacks.onProgress (job.trackId, event->progress, event->stage);
            }
            else
            {
                outcome = event;
            }
        }
        else
        {
            otherOutput.push_back (line);
            if (otherOutput.size() > 5)
                otherOutput.erase (otherOutput.begin());
        }
        line.clear();
    };

    while (process->readProcessOutput (&c, 1) == 1)
    {
        if (c == '\n')
            handleLine();
        else if (c != '\r')
            line.push_back (c);
    }
    handleLine();
    process->waitForProcessToFinish (10000);

    if (outcome && (outcome->kind == AnalyzerEvent::Kind::done || outcome->kind == AnalyzerEvent::Kind::cached))
    {
        return finish ([&] {
            if (callbacks.onFinished)
                callbacks.onFinished (job.trackId, outcome->jsonPath);
        });
    }

    if (outcome && outcome->kind == AnalyzerEvent::Kind::error)
        return fail (outcome->message);

    std::string message = "The analyzer stopped without a result (exit code "
                          + std::to_string (process->getExitCode()) + ").";
    for (const auto& extra : otherOutput)
        message += "\n" + extra;
    fail (message);
}

} // namespace djmix
