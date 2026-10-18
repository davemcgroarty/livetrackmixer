#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/TrackView.h"

using Catch::Approx;
using djmix::TrackView;

namespace
{
// A six-minute track at 124 BPM with a waveform at djanalyze's resolution,
// 8-bar phrases and a new section every 32 bars.
struct LongTrack
{
    djmix::TrackAnalysis analysis;
    djmix::WaveformOverview waveform;

    LongTrack()
    {
        auto& a = analysis;
        a.durationSec = 360.0;
        a.bpm = 124.0;
        a.beatConfidence = 0.7;
        a.key = { "A minor", "8A", 0.8 };
        const double period = 60.0 / a.bpm, first = 0.1;
        for (double t = first; t < a.durationSec; t += period)
            a.beats.push_back (t);
        const int numBars = static_cast<int> (a.beats.size() / 4);
        for (int b = 0; b < numBars; ++b)
        {
            const double phase = b / 32.0;
            a.bars.push_back ({ b + 1, a.beats[static_cast<size_t> (b * 4)], b * 4 });
            a.downbeats.push_back (a.bars.back().start);
            a.descriptorsPerBar.push_back ({ 0.5 + 0.5 * std::sin (phase * 3.0), 0.5 + 0.5 * std::cos (phase),
                                             0.3 + 0.2 * std::sin (phase * 7.0), 0.4 });
            if (b % 8 == 0)
                a.phrases.push_back ({ b / 8 + 1, b + 1, 8, b / 32 });
        }
        for (int s = 0; s * 32 < numBars; ++s)
        {
            const int startBar = s * 32 + 1;
            const int length = std::min (32, numBars - s * 32);
            const double start = a.bars[static_cast<size_t> (startBar - 1)].start;
            const auto endIndex = static_cast<size_t> (startBar - 1 + length);
            const double end = endIndex < a.bars.size() ? a.bars[endIndex].start : a.durationSec;
            a.sections.push_back ({ start, end, startBar, length, std::nullopt });
            a.descriptorsPerSection.push_back ({ 0.6, 0.5, 0.4, 0.3 });
        }

        waveform.sampleRate = 44100;
        waveform.samplesPerPoint = 256;
        const auto points = static_cast<size_t> (a.durationSec * 44100 / 256);
        for (size_t i = 0; i < points; ++i)
        {
            const double t = static_cast<double> (i) * waveform.secondsPerPoint();
            const double beatPhase = std::fmod (t - first, period) / period;
            const auto level = static_cast<float> (0.3 + 0.6 * std::exp (-beatPhase * 6.0));
            waveform.peak.push_back (level);
            waveform.rms.push_back (level * 0.5f);
        }
    }
};

double millisecondsPerPaint (TrackView& view, juce::Image& image, int frames)
{
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i)
    {
        juce::Graphics g (image);
        view.paintEntireComponent (g, true);
    }
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count() / frames;
}

// With DJMIX_SNAPSHOT_DIR set, the test also saves what it drew there.
void saveSnapshot (const juce::Image& image, const char* name)
{
    const char* dir = std::getenv ("DJMIX_SNAPSHOT_DIR");
    if (dir == nullptr)
        return;
    const auto file = juce::File (dir).getChildFile (name);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
}
} // namespace

