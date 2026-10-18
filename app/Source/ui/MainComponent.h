#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "ArrangementView.h"
#include "AudioOutput.h"
#include "LibraryPanel.h"
#include "TrackView.h"
#include "TransportBar.h"
#include "engine/ArrangementPlayer.h"
#include "engine/AuditionController.h"
#include "library/LibraryController.h"
#include "library/TrackCatalog.h"

namespace djmix
{

// Top-level window content: the Library on the left, the Track view on top
// and the Arrangement below it, with its transport bar.
//
// The project saves itself a moment after each edit: to its file, or to
// Untitled.djmix in the app's data folder until it is given a name with
// Save As. The project open when the app closes opens again next time.
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent (AppSettings& settings, LibraryController& controller, AudioOutput& audio, AuditionController& audition);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override; // follows the audition and the playhead

    // The arrangement.
    std::shared_ptr<const TrackMaterial> materialFor (int64_t trackId);
    void applyProject (const Project& project); // shows and plays it, and saves it soon
    void editProject (const Project& next, const juce::String& name);
    void togglePlay();
    void openProject (const std::filesystem::path& file);
    void switchProject (Project project, const std::filesystem::path& file);
    void newProject();
    void chooseProjectToOpen();
    void chooseSaveAs();
    void saveNow();
    std::filesystem::path saveTarget() const;
    void updateProjectName();

    AppSettings& settings;
    LibraryController& controller;
    AudioOutput& audio;
    AuditionController& audition;
    juce::UndoManager undo; // grid, section and arrangement edits

    LibraryPanel library;
    TrackView trackView;
    int64_t selectedTrack = 0;

    TrackCatalog catalog;
    std::unique_ptr<ArrangementPlayer> player;
    TransportBar transport;
    ArrangementView arrangement;
    Project project;
    std::filesystem::path projectFile; // empty: the untitled project
    std::optional<juce::uint32> saveDue; // when to save, in milliseconds
    juce::String saveError;              // why the last save failed, shown until one works
    double playerRate = 0.0;            // the device rate the player last built for
    std::unique_ptr<juce::FileChooser> chooser;
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar divider { &layout, 1, true };
    juce::TooltipWindow tooltips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace djmix
