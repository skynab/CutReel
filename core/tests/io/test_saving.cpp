#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "zaro/core/edit/Operations.h"
#include "zaro/core/io/ProjectIo.h"

#include "ModelFixtures.h"

using namespace zaro;
using zaro::testing::Fixture;

namespace {

/// A directory that cleans up after itself, so a failing test does not leave
/// files behind for the next run to trip over.
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("zaro-save-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] std::string file(const char* name) const { return (path / name).string(); }
};

}  // namespace

// --- Saving -----------------------------------------------------------------

TEST_CASE("Saving writes beside the file and renames over it", "[io][save]") {
    TempDir dir;
    const std::string path = dir.file("project.cutreel");
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));

    REQUIRE(io::saveProject(f.project, path));
    CHECK(std::filesystem::exists(path));
    // No leftovers: a directory littered with half-written files is how people
    // learn not to trust a program with their work.
    CHECK_FALSE(std::filesystem::exists(path + ".saving"));

    auto reloaded = io::loadProject(path);
    REQUIRE(reloaded);
    CHECK(reloaded->project.findSequence(f.sequenceId)->findTrack(f.v1)->clips().size() == 1);
}

TEST_CASE("A save that cannot be written leaves the previous version alone", "[io][save]") {
    TempDir dir;
    const std::string path = dir.file("project.cutreel");
    Fixture f;
    REQUIRE(io::saveProject(f.project, path));
    const auto originalSize = std::filesystem::file_size(path);

    // A directory where the file should be: the rename cannot replace it, and
    // the point is that the failure is reported rather than partly applied.
    const std::string blocked = dir.file("blocked.cutreel");
    std::filesystem::create_directories(blocked);
    CHECK_FALSE(io::saveProject(f.project, blocked));
    CHECK_FALSE(std::filesystem::exists(blocked + ".saving"));

    // And the good one is untouched.
    CHECK(std::filesystem::file_size(path) == originalSize);
}

TEST_CASE("A recovery file sits beside the project", "[io][save]") {
    CHECK(io::autosavePath("/takes/cut.cutreel") == "/takes/cut.cutreel.autosave");
}

TEST_CASE("A recovery file is only offered when it is newer", "[io][save]") {
    TempDir dir;
    const std::string path = dir.file("project.cutreel");
    Fixture f;

    CHECK_FALSE(io::hasNewerAutosave(path));

    REQUIRE(io::saveProject(f.project, path));
    CHECK_FALSE(io::hasNewerAutosave(path));

    REQUIRE(io::saveProject(f.project, io::autosavePath(path)));
    // Same content, later write: an autosave made after the last real save is
    // the one case where offering it is honest.
    std::filesystem::last_write_time(
        io::autosavePath(path), std::filesystem::last_write_time(path) + std::chrono::seconds{10});
    CHECK(io::hasNewerAutosave(path));

    SECTION("and an older one is not") {
        std::filesystem::last_write_time(
            io::autosavePath(path),
            std::filesystem::last_write_time(path) - std::chrono::seconds{10});
        // It describes work the last real save already includes.
        CHECK_FALSE(io::hasNewerAutosave(path));
    }
}

// --- Knowing what is unsaved ------------------------------------------------

TEST_CASE("A project starts modified and stops when it is saved", "[edit][save]") {
    Fixture f;
    // Never saved is modified: claiming otherwise would be a guess, and the
    // cost of guessing wrong is somebody's work.
    CHECK(f.stack.isModified());

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    CHECK(f.stack.isModified());

    f.stack.markSaved();
    CHECK_FALSE(f.stack.isModified());

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(100, 50))));
    CHECK(f.stack.isModified());
}

TEST_CASE("Undoing back to the saved state is not modified", "[edit][save]") {
    // A modified marker that will not go away is one people stop reading.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    f.stack.markSaved();

    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(100, 50))));
    CHECK(f.stack.isModified());
    f.stack.undo(f.project);
    CHECK_FALSE(f.stack.isModified());

    SECTION("and redoing past it is modified again") {
        f.stack.redo(f.project);
        CHECK(f.stack.isModified());
    }
}

