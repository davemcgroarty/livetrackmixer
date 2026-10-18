#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>

#include "TestPaths.h"
#include "analysis/AnalysisLoader.h"
#include "analysis/AnalyzerRunner.h"

using namespace djmix;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{
// Collects runner callbacks from its worker threads.
struct Recorder
{
    std::mutex mutex;
    std::condition_variable changed;
    std::map<int64_t, std::vector<double>> progress;
    std::map<int64_t, fs::path> finished;
    std::map<int64_t, std::string> failed;

    AnalyzerRunner::Callbacks callbacks()
    {
        return { [this] (int64_t id, double p, const std::string&)
                 {
                     std::lock_guard lock (mutex);
                     progress[id].push_back (p);
                 },
                 [this] (int64_t id, const fs::path& json)
                 {
                     std::lock_guard lock (mutex);
                     finished[id] = json;
                     changed.notify_all();
                 },
                 [this] (int64_t id, const std::string& message)
                 {
                     std::lock_guard lock (mutex);
                     failed[id] = message;
                     changed.notify_all();
                 } };
    }

    bool waitForResults (size_t count, std::chrono::seconds timeout = 60s)
    {
        std::unique_lock lock (mutex);
        return changed.wait_for (lock, timeout, [&] { return finished.size() + failed.size() >= count; });
    }
};

struct TempDir
{
    fs::path path = fs::temp_directory_path() / ("djmix-runner-" + std::to_string (std::rand()));
    TempDir() { fs::create_directories (path); }
    ~TempDir()
    {
        std::error_code ignored;
        fs::remove_all (path, ignored);
    }
};

fs::path fakeAnalyzer()
{
    return fs::path (DJMIX_FAKE_ANALYZER);
}
} // namespace

TEST_CASE ("The runner reports progress and the analysis file", "[runner]")
{
    TempDir cache;
    Recorder recorder;
    AnalyzerRunner runner (recorder.callbacks());
    runner.setExecutable (fakeAnalyzer());
    runner.setCacheDirectory (cache.path);

    runner.enqueue (1, cache.path / "first track.flac");
    runner.enqueue (2, cache.path / "second.mp3");
    runner.enqueue (2, cache.path / "second.mp3"); // a duplicate is ignored
    REQUIRE (recorder.waitForResults (2));

    std::lock_guard lock (recorder.mutex);
    CHECK (recorder.failed.empty());
    REQUIRE (recorder.finished.count (1));
    CHECK (fs::exists (recorder.finished[1]));
    CHECK (recorder.progress[1].size() >= 7);
    CHECK (std::holds_alternative<TrackAnalysis> (AnalysisLoader::fromFile (recorder.finished[1])));
}

TEST_CASE ("A cached analysis counts as finished", "[runner]")
{
    TempDir cache;
    Recorder first, second;
    {
        AnalyzerRunner runner (first.callbacks());
        runner.setExecutable (fakeAnalyzer());
        runner.setCacheDirectory (cache.path);
        runner.enqueue (1, cache.path / "track.wav");
        REQUIRE (first.waitForResults (1));
    }
    AnalyzerRunner runner (second.callbacks());
    runner.setExecutable (fakeAnalyzer());
    runner.setCacheDirectory (cache.path);
    runner.enqueue (1, cache.path / "track.wav");
    REQUIRE (second.waitForResults (1));
    std::lock_guard lock (second.mutex);
    CHECK (second.finished[1] == first.finished[1]);
}

TEST_CASE ("The runner reports analyzer failures", "[runner]")
{
    TempDir cache;
    Recorder recorder;
    AnalyzerRunner runner (recorder.callbacks());
    runner.setExecutable (fakeAnalyzer());
    runner.setCacheDirectory (cache.path);

    runner.enqueue (1, cache.path / "will fail.flac");
    runner.enqueue (2, cache.path / "will crash.flac");
    REQUIRE (recorder.waitForResults (2));

    std::lock_guard lock (recorder.mutex);
    CHECK (recorder.failed[1] == "ValueError: fake failure");
    CHECK (recorder.failed[2].find ("exit code 3") != std::string::npos);
    CHECK (recorder.failed[2].find ("fake crash") != std::string::npos);
}

TEST_CASE ("Missing or broken analyzers fail with a message", "[runner]")
{
    TempDir cache;
    Recorder recorder;
    AnalyzerRunner runner (recorder.callbacks());
    runner.setCacheDirectory (cache.path);

    runner.enqueue (1, cache.path / "a.wav");
    REQUIRE (recorder.waitForResults (1));
    runner.setExecutable (cache.path / "no-such-analyzer");
    runner.enqueue (2, cache.path / "b.wav");
    REQUIRE (recorder.waitForResults (2));

    std::lock_guard lock (recorder.mutex);
    CHECK (recorder.failed[1].find ("Settings") != std::string::npos);
    CHECK_FALSE (recorder.failed[2].empty());
}

