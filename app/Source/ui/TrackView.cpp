#include "TrackView.h"

#include <algorithm>
#include <cmath>

#include "analysis/AnalysisLoader.h"
#include "library/TrackColours.h"

namespace djmix
{

namespace
{
const juce::Colour background { 0xff23282d };
const juce::Colour waveColour { 0xffe8ecef };
const juce::Colour textColour { 0xffd8dde2 };
const juce::Colour dimText { 0xff8a939c };
const juce::Colour warningColour { 0xffe0b54f };

constexpr int headerHeight = 28;
constexpr int stripHeight = 14;
constexpr int scrollBarHeight = 10;
constexpr double minSpanBeats = 2.0;
constexpr double wheelZoomSpeed = 2.5;
constexpr double beatConfidenceWarning = 0.5;
constexpr int minCellGap = 12; // bar cells narrower than this touch

juce::String fromUtf8 (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

// Graphics::fillRectList turns whole-pixel rectangles into an anti-aliased
// edge table; filling them one by one keeps to the renderer's integer path,
// which is many times faster for the thousands a frame has.
void fillEach (juce::Graphics& g, const juce::RectangleList<int>& rects)
{
    for (const auto& r : rects)
        g.fillRect (r);
}

// The first index in sorted `times` at or after `t`.
size_t firstAtOrAfter (const std::vector<double>& times, double t)
{
    return static_cast<size_t> (std::distance (times.begin(), std::lower_bound (times.begin(), times.end(), t)));
}
} // namespace

TrackView::TrackView()
{
    setWantsKeyboardFocus (true);
    setOpaque (false);

    addChildComponent (scrollBar);
    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);
    scrollBar.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff5a646e));
    scrollBar.setColour (juce::ScrollBar::trackColourId, juce::Colour (0xff1e2226));
}

TrackView::~TrackView()
{
    scrollBar.removeListener (this);
}

void TrackView::showTrack (const TrackRecord* track)
{
    if (track == nullptr)
    {
        shown.reset();
        title = artist = status = {};
        showMessage ("Select a track in the Library");
        return;
    }

    const Shown next { track->id, track->analysisFile, track->analyzerVersion, track->status, track->colour };
    if (shown && *shown == next)
        return;

    const bool sameTrack = shown && shown->id == next.id;
    const bool sameAnalysis = sameTrack && loaded && shown->analysisFile == next.analysisFile
                              && shown->analyzerVersion == next.analyzerVersion;
    shown = next;

    // While a track is re-analysed its previous analysis stays on screen.
    switch (track->status)
    {
        case AnalysisStatus::pending: status = "Waiting to re-analyse"; break;
        case AnalysisStatus::analysing: status = "Re-analysing..."; break;
        case AnalysisStatus::failed: status = "Analysis failed"; break;
        case AnalysisStatus::ready: status = {}; break;
    }

    colour = juce::Colour (trackPalette[static_cast<size_t> (std::clamp (track->colour, 0, int (trackPalette.size()) - 1))].argb);
    title = fromUtf8 (track->title);
    artist = fromUtf8 (track->artist);

    if (sameAnalysis)
    {
        repaint();
        return;
    }

    std::error_code missing;
    if (track->analysisFile.empty() || ! std::filesystem::exists (track->analysisFile, missing))
    {
        if (track->status == AnalysisStatus::failed)
            showMessage ("Analysis failed: " + fromUtf8 (track->error));
        else
            showMessage (track->status == AnalysisStatus::analysing ? "Analysing..." : "Waiting for analysis");
        return;
    }

    const auto result = AnalysisLoader::fromFile (track->analysisFile);
    if (const auto* error = std::get_if<AnalysisLoadError> (&result))
    {
        showMessage ("The analysis could not be read: " + fromUtf8 (error->message));
        return;
    }
    const auto& analysis = std::get<TrackAnalysis> (result);

    std::string waveformError;
    const auto waveform = WaveformFile::read (track->analysisFile.parent_path() / analysis.waveformFile, &waveformError);
    if (! waveform)
    {
        showMessage ("The waveform could not be read: " + fromUtf8 (waveformError));
        return;
    }

    const auto keep = sameTrack && loaded ? std::optional<ViewRange> (range) : std::nullopt;
    const auto keepStatus = status;
    showAnalysis (analysis, *waveform, colour, title, artist);
    status = keepStatus;
    if (keep)
        showRange (keep->start(), keep->span());
}