TEST_CASE("A merge that changes nothing about the position still modifies", "[edit][save]") {
    // Dragging a value coalesces into the previous undo step, so the position
    // does not move -- but the project did. Without this, a drag after a save
    // would leave the project reading as unmodified while it had changed.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    const model::ClipId clipId = f.track(f.v1).clips().front().id;

    model::Transform moved;
    moved.positionX = 10.0;
    REQUIRE(f.run(edit::makeSetTransform(f.project, f.on(f.v1), clipId, moved)));
    f.stack.markSaved();
    CHECK_FALSE(f.stack.isModified());

    moved.positionX = 20.0;
    REQUIRE(f.run(edit::makeSetTransform(f.project, f.on(f.v1), clipId, moved)));
    CHECK(f.stack.isModified());
}

TEST_CASE("A saved state on a discarded branch is no longer recognised", "[edit][save]") {
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(100, 50))));
    f.stack.markSaved();

    f.stack.undo(f.project);
    CHECK(f.stack.isModified());
    // A new command here throws away the branch the saved state was on. There
    // is now no way back to it, so there is no way to recognise it either.
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(200, 50))));
    CHECK(f.stack.isModified());
    f.stack.undo(f.project);
    CHECK(f.stack.isModified());
}

TEST_CASE("A saved state that falls off the end of the history is forgotten", "[edit][save]") {
    Fixture f;
    edit::CommandStack shallow{3};
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 20))));
    shallow.markSaved();
    CHECK_FALSE(shallow.isModified());

    for (std::int64_t i = 0; i < 5; ++i) {
        auto built = edit::makeOverwrite(f.project, f.on(f.v1), f.clip(100 + (i * 30), 20));
        REQUIRE(built);
        shallow.execute(f.project, std::move(*built));
        shallow.breakMerge();
    }
    // The state it was saved at has been dropped from the history, so it can no
    // longer be undone back to -- and cannot be claimed as current either.
    CHECK(shallow.isModified());
}

TEST_CASE("Clearing the history forgets where the save was", "[edit][save]") {
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    f.stack.markSaved();
    f.stack.clear();
    // A caller that clears because it has just loaded a file marks the result
    // saved itself; guessing on its behalf would be wrong for every other
    // caller.
    CHECK(f.stack.isModified());
}

// --- Starting from nothing --------------------------------------------------

TEST_CASE("A new project has somewhere to put something", "[edit][newproject]") {
    model::Project project = model::newProject();
    const model::Sequence* sequence = project.findSequence(project.activeSequence());
    REQUIRE(sequence != nullptr);
    // A timeline with no tracks has nowhere to drop anything, and the first
    // thing anybody does is drop something.
    CHECK(sequence->videoTracks().size() == 1);
    CHECK(sequence->audioTracks().size() == 1);
    CHECK(sequence->duration().frames() == 0);

    SECTION("and its ids do not collide with what is added next") {
        const auto next = project.ids().next<model::ClipTag>();
        CHECK(next.value() > sequence->videoTracks().front().id().value());
    }

    SECTION("and it round trips") {
        auto text = io::saveProjectToString(project);
        REQUIRE(text);
        auto reloaded = io::loadProjectFromString(*text);
        REQUIRE(reloaded);
        CHECK(reloaded->project.activeSequence() == project.activeSequence());
    }
}

TEST_CASE("An empty sequence takes its format from what is put on it", "[edit][newproject]") {
    model::Project project = model::newProject();
    const model::SequenceId sequenceId = project.activeSequence();
    edit::CommandStack stack;

    auto built = edit::makeConformSequence(project, sequenceId, time::rates::fps24, 4096, 2160);
    REQUIRE(built);
    stack.execute(project, std::move(*built));

    const model::Sequence* sequence = project.findSequence(sequenceId);
    CHECK(sequence->frameRate() == time::rates::fps24);
    CHECK(sequence->width() == 4096);
    CHECK(sequence->height() == 2160);

    SECTION("and it is undoable like anything else") {
        stack.undo(project);
        CHECK(project.findSequence(sequenceId)->frameRate() == time::rates::fps25);
        CHECK(project.findSequence(sequenceId)->width() == 1920);
    }
}

