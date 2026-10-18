#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "PlaybackSnapshot.h"

namespace djmix
{

// Plays the arrangement on the Master bus from the latest PlaybackSnapshot,
// with a transport: play, pause and seek, the playhead kept in beats.
//
// A new snapshot takes over mid-playback with a short crossfade from the old
// one, so an edit ahead of (or under) the playhead never clicks; a change of
// tempo keeps the playhead on the same beat. Starting, pausing and seeking
// fade over the same few milliseconds. Playback starts one fade early, so the
// beat it starts on plays at full level.
//
// Threads: publish, the transport calls and releaseUnused are for the message
// thread; transport commands reach the audio thread through a lock-free FIFO
// and snapshots through an atomic pointer. The audio thread (the AudioEngine,
// or an offline render) calls prepare, render and stopped, and never
// allocates, locks or frees: snapshots are freed in releaseUnused(), on the
// message thread, once the audio thread has finished with them.
class MasterPlayer final
{
public:
    static constexpr double fadeSeconds = 0.005;

    struct State
    {
        bool playing = false;
        double beat = 0.0; // the playhead
    };

    MasterPlayer();
    ~MasterPlayer();

    // Plays this from now on. Its sample rate should be the device's; one at
    // another rate stays silent until a snapshot at the right rate replaces it.
    void publish (std::shared_ptr<PlaybackSnapshot> snapshot);
    const PlaybackSnapshot* latest() const { return newest.get(); }

    void play();
    void pause();
    void seek (double beat);

    State state() const;
    double deviceSampleRate() const { return deviceRate.load(); }
    bool isDeviceRunning() const { return deviceRunning.load(); }

    // Frees snapshots (and the renders only they held) that the audio thread
    // has finished with.
    void releaseUnused();
    size_t numHeldSnapshots() const { return held.size(); }

    // Audio thread: before the device starts, each block (writing the Master
    // bus over whatever is in left and right), and after it stops.
    void prepare (double sampleRate, int maxBlockSize);
    void render (float* left, float* right, int numSamples);
    void stopped();

private:
    struct Command
    {
        enum class Type
        {
            play,
            pause,
            seek
        };
        Type type = Type::pause;
        double beat = 0.0;
    };

    void push (const Command& command);
    void handleCommands();
    void takeLatest();
    double samplesPerBeat() const;
    void mix (const PlaybackSnapshot& snapshot, int64_t from, float* left, float* right, int numSamples);
    void publishState();

    static constexpr int commandCapacity = 64;
    std::array<Command, commandCapacity> commands {};
    juce::AbstractFifo fifo { commandCapacity };

    // The newest snapshot published, for the audio thread to load.
    std::atomic<const PlaybackSnapshot*> current { nullptr };

    // Audio thread only.
    const PlaybackSnapshot* now = nullptr;  // the snapshot playing
    const PlaybackSnapshot* prev = nullptr; // the one fading out, after a swap
    int64_t position = 0;     // the playhead, in samples at positionTempo
    int64_t prevPosition = 0; // the same moment, at prev's tempo
    int crossfadeLeft = 0;    // samples until prev is gone
    double positionTempo = 120.0; // the tempo `position` is counted at
    bool playing = false;
    float fade = 0.0f;
    float fadeTarget = 0.0f;
    bool seekPending = false;
    double seekBeat = 0.0;
    double rate = 0.0;
    int fadeLength = 1;
    juce::AudioBuffer<float> scratch; // now and prev, stereo each

    // Shared.
    std::atomic<double> deviceRate { 0.0 };
    std::atomic<bool> deviceRunning { false };
    std::atomic<bool> statePlaying { false };
    std::atomic<double> stateBeat { 0.0 };
    std::atomic<uint64_t> oldestInUse { 0 }; // the lowest serial the audio thread holds; 0 for none
    std::atomic<uint64_t> blocksDone { 0 };

    // Message thread only.
    struct Held
    {
        std::shared_ptr<const PlaybackSnapshot> snapshot;
        uint64_t replacedAfterBlock = 0; // blocksDone just after a newer one was published
    };
    std::shared_ptr<const PlaybackSnapshot> newest;
    std::vector<Held> held;
    uint64_t nextSerial = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterPlayer)
};

} // namespace djmix
