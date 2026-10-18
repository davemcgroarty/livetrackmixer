#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "analysis/AnalyzerProtocol.h"

using djmix::AnalyzerEvent;
using djmix::parseAnalyzerLine;

TEST_CASE ("Analyzer progress lines parse", "[analyzer][protocol]")
{
    const auto progress = parseAnalyzerLine (R"({"stage": "beats", "progress": 0.4})");
    REQUIRE (progress);
    CHECK (progress->kind == AnalyzerEvent::Kind::progress);
    CHECK (progress->stage == "beats");
    CHECK (progress->progress == Catch::Approx (0.4));

    const auto done = parseAnalyzerLine (
        R"({"stage": "done", "progress": 1.0, "json": "/cache/sha256-ab.json", "sources": ["builtin"]})");
    REQUIRE (done);
    CHECK (done->kind == AnalyzerEvent::Kind::done);
    CHECK (done->jsonPath == std::filesystem::path ("/cache/sha256-ab.json"));

    const auto cached = parseAnalyzerLine (R"({"stage": "cached", "progress": 1.0, "json": "x.json"})");
    REQUIRE (cached);
    CHECK (cached->kind == AnalyzerEvent::Kind::cached);

    const auto error = parseAnalyzerLine (R"({"stage": "error", "progress": 0.0, "message": "no such file"})");
    REQUIRE (error);
    CHECK (error->kind == AnalyzerEvent::Kind::error);
    CHECK (error->message == "no such file");
}

TEST_CASE ("Lines that are not analyzer events are ignored", "[analyzer][protocol]")
{
    CHECK_FALSE (parseAnalyzerLine (""));
    CHECK_FALSE (parseAnalyzerLine ("Traceback (most recent call last):"));
    CHECK_FALSE (parseAnalyzerLine ("[1, 2]"));
    CHECK_FALSE (parseAnalyzerLine (R"({"progress": 0.5})"));
}

TEST_CASE ("Analyzer arguments", "[analyzer][protocol]")
{
    const auto args = djmix::analyzerArguments ("/music/a.flac", "/cache");
    REQUIRE (args.size() == 4);
    CHECK (args[0] == "analyze");
    CHECK (args[2] == "--out");
}

TEST_CASE ("Analyzer versions parse from --version output", "[analyzer][protocol]")
{
    CHECK (djmix::parseAnalyzerVersion ("djanalyze 0.1.2") == "0.1.2");
    CHECK (djmix::parseAnalyzerVersion ("djanalyze 1.0.0\r\n") == "1.0.0");
    CHECK_FALSE (djmix::parseAnalyzerVersion ("Python 3.12.1"));
    CHECK_FALSE (djmix::parseAnalyzerVersion ("djanalyze "));
    CHECK_FALSE (djmix::parseAnalyzerVersion (""));
}

TEST_CASE ("findOnPath finds programs in folders with non-ASCII names", "[analyzer][protocol]")
{
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / fs::path (u8"djmix-päth-テスト");
    fs::create_directories (dir);
#ifdef _WIN32
    const auto program = dir / "djmix-find-test.exe";
#else
    const auto program = dir / "djmix-find-test";
#endif
    std::ofstream (program) << "x";

    // Put the folder on PATH for the test, then restore it.
#ifdef _WIN32
    const wchar_t* old = _wgetenv (L"PATH");
    const std::wstring saved = old != nullptr ? old : L"";
    _wputenv_s (L"PATH", (dir.native() + L";" + saved).c_str());
#else
    const char* old = std::getenv ("PATH");
    const std::string saved = old != nullptr ? old : "";
    setenv ("PATH", (dir.native() + ":" + saved).c_str(), 1);
#endif

    const auto found = djmix::findOnPath ("djmix-find-test");
    const auto missing = djmix::findOnPath ("djmix-no-such-program");

#ifdef _WIN32
    _wputenv_s (L"PATH", saved.c_str());
#else
    setenv ("PATH", saved.c_str(), 1);
#endif
    fs::remove_all (dir);

    REQUIRE (found.has_value());
    CHECK (*found == program);
    CHECK_FALSE (missing.has_value());
}