TEST_CASE("Conforming a sequence that has clips on it is refused", "[edit][newproject]") {
    // Every clip's timeline range is expressed at the sequence's rate, so
    // changing it under a cut would retime the whole thing -- silently, and by
    // a ratio nobody was thinking about.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    CHECK_FALSE(edit::makeConformSequence(f.project, f.sequenceId, time::rates::fps24, 1920, 1080));

    SECTION("and so is a rate or a size that is not one") {
        model::Project project = model::newProject();
        const model::SequenceId id = project.activeSequence();
        CHECK_FALSE(edit::makeConformSequence(project, id, time::Rational{0, 1}, 1920, 1080));
        CHECK_FALSE(edit::makeConformSequence(project, id, time::rates::fps24, 0, 1080));
    }
}

TEST_CASE("A sequence can be resized after it has clips on it", "[edit][newproject]") {
    // The counterpart to the test above, and the distinction is the point.
    // Conforming is refused on a populated sequence because it changes the
    // *rate*, and every clip's timeline range is expressed at that rate. A
    // resize changes only how many pixels wide the frame is, which nothing's
    // timing depends on -- and refusing it would leave a project's output
    // resolution fixed forever by whatever was dropped on it first, with no
    // way back, because this program's export does not scale.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    REQUIRE_FALSE(
        edit::makeConformSequence(f.project, f.sequenceId, time::rates::fps24, 1280, 720));

    auto built = edit::makeResizeSequence(f.project, f.sequenceId, 1080, 1920);
    REQUIRE(built);
    f.stack.execute(f.project, std::move(*built));

    const model::Sequence* sequence = f.project.findSequence(f.sequenceId);
    CHECK(sequence->width() == 1080);
    CHECK(sequence->height() == 1920);
    // The rate is untouched: this is the whole reason it is a separate command.
    CHECK(sequence->frameRate() == time::rates::fps25);
    // And the cut is where it was.
    CHECK(sequence->duration().frames() == 50);

    SECTION("and it undoes") {
        f.stack.undo(f.project);
        CHECK(f.project.findSequence(f.sequenceId)->width() == 1920);
        CHECK(f.project.findSequence(f.sequenceId)->height() == 1080);
    }

    SECTION("odd sizes are refused, because the encoders reject them") {
        // Caught here rather than at export: a resize that only fails once the
        // render is queued fails after the work is done.
        CHECK_FALSE(edit::makeResizeSequence(f.project, f.sequenceId, 1081, 1920));
        CHECK_FALSE(edit::makeResizeSequence(f.project, f.sequenceId, 1080, 1921));
    }

    SECTION("so are nonsense sizes and the size it already is") {
        CHECK_FALSE(edit::makeResizeSequence(f.project, f.sequenceId, 0, 1080));
        CHECK_FALSE(edit::makeResizeSequence(f.project, f.sequenceId, -2, 1080));
        CHECK_FALSE(edit::makeResizeSequence(f.project, f.sequenceId, 1080, 1920));
    }
}

// --- Interpreting footage ---------------------------------------------------

