#include "LibraryPanel.h"

#include <algorithm>

#include "AppSettings.h"
#include "library/ImportScanner.h"
#include "library/TrackColours.h"

namespace djmix
{

namespace
{
// Below this, the beat grid is flagged for review (design spec, Library).
constexpr double lowBeatConfidence = 0.5;

juce::String fromUtf8 (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

juce::Colour colourOf (int index)
{
    const auto clamped = std::clamp (index, 0, static_cast<int> (trackPalette.size()) - 1);
    return juce::Colour (trackPalette[static_cast<size_t> (clamped)].argb);
}

juce::String formatDuration (double seconds)
{
    const int total = juce::roundToInt (seconds);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

std::optional<double> parseBpm (const juce::TextEditor& editor)
{
    const auto text = editor.getText().trim();
    if (text.isEmpty())
        return std::nullopt;
    const double value = text.getDoubleValue();
    return value > 0.0 ? std::optional<double> (value) : std::nullopt;
}

bool needsReview (const TrackRecord& t)
{
    return t.status == AnalysisStatus::ready && t.beatConfidence && *t.beatConfidence < lowBeatConfidence;
}

// Sort key per column; tracks missing the value sort after the rest either way.
int compareTracks (const TrackRecord& a, const TrackRecord& b, int column)
{
    auto compareOptional = [] (const std::optional<double>& x, const std::optional<double>& y)
    {
        if (x && y)
            return *x < *y ? -1 : (*x > *y ? 1 : 0);
        return x ? -1 : (y ? 1 : 0);
    };
    auto compareText = [] (const std::string& x, const std::string& y)
    {
        return fromUtf8 (x).compareNatural (fromUtf8 (y));
    };

    switch (column)
    {
        case 1: return a.colour - b.colour;
        case 2: return compareText (a.title, b.title);
        case 3: return compareText (a.artist, b.artist);
        case 4: return compareOptional (a.effectiveBpm(), b.effectiveBpm());
        case 5:
        {
            const auto ka = parseCamelot (a.camelot), kb = parseCamelot (b.camelot);
            auto rank = [] (const std::optional<CamelotKey>& k)
            { return k ? std::optional<double> (k->number * 2 + (k->minor ? 0 : 1)) : std::nullopt; };
            return compareOptional (rank (ka), rank (kb));
        }
        case 6: return compareOptional (a.durationSec, b.durationSec);
        case 7: return static_cast<int> (a.status) - static_cast<int> (b.status);
        default: return static_cast<int> (a.id - b.id);
    }
}
} // namespace

LibraryPanel::LibraryPanel (LibraryController& c)
    : controller (c)
{
    addAndMakeVisible (addButton);
    addButton.onClick = [this] { showAddMenu(); };

    addAndMakeVisible (settingsButton);
    settingsButton.onClick = [this]
    {
        if (onOpenSettings)
            onOpenSettings();
    };

    addAndMakeVisible (summaryLabel);
    summaryLabel.setJustificationType (juce::Justification::centredRight);
    summaryLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    addChildComponent (analyzerWarning);
    analyzerWarning.setText ("The analyzer (djanalyze) is not set up, so tracks cannot be analysed.",
                             juce::dontSendNotification);
    analyzerWarning.setColour (juce::Label::backgroundColourId, juce::Colour (0xff5a4212));
    analyzerWarning.setColour (juce::Label::textColourId, juce::Colour (0xfff5d08a));
    addChildComponent (analyzerSetupButton);
    analyzerSetupButton.onClick = settingsButton.onClick;

    for (auto* editor : { &minBpm, &maxBpm })
    {
        addAndMakeVisible (*editor);
        editor->setInputRestrictions (6, "0123456789.");
        editor->setJustification (juce::Justification::centred);
        editor->onTextChange = [this] { updateFilter(); };
    }
    minBpm.setTextToShowWhenEmpty ("min", juce::Colours::grey);
    maxBpm.setTextToShowWhenEmpty ("max", juce::Colours::grey);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmDash);
    bpmDash.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (keyMatchToggle);
    keyMatchToggle.setTooltip ("Show only tracks that mix well with the selected track: same key, "
                               "one step on the Camelot wheel, or its relative major/minor.");
    keyMatchToggle.onClick = [this] { updateFilter(); };

    addAndMakeVisible (table);
    auto& header = table.getHeader();
    const int sortable = juce::TableHeaderComponent::defaultFlags;
    header.addColumn ({}, colourColumn, 26, 26, 26, sortable & ~juce::TableHeaderComponent::resizable);
    header.addColumn ("Title", titleColumn, 220, 80, 800, sortable);
    header.addColumn ("Artist", artistColumn, 150, 60, 600, sortable);
    header.addColumn ("BPM", bpmColumn, 60, 50, 90, sortable);
    header.addColumn ("Key", keyColumn, 46, 40, 80, sortable);
    header.addColumn ("Length", lengthColumn, 58, 50, 90, sortable);
    header.addColumn ("Status", statusColumn, 120, 90, 240, sortable);
    header.setStretchToFitActive (true);
    header.setColour (juce::TableHeaderComponent::backgroundColourId, juce::Colour (0xff2e343a));
    header.setColour (juce::TableHeaderComponent::textColourId, juce::Colours::white.withAlpha (0.75f));
    header.setColour (juce::TableHeaderComponent::outlineColourId, juce::Colour (0xff1e2226));
    header.setColour (juce::TableHeaderComponent::highlightColourId, juce::Colour (0xff3a5f8a));
    table.setMultipleSelectionEnabled (false);
    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1e2226));

