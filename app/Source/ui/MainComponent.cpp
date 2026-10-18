#include "MainComponent.h"

#include <variant>

#include "SettingsComponent.h"
#include "io/ProjectFile.h"
#include "model/ProjectEdits.h"

namespace djmix
{

namespace
{
// A grid or section edit to one track, undoable.
class OverridesEdit final : public juce::UndoableAction
{
public:
    OverridesEdit (LibraryController& c, int64_t id, TrackOverrides from, TrackOverrides to)
        : controller (c), trackId (id), before (std::move (from)), after (std::move (to))
    {
    }

    bool perform() override { return apply (after); }
    bool undo() override { return apply (before); }

private:
    bool apply (const TrackOverrides& overrides)
    {
        // The track may have been removed since.
        if (controller.findTrack (trackId) == nullptr)
            return false;
        controller.setOverrides (trackId, overrides);
        return true;
    }

    LibraryController& controller;
    int64_t trackId;
    TrackOverrides before, after;
};

// An arrangement edit, undoable: the whole project before and after.
class ProjectEdit final : public juce::UndoableAction
{
public:
    using Apply = std::function<void (const Project&)>;
    ProjectEdit (Apply a, Project from, Project to) : apply (std::move (a)), before (std::move (from)), after (std::move (to)) {}

