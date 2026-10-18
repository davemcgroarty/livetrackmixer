#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "CueMode.h"

namespace djmix
{

// A track decoded into memory: stereo float samples at the file's own rate.
struct DecodedAudio
{
    juce::AudioBuffer<float> samples; // two channels
    double sampleRate = 44100.0;

    double duration() const { return sampleRate > 0 ? samples.getNumSamples() / sampleRate : 0.0; }
};

// Plays auditions on the Cue bus: a stretch of a decoded track, resampled to
// the device rate, faded in and out over a few milliseconds so starts, stops
// and switches never click. Nothing it plays reaches Master.
//
// At the end of its stretch an audition stops, loops (crossfading back to its
// start over the same few milliseconds) or plays on to the end of the track,
// as its CueMode says.
//
// Threads: play, stop and the setters are for the message thread; commands
// reach the audio thread through a lock-free FIFO. The audio thread (the
// AudioEngine, which routes the Cue bus to its outputs) calls prepare,
// render and stopped; it never allocates, locks or frees: decoded audio stays
// owned here, on the message thread, until releaseUnused() finds the audio
// thread no longer uses it.
class CuePlayer final
{
public:
    static constexpr double fadeSeconds = 0.005;

    struct State
    {
        bool playing = false;
        uint64_t tag = 0;      // the tag given to play()
        double position = 0.0; // seconds into the track
    };

    CuePlayer();
    ~CuePlayer();

    // Plays [startSeconds, endSeconds) of `audio` (to the end if endSeconds
    // is not after startSeconds), replacing whatever is playing. `tag` comes
    // back in state(). Returns false if no device is running.
    bool play (std::shared_ptr<const DecodedAudio> audio,
               double startSeconds,
               double endSeconds,
               uint64_t tag,
               CueMode mode = CueMode::once);
    void stop();

    // Changes what the audition playing now does at the end of its stretch.
    void setMode (CueMode mode);

    void setGain (float linearGain) { gain.store (linearGain); }
    float getGain() const { return gain.load(); }

    State state() const;
    bool isDeviceRunning() const { return deviceRunning.load(); }

    // Frees decoded audio the audio thread has finished with.
    void releaseUnused();

    // Audio thread: before the device starts, each block (writing the Cue
    // bus, gain applied, over whatever is in left and right), and after it stops.
    void prepare (double deviceSampleRate, int maxBlockSize);
    void render (float* left, float* right, int numSamples);
    void stopped();

private:
    struct Command
    {
        enum class Type
        {
            play,
            stop,
            setMode
        };
        Type type = Type::stop;
        CueMode mode = CueMode::once;
        const DecodedAudio* audio = nullptr;
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        uint64_t tag = 0;
        uint64_t serial = 0;
    };

    struct Voice
    {
        const DecodedAudio* audio = nullptr;
        int64_t readIndex = 0;  // next source frame the interpolators read
        int64_t endIndex = 0;   // where it fades out and stops
        int64_t rangeStart = 0; // the stretch it auditions
        int64_t rangeEnd = 0;
        CueMode mode = CueMode::once;
        float fade = 0.0f;     // current fade gain
        float fadeTarget = 0.0f;
        uint64_t tag = 0;
        std::array<juce::LagrangeInterpolator, 2> interpolators;

        bool active() const { return audio != nullptr; }
    };

    void push (const Command& command);
    void handleCommands();
    void startVoice (const DecodedAudio* audio, int64_t start, int64_t end, CueMode mode, uint64_t tag);
    void applyMode (Voice& voice, CueMode mode);
    int samplesUntilLoop (const Voice& voice) const;
    int64_t fadeFrames (const DecodedAudio& audio) const; // source frames in one fade
    void renderVoice (Voice& voice, float* left, float* right, int numSamples);
    void publishState();

    static constexpr int commandCapacity = 64;
    std::array<Command, commandCapacity> commands {};
    juce::AbstractFifo fifo { commandCapacity };

    // Audio thread only.
    std::array<Voice, 2> voices; // the current one, and one fading out after a switch
    int current = 0;
    juce::AudioBuffer<float> scratch;
    double rate = 0.0;

    // Shared.
    std::atomic<bool> deviceRunning { false };
    std::atomic<float> gain { 1.0f };

    std::atomic<bool> stateIsPlaying { false };
    std::atomic<uint64_t> stateTag { 0 };
    std::atomic<double> statePosition { 0.0 };
    std::array<std::atomic<const DecodedAudio*>, 2> inUse {};
    std::atomic<uint64_t> lastConsumed { 0 };

    // Message thread only.
    uint64_t nextSerial = 1;
    struct Owned
    {
        uint64_t serial;
        std::shared_ptr<const DecodedAudio> audio;
    };
    std::vector<Owned> owned;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CuePlayer)
};

} // namespace djmix
