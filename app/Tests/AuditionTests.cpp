#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

#include "PostQueue.h"
#include "analysis/AnalyzerProtocol.h"
#include "engine/AuditionController.h"
#include "engine/TrackDecoder.h"
#include "library/LibraryDatabase.h"

using Catch::Approx;
using djmix::test::PostQueue;
using djmix::AuditionController;
using djmix::CuePlayer;
using djmix::TrackDecoder;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("djmix-audition", "", false);

    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }

    fs::path path (const char* name) const { return djmix::pathFromUtf8 (dir.getChildFile (name).getFullPathName().toStdString()); }
};

// A WAV of a sine at `hz` on the left and silence on the right (or the same
// on both for mono).
fs::path writeWav (const TempDir& temp, const char* name, double rate, double seconds, int channels, double hz = 440.0)
{
    const auto path = temp.path (name);
    const juce::File file (juce::String::fromUTF8 (djmix::pathToUtf8 (path).c_str()));
    juce::AudioBuffer<float> buffer (channels, static_cast<int> (rate * seconds));
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample (0, i, static_cast<float> (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / rate)));

    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    REQUIRE (stream != nullptr);
    std::unique_ptr<juce::OutputStream> out (stream.release());
    auto writer = wav.createWriterFor (out,
                                       juce::AudioFormatWriterOptions {}
                                           .withSampleRate (rate)
                                           .withNumChannels (channels)
                                           .withBitsPerSample (24));
    REQUIRE (writer != nullptr);
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return path;
}

void renderBlock (CuePlayer& player, int samples = 512)
{
    std::vector<float> data (static_cast<size_t> (2 * samples));
    player.render (data.data(), data.data() + samples, samples);
}
} // namespace

TEST_CASE ("TrackDecoder reads WAV files into stereo at their own rate", "[audio][decoder]")
{
    TempDir temp;
    const auto stereo = writeWav (temp, "stereo.wav", 48000.0, 0.5, 2);
    std::string error;
    auto audio = TrackDecoder::decode (stereo, error);
    INFO (error);
    REQUIRE (audio != nullptr);
    CHECK (audio->sampleRate == 48000.0);
    CHECK (audio->samples.getNumChannels() == 2);
    CHECK (audio->samples.getNumSamples() == 24000);
    CHECK (audio->samples.getSample (0, 12) == Approx (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * 12 / 48000.0)).margin (1e-4));
    CHECK (audio->samples.getMagnitude (1, 0, 24000) == 0.0f);

    // Mono plays on both sides.
    const auto mono = writeWav (temp, "mono.wav", 44100.0, 0.25, 1);
    audio = TrackDecoder::decode (mono, error);
    REQUIRE (audio != nullptr);
    CHECK (audio->samples.getSample (1, 100) == audio->samples.getSample (0, 100));
    CHECK (audio->samples.getMagnitude (1, 0, audio->samples.getNumSamples()) > 0.4f);
}

TEST_CASE ("TrackDecoder explains files it cannot read", "[audio][decoder]")
{
    TempDir temp;
    std::string error;
    CHECK (TrackDecoder::decode (temp.path ("missing.wav"), error) == nullptr);
    CHECK (error == "the file is missing");

    const auto text = temp.path ("notes.mp3");
    juce::File (juce::String::fromUTF8 (djmix::pathToUtf8 (text).c_str())).replaceWithText ("not audio");
    CHECK (TrackDecoder::decode (text, error) == nullptr);
    CHECK_FALSE (error.empty());
}

TEST_CASE ("TrackDecoder falls back to ffmpeg", "[audio][decoder][ffmpeg]")
{
    const auto ffmpeg = djmix::findOnPath ("ffmpeg");
    if (! ffmpeg)
        SKIP ("ffmpeg is not on PATH");

    TempDir temp;
    const auto wav = writeWav (temp, "tone.wav", 48000.0, 0.5, 2);
    std::string error;
    const auto audio = TrackDecoder::decodeWithFfmpeg (wav, *ffmpeg, error);
    INFO (error);
    REQUIRE (audio != nullptr);
    CHECK (audio->sampleRate == 44100.0);
    CHECK (audio->samples.getNumSamples() == Approx (22050).margin (64));
    CHECK (audio->samples.getMagnitude (0, 0, audio->samples.getNumSamples()) == Approx (0.5f).margin (0.02));
}

TEST_CASE ("Auditions decode in the background and then play", "[audio][audition]")
{
    TempDir temp;
    const auto a = writeWav (temp, "a.wav", 44100.0, 2.0, 2);
    const auto b = writeWav (temp, "b.wav", 44100.0, 2.0, 2, 880.0);

    CuePlayer player;
    player.prepare (44100.0, 512);
    PostQueue queue;
    AuditionController audition (player, queue.poster());

    audition.play (1, a, 0.5, 1.0);
    auto status = audition.status();
    CHECK (status.loading);
    CHECK (status.trackId == 1);
    CHECK_FALSE (status.playing);

    REQUIRE (queue.runNext()); // decoded: starts playing
    renderBlock (player);
    status = audition.status();
    CHECK_FALSE (status.loading);
    CHECK (status.playing);
    CHECK (status.start == 0.5);
    CHECK (status.end == 1.0);
    // Playback starts one fade early, so the cue's first beat is at full level.
    CHECK (status.position == Approx (0.5 - CuePlayer::fadeSeconds + 512 / 44100.0).margin (0.001));

    // A decoded track plays at once, without a background trip.
    audition.play (1, a, 1.0, 1.5);
    renderBlock (player);
    status = audition.status();
    CHECK (status.playing);
    CHECK (status.start == 1.0);

    // Prepared ahead: playing it later needs no decode either.
    audition.prepare (2, b);
    REQUIRE (queue.runNext());
    audition.play (2, b, 0.0, 0.0);
    renderBlock (player);
    status = audition.status();
    CHECK (status.trackId == 2);
    CHECK (status.playing);
    CHECK (audition.cachedFiles() == std::vector<fs::path> { b, a });

    // The cue mode applies to new auditions and to the one playing.
    audition.setMode (djmix::CueMode::loop);
    CHECK (audition.mode() == djmix::CueMode::loop);
    audition.play (1, a, 0.5, 0.6);
    for (int i = 0; i < 40; ++i) // 0.46 s: several times round 0.1 s
        renderBlock (player);
    status = audition.status();
    CHECK (status.playing);
    CHECK (status.position < 0.6);

    audition.setMode (djmix::CueMode::continueTrack);
    for (int i = 0; i < 20; ++i)
        renderBlock (player);
    CHECK (audition.status().position > 0.6);

    audition.stop();
    renderBlock (player, 4096);
    CHECK_FALSE (audition.status().playing);
}

TEST_CASE ("An audition that cannot play says why", "[audio][audition]")
{
    TempDir temp;
    CuePlayer player;
    player.prepare (44100.0, 512);
    PostQueue queue;
    AuditionController audition (player, queue.poster());

    audition.play (3, temp.path ("gone.wav"), 0.0, 0.0);
    REQUIRE (queue.runNext());
    const auto status = audition.status();
    CHECK_FALSE (status.loading);
    CHECK_FALSE (status.playing);
    CHECK (status.error.find ("the file is missing") != std::string::npos);

    // No device running.
    const auto wav = writeWav (temp, "c.wav", 44100.0, 0.5, 2);
    CuePlayer silent;
    AuditionController noDevice (silent, queue.poster());
    noDevice.play (4, wav, 0.0, 0.0);
    REQUIRE (queue.runNext());
    CHECK (noDevice.status().error.find ("No audio output") != std::string::npos);
}