TEST_CASE ("The Track view zooms from the whole track to beats", "[ui][trackview]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const LongTrack track;

    TrackView view;
    view.setSize (1600, 400);
    REQUIRE_FALSE (view.hasAnalysis());

    view.showAnalysis (track.analysis, track.waveform, juce::Colour (0xff4f9fe0), "Long Track", "Artist");
    REQUIRE (view.hasAnalysis());
    CHECK (view.viewRange().start() == 0.0);
    CHECK (view.viewRange().span() == Approx (360.0));

    // + zooms in around the middle, - back out, Home shows everything.
    const double middle = view.viewRange().start() + 0.5 * view.viewRange().span();
    CHECK (view.keyPressed (juce::KeyPress ('+', {}, '+')));
    CHECK (view.viewRange().span() == Approx (240.0));
    CHECK (view.viewRange().start() + 0.5 * view.viewRange().span() == Approx (middle).margin (1.0));
    CHECK (view.keyPressed (juce::KeyPress ('-', {}, '-')));
    CHECK (view.viewRange().span() == Approx (360.0));
    view.showRange (100.0, 10.0);
    CHECK (view.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (view.viewRange().span() == Approx (360.0));

    // No closer than two beats.
    view.showRange (100.0, 0.01);
    CHECK (view.viewRange().span() == Approx (2.0 * 60.0 / 124.0).epsilon (0.01));
}

TEST_CASE ("The Track view draws a six-minute track within a 60 fps frame", "[ui][trackview][performance]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const LongTrack track;

    TrackView view;
    view.setSize (1600, 400);
    view.showAnalysis (track.analysis, track.waveform, juce::Colour (0xffe0884f), "Long Track", "Artist");
    juce::Image image (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);

    struct Zoom
    {
        const char* name;
        double start, span;
    };
    const Zoom zooms[] = {
        { "whole", 0.0, 360.0 },
        { "phrase", 120.0, 16.0 },
        { "bar", 150.0, 2.0 },
        { "beats", 200.0, 1.0 },
    };

    for (const auto& zoom : zooms)
    {
        view.showRange (zoom.start, zoom.span);
        millisecondsPerPaint (view, image, 2); // warm up
        const double ms = millisecondsPerPaint (view, image, 20);
        WARN ("Track view, " << zoom.name << ": " << ms << " ms per frame");
        saveSnapshot (image, (std::string ("trackview-") + zoom.name + ".png").c_str());

        // A frame at 60 fps is 16.7 ms. Shared CI machines are slower and
        // noisier than a desktop, so this only catches gross regressions.
        CHECK (ms < 50.0);
    }

    // An edited track draws as quickly as an unedited one.
    view.setOverrides (djmix::edits::splitSection (track.analysis, djmix::edits::nudge ({}, 0.01), 100.0));
    view.showRange (0.0, 360.0);
    const double ms = millisecondsPerPaint (view, image, 20);
    WARN ("Track view, edited: " << ms << " ms per frame");
    CHECK (ms < 50.0);
}

TEST_CASE ("The Track view explains a track it cannot show", "[ui][trackview]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    TrackView view;
    view.setSize (800, 300);

    djmix::TrackRecord pending;
    pending.id = 1;
    pending.title = "Pending";
    view.showTrack (&pending);
    CHECK_FALSE (view.hasAnalysis());

    djmix::TrackRecord missing = pending;
    missing.status = djmix::AnalysisStatus::ready;
    missing.analysisFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("djmix-no-such-analysis.json")
                               .getFullPathName()
                               .toStdString();
    view.showTrack (&missing);
    CHECK_FALSE (view.hasAnalysis());

    view.showTrack (nullptr);
    CHECK_FALSE (view.hasAnalysis());
}

TEST_CASE ("The Track view edits the grid and hands the edit on to save", "[ui][trackview][edit]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const LongTrack track;
    TrackView view;
    view.setSize (1600, 400);
    view.showAnalysis (track.analysis, track.waveform, juce::Colour (0xff4f9fe0), "Long Track", "Artist");

    std::optional<djmix::TrackOverrides> saved;
    int edits = 0;
    view.onEditOverrides = [&] (const djmix::TrackOverrides& o, bool)
    {
        saved = o;
        ++edits;
    };

    // Alt+Right nudges the grid 5 ms later; Shift+Alt+Left 1 ms earlier.
    CHECK (view.keyPressed (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)));
    REQUIRE (saved);
    CHECK (saved->grid.offsetSeconds == Approx (0.005));
    CHECK (view.effective().beats.front() == Approx (track.analysis.beats.front() + 0.005));
    CHECK (view.keyPressed (juce::KeyPress (juce::KeyPress::leftKey,
                                            juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier, 0)));
    CHECK (saved->grid.offsetSeconds == Approx (0.004));
    CHECK (edits == 2);

    // Plain arrows still scroll, and edit nothing.
    view.showRange (100.0, 10.0);
    CHECK (view.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (view.viewRange().start() > 100.0);
    CHECK (edits == 2);

    // Overrides from the library (say, an undo) replace the view's own.
    djmix::TrackOverrides fromLibrary;
    fromLibrary.grid.bpm = 125.0;
    view.setOverrides (fromLibrary);
    CHECK (view.effective().bpm == 125.0);
    view.setOverrides ({});
    CHECK (view.effective().beats == track.analysis.beats);

}