TEST_CASE("A media reference reads its curve from the file until told otherwise", "[io][color]") {
    Fixture f;
    model::MediaRef& ref = f.project.mediaMutable().front();
    ref.info.videoStreams.front().color.transfer = media::TransferFunction::BT709;
    CHECK(ref.transfer() == media::TransferFunction::BT709);

    // A camera log file says BT.709 because a container has a number for that
    // and none for S-Log3. Nothing in the pixels can tell them apart.
    ref.transferOverride = media::TransferFunction::SLog3;
    CHECK(ref.transfer() == media::TransferFunction::SLog3);

    SECTION("and it survives a round trip") {
        auto text = io::saveProjectToString(f.project);
        REQUIRE(text);
        auto reloaded = io::loadProjectFromString(*text);
        REQUIRE(reloaded);
        CHECK(reloaded->project.media().front().transferOverride == media::TransferFunction::SLog3);
    }

    SECTION("and a file described correctly carries no override") {
        ref.transferOverride = media::TransferFunction::Unknown;
        auto text = io::saveProjectToString(f.project);
        REQUIRE(text);
        CHECK(text->find("transferOverride") == std::string::npos);
    }
}

TEST_CASE("A source grade on a media file survives a round trip", "[io][color][source]") {
    Fixture f;
    model::MediaRef& ref = f.project.mediaMutable().front();
    ref.color.exposure = 1.5;
    ref.color.saturation = 120.0;
    ref.wheels.slopeR = 1.1;
    ref.lut.path = "/luts/log_to_709.cube";
    ref.lut.amount = 0.8;
    REQUIRE(ref.isGraded());

    auto text = io::saveProjectToString(f.project);
    REQUIRE(text);
    auto reloaded = io::loadProjectFromString(*text);
    REQUIRE(reloaded);
    const model::MediaRef& back = reloaded->project.media().front();
    CHECK(back.color.exposure == 1.5);
    CHECK(back.color.saturation == 120.0);
    CHECK(back.wheels.slopeR == 1.1);
    CHECK(back.lut.path == "/luts/log_to_709.cube");
    CHECK(back.lut.amount == 0.8);
}

TEST_CASE("An ungraded media file carries no grade on disk", "[io][color][source]") {
    Fixture f;
    auto text = io::saveProjectToString(f.project);
    REQUIRE(text);
    // A bin of a thousand ungraded files should not carry a thousand copies of
    // "no correction", the same bargain the clip encoder makes.
    CHECK(text->find("\"wheels\"") == std::string::npos);
}

// --- Known bug: a reset is undone by the save that should record it ---------
//
// `mergePreserved` copies back every key the writer did not emit, and the
// writer omits a grade that is neutral. Together those mean "set it back to
// zero and save" reloads with the old numbers still on it: the omission that
// keeps ungraded projects small is indistinguishable, at merge time, from a
// field this build has never heard of.
//
// Both cases below are tagged `!shouldfail`, so the suite stays honest about
// what is broken and speaks up the day it starts working. Fixing it needs
// preservation to know which keys this version owns, which is a change to how
// every project file round-trips rather than a patch to the colour code.

TEST_CASE("Clearing a source grade actually clears it on disk",
          "[io][color][source][!shouldfail]") {
    // The writer omits a neutral grade, and unknown-field preservation merges
    // back anything the writer did not emit. Those two together are how a reset
    // could be silently undone by the save that was meant to record it.
    Fixture f;
    f.project.mediaMutable().front().color.exposure = 1.5;
    auto graded = io::saveProjectToString(f.project);
    REQUIRE(graded);
    auto loaded = io::loadProjectFromString(*graded);
    REQUIRE(loaded);
    REQUIRE(loaded->project.media().front().color.exposure == 1.5);

    // Reset it, and save with the document it was read from in hand.
    model::Project reset = loaded->project;
    reset.mediaMutable().front().color = model::ColorCorrection{};
    auto text = io::saveProjectToString(reset, loaded->unknown);
    REQUIRE(text);
    auto again = io::loadProjectFromString(*text);
    REQUIRE(again);
    CHECK(again->project.media().front().color.exposure == 0.0);
}

