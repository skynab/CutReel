#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "zaro/core/edit/Operations.h"
#include "zaro/core/io/OtioIo.h"

#include "ModelFixtures.h"

using namespace zaro;
using Catch::Approx;
using zaro::testing::Fixture;

TEST_CASE("A timeline round trips through OTIO", "[io][otio]") {
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50, 500))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(100, 30, 800))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.a1), f.clip(0, 50, 500))));

    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);

    const auto back = io::readOtio(*text);
    REQUIRE(back);
    const model::Sequence* sequence = &back->sequences().front();
    CHECK(sequence->frameRate() == f.sequence().frameRate());
    REQUIRE(sequence->videoTracks().size() == 2);
    REQUIRE(sequence->audioTracks().size() == 1);

    const model::Track& video = sequence->videoTracks().front();
    REQUIRE(video.clips().size() == 2);
    // Positions come back from the gaps, since an OTIO track states neither.
    CHECK(video.clips()[0].start().frames() == 0);
    CHECK(video.clips()[0].duration().frames() == 50);
    CHECK(video.clips()[1].start().frames() == 100);
    CHECK(video.clips()[1].duration().frames() == 30);
    // And so do the source ranges, which is what says a trim survived.
    CHECK(video.clips()[0].sourceRange.start().frames() == 500);
    CHECK(video.clips()[1].sourceRange.start().frames() == 800);
}

TEST_CASE("A hole in a track is written as a Gap", "[io][otio]") {
    // An OTIO track is a sequence, not a set of placed clips: position is
    // implied by order and duration, and a hole is an object rather than an
    // absence. A writer that left them out would move every clip after the hole.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(40, 10, 500))));

    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    // Checked in the text rather than by parsing it: the JSON library is an
    // implementation detail of the io layer, and a test that reached for it
    // would be the first thing to make it part of the interface.
    const std::size_t gap = text->find("\"Gap.1\"");
    const std::size_t clip = text->find("\"Clip.1\"");
    REQUIRE(gap != std::string::npos);
    REQUIRE(clip != std::string::npos);
    CHECK(gap < clip);  // the hole comes first, or the clip lands early

    const auto back = io::readOtio(*text);
    REQUIRE(back);
    const model::Track& video = back->sequences().front().videoTracks().front();
    REQUIRE(video.clips().size() == 1);
    CHECK(video.clips()[0].start().frames() == 40);
}

TEST_CASE("Broadcast rates survive the trip through a double", "[io][otio]") {
    // OTIO writes rates as doubles: 24000/1001 becomes 23.976023976023978, and
    // reading that back as a ratio gives something nobody means. Rates within a
    // hair of a standard are snapped back to the exact rational, or a sequence
    // drifts against every other tool by a fraction of a frame per hour.
    for (const time::Rational& rate :
         {time::rates::fps23_976, time::rates::fps29_97, time::rates::fps59_94,
          time::rates::fps119_88, time::rates::fps25, time::rates::fps24}) {
        model::Project project;
        model::Sequence sequence{project.ids().next<model::SequenceTag>(), "Reel", rate};
        const auto id = sequence.id();
        const auto track = project.ids().next<model::TrackTag>();
        sequence.addTrack(track, model::TrackKind::Video, "V1");
        project.addSequence(std::move(sequence));

        const auto text = io::writeOtio(project, id);
        REQUIRE(text);
        const auto back = io::readOtio(*text);
        REQUIRE(back);
        INFO("rate " << rate.toString());
        CHECK(back->sequences().front().frameRate() == rate);
    }
}

TEST_CASE("Media references become media, once each", "[io][otio]") {
    Fixture f;
    // Two clips from the same file: one media reference, not two.
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 20, 500))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 20, 900))));

    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    const auto back = io::readOtio(*text);
    REQUIRE(back);

    REQUIRE(back->media().size() == 1);
    CHECK(back->media().front().path == "/media/long.mov");
    const model::Track& video = back->sequences().front().videoTracks().front();
    REQUIRE(video.clips().size() == 2);
    CHECK(video.clips()[0].source == video.clips()[1].source);
    CHECK(video.clips()[0].source == back->media().front().id);
}

TEST_CASE("A file URL is written and read back as a path", "[io][otio]") {
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 20, 500))));
    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    CHECK(text->find("file:///media/long.mov") != std::string::npos);
}

