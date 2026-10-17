// A stand-in for djanalyze, so the analyzer runner can be tested without
// Python. It follows the real command line and output protocol:
//
//   fake_djanalyze --version
//   fake_djanalyze analyze <audio-file> --out <cache-dir>
//
// What it does depends on the audio file's name:
//   ...fail...   prints an error line and exits 1
//   ...crash...  prints a non-JSON line and exits 3 without a result
//   ...slow...   prints one progress line, then waits 30 s (for cancel tests)
//   otherwise    prints progress lines, writes the example analysis (with a
//                hash made from the file name) and prints a done line

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace
{
std::string fakeHash (const std::string& name)
{
    std::ostringstream hex;
    std::size_t seed = std::hash<std::string> {}(name);
    for (int i = 0; i < 4; ++i)
    {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        char block[17];
        std::snprintf (block, sizeof (block), "%016llx", static_cast<unsigned long long> (seed));
        hex << block;
    }
    return "sha256:" + hex.str();
}

void line (const std::string& json)
{
    std::cout << json << "\n" << std::flush;
}

std::string jsonString (std::string text)
{
    std::string out = "\"";
    for (char c : text)
        out += (c == '\\' || c == '"') ? std::string ("\\") + c : std::string (1, c);
    return out + "\"";
}
} // namespace

int main (int argc, char** argv)
{
    const std::string first = argc > 1 ? argv[1] : "";
    if (first == "--version")
    {
        std::cout << "djanalyze 0.0.0-fake\n";
        return 0;
    }

    if (argc != 5 || first != "analyze" || std::string (argv[3]) != "--out")
    {
        std::cerr << "usage: fake_djanalyze analyze <audio-file> --out <cache-dir>\n";
        return 2;
    }

    const fs::path audio = argv[2];
    const fs::path out = argv[4];
    const std::string name = audio.filename().string();

    line (R"({"stage": "hash", "progress": 0.0})");

    if (name.find ("fail") != std::string::npos)
    {
        line (R"({"stage": "error", "progress": 0.0, "message": "ValueError: fake failure"})");
        return 1;
    }
    if (name.find ("crash") != std::string::npos)
    {
        line ("Traceback (most recent call last): fake crash");
        return 3;
    }
    if (name.find ("slow") != std::string::npos)
    {
        std::this_thread::sleep_for (std::chrono::seconds (30));
        return 0;
    }

    const std::string hash = fakeHash (name);
    const std::string stem = "sha256-" + hash.substr (7);
    fs::create_directories (out);
    const fs::path json = out / (stem + ".json");

    if (fs::exists (json))
    {
        line (R"({"stage": "cached", "progress": 1.0, "json": )" + jsonString (json.string()) + "}");
        return 0;
    }

    for (const char* stage : { "decode", "beats", "structure", "descriptors", "key", "waveform", "write" })
    {
        line (std::string (R"({"stage": ")") + stage + R"(", "progress": 0.5})");
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    std::ifstream example (fs::path (DJMIX_REPO_ROOT) / "schemas" / "examples" / "analysis.v1.example.json");
    std::stringstream text;
    text << example.rdbuf();
    std::string document = text.str();
    const auto at = document.find ("sha256:");
    document.replace (at, 7 + 64, hash);

    std::ofstream (json) << document;
    line (R"({"stage": "done", "progress": 1.0, "json": )" + jsonString (json.string()) + "}");
    return 0;
}