    bool perform() override
    {
        apply (after);
        return true;
    }
    bool undo() override
    {
        apply (before);
        return true;
    }

private:
    Apply apply;
    Project before, after;
};

constexpr int saveDelayMs = 1000;

juce::String fromUtf8 (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}
} // namespace

MainComponent::MainComponent (AppSettings& s, LibraryController& c, AudioOutput& a, AuditionController& au)
    : settings (s), controller (c), audio (a), audition (au), library (c)
{
    addAndMakeVisible (library);
    addAndMakeVisible (divider);
    addAndMakeVisible (trackView);
    addAndMakeVisible (transport);
    addAndMakeVisible (arrangement);

    // The arrangement plays each clip from its track's file and effective grid.
    player = std::make_unique<ArrangementPlayer> (audio.master(), [this] (const Clip& clip) -> std::optional<ArrangementPlayer::TrackSource>
    {
        const auto material = materialFor (clip.trackId);
        if (material == nullptr)
            return std::nullopt;
        return ArrangementPlayer::TrackSource { material->file, material->analysis.beats };
    });
    player->onChanged = [this] { arrangement.repaint(); };
    playerRate = audio.master().deviceSampleRate();

    arrangement.setMaterialSource ([this] (int64_t id) { return materialFor (id); });
    arrangement.setClipStateSource ([this] (int64_t id) { return player->clipState (id); });
    arrangement.onEdit = [this] (const Project& next, const juce::String& name) { editProject (next, name); };
    arrangement.onSeek = [this] (double beat) { player->seek (beat); };
    arrangement.onTogglePlay = [this] { togglePlay(); };
    arrangement.onSnapModeChanged = [this] (SnapMode mode)
    {
        transport.setSnapMode (mode);
        settings.setSnapMode (mode);
    };
    arrangement.onFollowChanged = [this] (bool follow)
    {
        transport.setFollow (follow);
        settings.setFollowPlayhead (follow);
    };

    transport.onPlayPause = [this] { togglePlay(); };
    transport.onTempo = [this] (double bpm) { editProject (projectEdits::setTempo (project, bpm), "Change tempo"); };
    transport.onSnapMode = [this] (SnapMode mode)
    {
        arrangement.setSnapMode (mode);
        settings.setSnapMode (mode);
    };
    transport.onFollow = [this] (bool follow)
    {
        arrangement.setFollow (follow);
        settings.setFollowPlayhead (follow);
    };
    transport.onNew = [this] { newProject(); };
    transport.onOpen = [this] { chooseProjectToOpen(); };
    transport.onSaveAs = [this] { chooseSaveAs(); };

    arrangement.setSnapMode (settings.snapMode());
    transport.setSnapMode (settings.snapMode());
    arrangement.setFollow (settings.followPlayhead());
    transport.setFollow (settings.followPlayhead());

    // The project open last time, or the untitled one, or a new one.
    const auto last = settings.projectFile();
    std::error_code missing;
    if (! last.empty() && std::filesystem::exists (last, missing))
        openProject (last);
    else if (std::filesystem::exists (settings.untitledProjectFile(), missing))
        openProject ({});
    else
        switchProject (Project::create ("Untitled"), {});

    library.onOpenSettings = [this] { SettingsComponent::show (settings, controller, audio); };
    library.onSelectionChanged = [this] (int64_t id)
    {
        selectedTrack = id;
        const auto* track = controller.findTrack (id);
        trackView.showTrack (track);
        if (track != nullptr)
            audition.prepare (track->id, track->path); // decode ahead, so a click plays at once
    };

    // Auditions: sections and phrases from the Track view, whole tracks from the Library.
    trackView.onAudition = [this] (double start, double end)
    {
        if (const auto* track = controller.findTrack (selectedTrack))
            audition.play (track->id, track->path, start, end);
    };
    trackView.onStopAudition = [this] { audition.stop(); };
    trackView.onEditOverrides = [this] (const TrackOverrides& next, bool continuing)
    {
        const auto* track = controller.findTrack (selectedTrack);
        if (track == nullptr)
            return;
        if (! continuing)
            undo.beginNewTransaction();
        undo.perform (new OverridesEdit (controller, track->id, track->overrides, next));
    };
    audition.setMode (settings.cueMode());
    trackView.setCueMode (settings.cueMode());
    trackView.onCycleCueMode = [this]
    {
        const auto mode = nextCueMode (audition.mode());
        audition.setMode (mode);
        settings.setCueMode (mode);
        trackView.setCueMode (mode);
    };
    library.onAuditionTrack = [this] (int64_t id)
    {
        const auto now = audition.status();
        if (now.trackId == id && (now.playing || now.loading) && juce::exactlyEqual (now.start, 0.0)
            && juce::exactlyEqual (now.end, 0.0))
            audition.stop(); // double-clicking it again stops it
        else if (const auto* track = controller.findTrack (id))
            audition.play (id, track->path, 0.0, 0.0);
    };
    startTimerHz (60);
    controller.addChangeListener (this);

    layout.setItemLayout (0, 360, -0.8, -0.42); // library
    layout.setItemLayout (1, 6, 6, 6);          // divider
    layout.setItemLayout (2, 300, -1.0, -0.58); // track view and arrangement

    setSize (1280, 800);
}

MainComponent::~MainComponent()
{
    stopTimer();
    controller.removeChangeListener (this);
    if (saveDue)
        saveNow();
    player->pause();
}

//==============================================================================
// The arrangement

std::shared_ptr<const TrackMaterial> MainComponent::materialFor (int64_t trackId)
{
    const auto* track = controller.findTrack (trackId);
    return track != nullptr ? catalog.get (*track) : nullptr;
}

void MainComponent::applyProject (const Project& next)
{
    project = next;
    arrangement.setProject (project);
    player->setProject (project);
    transport.setTempo (project.tempo);
    saveDue = juce::Time::getMillisecondCounter() + saveDelayMs;
}

void MainComponent::editProject (const Project& next, const juce::String& name)
{
    if (next == project)
        return;
    undo.beginNewTransaction (name);
    undo.perform (new ProjectEdit ([this] (const Project& p) { applyProject (p); }, project, next));
}

void MainComponent::togglePlay()
{
    if (player->state().playing)
        player->pause();
    else
        player->play();
}

void MainComponent::switchProject (Project next, const std::filesystem::path& file)
{
    // Undo steps for the old project would bring it back over this one.
    undo.clearUndoHistory();
    player->pause();
    player->seek (0.0);
    projectFile = file;
    settings.setProjectFile (file);
    applyProject (next);
    saveDue.reset();
    arrangement.selectClip (std::nullopt);
    updateProjectName();
}

void MainComponent::openProject (const std::filesystem::path& file)
{
    const auto result = ProjectFile::load (file.empty() ? settings.untitledProjectFile() : file);
    if (const auto* error = std::get_if<ProjectLoadError> (&result))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot open the project",
                                                fromUtf8 (error->message));
        if (project.layers.empty())
            switchProject (Project::create ("Untitled"), {});
        return;
    }
    switchProject (std::get<Project> (result), file);
}

