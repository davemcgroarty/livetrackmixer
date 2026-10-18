#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "library/LibraryController.h"
#include "library/LibraryFilter.h"

namespace djmix
{

// The Library: a sortable, filterable table of tracks with their analysis
// state. Drop files or folders on it to import them.
class LibraryPanel final : public juce::Component,
                           public juce::TableListBoxModel,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener
{
public:
    explicit LibraryPanel (LibraryController& controller);
    ~LibraryPanel() override;

    // Called with the selected track's id, or 0 when nothing is selected.
    std::function<void (int64_t)> onSelectionChanged;
    std::function<void()> onOpenSettings;
    // Called with a track's id when its row is double-clicked, to audition it.
    std::function<void (int64_t)> onAuditionTrack;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool selected) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    void cellClicked (int row, int columnId, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int columnId, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void deleteKeyPressed (int lastRowSelected) override;
    juce::String getCellTooltip (int row, int columnId) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    enum Column
    {
        colourColumn = 1,
        titleColumn,
        artistColumn,
        bpmColumn,
        keyColumn,
        lengthColumn,
        statusColumn
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void updateFilter();
    void updateSummary();
    void showAddMenu();
    void showTrackMenu (const TrackRecord& track);
    void importFiles (const juce::Array<juce::File>& files);
    int64_t selectedTrackId() const;

    LibraryController& controller;
    std::vector<TrackRecord> rows;
    LibraryFilter filter;
    int sortColumn = 0;
    bool sortForwards = true;
    bool dragHovering = false;
    bool updatingSelection = false;

    juce::TextButton addButton { "Add..." };
    juce::TextButton settingsButton { "Settings..." };
    juce::Label summaryLabel;

    juce::Label analyzerWarning;
    juce::TextButton analyzerSetupButton { "Set up..." };

    juce::Label bpmLabel { {}, "BPM" };
    juce::TextEditor minBpm;
    juce::Label bpmDash { {}, "to" };
    juce::TextEditor maxBpm;
    juce::ToggleButton keyMatchToggle { "Compatible keys" };

    juce::TableListBox table { "Library", this };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
};

} // namespace djmix