TEST_CASE ("Cancelling stops a running analysis without a callback", "[runner]")
{
    TempDir cache;
    Recorder recorder;
    AnalyzerRunner runner (recorder.callbacks(), 1);
    runner.setExecutable (fakeAnalyzer());
    runner.setCacheDirectory (cache.path);

    runner.enqueue (1, cache.path / "slow.flac");
    runner.enqueue (2, cache.path / "queued.flac");
    runner.cancel (2);

    // Wait until the slow job has started, then cancel it.
    for (int i = 0; i < 200 && runner.activeJobCount() != 1; ++i)
        std::this_thread::sleep_for (10ms);
    std::this_thread::sleep_for (200ms);
    const auto start = std::chrono::steady_clock::now();
    runner.cancel (1);
    for (int i = 0; i < 500 && runner.activeJobCount() > 0; ++i)
        std::this_thread::sleep_for (10ms);

    CHECK (runner.activeJobCount() == 0);
    CHECK (std::chrono::steady_clock::now() - start < 5s);
    std::lock_guard lock (recorder.mutex);
    CHECK (recorder.finished.empty());
    CHECK (recorder.failed.empty());
}

TEST_CASE ("Destroying the runner kills running analyzers", "[runner]")
{
    TempDir cache;
    Recorder recorder;
    const auto start = std::chrono::steady_clock::now();
    {
        AnalyzerRunner runner (recorder.callbacks());
        runner.setExecutable (fakeAnalyzer());
        runner.setCacheDirectory (cache.path);
        runner.enqueue (1, cache.path / "slow.flac");
        std::this_thread::sleep_for (300ms);
    }
    CHECK (std::chrono::steady_clock::now() - start < 5s);
}

TEST_CASE ("Python interpreters run djanalyze as a module", "[runner]")
{
    const auto python = AnalyzerRunner::commandFor ("/venv/bin/python3", { "--version" });
    REQUIRE (python.size() == 4);
    CHECK (python[1] == "-m");
    CHECK (python[2] == "djanalyze.cli");

    const auto direct = AnalyzerRunner::commandFor ("/venv/bin/djanalyze", { "--version" });
    CHECK (direct.size() == 2);
}

TEST_CASE ("probeVersion identifies djanalyze", "[runner]")
{
    const auto version = AnalyzerRunner::probeVersion (fakeAnalyzer());
    REQUIRE (version);
    CHECK (version->rfind ("djanalyze", 0) == 0);
    CHECK_FALSE (AnalyzerRunner::probeVersion ("/no/such/program"));
    CHECK_FALSE (AnalyzerRunner::probeVersion ({}));
}

TEST_CASE ("The real djanalyze runs through the runner", "[runner][analyzer]")
{
    const char* exe = std::getenv ("DJMIX_DJANALYZE");
    if (exe == nullptr)
        SKIP ("DJMIX_DJANALYZE is not set");

    TempDir dir;

    // Ten seconds of clicks at 120 BPM as a 16-bit mono WAV.
    const int rate = 44100, frames = rate * 10;
    std::vector<int16_t> samples (frames, 0);
    for (int beat = 0; beat < 20; ++beat)
        for (int i = 0; i < 400; ++i)
            samples[static_cast<size_t> (beat * rate / 2 + i)] = static_cast<int16_t> ((i % 40 < 20 ? 1 : -1) * 20000 * (400 - i) / 400);

    const auto wav = dir.path / "clicks.wav";
    {
        std::ofstream out (wav, std::ios::binary);
        auto u32 = [&] (uint32_t v) { out.write (reinterpret_cast<const char*> (&v), 4); };
        auto u16 = [&] (uint16_t v) { out.write (reinterpret_cast<const char*> (&v), 2); };
        const uint32_t dataBytes = static_cast<uint32_t> (frames * 2);
        out.write ("RIFF", 4); u32 (36 + dataBytes); out.write ("WAVEfmt ", 8);
        u32 (16); u16 (1); u16 (1); u32 (rate); u32 (rate * 2); u16 (2); u16 (16);
        out.write ("data", 4); u32 (dataBytes);
        out.write (reinterpret_cast<const char*> (samples.data()), dataBytes);
    }

    Recorder recorder;
    AnalyzerRunner runner (recorder.callbacks());
    runner.setExecutable (exe);
    runner.setCacheDirectory (dir.path / "cache");
    runner.enqueue (1, wav);
    REQUIRE (recorder.waitForResults (1, 300s));

    std::lock_guard lock (recorder.mutex);
    INFO (recorder.failed[1]);
    REQUIRE (recorder.finished.count (1));
    const auto result = AnalysisLoader::fromFile (recorder.finished[1]);
    REQUIRE (std::holds_alternative<TrackAnalysis> (result));
    CHECK (std::get<TrackAnalysis> (result).bpm > 0.0);
}