TEST_CASE("Muted tracks come back muted", "[io][otio]") {
    Fixture f;
    f.track(f.v1).setMuted(true);
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 20, 500))));
    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    const auto back = io::readOtio(*text);
    REQUIRE(back);
    CHECK(back->sequences().front().videoTracks().front().isMuted());
}

TEST_CASE("A generated clip says it has no media", "[io][otio]") {
    // MissingReference is OTIO's own way of saying so, and it survives a trip
    // through a tool that has never heard of a shape layer.
    Fixture f;
    model::Graphic shape;
    shape.kind = model::GraphicKind::Rectangle;
    REQUIRE(f.run(edit::makeAddGraphic(f.project, f.on(f.v1), shape, f.range(0, 20))));

    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    CHECK(text->find("MissingReference") != std::string::npos);

    const auto back = io::readOtio(*text);
    REQUIRE(back);
    const model::Track& video = back->sequences().front().videoTracks().front();
    REQUIRE(video.clips().size() == 1);
    CHECK_FALSE(video.clips()[0].source.isValid());
    CHECK(video.clips()[0].duration().frames() == 20);
}

TEST_CASE("Something that is not a timeline is refused", "[io][otio]") {
    CHECK_FALSE(io::readOtio("not json at all"));
    CHECK_FALSE(io::readOtio(R"({"OTIO_SCHEMA": "Clip.1"})"));
    CHECK_FALSE(io::readOtio(R"({"OTIO_SCHEMA": "Timeline.1"})"));
    CHECK_FALSE(io::loadOtio("/definitely/not/a/file.otio"));
}

TEST_CASE("An unknown item in a track does not move what follows it", "[io][otio]") {
    // An item this reader has no model for is skipped rather than guessed at,
    // but its duration still has to advance the cursor or everything after it
    // lands early.
    //
    // A nested Stack, which is what that now means. This was written with a
    // Transition standing in, back when one was equally unknown -- and with a
    // `source_range`, which a real OTIO transition does not have: it is a
    // marker between two items rather than an item with a duration, and it
    // consumes no time at all. Transitions are read properly now, so the case
    // this guards needs an item that genuinely is both unknown and timed.
    const std::string text = R"({
        "OTIO_SCHEMA": "Timeline.1",
        "name": "with a nested stack",
        "tracks": {
            "OTIO_SCHEMA": "Stack.1",
            "children": [{
                "OTIO_SCHEMA": "Track.1",
                "kind": "Video",
                "children": [
                    {"OTIO_SCHEMA": "Stack.1",
                     "source_range": {"OTIO_SCHEMA": "TimeRange.1",
                        "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 0.0},
                        "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 12.0}}},
                    {"OTIO_SCHEMA": "Clip.1", "name": "after",
                     "source_range": {"OTIO_SCHEMA": "TimeRange.1",
                        "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 0.0},
                        "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 8.0}}}
                ]
            }]
        }
    })";

    const auto back = io::readOtio(text);
    REQUIRE(back);
    const model::Track& video = back->sequences().front().videoTracks().front();
    REQUIRE(video.clips().size() == 1);
    CHECK(video.clips()[0].start().frames() == 12);
    CHECK(video.clips()[0].name == "after");
}

TEST_CASE("A transition round trips through OTIO", "[io][otio][transition]") {
    // The gap this closes: transitions were written nowhere and read as
    // nothing, so a cut built here and handed to another tool arrived as a
    // hard cut. That is the silent-partial failure this format's reader
    // already refuses to make elsewhere -- it skips a Stack rather than
    // guessing at one -- and a dropped dissolve is the same thing on the way
    // out.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50, 500))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 50, 500))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));

    const model::TransitionId id = f.track(f.v1).transitions().front().id;
    edit::TransitionSettings settings;
    settings.kind = model::TransitionKind::Wipe;
    settings.direction = model::TransitionDirection::Up;
    settings.softness = 0.4;
    settings.easing = model::TransitionEasing::InOut;
    REQUIRE(f.run(edit::makeSetTransitionSettings(f.project, f.on(f.v1), id, settings)));
    const auto wanted = f.track(f.v1).transitions().front().range;

    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    // OTIO's own vocabulary, so a reader that has never heard of this program
    // still finds a transition where one belongs.
    CHECK(text->find("Transition.1") != std::string::npos);
    CHECK(text->find("in_offset") != std::string::npos);

    const auto back = io::readOtio(*text);
    REQUIRE(back);
    const model::Track& video = back->sequences().front().videoTracks().front();
    REQUIRE(video.transitions().size() == 1);
    const model::Transition& span = video.transitions().front();

    // Where it sits, and what it joins. The two clips are named rather than
    // assumed: a span that came back pointing at the wrong pair would still
    // have the right length.
    CHECK(span.range.start().frames() == wanted.start().frames());
    CHECK(span.range.duration().frames() == wanted.duration().frames());
    REQUIRE(video.clips().size() == 2);
    CHECK(span.from == video.clips()[0].id);
    CHECK(span.to == video.clips()[1].id);
    CHECK(span.isCrossFade());

    // And everything OTIO has no word for, which rides in metadata.
    CHECK(span.kind == model::TransitionKind::Wipe);
    CHECK(span.direction == model::TransitionDirection::Up);
    CHECK(span.softness == Approx(0.4));
    CHECK(span.easing == model::TransitionEasing::InOut);
}

TEST_CASE("A dissolve is written in the type OTIO knows", "[io][otio][transition]") {
    // A wipe is "Custom" because OTIO names only one blend; a dissolve is the
    // one it does name, and writing it as Custom would hide the commonest
    // transition there is from every tool that reads the file.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50, 500))));
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(50, 50, 500))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(20))));

    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    CHECK(text->find("SMPTE_Dissolve") != std::string::npos);
}