void MainComponent::newProject()
{
    if (saveDue)
        saveNow();
    auto start = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
        {
            safe->switchProject (Project::create ("Untitled", safe->project.tempo), {});
            safe->saveDue = juce::Time::getMillisecondCounter(); // replaces the untitled file
        }
    };

    // An untitled project would be lost: ask first.
    if (projectFile.empty() && ! project.clips.empty())
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New project",
                                            "The untitled project has not been saved under a name. Start a new one anyway?",
                                            "Start new", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([start] (int result) { if (result == 1) start(); }));
        return;
    }
    start();
}

void MainComponent::chooseProjectToOpen()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a project", toFile (projectFile).getParentDirectory(), "*.djmix");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        if (saveDue)
            saveNow();
        openProject (toPath (file));
    });
}

void MainComponent::chooseSaveAs()
{
    const auto suggested = toFile (projectFile.empty() ? std::filesystem::path() : projectFile);
    chooser = std::make_unique<juce::FileChooser> ("Save the project as", suggested, "*.djmix");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        if (! file.hasFileExtension (ProjectFile::extension))
            file = file.withFileExtension (ProjectFile::extension);
        project.name = file.getFileNameWithoutExtension().toStdString();
        projectFile = toPath (file);
        settings.setProjectFile (projectFile);
        saveNow();
        updateProjectName();
    });
}

std::filesystem::path MainComponent::saveTarget() const
{
    return projectFile.empty() ? settings.untitledProjectFile() : projectFile;
}

void MainComponent::saveNow()
{
    saveDue.reset();
    std::string error;
    saveError = ProjectFile::save (project, saveTarget(), error) ? juce::String() : "Not saved: " + fromUtf8 (error);
}

void MainComponent::updateProjectName()
{
    arrangement.setTitle (projectFile.empty() ? juce::String ("Untitled") : toFile (projectFile).getFileNameWithoutExtension());
}

void MainComponent::timerCallback()
{
    // The playhead, and the arrangement's state.
    const auto state = player->state();
    arrangement.setPlayhead (state.beat, state.playing);
    transport.setPlaying (state.playing);
    transport.setPosition (state.beat);
    const int rendering = player->rendersPending();
    if (rendering > 0)
        transport.setStatus ("Rendering " + juce::String (rendering) + (rendering == 1 ? " clip..." : " clips..."));
    else
        transport.setStatus (saveError);

    // A device at another rate needs renders at that rate.
    const double rate = audio.master().deviceSampleRate();
    if (rate > 0.0 && ! juce::exactlyEqual (rate, playerRate))
    {
        playerRate = rate;
        player->refresh();
    }
    if (saveDue && juce::Time::getMillisecondCounter() >= *saveDue)
        saveNow();

    const auto now = audition.status();
    const bool showing = now.trackId != 0 && now.trackId == selectedTrack
                         && (now.playing || now.loading || ! now.error.empty());
    if (! showing)
    {
        trackView.setAudition (std::nullopt);
        return;
    }
    TrackView::Audition a;
    a.start = now.start;
    a.end = now.end;
    a.position = now.position;
    a.playing = now.playing;
    a.loading = now.loading;
    a.error = juce::String::fromUTF8 (now.error.c_str());
    trackView.setAudition (a);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Esc stops an audition wherever the keyboard focus is.
    if (key == juce::KeyPress::escapeKey)
    {
        audition.stop();
        return true;
    }
    // Space plays and pauses the arrangement (unless typing).
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlay();
        return true;
    }

    // Undo and redo grid and section edits: Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z or Ctrl+Y.
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        if (mods.isShiftDown())
            undo.redo();
        else
            undo.undo();
        return true;
    }
    if (mods.isCommandDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y'))
    {
        undo.redo();
        return true;
    }
    return false;
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The selected track may have finished (re-)analysis, changed colour or gone.
    if (selectedTrack != 0)
        trackView.showTrack (controller.findTrack (selectedTrack));

    // A track in the arrangement may have a new grid (so new renders), colour or file.
    player->refresh();
    arrangement.repaint();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1d21));
}

void MainComponent::resized()
{
    juce::Component dummy; // the right-hand column is laid out below
    juce::Component* columns[] = { &library, &divider, &dummy };
    layout.layOutComponents (columns, 3, 0, 0, getWidth(), getHeight(), false, true);

    auto right = dummy.getBounds().reduced (6);
    trackView.setBounds (right.removeFromTop (right.getHeight() * 9 / 20));
    right.removeFromTop (6);
    transport.setBounds (right.removeFromTop (34));
    right.removeFromTop (4);
    arrangement.setBounds (right);
}

} // namespace djmix
