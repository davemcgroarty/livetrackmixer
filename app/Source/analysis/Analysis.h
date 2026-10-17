#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace djmix
{

// In-memory form of an analysis file (schemas/analysis.v1.schema.json).
// Positions are seconds from the file start. Beat indices are 0-based;
// bar, phrase and startBar numbers are 1-based. Section indices are 0-based.
struct AnalysisKey
{
    std::string name;    // "A minor"; may be empty for a section key
    std::string camelot; // "8A"
    double confidence = 0.0;
};

struct Bar
{
    int n = 0;
    double start = 0.0;
    int firstBeat = 0;
};

struct Phrase
{
    int n = 0;
    int startBar = 0;
    int lengthBars = 0;
    int section = 0;
};

struct Section
{
    double start = 0.0;
    double end = 0.0;
    int startBar = 0;
    int lengthBars = 0;
    std::optional<AnalysisKey> key; // empty when the section has no reliable key
};

struct ContentScores
{
    double rhythm = 0.0;
    double bass = 0.0;
    double high = 0.0;
    double melody = 0.0;
};

struct TrackAnalysis
{
    static constexpr int supportedSchemaVersion = 1;

    int schemaVersion = 0;
    std::string analyzerVersion;

    std::string fileHash; // "sha256:<hex>"
    double durationSec = 0.0;
    int sampleRate = 0;

    double bpm = 0.0;
    bool tempoVariable = false;

    AnalysisKey key;
    std::optional<double> integratedLufs;

    std::vector<double> beats;
    std::vector<double> downbeats;
    std::vector<Bar> bars;
    std::vector<Phrase> phrases;
    std::vector<Section> sections;

    std::vector<double> energyPerBar;
    std::vector<ContentScores> descriptorsPerBar;
    std::vector<ContentScores> descriptorsPerSection; // indexed by section

    double beatConfidence = 0.0;
    double sectionConfidence = 0.0;

    std::string waveformFile; // relative to the analysis JSON
};

} // namespace djmix