TEST_CASE("A transition from a tool that knows none of our kinds is a dissolve",
          "[io][otio][transition]") {
    // No metadata of ours, so nothing to read but the offsets and the type.
    // A file from anywhere else lands as a dissolve of the right length rather
    // than being dropped -- the same forgiving rule an unknown kind gets when
    // a project is loaded.
    const std::string text = R"({
      "OTIO_SCHEMA": "Timeline.1",
      "name": "from elsewhere",
      "global_start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 0.0},
      "tracks": {"OTIO_SCHEMA": "Stack.1", "name": "tracks", "children": [
        {"OTIO_SCHEMA": "Track.1", "name": "V1", "kind": "Video", "children": [
          {"OTIO_SCHEMA": "Clip.1", "name": "a", "source_range":
            {"OTIO_SCHEMA": "TimeRange.1",
             "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 0.0},
             "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 50.0}}},
          {"OTIO_SCHEMA": "Transition.1", "name": "x", "transition_type": "SMPTE_Dissolve",
           "in_offset": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 6.0},
           "out_offset": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 4.0}},
          {"OTIO_SCHEMA": "Clip.1", "name": "b", "source_range":
            {"OTIO_SCHEMA": "TimeRange.1",
             "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 0.0},
             "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 25.0, "value": 50.0}}}
        ]}
      ]}
    })";

    const auto back = io::readOtio(text);
    REQUIRE(back);
    const model::Track& video = back->sequences().front().videoTracks().front();
    // The transition consumed no timeline time: the second clip still starts
    // at 50, not at 60. An item that advanced the cursor would shift
    // everything after it, which is the bug this shape of reader invites.
    REQUIRE(video.clips().size() == 2);
    CHECK(video.clips()[1].start().frames() == 50);

    REQUIRE(video.transitions().size() == 1);
    const model::Transition& span = video.transitions().front();
    CHECK(span.kind == model::TransitionKind::CrossDissolve);
    // Asymmetric on purpose: the two offsets are not the same, and a reader
    // that used one for both would pass a symmetric test and lose this.
    CHECK(span.range.start().frames() == 44);
    CHECK(span.range.endExclusive().frames() == 54);
}

TEST_CASE("A fade is left out rather than written as a join", "[io][otio][transition]") {
    // A fade lies inside its clip and joins it to nothing, while an OTIO
    // transition is a join between two items -- so writing one would be
    // inventing the item on the far side. Left out, and said so here, because
    // the alternative is a file that claims a join nobody made.
    Fixture f;
    REQUIRE(f.run(edit::makeOverwrite(f.project, f.on(f.v1), f.clip(0, 50, 500))));
    REQUIRE(f.run(edit::makeAddCrossDissolve(f.project, f.on(f.v1), f.at(50), f.at(10))));
    REQUIRE(f.track(f.v1).transitions().front().isFadeOut());

    const auto text = io::writeOtio(f.project, f.sequenceId);
    REQUIRE(text);
    CHECK(text->find("Transition.1") == std::string::npos);

    const auto back = io::readOtio(*text);
    REQUIRE(back);
    CHECK(back->sequences().front().videoTracks().front().transitions().empty());
}