void TrackView::showAnalysis (const TrackAnalysis& analysis,
                              const WaveformOverview& waveform,
                              juce::Colour trackColour,
                              const juce::String& trackTitle,
                              const juce::String& trackArtist)
{
    structure = TrackStructure (analysis);
    pyramid = WaveformPyramid (waveform);
    colour = trackColour;
    title = trackTitle;
    artist = trackArtist;
    status = {};
    loaded = true;
    hover.reset();

    const double duration = analysis.durationSec > 0.0 ? analysis.durationSec : pyramid.duration();
    range = ViewRange (duration, minSpanBeats * structure.beatPeriod());
    range.setWidth (waveArea.getWidth());
    scrollBar.setVisible (true);
    rangeChanged();
}

void TrackView::showMessage (const juce::String& text)
{
    loaded = false;
    message = text;
    structure = {};
    pyramid = {};
    hover.reset();
    scrollBar.setVisible (false);
    repaint();
}

void TrackView::showRange (double start, double span)
{
    range.show (start, span);
    rangeChanged();
}

void TrackView::resized()
{
    auto area = getLocalBounds().reduced (1);
    headerArea = area.removeFromTop (headerHeight);
    scrollBar.setBounds (area.removeFromBottom (scrollBarHeight).reduced (2, 1));
    stripArea = area.removeFromTop (stripHeight);
    waveArea = area;
    range.setWidth (waveArea.getWidth());
    rangeChanged();
}

void TrackView::rangeChanged()
{
    if (loaded)
    {
        scrollBar.setRangeLimits (0.0, range.duration(), juce::dontSendNotification);
        scrollBar.setCurrentRange (range.start(), range.span(), juce::dontSendNotification);
    }
    repaint();
}

void TrackView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    range.show (newRangeStart, range.span());
    repaint();
}

void TrackView::zoomAt (float x, double factor)
{
    range.zoomAt (x - static_cast<float> (waveArea.getX()), factor);
    rangeChanged();
}

float TrackView::timeToX (double seconds) const
{
    return static_cast<float> (waveArea.getX() + range.timeToX (seconds));
}

//==============================================================================
void TrackView::paint (juce::Graphics& g)
{
    g.setColour (background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    paintHeader (g, headerArea);
    if (! loaded)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (15.0f);
        g.drawFittedText (message, waveArea.reduced (12), juce::Justification::centred, 3);
        return;
    }

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (waveArea);
        g.setColour (juce::Colour (0xff1b1f23));
        g.fillRect (waveArea);
        paintBars (g, waveArea);
        paintWaveform (g, waveArea);
        paintGrid (g, waveArea);
    }
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (stripArea);
        paintSectionStrip (g, stripArea);
    }
    paintAudition (g);
    paintHover (g);
}

void TrackView::paintAudition (juce::Graphics& g)
{
    if (! audition || audition->error.isNotEmpty())
        return;

    const auto& a = *audition;
    const double end = a.end > a.start ? a.end : structure.data().durationSec;
    const float x0 = std::max (timeToX (a.start), static_cast<float> (waveArea.getX()));
    const float x1 = std::min (timeToX (end), static_cast<float> (waveArea.getRight()));
    if (x1 > x0)
    {
        // The stretch being auditioned: lit in the strip, underlined below it.
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x0, static_cast<float> (stripArea.getY()) + 2.0f, x1 - x0,
                                            static_cast<float> (stripArea.getHeight()) - 4.0f));
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (x0, static_cast<float> (waveArea.getY()), x1 - x0, 2.0f));
    }

    if (a.playing)
    {
        const float x = timeToX (a.position);
        if (x >= static_cast<float> (waveArea.getX()) && x <= static_cast<float> (waveArea.getRight()))
        {
            g.setColour (juce::Colours::white);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, static_cast<float> (stripArea.getY()), 2.0f,
                                                static_cast<float> (waveArea.getBottom() - stripArea.getY())));
        }
    }
}