    controller.addChangeListener (this);
    refresh();
}

LibraryPanel::~LibraryPanel()
{
    controller.removeChangeListener (this);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262b30));
}

void LibraryPanel::paintOverChildren (juce::Graphics& g)
{
    if (rows.empty() && ! filter.isActive())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (15.0f);
        g.drawFittedText ("Drop audio files or folders here, or use Add...",
                          table.getBounds().reduced (20), juce::Justification::centred, 2);
    }

    if (dragHovering)
    {
        g.setColour (juce::Colour (0x3366aaff));
        g.fillRect (table.getBounds());
        g.setColour (juce::Colour (0xff66aaff));
        g.drawRect (table.getBounds(), 2);
    }
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().reduced (8);

    auto top = area.removeFromTop (28);
    addButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (6);
    settingsButton.setBounds (top.removeFromLeft (90));
    summaryLabel.setBounds (top);
    area.removeFromTop (6);

    if (analyzerWarning.isVisible())
    {
        auto banner = area.removeFromTop (30);
        analyzerSetupButton.setBounds (banner.removeFromRight (80).reduced (2));
        analyzerWarning.setBounds (banner);
        area.removeFromTop (6);
    }

    auto filters = area.removeFromTop (26);
    bpmLabel.setBounds (filters.removeFromLeft (38));
    minBpm.setBounds (filters.removeFromLeft (56));
    bpmDash.setBounds (filters.removeFromLeft (28));
    maxBpm.setBounds (filters.removeFromLeft (56));
    filters.removeFromLeft (16);
    keyMatchToggle.setBounds (filters.removeFromLeft (220));
    area.removeFromTop (6);

    table.setBounds (area);
}

int LibraryPanel::getNumRows()
{
    return static_cast<int> (rows.size());
}

void LibraryPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (juce::Colour (0xff3a5f8a));
    else if (row % 2 == 1)
        g.fillAll (juce::Colour (0xff23282d));
}