TEST_CASE("Clearing a CLIP grade actually clears it on disk",
          "[io][color][preserve][!shouldfail]") {
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50))));
    const model::ClipId clipId =
        f.project.findSequence(f.sequenceId)->findTrack(f.v1)->clips().front().id;
    f.project.findSequence(f.sequenceId)->findTrack(f.v1)->find(clipId)->color.exposure = 1.5;

    auto graded = io::saveProjectToString(f.project);
    REQUIRE(graded);
    auto loaded = io::loadProjectFromString(*graded);
    REQUIRE(loaded);

    model::Project reset = loaded->project;
    reset.findSequence(f.sequenceId)->findTrack(f.v1)->find(clipId)->color =
        model::ColorCorrection{};
    auto text = io::saveProjectToString(reset, loaded->unknown);
    REQUIRE(text);
    auto again = io::loadProjectFromString(*text);
    REQUIRE(again);
    CHECK(
        again->project.findSequence(f.sequenceId)->findTrack(f.v1)->find(clipId)->color.exposure ==
        0.0);
}

TEST_CASE("A sequence's delivery curve survives a round trip", "[io][tonemap]") {
    Fixture f;
    model::Sequence::Output delivery;
    delivery.transfer = media::TransferFunction::PQ;
    delivery.highlightKnee = 0.75;
    f.sequence().setOutput(delivery);

    auto text = io::saveProjectToString(f.project);
    REQUIRE(text);
    auto reloaded = io::loadProjectFromString(*text);
    REQUIRE(reloaded);
    const model::Sequence::Output& back = reloaded->project.findSequence(f.sequenceId)->output();
    CHECK(back.transfer == media::TransferFunction::PQ);
    CHECK(back.highlightKnee == 0.75);

    SECTION("and a sequence delivered the way every project always was writes nothing") {
        Fixture plain;
        auto bare = io::saveProjectToString(plain.project);
        REQUIRE(bare);
        CHECK(bare->find("\"output\"") == std::string::npos);
    }
}

// --- Portable paths ---------------------------------------------------------
//
// The thing these are all about: a project and its footage that were moved
// together should still find each other, and a project moved on its own
// should still find footage that stayed where it was.

namespace {

/// A project in `dir` with one media reference pointing at a file that really
/// exists at `mediaPath`, saved. Returns the project's path.
std::string saveProjectPointingAt(const std::filesystem::path& dir,
                                  const std::filesystem::path& mediaPath) {
    std::filesystem::create_directories(dir);
    std::filesystem::create_directories(mediaPath.parent_path());
    std::ofstream{mediaPath} << "not really a movie";

    Fixture f;
    std::vector<model::MediaRef> media = f.project.media();
    media.front().path = mediaPath.string();
    f.project.setMedia(std::move(media));

    const std::string path = (dir / "cut.cutreel").string();
    REQUIRE(io::saveProject(f.project, path));
    return path;
}

}  // namespace

TEST_CASE("A project moved with its footage still finds it", "[io][save][paths]") {
    TempDir dir;
    const std::filesystem::path was = dir.path / "was";
    const std::string project = saveProjectPointingAt(was, was / "footage" / "clip.mov");

    // The whole folder somewhere else -- another disk, another machine, a
    // different account. The absolute path in the file now names nothing.
    const std::filesystem::path now = dir.path / "now";
    std::filesystem::rename(was, now);

    auto reopened = io::loadProject((now / "cut.cutreel").string());
    REQUIRE(reopened);
    const std::filesystem::path found{reopened->project.media().front().path};
    CHECK(std::filesystem::exists(found));
    CHECK(std::filesystem::equivalent(found, now / "footage" / "clip.mov"));
}

TEST_CASE("A project moved away from its footage still finds it", "[io][save][paths]") {
    TempDir dir;
    // Footage that does not travel with the project: a shared library, a card
    // still in the reader, anything on another volume.
    const std::filesystem::path media = dir.path / "library" / "clip.mov";
    const std::filesystem::path was = dir.path / "was";
    const std::string project = saveProjectPointingAt(was, media);

    const std::filesystem::path now = dir.path / "elsewhere" / "deeper";
    std::filesystem::create_directories(now.parent_path());
    std::filesystem::rename(was, now);

    auto reopened = io::loadProject((now / "cut.cutreel").string());
    REQUIRE(reopened);
    // The relative path names nothing from here, so the absolute one wins --
    // which is the whole reason both are written.
    const std::filesystem::path found{reopened->project.media().front().path};
    CHECK(std::filesystem::exists(found));
    CHECK(std::filesystem::equivalent(found, media));
}