void TrackView::paintHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    area = area.reduced (8, 0);
    if (shown || loaded)
    {
        g.setColour (colour);
        g.fillRoundedRectangle (area.removeFromLeft (6).withSizeKeepingCentre (6, 14).toFloat(), 2.0f);
        area.removeFromLeft (8);
    }

    juce::String details;
    if (loaded)
    {
        const auto& a = structure.data();
        details << juce::String (a.bpm, 2) << " BPM";
        if (a.tempoVariable)
            details << " (variable)";
        if (! a.key.camelot.empty())
            details << "   " << fromUtf8 (a.key.camelot) << "  " << fromUtf8 (a.key.name);
    }

    juce::String warning = status;
    if (loaded && warning.isEmpty() && structure.data().beatConfidence < beatConfidenceWarning)
        warning = "Check beats";

    g.setFont (13.0f);

    // What Cue is doing with this track, rightmost.
    stopButtonArea = {};
    if (audition)
    {
        if (audition->error.isNotEmpty())
        {
            warning = audition->error;
        }
        else if (audition->loading || audition->playing)
        {
            const juce::String label = audition->loading ? "Loading..." : "Stop cue";
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 30;
            auto button = area.removeFromRight (w).withSizeKeepingCentre (w, 20);
            area.removeFromRight (10);
            g.setColour (juce::Colours::white.withAlpha (audition->playing ? 0.16f : 0.08f));
            g.fillRoundedRectangle (button.toFloat(), 4.0f);
            g.setColour (textColour);
            if (audition->playing)
            {
                g.fillRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ static_cast<float> (button.getX()) + 13.0f,
                                                                             static_cast<float> (button.getCentreY()) }));
                stopButtonArea = button;
            }
            g.drawText (label, button.withTrimmedLeft (22), juce::Justification::centredLeft);
        }
    }

    if (warning.isNotEmpty())
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), warning) + 12;
        g.setColour (warningColour);
        g.drawText (warning, area.removeFromRight (w), juce::Justification::centredRight);
    }
    if (details.isNotEmpty())
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), details) + 16;
        g.setColour (dimText);
        g.drawText (details, area.removeFromRight (w), juce::Justification::centredRight);
    }

    g.setFont (15.0f);
    g.setColour (textColour);
    juce::String name = title.isNotEmpty() ? title : juce::String ("Track view");
    if (artist.isNotEmpty())
        name << "  -  " << artist;
    g.drawText (name, area, juce::Justification::centredLeft, true);
}

void TrackView::paintSectionStrip (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& sections = structure.data().sections;
    const auto top = static_cast<float> (area.getY()) + 2.0f;
    const auto height = static_cast<float> (area.getHeight()) - 4.0f;
    const int hovered = hover && stripArea.contains (hover->toInt()) ? structure.sectionAt (range.xToTime (hover->x - waveArea.getX())) : -1;

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        if (s.end < range.start() || s.start > range.end())
            continue;
        const float x0 = timeToX (s.start), x1 = timeToX (s.end);
        const float alpha = static_cast<int> (i) == hovered ? 0.55f : (i % 2 == 0 ? 0.22f : 0.38f);
        g.setColour (colour.withAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (x0, top, x1 - x0, height));
        g.setColour (background);
        g.fillRect (juce::Rectangle<float> (x0, top, 1.5f, height));
    }
}

void TrackView::paintBars (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& a = structure.data();
    const int numBars = structure.numBars();
    if (numBars == 0)
        return;

    const int first = std::max (0, structure.barAt (range.start())); // 0 before the first bar too

    // Whole-pixel rectangles: see fillEach.
    juce::RectangleList<int> shadeEven, shadeOdd, bass, high;
    const int top = area.getY(), height = area.getHeight(), bottom = area.getBottom();
    for (int bar = first; bar < numBars; ++bar)
    {
        const double start = structure.barStart (bar);
        if (start > range.end())
            break;
        const int x0 = juce::roundToInt (timeToX (start));
        const int x1 = juce::roundToInt (timeToX (structure.barEnd (bar)));
        const int w = std::max (x1 - x0 - (x1 - x0 > minCellGap ? 1 : 0), 1); // a hairline gap between wide cells

        const int section = structure.sectionAt (start);
        (section % 2 == 0 ? shadeEven : shadeOdd).addWithoutMerging ({ x0, top, w, height });

        if (static_cast<size_t> (bar) < a.descriptorsPerBar.size())
        {
            const auto& d = a.descriptorsPerBar[static_cast<size_t> (bar)];
            const int bassHeight = juce::roundToInt (std::clamp (d.bass, 0.0, 1.0) * height * 0.5);
            const int highHeight = juce::roundToInt (std::clamp (d.high, 0.0, 1.0) * height * 0.5);
            bass.addWithoutMerging ({ x0, bottom - bassHeight, w, bassHeight });
            high.addWithoutMerging ({ x0, top, w, highHeight });
        }
    }

    g.setColour (colour.withAlpha (0.07f));
    fillEach (g, shadeEven);
    g.setColour (colour.withAlpha (0.13f));
    fillEach (g, shadeOdd);
    g.setColour (colour.withAlpha (0.34f));
    fillEach (g, bass);
    g.setColour (colour.withAlpha (0.2f));
    fillEach (g, high);
}