void LibraryPanel::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    if (row < 0 || row >= getNumRows())
        return;
    const auto& t = rows[static_cast<size_t> (row)];
    const auto bounds = juce::Rectangle<int> (width, height);
    const auto text = bounds.reduced (6, 0);
    g.setFont (14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));

    switch (columnId)
    {
        case colourColumn:
            g.setColour (colourOf (t.colour));
            g.fillRoundedRectangle (bounds.toFloat().reduced (7.0f, 6.0f), 3.0f);
            break;

        case titleColumn:
            g.drawText (fromUtf8 (t.title), text, juce::Justification::centredLeft, true);
            break;

        case artistColumn:
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText (fromUtf8 (t.artist), text, juce::Justification::centredLeft, true);
            break;

        case bpmColumn:
            if (const auto bpm = t.effectiveBpm())
                g.drawText (juce::String (*bpm, 1), text, juce::Justification::centredRight, false);
            break;

        case keyColumn:
            g.drawText (fromUtf8 (t.camelot), text, juce::Justification::centred, false);
            break;

        case lengthColumn:
            if (t.durationSec)
                g.drawText (formatDuration (*t.durationSec), text, juce::Justification::centredRight, false);
            break;

        case statusColumn:
        {
            if (t.status == AnalysisStatus::analysing || controller.progressFor (t.id))
            {
                const float fraction = static_cast<float> (controller.progressFor (t.id).value_or (0.0));
                auto bar = bounds.reduced (6, 7).toFloat();
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRoundedRectangle (bar, 3.0f);
                g.setColour (juce::Colour (0xff66aaff));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fraction), 3.0f);
                break;
            }

            juce::String label;
            juce::Colour colour = juce::Colours::grey;
            switch (t.status)
            {
                case AnalysisStatus::pending: label = "Queued"; break;
                case AnalysisStatus::failed: label = "Failed"; colour = juce::Colour (0xffe06a5f); break;
                case AnalysisStatus::ready:
                    if (needsReview (t))
                    {
                        label = "Check beats";
                        colour = juce::Colour (0xffe0b54f);
                    }
                    else
                    {
                        label = "Ready";
                    }
                    break;
                case AnalysisStatus::analysing: break;
            }

            if (needsReview (t))
            {
                // Warning badge: a small amber triangle before the label.
                juce::Path triangle;
                const float cy = static_cast<float> (height) * 0.5f;
                triangle.addTriangle (8.0f, cy + 5.0f, 14.0f, cy - 6.0f, 20.0f, cy + 5.0f);
                g.setColour (colour);
                g.fillPath (triangle);
                g.setColour (juce::Colours::black);
                g.setFont (10.0f);
                g.drawText ("!", juce::Rectangle<float> (8.0f, cy - 4.0f, 12.0f, 10.0f), juce::Justification::centred);
                g.setFont (14.0f);
                g.setColour (colour);
                g.drawText (label, text.withTrimmedLeft (18), juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour (colour);
                g.drawText (label, text, juce::Justification::centredLeft, true);
            }
            break;
        }

        default: break;
    }
}

juce::String LibraryPanel::getCellTooltip (int row, int columnId)
{
    if (row < 0 || row >= getNumRows())
        return {};
    const auto& t = rows[static_cast<size_t> (row)];

    if (columnId == keyColumn)
        return fromUtf8 (t.keyName);
    if (columnId == statusColumn)
    {
        if (t.status == AnalysisStatus::failed)
            return fromUtf8 (t.error);
        if (needsReview (t))
            return "The beat estimate is uncertain (confidence " + juce::String (*t.beatConfidence, 2)
                   + "). Check the downbeat before mixing.";
    }
    if (columnId == titleColumn)
        return toFile (t.path).getFullPathName();
    return {};
}

void LibraryPanel::sortOrderChanged (int columnId, bool forwards)
{
    sortColumn = columnId;
    sortForwards = forwards;
    refresh();
}

void LibraryPanel::cellClicked (int row, int, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() && row >= 0 && row < getNumRows())
    {
        table.selectRow (row);
        showTrackMenu (rows[static_cast<size_t> (row)]);
    }
}

void LibraryPanel::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row >= 0 && row < getNumRows() && onAuditionTrack)
        onAuditionTrack (rows[static_cast<size_t> (row)].id);
}

void LibraryPanel::selectedRowsChanged (int)
{
    if (! updatingSelection && onSelectionChanged)
        onSelectionChanged (selectedTrackId());
}

void LibraryPanel::deleteKeyPressed (int)
{
    if (const auto id = selectedTrackId())
        controller.removeTrack (id);
}

bool LibraryPanel::isInterestedInFileDrag (const juce::StringArray&)
{
    return true;
}

void LibraryPanel::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHovering = true;
    repaint();
}

void LibraryPanel::fileDragExit (const juce::StringArray&)
{
    dragHovering = false;
    repaint();
}

void LibraryPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragHovering = false;
    juce::Array<juce::File> dropped;
    for (const auto& f : files)
        dropped.add (juce::File (f));
    importFiles (dropped);
    repaint();
}

void LibraryPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void LibraryPanel::refresh()
{
    const auto selected = selectedTrackId();

    rows.clear();
    for (const auto& t : controller.tracks())
        if (filter.matches (t))
            rows.push_back (t);

    if (sortColumn != 0)
    {
        std::stable_sort (rows.begin(), rows.end(), [this] (const TrackRecord& a, const TrackRecord& b)
        {
            const int c = compareTracks (a, b, sortColumn);
            return sortForwards ? c < 0 : c > 0;
        });
    }

    table.updateContent();

    // Keep the same track selected across refreshes.
    const juce::ScopedValueSetter<bool> guard (updatingSelection, true);
    table.deselectAllRows();
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].id == selected)
            table.selectRow (static_cast<int> (i), true);

    table.repaint();

    const bool analyzerMissing = controller.analyzerExecutable().empty();
    if (analyzerWarning.isVisible() != analyzerMissing)
    {
        analyzerWarning.setVisible (analyzerMissing);
        analyzerSetupButton.setVisible (analyzerMissing);
        resized();
    }

    updateSummary();
    repaint();
}

void LibraryPanel::updateFilter()
{
    filter.minBpm = parseBpm (minBpm);
    filter.maxBpm = parseBpm (maxBpm);

    filter.compatibleWith.reset();
    keyMatchToggle.setButtonText ("Compatible keys");
    if (keyMatchToggle.getToggleState())
    {
        const auto* reference = controller.findTrack (selectedTrackId());
        const auto key = reference != nullptr ? parseCamelot (reference->camelot) : std::nullopt;
        if (key)
        {
            filter.compatibleWith = key;
            keyMatchToggle.setButtonText ("Compatible with " + fromUtf8 (toString (*key)));
        }
        else
        {
            keyMatchToggle.setToggleState (false, juce::dontSendNotification);
        }
    }

    refresh();
}

void LibraryPanel::updateSummary()
{
    const auto& all = controller.tracks();
    const auto busy = std::count_if (all.begin(), all.end(), [] (const TrackRecord& t)
    { return t.status == AnalysisStatus::pending || t.status == AnalysisStatus::analysing; });
    const auto failed = std::count_if (all.begin(), all.end(), [] (const TrackRecord& t)
    { return t.status == AnalysisStatus::failed; });

    juce::String text;
    if (filter.isActive())
        text << static_cast<int> (rows.size()) << " of ";
    text << static_cast<int> (all.size()) << (all.size() == 1 ? " track" : " tracks");
    if (busy > 0)
        text << " · " << static_cast<int> (busy) << " to analyse";
    if (failed > 0)
        text << " · " << static_cast<int> (failed) << " failed";
    summaryLabel.setText (text, juce::dontSendNotification);
}

void LibraryPanel::showAddMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Add files...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add audio files", juce::File(),
                                                       "*.wav;*.wave;*.flac;*.aif;*.aiff;*.mp3;*.ogg;*.opus;*.m4a;*.aac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) { importFiles (fc.getResults()); });
    });
    menu.addItem ("Add folder...", [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder of audio files");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) { importFiles (fc.getResults()); });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton));
}

void LibraryPanel::showTrackMenu (const TrackRecord& track)
{
    const auto id = track.id;
    const auto file = toFile (track.path);

    juce::PopupMenu colours;
    for (int i = 0; i < static_cast<int> (trackPalette.size()); ++i)
    {
        juce::PopupMenu::Item item (juce::String (trackPalette[static_cast<size_t> (i)].name.data()));
        item.setColour (colourOf (i));
        item.setTicked (track.colour == i);
        item.setAction ([this, id, i] { controller.setColour (id, i); });
        colours.addItem (item);
    }

    juce::PopupMenu menu;
    menu.addSubMenu ("Colour", colours);
    menu.addItem ("Analyse again", [this, id] { controller.reanalyse (id); });
    menu.addItem ("Show file", file.exists(), false, [file] { file.revealToUser(); });
    menu.addSeparator();
    menu.addItem ("Remove from library", [this, id] { controller.removeTrack (id); });
    menu.showMenuAsync (juce::PopupMenu::Options());
}

void LibraryPanel::importFiles (const juce::Array<juce::File>& files)
{
    std::vector<std::filesystem::path> paths;
    for (const auto& f : files)
        paths.push_back (toPath (f));
    if (paths.empty())
        return;

    if (controller.importPaths (paths) == 0)
        summaryLabel.setText ("No new audio files found", juce::dontSendNotification);
}

int64_t LibraryPanel::selectedTrackId() const
{
    const int row = table.getSelectedRow();
    return row >= 0 && row < static_cast<int> (rows.size()) ? rows[static_cast<size_t> (row)].id : 0;
}

} // namespace djmix