TEST_CASE("A copied project folder edits its own copies", "[io][save][paths]") {
    TempDir dir;
    const std::filesystem::path original = dir.path / "original";
    const std::string project = saveProjectPointingAt(original, original / "footage" / "clip.mov");

    const std::filesystem::path copy = dir.path / "copy";
    std::filesystem::copy(original, copy, std::filesystem::copy_options::recursive);

    auto reopened = io::loadProject((copy / "cut.cutreel").string());
    REQUIRE(reopened);
    // Both files exist. Reaching back into the folder this one was copied out
    // of would mean grading somebody's other project by accident.
    const std::filesystem::path found{reopened->project.media().front().path};
    CHECK(std::filesystem::equivalent(found, copy / "footage" / "clip.mov"));
}

TEST_CASE("A project file written before any of this loads as it always did", "[io][save][paths]") {
    TempDir dir;
    const std::filesystem::path media = dir.path / "footage" / "clip.mov";
    std::filesystem::create_directories(media.parent_path());
    std::ofstream{media} << "not really a movie";

    // Every project file written before the relative twin existed looks like
    // this: one path, and it is the absolute one.
    const std::string text = R"({
        "zaro": {"schemaVersion": 1},
        "media": [{"id": 1, "path": ")" +
                             std::filesystem::path{media}.generic_string() + R"("}],
        "sequences": [{"id": 2, "frameRate": "25"}]})";

    auto loaded = io::loadProjectFromString(text, dir.path.string());
    REQUIRE(loaded);
    REQUIRE(loaded->project.media().size() == 1);
    CHECK(std::filesystem::equivalent(std::filesystem::path{loaded->project.media().front().path},
                                      media));

    SECTION("and gains its twin the next time it is saved") {
        const std::string project = (dir.path / "cut.cutreel").string();
        REQUIRE(io::saveProject(loaded->project, project, loaded->unknown));
        std::ifstream file{project, std::ios::binary};
        std::ostringstream buffer;
        buffer << file.rdbuf();
        CHECK(buffer.str().find(R"("relativePath": "footage/clip.mov")") != std::string::npos);
    }
}

TEST_CASE("A relinked file does not keep the old file's relative path", "[io][save][paths]") {
    TempDir dir;
    const std::filesystem::path first = dir.path / "footage" / "one.mov";
    const std::string project = saveProjectPointingAt(dir.path, first);

    auto opened = io::loadProject(project);
    REQUIRE(opened);

    // Relinked to a file in a different folder, the way the relink dialog
    // does it: the path changes and nothing else does.
    const std::filesystem::path second = dir.path / "rushes" / "two.mov";
    std::filesystem::create_directories(second.parent_path());
    std::ofstream{second} << "also not a movie";
    std::vector<model::MediaRef> media = opened->project.media();
    media.front().path = second.string();
    opened->project.setMedia(std::move(media));

    REQUIRE(io::saveProject(opened->project, project, opened->unknown));
    auto reopened = io::loadProject(project);
    REQUIRE(reopened);
    CHECK(std::filesystem::equivalent(std::filesystem::path{reopened->project.media().front().path},
                                      second));
}

TEST_CASE("A project written without a folder to be relative to writes no twins",
          "[io][save][paths]") {
    Fixture f;
    auto text = io::saveProjectToString(f.project);
    REQUIRE(text);
    // Nothing to be relative to, so nothing is claimed. This is the shape
    // every caller that builds a project in memory gets.
    CHECK(text->find("relativePath") == std::string::npos);
}