void TrackView::paintWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (pyramid.empty())
        return;

    const auto& descriptors = structure.data().descriptorsPerBar;
    const int width = area.getWidth();
    const double secondsPerPixel = range.span() / range.pixelWidth();
    const int centre = area.getCentreY();
    // Scaled so the loudest peak fills the height, whatever the master level.
    const float scale = 1.0f / std::max (pyramid.maxPeak(), 0.05f);
    const float half = static_cast<float> (area.getHeight()) * 0.5f - 2.0f;

    // Columns are batched by bar, since a bar's rhythm score sets their contrast.
    // Whole pixels, as in paintBars.
    juce::RectangleList<int> peaks, rmses;
    int runBar = -2;
    auto flush = [&]
    {
        if (peaks.isEmpty() && rmses.isEmpty())
            return;
        double rhythm = 0.5;
        if (runBar >= 0 && static_cast<size_t> (runBar) < descriptors.size())
            rhythm = std::clamp (descriptors[static_cast<size_t> (runBar)].rhythm, 0.0, 1.0);
        const auto alpha = static_cast<float> (0.25 + 0.75 * rhythm);
        g.setColour (waveColour.withAlpha (alpha * 0.45f));
        fillEach (g, peaks);
        g.setColour (waveColour.withAlpha (alpha));
        fillEach (g, rmses);
        peaks.clear();
        rmses.clear();
    };

    const double trackEnd = pyramid.duration();
    for (int x = 0; x < width; ++x)
    {
        const double t0 = range.xToTime (x), t1 = t0 + secondsPerPixel;
        if (t0 >= trackEnd)
            break;
        const int bar = structure.barAt (0.5 * (t0 + t1));
        if (bar != runBar)
        {
            flush();
            runBar = bar;
        }
        const auto column = pyramid.column (t0, t1, secondsPerPixel);
        const int peak = std::max (juce::roundToInt (std::min (column.peak * scale, 1.0f) * half), 1);
        const int rms = juce::roundToInt (std::min (column.rms * scale, 1.0f) * half);
        const int px = area.getX() + x;
        peaks.addWithoutMerging ({ px, centre - peak, 1, 2 * peak });
        if (rms > 0)
            rmses.addWithoutMerging ({ px, centre - rms, 1, 2 * rms });
    }
    flush();
}

void TrackView::paintGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& a = structure.data();
    const auto top = static_cast<float> (area.getY());
    const auto bottom = static_cast<float> (area.getBottom());
    const auto grid = structure.gridAt (range.pixelsPerSecond());

    if (grid.beats)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (size_t i = firstAtOrAfter (a.beats, range.start()); i < a.beats.size() && a.beats[i] <= range.end(); ++i)
            g.fillRect (juce::Rectangle<float> (timeToX (a.beats[i]), top, 1.0f, bottom - top));
    }

    // Bar lines when there is room, phrase lines while phrases are at least a
    // few pixels apart, and section boundaries always.
    const double pixelsPerBar = range.pixelsPerSecond() * structure.beatPeriod() * 4.0;
    const bool phrases = pixelsPerBar * 8.0 >= TrackStructure::minBarSpacing;
    for (int bar = std::max (0, structure.barAt (range.start())); bar < structure.numBars(); ++bar)
    {
        const double start = structure.barStart (bar);
        if (start > range.end())
            break;
        if (start < range.start())
            continue;
        const float x = timeToX (start);
        if (structure.startsSection (bar))
        {
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, top, 2.0f, bottom - top));
        }
        else if (phrases && structure.startsPhrase (bar))
        {
            g.setColour (juce::Colours::white.withAlpha (0.42f));
            g.fillRect (juce::Rectangle<float> (x - 0.75f, top, 1.5f, bottom - top));
        }
        else if (grid.bars)
        {
            g.setColour (juce::Colours::white.withAlpha (0.20f));
            g.fillRect (juce::Rectangle<float> (x, top, 1.0f, bottom - top));
        }
    }

    // Past the end of the track.
    const float end = timeToX (a.durationSec);
    if (end < static_cast<float> (area.getRight()))
    {
        g.setColour (background.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (end, top, static_cast<float> (area.getRight()) - end, bottom - top));
    }
}

void TrackView::paintHover (juce::Graphics& g)
{
    if (! hover || dragging)
        return;

    const auto& a = structure.data();
    const double time = range.xToTime (hover->x - waveArea.getX());
    const bool overStrip = stripArea.contains (hover->toInt());
    const bool overWave = waveArea.contains (hover->toInt());
    if (! overStrip && ! overWave)
        return;

    const ContentScores* scores = nullptr;
    juce::String heading;
    float x0 = 0.0f, x1 = 0.0f;
    if (overStrip)
    {
        const int section = structure.sectionAt (time);
        if (section < 0 || static_cast<size_t> (section) >= a.descriptorsPerSection.size())
            return;
        const auto& s = a.sections[static_cast<size_t> (section)];
        scores = &a.descriptorsPerSection[static_cast<size_t> (section)];
        heading << "Section " << (section + 1) << "  -  bars " << s.startBar << "-" << (s.startBar + s.lengthBars - 1);
        x0 = timeToX (s.start);
        x1 = timeToX (s.end);
    }
    else
    {
        const int bar = structure.barAt (time);
        if (bar < 0 || static_cast<size_t> (bar) >= a.descriptorsPerBar.size())
            return;
        scores = &a.descriptorsPerBar[static_cast<size_t> (bar)];
        heading << "Bar " << (bar + 1);
        if (structure.startsPhrase (bar))
            heading << "  -  phrase start";
        x0 = timeToX (structure.barStart (bar));
        x1 = timeToX (structure.barEnd (bar));
    }

    // Outline what the values belong to.
    const auto outlined = juce::Rectangle<float> (x0, static_cast<float> (stripArea.getY()), std::max (x1 - x0, 1.0f),
                                                  static_cast<float> (waveArea.getBottom() - stripArea.getY()));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawRect (outlined.getIntersection (stripArea.getUnion (waveArea).toFloat()), 1.0f);

    // The box with the four scores, beside the pointer and inside the view.
    const std::pair<const char*, double> rows[] = {
        { "Bass", scores->bass }, { "High", scores->high }, { "Rhythm", scores->rhythm }, { "Melody", scores->melody }
    };
    const int boxWidth = 170, rowHeight = 16, boxHeight = 24 + 4 * rowHeight;
    auto box = juce::Rectangle<int> (boxWidth, boxHeight).withPosition (hover->toInt() + juce::Point<int> (14, 14));
    if (box.getRight() > waveArea.getRight())
        box.setX (juce::roundToInt (hover->x) - 14 - boxWidth);
    if (box.getBottom() > waveArea.getBottom())
        box.setY (waveArea.getBottom() - boxHeight - 2);
    box.setY (std::max (box.getY(), stripArea.getY()));

    g.setColour (juce::Colour (0xf0181b1f));
    g.fillRoundedRectangle (box.toFloat(), 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (box.toFloat(), 4.0f, 1.0f);

    auto inner = box.reduced (8, 4);
    g.setFont (13.0f);
    g.setColour (textColour);
    g.drawText (heading, inner.removeFromTop (20), juce::Justification::centredLeft, true);

    g.setFont (12.0f);
    for (const auto& [label, value] : rows)
    {
        auto row = inner.removeFromTop (rowHeight);
        g.setColour (dimText);
        g.drawText (label, row.removeFromLeft (50), juce::Justification::centredLeft);
        g.drawText (juce::String (value, 2), row.removeFromRight (32), juce::Justification::centredRight);
        const auto meter = row.reduced (4, 5).toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRect (meter);
        g.setColour (colour);
        g.fillRect (meter.withWidth (meter.getWidth() * static_cast<float> (std::clamp (value, 0.0, 1.0))));
    }
}

//==============================================================================
void TrackView::mouseMove (const juce::MouseEvent& e)
{
    if (! loaded)
        return;
    hover = e.position;
    repaint();
}

void TrackView::mouseExit (const juce::MouseEvent&)
{
    hover.reset();
    repaint();
}

void TrackView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! loaded)
        return;
    dragAnchorTime = range.xToTime (e.position.x - waveArea.getX());
    dragging = false;
}

void TrackView::mouseDrag (const juce::MouseEvent& e)
{
    if (! loaded)
        return;
    if (! dragging && e.getDistanceFromDragStart() < 3)
        return;
    dragging = true;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    // Keep the time grabbed under the pointer.
    range.show (dragAnchorTime - (e.position.x - waveArea.getX()) / range.pixelsPerSecond(), range.span());
    hover = e.position;
    rangeChanged();
}

void TrackView::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDrag = dragging;
    dragging = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    hover = e.position;
    repaint();
    if (! wasDrag && e.mouseWasClicked() && ! e.mods.isPopupMenu())
        handleClick (e.position);
}

void TrackView::handleClick (juce::Point<float> position)
{
    if (stopButtonArea.toFloat().contains (position))
    {
        if (onStopAudition)
            onStopAudition();
        return;
    }
    if (! loaded)
        return;

    const double time = range.xToTime (position.x - waveArea.getX());
    if (stripArea.toFloat().contains (position))
    {
        const int section = structure.sectionAt (time);
        if (section >= 0)
        {
            const auto& s = structure.data().sections[static_cast<size_t> (section)];
            requestAudition (s.start, s.end);
        }
    }
    else if (waveArea.toFloat().contains (position))
    {
        const int bar = structure.barAt (time);
        if (bar >= 0)
        {
            const auto [first, last] = structure.phraseAround (bar);
            requestAudition (structure.barStart (first), structure.barEnd (last));
        }
    }
}

void TrackView::requestAudition (double start, double end)
{
    // Clicking what is already playing stops it.
    const bool same = audition && (audition->playing || audition->loading)
                      && std::abs (audition->start - start) < 1e-3 && std::abs (audition->end - end) < 1e-3;
    if (same)
    {
        if (onStopAudition)
            onStopAudition();
    }
    else if (onAudition)
    {
        onAudition (start, end);
    }
}

void TrackView::setAudition (std::optional<Audition> next)
{
    if (next == audition)
        return;

    // While only the playhead moves, repaint just where it was and is.
    const bool onlyMoved = next && audition && juce::exactlyEqual (next->start, audition->start)
                           && juce::exactlyEqual (next->end, audition->end)
                           && next->playing == audition->playing && next->loading == audition->loading
                           && next->error == audition->error;
    if (onlyMoved && loaded)
    {
        repaint (playheadArea (*audition));
        repaint (playheadArea (*next));
    }
    else
    {
        repaint();
    }
    audition = std::move (next);
}

juce::Rectangle<int> TrackView::playheadArea (const Audition& a) const
{
    const int x = juce::roundToInt (timeToX (a.position));
    return { x - 3, stripArea.getY(), 7, waveArea.getBottom() - stripArea.getY() };
}

void TrackView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! loaded)
        return;
    const float sign = wheel.isReversed ? -1.0f : 1.0f;
    const float dx = wheel.deltaX * sign, dy = wheel.deltaY * sign;

    if (e.mods.isShiftDown() || std::abs (dx) > std::abs (dy))
    {
        const float delta = std::abs (dx) > std::abs (dy) ? dx : dy;
        range.scrollBy (-delta * range.span() * 0.5);
        rangeChanged();
        return;
    }
    zoomAt (e.position.x, std::exp (dy * wheelZoomSpeed));
}

void TrackView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (loaded)
        zoomAt (e.position.x, scaleFactor);
}

bool TrackView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && audition)
    {
        if (onStopAudition)
            onStopAudition();
        return true;
    }
    if (! loaded)
        return false;
    const auto centre = static_cast<float> (waveArea.getCentreX());
    const auto c = key.getTextCharacter();
    if (c == '+' || c == '=')
        zoomAt (centre, 1.5);
    else if (c == '-' || c == '_')
        zoomAt (centre, 1.0 / 1.5);
    else if (key.getKeyCode() == juce::KeyPress::homeKey)
        showRange (0.0, range.duration());
    else if (key.getKeyCode() == juce::KeyPress::leftKey)
        showRange (range.start() - 0.2 * range.span(), range.span());
    else if (key.getKeyCode() == juce::KeyPress::rightKey)
        showRange (range.start() + 0.2 * range.span(), range.span());
    else
        return false;
    return true;
}

} // namespace djmix
